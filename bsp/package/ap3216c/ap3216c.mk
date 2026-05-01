AP3216C_VERSION = 1.0
AP3216C_SITE = $(TOPDIR)/../src/ap3216c_drv
AP3216C_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-package))
