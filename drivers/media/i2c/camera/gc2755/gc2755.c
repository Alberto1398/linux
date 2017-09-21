/*
 * gc2145 Camera Driver
 *
 * Copyright (C) 2011 Actions Semiconductor Co.,LTD
 * Wang Xin <wangxin@actions-semi.com>
 *
 * Based on ov227x driver
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <media/v4l2-chip-ident.h>
#include <linux/platform_device.h>
#include "module_diff.h"
#include "../module_comm/module_comm.c"
#include "../module_comm/module_detect.c"

static int camera_i2c_read(struct i2c_adapter *i2c_adap,
			   unsigned int data_width, unsigned int reg,
			   unsigned int *dest)
{
	unsigned char regs_array[4] = { 0, 0, 0, 0 };
	unsigned char data_array[4] = { 0, 0, 0, 0 };
	struct i2c_msg msg;
	int ret = 0;

	if (I2C_REGS_WIDTH == 1)
		regs_array[0] = reg & 0xff;
	if (I2C_REGS_WIDTH == 2) {
		regs_array[0] = (reg >> 8) & 0xff;
		regs_array[1] = reg & 0xff;
	}
	msg.addr = MODULE_I2C_REAL_ADDRESS;
	msg.flags = 0;
	msg.len = I2C_REGS_WIDTH;
	msg.buf = regs_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret < 0) {
		pr_info("read register %s error %d", CAMERA_MODULE_NAME, ret);
		return ret;
	}

	msg.flags = I2C_M_RD;
	msg.len = data_width;
	msg.buf = data_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret >= 0) {
		ret = 0;
		if (data_width == 1)
			*dest = data_array[0];
		if (data_width == 2)
			*dest = data_array[0] << 8 | data_array[1];
	} else
		pr_info("read register %s error %d", CAMERA_MODULE_NAME, ret);

	return ret;
}

static int camera_i2c_write(struct i2c_adapter *i2c_adap,
			    unsigned int data_width, unsigned int reg,
			    unsigned int data)
{
	unsigned char regs_array[4] = { 0, 0, 0, 0 };
	unsigned char data_array[4] = { 0, 0, 0, 0 };
	unsigned char tran_array[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	struct i2c_msg msg;
	int ret, i;

	if (I2C_REGS_WIDTH == 1)
		regs_array[0] = reg & 0xff;
	if (I2C_REGS_WIDTH == 2) {
		regs_array[0] = (reg >> 8) & 0xff;
		regs_array[1] = reg & 0xff;
	}
	if (data_width == 1)
		data_array[0] = data & 0xff;
	if (data_width == 2) {
		data_array[0] = data & 0xff;
		data_array[1] = (data >> 8) & 0xff;
	}
	for (i = 0; i < I2C_REGS_WIDTH; i++)
		tran_array[i] = regs_array[i];

	for (i = I2C_REGS_WIDTH; i < (I2C_REGS_WIDTH + data_width); i++)
		tran_array[i] = data_array[i - I2C_REGS_WIDTH];

	msg.addr = MODULE_I2C_REAL_ADDRESS;
	msg.flags = 0;
	msg.len = I2C_REGS_WIDTH + data_width;
	msg.buf = tran_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret > 0)
		ret = 0;
	else if (ret < 0)
		pr_info("write register %s error %d", CAMERA_MODULE_NAME, ret);

	return ret;
}

static int camera_write_array(struct i2c_adapter *i2c_adap,
			      const struct regval_list *vals)
{
	while (vals->reg_num != 0xff) {
		int ret = camera_i2c_write(i2c_adap,
					   vals->data_width,
					   vals->reg_num,
					   vals->value);
		if (ret < 0) {
			pr_info("[camera] i2c write error!,i2c address is %x\n",
				MODULE_I2C_REAL_ADDRESS);
			return ret;
		}
		vals++;
	}
	return 0;
}

static int module_soft_reset(struct i2c_client *client)
{
	int ret = 0;
	unsigned int reg_0xfe;
	struct i2c_adapter *i2c_adap = client->adapter;
	DBG_INFO("");
	ret = camera_i2c_read(i2c_adap, 1, 0xfe, &reg_0xfe);
	reg_0xfe |= (0x1 << 7);
	ret |= camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
	mdelay(10);

	reg_0xfe &= (~(0x1 << 7));
	ret |= camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
	return ret;
}

static void update_after_init(struct i2c_adapter *i2c_adap)
{

}

static void enter_preview_mode(struct i2c_adapter *i2c_adap)
{

}

static void enter_capture_mode(struct i2c_adapter *i2c_adap)
{

}

static int module_start_aec(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_freeze_aec(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_save_exposure_param(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_set_exposure_param(struct v4l2_subdev *sd)
{
	int ret = 0;
	return ret;
}

static int module_set_auto_white_balance(struct v4l2_subdev *sd,
					 struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_white_balance_temperature(struct v4l2_subdev *sd,
						struct v4l2_ctrl *ctrl)
{
	unsigned int ret = 0;
	return ret;
}

static int module_set_exposure_auto(struct v4l2_subdev *sd,
				    struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_scene_exposure(struct v4l2_subdev *sd,
				     struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	return ret;
}

static int module_set_stream(struct i2c_client *client, int enable)
{
	struct camera_module_priv *priv = to_camera_priv(client);

	int ret = 0;

	if (!enable) {
		DBG_INFO("stream down");
		return ret;
	}

	if (NULL == priv->win || NULL == priv->cfmt) {
		DBG_ERR("cfmt or win select error");
		return -EPERM;
	}
	DBG_INFO("stream on");
	return 0;
}

/* It would take about 2 frames of time
 * before the register values set are making sense! */

static int module_set_exposure(struct i2c_client *client, int val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int shutter;
	int ret = 0;
	shutter = val;
	ret |= camera_i2c_write(i2c_adap, 1, 0x03, (shutter >> 8) & 0xff);
	ret |= camera_i2c_write(i2c_adap, 1, 0x04, shutter & 0xff);
	return ret;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int shutter, tmp;
	int ret = 0;

	ret |= camera_i2c_read(i2c_adap, 1, 0x03, &tmp);
	shutter = (tmp & 0xff) << 8;
	ret |= camera_i2c_read(i2c_adap, 1, 0x04, &tmp);
	shutter |= tmp & 0xff;

	*val = shutter;

	return ret;
}

#define ANALOG_GAIN_1 64	/* 1.00x */
#define ANALOG_GAIN_2 86	/* 1.35x */
#define ANALOG_GAIN_3 117	/* 1.83x */
#define ANALOG_GAIN_4 159	/* 2.48x */
#define ANALOG_GAIN_5 223	/* 3.49x */
#define ANALOG_GAIN_6 307	/* 4.80x */
#define ANALOG_GAIN_7 428	/* 6.68x */
#define MAX_GAIN      (64*32)

static int reg2app[] = { 64, 86, 117, 159, 223, 307, 428 };

static int module_get_gain(struct i2c_client *client, int *val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int gain_a, gain_d, tmp, tmp1, tmp2;
	int ret = 0;

	ret |= camera_i2c_read(i2c_adap, 1, 0xb6, &tmp);
	if (tmp > 6)
		tmp = 6;
	gain_a = reg2app[tmp];

	ret |= camera_i2c_read(i2c_adap, 1, 0xb1, &tmp1);
	gain_d = tmp1 * 64;
	ret |= camera_i2c_read(i2c_adap, 1, 0xb2, &tmp2);
	gain_d += (tmp2 >> 2) & 0x3f;

	*val = (gain_a * gain_d + 32) / 64;

	return ret;
}

static int module_set_gain(struct i2c_client *client, int val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int tmp;
	int ret = 0;

	if (val < 64)
		val = 64;
	if (val > MAX_GAIN)
		val = MAX_GAIN;

	if (val < ANALOG_GAIN_2) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 0);
		tmp = 64 * val / ANALOG_GAIN_1;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	} else if (ANALOG_GAIN_2 <= val && val < ANALOG_GAIN_3) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 1);
		tmp = 64 * val / ANALOG_GAIN_2;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	} else if (ANALOG_GAIN_3 <= val && val < ANALOG_GAIN_4) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 2);
		tmp = 64 * val / ANALOG_GAIN_3;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	} else if (ANALOG_GAIN_4 <= val && val < ANALOG_GAIN_5) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 3);
		tmp = 64 * val / ANALOG_GAIN_4;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	} else if (ANALOG_GAIN_5 <= val && val < ANALOG_GAIN_6) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 4);
		tmp = 64 * val / ANALOG_GAIN_5;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	} else if (ANALOG_GAIN_6 <= val && val < ANALOG_GAIN_7) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 5);
		tmp = 64 * val / ANALOG_GAIN_6;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	} else if (ANALOG_GAIN_7 <= val) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 6);
		tmp = 64 * val / ANALOG_GAIN_7;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	}

	return ret;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	return ret;
}

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_mbusformat(struct i2c_client *client,
				 const struct module_color_format *cfmt)
{
	int ret = 0;
	/*
	   struct i2c_adapter *i2c_adap = client->adapter;
	   enum v4l2_mbus_pixelcode code;
	   unsigned char reg_0x84;
	   unsigned char reg_0xfe = 0x00;  //pgae0

	   code = cfmt->code;

	   switch (code) {
	   case V4L2_MBUS_FMT_YUYV8_2X8:
	   reg_0x84 = 0x02;
	   break;

	   case V4L2_MBUS_FMT_UYVY8_2X8:
	   reg_0x84 = 0x00;
	   break;

	   case V4L2_MBUS_FMT_YVYU8_2X8:
	   reg_0x84 = 0x03;
	   break;

	   case V4L2_MBUS_FMT_VYUY8_2X8:
	   reg_0x84 = 0x01;
	   break;

	   default:
	   printk("[gc2155] mbus code error in %s() line %d\n",
	   __FUNCTION__, __LINE__);
	   return -1;
	   }

	   ret = camera_i2c_write(i2c_adap, 0xfe, reg_0xfe);
	   ret |= camera_i2c_write(i2c_adap, 0x84, reg_0x84);
	 */
	return ret;
}

static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *i2c_adap = client->adapter;
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;
	unsigned int reg17;
	unsigned int reg_0xfe = 0x00;

	DBG_INFO("");
	camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
	camera_i2c_read(i2c_adap, 1, 0x17, &reg17);
	reg17 &= ~0x03;		/* [0]:mirror, [1]: vertical flip */

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			priv->flip_flag |= 0x02;
		else
			priv->flip_flag &= ~0x02;
		break;
	case MODULE_FLAG_VFLIP:
		if (ctrl->val)
			priv->flip_flag |= 0x01;
		else
			priv->flip_flag &= ~0x01;
		break;
	default:
		DBG_ERR("set flip out of range\n");
		return -ERANGE;
	}
	camera_i2c_write(i2c_adap, 1, 0x17, reg17);

	return ret;
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	switch (mf) {
	case 0:
		/* no flip,no mirror */
		camera_i2c_write(client->adapter, 1, 0x17, 0x14);
		break;
	case 1:
		/* no flip,horizontal mirror */
		camera_i2c_write(client->adapter, 1, 0x17, 0x15);
		break;
	case 2:
		/* vertical flip,no mirror */
		camera_i2c_write(client->adapter, 1, 0x17, 0x16);
		break;
	case 3:
		/* vertical flip,horizontal mirror */
		camera_i2c_write(client->adapter, 1, 0x17, 0x17);
		break;
	default:
		camera_i2c_write(client->adapter, 1, 0x17, 0x14);
		break;
	}

	return 0;
}

static int module_set_color_format(int mf)
{
	int *p = (int *)&(module_cfmts[0].code);
	switch (mf) {
	case 0:
		*p = V4L2_MBUS_FMT_SGBRG10_1X10;
		break;
	case 1:
		*p = V4L2_MBUS_FMT_SBGGR10_1X10;
		break;
	case 2:
		*p = V4L2_MBUS_FMT_SRGGB10_1X10;
		break;
	case 3:
		*p = V4L2_MBUS_FMT_SGRBG10_1X10;
		break;
	default:
		*p = V4L2_MBUS_FMT_SGBRG10_1X10;
		break;
	}

	return 0;
}

static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv)
{
	unsigned int pidh = 0;
	unsigned int pidl = 0;
	int ret;

	DBG_INFO("");
	/*open sensor i2c */
	camera_i2c_write(i2c_adap, 1, 0xf3, 0x0);
	mdelay(10);

	/*
	 * check and show product ID and manufacturer ID
	 */
	ret = camera_i2c_read(i2c_adap, 1, PIDH, &pidh);
	ret |= camera_i2c_read(i2c_adap, 1, PIDL, &pidl);

	switch (VERSION(pidh, pidl)) {
	case CAMERA_MODULE_PID:
		pr_info("[%s] Product ID verified %x\n",
			CAMERA_MODULE_NAME, VERSION(pidh, pidl));
		break;
	default:
		pr_info("[%s] Product ID error %x\n",
			CAMERA_MODULE_NAME, VERSION(pidh, pidl));
		return -ENODEV;
	}
	return ret;
}

static int module_set_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	ctrl->val = NONE_AF;
	return ret;
}

static int module_get_af_status(struct camera_module_priv *priv,
				struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {

		if (rear) {
			/*
			   set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			   mdelay(5);
			   set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			   mdelay(10);
			   set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
			   mdelay(30);
			 */
		} else {
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			mdelay(5);
			set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			mdelay(10);
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_HIGH);
			mdelay(20);
		}

	} else {
		if (rear) {
			/*set_gpio_level(&spinfo->gpio_rear, GPIO_LOW); */
			mdelay(20);
		} else {
			set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			mdelay(20);
		}
	}
}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {

		if (rear) {
			/*
			   set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			 */
		} else {
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			msleep(20);
		}
	} else {
		if (rear)
			;/*set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH); */
		else {
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			msleep(20);
		}
	}
}

static int get_sensor_id(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	ctrl->val = CAMERA_MODULE_PID;
	return ret;
}

static int module_set_power_line(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_power_line(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{

	ctrl->val = V4L2_CID_POWER_LINE_FREQUENCY_AUTO;
	return 0;
}
