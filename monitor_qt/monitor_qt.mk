################################################################################
#
# monitor_qt
#
################################################################################

MONITOR_QT_VERSION = 1.0
MONITOR_QT_SITE = package/monitor_qt
MONITOR_QT_SITE_METHOD = local

# Khai báo các thư viện phụ thuộc để Buildroot build chúng trước
MONITOR_QT_DEPENDENCIES = qt5base libcurl tensorflow-lite

# Bước 1: Cấu hình (Chạy qmake)
define MONITOR_QT_CONFIGURE_CMDS
    (cd $(@D); $(QT5_QMAKE) monitor_app.pro)
endef

# Bước 2: Build (Chạy make)
define MONITOR_QT_BUILD_CMDS
    $(MAKE) -C $(@D)
endef

# Bước 3: Cài đặt vào Target (Copy file chạy vào /usr/bin)
define MONITOR_QT_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/monitor_app_qt $(TARGET_DIR)/usr/bin/monitor_app_qt
endef

$(eval $(generic-package))
