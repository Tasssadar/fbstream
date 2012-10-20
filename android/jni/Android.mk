LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := fbstream
LOCAL_SRC_FILES := fbstream.cpp
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -lz -lgui -lutils -lbinder -llog -lcutils -lhardware -lhardware_legacy -lui -lEGL -lGLESv2 -lcorkscrew -lwpa_client -lgccdemangle -lnetutils -lGLES_trace -lstlport -ljpeg -lskia -lemoji -lexpat
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
    LOCAL_CFLAGS := -DHAVE_NEON=1
endif
#LOCAL_CFLAGS += -g -O0
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/skia/core \
    $(LOCAL_PATH)/skia/effects \
    $(LOCAL_PATH)/skia/images \
    $(LOCAL_PATH)/skia/utils
include $(BUILD_EXECUTABLE)


