LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := snake
LOCAL_SRC_FILES := snake.c
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional

# Forzar binario estático y aplicar flags personalizados
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_CFLAGS := -static -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -s

# Hook post-compilación para comprimir con UPX
miutil_compressed := $(LOCAL_MODULE_PATH)/$(LOCAL_MODULE)
$(miutil_compressed): $(LOCAL_BUILT_MODULE)
	@echo ">>> Compressing $(LOCAL_BUILT_MODULE) with UPX..."
	$(hide) upx --ultra-brute $(LOCAL_BUILT_MODULE)

include $(BUILD_EXECUTABLE)
