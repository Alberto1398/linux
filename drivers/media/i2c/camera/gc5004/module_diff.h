/*
 * module different macro
 *
 * Copyright (C) 2015 Actions Semiconductor Co.,LTD
 * Zhiquan Deng <dengzhiquan@actions-semi.com>
 *
 * Based on:
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MODULE_DIFF_H__
#define __MODULE_DIFF_H__

#define CAMERA_MODULE_WITH_MOTOR
#include "./../module_comm/module_comm.h"
#include "./../host_comm/owl_device.h"

#define CAMERA_MODULE_NAME "gc5004"
#define CAMERA_MODULE_PID 0x5004
#define VERSION(pid, ver) ((pid<<8)|(ver&0xFF))

#define MODULE_I2C_REAL_ADDRESS (0x6c>>1)
#define MODULE_I2C_REG_ADDRESS (0x6c>>1)
#define I2C_REGS_WIDTH 1
#define I2C_DATA_WIDTH 1

#define PIDH 0xf0		/* Product ID Number H byte */
#define PIDL 0xf1		/* Product ID Number L byte */
#define OUTTO_SENSO_CLOCK 24000000

#define DEFAULT_VSYNC_ACTIVE_LEVEL V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define DEFAULT_HSYNC_ACTIVE_LEVEL V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define DEFAULT_PCLK_SAMPLE_EDGE   V4L2_MBUS_PCLK_SAMPLE_RISING
#define DEFAULT_POWER_LINE_FREQUENCY V4L2_CID_POWER_LINE_FREQUENCY_50HZ

#define MODULE_DEFAULT_WIDTH  WIDTH_SVGA
#define MODULE_DEFAULT_HEIGHT HEIGHT_SVGA
#define MODULE_MAX_WIDTH      WIDTH_QSXGA
#define MODULE_MAX_HEIGHT     HEIGHT_QSXGA

#define AHEAD_LINE_NUM    15	/*10 lines = 50 cycles */
#define DROP_NUM_CAPTURE   2
#define DROP_NUM_PREVIEW   5

/************************************************/
/*#define CAMERA_MODULE_WITH_MOTOR*/
#define MOTOR_I2C_REAL_ADDRESS 0x0C
#define PD                       15
#define FLAG                     14

#define DIRECT_MODE
/*#define LINEAR_MODE*/
/*#define DUAL_LEVEL_MODE*/

#ifdef DIRECT_MODE
#define LINEAR_MODE_PROTECTION_OFF 0xECA3
#define LINEAR_MODE_DLC_DISABLE    0xA105
#define LINEAR_MODE_T_SRC_SETTING  0xF200
#define LINEAR_MODE_PROTECTION_ON  0xDC51
#endif

#ifdef LINEAR_MODE
#define LINEAR_MODE_PROTECTION_OFF 0xECA3
#define LINEAR_MODE_DLC_DISABLE    0xA105
#define LINEAR_MODE_T_SRC_SETTING  0xF200
#define LINEAR_MODE_PROTECTION_ON  0xDC51
#endif

#ifdef DUAL_LEVEL_MODE
#define LINEAR_MODE_PROTECTION_OFF 0xECA3
#define LINEAR_MODE_DLC_DISABLE    0xA10C
#define LINEAR_MODE_T_SRC_SETTING  0xF200
#define LINEAR_MODE_PROTECTION_ON  0xDC51
#endif
/************************************************/

/*Every sensor must set this value*/
#define USE_AS_FRONT 0
#define USE_AS_REAR 1

/*
 * MIPI CSI params settings
 */
static struct mipi_setting mipi_csi_setting = {
	.lan_num = 1,		/*0~3 */
	.contex0_en = 1,
	.contex0_virtual_num = 0,
	.contex0_data_type = MIPI_RAW10,
	/*MIPI_YUV422 MIPI_RAW8 MIPI_RAW10 MIPI_RAW12 */
	.clk_settle_time = 26,
	.clk_term_time = 6,
	.data_settle_time = 26,	/*28 */
	.data_term_time = 6,	/*6 */
	.crc_en = 1,
	.ecc_en = 1,
	.hclk_om_ent_en = 0,
	.lp11_not_chek = 0,
	.hsclk_edge = 0,	/*0: rising edge; 1: falling edge */
	.clk_lane_direction = 1,	/*0:obverse; 1:inverse */
	.lane0_map = 0,
	.lane1_map = 1,
	.lane2_map = 2,
	.lane3_map = 3,
	.mipi_en = 1,
	.csi_clk = 220000000,
};

/*
 * ISP interface params settings
 */
static struct host_module_setting_t module_setting = {
	.hs_pol = 1,		/*0: active low 1:active high */
	.vs_pol = 0,		/*0: active low 1:active high */
	.clk_edge = 0,		/*0: rasing edge 1:falling edge */
	.color_seq = COLOR_SEQ_UYVY,
	/*0: BG/GR, U0Y0V0Y1, 1: GR/BG, V0Y0U0Y1£»
	   2: GB/RG, Y0U0Y1V0, 3: RG/GB, Y0V0Y1U0 */
};

struct module_info camera_module_info = {
	.flags =
	    0 | SENSOR_FLAG_10BIT | SENSOR_FLAG_RAW | SENSOR_FLAG_MIPI |
	    SENSOR_FLAG_CHANNEL0,
	.mipi_cfg = &mipi_csi_setting,
	.module_cfg = &module_setting,
};

static struct module_color_format module_cfmts[] = {
	{
	 .code = V4L2_MBUS_FMT_SBGGR10_1X10,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 },

};

/*static unsigned int frame_rate_vga[] = { 30, };
static unsigned int frame_rate_svga[] = { 30, };
static unsigned int frame_rate_720p[] = { 30, };
static unsigned int frame_rate_uxga[] = { 30, };*/
static unsigned int frame_rate_972p[] = { 30, };
static unsigned int frame_rate_1080p[] = { 30, };
static unsigned int frame_rate_qsxga[] = { 30, };

static struct regval_list module_init_regs[] = {
	/*2592x1944 */
	/***************************************************/
	/*********************   SYS   *********************/
	/***************************************************/
	{1, 0xfe, 0x80},
	{1, 0xfe, 0x80},
	{1, 0xfe, 0x80},
	{1, 0xf2, 0x00},	/*sync_pad_io_ebi */
	{1, 0xf6, 0x00},
	{1, 0xfc, 0x06},
	{1, 0xf7, 0x1d},	/*Pll enable */
	{1, 0xf8, 0x84},	/*Pll mode 2 */
	{1, 0xf9, 0xfe},	/*[0] pll enable */
	{1, 0xfa, 0x00},	/*div */
	{1, 0xfe, 0x00},

	/***************************************************/
	/***************   ANALOG & CISCTL   ***************/
	/***************************************************/
	{1, 0x00, 0x40},	/*[4]rowskip_skip_sh */
	{1, 0x01, 0x10},	/* 20140324 txlow buffer */
	{1, 0x03, 0x06},
	{1, 0x04, 0xd6},
	{1, 0x05, 0x01},	/*HB,506 */
	{1, 0x06, 0xfa},
	{1, 0x07, 0x00},	/*VB,28 */
	{1, 0x08, 0x1c},
	{1, 0x0a, 0x02},	/*row start */
	{1, 0x0c, 0x00},	/*col start */
	{1, 0x0d, 0x07},	/*Window setting */
	{1, 0x0e, 0xa8},
	{1, 0x0f, 0x0a},
	{1, 0x10, 0x50},
	{1, 0x17, 0x15},	/*[0]mirror [1]flip */
	{1, 0x18, 0x02},	/*sdark off */
	{1, 0x19, 0x0c},
	{1, 0x1a, 0x13},
	{1, 0x1b, 0x48},
	{1, 0x1c, 0x05},
	{1, 0x1e, 0xb8},
	{1, 0x1f, 0x78},
	{1, 0x20, 0xc5},	/*[7:6]ref_r [3:1]comv_r */
	{1, 0x21, 0x4f},
	{1, 0x22, 0xb2},	/* 82 20140722 */
	{1, 0x23, 0x43},	/*[7:3]opa_r [1:0]sRef */
	{1, 0x24, 0x2f},	/*PAD drive */
	{1, 0x2b, 0x01},
	{1, 0x2c, 0x68},	/*[6:4]rsgh_r */

	/***************************************************/
	/*********************   ISP   *********************/
	/***************************************************/
	{1, 0x86, 0x0a},
	{1, 0x89, 0x03},
	{1, 0x8a, 0x83},
	{1, 0x8b, 0x61},
	{1, 0x8c, 0x10},
	{1, 0x8d, 0x01},
	{1, 0x90, 0x01},
	{1, 0x92, 0x00},	/*crop win y */
	{1, 0x94, 0x0d},	/*crop win x */
	{1, 0x95, 0x07},	/*crop win height */
	{1, 0x96, 0x98},
	{1, 0x97, 0x0a},	/*crop win width */
	{1, 0x98, 0x20},

	/***************************************************/
	/*********************   BLK   *********************/
	/***************************************************/
	{1, 0x40, 0x72},	/* 22 20140722 */
	{1, 0x41, 0x00},

	{1, 0x50, 0x00},
	{1, 0x51, 0x00},
	{1, 0x52, 0x00},
	{1, 0x53, 0x00},
	{1, 0x54, 0x00},
	{1, 0x55, 0x00},
	{1, 0x56, 0x00},
	{1, 0x57, 0x00},
	{1, 0x58, 0x00},
	{1, 0x59, 0x00},
	{1, 0x5a, 0x00},
	{1, 0x5b, 0x00},
	{1, 0x5c, 0x00},
	{1, 0x5d, 0x00},
	{1, 0x5e, 0x00},
	{1, 0x5f, 0x00},
	{1, 0xd0, 0x00},
	{1, 0xd1, 0x00},
	{1, 0xd2, 0x00},
	{1, 0xd3, 0x00},
	{1, 0xd4, 0x00},
	{1, 0xd5, 0x00},
	{1, 0xd6, 0x00},
	{1, 0xd7, 0x00},
	{1, 0xd8, 0x00},
	{1, 0xd9, 0x00},
	{1, 0xda, 0x00},
	{1, 0xdb, 0x00},
	{1, 0xdc, 0x00},
	{1, 0xdd, 0x00},
	{1, 0xde, 0x00},
	{1, 0xdf, 0x00},

	{1, 0x70, 0x00},
	{1, 0x71, 0x00},
	{1, 0x72, 0x00},
	{1, 0x73, 0x00},
	{1, 0x74, 0x20},
	{1, 0x75, 0x20},
	{1, 0x76, 0x20},
	{1, 0x77, 0x20},

	/***************************************************/
	/*********************   GAIN   ********************/
	/***************************************************/
	{1, 0xb0, 0x70},	/* 50 20140722 */
	{1, 0xb1, 0x01},
	{1, 0xb2, 0x02},
	{1, 0xb3, 0x40},
	{1, 0xb4, 0x40},
	{1, 0xb5, 0x40},
	{1, 0xb6, 0x00},

	/***************************************************/
	/*********************   DNDD   ********************/
	/***************************************************/
	{1, 0xfe, 0x02},
	{1, 0x89, 0x15},
	{1, 0xfe, 0x00},

	/***************************************************/
	/*********************   scalar   ******************/
	/***************************************************/
	{1, 0x18, 0x42},
	{1, 0x80, 0x18},	/*[4]first_dd_en;[3]scaler en */
	{1, 0x84, 0x23},	/*[5]auto_DD,[1:0]scaler CFA */
	{1, 0x87, 0x12},
	{1, 0xfe, 0x02},
	{1, 0x86, 0x00},
	{1, 0xfe, 0x00},
	{1, 0x95, 0x07},
	{1, 0x96, 0x98},
	{1, 0x97, 0x0a},
	{1, 0x98, 0x20},

	/***************************************************/
	/*********************   MIPI   ********************/
	/***************************************************/
	{1, 0xfe, 0x03},
	{1, 0x01, 0x07},
	{1, 0x02, 0x33},	/*33 */
	{1, 0x03, 0x13},	/*93 */
	{1, 0x04, 0x80},
	{1, 0x05, 0x02},
	{1, 0x06, 0x80},
	{1, 0x11, 0x2b},
	{1, 0x12, 0xa8},
	{1, 0x13, 0x0c},
	{1, 0x15, 0x10},	/*10, // 12 20140722 */
	{1, 0x17, 0xb0},
	{1, 0x18, 0x00},
	{1, 0x19, 0x00},
	{1, 0x1a, 0x00},
	{1, 0x1d, 0x00},
	{1, 0x42, 0x20},
	{1, 0x43, 0x0a},

	{1, 0x21, 0x03},
	{1, 0x22, 0x03},
	{1, 0x23, 0x20},
	{1, 0x29, 0x03},
	{1, 0x2a, 0x08},
	{1, 0x10, 0x91},	/* 93 20140722 */
	{1, 0xfe, 0x00},

	ENDMARKER,
};

/* 640*480: VGA*/
static const struct regval_list module_vga_regs[] = {

	ENDMARKER,
};

/* 800*600: SVGA*/
static const struct regval_list module_svga_regs[] = {

	ENDMARKER,
};

/* 1280*720: 720P*/
static const struct regval_list module_720p_regs[] = {
	ENDMARKER,
};

/* 1600*1200: UXGA */
static const struct regval_list module_uxga_regs[] = {
	ENDMARKER,
};

/* 1920*1080: 1080P*/
static const struct regval_list module_1080p_regs[] = {
	/*1920x1080 */
	{1, 0x18, 0x02},	/*skip off */
	{1, 0x80, 0x10},	/*scaler off */

	{1, 0x05, 0x01},	/*HB,266 */
	{1, 0x06, 0x0a},
	{1, 0x09, 0x01},
	{1, 0x0a, 0xb0},	/*row start */
	{1, 0x0b, 0x01},
	{1, 0x0c, 0x10},	/*col start */
	{1, 0x0d, 0x04},	/*window height,1096 */
	{1, 0x0e, 0x48},
	{1, 0x0f, 0x07},	/*window width,2000 */
	{1, 0x10, 0xd0},
	{1, 0x4e, 0x00},	/*blk 20140722 */
	{1, 0x4f, 0x3c},

	{1, 0x17, 0x15},

	{1, 0x94, 0x0d},
	{1, 0x95, 0x04},
	{1, 0x96, 0x38},
	{1, 0x97, 0x07},
	{1, 0x98, 0x80},

	{1, 0xfe, 0x03},
	{1, 0x04, 0xe0},
	{1, 0x05, 0x01},
	{1, 0x12, 0x60},
	{1, 0x13, 0x09},
	{1, 0x42, 0x80},
	{1, 0x43, 0x07},
	{1, 0xfe, 0x00},

	ENDMARKER,
};

/* 1296*972: QSXGA*/
static const struct regval_list module_972p_regs[] = {
	/*1296x972 */
	{1, 0x18, 0x42},	/*skip on */
	{1, 0x80, 0x18},	/*scaler en */

	{1, 0x05, 0x03}, /*HB*/ {1, 0x06, 0x26},
	{1, 0x07, 0x03}, /*VB*/ {1, 0x08, 0x1c},
	{1, 0x09, 0x00},
	{1, 0x0a, 0x03},	/*row start */
	{1, 0x0b, 0x00},
	{1, 0x0c, 0x00},	/*col start */
	{1, 0x0d, 0x07},
	{1, 0x0e, 0xa8},
	{1, 0x0f, 0x0a},	/*Window setting */
	{1, 0x10, 0x50},
	{1, 0x4e, 0x00},	/*blk 20140722 */
	{1, 0x4f, 0x06},

	{1, 0x17, 0x35},

	{1, 0x94, 0x07},
	{1, 0x95, 0x03},
	{1, 0x96, 0xcc},
	{1, 0x97, 0x05},
	{1, 0x98, 0x10},

	{1, 0xfe, 0x03},
	{1, 0x04, 0x40},
	{1, 0x05, 0x01},
	{1, 0x12, 0x54},
	{1, 0x13, 0x06},
	{1, 0x42, 0x10},
	{1, 0x43, 0x05},
	{1, 0xfe, 0x00},

	ENDMARKER,
};

/* 2592*1944: QSXGA*/
static const struct regval_list module_qsxga_regs[] = {
	/*2592x1944 */
	{1, 0x18, 0x02},	/*skip off */
	{1, 0x80, 0x10},	/*scaler off */

	{1, 0x05, 0x03}, /*HB*/ {1, 0x06, 0x26},
	{1, 0x09, 0x00},
	{1, 0x0a, 0x02},	/*row start */
	{1, 0x0b, 0x00},
	{1, 0x0c, 0x00},	/*col start */
	{1, 0x0d, 0x07},
	{1, 0x0e, 0xa8},
	{1, 0x0f, 0x0a},	/*Window setting */
	{1, 0x10, 0x50},
	{1, 0x4e, 0x00},	/*blk 20140722 */
	{1, 0x4f, 0x3c},

	{1, 0x17, 0x15},

	{1, 0x94, 0x0d},
	{1, 0x95, 0x07},
	{1, 0x96, 0x98},
	{1, 0x97, 0x0a},
	{1, 0x98, 0x20},

	{1, 0xfe, 0x03},
	{1, 0x04, 0x80},
	{1, 0x05, 0x02},
	{1, 0x12, 0xa8},
	{1, 0x13, 0x0c},
	{1, 0x42, 0x20},
	{1, 0x43, 0x0a},
	{1, 0xfe, 0x00},
	ENDMARKER,
};

static const struct regval_list module_init_auto_focus[] = {
	ENDMARKER,
};

/*
 * window size list
 */

/* 640X480 */
/*
static struct camera_module_win_size module_win_vga = {
	.name = "VGA",
	.width = WIDTH_VGA,
	.height = HEIGHT_VGA,
	.win_regs = module_vga_regs,
	.frame_rate_array = frame_rate_vga,
	.capture_only = 0,
};
*/

/* 800X600 */
/*
static struct camera_module_win_size module_win_svga = {
	.name = "SVGA",
	.width = WIDTH_SVGA,
	.height = HEIGHT_SVGA,
	.win_regs = module_svga_regs,
	.frame_rate_array = frame_rate_svga,
	.capture_only = 0,
};
*/

/* 1280X720 */
/*
static struct camera_module_win_size module_win_720p = {
	.name = "720P",
	.width = WIDTH_720P,
	.height = HEIGHT_720P,
	.win_regs = module_720p_regs,
	.frame_rate_array = frame_rate_720p,
	.capture_only = 0,
};
*/

/* 1600X1200 */
/*
static struct camera_module_win_size module_win_uxga = {
	.name = "UXGA",
	.width = WIDTH_UXGA,
	.height = HEIGHT_UXGA,
	.win_regs = module_uxga_regs,
	.frame_rate_array = frame_rate_uxga,
	.capture_only = 0,
};
*/

/* 1920X1080 */
static struct camera_module_win_size module_win_1080p = {
	.name = "1080P",
	.width = WIDTH_1080P,
	.height = HEIGHT_1080P,
	.win_regs = module_1080p_regs,
	.frame_rate_array = frame_rate_1080p,
	.capture_only = 0,
};

/* 1296X972 */
static struct camera_module_win_size module_win_972p = {
	.name = "972P",
	.width = 1296,
	.height = 972,
	.win_regs = module_972p_regs,
	.frame_rate_array = frame_rate_972p,
	.capture_only = 0,
};

/* 2592X1944 */
static struct camera_module_win_size module_win_qsxga = {
	.name = "QSXGA",
	.width = WIDTH_QSXGA,
	.height = HEIGHT_QSXGA,
	.win_regs = module_qsxga_regs,
	.frame_rate_array = frame_rate_qsxga,
	.capture_only = 1,
};

static struct camera_module_win_size *module_win_list[] = {
	&module_win_972p,
	&module_win_qsxga,
	&module_win_1080p,
};

/****************** Manual AWB Setting ******************/
/*
static struct regval_list module_whitebance_auto_regs[] = {

	ENDMARKER,
};
*/

/* Cloudy Colour Temperature : 6500K - 8000K  */
/*
static struct regval_list module_whitebance_cloudy_regs[] = {

	ENDMARKER,
};
*/

/* ClearDay Colour Temperature : 5000K - 6500K  */
/*
static struct regval_list module_whitebance_sunny_regs[] = {

	ENDMARKER,
};
*/

/* Office Colour Temperature : 3500K - 5000K ,Ó«¹âµÆ */
/*
static struct regval_list module_whitebance_fluorescent_regs[] = {

	ENDMARKER,
};
*/

/* Home Colour Temperature : 2500K - 3500K £¬°×³ãµÆ */
/*
static struct regval_list module_whitebance_incandescent_regs[] = {

	ENDMARKER,
};
*/

/****************** Image Effect Setting ******************/
/*
static struct regval_list module_effect_normal_regs[] = {

	ENDMARKER,
};
*/

/*
static struct regval_list module_effect_white_black_regs[] = {

	ENDMARKER,
};
*/

/*
static struct regval_list module_effect_negative_regs[] = {

	ENDMARKER,
};
*/

/*
static struct regval_list module_effect_antique_regs[] = {

	ENDMARKER,
};
*/

/*
static struct regval_list module_scene_auto_regs[] = {

	ENDMARKER,
};
*/

/*
static struct regval_list module_scene_night_regs[] = {

	ENDMARKER,
};
*/

static struct v4l2_ctl_cmd_info v4l2_ctl_array[] = {
	{.id = V4L2_CID_GAIN,
	 .min = 64,
	 .max = 3942,
	 .step = 1,
	 .def = 64,},

	{.id = V4L2_CID_EXPOSURE,
	 .min = 1,
	 .max = 0x1fff,
	 .step = 1,
	 .def = 2608,},

	{.id = V4L2_CID_AUTO_WHITE_BALANCE,
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 1,},

	{.id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
	 .min = 0,
	 .max = 3,
	 .step = 1,
	 .def = 1,},

	/*{ .id = V4L2_CID_SCENE_EXPOSURE,
	   .min = 0,
	   .max = 1,
	   .step = 1,
	   .def = 0,}, */

	/*{ .id = V4L2_CID_PRIVATE_PREV_CAPT,
	   .min = 0,
	   .max = 1,
	   .step = 1,
	   .def = PREVIEW_MODE,}, */

	{.id = V4L2_CID_AF_MODE,
	 .min = NONE_AF,
	 .max = CONTINUE_AF | SINGLE_AF,
	 .step = 1,
	 .def = CONTINUE_AF | SINGLE_AF,},

	{.id = V4L2_CID_AF_STATUS,
	 .min = AF_STATUS_DISABLE,
	 .max = AF_STATUS_FAIL,
	 .step = 1,
	 .def = AF_STATUS_DISABLE,},

	{.id = V4L2_CID_MOTOR,
	 .min = 0,
	 .max = 0x3ff,
	 .step = 1,
	 .def = 0,},

	{.id = V4L2_CID_MOTOR_GET_MAX,
	 .min = 0,
	 .max = 0x3ff,
	 .step = 1,
	 .def = 0,},

#if 0
	{.id = V4L2_CID_FLASH_STROBE,
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 0,},

	{.id = V4L2_CID_FLASH_STROBE_STOP,
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 0,},
#endif

	{.id = V4L2_CID_SENSOR_ID,
	 .min = 0,
	 .max = 0xffffff,
	 .step = 1,
	 .def = 0,},
};

static struct v4l2_ctl_cmd_info_menu v4l2_ctl_array_menu[] = {
	{.id = V4L2_CID_COLORFX,
	 .max = 3,
	 .mask = 0x0,
	 .def = 0,},

	{.id = V4L2_CID_EXPOSURE_AUTO,
	 .max = 1,
	 .mask = 0x0,
	 .def = 1,},

	{
	 .id = V4L2_CID_POWER_LINE_FREQUENCY,
	 .max = V4L2_CID_POWER_LINE_FREQUENCY_AUTO,
	 .mask = 0x0,
	 .def = V4L2_CID_POWER_LINE_FREQUENCY_AUTO,},

#if 0
	{.id = V4L2_CID_FLASH_LED_MODE,
	 .max = 3,
	 .mask = 0x0,
	 .def = 0,},
#endif
};

#endif				/* __MODULE_DIFF_H__ */
