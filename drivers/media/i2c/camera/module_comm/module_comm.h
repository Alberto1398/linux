
/*
 * module common macro
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MODULE_COMM_H__
#define __MODULE_COMM_H__

#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include "../host_comm/owl_device.h"

extern struct sensor_pwd_info g__spinfo;
extern struct module_regulators g_isp_ir;
extern int board_type_num;
extern int mf;
extern bool gpio_flash_cfg_exist;
DECLARE_DTS_SENSOR_CFG(g_sensor_cfg);

/* for flags */
#define MODULE_FLAG_VFLIP	(1 << 0)	/* Vertical flip image */
#define MODULE_FLAG_HFLIP	(1 << 1)	/* Horizontal flip image */
#define MODULE_FLAG_V_H_FLIP (MODULE_FLAG_VFLIP | MODULE_FLAG_HFLIP)

#define MODULE_FLAG_8BIT	(1 << 2)	/* default 8 bit */
#define MODULE_FLAG_10BIT	(1 << 3)	/*  10 bit interface */
#define MODULE_FLAG_12BIT	(1 << 4)	/* 12 bit interface */
#define MODULE_FLAG_FRONT	(1 << 5)	/* posization front */
#define MODULE_FLAG_BACK	(1 << 6)	/* posization back */
#define MODULE_FLAG_PALL	(1 << 7)	/* parellal interface */
#define MODULE_FLAG_MIPI	(1 << 8)	/* mipi interface */
#define MODULE_FLAG_CHANNEL1	(1 << 9)	/* use isp channel1 */
#define MODULE_FLAG_CHANNEL2	(1 << 10)	/* use isp channel2 */
#define MODULE_FLAG_3D	        (1 << 11)	/* 3d mode */
#define MODULE_FLAG_HOST1	(1 << 12)	/* on host1 */
#define MODULE_FLAG_HOST2	(1 << 13)	/* on host2 */
#define MODULE_FLAG_NODVDD	(1 << 14)	/* no use dvdd */
#define MODULE_FLAG_AF	        (1 << 15)	/* AUTO FOCUS */
#define MODULE_FLAG_ALWAYS_POWER	(1 << 16)	/* always power on */
#define MODULE_FLAG_NO_AVDD	(1 << 17)	/* no need to operate avdd */

#define SENSOR_FRONT    0x1
#define SENSOR_REAR     0x2
#define SENSOR_DUAL     0x4

#define ENDMARKER       { 0xff, 0xff, 0xff }

#define WIDTH_QQVGA	160
#define HEIGHT_QQVGA	120
#define WIDTH_QVGA	320
#define HEIGHT_QVGA     240
#define WIDTH_VGA       640
#define HEIGHT_VGA      480
#define WIDTH_SVGA      800
#define HEIGHT_SVGA     600
#define WIDTH_720P      1280
#define HEIGHT_720P     720
#define WIDTH_1080P     1920
#define HEIGHT_1080P	1080
#define WIDTH_UXGA	1600
#define HEIGHT_UXGA	1200
#define WIDTH_QSXGA	2592
#define HEIGHT_QSXGA	1944
#define WIDTH_QXGA      2048
#define HEIGHT_QXGA     1536
#define WIDTH_QUXGA     3264
#define HEIGHT_QUXGA    2448

#define GPIO_HIGH	1
#define GPIO_LOW	0

#define MIPI_YUV422     0x1e
#define MIPI_RAW8       0x2a
#define MIPI_RAW10      0x2b
#define MIPI_RAW12      0x2c

/*color_seq*/
#define COLOR_SEQ_UYVY  (0x0)
#define COLOR_SEQ_VYUY  (0x1)
#define COLOR_SEQ_YUYV  (0x2)
#define COLOR_SEQ_YVYU  (0x3)
#define COLOR_SEQ_SBGGR (0x0)
#define COLOR_SEQ_SGRBG (0x1)
#define COLOR_SEQ_SGBRG (0x2)
#define COLOR_SEQ_SRGGB (0x3)

#define CSI_HSCLK_EDGE_FALLING (1<<10)
#define CSI_LP11_NOT_CHECK     (1<<9)
#define CSI_HCLK_OM_ENT_EN     (1<<8)
#define CSI_CRC_EN             (1<<7)
#define CSI_ECC_EN             (1<<6)

#define SI_ERROR_PEND ((0x3<<14)|(0x3<<10))
#define SI_CH0_PRE    (1<<9)
#define SI_CH0_END    (1<<8)
#define SI_CH1_PRE    (1<<13)
#define SI_CH1_END    (1<<12)

/*#define MODULE_DBG_INFO*/
#define MODULE_DBG_ERR

#define CAMERA_INFO "Camera INFO:"
#define CAMERA_ERR "Camera ERR:"

#ifdef MODULE_DBG_INFO
#define DBG_INFO(fmt, args...) \
	pr_info("%s %d--%s() "fmt"\n", \
	CAMERA_INFO, __LINE__, __func__, ##args)
#else
#define DBG_INFO(fmt, args...) do {} while (0)
#endif

#ifdef MODULE_DBG_ERR
#define DBG_ERR(fmt, args...) \
	pr_err("%s %d--%s() "fmt"\n", \
	CAMERA_ERR, __LINE__, __func__, ##args)
#else
#define DBG_ERR(fmt, args...) do {} while (0)
#endif

struct regval_list {
	unsigned short data_width;
	unsigned int reg_num;
	unsigned int value;
};
struct camera_module_win_size {
	char *name;
	__u32 width;
	__u32 height;
	const struct regval_list *win_regs;
	unsigned int *frame_rate_array;
	unsigned int capture_only;
};

struct exposure_param {
	unsigned int max_shutter;
	unsigned int shutter;
	unsigned int gain;
	unsigned int dummy_line;
	unsigned int dummy_pixel;
	unsigned int extra_line;
};

struct module_color_format {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

struct camera_module_priv {
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler hdl;
	struct module_info *info;
	const struct module_color_format *cfmt;
	const struct camera_module_win_size *win;
	int model;
	int pcv_mode;
	int flip_flag;

	unsigned short auto_white_balance;
	unsigned short exposure;
	unsigned short power_line_frequency;
	unsigned short white_balance_temperature;
	unsigned short colorfx;
	unsigned short exposure_auto;
	unsigned short scene_exposure;

	struct exposure_param preview_exposure_param;
	struct exposure_param capture_exposure_param;
	struct v4l2_afregion af_region;
	enum v4l2_flash_led_mode flash_led_mode;
	enum af_status af_status;
	enum af_mode af_mode;
};

static int get_parent_node_id(struct device_node *node,
			      const char *property, const char *stem);
static int detect_work(void);
static int detect_init(void);
static void detect_deinit_power_off(void);
static void detect_deinit_power_hold(void);

static int camera_i2c_read(struct i2c_adapter *i2c_adap,
			   unsigned int data_width, unsigned int reg,
			   unsigned int *dest);
static int camera_i2c_write(struct i2c_adapter *i2c_adap,
			    unsigned int data_width, unsigned int reg,
			    unsigned int src);
static int camera_write_array(struct i2c_adapter *i2c_adap,
			      const struct regval_list *vals);

static struct camera_module_priv
*to_camera_priv(const struct i2c_client *client);

static int module_soft_reset(struct i2c_client *client);
static int module_save_exposure_param(struct v4l2_subdev *sd);

static int module_set_auto_white_balance(struct v4l2_subdev *sd,
					 struct v4l2_ctrl *ctrl);
static int module_set_white_balance_temperature(struct v4l2_subdev *sd,
						struct v4l2_ctrl *ctrl);
static int module_set_exposure_auto(struct v4l2_subdev *sd,
				    struct v4l2_ctrl *ctrl);
static int module_set_scene_exposure(struct v4l2_subdev *sd,
				     struct v4l2_ctrl *ctrl);

static int module_set_mbusformat(struct i2c_client *client,
				 const struct module_color_format *cfmt);
static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
static int module_set_mirror_flip(struct i2c_client *client, int mf);
static int module_set_color_format(int mf);
static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv);
static int module_set_stream(struct i2c_client *client, int enable);
static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
static int module_set_gain(struct i2c_client *client, int val);
static int module_get_gain(struct i2c_client *client, int *val);
static int module_get_exposure(struct i2c_client *client, int *val);
static int module_set_exposure(struct i2c_client *client, int val);
static int module_set_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
static int module_get_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
static int module_get_af_status(struct camera_module_priv *priv,
				struct v4l2_ctrl *ctrl);
static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware);
void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware);
static int module_set_power_line(struct v4l2_subdev *sd,
				 struct v4l2_ctrl *ctrl);
static int module_get_power_line(struct v4l2_subdev *sd,
				 struct v4l2_ctrl *ctrl);

static int module_start_aec(struct v4l2_subdev *sd);
static int module_freeze_aec(struct v4l2_subdev *sd);
static int module_set_exposure_param(struct v4l2_subdev *sd);
static int module_save_exposure_param(struct v4l2_subdev *sd);
#ifdef CAMERA_MODULE_WITH_MOTOR
static int motor_init(struct i2c_client *client);
static int motor_set_pos(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
static int motor_get_pos(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
static int motor_get_max_pos(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);
#endif
static int get_sensor_id(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl);

static void update_after_init(struct i2c_adapter *i2c_adap);
static void enter_preview_mode(struct i2c_adapter *i2c_adap);
static void enter_capture_mode(struct i2c_adapter *i2c_adap);
#endif	/*__MODULE_COMM_H__*/
