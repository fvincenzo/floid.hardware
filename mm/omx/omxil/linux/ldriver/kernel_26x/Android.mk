ifneq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_BOARD_PLATFORM),iris)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := hx170_load.sh
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/spear
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
export ARCH=arm
export ANDROID_ROOT=$(ANDROID_BUILD_TOP)
#export ANDROID_ROOT=$(CURDIR)
export CROSS_COMPILE=$(ANDROID_ROOT)/prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-
export KDIR=$(ANDROID_ROOT)/kernel

HX170_PATH := $(LOCAL_PATH)

module := hx170dec.ko
cleanup := $(LOCAL_PATH)/dummy

.PHONY := $(module) $(cleanup)

$(cleanup):
	$(MAKE) -C $(HX170_PATH) clean

$(LOCAL_PATH)/$(module): $(cleanup)
  #cd $(LOCAL_PATH)
	$(MAKE) -C $(HX170_PATH)
	#$(CROSS_COMPILE)strip -g -S -d  $(module)

LOCAL_MODULE :=  $(module)
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/modules
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

endif
endif
