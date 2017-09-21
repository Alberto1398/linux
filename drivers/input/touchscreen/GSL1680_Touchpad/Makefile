#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.

$(shell mkdir -p $(objtree)/$(obj)/) 
$(shell cp -rf $(srctree)/$(src)/gsl_point_id_64_20150205.bin $(objtree)/$(obj)/) 

obj-$(CONFIG_TOUCHPAD_GSL_X680) += ctp_gslX680_touchpad.o
ctp_gslX680_touchpad-objs := gslX680.o gsl_point_id_64_20150205.bin
#ctp_gslX680_touchpad-objs := gslX680.o 
