/*
 * imx219 Camera Driver
 *
 * Copyright (C) 2014 Actions Semiconductor Co.,LTD
 * Kevin Deng <dengzhiquan@actions-semi.com>
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
#include "../host_comm/owl_device.h"
#include "../module_comm/module_comm.c"
#include "../module_comm/module_detect.c"

void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			mdelay(5);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
			mdelay(20);
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(10);
		} else {
			/*set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			   mdelay(5);
			   set_gpio_level(&spinfo->gpio_front_reset, GPIO_HIGH);
			   mdelay(20);
			   set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			   mdelay(10);
			 */
		}

	} else {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(10);
		} else {
			/*set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			   mdelay(10); */
		}
	}
}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
		} else {
			/* set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_front, GPIO_LOW); */
		}
	} else {
		if (rear)
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
		/*        else
		   set_gpio_level(&spinfo->gpio_front, GPIO_LOW); */
	}
}

static int motor_i2c_read(struct i2c_adapter *i2c_adap, unsigned int *data)
{
	struct i2c_msg msg;
	int ret = 0;
	unsigned char buf[2] = { 0, 0 };

	msg.addr = MOTOR_I2C_REAL_ADDRESS;
	msg.flags = I2C_M_RD;
	msg.len = 2;
	msg.buf = buf;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret >= 0) {
		ret = 0;
		*data = buf[0] << 8 | buf[1];
	} else
		pr_info("motor read register %s error %d",
			CAMERA_MODULE_NAME, ret);

	return ret;
}

static int motor_i2c_write(struct i2c_adapter *i2c_adap, unsigned int data)
{
	struct i2c_msg msg;
	int ret;
	unsigned char buf[2] = { 0, 0 };

	buf[0] = (data >> 8) & 0xff;
	buf[1] = data & 0xff;

	msg.addr = MOTOR_I2C_REAL_ADDRESS;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret > 0)
		ret = 0;
	else if (ret < 0)
		pr_info("motor write register %s error %d",
			CAMERA_MODULE_NAME, ret);

	return ret;
}

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
		pr_info("write register %s error %d", CAMERA_MODULE_NAME, ret);
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

static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv)
{
	unsigned int pidh = 0;
	unsigned int pidl = 0;
	int ret;

	/*
	 * check and show product ID and manufacturer ID
	 */
	ret = camera_i2c_read(i2c_adap, 1, PIDH, &pidh);
	ret |= camera_i2c_read(i2c_adap, 1, PIDL, &pidl);

	switch (VERSION(pidh, pidl)) {
	case CAMERA_MODULE_PID:
		/*              if(priv)
		   {
		   priv->model= V4L2_IDENT_GC2145;
		   } */
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

static void update_after_init(struct i2c_adapter *i2c_adap)
{

}

static void enter_preview_mode(struct i2c_adapter *i2c_adap)
{

}

static void enter_capture_mode(struct i2c_adapter *i2c_adap)
{

}

static int module_set_mbusformat(struct i2c_client *client,
				 const struct module_color_format *cfmt)
{
	return 0;
}

static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;
	unsigned int reg = 0;
	DBG_INFO("");
	ret |= camera_i2c_read(i2c_adap, 1, 0x0172, &reg);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			reg |= 0x1;
		else
			reg &= ~0x1;

		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			reg |= 0x2;
		else
			reg &= ~0x2;
		break;
	default:
		DBG_ERR("set flip out of range\n");
		return -ERANGE;
	}
	ret |= camera_i2c_write(i2c_adap, 1, 0x0172, reg);
	return ret;
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	switch (mf) {
	case 0:
		/* no flip,no mirror */
		camera_i2c_write(client->adapter, 1, 0x0172, 0x00);
		break;
	case 1:
		/* no flip,horizontal mirror */
		camera_i2c_write(client->adapter, 1, 0x0172, 0x01);
		break;
	case 2:
		/* vertical flip,no mirror */
		camera_i2c_write(client->adapter, 1, 0x0172, 0x02);
		break;
	case 3:
		/* vertical flip,horizontal mirror */
		camera_i2c_write(client->adapter, 1, 0x0172, 0x03);
		break;
	default:
		camera_i2c_write(client->adapter, 1, 0x0172, 0x03);
		break;
	}

	return 0;
}

static int module_set_color_format(int mf)
{
	int *p = (int *)&(module_cfmts[0].code);
	switch (mf) {
	case 0:
		*p = V4L2_MBUS_FMT_SRGGB10_1X10;
		break;
	case 1:
		*p = V4L2_MBUS_FMT_SGRBG10_1X10;
		break;
	case 2:
		*p = V4L2_MBUS_FMT_SGBRG10_1X10;
		break;
	case 3:
		*p = V4L2_MBUS_FMT_SBGGR10_1X10;
		break;
	default:
		*p = V4L2_MBUS_FMT_SBGGR10_1X10;
		break;
	}

	return 0;
}

static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_soft_reset(struct i2c_client *client)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;

	DBG_INFO("");

	ret |= camera_i2c_write(i2c_adap, 1, 0x0103, 0x01);
	mdelay(10);
	ret |= camera_i2c_write(i2c_adap, 1, 0x0103, 0x00);
	mdelay(10);

	return ret;
}

static int module_enter_standby(struct i2c_client *client)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;

	ret |= camera_i2c_write(i2c_adap, 1, 0x0100, 0x00);

	return ret;
}

static int module_exit_standby(struct i2c_client *client)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;

	ret |= camera_i2c_write(i2c_adap, 1, 0x0100, 0x01);

	return ret;
}

static int module_set_stream(struct i2c_client *client, int enable)
{
	struct camera_module_priv *priv = to_camera_priv(client);

	DBG_INFO("");

	if (!enable) {
		DBG_INFO("stream down");
		module_enter_standby(client);
		return 0;
	}

	if (NULL == priv->win || NULL == priv->cfmt) {
		DBG_ERR("cfmt or win select error");
		return -EPERM;
	}

	DBG_INFO("stream on");

	module_exit_standby(client);
	return 0;
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
	unsigned int ret = 0;
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
	int ret = 0;
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

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	unsigned int shutter, tmp;
	int ret = 0;

	ret |= camera_i2c_read(i2c_adap, 1, 0x015a, &tmp);
	shutter = (tmp & 0xff) << 8;
	ret |= camera_i2c_read(i2c_adap, 1, 0x015b, &tmp);
	shutter |= tmp & 0xff;

	*val = (int)shutter;

	return ret;
}

static int module_set_exposure(struct i2c_client *client, int val)
{
	struct i2c_adapter *i2c_adap = client->adapter;

	unsigned int shutter;
	int ret = 0;

	shutter = val;
	ret |= camera_i2c_write(i2c_adap, 1, 0x015a, (shutter >> 8) & 0xff);
	ret |= camera_i2c_write(i2c_adap, 1, 0x015b, shutter & 0xff);

	return ret;
}

#define ANALOG_GAIN_1 64	/* 1.000x, 0 */
#define ANALOG_GAIN_2 86	/* 1.344x, 65 */
#define ANALOG_GAIN_3 116	/* 1.813x, 115 */
#define ANALOG_GAIN_4 157	/* 2.453x, 152 */
#define ANALOG_GAIN_5 212	/* 3.313x, 179 */
#define ANALOG_GAIN_6 286	/* 4.469x, 199 */
#define ANALOG_GAIN_7 386	/* 6.031x, 214 */
#define ANALOG_GAIN_8 521	/* 8.141x, 225 */
#define ANALOG_GAIN_9 682	/* 10.66x, 232 */
#define MAX_GAIN     (1024*2)	/*16x */

static int gain_reg2app(int reg_val)
{
	int app_val;

	switch (reg_val) {
	case 0:
		app_val = 64;
		break;
	case 65:
		app_val = 86;
		break;
	case 115:
		app_val = 116;
		break;
	case 152:
		app_val = 157;
		break;
	case 179:
		app_val = 212;
		break;
	case 199:
		app_val = 286;
		break;
	case 214:
		app_val = 386;
		break;
	case 225:
		app_val = 521;
		break;
	case 232:
		app_val = 682;
		break;
	default:
		app_val = 64;
		break;
	}

	return app_val;
}

static int module_get_gain(struct i2c_client *client, int *val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int gain_a, gain_d, tmp;
	int ret = 0;

	ret |= camera_i2c_read(i2c_adap, 1, 0x0158, &tmp);
	gain_d = tmp * 256;
	ret |= camera_i2c_read(i2c_adap, 1, 0x0159, &tmp);
	gain_d += tmp;

	ret |= camera_i2c_read(i2c_adap, 1, 0x0157, &tmp);
	gain_a = gain_reg2app(tmp);
	*val = (gain_a * gain_d + 128) / 256;
	return 0;
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
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 0);
		tmp = 256 * val / ANALOG_GAIN_1;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_2 <= val && val < ANALOG_GAIN_3) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 65);
		tmp = 256 * val / ANALOG_GAIN_2;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_3 <= val && val < ANALOG_GAIN_4) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 115);
		tmp = 256 * val / ANALOG_GAIN_3;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_4 <= val && val < ANALOG_GAIN_5) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 152);
		tmp = 256 * val / ANALOG_GAIN_4;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_5 <= val && val < ANALOG_GAIN_6) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 179);
		tmp = 256 * val / ANALOG_GAIN_5;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_6 <= val && val < ANALOG_GAIN_7) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 199);
		tmp = 256 * val / ANALOG_GAIN_6;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_7 <= val && val < ANALOG_GAIN_8) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 214);
		tmp = 256 * val / ANALOG_GAIN_7;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_8 <= val && val < ANALOG_GAIN_9) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 225);
		tmp = 256 * val / ANALOG_GAIN_8;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	} else if (ANALOG_GAIN_9 <= val) {
		ret |= camera_i2c_write(i2c_adap, 1, 0x0157, 232);
		tmp = 256 * val / ANALOG_GAIN_9;
		ret |= camera_i2c_write(i2c_adap, 1, 0x0158, tmp >> 8);
		ret |= camera_i2c_write(i2c_adap, 1, 0x0159, tmp & 0xff);
	}
	return 0;
}

static int module_set_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_get_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	ctrl->val = CONTINUE_AF | SINGLE_AF;
	return ret;
}

static int module_get_af_status(struct camera_module_priv *priv,
				struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int motor_init(struct i2c_client *client)
{
	int ret = 0;
	struct i2c_adapter *i2c_adap = client->adapter;

	ret = motor_i2c_write(i2c_adap, LINEAR_MODE_PROTECTION_OFF);
	ret |= motor_i2c_write(i2c_adap, LINEAR_MODE_DLC_DISABLE);
	ret |= motor_i2c_write(i2c_adap, LINEAR_MODE_T_SRC_SETTING);
	ret |= motor_i2c_write(i2c_adap, LINEAR_MODE_PROTECTION_ON);

	return ret;
}

static int motor_set_pos(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	unsigned int data = 0;

	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *i2c_adap = client->adapter;

	DBG_INFO("set motor pos: %d\n", ctrl->val);

	data = ctrl->val & 0x3ff;
	data = data << 4;
	ret = motor_i2c_write(i2c_adap, data);

	return ret;
}

static int motor_get_pos(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	unsigned int data = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *i2c_adap = client->adapter;

	ret = motor_i2c_read(i2c_adap, &data);

	ctrl->val = (data >> 4) & 0x3ff;
	DBG_INFO("get motor pos: %d\n", ctrl->val);
	return ret;
}

static int motor_get_max_pos(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	ctrl->val = 0x3ff;
	DBG_INFO("get motor max pos: %d\n", ctrl->val);
	return ret;
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
