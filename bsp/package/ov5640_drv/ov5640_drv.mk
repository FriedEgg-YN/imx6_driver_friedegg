OV5640_DRV_VERSION = 1.0
OV5640_DRV_SITE = $(TOPDIR)/../src/ov5640_drv
OV5640_DRV_SITE_METHOD = local

define OV5640_DRV_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) app \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define OV5640_DRV_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ov5640_test $(TARGET_DIR)/usr/bin/ov5640_test
endef

$(eval $(kernel-module))
$(eval $(generic-package))
