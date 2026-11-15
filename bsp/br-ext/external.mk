# Upstream sources are the pinned submodules, not Buildroot downloads.
LAB_THIRD_PARTY := $(realpath $(BR2_EXTERNAL_LAB_PATH)/../../third_party)

include $(sort $(wildcard $(BR2_EXTERNAL_LAB_PATH)/package/*/*.mk))
