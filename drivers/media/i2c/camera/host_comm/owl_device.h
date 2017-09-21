/*
 * Actions OWL SoC Camera Host driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * LiChi <lichi@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OWL_DEVICE_H__
#define __OWL_DEVICE_H__

#include <media/soc_camera.h>
#include <linux/pinctrl/consumer.h>

/* for flags */
/* used by dts, will replace sensor's default value */
#define SENSOR_FLAG_CHANNEL0  (1 << 0) /* bit-0, 1 channel_1; 0 channel_2  */
#define SENSOR_FLAG_CHANNEL1  (0 << 0)
#define SENSOR_FLAG_CH_MASK   (SENSOR_FLAG_CHANNEL0)
/* bit-1, 1,  parellal interface, DVP; 0 mipi*/
#define SENSOR_FLAG_DVP       (1 << 1)
#define SENSOR_FLAG_MIPI      (0 << 1)
#define SENSOR_FLAG_INTF_MASK (SENSOR_FLAG_DVP)
/* bit-2, 1, output yuv data; 0 raw data */
#define SENSOR_FLAG_YUV       (1 << 2)
#define SENSOR_FLAG_RAW       (0 << 2)
#define SENSOR_FLAG_DATA_MASK (SENSOR_FLAG_YUV)
#define SENSOR_FLAG_DTS_MASK  \
	(SENSOR_FLAG_CH_MASK | SENSOR_FLAG_INTF_MASK | SENSOR_FLAG_DATA_MASK)

/* determined by sensor driver, not dts */
#define SENSOR_FLAG_8BIT      (1 << 8)
#define SENSOR_FLAG_10BIT     (1 << 9)
#define SENSOR_FLAG_12BIT     (1 << 10)

#define OUTTO_SENSO_CLOCK     24000000

#define HOST_MODULE_CHANNEL_0 0
#define HOST_MODULE_CHANNEL_1 1

#define SENSOR_DATA_TYPE_YUV  0
#define SENSOR_DATA_TYPE_RAW  1

/* ANSI Color codes */
#define VT(CODES)  "\033[" CODES "m"
#define VT_NORMAL  VT("")
#define VT_RED     VT("0;32;31")
#define VT_GREEN   VT("1;32")
#define VT_YELLOW  VT("1;33")
#define VT_BLUE    VT("1;34")
#define VT_PURPLE  VT("0;35")

#define xprintk(fmt, ...) \
	pr_info("%s()->%d " fmt, __func__, __LINE__, ## __VA_ARGS__)

#define _DBG(color, fmt, ...)  \
	xprintk(color "" fmt VT_NORMAL, ## __VA_ARGS__)

#define _INFO(color, fmt, ...) \
	xprintk(color "::" fmt ""VT_NORMAL, ## __VA_ARGS__)

/* mainly used in test code */
#define INFO_PURLPLE(fmt, args...) _INFO(VT_PURPLE, fmt, ## args)
#define INFO_RED(fmt, args...)     _INFO(VT_RED, fmt, ## args)
#define INFO_GREEN(fmt, args...)   _INFO(VT_GREEN, fmt, ## args)
#define INFO_BLUE(fmt, args...)    _INFO(VT_BLUE, fmt, ## args)

#define QQVGA_WIDTH     160
#define QQVGA_HEIGHT    120

#define QVGA_WIDTH      320
#define QVGA_HEIGHT     240

#define CIF_WIDTH       352
#define CIF_HEIGHT      288

#define VGA_WIDTH       640
#define VGA_HEIGHT      480

#define SVGA_WIDTH	800
#define SVGA_HEIGHT	600

#define XGA_WIDTH	1024
#define XGA_HEIGHT	768

#define WXGA_WIDTH	1280
#define WXGA_HEIGHT	720

#define V720P_WIDTH     (WXGA_WIDTH)
#define V720P_HEIGHT    (WXGA_HEIGHT)

#define SXGA_WIDTH	1280
#define SXGA_HEIGHT	960

#define V1080P_WIDTH    1920
#define V1080P_HEIGHT   1080

#define UXGA_WIDTH	1600
#define UXGA_HEIGHT	1200

#define QXGA_WIDTH	2048
#define QXGA_HEIGHT	1536

#define QSXGA_WIDTH     2592
#define QSXGA_HEIGHT	1944

struct mipi_setting {
	unsigned char mipi_en;
	unsigned char lan_num;    /*0~3*/
	unsigned char contex0_en;
	unsigned char contex0_virtual_num;
	/*MIPI_YUV422 MIPI_RAW8 MIPI_RAW10 MIPI_RAW12*/
	unsigned char contex0_data_type;
	unsigned char clk_settle_time;
	unsigned char clk_term_time;
	unsigned char data_settle_time;
	unsigned char data_term_time;
	unsigned char crc_en;
	unsigned char ecc_en;
	unsigned char hclk_om_ent_en;
	unsigned char lp11_not_chek;
	unsigned char hsclk_edge;   /*0: rising edge; 1: falling edge*/
	unsigned char clk_lane_direction; /*0:obverse; 1:inverse*/
	unsigned char lane0_map;
	unsigned char lane1_map;
	unsigned char lane2_map;
	unsigned char lane3_map;
	unsigned char color_seq;
	unsigned int csi_clk;
};

struct host_module_setting_t {
	unsigned char hs_pol;           /*0: active low 1:active high*/
	unsigned char vs_pol;           /*0: active low 1:active high*/
	unsigned char clk_edge;         /*0: rasing edge 1:falling edge*/
	/*0: BG/GR, U0Y0V0Y1, 1: GR/BG, V0Y0U0Y1,
	*2: GB/RG, Y0U0Y1V0, 3: RG/GB, Y0V0Y1U0*/
	unsigned char color_seq;
};

struct v4l2_ctl_cmd_info {
	unsigned int  id;
	int min;
	int max;
	unsigned int step;
	int def;
};

struct v4l2_ctl_cmd_info_menu {
	unsigned int  id;
	int max;
	int mask;
	int def;
};

/****************************************************/
/******************for Edge ctrl************************/
/***strength also control Auto or Manual Edge Control Mode **/
/******** see also OV2643_MANUAL_EDGE_CTRL* *********/
/****************************************************/

struct module_edge_ctrl {
	unsigned char strength;
	unsigned char threshold;
	unsigned char upper;
	unsigned char lower;
};

/* ****************/
/*** module info ***/
/* ****************/
struct module_info {
	unsigned long		flags;
	struct module_edge_ctrl	edgectrl;
	unsigned int video_devnum;
	struct mipi_setting *mipi_cfg;
	struct host_module_setting_t *module_cfg;
};


struct dts_gpio {
	int num;
	int active_level;  /* 1: high level active, 0:low level active */
};

struct dts_regulator {
	struct regulator *regul;
	unsigned int min; /* uV */
	unsigned int max;
};

struct module_regulators {
	int avdd_use_gpio;  /* 0: regul, 1: use gpio */
	union {
		struct dts_gpio gpio;
		struct dts_regulator regul;
	} avdd;

	int dvdd_use_gpio;  /* 0: regul, 1: use gpio */
	struct dts_gpio dvdd_gpio;
	struct dts_regulator dvdd;

	int dovdd_use_gpio;  /* 0: regul, 1: use gpio */
	union {
		struct dts_gpio gpio;
		struct dts_regulator regul;
	} dovdd;
};

struct sensor_pwd_info {
	int flag;  /* sensor supports: front only, rear only, or dual */
	struct dts_gpio gpio_rear;
	struct dts_gpio gpio_front;
	struct dts_gpio gpio_front_reset;
	struct dts_gpio gpio_rear_reset;
	struct dts_gpio gpio_power;
	struct dts_gpio gpio_rear_power;
	struct dts_gpio gpio_front_power;
	struct clk *ch_clk[2];
};

struct dts_sensor_config {
	int rear; /* 1: rear sensor, 0: front sensor */
	int channel; /* 0: channel-1, 1: channel-2 */
	int data_type; /* 0: output YUV data, 1: RAW data */
	int host; /* bus_id of soc_camera_link */
	int i2c_adapter;
	enum v4l2_mbus_type bus_type; /* dvp or mipi */
	struct device *dev; /* sensor's platform device */
	struct device_node *dn;
	struct pinctrl *mfp;
};

#define DECLARE_DTS_SENSOR_CFG(name) \
	static struct dts_sensor_config name = { \
	.rear = 1, .host = 0, .i2c_adapter = 1, \
}

/**
 * set current sensor mode, preview(or video) and capture
 */
enum {
	ACTS_PREVIEW_MODE = 0,
	ACTS_CAPTURE_MODE,
	ACTS_VIDEO_MODE,
};

/**
 * using sensor as a front/rear camera
 */
enum {
	ACTS_CAM_SENSOR_FRONT = 0,
	ACTS_CAM_SENSOR_REAR,
};

#define V4L2_CID_CAM_CV_MODE _IOW('v', BASE_VIDIOC_PRIVATE + 0, int)

bool fornt_sensor_detected;
bool rear_sensor_detected;

#endif  /*__OWL_DEVICE_H__*/

