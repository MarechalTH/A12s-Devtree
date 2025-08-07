LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := snake
LOCAL_SRC_FILES := snake.c
LOCAL_MODULE_TAGS := optional

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_CFLAGS := \
    -Os \
    -flto \
    -ffunction-sections \
    -fdata-sections \
    -s

LOCAL_LDFLAGS := \
    -Wl,--gc-sections

define compress-with-upx
@echo ">>> Comprimiendo $(LOCAL_INSTALLED_MODULE) con UPX..."
@# Ocultamos el comando para una salida más limpia
$(hide) upx --ultra-brute $(LOCAL_INSTALLED_MODULE)
endef

LOCAL_POST_INSTALL_CMD := $(compress-with-upx)

include $(BUILD_EXECUTABLE)
