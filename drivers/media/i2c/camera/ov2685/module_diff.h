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

#define CAMERA_MODULE_NAME		"OV2685"
#define CAMERA_MODULE_PID		0x2685
#define VERSION(pid, ver)		((pid<<8)|(ver&0xFF))

#define MODULE_I2C_REAL_ADDRESS		(0x78>>1)
#define MODULE_I2C_REG_ADDRESS		(0x78>>1)
#define I2C_REGS_WIDTH			2
#define I2C_DATA_WIDTH			1

#define PIDH				0x300a	/* Product ID Number H byte */
#define PIDL				0x300b	/* Product ID Number L byte */
#define OUTTO_SENSO_CLOCK	24000000

#define DEFAULT_VSYNC_ACTIVE_LEVEL		V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define DEFAULT_HSYNC_ACTIVE_LEVEL		V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define DEFAULT_PCLK_SAMPLE_EDGE		V4L2_MBUS_PCLK_SAMPLE_RISING
#define DEFAULT_POWER_LINE_FREQUENCY	V4L2_CID_POWER_LINE_FREQUENCY_50HZ

#define MODULE_DEFAULT_WIDTH	WIDTH_SVGA
#define MODULE_DEFAULT_HEIGHT	HEIGHT_SVGA
#define MODULE_MAX_WIDTH		WIDTH_UXGA
#define MODULE_MAX_HEIGHT		HEIGHT_UXGA

#define AHEAD_LINE_NUM			15
#define DROP_NUM_CAPTURE		2
#define DROP_NUM_PREVIEW		5

/*Every sensor must set this value*/
#define USE_AS_FRONT 1
#define USE_AS_REAR 0

static unsigned int frame_rate_720p[] = { 30, };

/*static unsigned int frame_rate_svga[] = {30,};
static unsigned int frame_rate_uxga[] = {15,};*/

#define OV5645_mirror
#define FLIP
/*
MIPI SENSOR params settings
*/
static struct mipi_setting mipi_sensor_setting = {
	.lan_num = 0,		/*0~3 */
	.contex0_en = 1,
	.contex0_virtual_num = 0,
	/*MIPI_YUV422 MIPI_RAW8 MIPI_RAW10 MIPI_RAW12 */
	.contex0_data_type = MIPI_YUV422,
	.clk_settle_time = 10,
	.clk_term_time = 8,
	.data_settle_time = 10,
	.data_term_time = 8,
	.crc_en = 1,
	.ecc_en = 1,
	.hclk_om_ent_en = 1,
	.lp11_not_chek = 0,
	.hsclk_edge = 0,	/*0: rising edge; 1: falling edge */
	.lane0_map = 0,
	.lane1_map = 1,
	.lane2_map = 2,
	.lane3_map = 3,
	.mipi_en = 1,
	.csi_clk = 165000000,
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
	    | SENSOR_FLAG_YUV | SENSOR_FLAG_MIPI | SENSOR_FLAG_CHANNEL1,
	.mipi_cfg = &mipi_sensor_setting,
	.module_cfg = &module_setting,
};

/*
 * supported color format list.
 * see definition in
 * http://thread.gmane.org/gmane.linux.drivers.video-input-infrastructure/
 *12830/focus=13394
 * YUYV8_2X8_LE == YUYV with LE packing
 * YUYV8_2X8_BE == UYVY with LE packing
 * YVYU8_2X8_LE == YVYU with LE packing
 * YVYU8_2X8_BE == VYUY with LE packing
 */
static const struct module_color_format module_cfmts[] = {
	{
	 .code = V4L2_MBUS_FMT_UYVY8_2X8,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 },
};

static const struct regval_list module_init_regs[] = {
	/**************** initialize OV2685*****************/
	/*********** OV2685_SVGA_YUV 15 fps *************/
	/****** 24 MHz input clock, 24Mhz PCLK  *************/
	/***********************************************/
	/*****OV2685 setting version History ***************/
	/**********************************************/
	/*************1. 09/23/2013 V00*****************/
	/** base on OV2685_AA_00_01_00.ovt *************/
	/****************** 1. Initial release************ */
	/* ****************************************** */
	/******************2. 10/02/2013 V01 ******************/
	/******** base on OV2685_AA_00_01_02.ovt **************/
	/* *****************1. update ISP setting***************** */
	/*****************************************************/
	/*****************************************************/
	/******************3. 10/08/2013 V03******************/
	/*****************base on OV2685_AA_00_01_03.ovt****/
	/* *****************1. remove unsed registers********* */
	/****************************************************/
	/* ************************************************** */
	/*******************4. 11/06/2013 V04******************/
	/* ********base on OV2685_AA_00_02_02.ovt************* */
	/* *****************1. updat0x3603, 0xfo,r***************** */
	/* ************2. updated AE f0x720p, 0xan,d below************ */
	/****************** 3. correct mirror/pattern error******************/
	/********MCLK=24Mhz, SysClk=33Mhz, MIPI 1 lane 528Mbps************/
	{1, 0x0103, 0x01},	/* software reset */
	{1, 0x3002, 0x00},	/* gpio input, vsync input, fsin input  */
	/* drive strength of low speed = 1x, bypass latch of hs_enable */
	{1, 0x3016, 0x1c},
	{1, 0x3018, 0x44},	/* MIPI 1 lane, 10-bit mode */
	{1, 0x301d, 0xf0},	/* enable clocks */
	{1, 0x3082, 0x2c},	/* PLL */
	{1, 0x3083, 0x03},	/* PLL */
	{1, 0x3084, 0x0f},	/* PLL */
	{1, 0x3085, 0x03},	/* PLL */
	{1, 0x3086, 0x00},	/* PLL */
	{1, 0x3087, 0x00},	/* PLL */
	{1, 0x3501, 0x26},	/* exposure M */
	{1, 0x3502, 0x40},	/* exposure L */
	{1, 0x3503, 0x03},	/* AGC manual, AEC manual */
	{1, 0x350b, 0x36},	/* Gain L */
	{1, 0x3600, 0xb4},	/* analog conrtol */
	{1, 0x3603, 0x35},
	{1, 0x3604, 0x24},
	{1, 0x3605, 0x00},
	{1, 0x3620, 0x24},
	{1, 0x3621, 0x34},
	{1, 0x3622, 0x03},
	{1, 0x3628, 0x10},	/* analog control */
	{1, 0x3705, 0x3c},	/* sensor control */
	{1, 0x370a, 0x23},
	{1, 0x370c, 0x50},
	{1, 0x370d, 0x00},
	{1, 0x3717, 0x58},
	{1, 0x3718, 0x80},
	{1, 0x3720, 0x00},
	{1, 0x3721, 0x09},
	{1, 0x3722, 0x0b},
	{1, 0x3723, 0x48},
	{1, 0x3738, 0x99},	/* sensor control */
	{1, 0x3781, 0x80},	/* PSRAM         */
	{1, 0x3784, 0x0c},
	{1, 0x3789, 0x60},	/*PSRAM */
	{1, 0x3800, 0x00},	/*x start H */
	{1, 0x3801, 0x00},	/*x start L */
	{1, 0x3802, 0x00},	/*y start H */
	{1, 0x3803, 0x00},	/*y start L */
	{1, 0x3804, 0x06},	/*x end H */
	{1, 0x3805, 0x4f},	/*x end L */
	{1, 0x3806, 0x04},	/*y end H */
	{1, 0x3807, 0xbf},	/*y end L */
	{1, 0x3808, 0x03},	/*x output size H */
	{1, 0x3809, 0x20},	/*x output size L */
	{1, 0x380a, 0x02},	/*y output size H */
	{1, 0x380b, 0x58},	/*y output size L */
	{1, 0x380c, 0x06},	/*HTS H */
	{1, 0x380d, 0xac},	/*HTS L */
	{1, 0x380e, 0x02},	/*VTS H */
	{1, 0x380f, 0x84},	/*VTS L */
	{1, 0x3810, 0x00},	/*ISP x win H */
	{1, 0x3811, 0x04},	/*ISP x win L */
	{1, 0x3812, 0x00},	/*ISP y win H */
	{1, 0x3813, 0x04},	/*ISP y win L */
	{1, 0x3814, 0x31},	/*x inc */
	{1, 0x3815, 0x31},	/*y inc */
	{1, 0x3819, 0x04},	/*Vsync end row L */
#ifdef FLIP
#ifdef MIRROR
	/* vsub48_blc on, vflip_blc on, Flip on, vbinf on */
	{1, 0x3820, 0xc6},
	{1, 0x3821, 0x05},	/* Mirror on, hbin on */
#else
	/* vsub48_blc on, vflip_blc on, Flip on, vbinf on, */
	{1, 0x3820, 0xc6},
	{1, 0x3821, 0x01},	/* hbin on */
#endif
#else
#ifdef MIRROR
	{1, 0x3820, 0xc2},	/* vsub48_blc on, vflip_blc on, vbinf on, */
	{1, 0x3821, 0x05},	/* Mirror on, hbin on  */
#else
	{1, 0x3820, 0xc2},	/* vsub48_blc on, vflip_blc on, vbinf on, */
	{1, 0x3821, 0x01},	/* hbin on                               */
#endif
#endif
	/* IQ setting sta0xfrom, 0xhe,re */
	{1, 0x382a, 0x08},	/*auto VTS */
	/* AEC */
	{1, 0x3a00, 0x43},	/*night mode enable, band enable */
	{1, 0x3a02, 0x90},	/*50Hz                          */
	{1, 0x3a03, 0x4e},	/*AEC target H                  */
	{1, 0x3a04, 0x40},	/*AEC target L                  */
	{1, 0x3a06, 0x00},	/*B50 H                         */
	{1, 0x3a07, 0xc1},	/*B50 L                         */
	{1, 0x3a08, 0x00},	/*B60 H                         */

	{1, 0x3a09, 0xa1},	/* B60 L                   */
	{1, 0x3a0a, 0x07},	/* max exp 50 H            */
	{1, 0x3a0b, 0x8a},	/* max exp 50 L, 10 band   */
	{1, 0x3a0c, 0x07},	/* max exp 60 H            */
	{1, 0x3a0d, 0x8c},	/* max exp 60 L, 12 band   */
	{1, 0x3a0e, 0x02},	/* VTS band 50 H           */
	{1, 0x3a0f, 0x43},	/* VTS band 50 L           */
	{1, 0x3a10, 0x02},	/* VTS band 60 H           */
	{1, 0x3a11, 0x84},	/* VTS band 60 L           */
	{1, 0x3a13, 0x80},	/* gain ceiling = 8x       */
	{1, 0x4000, 0x81},	/* avg_weight = 8, mf_en on */
	{1, 0x4001, 0x40},	/* format_trig_beh on      */
	{1, 0x4002, 0x00},	/* blc target              */
	{1, 0x4003, 0x10},	/* blc target              */
	{1, 0x4008, 0x00},	/* bl_start                */
	{1, 0x4009, 0x03},	/* bl_end                  */
	{1, 0x4300, 0x30},	/* YUV 422                 */
	{1, 0x430e, 0x00},
	{1, 0x4602, 0x02},	/* VFIFO R2, frame reset enable */
	{1, 0x481b, 0x40},	/* HS trail Min  */
	{1, 0x481f, 0x40},	/* CLK prepare Max */
	{1, 0x4837, 0x1e},	/* MIPI global timing */
	/* lenc_en, awb_gain_en, lcd_en, avg_en, bc_en, WC_en, blc_en */
	{1, 0x5000, 0xff},
	{1, 0x5001, 0x0d},	/* manual blc offset, avg after LCD */
	{1, 0x5002, 0x32},	/* dpc_href_s, sof_sel, bias_plus   */
	{1, 0x5003, 0x0c},	/* bias_man */
	/* uv_dsn_en, rgb_dns_en, gamma_en, cmx_en,
	*cip_en, raw_dns_en, strech_en, awb_en */
	{1, 0x5004, 0xff},
	{1, 0x5005, 0x12},	/* sde_en, rgb2yuv_en */
	{1, 0x4202, 0x0f},	/* stream off        */
	{1, 0x0100, 0x01},	/* wake up           */
	/* AWB */
	{1, 0x5180, 0xf4},	/* AWB */
	{1, 0x5181, 0x11},
	{1, 0x5182, 0x41},
	{1, 0x5183, 0x42},
	{1, 0x5184, 0x82},	/* cwf_x           */
	{1, 0x5185, 0x62},	/* cwf_y           */
	{1, 0x5186, 0x86},	/* kx(cwf 2 a)x2y  */
	{1, 0x5187, 0xd0},	/* Ky(cwf 2 day)y2x */
	{1, 0x5188, 0x10},	/* cwf range       */
	{1, 0x5189, 0x0e},	/* a range         */
	{1, 0x518a, 0x20},	/* day range       */
	{1, 0x518b, 0x4f},	/* day limit       */
	{1, 0x518c, 0x3c},	/* a limit         */
	{1, 0x518d, 0xf8},
	{1, 0x518e, 0x04},
	{1, 0x518f, 0x7f},
	{1, 0x5190, 0x40},
	{1, 0x5191, 0x5f},
	{1, 0x5192, 0x40},
	{1, 0x5193, 0xff},
	{1, 0x5194, 0x40},
	{1, 0x5195, 0x07},
	{1, 0x5196, 0x99},
	{1, 0x5197, 0x04},
	{1, 0x5198, 0x00},
	{1, 0x5199, 0x05},
	{1, 0x519a, 0x96},
	{1, 0x519b, 0x04},	/* AWB                                 */
	{1, 0x5200, 0x09},	/* stretch minimum = 3096, auto enable */
	{1, 0x5201, 0x00},	/* stretch min low level               */

	{1, 0x5202, 0x06},	/* stretch min low level          */
	{1, 0x5203, 0x20},	/* stretch m0xhigh, 0xle,vel      */
	{1, 0x5204, 0x41},	/* stretch step2, step1           */
	{1, 0x5205, 0x16},	/* stretch current low level      */
	{1, 0x5206, 0x00},	/* stretch curre0xhigh, 0xle,vel L */
	{1, 0x5207, 0x05},	/* stretch curre0xhigh, 0xle,vel H */
	{1, 0x520b, 0x30},	/* stretch_thres1 L */
	{1, 0x520c, 0x75},	/* stretch_thres1 M */
	{1, 0x520d, 0x00},	/* stretch_thres1 H */
	{1, 0x520e, 0x30},	/* stretch_thres2 L */
	{1, 0x520f, 0x75},	/* stretch_thres2 M */
	{1, 0x5210, 0x00},	/* stretch_thres2 H */
	/* Raw de-noise auto */
	/* m_nNoise YSlop = 5, Parameter noise and
	*edgethre calculated by noise list */
	{1, 0x5280, 0x15},
	{1, 0x5281, 0x06},	/* m_nNoiseList[0] */
	{1, 0x5282, 0x06},	/* m_nNoiseList[1] */
	{1, 0x5283, 0x08},	/* m_nNoiseList[2] */
	{1, 0x5284, 0x1c},	/* m_nNoiseList[3] */
	{1, 0x5285, 0x1c},	/* m_nNoiseList[4] */
	{1, 0x5286, 0x20},	/* m_nNoiseList[5] */
	{1, 0x5287, 0x10},	/* m_nMaxEdgeGThre */
	/*CIP Y de-noise, auto (default) */
	/*m_bColorEdgeEnable, m_bAntiAlasing on, m_nNoise YSlop = 5 */
	{1, 0x5300, 0xc5},
	{1, 0x5301, 0xa0},	/*m_nSharpenSlop = 1 */
	{1, 0x5302, 0x06},	/*m_nNoiseList[0]    */
	{1, 0x5303, 0x08},	/*m_nNoiseList[1]    */
	{1, 0x5304, 0x10},	/*m_nNoiseList[2]    */
	{1, 0x5305, 0x20},	/*m_nNoiseList[3]    */
	{1, 0x5306, 0x30},	/*m_nNoiseList[4]    */
	{1, 0x5307, 0x60},	/*m_nNoiseList[5]    */
	/*Sharpness */
	{1, 0x5308, 0x32},	/* m_nMaxSarpenGain, m_nMinSharpenGain */
	{1, 0x5309, 0x00},	/* m_nMinSharpen                      */
	{1, 0x530a, 0x2a},	/* m_nMaxSharpen                      */
	{1, 0x530b, 0x02},	/* m_nMinDetail                       */
	{1, 0x530c, 0x02},	/* m_nMaxDetail                       */
	{1, 0x530d, 0x00},	/* m_nDetailRatioList[0]              */
	{1, 0x530e, 0x0c},	/* m_nDetailRatioList[1]              */
	{1, 0x530f, 0x14},	/* m_nDetailRatioList[2]              */
	{1, 0x5310, 0x1a},	/* m_nSharpenNegEdgeRatio             */
	{1, 0x5311, 0x20},	/* m_nClrEdgeShT1                     */
	{1, 0x5312, 0x80},	/* m_nClrEdgeShT2                     */
	{1, 0x5313, 0x4b},	/* m_nClrEdgeShpSlop                  */
	/* color matrix */
	{1, 0x5380, 0x01},	/* nCCM_D[0][0] H */
	{1, 0x5381, 0x83},	/* nCCM_D[0][0] L */
	{1, 0x5382, 0x00},	/* nCCM_D[0][1] H */
	{1, 0x5383, 0x1f},	/* nCCM_D[0][1] L */
	{1, 0x5384, 0x00},	/* nCCM_D[1][0] H */
	{1, 0x5385, 0x88},	/* nCCM_D[1][0] L */
	{1, 0x5386, 0x00},	/* nCCM_D[1][1] H */
	{1, 0x5387, 0x82},	/* nCCM_D[1][1] L */
	{1, 0x5388, 0x00},	/* nCCM_D[2][0] H */
	{1, 0x5389, 0x40},	/* nCCM_D[2][0] L */
	{1, 0x538a, 0x01},	/* nCCM_D[2][1] H */
	{1, 0x538b, 0xb9},	/* nCCM_D[2][1] L */
	/* Sing bit [2][1], [2][0], [1][1], [1][0], [0][1], [0][0] */
	{1, 0x538c, 0x10},
	/* Gamma */
	{1, 0x5400, 0x0d},	/*m_pCurveYList[0] */
	{1, 0x5401, 0x1a},	/*m_pCurveYList[1] */
	{1, 0x5402, 0x32},	/*m_pCurveYList[2] */
	{1, 0x5403, 0x59},	/*m_pCurveYList[3] */
	{1, 0x5404, 0x68},	/*m_pCurveYList[4] */
	{1, 0x5405, 0x76},	/*m_pCurveYList[5] */
	{1, 0x5406, 0x82},	/*m_pCurveYList[6] */
	{1, 0x5407, 0x8c},	/*m_pCurveYList[7] */
	{1, 0x5408, 0x94},	/*m_pCurveYList[8] */
	{1, 0x5409, 0x9c},	/*m_pCurveYList[9] */
	{1, 0x540a, 0xa9},	/*m_pCurveYList[10] */
	{1, 0x540b, 0xb6},	/*m_pCurveYList[11] */
	{1, 0x540c, 0xcc},	/*m_pCurveYList[12] */
	{1, 0x540d, 0xdd},	/*m_pCurveYList[13] */
	{1, 0x540e, 0xeb},	/*m_pCurveYList[14] */
	{1, 0x540f, 0xa0},	/*m_nMaxShadowHGain */
	{1, 0x5410, 0x6e},	/*m_nMidTongHGain  */
	{1, 0x5411, 0x06},	/*m_nHighLightHGain */
	/* RGB De-noise */
	{1, 0x5480, 0x19},	/* m_nShadowExtraNoise = 12, m_bSmoothYEnable */
	{1, 0x5481, 0x00},	/* m_nNoiseYList[1], m_nNoiseYList[0]        */
	{1, 0x5482, 0x09},	/* m_nNoiseYList[3], m_nNoiseYList[2]        */
	{1, 0x5483, 0x12},	/* m_nNoiseYList[5], m_nNoiseYList[4]        */
	{1, 0x5484, 0x04},	/* m_nNoiseUVList[0] */
	{1, 0x5485, 0x06},	/* m_nNoiseUVList[1] */
	{1, 0x5486, 0x08},	/* m_nNoiseUVList[2] */
	{1, 0x5487, 0x0c},	/* m_nNoiseUVList[3] */
	{1, 0x5488, 0x10},	/* m_nNoiseUVList[4] */
	{1, 0x5489, 0x18},	/* m_nNoiseUVList[5] */
	/* UV de-noise auto -1 */
	{1, 0x5500, 0x00},	/* m_nNoiseList[0] */
	{1, 0x5501, 0x01},	/* m_nNoiseList[1] */
	{1, 0x5502, 0x02},	/* m_nNoiseList[2] */
	{1, 0x5503, 0x03},	/* m_nNoiseList[3] */
	{1, 0x5504, 0x04},	/* m_nNoiseList[4] */
	{1, 0x5505, 0x05},	/* m_nNoiseList[5] */
	/* uuv_dns_psra_man dis, m_nShadowExtraNoise = 0 */
	{1, 0x5506, 0x00},
	/* UV adjust */
	/*fixy off, neg off, gray off, fix_v off,
	*fix_u off, contrast_en, saturation on */
	{1, 0x5600, 0x06},
	{1, 0x5603, 0x40},	/*sat U */
	{1, 0x5604, 0x20},	/*sat V */
	{1, 0x5608, 0x00},
	{1, 0x5609, 0x10},	/* uvadj_th1 */
	{1, 0x560a, 0x40},	/* uvadj_th2 */
	{1, 0x5780, 0x3e},	/* DPC      */
	{1, 0x5781, 0x0f},
	{1, 0x5782, 0x04},
	{1, 0x5783, 0x02},
	{1, 0x5784, 0x01},
	{1, 0x5785, 0x01},
	{1, 0x5786, 0x00},
	{1, 0x5787, 0x04},
	{1, 0x5788, 0x02},
	{1, 0x5789, 0x00},
	{1, 0x578a, 0x01},
	{1, 0x578b, 0x02},
	{1, 0x578c, 0x03},
	{1, 0x578d, 0x03},
	{1, 0x578e, 0x08},
	{1, 0x578f, 0x0c},
	{1, 0x5790, 0x08},

	{1, 0x5791, 0x04},
	{1, 0x5792, 0x00},
	{1, 0x5793, 0x00},
	{1, 0x5794, 0x03},	/* DPC */
	/* lens correction */
	{1, 0x5800, 0x03},	/* red x0 H  */
	{1, 0x5801, 0x10},	/* red x0 L  */
	{1, 0x5802, 0x02},	/* red y0 H  */
	{1, 0x5803, 0x68},	/* red y0 L  */
	{1, 0x5804, 0x2a},	/* red a1    */
	{1, 0x5805, 0x05},	/* red a2    */
	{1, 0x5806, 0x12},	/* red b1    */
	{1, 0x5807, 0x05},	/* red b2    */
	{1, 0x5808, 0x03},	/* green x0 H */
	{1, 0x5809, 0x38},	/* green x0 L */
	{1, 0x580a, 0x02},	/* green y0 H */
	{1, 0x580b, 0x68},	/* green y0 L */
	{1, 0x580c, 0x20},	/* green a1  */
	{1, 0x580d, 0x05},	/* green a2  */
	{1, 0x580e, 0x52},	/* green b1  */
	{1, 0x580f, 0x06},	/* green b2  */
	{1, 0x5810, 0x03},	/* blue x0 H */
	{1, 0x5811, 0x10},	/* blue x0 L */
	{1, 0x5812, 0x02},	/* blue y0 H */
	{1, 0x5813, 0x7c},	/* blue y0 L */
	{1, 0x5814, 0x1c},	/* bule a1   */
	{1, 0x5815, 0x05},	/* blue a2   */
	{1, 0x5816, 0x42},	/* blue b1   */
	{1, 0x5817, 0x06},	/* blue b2   */
	{1, 0x5818, 0x0d},	/* rst_seed on, md_en, coef_m off, gcoef_en */
	{1, 0x5819, 0x10},	/* lenc_coef_th       */
	{1, 0x581a, 0x04},	/* lenc_gain_thre1    */
	{1, 0x581b, 0x08},	/* lenc_gain_thre2    */
	{1, 0x3503, 0x00},	/* AEC auto, AGC auto */
	ENDMARKER,
};

/* 800*600: SVGA*/
static const struct regval_list module_svga_regs[] = {
	/* 800x600 preview                              */
	/* 30-10fps                                     */
	/* MCLK=24Mhz, SysClk=33Mhz, MIPI 1 lane 528Mbps */
	/*0x0100, 0x00,  sleep                          */
	{1, 0x4200, 0x0f},	/* stream off    */
	{1, 0x3501, 0x26},	/* exposure M    */
	{1, 0x3502, 0x40},	/* exposure L    */
	{1, 0x3620, 0x24},	/* analog control */
	{1, 0x3621, 0x34},
	{1, 0x3622, 0x03},	/* analog control */
	{1, 0x370a, 0x23},	/* sensor control */
	{1, 0x370d, 0x00},
	{1, 0x3718, 0x88},
	{1, 0x3721, 0x09},
	{1, 0x3722, 0x0b},
	{1, 0x3723, 0x48},
	{1, 0x3738, 0x99},	/*sensor control */
	{1, 0x3801, 0x00},	/*x start L      */
	{1, 0x3803, 0x00},	/*y start L      */
	{1, 0x3804, 0x06},	/*x end H        */
	{1, 0x3805, 0x4f},	/*x end L        */
	{1, 0x3806, 0x04},	/*y end H        */
	{1, 0x3807, 0xbf},	/*y end L        */
	{1, 0x3808, 0x03},	/*x output size H */
	{1, 0x3809, 0x20},	/*x output size L */
	{1, 0x380a, 0x02},	/*y output size H */
	{1, 0x380b, 0x58},	/*y output size L */
	{1, 0x380c, 0x0d},	/*HTS H          */
	{1, 0x380d, 0x58},	/*HTS L          */
	{1, 0x380e, 0x02},	/*VTS H          */
	{1, 0x380f, 0x84},	/*VTS L          */
	{1, 0x3811, 0x04},	/*ISP x win L    */
	{1, 0x3813, 0x04},	/*ISP y win L    */
	{1, 0x3814, 0x31},	/*x inc          */
	{1, 0x3815, 0x31},	/*y inc          */
	{1, 0x3820, 0xc2},	/*vsub48_blc on, vflip_blc on, vbinf on, */
	{1, 0x3821, 0x01},	/*hbin on                               */
	{1, 0x3820, 0xc2},	/*vsub48_blc on, vflip_blc on, vbinf on, */

	{1, 0x3821, 0x05},	/* Mirror on, hbin on*/
	/* vsub48_blc on, vflip_blc on, Flip on, vbinf on, */
	{1, 0x3820, 0xc6},
	{1, 0x3821, 0x01},	/* hbin on*/
	/* vsub48_blc on, vflip_blc on, Flip on, vbinf on, */
	{1, 0x3820, 0xc6},
	{1, 0x3821, 0x05},	/* Mirror on, hbin on      */
	{1, 0x382a, 0x00},	/* fixed VTS               */
	{1, 0x3a00, 0x41},	/* night mode off, band on */
	{1, 0x3a07, 0xc1},	/* B50 L                   */
	{1, 0x3a09, 0xa1},	/* B60 L                   */
	{1, 0x3a0a, 0x12},	/* max exp 50 H            */
	{1, 0x3a0b, 0x18},	/* max exp 50 L            */
	{1, 0x3a0c, 0x14},	/* max exp 60 H            */
	{1, 0x3a0d, 0x20},	/* max exp 60 L            */
	{1, 0x3a0e, 0x02},	/* VTS band 50 H           */
	{1, 0x3a0f, 0x43},	/* VTS band 50 L           */
	{1, 0x3a10, 0x02},	/* VTS band 60 H           */
	{1, 0x3a11, 0x84},	/* VTS band 60 L           */
	{1, 0x3a13, 0x80},	/* gain ceiling = 8x       */
	{1, 0x4008, 0x00},	/* bl_start                */
	{1, 0x4009, 0x03},	/* bl_end                  */
	{1, 0x5003, 0x0c},	/* manual blc offset       */
	{1, 0x4202, 0x00},	/* stream on               */
	ENDMARKER,
};

/* 1280*720: 720P*/
static const struct regval_list module_720p_regs[] = {
	{1, 0x4202, 0x0f},	/*stream off        */
	{1, 0x3503, 0x00},	/*AGC auto, AEC auto */
	{1, 0x3501, 0x2d},	/*exposure M        */
	{1, 0x3502, 0x80},	/*exposure L        */
	{1, 0x3620, 0x26},	/*analog control    */
	{1, 0x3621, 0x37},
	{1, 0x3622, 0x04},	/* analog control */
	{1, 0x370a, 0x21},	/* sensor control */
	{1, 0x370d, 0xc0},
	{1, 0x3718, 0x88},
	{1, 0x3721, 0x00},
	{1, 0x3722, 0x00},
	{1, 0x3723, 0x00},
	{1, 0x3738, 0x00},	/*sensor control */
	{1, 0x3801, 0xa0},	/*x start L      */
	{1, 0x3803, 0xf2},	/*y start L      */
	{1, 0x3804, 0x05},	/*x end H        */
	{1, 0x3805, 0xaf},	/*x end L        */
	{1, 0x3806, 0x03},	/*y end H        */
	{1, 0x3807, 0xcd},	/*y end L        */
	{1, 0x3808, 0x05},	/*x output size H */
	{1, 0x3809, 0x00},	/*x output size L */
	{1, 0x380a, 0x02},	/*y output size H */
	{1, 0x380b, 0xd0},	/*y output size L */
	{1, 0x380c, 0x05},	/*HTS H          */
	{1, 0x380d, 0xa6},	/*HTS L          */
	{1, 0x380e, 0x02},	/*VTS H          */
	{1, 0x380f, 0xf8},	/*VTS L          */
	{1, 0x3811, 0x08},	/*ISP x win L    */
	{1, 0x3813, 0x06},	/*ISP y win L    */
	{1, 0x3814, 0x11},	/*x inc          */
	{1, 0x3815, 0x11},	/*y inc          */
	{1, 0x3820, 0xc0},	/*vsub48_blc on, vflip_blc on, vbinf off, */
	{1, 0x3821, 0x00},	/*hbin off                               */
	{1, 0x3820, 0xc0},	/*vsub48_blc on, vflip_blc on, vbinf off, */
	{1, 0x3821, 0x04},	/*Mirror on, hbin off                    */
	/*vsub48_blc on, vflip_blc on, Flip on, vbinf off, */
	{1, 0x3820, 0xc4},
	{1, 0x3821, 0x00},	/*hbin off */
	/*vsub48_blc on, vflip_blc on, Flip on, vbinf off, */
	{1, 0x3820, 0xc4},
	{1, 0x3821, 0x04},	/*Mirror on, hbin off    */
	{1, 0x382a, 0x00},	/*fixed VTS              */
	{1, 0x3a00, 0x41},	/*night mode off, band on */
	{1, 0x3a07, 0xe4},	/*B50 L       */
	{1, 0x3a09, 0xbe},	/*B60 L       */
	{1, 0x3a0a, 0x15},	/*max exp 50 H */
	{1, 0x3a0b, 0x60},	/*max exp 50 L */
	{1, 0x3a0c, 0x17},	/*max exp 60 H */
	{1, 0x3a0d, 0xc0},	/* max exp 60 L */
	{1, 0x3a0e, 0x02},	/* VTS band 50 H */
	{1, 0x3a0f, 0xac},	/* VTS band 50 L */
	{1, 0x3a10, 0x02},	/* VTS band 60 H */
	{1, 0x3a11, 0xf8},	/* VTS band 60 L */
	{1, 0x3a13, 0xf8},	/* gain ceilling = 15.5x */
	{1, 0x4008, 0x02},	/* bl_start */
	{1, 0x4009, 0x09},	/* bl_end   */
	{1, 0x4202, 0x00},	/* stream on */
	ENDMARKER,
};

/* 1600*1200: 2M*/
static const struct regval_list module_uxga_regs[] = {
	/* MCLK=24Mhz, SysClk=33Mhz, MIPI 1 lane 528Mbps */
	{1, 0x4202, 0x0f},	/*stream off            */
	{1, 0x3503, 0x03},	/*AGC manual, AEC manual */
	{1, 0x3501, 0x4e},	/*exposure M            */
	{1, 0x3502, 0xe0},	/*exposure L            */
	{1, 0x3620, 0x24},	/*analog control        */
	{1, 0x3621, 0x34},
	{1, 0x3622, 0x03},	/* analog control */
	{1, 0x370a, 0x21},	/* sensor control */
	{1, 0x370d, 0xc0},
	{1, 0x3718, 0x80},
	{1, 0x3721, 0x09},
	{1, 0x3722, 0x06},
	{1, 0x3723, 0x59},
	{1, 0x3738, 0x99},	/* sensor control */
	{1, 0x3801, 0x00},	/* x start L      */
	{1, 0x3803, 0x00},	/* y start L      */
	{1, 0x3804, 0x06},	/* x end H        */
	{1, 0x3805, 0x4f},	/* x end L        */
	{1, 0x3806, 0x04},	/* y end H        */
	{1, 0x3807, 0xbf},	/* y end L        */
	{1, 0x3808, 0x06},	/* x output size H */
	{1, 0x3809, 0x40},	/* x output size L */
	{1, 0x380a, 0x04},	/* y output size H */
	{1, 0x380b, 0xb0},	/* y output size L */
	{1, 0x380c, 0x06},	/* HTS H          */
	{1, 0x380d, 0xa4},	/* HTS L          */
	{1, 0x380e, 0x05},	/* VTS H          */
	{1, 0x380f, 0x0e},	/* VTS L          */
	{1, 0x3811, 0x08},	/* ISP x win L    */
	{1, 0x3813, 0x08},	/* ISP y win L    */
	{1, 0x3814, 0x11},	/* x inc          */
	{1, 0x3815, 0x11},	/* y inc          */
	/* vsub48_blc on, vflip_blc on, vbinf off,*/
	{1, 0x3820, 0xc0},
	{1, 0x3821, 0x00},	/* hbin off*/
	/* vsub48_blc on, vflip_blc on, vbinf off,*/
	{1, 0x3820, 0xc0},
	{1, 0x3821, 0x04},	/* Mirror on, hbin off */
	/* vsub48_blc on, vflip_blc on, Flip on, vbinf off, */
	{1, 0x3820, 0xc4},
	{1, 0x3821, 0x00},	/* hbin off*/
	/* vsub48_blc on, vflip_blc on, Flip on, vbinf off, */
	{1, 0x3820, 0xc4},
	{1, 0x3821, 0x04},	/* Mirror on, hbin off       */
	{1, 0x382a, 0x08},	/* auto VTS                  */
	{1, 0x3a00, 0x41},	/* night mode off, band on   */
	{1, 0x3a07, 0xc2},	/* B50 L                     */
	{1, 0x3a09, 0xa1},	/* B60 L                     */
	{1, 0x3a0a, 0x09},	/* max exp 50 H              */
	{1, 0x3a0b, 0xda},	/* max exp 50 L  */
	{1, 0x3a0c, 0x0a},	/* max exp 60 H  */
	{1, 0x3a0d, 0x10},	/* max exp 60 L  */
	{1, 0x3a0e, 0x04},	/* VTS band 50 H */
	{1, 0x3a0f, 0x8c},	/* VTS band 50 L */
	{1, 0x3a10, 0x05},	/* VTS band 60 H */
	{1, 0x3a11, 0x08},	/* VTS band 60 L */
	{1, 0x4008, 0x02},	/* bl_start      */
	{1, 0x4009, 0x09},	/* bl_end        */
	{1, 0x4202, 0x00},	/* stream on     */
	ENDMARKER,
};

/*
 * window size list
 */
/* SVGA */
/*
static struct camera_module_win_size module_win_svga = {
	.name             = "SVGA",
	.width            = WIDTH_SVGA,
	.height           = HEIGHT_SVGA,
	.win_regs         = module_svga_regs,
	.frame_rate_array = frame_rate_svga,
	.capture_only     = 0,
};
*/
/* 1280X720 */
static struct camera_module_win_size module_win_720p = {
	.name = "720P",
	.width = WIDTH_720P,
	.height = HEIGHT_720P,
	.win_regs = module_720p_regs,
	.frame_rate_array = frame_rate_720p,
	.capture_only = 0,
};

/* 1600X1200 */
/*
static struct camera_module_win_size module_win_uxga = {
	.name             = "UXGA",
	.width            = WIDTH_UXGA,
	.height           = HEIGHT_UXGA,
	.win_regs         = module_uxga_regs,
	.frame_rate_array = frame_rate_uxga,
	.capture_only     = 0,
};
*/

static struct camera_module_win_size *module_win_list[] = {
	/* &module_win_svga, */
	&module_win_720p,
	/* &module_win_uxga, */
};

/*
 * The exposure target setttings
 */

static struct regval_list module_exp_comp_neg3_regs[] = {
	{1, 0x3a03, 0x32},
	{1, 0x3a04, 0x28},
	ENDMARKER,
};

static struct regval_list module_exp_comp_neg2_regs[] = {
	{1, 0x3a03, 0x3a},
	{1, 0x3a04, 0x30},
	ENDMARKER,
};

static struct regval_list module_exp_comp_neg1_regs[] = {
	{1, 0x3a03, 0x42},
	{1, 0x3a04, 0x38},
	ENDMARKER,
};

static struct regval_list module_exp_comp_zero_regs[] = {
	{1, 0x3a03, 0x4e},
	{1, 0x3a04, 0x40},
	ENDMARKER,
};

static struct regval_list module_exp_comp_pos1_regs[] = {
	{1, 0x3a03, 0x52},
	{1, 0x3a04, 0x48},
	ENDMARKER,
};

static struct regval_list module_exp_comp_pos2_regs[] = {
	{1, 0x3a03, 0x5a},
	{1, 0x3a04, 0x50},
	ENDMARKER,
};

static struct regval_list module_exp_comp_pos3_regs[] = {
	{1, 0x3a03, 0x62},
	{1, 0x3a04, 0x58},
	ENDMARKER,
};

static struct v4l2_ctl_cmd_info v4l2_ctl_array[] = {
	{
	 .id = V4L2_CID_EXPOSURE_COMP,
	 .min = -3,
	 .max = 3,
	 .step = 1,
	 .def = 0,
	 },
	{
	 .id = V4L2_CID_GAIN,
	 .min = 256,
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
#if 0
	{
	 .id = V4L2_CID_FLASH_LED_MODE,
	 .max = 3,
	 .mask = 0x0,
	 .def = 0,},
#endif
};

#endif				/* __MODULE_DIFF_H__ */
