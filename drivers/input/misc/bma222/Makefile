#
# Makefile for the gsensor drivers
#

obj-$(CONFIG_GSENSOR_BMA222)		  += gsensor_bma222.o
gsensor_bma222-objs := bma222.o


# KO_TYPE = gsensor
# KO_NAME = bma222

# PWD := $(shell pwd)

# ifeq ($(KERNELRELEASE),)
# # current directory is driver
# CFGDIR = $(PWD)/../../../../../owl/platform/config

# include $(CFGDIR)/build_config.cfg
# include $(CFGDIR)/rules.make

# endif

# EXTRA_CFLAGS += 


# obj-m += $(KO_NAME).o

# default:
	# $(MAKE) -C $(KERNEL_BUILD_PATH) ARCH=$(R_ARCH) CROSS_COMPILE=$(R_CROSS_COMPILE) \
		# EXTRA_CFLAGS="$(EXTRA_CFLAGS)" M=$(PWD) modules
	# cp $(KO_NAME).ko $(DEFAULT_MODULES_DIR)/$(KO_TYPE)_$(KO_NAME).ko

# clean:
	# $(MAKE) -C $(KERNEL_BUILD_PATH) ARCH=$(R_ARCH) CROSS_COMPILE=$(R_CROSS_COMPILE) \
		# M=$(PWD) clean
