PRINT_CHASING_LED_VERSION = 1.0
PRINT_CHASING_LED_SITE = $(TOPDIR)/../src/print_chasing_LED
PRINT_CHASING_LED_SITE_METHOD = local

define PRINT_CHASING_LED_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) app \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define PRINT_CHASING_LED_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/print_chasing_LED_test $(TARGET_DIR)/usr/bin/print_chasing_LED_test
endef

$(eval $(kernel-module))
$(eval $(generic-package))
