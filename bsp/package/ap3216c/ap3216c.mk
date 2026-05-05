AP3216C_VERSION = 1.0
AP3216C_SITE = $(TOPDIR)/../src/ap3216c_drv
AP3216C_SITE_METHOD = local

define AP3216C_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) app \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define AP3216C_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ap3216cApp $(TARGET_DIR)/usr/bin/ap3216cApp
endef

$(eval $(kernel-module))
$(eval $(generic-package))
