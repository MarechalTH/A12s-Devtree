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
    xxhash.c
    bench.c \
    lorem.c

LOCAL_CFLAGS := -Os -flto -ffunction-sections -fdata-sections \
    -DXXH_NAMESPACE=LZ4_ -DLZ4IO_MULTITHREAD

LOCAL_LDFLAGS := -static -flto -Wl,--gc-sections -s -ffunction-sections -fdata-sections

LOCAL_LDLIBS := -lpthread

LOCAL_MODULE_FILENAME := lz4.bin

LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin

include $(BUILD_EXECUTABLE)

lz4_bin := $(LOCAL_MODULE_PATH)/$(LOCAL_MODULE_FILENAME)
$(lz4_bin): $(LOCAL_BUILT_MODULE)
	@echo ">>> UPX compressing $@"
	$(hide) upx --ultra-brute $@
