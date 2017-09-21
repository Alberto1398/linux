/*
 * Actions OWL SoCs ISP driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Shiheng Tan <tanshiheng@actions-semi.com>
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

#ifndef __BISP_DRV_H__
#define __BISP_DRV_H__

#if 1
#define uint8 unsigned char
#define int8 signed char
#define uint16 unsigned short
#define int16 signed short
#define int32 signed int
#define uint32 unsigned int
#ifndef phys_addr_t
#define phys_addr_t unsigned int
#endif
#else
#include "types.h"
#endif

struct bisp_blc_t {
	uint8 ben;
	uint16 ngr_offset;
	uint16 nr_offset;
	uint16 nb_offset;
	uint16 ngb_offset;
};

struct bisp_dpc_t {
	uint16 bd_en;
	uint16 nd_num;
	uint16 nd_th;
	uint16 bnr_en;
	uint16 bnr_th1;
	uint16 bnr_th2;
};

struct bisp_clsc_t {
	uint16 blsc_en;
	uint16 nbw;
	int bsplit;
	phys_addr_t ncoeff_addr;
	phys_addr_t ncoeff_addr_next;
};

struct bisp_gain_t {
	uint8 nidx;
	uint16 nrgain;
	uint16 nggain;
	uint16 nbgain;
};

struct bisp_rgb_gamma_t {
	uint32 ben;
	uint16 rgb_gamma[3][64];
};

struct bisp_mcc_t {
	uint16 ben;
	uint32 mcc[5];		/*2^14 Q10 */
};

struct bisp_csc_t {
	uint8 nidx;
	uint16 ny_off;
	uint16 ncb_off;
	uint16 ncr_off;
	uint16 nyr;
	uint16 nyg;
	uint16 nyb;
	uint16 nur;
	uint16 nug;
	uint16 nub;
	uint16 nvr;
	uint16 nvg;
	uint16 nvb;
};

struct bisp_bc_t {
	uint8 ben;
	uint8 noffset;
	uint32 ngtbl[64];
	uint32 nutbl[128];
};

struct bisp_fcs_t {
	uint8 ben;
	uint32 nutbl[64];
};

struct bisp_ccs_t {
	uint8 ben;
	uint8 net;		/*0-30 */
	uint8 nct1;		/*0-5 */
	uint8 nct2;		/*0-32 */
	uint8 nyt1;		/*0-255 */
};

struct bisp_bsch_t {
	uint16 bri_level;
	uint16 sta_level;
	uint16 con_offset;
	uint16 con_level;
	uint16 hue_level0;
	uint16 hue_level1;
};

struct bisp_cfixed_t {
	uint8 ncb_fixed;
	uint8 ncr_fixed;
	uint8 ncb_val;
	uint8 ncr_val;
	uint8 ny_invert;
};

struct bisp_region_t {
	uint8 nidx;
	uint16 nsx;
	uint16 nsy;
	uint16 nww;
	uint16 nwh;
};

struct bisp_temp_t {
	uint8 nidx;
	uint32 ncoeff[18];
};

struct bisp_wps_t {
	uint8 nidx;
	uint32 ncoeff[16];
};

struct bisp_af_param_t {
	int af_al;
	int nr_thold;
	int tshift;
};

struct bisp_af_region_t {
	int af_inc;
	int win_size;
	unsigned int start_x;
	unsigned int start_y;
	unsigned int skip_wx;
	unsigned int skip_wy;
};

struct bisp_ads_t {
	/*isp out addr */
	phys_addr_t yphy_addr;
	/*isp stat buffer addr */
	phys_addr_t sphy_addr;
	uint32 ngain;
	uint32 nexp;
};

struct bisp_frame_info_t {
	struct bisp_gain_t ngain;
	struct bisp_gain_t ngain2;
	struct bisp_ads_t nfrm;
};

struct bisp_stat_buffers_info_t {
	int chid;
	unsigned int buffer_num;	/* <16 */
	phys_addr_t phy_addr[16];
};

struct bisp_rawstore_buffers_info_t {
	int chid;
	int nskip_frm;
	unsigned int nraw_num;
	unsigned int nraw_size;
	phys_addr_t praw_phy[8];
};

struct module_name_t {
	char buf[16];
	int rev[4];
};

#define BRAWISP_DRV_IOC_MAGIC_NUMBER             'I'

/*isp:ctrl*/
#define BRI_S_CHID       _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x100, int)
#define BRI_S_EN         _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x101,\
				unsigned int)

/*isp:statistics*/
#define BRI_S_SBS        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x102,\
				struct bisp_stat_buffers_info_t)
#define BRI_S_SCRP       _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x103,\
				struct bisp_region_t)
#define BRI_S_SCRP2      _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x104,\
				struct bisp_region_t)
#define BRI_S_PCSC       _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x105,\
				struct bisp_csc_t)
#define BRI_S_TEMP       _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x106,\
				struct bisp_temp_t)
#define BRI_S_WPS        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x107,\
				struct bisp_wps_t)
#define BRI_S_WPS2       _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x108,\
				struct bisp_wps_t)

/*isp:raw store & read back*/
#define BRI_S_RSEN       _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x109,\
				unsigned int)
#define BRI_S_RBS        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x10a,\
				struct bisp_rawstore_buffers_info_t)

/*isp:pipeline*/
#define BRI_S_BLC        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x10b,\
				struct bisp_blc_t)
#define BRI_S_DPC        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x10c,\
				struct bisp_dpc_t)
#define BRI_S_DPCNR      _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x10d,\
				struct bisp_dpc_t)
#define BRI_S_LSC        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x10e,\
				struct bisp_clsc_t)
#define BRI_S_BPITHD      _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x10f,\
				unsigned int)
#define BRI_S_GN         _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x110,\
				struct bisp_gain_t)
#define BRI_S_RGBGAMMA    _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x111,\
				struct bisp_rgb_gamma_t)
#define BRI_S_MCC        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x112,\
				struct bisp_mcc_t)
#define BRI_S_GN2        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x113,\
				struct bisp_gain_t)
#define BRI_S_CSC        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x114,\
				struct bisp_csc_t)
#define BRI_S_BC        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x115,\
				struct bisp_bc_t)
#define BRI_S_FCS        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x116,\
				struct bisp_fcs_t)
#define BRI_S_CCS        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x117,\
				struct bisp_ccs_t)
#define BRI_S_BRI        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x118,\
				struct bisp_bsch_t)
#define BRI_S_STA        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x119,\
				struct bisp_bsch_t)
#define BRI_S_CON        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x11a,\
				struct bisp_bsch_t)
#define BRI_S_HUE        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x11b,\
				struct bisp_bsch_t)
#define BRI_S_CFX        _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x11c,\
				struct bisp_cfixed_t)

/*isp:get info*/
#define BRI_G_SIF       _IOWR(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x200,\
				struct bisp_ads_t)
#define BRI_G_FIF        _IOWR(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x201,\
				struct bisp_frame_info_t)
#define BRI_G_CRP        _IOWR(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x202,\
				struct bisp_region_t)
#define BRI_G_MDL        _IOWR(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x203,\
				struct module_name_t)
#define BRI_G_GN         _IOWR(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x204,\
				struct bisp_gain_t)
#define BRI_G_GN2        _IOWR(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x205,\
				struct bisp_gain_t)
#define BRI_G_BV         _IOWR(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x206,\
				struct bisp_blc_t)

/*isp:auto focus*/
#define BRI_S_AF_ENABLE  _IO(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x300)
#define BRI_S_AF_DISABLE _IO(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x301)
#define BRI_S_AF_WINS    _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x302,\
				struct bisp_af_region_t)
#define BRI_S_AF_PARAM   _IOW(BRAWISP_DRV_IOC_MAGIC_NUMBER, 0x303,\
				struct bisp_af_param_t)

#endif				/* __BISP_DRV_H__ */
