/*
 * gc2035 Camera Driver
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
	int ret;
	unsigned int reg_0xfe;
	struct i2c_adapter *i2c_adap = client->adapter;
    ret = camera_i2c_read(i2c_adap, 1, 0xfe, &reg_0xfe);
    reg_0xfe |= (0x1 << 7);
    ret |= camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
    mdelay(1);
    reg_0xfe &= (~(0x1 << 7));
    ret |= camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
    return ret;
}

static int module_start_aec(struct v4l2_subdev *sd)
{
  int ret = 0;
  
  struct i2c_client *client = v4l2_get_subdevdata(sd);
  struct i2c_adapter *i2c_adap = client->adapter;
  
  unsigned int reg_0xfe = 0x00;
  unsigned int reg_0xb6 = 0x03;

  ret = camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe); //page 0
  ret |= camera_i2c_write(i2c_adap, 1, 0xb6, reg_0xb6); 

  return ret;
}

static int module_freeze_aec(struct v4l2_subdev *sd)
{
    int ret = 0;
 
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct i2c_adapter *i2c_adap = client->adapter;
 
    unsigned int reg_0xfe = 0x00;
    unsigned int reg_0xb6 = 0x00;

    ret = camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe); //page 0
    ret |= camera_i2c_write(i2c_adap, 1, 0xb6, reg_0xb6); 
 
    return ret;
}

static int module_save_exposure_param(struct v4l2_subdev *sd)
{
	int ret = 0;
	
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	struct i2c_adapter *i2c_adap = client->adapter;
	
	unsigned int reg_0xfe = 0x00;
	unsigned int reg_0x03;
	unsigned int reg_0x04;

	ret = camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe); //page 0
	ret |= camera_i2c_read(i2c_adap, 1, 0x03, &reg_0x03);
	ret |= camera_i2c_read(i2c_adap, 1, 0x04, &reg_0x04);
		
	priv->preview_exposure_param.shutter = (reg_0x03 << 8) | reg_0x04;
	priv->capture_exposure_param.shutter = (priv->preview_exposure_param.shutter)/2;
	
	//printk("GC2155 module_save_exposure_param, win->name:%s\n", priv->win->name);
	return ret;
}

static int module_set_exposure_param(struct v4l2_subdev *sd)
{
 	int ret = 0;
  
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

	ret = camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe); //page 0
	ret |= camera_i2c_write(i2c_adap, 1, 0x03, reg_0x03);
	ret |= camera_i2c_write(i2c_adap, 1, 0x04, reg_0x04);
	
	return ret;
}

static int module_set_exposure_auto(struct v4l2_subdev *sd,
				    struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	struct i2c_adapter *i2c_adap = client->adapter;
    int exposure_auto;
	int ret = 0;

	if(ctrl)
		exposure_auto = ctrl->val;
	else
		exposure_auto = V4L2_EXPOSURE_AUTO;

	if (exposure_auto < 0 || exposure_auto > 1) {
		return -ERANGE;
	}
  
	switch (exposure_auto) {
	case V4L2_EXPOSURE_AUTO:/*  auto */
        ret = camera_write_array(i2c_adap, module_scene_auto_regs);
		break;

    case V4L2_EXPOSURE_MANUAL: // non auto
        ret = 0;
        break;
    }

	priv->exposure_auto = exposure_auto;
	if(ctrl)
		ctrl->cur.val = exposure_auto;

    return 0;
}

static int module_set_auto_white_balance(struct v4l2_subdev *sd,
					 struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	struct i2c_adapter *i2c_adap = client->adapter;
	int auto_white_balance;
	int ret = 0;
    unsigned int reg_0x82;
    unsigned int reg_0xfe = 0x00;

	if(ctrl)
		auto_white_balance = ctrl->val;
	else
		auto_white_balance = 1;
  
	if (auto_white_balance < 0 || auto_white_balance > 1) {
		printk("[gc2155] set auto_white_balance over range, auto_white_balance = %d\n", auto_white_balance);
		return -ERANGE;
	}
	
	ret = camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
	ret |= camera_i2c_read(i2c_adap, 1, 0x82, &reg_0x82); 

	switch(auto_white_balance)
	{
	case 0:
		ret = 0;
		goto change_val;
		
	case 1:	
		ret |=camera_write_array(i2c_adap, module_whitebance_auto_regs);
		break;
	}
	
	reg_0x82 |= 0x02;
	ret |= camera_i2c_write(i2c_adap, 1, 0x82, reg_0x82); 
 
change_val:
	priv->auto_white_balance = auto_white_balance;
	if(ctrl)
		ctrl->cur.val = auto_white_balance;  
		
	return ret;
}

static int module_set_white_balance_temperature(struct v4l2_subdev *sd,
						struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
    int white_balance_temperature = ctrl->val;
    struct i2c_adapter *i2c_adap = client->adapter;
    unsigned int reg_0x82;
    unsigned int reg_0xfe = 0x00;
	int ret = 0;
	
	ret = camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
	ret |= camera_i2c_read(i2c_adap, 1, 0x82, &reg_0x82); 
	 
	reg_0x82 &= (~0x02);
	ret |= camera_i2c_write(i2c_adap, 1, 0x82, reg_0x82);  

	switch(white_balance_temperature)
	{
	case V4L2_WHITE_BALANCE_INCANDESCENT:
		ret = camera_write_array(i2c_adap, module_whitebance_incandescent_regs);
		break;
	
	case V4L2_WHITE_BALANCE_FLUORESCENT:
		ret = camera_write_array(i2c_adap, module_whitebance_fluorescent_regs);
		break;
	
	case V4L2_WHITE_BALANCE_DAYLIGHT:
		ret = camera_write_array(i2c_adap, module_whitebance_sunny_regs);
		break;
	
	case V4L2_WHITE_BALANCE_CLOUDY:
		ret = camera_write_array(i2c_adap, module_whitebance_cloudy_regs);
		break;
	
	default:
		printk("[gc2035] set white_balance_temperature over range, white_balance_temperature = %d\n", white_balance_temperature);
		return -ERANGE;	
	}
	
	priv->auto_white_balance = 0;
	priv->white_balance_temperature = white_balance_temperature;
	ctrl->cur.val = white_balance_temperature;
	
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
	return 0;
}

static int module_set_stream(struct i2c_client *client, int enable)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	struct i2c_adapter *i2c_adap = client->adapter;
	int ret = 0;
	unsigned char reg_0xfe = 0;
	unsigned char reg_0xf2= 0x0f;

    if (!enable) {
        pr_info("stream down");
        reg_0xf2 = 0x08;
        camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
        camera_i2c_write(i2c_adap, 1, 0xf2, reg_0xf2);
        return ret;
    }
    
    if (NULL == priv->win || NULL == priv->cfmt) {
        pr_err("cfmt or win select error");
        return (-EPERM);
    }	
    pr_info("stream on");
    reg_0xf2 = 0x70;
    camera_i2c_write(i2c_adap, 1, 0xfe, reg_0xfe);
    camera_i2c_write(i2c_adap, 1, 0xf2, reg_0xf2);
	   
	 return 0;   
}

static int module_set_exposure(struct i2c_client *client, int val)
{
	int ret = 0;
	return ret;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
    return 0;
}

static int module_get_gain(struct i2c_client *client, int *val)
{
    return 0;
}

static int module_set_gain(struct i2c_client *client, int val)
{
    return 0;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
    int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	
	switch(ctrl->val){
		case 4:
			camera_write_array(client->adapter,module_exp_comp_pos4_regs);
			break;
		case 3:
			camera_write_array(client->adapter,module_exp_comp_pos3_regs);
			break;
		case 2:
			camera_write_array(client->adapter,module_exp_comp_pos2_regs);
			break;
		case 1:
			camera_write_array(client->adapter,module_exp_comp_pos1_regs);
			break;
		case 0:
			camera_write_array(client->adapter,module_exp_comp_zero_regs);
			break;
		case -1:
			camera_write_array(client->adapter,module_exp_comp_neg1_regs);
			break;
		case -2:
			camera_write_array(client->adapter,module_exp_comp_neg2_regs);
			break;
		case -3:
			camera_write_array(client->adapter,module_exp_comp_neg3_regs);
			break;
		case -4:
			camera_write_array(client->adapter,module_exp_comp_neg4_regs);
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
	return 0;
}

static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv)
{
	unsigned int pidh = 0;
    unsigned int pidl = 0;
    int ret;
	
	ret = camera_i2c_read(i2c_adap, 1, PIDH, &pidh); 
	ret |= camera_i2c_read(i2c_adap, 1, PIDL, &pidl); 
	switch (VERSION(pidh, pidl)) 
    {
	case CAMERA_MODULE_PID:
		printk("[%s] Product ID verified %x\n",CAMERA_MODULE_NAME, VERSION(pidh, pidl));
		break;
	default:
		printk("[%s] Product ID error %x\n",CAMERA_MODULE_NAME, VERSION(pidh, pidl));
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
			DBG_INFO("Cold power on gc2035 as front camera.\n");
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
			DBG_INFO("Soft power on gc2035 as front camera.\n");
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
			DBG_INFO("Cold power off gc2035 as front camera.\n");
		} else {
			set_gpio_level(&spinfo->gpio_front_reset, GPIO_LOW);
			mdelay(2);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
		}
	} else {
		if (rear)
			DBG_INFO("Soft power off gc2035 as front camera.\n");
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
