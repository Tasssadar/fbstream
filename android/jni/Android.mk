LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := fbstream
LOCAL_SRC_FILES := fbstream.c
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -lz -ljpeg -lcutils -llog
include $(BUILD_EXECUTABLE)
