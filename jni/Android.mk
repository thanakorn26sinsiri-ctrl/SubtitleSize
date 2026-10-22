LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := SubtitleSize
LOCAL_SRC_FILES := SubtitleSize.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS += -O2 -std=c++17
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
