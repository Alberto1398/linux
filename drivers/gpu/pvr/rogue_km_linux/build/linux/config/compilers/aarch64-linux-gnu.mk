# 64-bit arm64 compiler

TARGET_PRIMARY_ARCH := target_arm64
ifeq ($(MULTIARCH),1)
 TARGET_SECONDARY_ARCH := target_armhf
 ifeq ($(CROSS_COMPILE_SECONDARY),)
  $(error CROSS_COMPILE_SECONDARY must be set for multiarch ARM builds)
 endif
endif # MULTIARCH

