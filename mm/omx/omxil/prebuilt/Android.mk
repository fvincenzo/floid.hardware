
ifneq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_BOARD_PLATFORM),SPEAr1340)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libhantrovideodec
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := libhantrovideodec.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libhantroimagedec
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := libhantroimagedec.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libhantro_omx_core
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := libhantro_omx_core.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libhantrovideoenc
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := libhantrovideoenc.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libhantroimageenc
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := libhantroimageenc.so
include $(BUILD_PREBUILT)

endif
endif
