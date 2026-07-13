IMX6_SMART_MONITOR_VERSION = 1.0
IMX6_SMART_MONITOR_SITE = $(TOPDIR)/../src/imx6_smart_monitor
IMX6_SMART_MONITOR_SITE_METHOD = local
IMX6_SMART_MONITOR_LICENSE = MIT

define IMX6_SMART_MONITOR_INSTALL_TARGET_CMDS
	for app in \
		imx6-smart-monitor \
		imx6-sm-touch-test \
		imx6-sm-ap3216c-test \
		imx6-sm-ld2410-test \
		imx6-sm-camera-test \
		imx6-sm-storage-test \
		imx6-sm-core-test; do \
		$(INSTALL) -D -m 0755 $(@D)/bin/$$app $(TARGET_DIR)/usr/bin/$$app; \
	done
endef

$(eval $(qmake-package))
