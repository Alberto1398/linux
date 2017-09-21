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
		DBG_ERR("write register %s error %d", CAMERA_MODULE_NAME, ret);
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
	} else {
		DBG_ERR("read register %s error %d", CAMERA_MODULE_NAME, ret);
	}

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
		data_array[0] = (data >> 8) & 0xff;
		data_array[1] = data & 0xff;
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
		DBG_ERR("write register %s error %d", CAMERA_MODULE_NAME, ret);

	return ret;
}

static int camera_write_array(struct i2c_adapter *i2c_adap,
			      const struct regval_list *vals)
{
	while (vals->reg_num != 0xff) {
		if (vals->reg_num == 0xfffe) {
			DBG_INFO("delay %d\n", vals->value);
			mdelay(vals->value);
			vals++;
		} else {
			int ret = camera_i2c_write(i2c_adap,
						   vals->data_width,
						   vals->reg_num,
						   vals->value);
			if (ret < 0) {
				DBG_ERR
				    ("i2c write error!,i2c address is %x\n",
				     MODULE_I2C_REAL_ADDRESS);
				return ret;
			}
			vals++;
		}
	}

	return 0;
}

static int module_soft_reset(struct i2c_client *client)
{
	int ret = 0;
	return ret;
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

static int module_set_exposure_auto(struct v4l2_subdev *sd,
				    struct v4l2_ctrl *ctrl)
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
	int ret = 0;
	return ret;
}

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	unsigned int ret = 0;
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
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;

	if (!enable) {
		DBG_INFO("stream down");
		camera_i2c_write(i2c_adap, 1, 0x0100, 0x00);
		return ret;
	}

	if (NULL == priv->win || NULL == priv->cfmt) {
		DBG_ERR("cfmt or win select error");
		return -EPERM;
	}
	DBG_INFO("stream on");
	camera_i2c_write(i2c_adap, 1, 0x0100, 0x01);

	return 0;
}

static int module_set_exposure(struct i2c_client *client, int val)
{
	int ret = 0;
	return ret;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	int ret = 0;
	DBG_INFO("val = 0x%04x", val);
	return ret;
}

static int module_get_gain(struct i2c_client *client, int *val)
{
	int ret = 0;
	DBG_INFO("val = 0x%04x", val);
	return ret;
}

static int module_set_gain(struct i2c_client *client, int val)
{
	int ret = 0;
	return ret;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_mbusformat(struct i2c_client *client,
				 const struct module_color_format *cfmt)
{
	return 0;
}

static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv)
{
	unsigned int pidh = 0;
	unsigned int pidl = 0;
	unsigned int PID = 0;
	int ret;

	DBG_INFO("");
	DBG_INFO("in the module_verify_pid\n");
	camera_i2c_write(i2c_adap, 1, 0xfe, 0x00);

	/*
	 * check and show product ID and manufacturer ID
	 */
	ret = camera_i2c_read(i2c_adap, 1, PIDH, &pidh);
	ret = camera_i2c_read(i2c_adap, 1, PIDL, &pidl);

	PID = pidh << 8 | pidl;
	DBG_INFO("in the module_verify_pid 0x%x\n", PID);
	switch (PID) {
	case CAMERA_MODULE_PID:
		DBG_INFO("[%s] Product ID verified %x\n", CAMERA_MODULE_NAME,
			 pidh);
		break;

	default:
		DBG_INFO("[%s] Product ID error %x\n", CAMERA_MODULE_NAME,
			 pidh);
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

static void update_after_init(struct i2c_adapter *i2c_adap)
{
}

static void enter_preview_mode(struct i2c_adapter *i2c_adap)
{
}

static void enter_capture_mode(struct i2c_adapter *i2c_adap)
{
}

void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {
		if (rear) {
			DBG_INFO("Cold power on gc0312 as rear camera.\n");
		} else {
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			mdelay(2);
			set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			mdelay(2);
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_HIGH);
		}
	} else {
		if (rear) {
			DBG_INFO("Soft power on gc0312 as rear camera.\n");
		} else {
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			mdelay(2);
			set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
		}
	}
}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {
		if (rear) {
			DBG_INFO("Cold power off gc0312 as rear camera.\n");
		} else {
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			mdelay(2);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
		}
	} else {
		if (rear)
			DBG_INFO("Soft power off gc0312 as rear camera.\n");
		else
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
	}
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	return 0;
}

static int module_set_color_format(int mf)
{
	return 0;
}

static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
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

static int get_sensor_id(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	ctrl->val = CAMERA_MODULE_PID;
	return ret;
}
