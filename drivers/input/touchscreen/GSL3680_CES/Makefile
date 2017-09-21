#
# Makefile for the gslX680_CES tp driver
#

# copy prebuilt objs to output directory
$(shell mkdir -p $(objtree)/$(obj)/) 
$(shell cp -rf $(srctree)/$(src)/gsl_point_id.bin $(objtree)/$(obj)/) 

obj-$(CONFIG_TOUCHSCREEN_GSL_X680_CES)		  += ces_gslX680.o
ces_gslX680-objs := gslX680.o gsl_point_id.bin

# KO_NAME = gslX680

# PWD := $(shell pwd)

# ifeq ($(KERNELRELEASE),)
# # current directory is driver
# CFGDIR = $(PWD)/../../../../../owl/platform/config

# include $(CFGDIR)/build_config.cfg
# include $(CFGDIR)/rules.make
# endif


# obj-m += $(KO_NAME).o
# $(KO_NAME)-objs    += gslX680.o

# default:
#	$(MAKE) -C $(KERNEL_BUILD_PATH) ARCH=$(R_ARCH) CROSS_COMPILE=$(R_CROSS_COMPILE) \
#		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" M=$(PWD) modules
#	#cp $(KO_NAME).ko $(DEFAULT_MODULES_DIR)/ctp_$(KO_NAME).ko
#	#cp $(KO_NAME).ko ctp_$(KO_NAME).ko

#clean:
#	$(MAKE) -C $(KERNEL_BUILD_PATH) ARCH=$(R_ARCH) CROSS_COMPILE=$(R_CROSS_COMPILE) \
#		M=$(PWD) clean