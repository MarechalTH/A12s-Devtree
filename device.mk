LOCAL_PATH := device/samsung/a12s

# Fastbootd
PRODUCT_PACKAGES += \
    android.hardware.fastboot@1.1-impl-mock \
    android.hardware.fastboot@1.0-impl-mock.recovery \
    fastbootd

# My own binaries
PRODUCT_PACKAGES += \
    snake \
    sgdisk \
    fixparts \
    lz4

PRODUCT_PRODUCT_PROPERTIES += \
	ro.fastbootd.available=true    

# Props
PRODUCT_PROPERTY_OVERRIDES +=\
    ro.boot.dynamic_partitions=true

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(DEVICE_PATH)
