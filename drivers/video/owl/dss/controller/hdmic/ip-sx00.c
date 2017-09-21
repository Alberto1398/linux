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
#define DEBUGX
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/timer.h>

#include <video/owl_dss.h>

#include "hdmi.h"
#include "ip-sx00.h"

enum ip_sx00_ic_type {
	IC_TYPE_S500,
	IC_TYPE_S700,
	IC_TYPE_S900,
};

struct ip_sx00_hwdiff {
	enum ip_sx00_ic_type		ic_type;

	int				hp_start;
	int				hp_end;
	int				vp_start;
	int				vp_end;
	int				mode_start;
	int				mode_end;

	uint32_t			pll_reg;
	int				pll_24m_en;
	int				pll_en;

	uint32_t			pll_debug0_reg;
	uint32_t			pll_debug1_reg;
};

struct ip_sx00_data {
	struct hdmi_ip			ip;

	const struct ip_sx00_hwdiff	*hwdiff;

	void __iomem			*cmu_base;
	void __iomem			*sps_base;

	struct reset_control		*rst;
	struct clk			*hdmi_dev_clk;

	struct regulator		*tmds_avdd;

	/* used for registers setting */
	uint32_t			pll_val;
	uint32_t			pll_debug0_val;
	uint32_t			pll_debug1_val;
	uint32_t			tx_1;
	uint32_t			tx_2;
	uint32_t			phyctrl_1;
	uint32_t			phyctrl_2;
};
#define IP_TO_IP_DATA(ip) container_of((ip), struct ip_sx00_data, ip)


static const struct ip_sx00_hwdiff ip_s500 = {
	.ic_type			= IC_TYPE_S500,
	.hp_start			= 16,
	.hp_end				= 28,
	.vp_start			= 4,
	.vp_end				= 15,
	.mode_start			= 0,
	.mode_end			= 0,

	.pll_reg			= 0x18,
	.pll_24m_en			= 23,
	.pll_en				= 3,

	.pll_debug0_reg			= 0xEC,
	.pll_debug1_reg			= 0xF4,
};

static const struct ip_sx00_hwdiff ip_s700 = {
	.ic_type			= IC_TYPE_S700,
	.hp_start			= 16,
	.hp_end				= 28,
	.vp_start			= 4,
	.vp_end				= 15,
	.mode_start			= 0,
	.mode_end			= 0,

	.pll_reg			= 0x18,
	.pll_24m_en			= 23,
	.pll_en				= 3,

	.pll_debug0_reg			= 0xF0,
	.pll_debug1_reg			= 0xF4,
};

static const struct ip_sx00_hwdiff ip_s900 = {
	.ic_type			= IC_TYPE_S900,
	.hp_start			= 15,
	.hp_end				= 27,
	.vp_start			= 0,
	.vp_end				= 11,
	.mode_start			= 18,
	.mode_end			= 19,

	.pll_reg			= 0x18,
	.pll_24m_en			= 8,
	.pll_en				= 0,

	.pll_debug0_reg			= 0xEC,
	.pll_debug1_reg			= 0xFC,
};

/*
 * HDMI IP configurations
 */

static int ip_update_reg_values(struct hdmi_ip *ip)
{
	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	ip_data->pll_val = 0;

	/* bit31 = 0, debug mode disable, default value if it is not set */
	ip_data->pll_debug0_val = 0;
	ip_data->pll_debug1_val = 0;

	ip_data->tx_1 = 0;
	ip_data->tx_2 = 0;

	/*
	 * now only support 24bit, normal 2D or Side-by-Side Half 3D
	 * or Top-and-Bottom 3D,
	 * Frame 3D, 30bit, 36bit, TODO
	 */

	if (ip_data->hwdiff->ic_type == IC_TYPE_S500) {
		switch (ip->cfg->vid) {
		case VID640x480P_60_4VS3:
			ip_data->pll_val = 0x00000008;	/* 25.2MHz */

			ip_data->tx_1 = 0x819c2984;
			ip_data->tx_2 = 0x18f80f87;
			break;

		case VID720x576P_50_4VS3:
		case VID720x480P_60_4VS3:
			ip_data->pll_val = 0x00010008;	/* 27MHz */

			ip_data->tx_1 = 0x819c2984;
			ip_data->tx_2 = 0x18f80f87;
			break;

		case VID1280x720P_60_16VS9:
		case VID1280x720P_50_16VS9:
			ip_data->pll_val = 0x00040008;	/* 74.25MHz */

			ip_data->tx_1 = 0x81942986;
			ip_data->tx_2 = 0x18f80f87;
			break;

		case VID1920x1080P_60_16VS9:
		case VID1920x1080P_50_16VS9:
			ip_data->pll_val = 0x00060008;	/* 148.5MHz */

			ip_data->tx_1 = 0x8190284f;
			ip_data->tx_2 = 0x18fa0f87;
			break;

		case VID3840x2160p_30:
		case VID3840x1080p_60:
		case VID4096x2160p_30:
			ip_data->pll_val = 0x00070008;	/* 297MHz */

			ip_data->tx_1 = 0x8086284F;
			ip_data->tx_2 = 0x000E0F01;
			break;

		default:
			return -EINVAL;
		}
	} else if (ip_data->hwdiff->ic_type == IC_TYPE_S700) {
		switch (ip->cfg->vid) {
		case VID640x480P_60_4VS3:
			ip_data->pll_val = 0x00000008;	/* 25.2MHz */

			ip_data->tx_1 = 0x819c2984;
			ip_data->tx_2 = 0x18f80f39;
			break;

		case VID720x576P_50_4VS3:
		case VID720x480P_60_4VS3:
			ip_data->pll_val = 0x00010008;	/* 27MHz */

			ip_data->tx_1 = 0x819c2984;
			ip_data->tx_2 = 0x18f80f39;
			break;

		case VID1280x720P_60_16VS9:
		case VID1280x720P_50_16VS9:
			ip_data->pll_val = 0x00040008;	/* 74.25MHz */

			ip_data->tx_1 = 0x81982984;
			ip_data->tx_2 = 0x18f80f39;
			break;

		case VID1920x1080P_60_16VS9:
		case VID1920x1080P_50_16VS9:
			ip_data->pll_val = 0x00060008;	/* 148.5MHz */

			ip_data->tx_1 = 0x81942988;
			ip_data->tx_2 = 0x18fe0f39;
			break;

		case VID3840x2160p_30:
		case VID3840x1080p_60:
		case VID4096x2160p_30:
			ip_data->pll_val = 0x00070008;	/* 297MHz */

			ip_data->tx_1 = 0x819029de;
			ip_data->tx_2 = 0x18fe0f39;
			break;

		default:
			return -EINVAL;
		}
	} else if (ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		if (ip->settings.hdmi_mode == MHL_24BIT) {
			switch (ip->cfg->vid) {
			case VID640x480P_60_4VS3:
				ip_data->pll_val = 0x400311;

				ip_data->phyctrl_1 = 0x0496f485;
				ip_data->phyctrl_2 = 0x2101b;
				break;

			case VID720x576P_50_4VS3:
			case VID720x480P_60_4VS3:
				ip_data->pll_val = 0x410311;

				ip_data->phyctrl_1 = 0x0496f485;
				ip_data->phyctrl_2 = 0x2101b;
				break;

			case VID1280x720P_60_16VS9:
			case VID1280x720P_50_16VS9:
				ip_data->pll_val = 0x440311;

				ip_data->phyctrl_2 = 0x2081b;
				ip_data->phyctrl_1 = 0x0497f885;
				break;

			case VID1920x1080P_60_16VS9:
			case VID1920x1080P_50_16VS9:
				ip_data->pll_val = 0x460311;

				ip_data->phyctrl_2 = 0x2001b;
				ip_data->phyctrl_1 = 0x04abfb05;
				break;

			default:
				return -EINVAL;
				break;
			}
		} else {
			switch (ip->cfg->vid) {
			case VID640x480P_60_4VS3:
				ip_data->pll_val = 0x00000008;	/* 25.2MHz */

				ip_data->tx_1 = 0x808c2904;
				ip_data->tx_2 = 0x00f00fc1;
				break;

			case VID720x576P_50_4VS3:
			case VID720x480P_60_4VS3:
				ip_data->pll_val = 0x00010008;	/* 27MHz */

				ip_data->tx_1 = 0x808c2904;
				ip_data->tx_2 = 0x00f00fc1;
				break;

			case VID1280x720P_60_16VS9:
			case VID1280x720P_50_16VS9:
				ip_data->pll_val = 0x00040008;	/* 74.25MHz */

				ip_data->tx_1 = 0x80882904;
				ip_data->tx_2 = 0x00f00fc1;
				break;

			case VID1920x1080P_60_16VS9:
			case VID1920x1080P_50_16VS9:
				ip_data->pll_val = 0x00060008;	/* 148.5MHz */

				ip_data->tx_1 = 0x80842846;
				ip_data->tx_2 = 0x00000FC1;
				break;

			case VID3840x2160p_30:
			case VID3840x1080p_60:
			case VID4096x2160p_30:
				ip_data->pll_val = 0x00070008;	/* 297MHz */
				ip_data->pll_debug0_val = 0x80000000;
				ip_data->pll_debug1_val = 0x0005f642;

				ip_data->tx_1 = 0x8080284F;
				ip_data->tx_2 = 0x000E0F01;
				break;

			case VID2560x1024p_75:
				ip_data->pll_debug0_val = 0x80000000;
				ip_data->pll_debug1_val = 0x00056042;

				ip_data->tx_1 = 0x8080284F;
				ip_data->tx_2 = 0x000E0F01;
				break;

			case VID2560x1024p_60:
				ip_data->pll_debug0_val = 0x80000000;
				ip_data->pll_debug1_val = 0x00044642;

				ip_data->tx_1 = 0x8084284F;
				ip_data->tx_2 = 0x000E0FC1;
				break;

			case VID1280x1024p_60:
				ip_data->pll_val = 0x00050008;	/* 108MHz */

				ip_data->tx_1 = 0x80882904;
				ip_data->tx_2 = 0x00f00fc1;
				break;

			default:
				return -EINVAL;
			}

			/* set tx pll locked to clkhdmi's fall edge */
			ip_data->tx_1 = REG_SET_VAL(ip_data->tx_1, 1, 13, 13);
		}
	}

	return 0;
}

/* devclk will used by HDMI HPD */
static void __ip_devclk_enable(struct hdmi_ip *ip)
{
	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	clk_prepare_enable(ip_data->hdmi_dev_clk);
}

static void __ip_devclk_disable(struct hdmi_ip *ip)
{
	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	clk_disable_unprepare(ip_data->hdmi_dev_clk);
}

static void __ip_tmds_ldo_enable(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 &&
	    ip_data->tmds_avdd != NULL) {
		/* S900 uses external TMDS LDO */

		regulator_enable(ip_data->tmds_avdd);
		regulator_set_voltage(ip_data->tmds_avdd, 1800000, 1800000);
	}

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		/* SPS_LDO_CTL */

		val = readl(ip_data->sps_base + 0x14);
		val &= 0xfffffff0;

		if (ip->cfg->vid == VID3840x2160p_30 ||
		    ip->cfg->vid == VID3840x1080p_60 ||
		    ip->cfg->vid == VID4096x2160p_30 ||
		    ip->cfg->vid == VID2560x1024p_60 ||
		    ip->cfg->vid == VID2560x1024p_75)
			val |= 0xe;
		else
			val |= 0xa;

		writel(val, ip_data->sps_base + 0x14);	/* SPS_LDO_CTL */
	}

	/* S500 & S700 uses internal TMDS LDO */
	if (ip_data->hwdiff->ic_type == IC_TYPE_S700 &&
		ip_data->tmds_avdd != NULL) {
		/* if in 4k mode adjusted LDO to 3.3v.
		 * Attention, the "tmds_avdd" is external "tmds_avcc"
		 * for s700 hdmi
		 * */
		regulator_enable(ip_data->tmds_avdd);
		if (ip->cfg->vid == VID3840x2160p_30 ||
		    ip->cfg->vid == VID3840x1080p_60 ||
		    ip->cfg->vid == VID4096x2160p_30) {
			regulator_set_voltage(ip_data->tmds_avdd, 3300000, 3300000);
		} else {
			regulator_set_voltage(ip_data->tmds_avdd, 3100000, 3100000);
		}
	}
	/* do not enable HDMI lane util video enable */
	val = ip_data->tx_2 & (~((0xf << 8) | (1 << 17)));
	hdmi_ip_writel(ip, HDMI_TX_2, val);
}

static void __ip_tmds_ldo_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 &&
	    ip_data->tmds_avdd != NULL) {
		/* S900 uses external TMDS LDO */

		/* set a default value in case it cannot be disable */
		regulator_set_voltage(ip_data->tmds_avdd, 1800000, 1800000);
		regulator_disable(ip_data->tmds_avdd);
	}

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		val = readl(ip_data->sps_base + 0x14);	/* SPS_LDO_CTL */
		val &= 0xfffffff0;
		val |= 0xa;
		writel(val, ip_data->sps_base + 0x14);	/* SPS_LDO_CTL */
	}

	/* S500 & S700 uses internal TMDS LDO */
	val = hdmi_ip_readl(ip, HDMI_TX_2);
	val = REG_SET_VAL(val, 0, 27, 27);	/* LDO_TMDS power off */
	hdmi_ip_writel(ip, HDMI_TX_2, val);
}


/* set HDMI_TX_1, txpll_pu, txpll, vco, scale, driver, etc. */
static void __ip_phy_enable(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	/* TMDS Encoder */
	val = hdmi_ip_readl(ip, TMDS_EODR0);
	val = REG_SET_VAL(val, 1, 31, 31);
	hdmi_ip_writel(ip, TMDS_EODR0, val);

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 &&
	    ip->settings.hdmi_mode == MHL_24BIT) {
		hdmi_ip_writel(ip, MHL_PHYCTL1, ip_data->phyctrl_1);
		hdmi_ip_writel(ip, MHL_PHYCTL2, ip_data->phyctrl_2);
	} else {
		hdmi_ip_writel(ip, HDMI_TX_1, ip_data->tx_1);
	}
}

static void __ip_phy_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	/* TMDS Encoder */
	val = hdmi_ip_readl(ip, TMDS_EODR0);
	val = REG_SET_VAL(val, 0, 31, 31);
	hdmi_ip_writel(ip, TMDS_EODR0, val);

	/* txpll_pu */
	val = hdmi_ip_readl(ip, HDMI_TX_1);
	val = REG_SET_VAL(val, 0, 23, 23);
	hdmi_ip_writel(ip, HDMI_TX_1, val);
}

static void __ip_pll_enable(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 &&
	    ip->settings.hdmi_mode == MHL_24BIT) {
		/* TODO, FIXME */
		writel(ip_data->pll_val,
		       ip_data->cmu_base + ip_data->hwdiff->pll_reg);
		return;
	}

	/* 24M enable */
	val = readl(ip_data->cmu_base + ip_data->hwdiff->pll_reg);
	val |= (1 << ip_data->hwdiff->pll_24m_en);
	writel(val, ip_data->cmu_base + ip_data->hwdiff->pll_reg);
	mdelay(1);

	/* set PLL, only bit18:16 of pll_val is used */
	val = readl(ip_data->cmu_base + ip_data->hwdiff->pll_reg);
	val &= ~(0x7 << 16);
	val |= (ip_data->pll_val & (0x7 << 16));
	writel(val, ip_data->cmu_base + ip_data->hwdiff->pll_reg);
	mdelay(1);

	/* set debug PLL */
	writel(ip_data->pll_debug0_val,
	       ip_data->cmu_base + ip_data->hwdiff->pll_debug0_reg);
	writel(ip_data->pll_debug1_val,
	       ip_data->cmu_base + ip_data->hwdiff->pll_debug1_reg);

	/* enable PLL */
	val = readl(ip_data->cmu_base + ip_data->hwdiff->pll_reg);
	val |= (1 << ip_data->hwdiff->pll_en);
	writel(val, ip_data->cmu_base + ip_data->hwdiff->pll_reg);
	mdelay(1);

	/* S900&S700 need TDMS clock calibration */
	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 ||
	    ip_data->hwdiff->ic_type == IC_TYPE_S700) {
		val = hdmi_ip_readl(ip, CEC_DDC_HPD);

		/* 0 to 1, start calibration */
		val = REG_SET_VAL(val, 0, 20, 20);
		hdmi_ip_writel(ip, CEC_DDC_HPD, val);

		udelay(10);

		val = REG_SET_VAL(val, 1, 20, 20);
		hdmi_ip_writel(ip, CEC_DDC_HPD, val);

		while (1) {
			val = hdmi_ip_readl(ip, CEC_DDC_HPD);
			if ((val >> 24) & 0x1)
				break;
		}
	}
}

static void __ip_pll_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	val = readl(ip_data->cmu_base + ip_data->hwdiff->pll_reg);
	val &= ~(1 << ip_data->hwdiff->pll_24m_en);
	val &= ~(1 << ip_data->hwdiff->pll_en);
	writel(val, ip_data->cmu_base + ip_data->hwdiff->pll_reg);

	/* reset TVOUTPLL */
	writel(0, ip_data->cmu_base + ip_data->hwdiff->pll_reg);

	/* reset TVOUTPLL_DEBUG0 & TVOUTPLL_DEBUG1 */
	if (ip_data->hwdiff->ic_type == IC_TYPE_S700 ||
	    ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		writel(0x0, ip_data->cmu_base
		       + ip_data->hwdiff->pll_debug0_reg);
		writel(0x2614a, ip_data->cmu_base
		       + ip_data->hwdiff->pll_debug1_reg);
	}
}

static void __ip_core_deepcolor_mode_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.deep_color, 17, 16);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_pixel_fomat_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.pixel_encoding, 5, 4);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_preline_config(struct hdmi_ip *ip)
{
	int preline;
	uint32_t val = 0;

	preline = ip->settings.prelines;
	preline = (preline <= 0 ? 1 : preline);
	preline = (preline > 16 ? 16 : preline);

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, preline - 1, 23, 20);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_3d_mode_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);

	if (ip->settings.mode_3d == OWL_3D_MODE_FRAME)
		val = REG_SET_VAL(val, 1, 8, 8);
	else
		val = REG_SET_VAL(val, 0, 8, 8);

	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_mode_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.hdmi_mode,
			  ip_data->hwdiff->mode_end,
			  ip_data->hwdiff->mode_start);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);

	/* ATM9009's HDMI mode, should set HDCP_KOWR & HDCP_OWR */
	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 &&
	    ip->settings.hdmi_mode == 1) {
		/*42,end 651,star 505 */
		hdmi_ip_writel(ip, HDCP_KOWR,
			       HDCP_KOWR_HDCPREKEYKEEPOUTWIN(0x2a) |
			       HDCP_KOWR_HDCPVERKEEPOUTWINEND(0x28b) |
			       HDCP_KOWR_HDCPVERTKEEPOUTWINSTART(0x1f9));

		/*HDCP1.1 Mode: start 510,end 526 */
		hdmi_ip_writel(ip, HDCP_OWR, HDCP_OWR_HDCPOPPWINEND(0x20e) |
			       HDCP_OWR_HDCPOPPWINSTART(0x1fe));
	}
}

static void __ip_core_invert_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.bit_invert, 28, 28);
	val = REG_SET_VAL(val, ip->settings.channel_invert, 29, 29);

	hdmi_ip_writel(ip, HDMI_SCHCR, val);
	return;
}

static void __ip_core_colordepth_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	uint32_t mode = ip->settings.deep_color;

	val = hdmi_ip_readl(ip, HDMI_GCPCR);

	val = REG_SET_VAL(val, mode, 7, 4);
	val = REG_SET_VAL(val, 1, 31, 31);

	if (mode > HDMI_PACKETMODE24BITPERPIXEL)
		val = REG_SET_VAL(val, 1, 30, 30);
	else
		val = REG_SET_VAL(val, 0, 30, 30);

	/* clear specify avmute flag in gcp packet */
	val = REG_SET_VAL(val, 1, 1, 1);

	hdmi_ip_writel(ip, HDMI_GCPCR, val);
}

static void __ip_core_input_src_config(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_ICR);

	if (ip->settings.hdmi_src == VITD) {
		val = REG_SET_VAL(val, 0x01, 24, 24);
		val = REG_SET_VAL(val, ip->settings.vitd_color, 23, 0);
	} else {
		val = REG_SET_VAL(val, 0x00, 24, 24);
	}

	hdmi_ip_writel(ip, HDMI_ICR, val);
}

static void __ip_video_format_config(struct hdmi_ip *ip)
{
	uint32_t val;
	uint32_t val_hp, val_vp;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);
	const struct owl_videomode *mode = &ip->cfg->mode;

	val_hp = mode->xres + mode->hbp + mode->hfp + mode->hsw;
	val_vp = mode->yres + mode->vbp + mode->vfp + mode->vsw;

	dev_dbg(&ip->pdev->dev, "x %d %d %d %d\n",
		mode->xres, mode->hbp, mode->hfp, mode->hsw);
	dev_dbg(&ip->pdev->dev, "x %d %d %d %d\n",
		mode->yres, mode->vbp, mode->vfp, mode->vsw);

	val = hdmi_ip_readl(ip, HDMI_VICTL);

	val = REG_SET_VAL(val, val_hp - 1, ip_data->hwdiff->hp_end,
			  ip_data->hwdiff->hp_start);

	if (ip->cfg->interlace == 0)
		val = REG_SET_VAL(val, val_vp - 1, ip_data->hwdiff->vp_end,
				  ip_data->hwdiff->vp_start);
	else
		val = REG_SET_VAL(val, val_vp * 2, ip_data->hwdiff->vp_end,
				  ip_data->hwdiff->vp_start);

	dev_dbg(&ip->pdev->dev, "%s: val = %x hp = %x vp=%x\n",
		__func__, val, val_hp, val_vp);

	hdmi_ip_writel(ip, HDMI_VICTL, val);
}

static void __ip_video_interface_config(struct hdmi_ip *ip)
{
	uint32_t val;

	const struct owl_videomode *mode = &ip->cfg->mode;

	dev_dbg(&ip->pdev->dev, "%s: interlace %d\n",
		__func__, ip->cfg->interlace);

	if (ip->cfg->interlace == 0) {
		val = 0;
		hdmi_ip_writel(ip, HDMI_VIVSYNC, val);

		val = hdmi_ip_readl(ip, HDMI_VIVHSYNC);

		if (ip->cfg->vstart != 1) {
			val = REG_SET_VAL(val, mode->hsw - 1, 8, 0);
			val = REG_SET_VAL(val, ip->cfg->vstart - 2, 23, 12);
			val = REG_SET_VAL(val, ip->cfg->vstart + mode->vsw - 2,
					  27, 24);
		} else {
			val = REG_SET_VAL(val, mode->hsw - 1, 8, 0);
			val = REG_SET_VAL(val, mode->yres + mode->vbp
					  + mode->vfp + mode->vsw - 1,
					  23, 12);
			val = REG_SET_VAL(val, mode->vsw - 1, 27, 24);
		}
		hdmi_ip_writel(ip, HDMI_VIVHSYNC, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIVHSYNC 0x%x\n",
			__func__, val);

		/*
		 * VIALSEOF = (yres + vbp + vsp - 1) | ((vbp + vfp - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIALSEOF);
		val = REG_SET_VAL(val, ip->cfg->vstart - 1 + mode->vsw
				  + mode->vbp + mode->yres - 1, 23, 12);
		val = REG_SET_VAL(val, ip->cfg->vstart - 1 + mode->vsw
				  + mode->vbp - 1, 10, 0);
		hdmi_ip_writel(ip, HDMI_VIALSEOF, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIALSEOF 0x%x\n",
			__func__, val);

		val = 0;
		hdmi_ip_writel(ip, HDMI_VIALSEEF, val);

		/*
		 * VIADLSE = (xres + hbp + hsp - 1) | ((hbp + hsw - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIADLSE);
		val = REG_SET_VAL(val, mode->hbp +  mode->hsw - 1,
				  11, 0);
		val = REG_SET_VAL(val, mode->xres + mode->hbp
				  + mode->hsw - 1, 28, 16);
		hdmi_ip_writel(ip, HDMI_VIADLSE, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIADLSE 0x%x\n",
			__func__, val);
	} else {
		val = 0;
		hdmi_ip_writel(ip, HDMI_VIVSYNC, val);

		/*
		 * VIVHSYNC =
		 * (hsw -1 ) | ((yres + vsw + vfp + vbp - 1 ) << 12)
		 *  | (vfp -1 << 24)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIVHSYNC);
		val = REG_SET_VAL(val, mode->hsw - 1, 8, 0);
		val = REG_SET_VAL(val, (mode->yres + mode->vbp
				  + mode->vfp + mode->vsw) * 2, 22, 12);
		val = REG_SET_VAL(val, mode->vfp * 2, 22, 12);/* ??, TODO */
		hdmi_ip_writel(ip, HDMI_VIVHSYNC, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIVHSYNC 0x%x\n",
			__func__, val);

		/*
		 * VIALSEOF = (yres + vbp + vfp - 1) | ((vbp + vfp - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIALSEOF);
		val = REG_SET_VAL(val, mode->vbp + mode->vfp  - 1,
				  22, 12);
		val = REG_SET_VAL(val, (mode->yres + mode->vbp
					+ mode->vfp) * 2, 10, 0);
		hdmi_ip_writel(ip, HDMI_VIALSEOF, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIALSEOF 0x%x\n",
			__func__, val);

		val = 0;
		hdmi_ip_writel(ip, HDMI_VIALSEEF, val);

		/*
		 * VIADLSE = (xres + hbp + hsp - 1) | ((hbp + hsw - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIADLSE);
		val = REG_SET_VAL(val, mode->hbp +  mode->hsw - 1,
				  27, 16);
		val = REG_SET_VAL(val, mode->xres + mode->hbp
				  + mode->hsw - 1, 11, 0);
		hdmi_ip_writel(ip, HDMI_VIADLSE, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIADLSE 0x%x\n",
			__func__, val);
	}
}

static void __ip_video_interval_packet_config(struct hdmi_ip *ip)
{
	uint32_t val;

	switch (ip->cfg->vid) {
	case VID640x480P_60_4VS3:
	case VID720x480P_60_4VS3:
	case VID720x576P_50_4VS3:
		val = 0x701;
		break;

	case VID1280x720P_60_16VS9:
	case VID1280x720P_50_16VS9:
	case VID1920x1080P_50_16VS9:
		val = 0x1107;
		break;

	case VID1920x1080P_60_16VS9:
	case VID3840x1080p_60:
		val = 0x1105;
		break;

	default:
		val = 0x1107;
		break;
	}

	hdmi_ip_writel(ip, HDMI_DIPCCR, val);
}

static void __ip_video_timing_config(struct hdmi_ip *ip)
{
	bool vsync_pol, hsync_pol, interlace, repeat;
	uint32_t val;
	const struct owl_videomode *mode = &ip->cfg->mode;

	vsync_pol = ((mode->sync & DSS_SYNC_VERT_HIGH_ACT) == 0);
	hsync_pol = ((mode->sync & DSS_SYNC_HOR_HIGH_ACT) == 0);

	interlace = ip->cfg->interlace;
	repeat = ip->cfg->repeat;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, hsync_pol, 1, 1);
	val = REG_SET_VAL(val, vsync_pol, 2, 2);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);

	val = hdmi_ip_readl(ip, HDMI_VICTL);
	val = REG_SET_VAL(val, interlace, 28, 28);
	val = REG_SET_VAL(val, repeat, 29, 29);
	hdmi_ip_writel(ip, HDMI_VICTL, val);
}

static void __ip_video_start(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 1, 0, 0);
	hdmi_ip_writel(ip, HDMI_CR, val);

	val = hdmi_ip_readl(ip, HDMI_TX_2);
	val = REG_SET_VAL(val, (ip_data->tx_2 >> 8) & 0xf, 11, 8);
	val = REG_SET_VAL(val, (ip_data->tx_2 >> 17) & 0x1, 17, 17);
	hdmi_ip_writel(ip, HDMI_TX_2, val);
}

static void __ip_video_stop(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_TX_2);
	val = REG_SET_VAL(val, 0x0, 11, 8);
	val = REG_SET_VAL(val, 0x0, 17, 17);
	hdmi_ip_writel(ip, HDMI_TX_2, val);

	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 0, 0, 0);
	hdmi_ip_writel(ip, HDMI_CR, val);
}


static int ip_sx00_init(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		/* init LDO to a fix value */
		val = readl(ip_data->sps_base + 0x14);
		val &= 0xfffffff0;
		val |= 0xa;
		writel(val, ip_data->sps_base + 0x14);	/* SPS_LDO_CTL */
	}

	return 0;
}

static void ip_sx00_exit(struct hdmi_ip *ip)
{
	dev_dbg(&ip->pdev->dev, "%s\n", __func__);
}

static bool ip_sx00_is_video_enabled(struct hdmi_ip *ip);
static int ip_sx00_power_on(struct hdmi_ip *ip)
{
	int ret = 0;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	/* only reset IP when it is not video enabled */
	if (!ip_sx00_is_video_enabled(ip))
		reset_control_assert(ip_data->rst);

	__ip_devclk_enable(ip);
	mdelay(1);

	if (!ip_sx00_is_video_enabled(ip)) {
		reset_control_deassert(ip_data->rst);
		mdelay(1);
	}

	return ret;
}

static void ip_sx00_power_off(struct hdmi_ip *ip)
{
	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	reset_control_assert(ip_data->rst);

	__ip_devclk_disable(ip);
}

static void ip_sx00_hpd_enable(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);

	val = REG_SET_VAL(val, 0x0f, 27, 24);	/* hotplug debounce */
	val = REG_SET_VAL(val, 0x01, 31, 31);	/* enable hotplug interrupt */
	val = REG_SET_VAL(val, 0x01, 28, 28);	/* enable hotplug function */
	val = REG_SET_VAL(val, 0x00, 30, 30);	/* not clear pending bit */

	hdmi_ip_writel(ip, HDMI_CR, val);
}

static void ip_sx00_hpd_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);

	val = REG_SET_VAL(val, 0x00, 31, 31);	/* disable hotplug interrupt */
	val = REG_SET_VAL(val, 0x00, 28, 28);	/* enable hotplug function */
	val = REG_SET_VAL(val, 0x01, 30, 30);	/* clear pending bit */

	hdmi_ip_writel(ip, HDMI_CR, val);
}

static bool ip_sx00_hpd_is_pending(struct hdmi_ip *ip)
{
	return (hdmi_ip_readl(ip, HDMI_CR) & (1 << 30)) != 0;
}

static void ip_sx00_hpd_clear_pending(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 0x01, 30, 30);	/* clear pending bit */
	hdmi_ip_writel(ip, HDMI_CR, val);
}

static bool ip_sx00_cable_status(struct hdmi_ip *ip)
{
	if ((hdmi_ip_readl(ip, HDMI_CR) & (1 << 29)) &&
	    ((hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 14)) ||
	     (hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 12)) ||
	     (hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 10)) ||
	     (hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 8))))
		return true;
	else
		return false;
}


static int ip_sx00_video_enable(struct hdmi_ip *ip)
{
	int ret = 0;

	ret = ip_update_reg_values(ip);
	if (ret < 0) {
		dev_err(&ip->pdev->dev, "ip cfg is invalid\n");
		return ret;
	}

	__ip_tmds_ldo_enable(ip);
	udelay(500);

	__ip_phy_enable(ip);

	__ip_pll_enable(ip);
	mdelay(10);

	__ip_video_timing_config(ip);
	__ip_video_format_config(ip);
	__ip_video_interface_config(ip);
	__ip_video_interval_packet_config(ip);
	__ip_core_input_src_config(ip);
	__ip_core_pixel_fomat_config(ip);
	__ip_core_preline_config(ip);
	__ip_core_deepcolor_mode_config(ip);
	__ip_core_mode_config(ip);
	__ip_core_invert_config(ip);
	__ip_core_colordepth_config(ip);
	__ip_core_3d_mode_config(ip);

	hdmi_packet_gen_infoframe(ip);

	__ip_video_start(ip);

	return 0;
}

static void ip_sx00_video_disable(struct hdmi_ip *ip)
{
	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	/* TODO for s700 set avcc value 2.9v, at default */
	if (ip_data->hwdiff->ic_type == IC_TYPE_S700 &&
		ip_data->tmds_avdd != NULL) {
		/*
		 * Attention, the "tmds_avdd" is external "tmds_avcc"
		 * for s700 hdmi
		 * */
		regulator_enable(ip_data->tmds_avdd);
		regulator_set_voltage(ip_data->tmds_avdd, 2900000, 2900000);
	}
	__ip_video_stop(ip);
	__ip_pll_disable(ip);
	__ip_phy_disable(ip);
	__ip_tmds_ldo_disable(ip);
}

static bool ip_sx00_is_video_enabled(struct hdmi_ip *ip)
{
	return (hdmi_ip_readl(ip, HDMI_CR) & 0x01) != 0;
}
static void ip_sx00_refresh_3d_mode(struct hdmi_ip *ip)
{
	uint32_t val;

	/* hdmi module disable */;
	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 0, 0, 0);
	hdmi_ip_writel(ip, HDMI_CR, val);

	hdmi_packet_gen_infoframe(ip);

	msleep(500);
	/* hdmi module enable */;
	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 1, 0, 0);
	hdmi_ip_writel(ip, HDMI_CR, val);

}

static int ip_sx00_audio_enable(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_ICR);
	val |= (1 << 25);
	hdmi_ip_writel(ip, HDMI_ICR, val);

	return 0;
}

static int ip_sx00_audio_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_ICR);
	val &= ~(1 << 25);
	hdmi_ip_writel(ip, HDMI_ICR, val);

	return 0;
}

static int __ip_enable_write_ram_packet(struct hdmi_ip *ip)
{
	int i;
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_OPCR);
	val |= (1 << 31);
	hdmi_ip_writel(ip, HDMI_OPCR, val);

	i = 100;
	while (i--) {
		val = hdmi_ip_readl(ip, HDMI_OPCR);
		val = val >> 31;
		if (val == 0)
			break;
		udelay(1);
	}

	return 0;
}


static int ip_sx00_packet_generate(struct hdmi_ip *ip, uint32_t no,
				   uint8_t *pkt)
{
	uint8_t tpkt[36];
	int i, j;
	uint32_t reg[9];
	uint32_t addr = 126 + no * 14;


	if (no >= PACKET_MAX)
		return -1;

	/* Packet Header */
	tpkt[0] = pkt[0];
	tpkt[1] = pkt[1];
	tpkt[2] = pkt[2];
	tpkt[3] = 0;

	/* Packet Word0 */
	tpkt[4] = pkt[3];
	tpkt[5] = pkt[4];
	tpkt[6] = pkt[5];
	tpkt[7] = pkt[6];

	/* Packet Word1 */
	tpkt[8] = pkt[7];
	tpkt[9] = pkt[8];
	tpkt[10] = pkt[9];
	tpkt[11] = 0;

	/* Packet Word2 */
	tpkt[12] = pkt[10];
	tpkt[13] = pkt[11];
	tpkt[14] = pkt[12];
	tpkt[15] = pkt[13];

	/* Packet Word3 */
	tpkt[16] = pkt[14];
	tpkt[17] = pkt[15];
	tpkt[18] = pkt[16];
	tpkt[19] = 0;

	/* Packet Word4 */
	tpkt[20] = pkt[17];
	tpkt[21] = pkt[18];
	tpkt[22] = pkt[19];
	tpkt[23] = pkt[20];

	/* Packet Word5 */
	tpkt[24] = pkt[21];
	tpkt[25] = pkt[22];
	tpkt[26] = pkt[23];
	tpkt[27] = 0;

	/* Packet Word6 */
	tpkt[28] = pkt[24];
	tpkt[29] = pkt[25];
	tpkt[30] = pkt[26];
	tpkt[31] = pkt[27];

	/* Packet Word7 */
	tpkt[32] = pkt[28];
	tpkt[33] = pkt[29];
	tpkt[34] = pkt[30];
	tpkt[35] = 0;

	/* for s900 change ?? TODO */
	for (i = 0; i < 9; i++) {
		reg[i] = 0;
		for (j = 0; j < 4; j++)
			reg[i] |= (tpkt[i * 4 + j]) << (j * 8);
	}

	hdmi_ip_writel(ip, HDMI_OPCR,    (1 << 8) | (addr & 0xff));
	hdmi_ip_writel(ip, HDMI_ORP6PH,  reg[0]);
	hdmi_ip_writel(ip, HDMI_ORSP6W0, reg[1]);
	hdmi_ip_writel(ip, HDMI_ORSP6W1, reg[2]);
	hdmi_ip_writel(ip, HDMI_ORSP6W2, reg[3]);
	hdmi_ip_writel(ip, HDMI_ORSP6W3, reg[4]);
	hdmi_ip_writel(ip, HDMI_ORSP6W4, reg[5]);
	hdmi_ip_writel(ip, HDMI_ORSP6W5, reg[6]);
	hdmi_ip_writel(ip, HDMI_ORSP6W6, reg[7]);
	hdmi_ip_writel(ip, HDMI_ORSP6W7, reg[8]);

	__ip_enable_write_ram_packet(ip);

	return 0;
}

static int ip_sx00_packet_send(struct hdmi_ip *ip, uint32_t no, int period)
{
	uint32_t val;

	if (no > PACKET_MAX || no < 0)
		return -1;

	if (period > 0xf || period < 0)
		return -1;

	val = hdmi_ip_readl(ip, HDMI_RPCR);
	val &= (~(1 << no));
	hdmi_ip_writel(ip, HDMI_RPCR,  val);

	val = hdmi_ip_readl(ip, HDMI_RPCR);
	val &= (~(0xf << (no * 4 + 8)));
	hdmi_ip_writel(ip, HDMI_RPCR, val);

	/* enable and set period */
	if (period) {
		val = hdmi_ip_readl(ip, HDMI_RPCR);
		val |= (period << (no * 4 + 8));
		hdmi_ip_writel(ip, HDMI_RPCR,  val);

		val = hdmi_ip_readl(ip, HDMI_RPCR);
		val |= (1 << no);
		hdmi_ip_writel(ip, HDMI_RPCR,  val);
	}

	return 0;
}

static void __hdcp_set_opportunity_window(struct hdmi_ip *ip)
{
	/*42,end 651,star 505 */
	hdmi_ip_writel(ip, HDCP_KOWR,
		       HDCP_KOWR_HDCPREKEYKEEPOUTWIN(0x2a) |
		       HDCP_KOWR_HDCPVERKEEPOUTWINEND(0x28b) |
		       HDCP_KOWR_HDCPVERTKEEPOUTWINSTART(0x1f9));

	/*HDCP1.1 Mode: start 510,end 526 */
	hdmi_ip_writel(ip, HDCP_OWR,
		       HDCP_OWR_HDCPOPPWINEND(0x20e) |
		       HDCP_OWR_HDCPOPPWINSTART(0x1fe));
}

static void ip_sx00_hdcp_init(struct hdmi_ip *ip)
{
	uint32_t val;

	/* set 'keep out window' and 'opportunity window' */
	__hdcp_set_opportunity_window(ip);

	/* set RiRate to 128 and PjRate to 16 */
	hdmi_ip_writel(ip, HDCP_ICR, 0x7f0f);

	/* enable HDCP1.1 features */
	val = hdmi_ip_readl(ip, HDCP_CR);
	val |= HDCP_CR_EN1DOT1_FEATURE;
	hdmi_ip_writel(ip, HDCP_CR, val);
}

static void ip_sx00_hdcp_reset(struct hdmi_ip *ip)
{
	uint32_t val;

	/*
	 * disable HDCP encryption and wait for
	 * HDCP_Encrypt_Status to zero
	 */
	val = hdmi_ip_readl(ip, HDCP_CR);
	val &= ~HDCP_CR_HDCP_ENCRYPTENABLE;
	hdmi_ip_writel(ip, HDCP_CR, val);

	WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SR)
			   & HDCP_SR_HDCP_ENCRYPT_STATUS) == 0, 1000);

	val = hdmi_ip_readl(ip, HDCP_CR);

	val &= ~HDCP_CR_ENRIUPDINT;		/* disable Ri update INT */
	val &= ~HDCP_CR_ENPJUPDINT;		/* disable Pj update INT */
	val |= HDCP_CR_FORCETOUNAUTHENTICATED;	/* Force to authenticated */

	hdmi_ip_writel(ip, HDCP_CR, val);
}

/* get An influence from CRC64 */
static int ip_sx00_hdcp_an_generate(struct hdmi_ip *ip, uint8_t *an)
{
	uint32_t val;
	int i;

	val = hdmi_ip_readl(ip, HDCP_CR);
	val |= HDCP_CR_ANINFREQ;
	hdmi_ip_writel(ip, HDCP_CR, val);

	val = hdmi_ip_readl(ip, HDCP_CR);
	val |= HDCP_CR_ANINFLUENCEMODE;
	hdmi_ip_writel(ip, HDCP_CR, val);

	/* write 1 to trigger to generate An */
	val = hdmi_ip_readl(ip, HDCP_CR);
	val |= HDCP_CR_AUTHREQUEST;
	hdmi_ip_writel(ip, HDCP_CR, val);

	dev_dbg(&ip->pdev->dev, "%s: wait An ready\n", __func__);
	WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SR) & HDCP_SR_ANREADY) != 0,
			  1000);
	dev_dbg(&ip->pdev->dev, "%s: wait An ready OK\n", __func__);

	/* leave An influence mode */
	val = hdmi_ip_readl(ip, HDCP_CR);
	val &= (~HDCP_CR_ANINFLUENCEMODE);
	hdmi_ip_writel(ip, HDCP_CR, val);

	/*
	 * Convert HDCP An from bit endien to little endien
	 * HDCP An should stored in little endien,
	 * but HDCP HW store in bit endien.
	 */
	an[0] = 0x18;
	val = hdmi_ip_readl(ip, HDCP_ANLR);
	an[1] = val & 0xff;
	an[2] = (val >> 8) & 0xff;
	an[3] = (val >> 16) & 0xff;
	an[4] = (val >> 24) & 0xff;

	val = hdmi_ip_readl(ip, HDCP_ANMR);

	an[5] = val & 0xff;
	an[6] = (val >> 8) & 0xff;
	an[7] = (val >> 16) & 0xff;
	an[8] = (val >> 24) & 0xff;

	for (i = 0; i < 9; i++)
		dev_dbg(&ip->pdev->dev, "%s: an[%d]: 0x%x\n",
			__func__, i, an[i]);

	return 0;
}

static void ip_sx00_hdcp_repeater_enable(struct hdmi_ip *ip, bool enable)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDCP_CR);

	if (enable)
		val |= HDCP_CR_DOWNSTRISREPEATER;
	else
		val &= ~HDCP_CR_DOWNSTRISREPEATER;

	hdmi_ip_writel(ip, HDCP_CR, val);
}

/* convert INT8 number to little endien number */
static void __hdcp_c2ln14(uint8_t *num, uint8_t *a)
{
	int i;
	int n = 14;
	for (i = 0; i < 11; i++)
		num[i] = 0;

	for (i = 0; i < n; i++) {
		if (i % 2) {
			if (a[n - i - 1] >= '0' && a[n - i - 1] <= '9')
				num[i / 2] |= (a[n - i - 1] - '0') << 4;
			else if (a[n - i - 1] >= 'a' && a[n - i - 1] <= 'f')
				num[i / 2] |= (a[n - i - 1] - 'a' + 10) << 4;
			else if (a[n - i - 1] >= 'A' && a[n - i - 1] <= 'F')
				num[i / 2] |= (a[n - i - 1] - 'A' + 10) << 4;
		} else {
			if (a[n - i - 1] >= '0' && a[n - i - 1] <= '9')
				num[i / 2] |= (a[n - i - 1] - '0');
			else if (a[n - i - 1] >= 'a' && a[n - i - 1] <= 'f')
				num[i / 2] |= (a[n - i - 1] - 'a' + 10);
			else if (a[n - i - 1] >= 'A' && a[n - i - 1] <= 'F')
				num[i / 2] |= (a[n - i - 1] - 'A' + 10);
		}
	}
}

static void __hdcp_set_km(struct hdmi_ip *ip, unsigned char *key, int pnt)
{
	uint32_t val;
	uint8_t dKey[11];

	dKey[0] = key[0] ^ pnt;
	dKey[1] = ~key[1] ^ dKey[0];
	dKey[2] = key[2] ^ dKey[1];
	dKey[3] = key[3] ^ dKey[2];
	dKey[4] = key[4] ^ dKey[3];
	dKey[5] = ~key[5] ^ dKey[4];
	dKey[6] = ~key[6] ^ dKey[5];

	/* write to HW */
	val = pnt | (dKey[0] << 8) | (dKey[1] << 16) | (dKey[2] << 24);
	hdmi_ip_writel(ip, HDCP_DPKLR, val);

	val = dKey[3] | (dKey[4] << 8) | (dKey[5] << 16) | (dKey[6] << 24);
	hdmi_ip_writel(ip, HDCP_DPKMR, val);

	/* wait accumulation finish */
	WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SR) &
			   HDCP_SR_CURDPKACCDONE) != 0, 10000);
}

static int ip_sx00_hdcp_ks_m0_r0_generate(struct hdmi_ip *ip, uint8_t *bksv,
				uint8_t hdcp_key[][KEY_COL_LENGTH * 2 + 1])
{
	uint8_t key[11];
	int i, j;
	uint32_t val;

	val = hdmi_ip_readl(ip, HDCP_CR);
	val &= ~HDCP_CR_HDCP_ENCRYPTENABLE;	/* force Encryption disable */
	val |= HDCP_CR_RESETKMACC;		/* reset Km accumulation */
	hdmi_ip_writel(ip, HDCP_CR, val);

	for (i = 0; i < 5; i++) {
		for (j = 0; j < 8; j++) {
			if (bksv[i] & (1 << j)) {
				__hdcp_c2ln14(key, hdcp_key[i * 8 + j]);
				__hdcp_set_km(ip, key, 0x55);
			}
		}
	}

	val = hdmi_ip_readl(ip, HDCP_CR);
	val &= ~HDCP_CR_ENRIUPDINT;	/* disable Ri update interrupt */
	hdmi_ip_writel(ip, HDCP_CR, val);

	val = hdmi_ip_readl(ip, HDCP_SR);
	val |= HDCP_SR_RIUPDATED;	/* clear Ri updated pending bit */
	hdmi_ip_writel(ip, HDCP_SR, val);

	val = hdmi_ip_readl(ip, HDCP_CR);
	val |= HDCP_CR_AUTHCOMPUTE;	/* compute Km, Ks, M0, R0 */
	hdmi_ip_writel(ip, HDCP_CR, val);

	/* wait Ri updated */
	WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SR) & HDCP_SR_RIUPDATED) != 0,
			  100000);
	return 0;
}

static int ip_sx00_hdcp_ri_get(struct hdmi_ip *ip)
{
	return (hdmi_ip_readl(ip, HDCP_LIR) >> 16) & 0xffff;
}

static void ip_sx00_hdcp_m0_get(struct hdmi_ip *ip, uint8_t *m0)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDCP_MILR);
	m0[0] = (uint8_t)(val & 0xff);
	m0[1] = (uint8_t)((val >> 8) & 0xff);
	m0[2] = (uint8_t)((val >> 16) & 0xff);
	m0[3] = (uint8_t)((val >> 24) & 0xff);

	val = hdmi_ip_readl(ip, HDCP_MIMR);
	m0[4] = (uint8_t)(val & 0xff);
	m0[5] = (uint8_t)((val >> 8) & 0xff);
	m0[6] = (uint8_t)((val >> 16) & 0xff);
	m0[7] = (uint8_t)((val >> 24) & 0xff);
}


static bool ip_sx00_hdcp_vprime_verify(struct hdmi_ip *ip, uint8_t *v,
				       uint8_t *ksvlist, uint8_t *bstatus,
				       uint8_t *m0)
{
	uint8_t sha_1_input_data[MAX_SHA_1_INPUT_LENGTH];

	uint32_t val;

	int i, j;
	int data_counter;
	int nblock;
	int cnt2 = bstatus[0] & 0x7f;

	for (i = 0; i < MAX_SHA_1_INPUT_LENGTH; i++)
		sha_1_input_data[i] = 0;

	for (data_counter = 0;
	     data_counter < cnt2 * KSV_LENGTH + BSTATUS_LENGTH + M0_LENGTH;
	     data_counter++) {
		if (data_counter < cnt2 * KSV_LENGTH)
			sha_1_input_data[data_counter] = ksvlist[data_counter];
		else if ((data_counter >= cnt2 * KSV_LENGTH) &&
			 (data_counter < cnt2 * KSV_LENGTH + BSTATUS_LENGTH))
			sha_1_input_data[data_counter] = bstatus[data_counter
					- (cnt2 * KSV_LENGTH)];
		else
			sha_1_input_data[data_counter] = m0[data_counter
				- (cnt2 * KSV_LENGTH + BSTATUS_LENGTH)];
	}

	sha_1_input_data[data_counter] = 0x80;	/* block ending signal */

	nblock = (int)(data_counter / 64);

	/* total SHA counter high */
	sha_1_input_data[nblock * 64 + 62]
		= (uint8_t)(((data_counter * 8) >> 8) & 0xff);

	/* total SHA counter low */
	sha_1_input_data[nblock * 64 + 63]
		= (uint8_t)((data_counter * 8) & 0xff);

	/* reset SHA write pointer */
	val = hdmi_ip_readl(ip, HDCP_SHACR);
	hdmi_ip_writel(ip, HDCP_SHACR, val | 0x1);

	/* wait reset completing */
	WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SHACR) & 0x1) == 0, 100000);

	/* set new SHA-1 operation */
	val = hdmi_ip_readl(ip, HDCP_SHACR);
	hdmi_ip_writel(ip, HDCP_SHACR, val | 0x2);

	for (i = 0; i < nblock; i++) {
		for (j = 0; j < 16; j++) {
			val = (sha_1_input_data[i * 64 + (j * 4 + 0)] << 24) |
			      (sha_1_input_data[i * 64 + (j * 4 + 1)] << 16) |
			      (sha_1_input_data[i * 64 + (j * 4 + 2)] << 8) |
			      (sha_1_input_data[i * 64 + (j * 4 + 3)]);
			hdmi_ip_writel(ip, HDCP_SHADR, val);
			hdmi_ip_readl(ip, HDCP_SHADR);
		}

		 /* Start 512bit SHA operation */
		val = hdmi_ip_readl(ip, HDCP_SHACR);
		hdmi_ip_writel(ip, HDCP_SHACR, val | 0x4);

		/* after 512bit SHA operation, this bit will be set to 1 */
		WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SHACR) & 0x8) != 0,
				  100000);

		/* clear SHAfirst bit */
		val = hdmi_ip_readl(ip, HDCP_SHACR);
		hdmi_ip_writel(ip, HDCP_SHACR, val | 0xfd);
		hdmi_ip_readl(ip, HDCP_SHACR);
	}

	for (j = 0; j < 16; j++) {
		/* P_HDCP_SHADR */
		val = (sha_1_input_data[nblock * 64 + (j * 4 + 0)] << 24) |
		      (sha_1_input_data[nblock * 64 + (j * 4 + 1)] << 16) |
		      (sha_1_input_data[nblock * 64 + (j * 4 + 2)] << 8) |
		      (sha_1_input_data[nblock * 64 + (j * 4 + 3)]);
		hdmi_ip_writel(ip, HDCP_SHADR, val);
		hdmi_ip_readl(ip, HDCP_SHADR);
	}

	/* Start 512bit SHA operation */
	val = hdmi_ip_readl(ip, HDCP_SHACR);
	hdmi_ip_writel(ip, HDCP_SHACR, val | 0x4);

	/* after 512bit SHA operation, this bit will be set to 1 */
	WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SHACR) & 0x8) != 0, 100000);

	/* write V */
	val = (v[3] << 24) | (v[2] << 16) | (v[1] << 8) | (v[0] << 0);
	hdmi_ip_writel(ip, HDCP_SHADR, val);
	hdmi_ip_readl(ip, HDCP_SHADR);

	val = (v[7] << 24) | (v[6] << 16) | (v[5] << 8) | (v[4] << 0);
	hdmi_ip_writel(ip, HDCP_SHADR, val);
	hdmi_ip_readl(ip, HDCP_SHADR);

	val = (v[11] << 24) | (v[10] << 16) | (v[9] << 8) | (v[8] << 0);
	hdmi_ip_writel(ip, HDCP_SHADR, val);
	hdmi_ip_readl(ip, HDCP_SHADR);

	val = (v[15] << 24) | (v[14] << 16) | (v[13] << 8) | (v[12] << 0);
	hdmi_ip_writel(ip, HDCP_SHADR, val);
	hdmi_ip_readl(ip, HDCP_SHADR);

	val = (v[19] << 24) | (v[18] << 16) | (v[17] << 8) | (v[16] << 0);
	hdmi_ip_writel(ip, HDCP_SHADR, val);
	hdmi_ip_readl(ip, HDCP_SHADR);

	/* wait Vmatch */
	WAIT_WITH_TIMEOUT((hdmi_ip_readl(ip, HDCP_SHACR) & 0x10) != 0, 500000);

	return !!(hdmi_ip_readl(ip, HDCP_SHACR) & 0x10);
}

static void ip_sx00_hdcp_auth_start(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDCP_CR);
	val |= HDCP_CR_DEVICEAUTHENTICATED;	/* set to authenticated state */
	val |= HDCP_CR_HDCP_ENCRYPTENABLE;	/* start encryption */
	hdmi_ip_writel(ip, HDCP_CR, val);
}

static void ip_sx00_regs_dump(struct hdmi_ip *ip)
{
#define DUMPREG(name,r) pr_info("%s %08x\n",name,hdmi_ip_readl(ip, r))
	DUMPREG("HDMI_VICTL  value is ", HDMI_VICTL);
	DUMPREG("HDMI_VIVSYNC  value is ", HDMI_VIVSYNC);
	DUMPREG("HDMI_VIVHSYNC  value is ", HDMI_VIVHSYNC);
	DUMPREG("HDMI_VIALSEOF  value is ", HDMI_VIALSEOF);
	DUMPREG("HDMI_VIALSEEF  value is ", HDMI_VIALSEEF);
	DUMPREG("HDMI_VIADLSE  value is ", HDMI_VIADLSE);
	DUMPREG("HDMI_VR  value is ", HDMI_VR);
	DUMPREG("HDMI_CR  value is ", HDMI_CR);
	DUMPREG("HDMI_SCHCR  value is ", HDMI_SCHCR);
	DUMPREG("HDMI_ICR  value is ", HDMI_ICR);
	DUMPREG("HDMI_SCR  value is ", HDMI_SCR);
	DUMPREG("HDMI_LPCR  value is ", HDMI_LPCR);
	DUMPREG("HDCP_CR  value is ", HDCP_CR);
	DUMPREG("HDCP_SR  value is ", HDCP_SR);
	DUMPREG("HDCP_ANLR  value is ", HDCP_ANLR);
	DUMPREG("HDCP_ANMR  value is ", HDCP_ANMR);
	DUMPREG("HDCP_ANILR  value is ", HDCP_ANILR);
	DUMPREG("HDCP_ANIMR  value is ", HDCP_ANIMR);
	DUMPREG("HDCP_DPKLR  value is ", HDCP_DPKLR);
	DUMPREG("HDCP_DPKMR  value is ", HDCP_DPKMR);
	DUMPREG("HDCP_LIR  value is ", HDCP_LIR);
	DUMPREG("HDCP_SHACR  value is ", HDCP_SHACR);
	DUMPREG("HDCP_SHADR  value is ", HDCP_SHADR);
	DUMPREG("HDCP_ICR  value is ", HDCP_ICR);
	DUMPREG("HDCP_KMMR  value is ", HDCP_KMMR);
	DUMPREG("HDCP_KMLR  value is ", HDCP_KMLR);
	DUMPREG("HDCP_MILR  value is ", HDCP_MILR);
	DUMPREG("HDCP_MIMR  value is ", HDCP_MIMR);
	DUMPREG("HDCP_KOWR  value is ", HDCP_KOWR);
	DUMPREG("HDCP_OWR  value is ", HDCP_OWR);

	DUMPREG("TMDS_STR0  value is ", TMDS_STR0);
	DUMPREG("TMDS_STR1  value is ", TMDS_STR1);
	DUMPREG("TMDS_EODR0  value is ", TMDS_EODR0);
	DUMPREG("TMDS_EODR1  value is ", TMDS_EODR1);
	DUMPREG("HDMI_ASPCR  value is ", HDMI_ASPCR);
	DUMPREG("HDMI_ACACR  value is ", HDMI_ACACR);
	DUMPREG("HDMI_ACRPCR  value is ", HDMI_ACRPCR);
	DUMPREG("HDMI_ACRPCTSR  value is ", HDMI_ACRPCTSR);
	DUMPREG("HDMI_ACRPPR value is ", HDMI_ACRPPR);
	DUMPREG("HDMI_GCPCR  value is ", HDMI_GCPCR);
	DUMPREG("HDMI_RPCR  value is ", HDMI_RPCR);
	DUMPREG("HDMI_RPRBDR  value is ", HDMI_RPRBDR);
	DUMPREG("HDMI_OPCR  value is ", HDMI_OPCR);
	DUMPREG("HDMI_DIPCCR  value is ", HDMI_DIPCCR);
	DUMPREG("HDMI_ORP6PH  value is ", HDMI_ORP6PH);
	DUMPREG("HDMI_ORSP6W0  value is ", HDMI_ORSP6W0);
	DUMPREG("HDMI_ORSP6W1  value is ", HDMI_ORSP6W1);
	DUMPREG("HDMI_ORSP6W2  value is ", HDMI_ORSP6W2);
	DUMPREG("HDMI_ORSP6W3  value is ", HDMI_ORSP6W3);
	DUMPREG("HDMI_ORSP6W4  value is ", HDMI_ORSP6W4);
	DUMPREG("HDMI_ORSP6W5  value is ", HDMI_ORSP6W5);
	DUMPREG("HDMI_ORSP6W6v  value is ", HDMI_ORSP6W6);
	DUMPREG("HDMI_ORSP6W7  value is ", HDMI_ORSP6W7);
	DUMPREG("HDMI_CECCR  value is ", HDMI_CECCR);
	DUMPREG("HDMI_CECRTCR  value is ", HDMI_CECRTCR);
	DUMPREG("HDMI_CRCCR  value is ", HDMI_CRCCR);
	DUMPREG("HDMI_CRCDOR  value is ", HDMI_CRCDOR);
	DUMPREG("HDMI_TX_1  value is ", HDMI_TX_1);
	DUMPREG("HDMI_TX_2  value is ", HDMI_TX_2);
	DUMPREG("CEC_DDC_HPD  value is ", CEC_DDC_HPD);
#undef DUMPREG
}

static const struct hdmi_ip_ops ip_sx00_ops = {
	.init =	ip_sx00_init,
	.exit =	ip_sx00_exit,

	.power_on = ip_sx00_power_on,
	.power_off = ip_sx00_power_off,

	.hpd_enable = ip_sx00_hpd_enable,
	.hpd_disable = ip_sx00_hpd_disable,
	.hpd_is_pending = ip_sx00_hpd_is_pending,
	.hpd_clear_pending = ip_sx00_hpd_clear_pending,
	.cable_status = ip_sx00_cable_status,

	.video_enable = ip_sx00_video_enable,
	.video_disable = ip_sx00_video_disable,
	.is_video_enabled = ip_sx00_is_video_enabled,

	.refresh_3d_mode = ip_sx00_refresh_3d_mode,

	.audio_enable = ip_sx00_audio_enable,
	.audio_disable = ip_sx00_audio_disable,

	.packet_generate = ip_sx00_packet_generate,
	.packet_send = ip_sx00_packet_send,

	.hdcp_init = ip_sx00_hdcp_init,
	.hdcp_reset = ip_sx00_hdcp_reset,
	.hdcp_an_generate = ip_sx00_hdcp_an_generate,
	.hdcp_repeater_enable = ip_sx00_hdcp_repeater_enable,
	.hdcp_ks_m0_r0_generate = ip_sx00_hdcp_ks_m0_r0_generate,
	.hdcp_ri_get = ip_sx00_hdcp_ri_get,
	.hdcp_m0_get = ip_sx00_hdcp_m0_get,
	.hdcp_vprime_verify = ip_sx00_hdcp_vprime_verify,
	.hdcp_auth_start = ip_sx00_hdcp_auth_start,

	.regs_dump = ip_sx00_regs_dump,
};

static const struct of_device_id ip_sx00_of_match[] = {
	{
		.compatible = "actions,s500-hdmi",
		.data = &ip_s500,
	},
	{
		.compatible = "actions,s700-hdmi",
		.data = &ip_s700,
	},
	{
		.compatible = "actions,s900-hdmi",
		.data = &ip_s900,
	},
	{}
};
MODULE_DEVICE_TABLE(of, ip_sx00_of_match);

static int ip_sx00_probe(struct platform_device *pdev)
{
	int ret = 0;

	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct of_device_id *match;

	struct ip_sx00_data *ip_data;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(ip_sx00_of_match, dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	ip_data = devm_kzalloc(dev, sizeof(*ip_data), GFP_KERNEL);
	if (ip_data == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, ip_data);

	ip_data->hwdiff = (struct ip_sx00_hwdiff *)match->data;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmu_base");
	if (!res)
		return -ENODEV;
	ip_data->cmu_base = ioremap(res->start, resource_size(res));

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sps_base");
	if (!res)
		return -ENODEV;
	ip_data->sps_base = ioremap(res->start, resource_size(res));

	if (IS_ERR(ip_data->cmu_base) || IS_ERR(ip_data->sps_base)) {
		dev_err(dev, "map registers error\n");
		return -ENODEV;
	}

	ip_data->hdmi_dev_clk = devm_clk_get(dev, "hdmi_dev");
	if (IS_ERR(ip_data->hdmi_dev_clk)) {
		dev_err(dev, "can't get hdmi clk\n");
		return -EINVAL;
	}

	ip_data->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(ip_data->rst)) {
		dev_err(dev, "can't get the reset\n");
		return PTR_ERR(ip_data->rst);
	}

	ip_data->tmds_avdd = regulator_get(dev, "tmds-avdd");
	if (IS_ERR(ip_data->tmds_avdd)) {
		dev_info(dev, "no tmds-avdd\n");
		ip_data->tmds_avdd = NULL;
	} else {
		dev_dbg(dev, "tmds_avdd %p, current is %duv\n",
			ip_data->tmds_avdd,
			regulator_get_voltage(ip_data->tmds_avdd));
	}

	ip_data->ip.pdev = pdev;
	ip_data->ip.ops = &ip_sx00_ops;

	ret = hdmi_ip_register(&ip_data->ip);
	if (ret < 0) {
		dev_err(dev, "hdmi_ip_register failed(%d)\n", ret);
		return ret;
	}

	return 0;
}

static int ip_sx00_remove(struct platform_device *pdev)
{
	struct ip_sx00_data *ip_data;

	pr_info("%s\n", __func__);

	ip_data = dev_get_drvdata(&pdev->dev);

	hdmi_ip_unregister(&ip_data->ip);

	return 0;
}

static int ip_sx00_suspend(struct device *dev)
{
	struct ip_sx00_data *ip_data;

	dev_info(dev, "%s\n", __func__);


	/* you can do something special */

	ip_data = dev_get_drvdata(dev);
	hdmi_ip_generic_suspend(&ip_data->ip);

	return 0;
}

static int ip_sx00_resume(struct device *dev)
{
	struct ip_sx00_data *ip_data;

	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	ip_data = dev_get_drvdata(dev);
	hdmi_ip_generic_resume(&ip_data->ip);

	return 0;
}

static UNIVERSAL_DEV_PM_OPS(ip_sx00_pm_ops, ip_sx00_suspend,
			    ip_sx00_resume, NULL);

static struct platform_driver ip_sx00_driver = {
	.probe		= ip_sx00_probe,
	.remove         = ip_sx00_remove,

	.driver         = {
		.name   = "hdmi-ip-sx00",
		.owner  = THIS_MODULE,
		.of_match_table = ip_sx00_of_match,
		.pm		= &ip_sx00_pm_ops,
	},
};

int __init ip_sx00_platform_init(void)
{
	pr_info("%s\n", __func__);

	if (platform_driver_register(&ip_sx00_driver) < 0) {
		pr_err("failed to register ip_sx00_driver\n");
		return -ENODEV;
	}

	return 0;
}

void __exit ip_sx00_platform_uninit(void)
{
	pr_info("%s\n", __func__);

	platform_driver_unregister(&ip_sx00_driver);
}

module_init(ip_sx00_platform_init);
module_exit(ip_sx00_platform_uninit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL S900 DE Driver");
MODULE_LICENSE("GPL v2");
