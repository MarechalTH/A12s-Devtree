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

LOCAL_LDFLAGS := -Wl,--gc-sections -flto

LOCAL_STATIC_LIBRARIES := libpopt libuuid

LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true

define compress-sgdisk-with-upx
@echo ">>> UPX compressing $(LOCAL_INSTALLED_MODULE)"
@$(hide) upx --ultra-brute $(LOCAL_INSTALLED_MODULE)
endef

LOCAL_POST_INSTALL_CMD := $(compress-sgdisk-with-upx)

include $(BUILD_EXECUTABLE)

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

LOCAL_LDFLAGS := -Wl,--gc-sections -flto

LOCAL_STATIC_LIBRARIES := libpopt libuuid

LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true

define compress-fixparts-with-upx
@echo ">>> UPX compressing $(LOCAL_INSTALLED_MODULE)"
@$(hide) upx --ultra-brute $(LOCAL_INSTALLED_MODULE)
endef

LOCAL_POST_INSTALL_CMD := $(compress-fixparts-with-upx)

include $(BUILD_EXECUTABLE)
