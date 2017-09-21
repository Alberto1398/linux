/*
 * GC5004 Camera Driver
 *
 * Copyright (C) 2015 Actions Semiconductor Co.,LTD
 * Zhiquan Deng <dengzhiquan@actions-semi.com>
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
		pr_err("write register %s error %d", CAMERA_MODULE_NAME, ret);
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
		pr_err("read register %s error %d", CAMERA_MODULE_NAME, ret);
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
		DBG_ERR("write register %s error %d", CAMERA_MODULE_NAME, ret);

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
			pr_err("[camera] i2c write error!,i2c address is %x\n",
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

/*
static void enter_video_mode(struct i2c_adapter *i2c_adap)
{
}
*/

static int module_soft_standby(struct i2c_client *client)
{
	/*struct i2c_adapter *i2c_adap = client->adapter;
	*unsigned int reg_0xfc = 0x01;
	*unsigned int reg_0xf2 = 0x00;*/
	int ret = 0;

	/*
	   ret = camera_i2c_read(i2c_adap, 1, 0xfc, &reg_0xfc);
	   if(ret < 0)
	   return ret;

	   reg_0xfc = reg_0xfc | 0x01;
	   ret = camera_i2c_write(i2c_adap, 1, 0xfc, &reg_0xfc);
	   ret |= camera_i2c_write(i2c_adap, 1, 0xf2, &reg_0xf2);
	 */

	return ret;
}

static int module_normal(struct i2c_client *client)
{
	/*struct i2c_adapter *i2c_adap = client->adapter;
	*unsigned int reg_0xfc = 0x00;
	*unsigned int reg_0xf2 = 0x0f;*/
	int ret = 0;

	/*
	   ret = camera_i2c_read(i2c_adap, 1, 0xfc, &reg_0xfc);
	   if(ret < 0)
	   return ret;

	   reg_0xfc = reg_0xfc & 0xfe;
	   ret = camera_i2c_write(i2c_adap, 1, 0xfc, &reg_0xfc);
	   ret |= camera_i2c_write(i2c_adap, 1, 0xf2, &reg_0xf2);
	 */

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

static int module_set_stream(struct i2c_client *client, int enable)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;

	if (!enable) {
		DBG_INFO("stream down");
		module_soft_standby(client);
		return ret;
	}

	if (NULL == priv->win || NULL == priv->cfmt) {
		DBG_ERR("cfmt or win select error");
		return -EPERM;
	}
	DBG_INFO("stream on");
	module_normal(client);
	return 0;
}

static int module_set_exposure(struct i2c_client *client, int val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int shutter = val;
	int ret = 0;

	/*printk("%s: %d\n", __func__, val); */

	if (shutter < 4)
		shutter = 4;
	if (shutter > 0x1fff)
		shutter = 0x1fff;

	ret |= camera_i2c_write(i2c_adap, 1, 0x04, shutter & 0xff);
	ret |= camera_i2c_write(i2c_adap, 1, 0x03, (shutter >> 8) & 0x1f);

	return ret;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int shutter, temp_reg1, temp_reg2;
	int ret = 0;

	ret |= camera_i2c_read(i2c_adap, 1, 0x04, &temp_reg2);
	ret |= camera_i2c_read(i2c_adap, 1, 0x03, &temp_reg1);

	shutter = temp_reg2 & 0xff;
	shutter |= (temp_reg1 & 0x1f) << 8;

	*val = shutter;
	/*printk("%s: %d\n", __func__, shutter); */

	return ret;
}

#define ANALOG_GAIN_1 64	/* 1.00x */
#define ANALOG_GAIN_2 91	/* 1.41x */
#define ANALOG_GAIN_3 128	/* 2.00x */
#define ANALOG_GAIN_4 178	/* 2.78x */
#define ANALOG_GAIN_5 247	/* 3.85x, maximum analog gain of GC5004 */
#define ANALOG_GAIN_6 332	/* 5.18x */
#define ANALOG_GAIN_7 436	/* 6.80x */

static int lev2val[] = { 100, 141, 200, 278, 385 };

static int module_get_gain(struct i2c_client *client, int *val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int gain_a, gain_d, temp_reg0, temp_reg1, temp_reg2;
	int ret = 0;

	ret |= camera_i2c_read(i2c_adap, 1, 0xb6, &temp_reg0);
	if (temp_reg0 > 4)
		temp_reg0 = 4;
	gain_a = lev2val[temp_reg0];

	ret |= camera_i2c_read(i2c_adap, 1, 0xb1, &temp_reg1);
	ret |= camera_i2c_read(i2c_adap, 1, 0xb2, &temp_reg2);
	gain_d = temp_reg1 * 64;
	gain_d += (temp_reg2 >> 2) & 0x3f;

	*val = (gain_a * gain_d + 50) / 100;
	/*printk("%s: %d, %x, %x, %x\n", __func__,
		*val, temp_reg0, temp_reg1, temp_reg2); */

	return ret;
}

static int module_set_gain(struct i2c_client *client, int val)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	int tmp;
	int ret = 0;

	/*printk("%s: %d\n", __func__, val); */

	if (val < 64)
		val = 64;

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
	} else if (ANALOG_GAIN_5 <= val /* && val < ANALOG_GAIN_6 */) {
		ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 4);
		tmp = 64 * val / ANALOG_GAIN_5;
		ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
		ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	}
	/*else if(ANALOG_GAIN_6 <= val && val < ANALOG_GAIN_7) {
	   ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 5);
	   tmp = 64 * val / ANALOG_GAIN_6;
	   ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
	   ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	   }
	   else if(ANALOG_GAIN_7 <= val) {
	   ret |= camera_i2c_write(i2c_adap, 1, 0xb6, 6);
	   tmp = 64 * val / ANALOG_GAIN_7;
	   ret |= camera_i2c_write(i2c_adap, 1, 0xb1, tmp >> 6);
	   ret |= camera_i2c_write(i2c_adap, 1, 0xb2, (tmp << 2) & 0xfc);
	   } */

	/*printk("%s: %x, %x, %x\n", __func__, tmp, tmp>>6, (tmp<<2)&0xfc); */
	/*msleep(30); */

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
	int ret = 0;

	return ret;
}

static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	return ret;
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	return 0;
}

static int module_set_color_format(int mf)
{
	/*
	   int *p = (int*)&(module_cfmts[0].code);
	   switch(mf) {
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
	   } */

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
		pr_info("[%s] Product ID verified %x\n", CAMERA_MODULE_NAME,
		       VERSION(pidh, pidl));
		break;

	default:
		pr_err("[%s] Product ID error %x\n", CAMERA_MODULE_NAME,
		       VERSION(pidh, pidl));
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
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(5);
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			mdelay(10);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
			mdelay(20);
		} else {
			/*set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			   mdelay(5);
			   set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			   mdelay(10);
			   set_gpio_level(&spinfo->gpio_front_reset, GPIO_HIGH);
			   mdelay(20); */
		}

	} else {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			mdelay(10);
		} else {
			/*set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			   mdelay(10); */
		}
	}
}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {

		if (rear) {
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(20);
		} else {
			/*set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			   mdelay(20); */
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
	} else {
		pr_err("motor read register %s error %d", CAMERA_MODULE_NAME,
		       ret);
	}

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
	if (ret > 0) {
		ret = 0;
	} else if (ret < 0) {
		pr_err("motor write register %s error %d", CAMERA_MODULE_NAME,
		       ret);
	}

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
