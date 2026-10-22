#include <mod/amlmod.h>
#include <math.h>

MYMOD(net.user.subtitlesize, SubtitleSize, 1.0, user)

static const float SCALE = 1.3f;

struct Patch { uintptr_t off; float orig; };

extern "C" void OnModLoad()
{
    uintptr_t base = aml->GetLib("libGTASA.so");
    if (!base) return;

    const Patch patches[] = {
        { 0x439230, 0.85f },
        { 0x439234, 1.02f },
        { 0x43925C, 1.22f },
    };

    for (const Patch& p : patches)
    {
        float cur = *(float*)(base + p.off);
        if (fabsf(cur - p.orig) > 0.001f) continue;

        float v = p.orig * SCALE;
        aml->Write(base + p.off, (uintptr_t)&v, sizeof(v));
    }
}
