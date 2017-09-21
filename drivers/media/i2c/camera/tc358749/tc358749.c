/*
 * tc358749 Hdmi-in Driver
 *
 * Copyright (C) 2014 Actions Semiconductor Co.,LTD
 * Yiguang <liuyiguang@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <media/v4l2-chip-ident.h>
#include <linux/platform_device.h>
#include "module_diff.h"
#include "../host_comm/owl_device.h"
#include "../module_comm/module_comm.c"
#include "../module_comm/module_detect.c"

#include <linux/init.h>
#include <linux/kthread.h>

#include <linux/switch.h>

bool verified;
static struct task_struct *tsk;

static struct switch_dev sdev_hdmi_audio = {
	.name = "hdmi_in_audio",
};

static inline void free_thread()
{
    if (verified)
    {
        if (tsk != NULL) {
            if (!IS_ERR(tsk)) {
                int ret = kthread_stop(tsk);
                pr_info("check_hdmi_thread is stop %d ==============\n", ret);
	            switch_dev_unregister(&sdev_hdmi_audio);
                pr_info("unregist audio switch dev ==============\n");
            }
        }
    }   
}

static int check_hdmi(void *data)
{
    struct i2c_adapter *i2c_adap = data;
	unsigned int hdmi_check_value[50] = {0};
	int i;
    int j = 0;
    bool hdmi_input = false;
    switch_set_state(&sdev_hdmi_audio, 0);
    
    do {
	    camera_i2c_read(i2c_adap, 1, 0x8520, &hdmi_check_value[0]); // SYS_STATUS
	    camera_i2c_read(i2c_adap, 1, 0x8521, &hdmi_check_value[1]); // Video input status
	    camera_i2c_read(i2c_adap, 1, 0x8522, &hdmi_check_value[2]); // Video input status1
	    camera_i2c_read(i2c_adap, 1, 0x8523, &hdmi_check_value[3]); // audio status0
	    camera_i2c_read(i2c_adap, 1, 0x8524, &hdmi_check_value[4]); // audio status1
	    camera_i2c_read(i2c_adap, 1, 0x8525, &hdmi_check_value[5]); // Video input status2
	    camera_i2c_read(i2c_adap, 1, 0x8526, &hdmi_check_value[6]); // clk status
	    camera_i2c_read(i2c_adap, 1, 0x8527, &hdmi_check_value[7]); // phyerr status
	    camera_i2c_read(i2c_adap, 1, 0x8528, &hdmi_check_value[8]); // VI status

        // HDMI Input Video Timing Check (PCLK)
	    camera_i2c_read(i2c_adap, 1, 0x852E, &hdmi_check_value[10]); // PX_FREQ0
	    camera_i2c_read(i2c_adap, 1, 0x852F, &hdmi_check_value[11]); // PX_FREQ1
	    
        // HDMI Input Video Timing Check (Horizontal Related)
	    camera_i2c_read(i2c_adap, 1, 0x858A, &hdmi_check_value[12]); // H_SIZE0
	    camera_i2c_read(i2c_adap, 1, 0x858B, &hdmi_check_value[13]); // H_SIZE1
	    camera_i2c_read(i2c_adap, 1, 0x8580, &hdmi_check_value[14]); // DE_POS_H0
	    camera_i2c_read(i2c_adap, 1, 0x8581, &hdmi_check_value[15]); // DE_POS_H1
	    camera_i2c_read(i2c_adap, 1, 0x8582, &hdmi_check_value[16]); // DE_WIDTH_H0
	    camera_i2c_read(i2c_adap, 1, 0x8583, &hdmi_check_value[17]); // DE_WIDTH_H1
	    
        // HDMI Input Video Timing Check (Vertical Related)
	    camera_i2c_read(i2c_adap, 1, 0x858C, &hdmi_check_value[18]); // V_SIZE0
	    camera_i2c_read(i2c_adap, 1, 0x858D, &hdmi_check_value[19]); // V_SIZE1
	    camera_i2c_read(i2c_adap, 1, 0x8584, &hdmi_check_value[20]); // DE_POS_V10
	    camera_i2c_read(i2c_adap, 1, 0x8585, &hdmi_check_value[21]); // DE_POS_V11
	    camera_i2c_read(i2c_adap, 1, 0x8586, &hdmi_check_value[22]); // DE_POS_V20
	    camera_i2c_read(i2c_adap, 1, 0x8587, &hdmi_check_value[23]); // DE_POS_V21
	    camera_i2c_read(i2c_adap, 1, 0x8588, &hdmi_check_value[24]); // DE_WIDTH_V0
	    camera_i2c_read(i2c_adap, 1, 0x8589, &hdmi_check_value[25]); // DE_WIDTH_V1
        //printk("Checking hdmi signal========\n");

        //1080P
        if (hdmi_check_value[17] == 0x07 && hdmi_check_value[16] == 0x80 && \
                hdmi_check_value[25] == 0x04 && hdmi_check_value[24] == 0x38) {
            //Only print this message for the first time to get the hdmi signal.
            if (j < 5) {
                pr_info("Get hdmi signal 1080P========\n");
                j++;
            } else if (!hdmi_input) {
                pr_info("Get stable hdmi signal, enable audio=====\n");
                hdmi_input = true;
		        switch_set_state(&sdev_hdmi_audio, 1);
            }
            if (1920 != module_win_list[0]->width && 1080 != module_win_list[0]->height) {
                pr_info("Change regs to 1080P========\n");
	            camera_write_array(i2c_adap, module_1080p_regs);
                module_win_list[0] = &module_win_1080p;
            }
            continue;
        }
        //720P
        if (hdmi_check_value[17] == 0x05 && hdmi_check_value[16] == 0x00 && \
                hdmi_check_value[25] == 0x02 && hdmi_check_value[24] == 0xD0) {
            //Only print this message for the first time to get the hdmi signal.
            if (j < 5) {
                pr_info("Get hdmi signal 720P========\n");
                j++;
            } else if (!hdmi_input) {
                pr_info("Get stable hdmi signal, enable audio=====\n");
                hdmi_input = true;
		        switch_set_state(&sdev_hdmi_audio, 1);
            }
            if (1280 != module_win_list[0]->width && 720 != module_win_list[0]->height) {
                pr_info("Change regs to 720P========\n");
	            camera_write_array(i2c_adap, module_720p_regs);
                module_win_list[0] = &module_win_720p;
            }
            continue;
        }
        if (hdmi_input) {
            pr_info("No Hdmi siganl input, close audio ========\n");
            hdmi_input = false;
		    switch_set_state(&sdev_hdmi_audio, 0);
        }
        j = 0;
        msleep(100);
    } while (!kthread_should_stop());

    return 0;
}

static bool gpio_hdmiin_status = false;
static struct dts_gpio hdmiin_gpio_int;
static int hdmiin_int_init(void)
{
	struct device_node *fdt_node = NULL;
	int ret = 0;

       if (true ==  gpio_hdmiin_status)
       {
            return 0;
       }
	fdt_node = of_find_compatible_node(NULL, NULL, "tc358749");
	if (NULL == fdt_node) 
       {
		DBG_INFO("no [hdmiin] in dts\n");
              goto failed;
	}
    
	if (gpio_init(fdt_node, "hdmiin-int-gpios",
	      &hdmiin_gpio_int, GPIO_LOW)) 
	{
	    goto failed;
       }
       gpio_hdmiin_status = true;
	return 0;
    failed:
	gpio_hdmiin_status = false;
       return -1;
}

void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
    printk("(%s)--line=%d",__FUNCTION__,__LINE__);
//	void __iomem *mfp_base;
//const volatile 
void __iomem *adds= ioremap((phys_addr_t)0xe01b0000,36);
	if (hardware) {
		if (rear) {
                     DBG_INFO("----------------hardware=1--------------------\n");
                     DBG_INFO("line=(%d) gpio aout=0x%x ain=0x%x adat=0x%x\n", __LINE__,readl(adds),readl(adds+4),readl(adds+8));
                     DBG_INFO("line=(%d) gpio bout=0x%x bin=0x%x bdat=0x%x\n", __LINE__,readl(adds+0xc),readl(adds+0x10),readl(adds+0x14));
                     //gpio a27 out h
                     if (!hdmiin_int_init())
                     {
                        set_gpio_level(&hdmiin_gpio_int, GPIO_LOW);
                     }

                     DBG_INFO("line=(%d) gpio aout=0x%x ain=0x%x adat=0x%x\n", __LINE__,readl(adds),readl(adds+4),readl(adds+8));
                     DBG_INFO("line=(%d) gpio bout=0x%x bin=0x%x bdat=0x%x\n", __LINE__,readl(adds+0xc),readl(adds+0x10),readl(adds+0x14));
			set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
			mdelay(50);
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			mdelay(50);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
			set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
		} else {
			DBG_INFO("Cold power on tc358749 as rear camera.\n");
		}
	} else {
		if (rear) {
			//set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
			//set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
			//mdelay(50);
            DBG_INFO("----------------hardware = 0--------------------\n");
			set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
			set_gpio_level(&spinfo->gpio_front, GPIO_HIGH);
		} else {
			DBG_INFO("Soft power on tc358749 as rear camera.\n");
		}
	}

}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
    printk("(%s)--line=%d",__FUNCTION__,__LINE__);
    if ( true == gpio_hdmiin_status)
       {
            gpio_exit(&hdmiin_gpio_int, GPIO_LOW);
            gpio_hdmiin_status = false;
       }
	//if (hardware) {
	//	if (rear) {
	//		set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
	//		mdelay(50);
	//		set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
	//		set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
	//	} else {
	//		DBG_INFO("Cold power off tc358749 as rear camera.\n");
	//	}
	//} else {
	//	if (rear) {
	//		set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
	//		set_gpio_level(&spinfo->gpio_front, GPIO_LOW);
    //    } else
	//		DBG_INFO("Soft power off tc358749 as rear camera.\n");
	//}

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
			*dest = data_array[1] << 8 | data_array[0];
		if (data_width == 4)
			*dest = data_array[3] << 24 | data_array[2] << 16 | data_array[1] << 8 | data_array[0];
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
	if (data_width == 4) {
		data_array[0] = data & 0xff;
		data_array[1] = (data >> 8) & 0xff;
		data_array[2] = (data >> 16) & 0xff;
		data_array[3] = (data >> 24) & 0xff;
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
	int ret;

	/*
	 * check and show product ID and manufacturer ID
	 */
       printk("(%s)--line=%d",__FUNCTION__,__LINE__);
	 
	ret = camera_i2c_read(i2c_adap, 2, PIDH, &pidh);

	switch (pidh) {
	case CAMERA_MODULE_PID:
		pr_info("[%s] Product ID verified %x\n",
			CAMERA_MODULE_NAME, pidh);
        verified = true;
		break;

	default:
		pr_info("[%s] Product ID error %x\n",
			CAMERA_MODULE_NAME, pidh);
        verified = false;
		return -ENODEV;
	}
	return ret;
}

static void update_after_init(struct i2c_adapter *i2c_adap)
{
	int ret = 0;

    if (verified)
    {
        tsk = kthread_run(check_hdmi, i2c_adap, "check_hdmi_thread");
        if (IS_ERR(tsk)) {
            pr_info("create check_hdmi_thread failed!\n");
        } else {
            pr_info("create check_hdmi_thread succeed!\n");
	        ret = switch_dev_register(&sdev_hdmi_audio);
	        if (ret < 0) {
                pr_err("%s: register siwtch failed(%d)\n", __func__, ret);
	        }
        }
    }       
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
	return 0;
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

static int module_soft_reset(struct i2c_client *client)
{
	//struct i2c_adapter *i2c_adap = client->adapter;
	//int ret = 0;

	//ret |= camera_i2c_write(i2c_adap, 1, 0x0103, 0x01);
	//mdelay(10);
	//ret |= camera_i2c_write(i2c_adap, 1, 0x0103, 0x00);
	//mdelay(10);

	return 0;
}

static int module_set_stream(struct i2c_client *client, int enable)
{
	return 0;
}

static int module_start_aec(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_freeze_aec(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_save_exposure_param(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_set_exposure_param(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_set_auto_white_balance(struct v4l2_subdev *sd,
					 struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_white_balance_temperature(struct v4l2_subdev *sd,
						struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_exposure_auto(struct v4l2_subdev *sd,
				    struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_scene_exposure(struct v4l2_subdev *sd,
				     struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	return 0;
}

static int module_set_exposure(struct i2c_client *client, int val)
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

static int module_set_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_af_status(struct camera_module_priv *priv,
				struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
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
