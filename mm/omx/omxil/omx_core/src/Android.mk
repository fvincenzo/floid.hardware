LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libhantro_omx_core
LOCAL_MODULE_PATH := $(LOCAL_PATH)/../../prebuilt
LOCAL_MODULE_TAGS := optional

LOCAL_PRELINK_MODULE := false

LOCAL_SRC_FILES := \
    hantro_omx_core.c

LOCAL_STATIC_LIBRARIES := \


LOCAL_SHARED_LIBRARIES := \
    libdl \
    liblog

LOCAL_CFLAGS := \

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../inc 


include $(BUILD_SHARED_LIBRARY)
