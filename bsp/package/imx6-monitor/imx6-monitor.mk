IMX6_MONITOR_VERSION = 1.0
IMX6_MONITOR_SITE = $(TOPDIR)/../src/imx6_monitor
IMX6_MONITOR_SITE_METHOD = local
IMX6_MONITOR_DEPENDENCIES = jpeg-turbo

define IMX6_MONITOR_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define IMX6_MONITOR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/imx6-monitor $(TARGET_DIR)/usr/bin/imx6-monitor
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_BSP_PATH)/package/imx6-monitor/S90imx6-monitor \
		$(TARGET_DIR)/etc/init.d/S90imx6-monitor
endef

$(eval $(generic-package))
