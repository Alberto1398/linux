/*
 * gs5604 Camera Driver
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

/*************************************************************************
* FUNCTION
*   GS5604MIPI_WAIT_STATUS
*
* DESCRIPTION
*   This function wait the 0x000E bit 0 is 1;then clear the bit 0;
*   The salve address is 0x34
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void gs5604mipi_wait_status(struct i2c_adapter *i2c_adap)
{
	unsigned int tmp = 0, count = 0;
	DBG_INFO("");

	do {
		count++;
		if (count > 50)
			break;
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x01;
		DBG_INFO("gs5604mipi_wait_status while1!\r\n");
	} while (!tmp);

	mdelay(10);
	camera_i2c_write(i2c_adap, 1, 0x0012, 0x01);
	mdelay(10);
	do {
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x01;
		DBG_INFO("gs5604mipi_wait_status while2!\r\n");
	} while (tmp);

	mdelay(100);

	DBG_INFO("gs5604mipi_wait_status exit\n ");
}

/*************************************************************************
* FUNCTION
*   GS5604MIPI_WAIT_STATUS1
*
* DESCRIPTION
*   This function wait the 0x000E bit 1 is 1;then clear the bit 1;
*   The salve address is 0x78
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void gs5604mipi_wait_status1(struct i2c_adapter *i2c_adap)
{
	unsigned int tmp = 0, count = 0;
	DBG_INFO("[GS5604MIPI]enter GS5604MIPI_WAIT_STATUS1 function:\n ");

	do {
		count++;
		if (count > 50)
			break;
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x02;
		DBG_INFO("gs5604mipi_wait_status while1!\r\n");
	} while (tmp != 0x02);
	mdelay(10);

	camera_i2c_write(i2c_adap, 1, 0x0012, 0x02);

	do {
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x02;
		DBG_INFO("gs5604mipi_wait_status while2!\r\n");
	} while (tmp);

	mdelay(10);
	DBG_INFO("[GS5604MIPI]exit GS5604MIPI_WAIT_STATUS1 function:\n ");
}

/*************************************************************************
* FUNCTION
*   GS5604MIPI_WAIT_STATUS2
*
* DESCRIPTION
*   This function wait the 0x000E bit 0 is 1;then clear the bit 0;
*   The salve address is 0x78
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void gs5604mipi_wait_status2(struct i2c_adapter *i2c_adap)
{
	unsigned int tmp = 0, count = 0;

	DBG_INFO("[GS5604MIPI]enter GS5604MIPI_WAIT_STATUS2 function:\n ");

	do {
		count++;
		if (count > 50)
			break;
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x01;
		DBG_INFO("gs5604mipi_wait_status while1!\r\n");
	} while (!tmp);
	mdelay(10);

	camera_i2c_write(i2c_adap, 1, 0x0012, 0x01);

	do {
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x01;
		DBG_INFO("gs5604mipi_wait_status while2!\r\n");
	} while (tmp);

	mdelay(10);
	DBG_INFO("[GS5604MIPI]exit GS5604MIPI_WAIT_STATUS2 function:\n ");
}

/*************************************************************************
* FUNCTION
*   GS5604MIPI_WAIT_STATUS3
*
* DESCRIPTION
*   This function wait the 0x000E bit 4 is 1;then clear the bit 1;
*   The salve address is 0x78
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void gs5604mipi_wait_status3(struct i2c_adapter *i2c_adap)
{
	unsigned int tmp = 0;
	int count = 0;
	DBG_INFO("[GS5604MIPI]enter GS5604MIPI_WAIT_STATUS3 function:\n");
	do {
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x10;
		DBG_INFO("gs5604mipi_wait_status while1!\r\n");
	} while (tmp != 0x10);

	mdelay(10);
	camera_i2c_write(i2c_adap, 1, 0x0012, 0x10);

	do {
		count++;
		if (count > 50)
			break;
		camera_i2c_read(i2c_adap, 1, 0x000E, &tmp);
		tmp &= 0x10;
		DBG_INFO("gs5604mipi_wait_status while1!\r\n");
	} while (tmp);

	mdelay(10);
	DBG_INFO("[GS5604MIPI]exit GS5604MIPI_WAIT_STATUS3 function:\n ");
}

static void update_after_init(struct i2c_adapter *i2c_adap)
{

	/*change iic address form 0x1a to 0x3c */
	MODULE_I2C_REAL_ADDRESS = 0x3c;

	mdelay(50);		/*don't delete */
	gs5604mipi_wait_status2(i2c_adap);
	camera_i2c_write(i2c_adap, 1, 0x5008, 0x00);
	mdelay(50);
}

static void start_af(struct i2c_adapter *i2c_adap)
{
	int af_state = 0;
	int count = 0;

	/***** AF manual start******/
	camera_i2c_write(i2c_adap, 2, 0x6648, 0x00);
	camera_i2c_write(i2c_adap, 1, 0x00B2, 0x02);
	/*halfrelease mode: manual af */
	camera_i2c_write(i2c_adap, 1, 0x00B3, 0x02);
	camera_i2c_write(i2c_adap, 1, 0x00B4, 0x02);
	camera_i2c_write(i2c_adap, 1, 0x00B1, 0x01);	/*restart */
	mdelay(100);
	do {
		count++;
		if (count > 50)
			break;
		camera_i2c_read(i2c_adap, 1, 0x8b8a, &af_state);
	} while (af_state != 0x03);
	/***** AF manual end******/
}

static void enter_preview_mode(struct i2c_adapter *i2c_adap)
{
	gs5604mipi_wait_status1(i2c_adap);
	mdelay(20);
}

static void enter_capture_mode(struct i2c_adapter *i2c_adap)
{
	start_af(i2c_adap);
	gs5604mipi_wait_status1(i2c_adap);
	mdelay(100);
}

/*
static void enter_video_mode(struct i2c_adapter *i2c_adap)
{
	gs5604mipi_wait_status1(i2c_adap);
	mdelay(20);
}
*/
static int module_soft_reset(struct i2c_client *client)
{
	int ret = 0;

	struct i2c_adapter *i2c_adap = client->adapter;
	DBG_INFO("");

	gs5604mipi_wait_status(i2c_adap);
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

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
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
	DBG_INFO("");
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

static int module_set_exposure(struct i2c_client *client, int val)
{
	int ret = 0;
	/*
	   struct i2c_adapter *i2c_adap = client->adapter;
	   unsigned int exposure_3500 = 0;
	   unsigned int exposure_3501 = 0;
	   unsigned int exposure_3502 = 0;

	   DBG_INFO(" val = 0x%04x\n", val);

	   exposure_3500 = (val >> 16) & 0x0f;
	   exposure_3501 = (val >> 8) & 0xff;
	   exposure_3502 = val & 0xf0;

	   ret = camera_i2c_write(i2c_adap, 0x3500, exposure_3500);
	   ret |= camera_i2c_write(i2c_adap, 0x3501, exposure_3501);
	   ret |= camera_i2c_write(i2c_adap, 0x3502, exposure_3502);
	 */
	return ret;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	int ret = 0;
	/*
	   struct i2c_adapter *i2c_adap = client->adapter;
	   unsigned int exposure_3500 = 0;
	   unsigned int exposure_3501 = 0;
	   unsigned int exposure_3502 = 0;

	   ret = camera_i2c_read(i2c_adap, 0x3500, &exposure_3500);
	   ret |= camera_i2c_read(i2c_adap, 0x3501, &exposure_3501);
	   ret |= camera_i2c_read(i2c_adap, 0x3502, &exposure_3502);
	   val = (exposure_3500 & 0x0f) << 16 +
	   (exposure_3501 & 0xff) << 8 + exposure_3502 & 0xf0;
	   DBG_INFO(" val = 0x%04x\n", val);
	 */
	return ret;
}

static int module_get_gain(struct i2c_client *client, int *val)
{
	int ret = 0;
	/*
	   struct i2c_adapter *i2c_adap = client->adapter;
	   unsigned int gain_350a = 0;
	   unsigned int gain_350b = 0;

	   ret = camera_i2c_read(i2c_adap, 0x350a, &gain_350a);
	   ret |= camera_i2c_read(i2c_adap, 0x350b, &gain_350b);
	   val = (gain_350a & 0x03) << 8 + gain_350b & 0xff;

	   DBG_INFO(" val = 0x%04x\n", val);
	 */
	return ret;
}

static int module_set_gain(struct i2c_client *client, int val)
{
	int ret = 0;
	/*
	   struct i2c_adapter *i2c_adap = client->adapter;
	   unsigned int gain_350a = 0;
	   unsigned int gain_350b = 0;

	   DBG_INFO(" val = 0x%04x\n", val);
	   gain_350a = (val >> 8) &0x03;
	   gain_350b = val & 0xff;
	   ret = camera_i2c_write(i2c_adap, 0x350a, gain_350a);
	   ret |= camera_i2c_write(i2c_adap, 0x350b, gain_350b);
	 */
	return ret;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	switch (ctrl->val) {
	case 4:
		camera_write_array(client->adapter, module_exp_comp_pos4_regs);
		break;
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
	case -4:
		camera_write_array(client->adapter, module_exp_comp_neg4_regs);
		break;
	default:
		break;
	}

	return ret;
}

static int module_set_mbusformat(struct i2c_client *client,
				 const struct module_color_format *cfmt)
{
	return 0;
}

static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	switch (mf) {
	case 0:
		/* no flip,no mirror */
		camera_i2c_write(client->adapter, 1, 0x008c, 0x00);
		break;
	case 1:
		/* no flip,horizontal mirror */
		camera_i2c_write(client->adapter, 1, 0x008c, 0x01);
		break;
	case 2:
		/* vertical flip,no mirror */
		camera_i2c_write(client->adapter, 1, 0x008c, 0x02);
		break;
	case 3:
		/* vertical flip,horizontal mirror */
		camera_i2c_write(client->adapter, 1, 0x008c, 0x03);
		break;
	default:
		camera_i2c_write(client->adapter, 1, 0x008c, 0x03);
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
	unsigned int pid = 0;
	int ret;

	DBG_INFO("");

	/*
	 * check and show product ID and manufacturer ID
	 */
	ret = camera_i2c_read(i2c_adap, 1, PID, &pid);

	switch (pid) {
	case CAMERA_MODULE_PID:
		pr_info("[%s] Product ID verified %x\n",
			CAMERA_MODULE_NAME, pid);
		break;
	default:
		pr_info("[%s] Product ID error %x\n", CAMERA_MODULE_NAME, pid);
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
		ret = camera_i2c_write(i2c_adap, 1, 0x00B2, 0x00);
		ret |= camera_i2c_write(i2c_adap, 1, 0x00B3, 0x00);
		ret |= camera_i2c_write(i2c_adap, 1, 0x00B4, 0x00);
		pr_info("in the single mode\n");
		mdelay(10);
		if (ret < 0)
			return ret;
		break;
	case CONTINUE_AF:
		priv->af_mode = CONTINUE_AF;
		ret = camera_i2c_write(i2c_adap, 1, 0x00B2, 0x01);
		ret |= camera_i2c_write(i2c_adap, 1, 0x00B3, 0x01);
		ret |= camera_i2c_write(i2c_adap, 1, 0x00B4, 0x01);
		pr_info("in the contious mode\n");
		mdelay(10);
		if (ret < 0)
			return ret;
		break;
	case ZONE_AF:
		priv->af_mode = ZONE_AF;
		camera_i2c_write(i2c_adap, 1, 0x6a50,
				 priv->af_region.position_x);
		camera_i2c_write(i2c_adap, 1, 0x6a52,
				 priv->af_region.position_y);
		camera_i2c_write(i2c_adap, 1, 0x6a54, 80);
		camera_i2c_write(i2c_adap, 1, 0x6a56, 60);
		ret = camera_i2c_write(i2c_adap, 1, 0x00B2, 0x00);
		ret |= camera_i2c_write(i2c_adap, 1, 0x00B3, 0x00);
		ret |= camera_i2c_write(i2c_adap, 1, 0x00B4, 0x00);
		pr_info("in the ZONE_AF mode\n");
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
	struct v4l2_subdev *sd = &priv->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;
	unsigned int af_status = 0;
	ctrl->val = AF_STATUS_DISABLE;
	priv->af_status = AF_STATUS_DISABLE;

	ret = camera_i2c_read(i2c_adap, 1, 0x8b8a, &af_status);
	switch (af_status) {
	case 0x08:

		ctrl->val = AF_STATUS_OK;
		priv->af_status = AF_STATUS_OK;

		break;
	case 0x0f:
		ctrl->val = AF_STATUS_OK;
		priv->af_status = AF_STATUS_OK;

		break;
	case 0xff:

		ctrl->val = AF_STATUS_FAIL;
		priv->af_status = AF_STATUS_FAIL;
		break;
	default:
		ctrl->val = AF_STATUS_UNFINISH;
		priv->af_status = AF_STATUS_UNFINISH;

		break;
	}

	return 0;
}

static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int tmp_xstart = 0, tmp_ystart = 0, tmp_af_width = 0, tmp_af_height = 0;
	int xstart = 0, ystart = 0, af_width = 0, af_height = 0;
	void __user *from;
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
	tmp_xstart = tmp.position_x * priv->win->width / 2000;
	tmp_ystart = tmp.position_y * priv->win->height / 2000;
	tmp_af_width = tmp.width * priv->win->width / 2000;
	tmp_af_height = tmp.height * priv->win->height / 2000;
	xstart = tmp_xstart + 49;
	ystart = tmp_ystart + 4;
	af_width = abs(tmp_af_width - tmp_xstart);
	af_height = abs(tmp_af_height - tmp_ystart);
	if (af_width < 350)
		af_width = 350;
	if (af_width > 2559)
		af_width = 2559;
	if (af_height < 350)
		af_height = 350;
	if (af_height > 1939)
		af_height = 1939;
	if ((xstart + af_width) > 2559)
		xstart = 2559 - af_width;
	if ((ystart + af_height) > 1939)
		ystart = 1939 - af_height;
	priv->af_region.position_x = xstart;
	priv->af_region.position_y = ystart;
	priv->af_region.width = af_width;
	priv->af_region.height = af_height;
	pr_info("the x is %d,the y is %d\n", xstart, ystart);
	return 0;
}

void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{

	if (hardware) {

		if (rear) {
			set_gpio_level(&spinfo->gpio_power, GPIO_LOW);
			mdelay(50);
			set_gpio_level(&spinfo->gpio_power, GPIO_HIGH);
			mdelay(50);
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			mdelay(50);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
			mdelay(50);
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(50);
		} else {
			/*
			   set_gpio_level(spinfo->gpio_power, GPIO_LOW);
			   mdelay(500);
			   set_gpio_level(spinfo->gpio_power, GPIO_HIGH);
			   mdelay(500);
			   set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			   set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			   mdelay(500);
			   set_gpio_level(&spinfo->gpio_front_reset, GPIO_HIGH);
			   mdelay(500);
			   set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			   mdelay(500);
			 */
		}

	} else {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(20);
		} else {
			/*
			   set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			   mdelay(20);
			 */
		}
	}
}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
	if (hardware) {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			mdelay(5);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			mdelay(10);
		} else {
			/*set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			   mdelay(5);
			   set_gpio_level(&spinfo->gpio_front_reset,
			   GPIO_LOW); */
		}

		/* There are two i2c addresses for GS5604.
		   When init regs, we should use 0x1a,after that,change
		   it to 0x3c. Here change it to 0x1a for resume */
		MODULE_I2C_REAL_ADDRESS = 0x1a;
	} else {
		if (rear) {
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			mdelay(10);
		}
		/*else
		   set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
		 */
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
