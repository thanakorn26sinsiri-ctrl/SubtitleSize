// AML mod "SubtitleSize": configurable font size for subtitles / menus / HUD,
// plus subtitle up/down offset. Settings are read from configs/SubtitleSize.ini
// Offsets are for libGTASA.so of GTASA_2_00_AML1_4_0_FLA7_6_Unprot.apk (armeabi-v7a) only.
#include <mod/amlmod.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <math.h>

MYMOD(net.user.subtitlesize, SubtitleSize, 2.1, user)

enum { CAT_NONE = 0, CAT_SUB, CAT_MENU, CAT_HUD };

static uintptr_t g_base = 0;
static float g_subScale = 1.3f;
static float g_subOffsetY = 0.0f;
static float g_menuScale = 1.0f;
static float g_hudScale = 1.0f;

static char g_soDir[512] = "";     // folder of this .so (.../files)
static char g_parentDir[512] = ""; // parent of that folder

// folders where the log is written (first one that works wins; all are tried)
static const char* const kLogDirs[] = {
    "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files",
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files",
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa",
};

// ---------------------------------------------------------------- helpers
// mkdir -p
static void MkdirP(const char* path)
{
    char tmp[640];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; ++p)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void Log(const char* fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    const char* dirs[1 + sizeof(kLogDirs) / sizeof(kLogDirs[0])];
    int nd = 0;
    dirs[nd++] = g_soDir;
    for (size_t i = 0; i < sizeof(kLogDirs) / sizeof(kLogDirs[0]); i++) dirs[nd++] = kLogDirs[i];

    for (int i = 0; i < nd; i++)
    {
        if (!dirs[i][0]) continue;
        char p[640];
        snprintf(p, sizeof(p), "%s/SubtitleSize.log", dirs[i]);
        FILE* f = fopen(p, "a");
        if (f) { fprintf(f, "%s\n", msg); fclose(f); }
    }
}

static void (*SetScale_orig)(float) = nullptr;
static void (*PrintString_orig)(float, float, unsigned short*) = nullptr;

// ---------------------------------------------------------------- config
static void SetKey(const char* key, float v)
{
    if (v < 0.1f && strstr(key, ".Scale")) v = 0.1f;
    if (v > 5.0f && strstr(key, ".Scale")) v = 5.0f;
    if (!strcmp(key, "Subtitles.Scale")) g_subScale = v;
    else if (!strcmp(key, "Subtitles.OffsetY")) g_subOffsetY = v;
    else if (!strcmp(key, "Menu.Scale")) g_menuScale = v;
    else if (!strcmp(key, "Hud.Scale")) g_hudScale = v;
}

static bool ReadIni(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[256], section[64] = "";
    while (fgets(line, sizeof(line), f))
    {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == 0 || *p == '\n' || *p == '\r' || *p == ';' || *p == '#') continue;
        if (*p == '[')
        {
            char* e = strchr(p, ']');
            if (e) { *e = 0; snprintf(section, sizeof(section), "%s", p + 1); }
            continue;
        }
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = p + strlen(p);
        while (k > p && (k[-1] == ' ' || k[-1] == '\t')) *--k = 0;
        char full[128];
        snprintf(full, sizeof(full), "%s.%s", section, p);
        SetKey(full, (float)atof(eq + 1));
    }
    fclose(f);
    return true;
}

static bool WriteDefaultIni(const char* path)
{
    // fopen() does not create folders -> create ".../configs" first
    char dir[640];
    snprintf(dir, sizeof(dir), "%s", path);
    char* slash = strrchr(dir, '/');
    if (slash) { *slash = 0; MkdirP(dir); }

    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f,
        "[Subtitles]\n"
        "; font size multiplier (1.0 = original game size)\n"
        "Scale = 1.3\n"
        "; vertical offset in percent of screen height (+ = up, - = down)\n"
        "OffsetY = 0.0\n"
        "\n"
        "[Menu]\n"
        "; font size multiplier for pause menu, settings and other menu screens\n"
        "Scale = 1.0\n"
        "\n"
        "[Hud]\n"
        "; font size multiplier for on-screen HUD text (area name, mission text, widgets)\n"
        "Scale = 1.0\n");
    fclose(f);
    return true;
}

extern "C" void OnModLoad();

static void LoadConfig()
{
    char cand[5][640];
    int n = 0;
    snprintf(cand[n++], sizeof(cand[0]), "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/configs/SubtitleSize.ini");
    if (g_parentDir[0]) snprintf(cand[n++], sizeof(cand[0]), "%s/configs/SubtitleSize.ini", g_parentDir);
    snprintf(cand[n++], sizeof(cand[0]), "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/configs/SubtitleSize.ini");
    snprintf(cand[n++], sizeof(cand[0]), "/storage/emulated/0/Android/data/com.rockstargames.gtasa/configs/SubtitleSize.ini");

    for (int i = 0; i < n; i++)
        if (ReadIni(cand[i])) { Log("read ini: %s", cand[i]); return; }

    for (int i = 0; i < n; i++)
    {
        if (WriteDefaultIni(cand[i])) { Log("created ini: %s", cand[i]); return; }
        Log("cannot write %s (errno %d)", cand[i], errno);
    }
}

// ---------------------------------------------------------------- caller classification
static int Classify(const char* n)
{
    if (strstr(n, "DrawSubtitles")) return CAT_SUB;
    if (strstr(n, "MenuScreen") || strstr(n, "CMenuManager") || strstr(n, "CMenuSystem") ||
        strstr(n, "FlowScreen") || strstr(n, "StatsScreen") || strstr(n, "KeyboardControlsScreen") ||
        strstr(n, "Menu_Map"))
        return CAT_MENU;
    if (strstr(n, "CHud") || strstr(n, "CWidget") || strstr(n, "CAdjustableHUD") ||
        strstr(n, "CDarkel") || strstr(n, "CGarages") || strstr(n, "CPickups") || strstr(n, "CSpecialFX"))
        return CAT_HUD;
    return CAT_NONE;
}

struct CacheEnt { uintptr_t key; int cat; };
static CacheEnt g_cache[1024];

static int Category(uintptr_t lr)
{
    lr &= ~(uintptr_t)1;
    CacheEnt& e = g_cache[(lr >> 1) & 1023];
    if (e.key == lr) return e.cat;
    int cat = CAT_NONE;
    Dl_info di;
    if (dladdr((void*)lr, &di) && di.dli_sname) cat = Classify(di.dli_sname);
    e.cat = cat;
    e.key = lr;
    return cat;
}

// ---------------------------------------------------------------- hooks
static void SetScale_hook(float s)
{
    float m = 1.0f;
    switch (Category((uintptr_t)__builtin_return_address(0)))
    {
        case CAT_SUB:  m = g_subScale;  break;
        case CAT_MENU: m = g_menuScale; break;
        case CAT_HUD:  m = g_hudScale;  break;
        default: break;
    }
    SetScale_orig(s * m);
}

static void PrintString_hook(float x, float y, unsigned short* text)
{
    if (g_subOffsetY != 0.0f && Category((uintptr_t)__builtin_return_address(0)) == CAT_SUB)
    {
        int h = *(int*)(g_base + 0x9FC8FC + 8); // RsGlobal.maximumHeight
        if (h > 0) y -= g_subOffsetY * 0.01f * (float)h;
    }
    PrintString_orig(x, y, text);
}

static void HookGot(uintptr_t slot, void* hook, void** orig)
{
    *orig = *(void**)slot;
    uintptr_t page = slot & ~(uintptr_t)0xFFF;
    mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE);
    *(void**)slot = hook;
    mprotect((void*)page, 0x1000, PROT_READ);
}

extern "C" void OnModLoad()
{
    Dl_info di;
    const char* fname = "";
    if (dladdr((void*)&OnModLoad, &di) && di.dli_fname) fname = di.dli_fname;
    snprintf(g_soDir, sizeof(g_soDir), "%s", fname);
    char* s1 = strrchr(g_soDir, '/');
    if (s1) {
        *s1 = 0;
        snprintf(g_parentDir, sizeof(g_parentDir), "%s", g_soDir);
        char* s2 = strrchr(g_parentDir, '/');
        if (s2) *s2 = 0; else g_parentDir[0] = 0;
    } else {
        g_soDir[0] = 0;
    }
    Log("OnModLoad v2.1 so=%s", fname);

    g_base = aml->GetLib("libGTASA.so");
    Log("libGTASA base=%p", (void*)g_base);
    if (!g_base) return;

    LoadConfig();

    // version check: subtitle scale table of libGTASA.so 2.00 (0.85f, 1.02f)
    float a = *(float*)(g_base + 0x439230);
    float b = *(float*)(g_base + 0x439234);
    if (fabsf(a - 0.85f) > 0.001f || fabsf(b - 1.02f) > 0.001f)
    {
        Log("version check failed: %f %f (hooks not installed)", a, b);
        return;
    }

    HookGot(g_base + 0x670954, (void*)&SetScale_hook, (void**)&SetScale_orig);     // CFont::SetScale
    HookGot(g_base + 0x6737C4, (void*)&PrintString_hook, (void**)&PrintString_orig); // CFont::PrintString
    Log("hooks installed; sub=%.2f offY=%.2f menu=%.2f hud=%.2f", g_subScale, g_subOffsetY, g_menuScale, g_hudScale);
}
