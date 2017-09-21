#
# Makefile for Invensense inv-mpu-iio device.
#

obj-$(CONFIG_IIO_INV_MPU6500) += gyrosensor_inv_mpu6500_iio.o

gyrosensor_inv_mpu6500_iio-objs := inv_mpu_core.o
gyrosensor_inv_mpu6500_iio-objs += inv_mpu_ring.o
gyrosensor_inv_mpu6500_iio-objs += inv_mpu_trigger.o
gyrosensor_inv_mpu6500_iio-objs += inv_mpu_misc.o
gyrosensor_inv_mpu6500_iio-objs += inv_mpu3050_iio.o
gyrosensor_inv_mpu6500_iio-objs += dmpDefaultMPU6050.o

# the Bosch BMA250 driver is added to the inv-mpu device driver because it
# must be connected to an MPU3050 device on the secondary slave bus.
ifeq ($(CONFIG_INV_IIO_MPU3050_ACCEL_SLAVE_BMA250), y)
inv-mpu-iio-objs += inv_slave_bma250.o
CFLAGS_inv_slave_bma250.o   += -Idrivers/staging/iio
endif

