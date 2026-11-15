################################################################################
#
# optee-client-lab: libteec + tee-supplicant from third_party/optee_client
#
################################################################################

OPTEE_CLIENT_LAB_VERSION = 4.11.0
OPTEE_CLIENT_LAB_SITE = $(LAB_THIRD_PARTY)/optee_client
OPTEE_CLIENT_LAB_SITE_METHOD = local
OPTEE_CLIENT_LAB_LICENSE = BSD-2-Clause
OPTEE_CLIENT_LAB_LICENSE_FILES = LICENSE
OPTEE_CLIENT_LAB_INSTALL_STAGING = YES
OPTEE_CLIENT_LAB_DEPENDENCIES = host-pkgconf util-linux-libs

# No systemd/udev in this rootfs: the init script below sets device
# permissions instead.
OPTEE_CLIENT_LAB_CONF_OPTS = \
	-DCFG_ENABLE_SYSTEMD=OFF -DCFG_ENABLE_UDEV=OFF \
	-DCFG_TEE_FS_PARENT_PATH=/var/lib/tee

# Least privilege: tee-supplicant does not run as root. It gets /dev/teepriv0
# through group teepriv; clients get /dev/tee0 through group tee.
define OPTEE_CLIENT_LAB_USERS
	teesuppl -1 teesuppl -1 * /var/lib/tee - teepriv tee-supplicant daemon
	- -1 tee -1 - - - - TEE client users
endef

define OPTEE_CLIENT_LAB_INSTALL_INIT_SYSV
	$(INSTALL) -m 0755 -D $(OPTEE_CLIENT_LAB_PKGDIR)/S30optee \
		$(TARGET_DIR)/etc/init.d/S30optee
endef

$(eval $(cmake-package))
