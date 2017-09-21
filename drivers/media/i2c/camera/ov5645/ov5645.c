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
		pr_info("write register %s error %d", CAMERA_MODULE_NAME, ret);
		return ret;
	}

	msg.flags = I2C_M_RD;
	msg.len = data_width;
	msg.buf = data_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret >= 0) {
		ret = 0;
		*dest = data_array[0];
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
	int ret;
	unsigned int reg_0xfe;
	struct i2c_adapter *i2c_adap = client->adapter;
	DBG_INFO("");
	ret = camera_i2c_read(i2c_adap, 1, 0x3008, &reg_0xfe);
	reg_0xfe |= (0x1 << 7);
	ret |= camera_i2c_write(i2c_adap, 1, 0x3008, reg_0xfe);
	mdelay(1);
	reg_0xfe &= (~(0x1 << 7));
	reg_0xfe &= (~(0x1 << 6));
	ret |= camera_i2c_write(i2c_adap, 1, 0x3008, reg_0xfe);
	return ret;
}

static int module_start_aec(struct v4l2_subdev *sd)
{
	int ret = 0;
	/*
	   struct i2c_client *client = v4l2_get_subdevdata(sd);
	   struct camera_module_priv *priv = to_camera_priv(client);
	   struct i2c_adapter *i2c_adap = client->adapter;

	   unsigned int reg_0xfe = 0x00;
	   unsigned int reg_0xb6 = 0x01;

	   ret = camera_i2c_write(i2c_adap, 0xfe, reg_0xfe); //page 0
	   ret |= camera_i2c_write(i2c_adap, 0xb6, reg_0xb6);
	 */

	return ret;
}

static int module_freeze_aec(struct v4l2_subdev *sd)
{
	int ret = 0;
	/*
	   struct i2c_client *client = v4l2_get_subdevdata(sd);
	   struct camera_module_priv *priv = to_camera_priv(client);
	   struct i2c_adapter *i2c_adap = client->adapter;

	   unsigned int reg_0xfe = 0x00;
	   unsigned int reg_0xb6 = 0x00;

	   ret = camera_i2c_write(i2c_adap, 0xfe, reg_0xfe); //page 0
	   ret |= camera_i2c_write(i2c_adap, 0xb6, reg_0xb6);
	 */

	return ret;
}

static int module_save_exposure_param(struct v4l2_subdev *sd)
{
	int ret = 0;
	/*
	   struct i2c_client *client = v4l2_get_subdevdata(sd);
	   struct camera_module_priv *priv = to_camera_priv(client);
	   struct i2c_adapter *i2c_adap = client->adapter;

	   unsigned int reg_0xfe = 0x00;
	   unsigned int reg_0x03;
	   unsigned int reg_0x04;

	   ret = camera_i2c_write(i2c_adap, 0xfe, reg_0xfe); //page 0
	   ret |= camera_i2c_read(i2c_adap, 0x03, &reg_0x03);
	   ret |= camera_i2c_read(i2c_adap, 0x04, &reg_0x04);

	   priv->preview_exposure_param.shutter = (reg_0x03 << 8) | reg_0x04;
	   priv->capture_exposure_param.shutter =
	   (priv->preview_exposure_param.shutter)/2;

	   printk("GC2155 module_save_exposure_param, win->name:%s\n",
	   priv->win->name);
	 */
	return ret;
}

static int module_set_exposure_param(struct v4l2_subdev *sd)
{
	int ret = 0;
	/*
	   struct i2c_client *client = v4l2_get_subdevdata(sd);
	   struct camera_module_priv *priv = to_camera_priv(client);
	   struct i2c_adapter *i2c_adap = client->adapter;

	   unsigned int reg_0xfe = 0x00;
	   unsigned char reg_0x03;
	   unsigned char reg_0x04;

	   if(priv->capture_exposure_param.shutter < 1) {
	   priv->capture_exposure_param.shutter = 1;
	   }

	   reg_0x03 = ((priv->capture_exposure_param.shutter)>>8) & 0x1F ;
	   reg_0x04 = (priv->capture_exposure_param.shutter) & 0xFF;

	   ret = camera_i2c_write(i2c_adap, 0xfe, reg_0xfe); //page 0
	   ret |= camera_i2c_write(i2c_adap, 0x03, reg_0x03);
	   ret |= camera_i2c_write(i2c_adap, 0x04, reg_0x04);
	 */
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
	/*
	   struct i2c_client *client = v4l2_get_subdevdata(sd);
	   struct camera_module_priv *priv = to_camera_priv(client);
	   struct i2c_adapter *i2c_adap = client->adapter;
	   int exposure_auto;

	   if (ctrl)
	   exposure_auto = ctrl->val;
	   else
	   exposure_auto = V4L2_EXPOSURE_AUTO;

	   if (exposure_auto < 0 || exposure_auto > 1)
	   return -ERANGE;

	   switch (exposure_auto) {
	   case V4L2_EXPOSURE_AUTO:
	   ret = camera_write_array(i2c_adap, module_scene_auto_regs);
	   break;
	   case V4L2_EXPOSURE_MANUAL: // non auto
	   ret = 0;
	   break;
	   }

	   priv->exposure_auto = exposure_auto;
	   if(ctrl)
	   ctrl->cur.val = exposure_auto;
	 */
	return ret;
}

static int module_set_scene_exposure(struct v4l2_subdev *sd,
				     struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

/* bool download_fw = true; */

static int sensor_download_af_fw(struct i2c_client *client)
{
	int i, ret, length, address;
	struct i2c_adapter *i2c_adap = client->adapter;

	/*
	   reset sensor MCU
	 */
	ret = camera_i2c_write(i2c_adap, 1, 0x3000, 0x20);
	camera_i2c_write(i2c_adap, 1, 0x3004, 0xef);
	if (ret < 0) {
		DBG_ERR("reset sensor MCU error\n");
		return ret;
	}
	/*
	   download af fw
	 */
	length = sizeof(sensor_af_fw_regs) / sizeof(int);
	address = 0x8000;
	for (i = 0; i < length; i++) {
		camera_i2c_write(i2c_adap, 1, address, sensor_af_fw_regs[i]);
		address++;
	}

	camera_i2c_write(i2c_adap, 1, 0x3022, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x3023, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x3024, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x3025, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x3026, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x3027, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x3028, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x3029, 0x7f);
	camera_i2c_write(i2c_adap, 1, 0x3000, 0x00);	/* Enable MCU */

	/* check the registers */
	camera_i2c_read(i2c_adap, 1, 0x3000, &ret);
	pr_info("0x3000 = 0x%x\n", ret);
	camera_i2c_read(i2c_adap, 1, 0x3004, &ret);
	pr_info("0x3004 = 0x%x\n", ret);
	camera_i2c_read(i2c_adap, 1, 0x3001, &ret);
	pr_info("0x3001 = 0x%x\n", ret);
	camera_i2c_read(i2c_adap, 1, 0x3005, &ret);
	pr_info("0x3005 = 0x%x\n", ret);

	return 0;
}

static int module_set_stream(struct i2c_client *client, int enable)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;

	DBG_INFO("");
	if (!enable) {
		DBG_INFO("stream down");
		camera_i2c_write(i2c_adap, 1, 0x4202, 0x0f);
		return ret;
	}

	if (NULL == priv->win || NULL == priv->cfmt) {
		DBG_ERR("cfmt or win select error");
		return -EPERM;
	}
	DBG_INFO("stream on");
	if (download_fw) {
		pr_info("=====sensor_download_af_fw start=====\n");
		sensor_download_af_fw(client);
		pr_info("=====sensor_download_af_fw end=====\n");
		download_fw = false;
	}

	camera_i2c_write(i2c_adap, 1, 0x4202, 0x00);
	return 0;
}

static int module_set_exposure(struct i2c_client *client, int val)
{
	return 0;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	int ret = 0;

	return ret;
}

static int module_get_gain(struct i2c_client *client, int *val)
{
	unsigned int total_gain = 0;

	return total_gain;
}

static int module_set_gain(struct i2c_client *client, int val)
{
	return 0;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	switch (ctrl->val) {
	case 3:
		camera_write_array(client->adapter, module_exp_comp_pos3_regs);
		break;
	case 2:
		camera_write_array(client->adapter, module_exp_comp_pos2_regs);
		break;
	case 1:
		camera_write_array(client->adapter, module_exp_comp_pos1_regs);
		break;
	case 0:
		camera_write_array(client->adapter, module_exp_comp_zero_regs);
		break;
	case -1:
		camera_write_array(client->adapter, module_exp_comp_neg1_regs);
		break;
	case -2:
		camera_write_array(client->adapter, module_exp_comp_neg2_regs);
		break;
	case -3:
		camera_write_array(client->adapter, module_exp_comp_neg3_regs);
		break;
	default:
		break;
	}

	return ret;
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
	int ret = 0;
	return ret;
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	int tmp;
	switch (mf) {
	case 0:
		/* no flip,no mirror */
		camera_i2c_read(client->adapter, 1, 0x3820, &tmp);
		tmp = tmp & 0xf9;	/*flip off */
		camera_i2c_write(client->adapter, 1, 0x3820, tmp);
		camera_i2c_read(client->adapter, 1, 0x3821, &tmp);
		tmp = tmp & 0xf9;	/*mirror off */
		camera_i2c_write(client->adapter, 1, 0x3821, tmp);
		break;
	case 1:
		/* no flip,horizontal mirror */
		camera_i2c_read(client->adapter, 1, 0x3820, &tmp);
		tmp = tmp & 0xf9;	/*flip off */
		camera_i2c_write(client->adapter, 1, 0x3820, tmp);
		camera_i2c_read(client->adapter, 1, 0x3821, &tmp);
		tmp = tmp | 0x06;	/*mirror on */
		camera_i2c_write(client->adapter, 1, 0x3821, tmp);
		break;
	case 2:
		/* vertical flip,no mirror */
		camera_i2c_read(client->adapter, 1, 0x3820, &tmp);
		tmp = tmp | 0x06;	/*flip on */
		camera_i2c_write(client->adapter, 1, 0x3820, tmp);
		camera_i2c_read(client->adapter, 1, 0x3821, &tmp);
		tmp = tmp & 0xf9;	/*mirror off */
		camera_i2c_write(client->adapter, 1, 0x3821, tmp);
		break;
	case 3:
		/* vertical flip,horizontal mirror */
		camera_i2c_read(client->adapter, 1, 0x3820, &tmp);
		tmp = tmp | 0x06;	/*flip on */
		camera_i2c_write(client->adapter, 1, 0x3820, tmp);
		camera_i2c_read(client->adapter, 1, 0x3821, &tmp);
		tmp = tmp | 0x06;	/*mirror on */
		camera_i2c_write(client->adapter, 1, 0x3821, tmp);
		break;
	default:
		camera_i2c_read(client->adapter, 1, 0x3820, &tmp);
		tmp = tmp & 0xf9;	/*flip off */
		camera_i2c_write(client->adapter, 1, 0x3820, tmp);
		camera_i2c_read(client->adapter, 1, 0x3821, &tmp);
		tmp = tmp & 0xf9;	/*mirror off */
		camera_i2c_write(client->adapter, 1, 0x3821, tmp);
		break;
	}

	return 0;
}

static int module_set_color_format(int mf)
{
	return 0;
}

static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv)
{
	unsigned int pidh = 0;
	unsigned int pidl = 0;
	int ret;

	DBG_INFO("");

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
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	struct i2c_adapter *i2c_adap = client->adapter;
	int mode = ctrl->val;
	int ret = 0;
	switch (mode) {
	case NONE_AF:
		priv->af_mode = NONE_AF;
		break;
	case SINGLE_AF:
		priv->af_mode = SINGLE_AF;
		pr_info("AF set to single mode\n");
		ret = camera_i2c_write(i2c_adap, 1, 0x3023, 0x01);
		ret |= camera_i2c_write(i2c_adap, 1, 0x3022, 0x81);
		mdelay(20);
		ret |= camera_i2c_write(i2c_adap, 1, 0x3022, 0x03);
		if (ret < 0)
			return ret;
		break;

	case CONTINUE_AF:
		priv->af_mode = CONTINUE_AF;
		pr_info("AF set to the contious mode\n");
		camera_i2c_write(i2c_adap, 1, 0x3023, 0x01);
		camera_i2c_write(i2c_adap, 1, 0x3022, 0x04);
		break;

	case ZONE_AF:
		priv->af_mode = ZONE_AF;
		pr_info("AF set to zone mode\n");
		pr_info("AF x = %d,y = %d\n",
			priv->af_region.position_x, priv->af_region.position_y);
		camera_i2c_write(i2c_adap, 1, 0x3024,
				 priv->af_region.position_x);
		camera_i2c_write(i2c_adap, 1, 0x3025,
				 priv->af_region.position_y);
		mdelay(10);
		if (ret < 0)
			return ret;
		break;
	default:
		return -ERANGE;
	}
	return 0;
}

static int module_get_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	ctrl->val = priv->af_mode;

	return ret;
}

static int module_get_af_status(struct camera_module_priv *priv,
				struct v4l2_ctrl *ctrl)
{
	int ret = 1;
	struct v4l2_subdev *sd = &priv->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *i2c_adap = client->adapter;
	unsigned int af_status = 0;
	ctrl->val = AF_STATUS_DISABLE;
	priv->af_status = AF_STATUS_DISABLE;
	camera_i2c_read(i2c_adap, 1, 0x3028, &ret);
	camera_i2c_read(i2c_adap, 1, 0x3029, &af_status);
	mdelay(1);
	if (ret == 0)
		pr_err("AF status not ready\n");
	else if (ret == 1) {
		switch (af_status) {
		case 0x70:
			ctrl->val = AF_STATUS_DISABLE;
			priv->af_status = AF_STATUS_DISABLE;
			break;
		case 0x10:
			ctrl->val = AF_STATUS_OK;
			priv->af_status = AF_STATUS_OK;
			break;
		case 0x20:
			ctrl->val = AF_STATUS_OK;
			priv->af_status = AF_STATUS_OK;
			break;
		default:
			ctrl->val = AF_STATUS_UNFINISH;
			priv->af_status = AF_STATUS_UNFINISH;
			break;
		}
	}

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

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	void __user *from;
	int tmp_xstart = 0, tmp_ystart = 0, tmp_af_width = 0, tmp_af_height = 0;
	int xstart = 0, ystart = 0, af_width = 0, af_height = 0;
	struct v4l2_afregion tmp;
	if (1 == ctrl->val) {
		pr_err("0xbfffffff module_set_af_region\n");
		return 0;
	}
	from = compat_ptr(ctrl->val);
	if (copy_from_user(&tmp, from, sizeof(struct v4l2_afregion))) {
		pr_err("err! copy_from_user failed!\n");
		return -1;
	}
	tmp_xstart = tmp.position_x * priv->win->width / 800;
	tmp_ystart = tmp.position_y * priv->win->height / 600;
	tmp_af_width = tmp.width * priv->win->width / 800;
	tmp_af_height = tmp.height * priv->win->height / 600;
	tmp_xstart = (tmp_xstart + tmp_af_width) / 32;
	tmp_ystart = (tmp_ystart + tmp_af_height) / 32;
	if (tmp_xstart < 1)
		xstart = 1;
	else if (tmp_xstart > 79)
		xstart = 79;
	else
		xstart = tmp_xstart;

	if (tmp_ystart < 1)
		ystart = 1;
	else if (tmp_ystart > 59)
		ystart = 59;
	else
		ystart = tmp_ystart;
	priv->af_region.position_x = xstart;
	priv->af_region.position_y = ystart;
	priv->af_region.width = af_width;
	priv->af_region.height = af_height;
	return 0;
}

void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	pr_err("sensor_power_on  fffffffffffff rear =1\n");
	if (hardware) {

		if (rear) {
			pr_err("sensor_power_on rear =1\n");
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			mdelay(100);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			mdelay(100);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
			mdelay(100);
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(100);
		} else {
#if 0
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			mdelay(5);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			mdelay(10);
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_HIGH);
			mdelay(30);
#endif
		}
	} else {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(30);
			pr_err("mdelay(30) sensor_power_on rear =1\n");

		} else {
#if 0
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			pr_err("mdelay(30) GPIO_HIGHsensor_power_on rear =1\n");
			mdelay(30);
#endif
		}
	}
}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {

		if (rear) {
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			pr_err("rear =1\n");
		} else {
			/* printk(KERN_ERR"rear =0\n");
			   set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_front, GPIO_LOW); */
		}
	} else {
		if (rear)
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
		/* else
		   set_gpio_level(&spinfo->gpio_front, GPIO_LOW); */
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
