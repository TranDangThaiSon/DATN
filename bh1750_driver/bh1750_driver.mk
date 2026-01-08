BH1750_DRIVER_VERSION = 1.0
BH1750_DRIVER_SITE = $(TOPDIR)/package/bh1750_driver
BH1750_DRIVER_SITE_METHOD = local
BH1750_DRIVER_MODULE_SUBDIRS = .

# Script tự động load driver khi boot
define BH1750_DRIVER_INSTALL_INIT_SYSV
    echo '#!/bin/sh' > $(TARGET_DIR)/etc/init.d/S98bh1750
    echo 'modprobe bh1750_driver' >> $(TARGET_DIR)/etc/init.d/S98bh1750
    chmod +x $(TARGET_DIR)/etc/init.d/S98bh1750
endef

$(eval $(kernel-module))
$(eval $(generic-package))
