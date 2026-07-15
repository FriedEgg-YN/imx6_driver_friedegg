LD2410C_VERSION = 1.0
LD2410C_SITE = $(TOPDIR)/../src/ld2410c
LD2410C_SITE_METHOD = local
LD2410C_INSTALL_STAGING = YES

define LD2410C_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) app \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define LD2410C_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ld2410c_attach $(TARGET_DIR)/usr/bin/ld2410c_attach
	$(INSTALL) -D -m 0755 $(@D)/ld2410c_rawdump $(TARGET_DIR)/usr/bin/ld2410c_rawdump
endef

define LD2410C_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/include/friedegg/ld2410c.h $(STAGING_DIR)/usr/include/friedegg/ld2410c.h
endef

$(eval $(kernel-module))
$(eval $(generic-package))
