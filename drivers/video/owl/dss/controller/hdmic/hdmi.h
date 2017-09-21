/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/9/9: Created by Lipeng.
 */
#ifndef __OWL_HDMI_H
#define __OWL_HDMI_H

#include <video/owl_dss.h>

/*
 * some definition for HDCP
 */
#define M0_LENGTH		8
#define BSTATUS_LENGTH		2
#define KSV_LENGTH		5
#define KSV_RESERVED		3

#define KEY_LENGTH		280
#define KEY_ROW_LENGTH		40
#define KEY_COL_LENGTH		7

#define VERIFY_LENGTH		20

#define MAX_SHA_1_INPUT_LENGTH	704

enum hdmi_core_hdmi_dvi {
	HDMI_DVI = 0,
	HDMI_HDMI = 1,
	MHL_24BIT = 2,
	MHL_PK = 3
};

enum SRC_SEL {
	VITD = 0,
	DE,
	SRC_MAX
};

enum hdmi_core_packet_mode {
	HDMI_PACKETMODERESERVEDVALUE = 0,
	HDMI_PACKETMODE24BITPERPIXEL = 4,
	HDMI_PACKETMODE30BITPERPIXEL = 5,
	HDMI_PACKETMODE36BITPERPIXEL = 6,
	HDMI_PACKETMODE48BITPERPIXEL = 7
};

enum hdmi_packing_mode {
	HDMI_PACK_10b_RGB_YUV444 = 0,
	HDMI_PACK_24b_RGB_YUV444_YUV422 = 1,
	HDMI_PACK_20b_YUV422 = 2,
	HDMI_PACK_ALREADYPACKED = 7
};

enum hdmi_pixel_format {
	RGB444 = 0,
	YUV444 = 2
};

enum hdmi_deep_color {
	color_mode_24bit = 0,
	color_mode_30bit = 1,
	color_mode_36bit = 2,
	color_mode_48bit = 3
};

/*
 * a configuration structure to convet HDMI resolutoins
 * between vid and owl_video_timings.
 * vid is some fix number defined by HDMI spec,
 * are used in EDID etc.
 */

enum hdmi_vid_table {
	VID640x480P_60_4VS3 = 1,
	VID720x480P_60_4VS3,
	VID1280x720P_60_16VS9 = 4,
	VID1920x1080I_60_16VS9,
	VID720x480I_60_4VS3,
	VID1920x1080P_60_16VS9 = 16,
	VID720x576P_50_4VS3,
	VID1280x720P_50_16VS9 = 19,
	VID1920x1080I_50_16VS9,
	VID720x576I_50_4VS3,
	VID1440x576P_50_4VS3 = 29,
	VID1920x1080P_50_16VS9 = 31,
	VID1920x1080P_24_16VS9,
	VID3840x2160p_24 = 93,
	VID3840x2160p_25,
	VID3840x2160p_30,
	VID4096x2160p_24 = 98,
	VID4096x2160p_25,
	VID4096x2160p_30,

	/* some specail VID */
	VID1280x1024p_60 = 124,
	VID2560x1024p_60 = 125,
	VID2560x1024p_75 = 126,
	VID3840x1080p_60 = 127,

	/* VERY VERY specail VID, must be the last one */
	VID1280x720P_60_DVI = 128,
};

enum hdmi_packet_type {
	PACKET_AVI_SLOT		= 0,
	PACKET_AUDIO_SLOT	= 1,
	PACKET_SPD_SLOT		= 2,
	PACKET_GBD_SLOT		= 3,
	PACKET_VS_SLOT		= 4,
	PACKET_HFVS_SLOT	= 5,
	PACKET_MAX,
};


struct hdmi_config {
	int			vid;

	struct owl_videomode	mode;

	bool			interlace;
	int			vstart;	/* Vsync start line */
	bool			repeat;	/* video data repetetion */
};

#define HDMI_EDID_LEN		1024

struct hdmi_edid {
	u8			edid_buf[HDMI_EDID_LEN];
	u8			device_support_vic[512];
	u8			hdmi_mode;
	u8			ycbcr444_support;
	bool			read_ok;
};

/*===========================================================================
 *				HDMI IP
 *=========================================================================*/
struct hdmi_ip;

struct hdmi_ip_ops {
	int (*init)(struct hdmi_ip *ip);
	void (*exit)(struct hdmi_ip *ip);

	int (*power_on)(struct hdmi_ip *ip);
	void (*power_off)(struct hdmi_ip *ip);
	bool (*is_power_on)(struct hdmi_ip *ip);

	void (*hpd_enable)(struct hdmi_ip *ip);
	void (*hpd_disable)(struct hdmi_ip *ip);
	bool (*hpd_is_pending)(struct hdmi_ip *ip);
	void (*hpd_clear_pending)(struct hdmi_ip *ip);
	bool (*cable_status)(struct hdmi_ip *ip);

	int (*video_enable)(struct hdmi_ip *ip);
	void (*video_disable)(struct hdmi_ip *ip);
	bool (*is_video_enabled)(struct hdmi_ip *ip);

	void (*refresh_3d_mode)(struct hdmi_ip *ip);

	int (*audio_enable)(struct hdmi_ip *ip);
	void (*audio_disable)(struct hdmi_ip *ip);

	/* generate HDMI packect to 'pkt' according to packect type ('no') */
	int (*packet_generate)(struct hdmi_ip *ip, uint32_t no, uint8_t *pkt);

	/* send packect with 'period', 0 period will stop the packect */
	int (*packet_send)(struct hdmi_ip *ip, uint32_t no, int period);

	void (*hdcp_init)(struct hdmi_ip *ip);
	void (*hdcp_reset)(struct hdmi_ip *ip);

	/*
	 * First Part of Authentication Protocol:
	 * Generate An
	 *			----send An & Aksv to device---->
	 *			<---read Bksv & REPEATER from device----
	 * Caculate Km
	 *   = Sigma(Akeys over Bksv)
	 * Caculate Ks, M0, R0
	 *   =hdcpBlkCipher(Km, REPEATER || An)
	 *
	 * 			<---read R0' from device----
	 * Verify R0=R0'
	 */
	int (*hdcp_an_generate)(struct hdmi_ip *ip, uint8_t *an);
	void (*hdcp_repeater_enable)(struct hdmi_ip *ip, bool enable);
	int (*hdcp_ks_m0_r0_generate)(struct hdmi_ip *ip, uint8_t *bksv,
				uint8_t hdcp_key[][KEY_COL_LENGTH * 2 + 1]);
	int (*hdcp_ri_get)(struct hdmi_ip *ip);
	void (*hdcp_m0_get)(struct hdmi_ip *ip, uint8_t *m0);

	/*
	 * Second Part of Authentication Protocol(if repeater=1):
	 *			<---Poll KSV list ready----
	 *			<---read KSV list & Bstatus & V'
	 * caculate V
	 *   =SHA-1(KSV list || Bstatus || M0)
	 * Verify V=V'
	 * NOTE: caculate and verify are all done by HDCP controller
	 */
	bool (*hdcp_vprime_verify)(struct hdmi_ip *ip, uint8_t *v,
				   uint8_t *ksvlist, uint8_t *bstatus,
				   uint8_t *m0);

	/*
	 * Third Part of Authentication Protocol:
	 * done by HDCP controller:
	 *	set HDCP to authenticated state and start encryption
	 */
	void (*hdcp_auth_start)(struct hdmi_ip *ip);


	void (*regs_dump)(struct hdmi_ip *ip);
};

struct hdmi_ip_settings {
	int hdmi_src;
	int vitd_color;
	int pixel_encoding;
	int color_xvycc;
	int deep_color;
	int hdmi_mode;
	int prelines;

	int channel_invert;
	int bit_invert;

	enum owl_3d_mode mode_3d;
};

struct hdmi_ip {
	struct platform_device		*pdev;
	const struct hdmi_ip_ops	*ops;

	void __iomem			*base;
	int				irq;
	void				*pdata;

	struct hdmi_ip_settings		settings;

	const struct hdmi_config	*cfg;
};


static inline void hdmi_ip_writel(struct hdmi_ip *ip, const uint16_t idx,
				  uint32_t val)
{
	writel(val, ip->base + idx);
}

static inline uint32_t hdmi_ip_readl(struct hdmi_ip *ip, const uint16_t idx)
{
	return readl(ip->base + idx);
}

int hdmi_ip_register(struct hdmi_ip *ip);
void hdmi_ip_unregister(struct hdmi_ip *ip);

int hdmi_ip_generic_suspend(struct hdmi_ip *ip);
int hdmi_ip_generic_resume(struct hdmi_ip *ip);

/*===========================================================================
 *				HDMI DDC
 *=========================================================================*/
int hdmi_ddc_init(void);

int hdmi_ddc_edid_read(char segment_index, char segment_offset, char *pbuf);
int hdmi_ddc_hdcp_write(const char *buf, unsigned short offset, int count);
int hdmi_ddc_hdcp_read(char *buf, unsigned short offset, int count);

/*===========================================================================
 *				HDMI edid
 *=========================================================================*/
int hdmi_edid_parse(struct hdmi_edid *edid);

/*===========================================================================
 *				HDMI packet
 *=========================================================================*/
int hdmi_packet_gen_infoframe(struct hdmi_ip *ip);

/*===========================================================================
 *				HDMI hdcp
 *=========================================================================*/
int hdmi_hdcp_init(struct hdmi_ip *ip);
void hdmi_hdcp_exit(struct hdmi_ip *ip);
int hdmi_hdcp_enable(struct hdmi_ip *ip, bool enable);
/*===========================================================================
 *				HDMI cec
 *=========================================================================*/
int hdmi_cec_init(struct hdmi_ip *ip);

#endif
