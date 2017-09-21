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
#define CAMERA_MODULE_WITH_MOTOR

#include "./../module_comm/module_comm.h"
#include "./../host_comm/owl_device.h"

#define CAMERA_MODULE_NAME		"imx0219"
#define CAMERA_MODULE_PID		0x0219
#define VERSION(pid, ver)		((pid<<8)|(ver&0xFF))

#define MOTOR_I2C_REAL_ADDRESS		0x0C
#define PD							15
#define FLAG						14

#define DIRECT_MODE
/*#define LINEAR_MODE
#define DUAL_LEVEL_MODE*/

#ifdef DIRECT_MODE
#define LINEAR_MODE_PROTECTION_OFF	0xECA3
#define LINEAR_MODE_DLC_DISABLE		0xA105
#define LINEAR_MODE_T_SRC_SETTING	0xF200
#define LINEAR_MODE_PROTECTION_ON	0xDC51
#endif

#ifdef LINEAR_MODE
#define LINEAR_MODE_PROTECTION_OFF	0xECA3
#define LINEAR_MODE_DLC_DISABLE		0xA105
#define LINEAR_MODE_T_SRC_SETTING	0xF200
#define LINEAR_MODE_PROTECTION_ON	0xDC51
#endif

#ifdef DUAL_LEVEL_MODE
#define LINEAR_MODE_PROTECTION_OFF	0xECA3
#define LINEAR_MODE_DLC_DISABLE		0xA10C
#define LINEAR_MODE_T_SRC_SETTING	0xF200
#define LINEAR_MODE_PROTECTION_ON	0xDC51
#endif

#define MODULE_I2C_REAL_ADDRESS		(0x20>>1)
#define MODULE_I2C_REG_ADDRESS		(0x20>>1)
#define I2C_REGS_WIDTH				2
#define I2C_DATA_WIDTH				1

#define PIDH				0x0000	/* Product ID Number H byte */
#define PIDL				0x0001	/* Product ID Number L byte */
#define OUTTO_SENSO_CLOCK	24000000

#define DEFAULT_VSYNC_ACTIVE_LEVEL		V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define DEFAULT_HSYNC_ACTIVE_LEVEL		V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define DEFAULT_PCLK_SAMPLE_EDGE		V4L2_MBUS_PCLK_SAMPLE_RISING
#define DEFAULT_POWER_LINE_FREQUENCY	V4L2_CID_POWER_LINE_FREQUENCY_50HZ

#define MODULE_DEFAULT_WIDTH	WIDTH_SVGA
#define MODULE_DEFAULT_HEIGHT	HEIGHT_SVGA
#define MODULE_MAX_WIDTH		WIDTH_QUXGA
#define MODULE_MAX_HEIGHT		HEIGHT_QUXGA

#define AHEAD_LINE_NUM			15	/*10 lines = 50 cycles */
#define DROP_NUM_CAPTURE		0
#define DROP_NUM_PREVIEW		0

/*Every sensor must set this value*/
#define USE_AS_FRONT 0
#define USE_AS_REAR 1

/*
MIPI SENSOR params settings
*/
static struct mipi_setting mipi_sensor_setting = {
	.lan_num = 3,		/*the number of lane */
	.contex0_en = 1,
	.contex0_virtual_num = 0,
	/*MIPI_YUV422 MIPI_RAW8 MIPI_RAW10 MIPI_RAW12 */
	.contex0_data_type = MIPI_RAW10,
	.clk_settle_time = 16,
	.clk_term_time = 3,	/*data_settle_time = 16,  */
	.data_settle_time = 16,	/*3 */
	.data_term_time = 3,	/*crc_en = 1, */
	.crc_en = 1,
	.ecc_en = 1,
	.hclk_om_ent_en = 0,
	.lp11_not_chek = 0,
	.hsclk_edge = 0,	/*0: rising edge; 1: falling edge */
	.clk_lane_direction = 0,	/*0:obverse; 1:inverse */
	.lane0_map = 0,
	.lane1_map = 1,
	.lane2_map = 2,
	.lane3_map = 3,
	.mipi_en = 1,
	.csi_clk = 330000000,
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
	.flags = SENSOR_FLAG_CHANNEL0
	    | SENSOR_FLAG_MIPI | SENSOR_FLAG_RAW | SENSOR_FLAG_10BIT,
	.mipi_cfg = &mipi_sensor_setting,
	.module_cfg = &module_setting,
};

static unsigned int frame_rate_uxga[] = { 15, };
static unsigned int frame_rate_quxga[] = { 15, };

/*
 * initial setting
 */
static const struct regval_list module_init_regs[] = {
	ENDMARKER,
};

/* 800*600: SVGA*/
static const struct regval_list module_svga_regs[] = {
	/* Reset for operation ...
	   {0x0103, 0x01}, soft reset
	   {0x0103, 0x00}, streaming off */

	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x0c},
	{1, 0x300a, 0xff},
	{1, 0x300b, 0xff},
	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x09},

	{1, 0x0114, 0x03},	/*!3:4 lane   1:2 lane */

	{1, 0x0128, 0x00},	/*phy_contrl */
	{1, 0x012A, 0x18},	/*exck_freq */
	{1, 0x012B, 0x00},	/*exck_freq */

	{1, 0x0160, 0x06},	/*frame lenth 15:8  */
	{1, 0x0161, 0x40},	/*frame lenth 7:0   */
	{1, 0x0162, 0x0d},	/*line lenth 15:8   */
	{1, 0x0163, 0xe8},	/*line lenth 7:0    */
	{1, 0x0164, 0x00},	/*x_start 11:8      */
	{1, 0x0165, 0x00},	/*x_start 7:0       */
	{1, 0x0166, 0x0c},	/*x_end 11:8        */
	{1, 0x0167, 0xcf},	/*x_end 7:0         */
	{1, 0x0168, 0x00},	/*y_start 11:8      */
	{1, 0x0169, 0x00},	/*y_start   7:0     */
	{1, 0x016A, 0x09},	/*y_end 11:8        */
	{1, 0x016B, 0x9f},	/*y_end 7:0         */
	{1, 0x016C, 0x03},	/*x output size 11:8 */
	{1, 0x016D, 0x20},	/*x output size 7:0 */
	{1, 0x016E, 0x02},	/*y output size 11:8 */
	{1, 0x016F, 0x58},	/*y output size 7:0 */

	{1, 0x0170, 0x01},	/*x inc               */
	{1, 0x0171, 0x01},	/*y inc               */
	{1, 0x0172, 0x03},	/*mirror              */
	{1, 0x0174, 0x02},	/*h binning 1:x2 2:x4 */
	{1, 0x0175, 0x02},	/*v binning 1:x2 2:x4 */

	{1, 0x018C, 0x0a},	/*csi data format 15:8 */
	{1, 0x018D, 0x0a},	/*csi data format 7:0 */

	{1, 0x0301, 0x05},	/*!!!vtpxck_div=5 4lane a:2lane  */
	{1, 0x0303, 0x01},	/*vtsyck_div   */
	{1, 0x0304, 0x03},	/*prepllck_vt_div */
	{1, 0x0305, 0x03},	/*repllck_op_div */
	{1, 0x0306, 0x00},	/*pll_vt_mpy 10:8 */
	/*pll_vt_mpy 7:0  0x57 13: 20M pixel clk   18:28.8M */
	{1, 0x0307, 0x4e},
	{1, 0x0309, 0x0a},	/*oppxck_div normal 0xa */
	{1, 0x030B, 0x01},	/*opsyck_div   */
	{1, 0x030C, 0x00},	/*pll_op_mpy 10:8 */
	{1, 0x030D, 0x50},	/*pll_op_mpy 7:0 0x5a 90 720Mbps   15:120M   */

	{1, 0x4767, 0x0F},	/*cis tuning */
	{1, 0x4750, 0x14},	/*cis tuning */
	{1, 0x47B4, 0x14},	/*cis tuning */

	/*{0x0100, 0x01}, //mode select, stream on */

	ENDMARKER,
};

	/* 1280*720: 720P */
static const struct regval_list module_720p_regs[] = {
	/* Reset for operation ...       */
	/*{0x0103, 0x01}, //soft reset   */
	/*{0x0103, 0x00}, //streaming off */

	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x0c},
	{1, 0x300a, 0xff},
	{1, 0x300b, 0xff},
	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x09},

	{1, 0x0114, 0x03},	/*!3:4 lane   1:2 lane */

	{1, 0x0128, 0x00},	/*phy_contrl */
	{1, 0x012A, 0x18},	/*exck_freq */
	{1, 0x012B, 0x00},	/*exck_freq */

	{1, 0x0160, 0x0a},	/*frame lenth 15:8  */
	{1, 0x0161, 0xc4},	/*frame lenth 7:0   */
	{1, 0x0162, 0x0d},	/*line lenth 15:8   */
	{1, 0x0163, 0x78},	/*line lenth 7:0    */
	{1, 0x0164, 0x01},	/*x_start 11:8      */
	{1, 0x0165, 0x68},	/*x_start 7:0       */
	{1, 0x0166, 0x0b},	/*x_end 11:8        */
	{1, 0x0167, 0x67},	/*x_end 7:0         */
	{1, 0x0168, 0x02},	/*y_start 11:8      */
	{1, 0x0169, 0x00},	/*y_start   7:0     */
	{1, 0x016A, 0x07},	/*y_end 11:8        */
	{1, 0x016B, 0x9f},	/*y_end 7:0         */
	{1, 0x016C, 0x05},	/*x output size 11:8 */
	{1, 0x016D, 0x00},	/*x output size 7:0 */
	{1, 0x016E, 0x02},	/*y output size 11:8 */
	{1, 0x016F, 0xd0},	/*y output size 7:0 */

	{1, 0x0170, 0x01},	/*x inc              */
	{1, 0x0171, 0x01},	/*y inc              */
	{1, 0x0172, 0x03},	/*mirror             */
	{1, 0x0174, 0x01},	/*h binning 1:x2 2:x4 */
	{1, 0x0175, 0x01},	/*v binning 1:x2 2:x4 */

	{1, 0x018C, 0x0a},	/*csi data format 15:8 */
	{1, 0x018D, 0x0a},	/*csi data format 7:0 */

	{1, 0x0301, 0x05},	/*!!vtpxck_div=5 4lane a:2lane  */
	{1, 0x0303, 0x01},	/*vtsyck_div  */
	{1, 0x0304, 0x03},	/*prepllck_vt_div   */
	{1, 0x0305, 0x03},	/*repllck_op_div  */
	{1, 0x0306, 0x00},	/*pll_vt_mpy 10:8 */
	/*pll_vt_mpy 7:0  0x57 13: 20M pixel clk   18:28.8M */
	{1, 0x0307, 0x50},
	{1, 0x0309, 0x0a},	/*oppxck_div normal 0xa   */
	{1, 0x030B, 0x01},	/*opsyck_div */
	{1, 0x030C, 0x00},	/*pll_op_mpy 10:8 */
	{1, 0x030D, 0x4c},	/*pll_op_mpy 7:0 0x5a 90 720Mbps   15:120M  */

	{1, 0x4767, 0x0F},	/*cis tuning */
	{1, 0x4750, 0x14},	/*cis tuning */
	{1, 0x47B4, 0x14},	/*cis tuning */

	/*{0x0100, 0x01}, //mode select, stream on */
	ENDMARKER,
};

/* 1920*1080: 1080P*/
static const struct regval_list module_1080p_regs[] = {
	/* Reset for operation ...       */
	/*{0x0103, 0x01}, //soft reset   */
	/*{0x0103, 0x00}, //streaming off */

	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x0c},
	{1, 0x300a, 0xff},
	{1, 0x300b, 0xff},
	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x09},

	{1, 0x0114, 0x03},	/*!!3:4 lane   1:2 lane */

	{1, 0x0128, 0x00},	/*phy_contrl */
	{1, 0x012A, 0x18},	/*exck_freq */
	{1, 0x012B, 0x00},	/*exck_freq */

	{1, 0x0160, 0x0a},	/*frame lenth 15:8  */
	{1, 0x0161, 0x82},	/*frame lenth 7:0   */
	{1, 0x0162, 0x0d},	/*line lenth 15:8   */
	{1, 0x0163, 0x78},	/*line lenth 7:0    */
	{1, 0x0164, 0x02},	/*x_start 11:8      */
	{1, 0x0165, 0xa8},	/*x_start 7:0       */
	{1, 0x0166, 0x0a},	/*x_end 11:8        */
	{1, 0x0167, 0x27},	/*x_end 7:0         */
	{1, 0x0168, 0x02},	/*y_start 11:8      */
	{1, 0x0169, 0xb4},	/*y_start   7:0     */
	{1, 0x016A, 0x06},	/*y_end 11:8        */
	{1, 0x016B, 0xeb},	/*y_end 7:0         */
	{1, 0x016C, 0x07},	/*x output size 11:8 */
	{1, 0x016D, 0x80},	/*x output size 7:0 */
	{1, 0x016E, 0x04},	/*y output size 11:8 */
	{1, 0x016F, 0x38},	/*y output size 7:0 */

	{1, 0x0170, 0x01},	/*x inc              */
	{1, 0x0171, 0x01},	/*y inc              */
	{1, 0x0172, 0x03},	/*mirror             */
	{1, 0x0174, 0x00},	/*h binning 1:x2 2:x4 */
	{1, 0x0175, 0x00},	/*v binning 1:x2 2:x4 */

	{1, 0x018C, 0x0a},	/*csi data format 15:8 */
	{1, 0x018D, 0x0a},	/*csi data format 7:0 */

	{1, 0x0301, 0x05},	/*!vtpxck_div=5 4lane a:2lane  */
	{1, 0x0303, 0x01},	/*vtsyck_div  */
	{1, 0x0304, 0x03},	/*prepllck_vt_div */
	{1, 0x0305, 0x03},	/*repllck_op_div */
	{1, 0x0306, 0x00},	/*pll_vt_mpy 10:8 */
	/*pll_vt_mpy 7:0  0x57 13: 20M pixel clk   18:28.8M */
	{1, 0x0307, 0x50},
	{1, 0x0309, 0x0a},	/*oppxck_div normal 0xa */
	{1, 0x030B, 0x01},	/*opsyck_div */
	{1, 0x030C, 0x00},	/*pll_op_mpy 10:8 */
	{1, 0x030D, 0x4c},	/*pll_op_mpy 7:0 0x5a 90 720Mbps   15:120M */

	{1, 0x4767, 0x0F},	/*cis tuning */
	{1, 0x4750, 0x14},	/*cis tuning */
	{1, 0x47B4, 0x14},	/*cis tuning */

	/*{0x0100, 0x01}, //mode select, stream on */
	ENDMARKER,
};

/* 1600*1200: UXGA*/
static const struct regval_list module_uxga_regs[] = {
	/* Reset for operation ...         */
	/*{0x0103, 0x01}, //soft reset     */
	/*{0x0103, 0x00}, //streaming off  */

	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x0c},
	{1, 0x300a, 0xff},
	{1, 0x300b, 0xff},
	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x09},

	{1, 0x0114, 0x03},	/*!!!!!!!!!!!!!!!!!!!!!3:4 lane   1:2 lane */

	{1, 0x0128, 0x00},	/*/phy_contrl */
	{1, 0x012A, 0x18},	/*/exck_freq */
	{1, 0x012B, 0x00},	/*/exck_freq */

	{1, 0x0160, 0x0a},	/*frame lenth 15:8  */
	{1, 0x0161, 0x82},	/*frame lenth 7:0   */
	{1, 0x0162, 0x0d},	/*line lenth 15:8   */
	{1, 0x0163, 0x78},	/*line lenth 7:0    */
	{1, 0x0164, 0x00},	/*x_start 11:8      */
	{1, 0x0165, 0x28},	/*x_start 7:0       */
	{1, 0x0166, 0x0c},	/*x_end 11:8        */
	{1, 0x0167, 0xa7},	/*x_end 7:0         */
	{1, 0x0168, 0x00},	/*y_start 11:8      */
	{1, 0x0169, 0x20},	/*y_start   7:0     */
	{1, 0x016A, 0x09},	/*y_end 11:8        */
	{1, 0x016B, 0x7f},	/*y_end 7:0         */
	{1, 0x016C, 0x06},	/*x output size 11:8 */
	{1, 0x016D, 0x40},	/*x output size 7:0 */
	{1, 0x016E, 0x04},	/*y output size 11:8 */
	{1, 0x016F, 0xb0},	/*y output size 7:0 */

	{1, 0x0170, 0x01},	/*x inc              */
	{1, 0x0171, 0x01},	/*y inc              */
	{1, 0x0172, 0x03},	/*mirror             */
	{1, 0x0174, 0x01},	/*h binning 1:x2 2:x4 */
	{1, 0x0175, 0x01},	/*v binning 1:x2 2:x4 */

	{1, 0x018C, 0x0a},	/*csi data format 15:8 */
	{1, 0x018D, 0x0a},	/*csi data format 7:0 */
	/*!!!!!vtpxck_div=5 4lane a:2lane  */
	{1, 0x0301, 0x05},
	{1, 0x0303, 0x01},	/*vtsyck_div*/
	{1, 0x0304, 0x03},	/*prepllck_vt_div */
	{1, 0x0305, 0x03},	/*repllck_op_div*/
	{1, 0x0306, 0x00},	/*pll_vt_mpy 10:8*/
	/*pll_vt_mpy 7:0  0x57 13: 20M pixel clk   18:28.8M */
	{1, 0x0307, 0x50},
	{1, 0x0309, 0x0a},	/*oppxck_div normal 0xa */
	{1, 0x030B, 0x01},	/*opsyck_div*/
	{1, 0x030C, 0x00},	/*pll_op_mpy 10:8 */
	{1, 0x030D, 0x4c},	/*pll_op_mpy 7:0 0x5a 90 720Mbps   15:120M*/

	{1, 0x4767, 0x0F},	/*cis tuning */
	{1, 0x4750, 0x14},	/*cis tuning */
	{1, 0x47B4, 0x14},	/*cis tuning */

	/*{0x0100, 0x01}, //mode select, stream on */
	ENDMARKER,
};

/* 1632*1224*/
static const struct regval_list module_1632x1224_regs[] = {
	/* Reset for operation ...       */
	/*{0x0103, 0x01}, //soft reset   */
	/*{0x0103, 0x00}, //streaming off */

	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x0c},
	{1, 0x300a, 0xff},
	{1, 0x300b, 0xff},
	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x09},

	{1, 0x0114, 0x03},	/*!!3:4 lane   1:2 lane */
	{1, 0x0128, 0x00},	/*phy_contrl */
	{1, 0x012A, 0x18},	/*exck_freq */
	{1, 0x012B, 0x00},	/*exck_freq */

	{1, 0x0160, 0x0a},	/*frame lenth 15:8  */
	{1, 0x0161, 0xc8},	/*frame lenth 7:0   */
	{1, 0x0162, 0x0d},	/*line lenth 15:8   */
	{1, 0x0163, 0x78},	/*line lenth 7:0    */
	{1, 0x0164, 0x00},	/*x_start 11:8      */
	{1, 0x0165, 0x00},	/*x_start 7:0       */
	{1, 0x0166, 0x0c},	/*x_end 11:8        */
	{1, 0x0167, 0xbf},	/*x_end 7:0         */
	{1, 0x0168, 0x00},	/*y_start 11:8      */
	{1, 0x0169, 0x00},	/*y_start   7:0     */
	{1, 0x016A, 0x09},	/*y_end 11:8        */
	{1, 0x016B, 0x8f},	/*y_end 7:0         */
	{1, 0x016C, 0x06},	/*x output size 11:8 */
	{1, 0x016D, 0x60},	/*x output size 7:0 */
	{1, 0x016E, 0x04},	/*y output size 11:8 */
	{1, 0x016F, 0xC8},	/*y output size 7:0 */

	{1, 0x0170, 0x01},	/*x inc              */
	{1, 0x0171, 0x01},	/*y inc              */
	{1, 0x0174, 0x01},	/*h binning 1:x2 2:x4 */
	{1, 0x0175, 0x01},	/*v binning 1:x2 2:x4 */

	{1, 0x018C, 0x0A},	/*csi data format 15:8 */
	{1, 0x018D, 0x0A},	/*csi data format 7:0 */

	{1, 0x0301, 0x05},	/*!vtpxck_div=5 4lane a:2lane  */
	{1, 0x0303, 0x01},	/*vtsyck_div */
	{1, 0x0304, 0x03},	/*prepllck_vt_div*/
	{1, 0x0305, 0x03},	/*repllck_op_div*/
	{1, 0x0306, 0x00},	/*pll_vt_mpy 10:8*/
	/*pll_vt_mpy 7:0  0x57 13: 20M pixel clk   18:28.8M */
	{1, 0x0307, 0x50},
	{1, 0x0309, 0x0a},	/*oppxck_div normal 0xa */
	{1, 0x030B, 0x01},	/*opsyck_div*/
	{1, 0x030C, 0x00},	/*pll_op_mpy 10:8*/
	{1, 0x030D, 0x4c},	/*pll_op_mpy 7:0 0x5a 90 720Mbps   15:120M*/

	{1, 0x4767, 0x0F},	/*cis tuning */
	{1, 0x4750, 0x14},	/*cis tuning */
	{1, 0x47B4, 0x14},	/*cis tuning */

	/*{0x0100, 0x01}, //mode select, stream on */

	ENDMARKER,
};

/* 3264*2448: QUXGA*/
static const struct regval_list module_quxga_regs[] = {
	/* Reset for operation ...       */
	/*{0x0103, 0x01}, //soft reset   */
	/*{0x0103, 0x00}, //streaming off */

	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x0c},
	{1, 0x300a, 0xff},
	{1, 0x300b, 0xff},
	{1, 0x30eb, 0x05},
	{1, 0x30eb, 0x09},

	{1, 0x0114, 0x03},	/*!3:4 lane   1:2 lane */
	{1, 0x0128, 0x00},	/*phy_contrl */
	{1, 0x012A, 0x18},	/*exck_freq  */
	{1, 0x012B, 0x00},	/*exck_freq  */

	{1, 0x0160, 0x14},	/*frame lenth 15:8  */
	{1, 0x0161, 0xc8},	/*frame lenth 7:0   */
	{1, 0x0162, 0x0d},	/*line lenth 15:8   */
	{1, 0x0163, 0x78},	/*line lenth 7:0    */
	{1, 0x0164, 0x00},	/*x_start 11:8      */
	{1, 0x0165, 0x00},	/*x_start 7:0       */
	{1, 0x0166, 0x0c},	/*x_end 11:8        */
	{1, 0x0167, 0xbf},	/*x_end 7:0         */
	{1, 0x0168, 0x00},	/*y_start 11:8      */
	{1, 0x0169, 0x00},	/*y_start   7:0     */
	{1, 0x016A, 0x09},	/*y_end 11:8        */
	{1, 0x016B, 0x8f},	/*y_end 7:0         */
	{1, 0x016C, 0x0c},	/*x output size 11:8 */
	{1, 0x016D, 0xc0},	/*x output size 7:0 */
	{1, 0x016E, 0x09},	/*y output size 11:8 */
	{1, 0x016F, 0x90},	/*y output size 7:0 */

	{1, 0x0170, 0x01},	/*x inc */
	{1, 0x0171, 0x01},	/*y inc */
	{1, 0x0174, 0x00},	/*h binning 1:x2 2:x4 */
	{1, 0x0175, 0x00},	/*v binning 1:x2 2:x4 */

	{1, 0x018C, 0x0A},	/*csi data format 15:8 */
	{1, 0x018D, 0x0A},	/*csi data format 7:0 */
	/*!!!vtpxck_div=5 4lane a:2lane   */
	{1, 0x0301, 0x05},
	{1, 0x0303, 0x01},	/*vtsyck_div */
	{1, 0x0304, 0x03},	/*prepllck_vt_div  */
	{1, 0x0305, 0x03},	/*repllck_op_div */
	{1, 0x0306, 0x00},	/*pll_vt_mpy 10:8 */
	/*pll_vt_mpy 7:0  0x57 13: 20M pixel clk   18:28.8M */
	{1, 0x0307, 0x2a},
	{1, 0x0309, 0x0a},	/*oppxck_div normal 0xa */
	{1, 0x030B, 0x01},	/*opsyck_div  */
	{1, 0x030C, 0x00},	/*pll_op_mpy 10:8 */
	{1, 0x030D, 0x2a},	/*pll_op_mpy 7:0 0x5a 90 720Mbps15:120M */

	{1, 0x4767, 0x0F},	/*cis tuning */
	{1, 0x4750, 0x14},	/*cis tuning */
	{1, 0x47B4, 0x14},	/*cis tuning */

	/*{0x0100, 0x01}, //mode select, stream on */

	ENDMARKER,
};

/*
 * window size list
 */

/* 1600X1200 */
static struct camera_module_win_size module_win_uxga = {
	.name = "UXGA",
	.width = WIDTH_UXGA,
	.height = HEIGHT_UXGA,
	.win_regs = module_uxga_regs,
	.frame_rate_array = frame_rate_uxga,
	.capture_only = 0,
};

/* 3200X2400 */
static struct camera_module_win_size module_win_quxga = {
	.name = "QUXGA",
	.width = WIDTH_QUXGA,
	.height = HEIGHT_QUXGA,
	.win_regs = module_quxga_regs,
	.frame_rate_array = frame_rate_quxga,
	.capture_only = 1,
};

static struct camera_module_win_size *module_win_list[] = {
	&module_win_uxga,
	&module_win_quxga,
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
	 .code = V4L2_MBUS_FMT_SBGGR10_1X10,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 },
};

static struct v4l2_ctl_cmd_info v4l2_ctl_array[] = {
	{
	 .id = V4L2_CID_GAIN,
	 .min = 64,
	 .max = 1024 * 2,
	 .step = 1,
	 .def = 256,},

	{
	 .id = V4L2_CID_EXPOSURE,
	 .min = 1,
	 .max = 0XFFFF,
	 .step = 1,
	 .def = 2560,},

	{
	 .id = V4L2_CID_AF_MODE,
	 .min = NONE_AF,
	 .max = CONTINUE_AF | SINGLE_AF,
	 .step = 1,
	 .def = CONTINUE_AF | SINGLE_AF,},

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
	 .id = V4L2_CID_AF_STATUS,
	 .min = AF_STATUS_DISABLE,
	 .max = AF_STATUS_FAIL,
	 .step = 1,
	 .def = AF_STATUS_DISABLE,},

	{
	 .id = V4L2_CID_FLASH_STROBE,
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 0,},

	{
	 .id = V4L2_CID_FLASH_STROBE_STOP,
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 0,},
	{
	 .id = V4L2_CID_MOTOR,
	 .min = 0,
	 .max = 0x3ff,
	 .step = 1,
	 .def = 0,},
	{
	 .id = V4L2_CID_MOTOR_GET_MAX,
	 .min = 0,
	 .max = 0x3ff,
	 .step = 1,
	 .def = 0,},
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
#if 1
	{
	 .id = V4L2_CID_FLASH_LED_MODE,
	 .max = 3,
	 .mask = 0x0,
	 .def = 0,},
#endif
};

#endif				/* __MODULE_DIFF_H__ */
