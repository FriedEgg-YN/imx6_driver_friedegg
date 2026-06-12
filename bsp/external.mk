include $(BR2_EXTERNAL_BSP_PATH)/toolchain.mk
include $(sort $(wildcard $(BR2_EXTERNAL_BSP_PATH)/package/*/*.mk))
