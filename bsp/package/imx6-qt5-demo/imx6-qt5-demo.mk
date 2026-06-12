IMX6_QT5_DEMO_VERSION = 1.0
IMX6_QT5_DEMO_SITE = $(TOPDIR)/../src/imx6_qt5_demo
IMX6_QT5_DEMO_SITE_METHOD = local
IMX6_QT5_DEMO_LICENSE = MIT

define IMX6_QT5_DEMO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/imx6-qt5-demo $(TARGET_DIR)/usr/bin/imx6-qt5-demo
endef

$(eval $(qmake-package))
