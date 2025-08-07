LOCAL_PATH := device/samsung/a12s

# Fastbootd
PRODUCT_PACKAGES += \
    android.hardware.fastboot@1.1-impl-mock \
    android.hardware.fastboot@1.0-impl-mock.recovery \
    fastbootd

PRODUCT_PRODUCT_PROPERTIES += \
	ro.fastbootd.available=true \
    persist.sys.usb.config=mtp

# Props
PRODUCT_PROPERTY_OVERRIDES +=\
    ro.boot.dynamic_partitions=true

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(DEVICE_PATH)
