################################################################################
#
# optee-test-lab: xtest + test TAs from third_party/optee_test
#
################################################################################

OPTEE_TEST_LAB_VERSION = 4.11.0
OPTEE_TEST_LAB_SITE = $(LAB_THIRD_PARTY)/optee_test
OPTEE_TEST_LAB_SITE_METHOD = local
OPTEE_TEST_LAB_LICENSE = BSD-2-Clause, GPL-2.0
OPTEE_TEST_LAB_LICENSE_FILES = LICENSE.md
OPTEE_TEST_LAB_DEPENDENCIES = optee-client-lab openssl

# The TA dev kit comes from the separate optee_os build (bsp/Makefile passes
# it on the Buildroot command line). xtest needs its host_include/ too.
OPTEE_TEST_LAB_SDK = $(LAB_TA_DEV_KIT_DIR)
OPTEE_TEST_LAB_CONF_OPTS = -DOPTEE_TEST_SDK=$(OPTEE_TEST_LAB_SDK)

# TAs are not Linux programs: they are built with the TA dev kit's own
# makefiles (TA_DEV_KIT_DIR), linked as position-independent ELF, and signed
# with the dev kit's test key, so OP-TEE accepts them when loading.
# The cross compiler is the same toolchain; LDFLAGS is cleared by the TA makefile.
# -f Makefile.gmake: Buildroot runs CMake in the source tree, and CMake
# overwrites ta/Makefile with a generated one; optee_test ships the original
# GNU make file under this second name for exactly this case.
define OPTEE_TEST_LAB_BUILD_TAS
	@test -d "$(OPTEE_TEST_LAB_SDK)" || \
		{ echo "LAB_TA_DEV_KIT_DIR not set or missing: build optee-os first"; exit 1; }
	$(MAKE) -C $(@D)/ta -f Makefile.gmake O=$(@D)/out \
		CROSS_COMPILE=$(TARGET_CROSS) TA_DEV_KIT_DIR=$(OPTEE_TEST_LAB_SDK) \
		PYTHON3=python3 all
endef
OPTEE_TEST_LAB_POST_BUILD_HOOKS += OPTEE_TEST_LAB_BUILD_TAS

# OP-TEE looks for TAs in /lib/optee_armtz/<uuid>.ta (via tee-supplicant).
define OPTEE_TEST_LAB_INSTALL_TAS
	$(INSTALL) -d $(TARGET_DIR)/lib/optee_armtz
	$(INSTALL) -m 0444 -t $(TARGET_DIR)/lib/optee_armtz \
		$(@D)/out/ta/*/*.ta
endef
OPTEE_TEST_LAB_POST_INSTALL_TARGET_HOOKS += OPTEE_TEST_LAB_INSTALL_TAS

$(eval $(cmake-package))
