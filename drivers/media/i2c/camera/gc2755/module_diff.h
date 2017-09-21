/*
 * module different macro
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

#include "./../module_comm/module_comm.h"
#include "./../host_comm/owl_device.h"

#define CAMERA_MODULE_NAME		"gc2755"
#define CAMERA_MODULE_PID		0x2655
#define VERSION(pid, ver)		((pid<<8)|(ver&0xFF))

#define MODULE_I2C_REAL_ADDRESS		(0x78>>1)
#define MODULE_I2C_REG_ADDRESS		(0x78>>1)
#define I2C_REGS_WIDTH				1
#define I2C_DATA_WIDTH				1

#define PIDH			0xf0	/* Product ID Number H byte */
#define PIDL			0xf1	/* Product ID Number L byte */
#define OUTTO_SENSO_CLOCK		24000000

#define DEFAULT_VSYNC_ACTIVE_LEVEL	V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define DEFAULT_HSYNC_ACTIVE_LEVEL	V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define DEFAULT_PCLK_SAMPLE_EDGE	V4L2_MBUS_PCLK_SAMPLE_RISING
#define DEFAULT_POWER_LINE_FREQUENCY	V4L2_CID_POWER_LINE_FREQUENCY_50HZ

#define MODULE_DEFAULT_WIDTH	WIDTH_SVGA
#define MODULE_DEFAULT_HEIGHT	HEIGHT_SVGA
#define MODULE_MAX_WIDTH		WIDTH_1080P
#define MODULE_MAX_HEIGHT		HEIGHT_1080P

#define AHEAD_LINE_NUM			15	/*10 lines= 50 resycles */
#define DROP_NUM_CAPTURE		2
#define DROP_NUM_PREVIEW		5

/*Every sensor must set this value*/
#define USE_AS_FRONT 1
#define USE_AS_REAR 0

/*
MIPI CSI params setting
*/
static struct mipi_setting mipi_csi_setting = {
	.lan_num = 1,		/*0~3 */
	.contex0_en = 1,
	.contex0_virtual_num = 0,
	/*MIPI_YUV422 MIPI_RAW8 MIPI_RAW10 MIPI_RAW12 */
	.contex0_data_type = MIPI_RAW10,
	.clk_settle_time = 2,
	.clk_term_time = 1,	/*    .data_settle_time = 15,  //8 */
	.data_settle_time = 15,	/*8 */
	.data_term_time = 5,	/*6 */
	.crc_en = 1,
	.ecc_en = 1,
	.hclk_om_ent_en = 1,
	.lp11_not_chek = 0,
	.hsclk_edge = 0,	/*0: rising edge; 1: falling edge */
	.clk_lane_direction = 0,	/*0:obverse; 1:inverse */
	.lane0_map = 0,
	.lane1_map = 1,
	.lane2_map = 2,
	.lane3_map = 3,
	.mipi_en = 1,
	.csi_clk = 220000000,
};

/*
ISP interface params settings
*/
static struct host_module_setting_t module_setting = {
	.hs_pol = 1,		/*0: active low 1:active high */
	.vs_pol = 0,		/*0: active low 1:active high */
	.clk_edge = 0,		/*0: rasing edge 1:falling edge */
	/*0: BG/GR, U0Y0V0Y1, 1: GR/BG, V0Y0U0Y1,
	*2: GB/RG, Y0U0Y1V0, 3: RG/GB, Y0V0Y1U0 */
	.color_seq = COLOR_SEQ_UYVY,
};

struct module_info camera_module_info = {
	.flags = 0
	    | SENSOR_FLAG_10BIT
	    | SENSOR_FLAG_RAW | SENSOR_FLAG_MIPI | SENSOR_FLAG_CHANNEL1,
	.mipi_cfg = &mipi_csi_setting,
	.module_cfg = &module_setting,
};

/*
 * supported color format list.
 * see definition in
 *     http://thread.gmane.org/gmane.linux.drivers.
 *video-input-infrastructure/12830/focus=13394
 * YUYV8_2X8_LE == YUYV with LE packing
 * YUYV8_2X8_BE == UYVY with LE packing
 * YVYU8_2X8_LE == YVYU with LE packing
 * YVYU8_2X8_BE == VYUY with LE packing
 */
static struct module_color_format module_cfmts[] = {
	{
	 .code = V4L2_MBUS_FMT_SGBRG10_1X10,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 },

};

static unsigned int frame_rate_1080p[] = { 30, };

static struct regval_list module_init_regs[] = {

	/*****************************************************/
	/*********************  SYS  **************************/
	/*****************************************************/
	{1, 0xfe, 0x80},
	{1, 0xfe, 0x80},
	{1, 0xfe, 0x80},
	{1, 0xf6, 0x00},
	{1, 0xf7, 0x31},
	{1, 0xf8, 0x06},
	{1, 0xf9, 0x2e},
	{1, 0xfa, 0x00},
	{1, 0xfc, 0x3e},
	{1, 0xfe, 0x00},

	/*****************************************************/
	/****************** ANALOG & CISCTL ******************/
	/*****************************************************/
	{1, 0x17, 0x15},	/*mirro and filp */
	{1, 0x03, 0x03},
	{1, 0x04, 0xed},
	{1, 0x05, 0x03},
	{1, 0x06, 0x12},
	{1, 0x07, 0x00},
	{1, 0x08, 0x49},
	{1, 0x0a, 0x00},
	{1, 0x0c, 0x04},
	{1, 0x0d, 0x04},
	{1, 0x0e, 0x48},
	{1, 0x0f, 0x07},
	{1, 0x10, 0x90},
	{1, 0x11, 0x00},
	{1, 0x12, 0x0e},
	{1, 0x13, 0x11},
	{1, 0x14, 0x01},
	{1, 0x19, 0x08},
	{1, 0x1b, 0x4f},
	{1, 0x1c, 0x11},
	{1, 0x1d, 0x10},
	{1, 0x1e, 0xcc},
	{1, 0x1f, 0xc9},
	{1, 0x20, 0x71},
	{1, 0x21, 0x20},
	{1, 0x22, 0xd0},
	{1, 0x23, 0x51},
	{1, 0x24, 0x19},
	{1, 0x27, 0x20},
	{1, 0x28, 0x00},
	{1, 0x2b, 0x81},
	{1, 0x2c, 0x38},
	{1, 0x2e, 0x1f},
	{1, 0x2f, 0x14},
	{1, 0x30, 0x00},
	{1, 0x31, 0x01},
	{1, 0x32, 0x02},
	{1, 0x33, 0x03},
	{1, 0x34, 0x07},
	{1, 0x35, 0x0b},
	{1, 0x36, 0x0f},

	/*****************************************************/
	/**********************   gain   ************************/
	/*****************************************************/
	{1, 0xb0, 0x56},
	{1, 0xb1, 0x01},
	{1, 0xb2, 0x00},
	{1, 0xb3, 0x40},
	{1, 0xb4, 0x40},
	{1, 0xb5, 0x40},
	{1, 0xb6, 0x00},

	/*****************************************************/
	/**********************   crop   **********************/
	/*****************************************************/
	{1, 0x92, 0x07},
	{1, 0x94, 0x08},
	{1, 0x95, 0x04},
	{1, 0x96, 0x38},
	{1, 0x97, 0x07},
	{1, 0x98, 0x80},	/*out window set 1920x1080 */

	/*****************************************************/
	/**********************BLK  **************************/
	/*****************************************************/
	{1, 0x18, 0x12},
	{1, 0x1a, 0x01},
	{1, 0x40, 0x42},
	{1, 0x41, 0x00},
	{1, 0x4e, 0x3c},
	{1, 0x4f, 0x00},
	{1, 0x5e, 0x00},
	{1, 0x66, 0x20},
	{1, 0x6a, 0x00},
	{1, 0x6b, 0x00},
	{1, 0x6c, 0x00},
	{1, 0x6d, 0x00},
	{1, 0x6e, 0x00},
	{1, 0x6f, 0x00},
	{1, 0x70, 0x00},
	{1, 0x71, 0x00},

	/*****************************************************/
	/***********************dark sun **********************/
	/*****************************************************/
	{1, 0x87, 0x03},
	{1, 0xe5, 0x27},
	{1, 0xe7, 0x53},
	{1, 0xe8, 0xff},
	{1, 0xe9, 0x3f},

	/*****************************************************/
	/********************** MIPI **************************/
	/*****************************************************/
	{1, 0xfe, 0x03},
	{1, 0x01, 0x87},
	{1, 0x02, 0x00},
	{1, 0x03, 0x10},
	{1, 0x04, 0x01},
	{1, 0x05, 0x00},
	{1, 0x06, 0xa2},
	{1, 0x10, 0x91},
	{1, 0x11, 0x2b},
	{1, 0x12, 0x60},
	{1, 0x13, 0x09},
	{1, 0x15, 0x62},
	{1, 0x20, 0x40},
	{1, 0x21, 0x10},
	{1, 0x22, 0x02},
	{1, 0x23, 0x20},
	{1, 0x24, 0x02},
	{1, 0x25, 0x10},
	{1, 0x26, 0x04},
	{1, 0x27, 0x06},
	{1, 0x29, 0x02},
	{1, 0x2a, 0x08},
	{1, 0x2b, 0x04},
	{1, 0xfe, 0x00},
	ENDMARKER,
};

/* 1920*1080: 1080P*/
static const struct regval_list module_1080p_regs[] = {
	ENDMARKER,
};

static const struct regval_list module_init_auto_focus[] = {
	ENDMARKER,
};

/*
 * window size list
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

static struct camera_module_win_size *module_win_list[] = {
	/***&module_win_vga,
	***&module_win_svga,
	***&module_win_720p,
	***&module_win_uxga,***/
	&module_win_1080p,
};

static struct v4l2_ctl_cmd_info v4l2_ctl_array[] = {
	{
	 .id = V4L2_CID_GAIN,
	 .min = 64,
	 .max = 64 * 32,
	 .step = 1,
	 .def = 64,},

	{
	 .id = V4L2_CID_EXPOSURE,
	 .min = 1,
	 .max = 0xffff,
	 .step = 1,
	 .def = 2560,},

	{
	 .id = V4L2_CID_AUTO_WHITE_BALANCE,
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 1,},
	{
	 .id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
	 .min = 0,
	 .max = 3,
	 .step = 1,
	 .def = 1,},
	{
	 .id = V4L2_CID_SENSOR_ID,
	 .min = 0,
	 .max = 0xffffff,
	 .step = 1,
	 .def = 0,},
};

static struct v4l2_ctl_cmd_info_menu v4l2_ctl_array_menu[] = {
	{
	 .id = V4L2_CID_COLORFX,
	 .max = 3,
	 .mask = 0x0,
	 .def = 0,},
	{
	 .id = V4L2_CID_EXPOSURE_AUTO,
	 .max = 1,
	 .mask = 0x0,
	 .def = 1,},
	{
	 .id = V4L2_CID_POWER_LINE_FREQUENCY,
	 .max = V4L2_CID_POWER_LINE_FREQUENCY_AUTO,
	 .mask = 0x0,
	 .def = V4L2_CID_POWER_LINE_FREQUENCY_AUTO,},
};

#endif				/* __MODULE_DIFF_H__ */
