GT9147_VERSION = 1.0
GT9147_SITE = $(TOPDIR)/../src/gt9147
GT9147_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-package))
