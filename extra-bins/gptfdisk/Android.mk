LOCAL_PATH := $(call my-dir)

# ======================
# SGDISK
# ======================
include $(CLEAR_VARS)

LOCAL_MODULE := sgdisk
LOCAL_SRC_FILES := \
    sgdisk.cc \
    gptcl.cc \
    gptpart.cc \
    gpt.cc \
    support.cc \
    diskio.cc \
    diskio-unix.cc \
    basicmbr.cc \
    mbr.cc \
    bsd.cc \
    mbrpart.cc \
    attributes.cc \
    crc32.cc \
    parttypes.cc \
    guid.cc

LOCAL_CFLAGS := -std=c++11 -Os -flto -ffunction-sections -fdata-sections \
    -Wall -Wextra -Wshadow -Wcast-qual -Wcast-align -Wswitch-enum \
    -Wdeclaration-after-statement -Wstrict-prototypes -Wundef \
    -Wpointer-arith -Wstrict-aliasing=1

LOCAL_LDFLAGS := -static -flto -Wl,--gc-sections -s -ffunction-sections -fdata-sections
LOCAL_LDLIBS := -lpthread -lpopt -luuid

LOCAL_MODULE_FILENAME := sgdisk.bin
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin

include $(BUILD_EXECUTABLE)

sgdisk_bin := $(LOCAL_MODULE_PATH)/$(LOCAL_MODULE_FILENAME)
$(sgdisk_bin): $(LOCAL_BUILT_MODULE)
	@echo ">>> UPX compressing $@"
	$(hide) upx --ultra-brute $@

# ======================
# FIXPARTS
# ======================
include $(CLEAR_VARS)

LOCAL_MODULE := fixparts
LOCAL_SRC_FILES := \
    fixparts.cc \
    support.cc \
    diskio.cc \
    basicmbr.cc \
    mbrpart.cc \
    mbr.cc \
    diskio-unix.cc \
    attributes.cc \
    crc32.cc \
    parttypes.cc \
    guid.cc \
    bsd.cc

LOCAL_CFLAGS := -std=c++11 -Os -flto -ffunction-sections -fdata-sections \
    -Wall -Wextra -Wshadow -Wcast-qual -Wcast-align -Wswitch-enum \
    -Wdeclaration-after-statement -Wstrict-prototypes -Wundef \
    -Wpointer-arith -Wstrict-aliasing=1

LOCAL_LDFLAGS := -static -flto -Wl,--gc-sections -s -ffunction-sections -fdata-sections
LOCAL_LDLIBS := -lpthread -lpopt -luuid

LOCAL_MODULE_FILENAME := fixparts.bin
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin

include $(BUILD_EXECUTABLE)

fixparts_bin := $(LOCAL_MODULE_PATH)/$(LOCAL_MODULE_FILENAME)
$(fixparts_bin): $(LOCAL_BUILT_MODULE)
	@echo ">>> UPX compressing $@"
	$(hide) upx --ultra-brute $@
