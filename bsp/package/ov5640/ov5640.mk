OV5640_VERSION = 1.0
OV5640_SITE = $(TOPDIR)/../src/ov5640
OV5640_SITE_METHOD = local

define OV5640_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) app \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define OV5640_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ov5640_test $(TARGET_DIR)/usr/bin/ov5640_test
	$(INSTALL) -D -m 0755 $(@D)/ov5640_interface_demo $(TARGET_DIR)/usr/bin/ov5640_interface_demo
	$(INSTALL) -D -m 0755 $(@D)/reinsmod.sh $(TARGET_DIR)/usr/bin/reinsmod.sh
	$(INSTALL) -D -m 0755 $(@D)/test_v4l2_matrix.sh $(TARGET_DIR)/usr/bin/test_v4l2_matrix.sh
endef

$(eval $(kernel-module))
$(eval $(generic-package))
