################################################################################
#
# dht11_driver
#
################################################################################

DHT11_DRIVER_VERSION = 1.0
DHT11_DRIVER_SITE = $(TOPDIR)/package/dht11_driver
DHT11_DRIVER_SITE_METHOD = local

# Đây là dòng quan trọng để Buildroot biết đây là Kernel Module
DHT11_DRIVER_MODULE_SUBDIRS = .

$(eval $(kernel-module))
$(eval $(generic-package))
