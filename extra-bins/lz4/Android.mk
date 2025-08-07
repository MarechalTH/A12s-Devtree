LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := lz4
LOCAL_SRC_FILES := \
    lz4.c \
    lz4cli.c \
    lz4file.c \
    lz4frame.c \
    lz4hc.c \
    lz4io.c \
    threadpool.c \
    timefn.c \
    util.c \
    xxhash.c \
    bench.c \
    lorem.c

LOCAL_CFLAGS := -Os -flto -ffunction-sections -fdata-sections \
    -DXXH_NAMESPACE=LZ4_ -DLZ4IO_MULTITHREAD

LOCAL_LDFLAGS := -flto -Wl,--gc-sections

LOCAL_MODULE_FILENAME := lz4.bin

LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true

define compress-lz4-with-upx
@echo ">>> UPX compressing $(LOCAL_INSTALLED_MODULE)"
@$(hide) upx --ultra-brute $(LOCAL_INSTALLED_MODULE)
endef

LOCAL_POST_INSTALL_CMD := $(compress-lz4-with-upx)

include $(BUILD_EXECUTABLE)
