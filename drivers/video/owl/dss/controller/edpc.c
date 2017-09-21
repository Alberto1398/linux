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
 *	2015/8/24: Created by Lipeng.
 */
#define DEBUGX
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>

#include <video/owl_dss.h>

#include "edpc.h"

#define CMU_EDPCLK		(0x0088)

struct edpc_config {
	uint32_t		link_rate;

	uint32_t		lane_count;
	uint32_t		lane_polarity;
	uint32_t		lane_mirror;

	uint32_t		mstream_polarity;
	uint32_t		user_sync_polarity;

	uint32_t		pclk_parent;
	uint32_t		pclk_rate;
};

struct edpc_data {
	struct owl_display_ctrl	ctrl;
	struct edpc_config	configs;

	enum owl_dss_state	state;
	struct mutex		lock;

	/*
	 * resources used by EDP controller
	 */
	struct platform_device	*pdev;

	void __iomem		*base;
	void __iomem		*cmu_base;

	struct reset_control	*rst;

	struct clk		*edp_clk;

	struct clk		*edp_clk_parent;
};

#define edpc_writel(edpc, index, val)	writel((val), edpc->base + (index))
#define edpc_readl(edpc, index)		readl(edpc->base + (index))
unsigned short edp_auxread(struct edpc_data *edpc, unsigned short addr)
{
	unsigned int val;

	dev_dbg(&edpc->pdev->dev,"%s, 0x%x start\n", __func__, addr);

	edpc_writel(edpc, EDP_AUX_ADDR, addr);
	edpc_writel(edpc, EDP_AUX_COMD, 0x09 << 8); /* aux read, 1byte */

	do {
		val = edpc_readl(edpc, EDP_AUX_STATE);
	} while(!(val & (1 << 2)));		/* if 1, reply is in progress */

	val = edpc_readl(edpc, EDP_AUX_RPLY_CODE);
	val = edpc_readl(edpc, EDP_AUX_RPLY_COUNT );
	val = edpc_readl(edpc, EDP_AUX_RPLY_DAT_CNT);
	val = edpc_readl(edpc, EDP_AUX_RPLY_DAT);

	dev_dbg(&edpc->pdev->dev,"%s, end\n", __func__);
	return val;
}

void edp_auxwrite(struct edpc_data *edpc, unsigned short addr,unsigned short Data)
{
	unsigned int temp;

	dev_dbg(&edpc->pdev->dev,"%s, 0x%x start\n", __func__, addr);

	edpc_writel(edpc, EDP_AUX_ADDR, addr);
	edpc_writel(edpc, EDP_AUX_WR_FIFO, Data);
	edpc_writel(edpc, EDP_AUX_COMD, 0x08<<8); //aux write, 1byte
	do{
		temp = edpc_readl(edpc, EDP_AUX_STATE);
	}while(temp&(1<<1));		//if 1, request is in progress

	temp =edpc_readl(edpc, EDP_AUX_RPLY_CODE);
}

static void edpc_clk_enable(struct edpc_data *edpc)
{
	uint32_t val;

	dev_dbg(&edpc->pdev->dev, "%s\n", __func__);

	/* config and enable edpclk */
	clk_set_parent(edpc->edp_clk, edpc->edp_clk_parent);
	clk_set_rate(edpc->edp_clk, edpc->configs.pclk_rate);
	clk_prepare_enable(edpc->edp_clk);

	/* enable 24MOUTEN */
	val = readl(edpc->cmu_base + CMU_EDPCLK);
	val |= (1 << 8);
	writel(val, edpc->cmu_base + CMU_EDPCLK);
	mdelay(1);

	/* config and enable DP PLL */
	val = readl(edpc->cmu_base + CMU_EDPCLK);

	/*
	 * 0, 1.62Gbps--810M
	 * 1, 2.7Gbps--1350M
	 * 2, 5.4Gbps--2700M
	 */
	val |= edpc->configs.link_rate;

	/* DP PLL enable */
	val |= (1 << 9);

	writel(val, edpc->cmu_base + CMU_EDPCLK);
	mdelay(1);
}

static void edpc_clk_disable(struct edpc_data *edpc)
{
	uint32_t val;

	dev_dbg(&edpc->pdev->dev, "%s\n", __func__);

	/* disable 24MOUTEN & DP PLL */
	val = readl(edpc->cmu_base + CMU_EDPCLK);
	val &= ~(1 << 8);
	val &= ~(1 << 9);
	writel(val, edpc->cmu_base + CMU_EDPCLK);

	clk_disable_unprepare(edpc->edp_clk);
}

static void edpc_reset_assert(struct edpc_data *edpc)
{
	if (IS_ERR(edpc->rst))
		return;

	reset_control_assert(edpc->rst);
}

static void edpc_reset_deassert(struct edpc_data *edpc)
{
	if (IS_ERR(edpc->rst))
		return;

	reset_control_deassert(edpc->rst);
}

static void edpc_set_size(struct edpc_data *edpc,
			  uint16_t width, uint16_t height)
{
	u32 val;

	BUG_ON((width > (1 << 14)) || (height > (1 << 14)));

	val = REG_VAL(width, 14, 0);
	edpc_writel(edpc, EDP_MSTREAM_HRES, val);

	val = REG_VAL(height, 14, 0);
	edpc_writel(edpc, EDP_MSTREAM_VRES, val);
}

static void edpc_set_mode(struct edpc_data *edpc,
			  uint16_t width, uint16_t height,
			  uint16_t hbp, uint16_t hfp,
			  uint16_t hsw, uint16_t vbp,
			  uint16_t vfp, uint16_t vsw)
{
	uint32_t bpp = PANEL_BPP(edpc->ctrl.panel);

	BUG_ON((hbp > (1 << 15)) || (hfp > (1 << 15)) || (hsw > (1 << 15)));

	BUG_ON((vbp > (1 << 15)) || (vfp > (1 << 15)) || (vsw > (1 << 15)));

	edpc_writel(edpc, EDP_MSTREAM_HSWIDTH, hsw);
	edpc_writel(edpc, EDP_MSTREAM_HSTART, hbp + hsw);
	edpc_writel(edpc, EDP_MSTREAM_HTOTAL, hsw + hbp + width + hfp);

	edpc_writel(edpc, EDP_MSTREAM_VSWIDTH, vsw);
	edpc_writel(edpc, EDP_MSTREAM_VSTART, vbp + vsw);
	edpc_writel(edpc, EDP_MSTREAM_VTOTAL, vsw + vbp + height + vfp);

	/* others, TODO */
	edpc_writel(edpc, EDP_MSTREAM_POLARITY,
		    edpc->configs.mstream_polarity);

	edpc_writel(edpc, EDP_M_VID, 0x7d28);/* vidio/1K TODO */
	/* 64byte each micro packet */
	edpc_writel(edpc, EDP_MTRANSFER_UNIT, 64);

	edpc_writel(edpc, EDP_N_VID, 0x8000);
	edpc_writel(edpc, EDP_USER_DATA_COUNT, (width * bpp / 16 - 1));
	edpc_writel(edpc, EDP_USER_SYNC_POLARITY,
		    edpc->configs.user_sync_polarity);
}

static void edpc_set_default_color(struct edpc_data *edpc, uint32_t color)
{
	edpc_writel(edpc, EDP_RGB_COLOR, color);
}

static void edpc_enable_internal_scramble(struct edpc_data *edpc, bool enable)
{
	bool val;
	val = !enable;

	edpc_writel(edpc, EDP_LNK_SCR_CTRL, val);
}

static void edpc_set_single_format(struct edpc_data *edpc, uint8_t format)
{
	u32 val;

	val = edpc_readl(edpc, EDP_MSTREAM_MISC0);
	val = REG_SET_VAL(val, format, 2, 1);
	edpc_writel(edpc, EDP_MSTREAM_MISC0, val);
}

static void edpc_set_data_width(struct edpc_data *edpc)
{
	u8 bit_depth;
	u32 val;

	switch (PANEL_BPP(edpc->ctrl.panel)) {
	case 18:
		bit_depth = 0;
		break;

	case 24:
		bit_depth = 1;
		break;

	case 30:
		bit_depth = 2;
		break;

	case 36:
		bit_depth = 3;
		break;

	case 48:
		bit_depth = 4;
		break;

	default:
		bit_depth = 0;
		break;
	}

	val = edpc_readl(edpc, EDP_MSTREAM_MISC0);
	val = REG_SET_VAL(val, bit_depth, 7, 5);
	edpc_writel(edpc, EDP_MSTREAM_MISC0, val);
}

static void edpc_set_preline(struct edpc_data *edpc)
{
	int preline;
	u32 val;

	preline = owl_panel_get_preline_num(edpc->ctrl.panel);
	preline -= 1;
	preline = (preline < 0 ? 0 : preline);
	preline = (preline > 31 ? 31 : preline);

	val = edpc_readl(edpc, EDP_RGB_CTL);
	val = REG_SET_VAL(val, preline, 8, 4);
	edpc_writel(edpc, EDP_RGB_CTL, val);
}

static void edpc_set_single_from(struct edpc_data *edpc, uint8_t single)
{
	u32 val;

	val = edpc_readl(edpc, EDP_RGB_CTL);
	val = REG_SET_VAL(val, single, 1, 1);
	edpc_writel(edpc, EDP_RGB_CTL, val);
}

static void edpc_phy_config(struct edpc_data *edpc)
{
	edpc_writel(edpc, EDP_PHY_PREEM_L0, 0);
	edpc_writel(edpc, EDP_PHY_PREEM_L1, 0);
	edpc_writel(edpc, EDP_PHY_PREEM_L2, 0);
	edpc_writel(edpc, EDP_PHY_PREEM_L3, 0);

	edpc_writel(edpc, EDP_PHY_VSW_L0, 0);
	edpc_writel(edpc, EDP_PHY_VSW_L1, 0);
	edpc_writel(edpc, EDP_PHY_VSW_L2, 0);
	edpc_writel(edpc, EDP_PHY_VSW_L3, 0);

	/* set the aux channel voltage swing 400mV */
	edpc_writel(edpc, EDP_PHY_VSW_AUX, 0);

	/* set no Mirror,polarity not changed */
	edpc_writel(edpc, EDP_PHY_CTRL, 0);
}

static void edpc_tx_init(struct edpc_data *edpc)
{
	edpc_writel(edpc, EDP_CORE_TX_EN, 0x00);
	edpc_writel(edpc, EDP_PHY_PWR_DOWN, 0x00);
	edpc_writel(edpc, EDP_PHY_RESET, 0x00);
	mdelay(1);	/* wait for complete */
}

static void edpc_link_config(struct edpc_data *edpc)
{
	/* set aux clock 1MHz */
	edpc_writel(edpc, EDP_AUX_CLK_DIV, 24);

	edpc_writel(edpc, EDP_LNK_LANE_COUNT, edpc->configs.lane_count);

	/* enable enhanced frame */
	edpc_writel(edpc, EDP_LNK_ENHANCED, 0x01);

	/* eDP enable, use only for embedded application */
	edpc_writel(edpc, EDP_LNK_SCR_RST, 0x00);

	/* enable internal scramble */
	edpc_enable_internal_scramble(edpc, true);
}

static void edpc_display_init_edp(struct edpc_data *edpc)
{
	struct owl_videomode *mode = &edpc->ctrl.panel->mode;

	edpc_set_size(edpc, mode->xres, mode->yres);
	edpc_set_mode(edpc, mode->xres, mode->yres, mode->hbp, mode->hfp,
		      mode->hsw, mode->vbp, mode->vfp, mode->vsw);

	edpc_set_default_color(edpc, 0xff);
	edpc_set_single_format(edpc, 0);
	edpc_set_data_width(edpc);

	edpc_set_preline(edpc);
	edpc_set_single_from(edpc, 0);	/* 0 DE, 1 default color */
	edpc_phy_config(edpc);
	edpc_tx_init(edpc);

	edpc_link_config(edpc);
}

static void edpc_video_enable(struct edpc_data *edpc, bool enable)
{
	uint32_t val;

	edpc_writel(edpc, EDP_CORE_FSCR_RST, 1);		/* TODO */

	/* Enable the main stream */
	val = edpc_readl(edpc, EDP_CORE_MSTREAM_EN);
	val = REG_SET_VAL(val, enable, 0, 0);
	edpc_writel(edpc, EDP_CORE_MSTREAM_EN, val);

	/* enable RGB interface	*/
	val = edpc_readl(edpc, EDP_RGB_CTL);
	val = REG_SET_VAL(val, enable, 0, 0);
	edpc_writel(edpc, EDP_RGB_CTL, val);

}

static void edpc_single_enable(struct edpc_data *edpc, bool enable)
{
	uint32_t val;

	/* start calibrate */
	val = edpc_readl(edpc, EDP_PHY_CAL_CTRL);
	val = REG_SET_VAL(val, enable, 8, 8);
	edpc_writel(edpc, EDP_PHY_CAL_CTRL, val);

	do {
		val = edpc_readl(edpc, EDP_PHY_CAL_CTRL);
	} while (val & 0x100);

	/* enable transmit */
	edpc_writel(edpc, EDP_CORE_TX_EN, enable);

	/* phy ctl */
	val = ((((1 << edpc->configs.lane_count) - 1) & 0xf) << 9);
	val |= (edpc->configs.lane_polarity << 4);
	val |= (edpc->configs.lane_mirror << 15);
	edpc_writel(edpc, EDP_PHY_CTRL, val);

}

static bool edpc_is_enabled(struct edpc_data *edpc)
{
	bool enabled = false;

	/* enable EDP clock at booting, or 'edpc_is_enabled' will die!! */
	edpc_clk_enable(edpc);
	edpc_reset_deassert(edpc);
	mdelay(1);
	enabled = ((edpc_readl(edpc, EDP_RGB_CTL) & 0x1) == 0x1);

	dev_info(&edpc->pdev->dev, "%s, %d\n", __func__, enabled);
	return enabled;
}

void set_sink_speed(struct edpc_data *edpc, unsigned int speed)
{
	switch(speed)
	{
		case 0: edp_auxwrite(edpc, 0x100,0x06); break;	//1.62G
		case 1: edp_auxwrite(edpc, 0x100,0x0a); break;	//2.7G
		case 2: edp_auxwrite(edpc, 0x100,0x14); break;	//5.4G
		default: break; ;
	}
}

void set_source_pre_emp(struct edpc_data *edpc, unsigned int level)
{
	//set source and sink to swing level0
	edpc_writel(edpc, EDP_PHY_PREEM_L0, level);
	edpc_writel(edpc, EDP_PHY_PREEM_L1, level);
	edpc_writel(edpc, EDP_PHY_PREEM_L2, level);
	edpc_writel(edpc, EDP_PHY_PREEM_L3, level);
}

unsigned int link_training_pattern1(struct edpc_data *edpc)
{
	unsigned int val;

	dev_dbg(&edpc->pdev->dev,"%s, start\n", __func__);

	msleep(10);
	set_source_pre_emp(edpc, 2);

	edpc_writel(edpc, EDP_LNK_TRAP, 1);	/* set training pattern1 */

	set_sink_speed(edpc, 1);
	msleep(10);
	edp_auxwrite(edpc, 0x0101, 0x84);

	val = edp_auxread(edpc, 0x0000);
	val = edp_auxread(edpc, 0x0001);
	val = edp_auxread(edpc, 0x0002);

	val = edp_auxread(edpc, 0x0003);
	edp_auxwrite(edpc, 0x0600, 0x01);

	val = edp_auxread(edpc, 0x0000);
	val = edp_auxread(edpc, 0x0100);
	val = edp_auxread(edpc, 0x0002);
	val = edp_auxread(edpc, 0x0101);
	val = edp_auxread(edpc, 0x0000);
	val = edp_auxread(edpc, 0x0102);

	edp_auxwrite(edpc, 0x0102, 0x21);	/* pattern1 training */

	edp_auxwrite(edpc, 0x0103, 0x00);
	edp_auxwrite(edpc, 0x0104, 0x00);
	edp_auxwrite(edpc, 0x0105, 0x00);
	edp_auxwrite(edpc, 0x0106, 0x00);
	msleep(10);

	val = edp_auxread(edpc, 0x202);	/* lane0\1 done */
	val = edp_auxread(edpc, 0x203);	/* lane2\3 done */

	val = edp_auxread(edpc, 0x0101);

	val = edp_auxread(edpc, 0x0206);
	val = edp_auxread(edpc, 0x0207);

	edp_auxwrite(edpc, 0x0103, 0x38);/* swing= level 0; pre-emphasis =3 */
	edp_auxwrite(edpc, 0x0104, 0x38);
	edp_auxwrite(edpc, 0x0105, 0x38);
	edp_auxwrite(edpc, 0x0106, 0x38);

	msleep(10);

	val = edp_auxread(edpc, 0x202);	/* lane0\1 done */
	val = edp_auxread(edpc, 0x203);	/* lane2\3 done */

	val = edp_auxread(edpc, 0x0000);
	val = edp_auxread(edpc, 0x0102);

	dev_dbg(&edpc->pdev->dev,"%s, wait for lane done\n", __func__);
	do {
		val = edp_auxread(edpc, 0x202);	/* lane0\1 done */
		val = edp_auxread(edpc, 0x203);	/* lane2\3 done */
	} while((val & 0x11) != 0x11);

	return 0;

}

unsigned int link_training_pattern2(struct edpc_data *edpc)
{
	unsigned int val;

	edpc_writel(edpc,EDP_LNK_TRAP, 2);	/*set training partern 2*/

	edp_auxwrite(edpc, 0x0102, 0x22);	/* training pattern 2 */
	msleep(10);

	val = edp_auxread(edpc, 0x202);		/* lane0\1 done */
	val = edp_auxread(edpc, 0x203);		/* lane2\3 done */

	val = edp_auxread(edpc, 0x101);

	val = edp_auxread(edpc, 0x0206);
	val = edp_auxread(edpc, 0x0207);

	edp_auxwrite(edpc, 0x0103, 0x38);/*swing= level 0; pre-emphasis =3 */
	edp_auxwrite(edpc, 0x0104, 0x38);
	edp_auxwrite(edpc, 0x0105, 0x38);
	edp_auxwrite(edpc, 0x0106, 0x38);
	msleep(10);

	val = edp_auxread(edpc, 0x202);	/* lane0\1 done */
	val = edp_auxread(edpc, 0x203);	/* lane2\3 done */

	val = edp_auxread(edpc, 0x101);

	val = edp_auxread(edpc, 0x0206);
	val = edp_auxread(edpc, 0x0207);

	edp_auxwrite(edpc, 0x0103, 0x10);/* swing= level 0; pre-emphasis =1 */
	edp_auxwrite(edpc, 0x0104, 0x10);
	edp_auxwrite(edpc, 0x0105, 0x10);
	edp_auxwrite(edpc, 0x0106, 0x10);
	msleep(10);

	val = edp_auxread(edpc, 0x202);	/* lane0\1 done */
	val = edp_auxread(edpc, 0x203);	/* lane2\3 done */

	val = edp_auxread(edpc, 0x101);

	val = edp_auxread(edpc, 0x0206);
	val = edp_auxread(edpc, 0x0207);

	edp_auxwrite(edpc, 0x0103, 0x10);     /* swing= level 0; pre-emphasis = 1 */
	edp_auxwrite(edpc, 0x0104, 0x10);
	edp_auxwrite(edpc, 0x0105, 0x10);
	edp_auxwrite(edpc, 0x0106, 0x10);

	msleep(10);

	val = edp_auxread(edpc, 0x202);	/* lane0\1 done */
	val = edp_auxread(edpc, 0x203);	/* lane2\3 done */

	do {
		val = edp_auxread(edpc, 0x202);	/* lane0\1 done */
	} while((val & 0x77) != 0x77);

	val = edp_auxread(edpc, 0x204);
	val = edp_auxread(edpc, 0x102);

	edp_auxwrite(edpc, 0x0102, 0x00);

}

void edp_delay(unsigned int num)
{
	unsigned int i, k;

	for (i = 0; i < num; i++)
	{
		k= 0;
	}
	return;
}

void edpc_link_training(struct edpc_data *edpc)
{

	unsigned int temp;
	dev_info(&edpc->pdev->dev,"%s, start\n", __func__);

	temp = edp_auxread(edpc, 0x0000);
	temp = edp_auxread(edpc, 0x0001);
	temp = edp_auxread(edpc, 0x0002);

	edpc_enable_internal_scramble(edpc, false);	/* disable internal scramble */

	temp = link_training_pattern1(edpc);
	dev_dbg(&edpc->pdev->dev,"%s, link_training_pattern1 end\n", __func__);
	msleep(10);

	temp = edp_auxread(edpc, 0x000); 		/* REV	0x12 */
	temp = link_training_pattern2(edpc);
	msleep(10);
	dev_dbg(&edpc->pdev->dev,"%s, link_training_pattern2 end\n", __func__);

	edp_auxwrite(edpc, 0x102, 0);			/* set Sink training pattern3 */
	edp_delay(50);
	temp = edp_auxread(edpc, 0x102);

	edpc_writel(edpc, EDP_LNK_TRAP, 0x00);		/* training off */
	edpc_enable_internal_scramble(edpc, true);	/* enable internal scramble */

	dev_info(&edpc->pdev->dev,"%s, end\n", __func__);
}
static int owl_edpc_enable(struct owl_display_ctrl *ctrl)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);
	dev_info(&edpc->pdev->dev, "%s\n", __func__);
	mutex_lock(&edpc->lock);

	if (edpc->state != OWL_DSS_STATE_ON) {

		edpc_video_enable(edpc, true);

		edpc->state = OWL_DSS_STATE_ON;
	}

	mutex_unlock(&edpc->lock);
	return 0;
}

static int owl_edpc_disable(struct owl_display_ctrl *ctrl)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);

	dev_info(&edpc->pdev->dev, "%s\n", __func__);

	mutex_lock(&edpc->lock);

	if (edpc->state == OWL_DSS_STATE_ON) {
		edpc_single_enable(edpc, false);
	
		edpc_video_enable(edpc, false);

		edpc_clk_disable(edpc);
		edpc_reset_assert(edpc);

		edpc->state = OWL_DSS_STATE_OFF;
	}

	mutex_unlock(&edpc->lock);
	return 0;
}

static owl_edpc_ctrl_is_enabled(struct owl_display_ctrl *ctrl)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);
	bool enabled;

	enabled = edpc_is_enabled(edpc);
	dev_info(&edpc->pdev->dev, "%s, is_enabled %d\n", __func__, enabled);

	return enabled;
}

static int owl_edpc_power_on(struct owl_display_ctrl *ctrl)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);
	dev_info(&edpc->pdev->dev, "%s\n", __func__);

	mutex_lock(&edpc->lock);
	if (edpc->state != OWL_DSS_STATE_ON) {
		edpc_reset_assert(edpc);

		edpc_clk_enable(edpc);
		mdelay(5);

		edpc_reset_deassert(edpc);
		mdelay(1);

		edpc_display_init_edp(edpc);

		edpc_single_enable(edpc, true);
	}
	mutex_unlock(&edpc->lock);
}

static int owl_edpc_power_off(struct owl_display_ctrl *ctrl)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);
	dev_info(&edpc->pdev->dev, "%s\n", __func__);

}


int owl_edpc_aux_write(struct owl_display_ctrl *ctrl, const char *buf,
			int count)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);
	unsigned int temp;

	/* TODO */
	edpc_link_training(edpc);

}

int owl_edpc_aux_read(struct owl_display_ctrl *ctrl, char *buf, int count)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);

}

static struct owl_display_ctrl_ops owl_edp_ctrl_ops = {
	.enable = owl_edpc_enable,
	.disable = owl_edpc_disable,

	.power_on = owl_edpc_power_on,
	.power_off = owl_edpc_power_off,

	.aux_read = owl_edpc_aux_read,
	.aux_write = owl_edpc_aux_write,

	.ctrl_is_enabled = owl_edpc_ctrl_is_enabled,
};

/*============================================================================
 *			platform driver
 *==========================================================================*/

static int edpc_parse_configs(struct edpc_data *edpc)
{
	struct device *dev = &edpc->pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *entry;

	struct edpc_config *configs = &edpc->configs;

	entry = of_parse_phandle(of_node, "panel_configs", 0);
	if (!entry) {
		dev_err(dev, "no etry for 'panel_configs'\n");
		return -EINVAL;
	}
	dev_dbg(dev, "entry name: %s\n", entry->name);

	#define OF_READ_U32(name, p) of_property_read_u32(entry, (name), (p))

	if (OF_READ_U32("link_rate", &configs->link_rate))
		return -EINVAL;

	if (OF_READ_U32("lane_count", &configs->lane_count))
		return -EINVAL;
	if (OF_READ_U32("lane_polarity", &configs->lane_polarity))
		return -EINVAL;
	if (OF_READ_U32("lane_mirror", &configs->lane_mirror))
		return -EINVAL;

	if (OF_READ_U32("mstream_polarity", &configs->mstream_polarity))
		return -EINVAL;
	if (OF_READ_U32("user_sync_polarity", &configs->user_sync_polarity))
		return -EINVAL;

	if (OF_READ_U32("pclk_parent", &configs->pclk_parent))
		return -EINVAL;
	if (OF_READ_U32("pclk_rate", &configs->pclk_rate))
		return -EINVAL;

	dev_dbg(dev, "%s:\n", __func__);
	dev_dbg(dev, "link_rate %d\n", configs->link_rate);
	dev_dbg(dev, "lane_count %d, lane_polarity %x, lane_mirror = %d\n",
		configs->link_rate, configs->lane_count, configs->lane_mirror);
	dev_dbg(dev, "mstream_polarity %x, user_sync_polarity = %x\n",
		configs->mstream_polarity, configs->user_sync_polarity);
	dev_dbg(dev, "pclk_parent %d pclk_rate %d\n",
		configs->pclk_parent, configs->pclk_rate);

	#undef OF_READ_U32

	return 0;
}

static int edpc_get_resources(struct edpc_data *edpc)
{
	struct platform_device *pdev = edpc->pdev;
	struct device *dev = &pdev->dev;

	struct resource *res;

	dev_dbg(dev, "%s\n", __func__);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (IS_ERR(res)) {
		dev_err(dev, "can't get regs\n");
		return PTR_ERR(res);
	}
	edpc->base = devm_ioremap_resource(dev, res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmu_base");
	if (IS_ERR(res)) {
		dev_err(dev, "can't get cmu_edpclk\n");
		return PTR_ERR(res);
	}
	edpc->cmu_base = ioremap(res->start, resource_size(res));

	if (IS_ERR(edpc->base) || IS_ERR(edpc->cmu_base)) {
		dev_err(dev, "map registers error\n");
		return -ENODEV;
	}
	dev_dbg(dev, "base: 0x%p, cmu_base: 0x%p\n", edpc->base,
		edpc->cmu_base);

	edpc->edp_clk = devm_clk_get(dev, "edp");
	if (IS_ERR(edpc->edp_clk)) {
		dev_err(dev, "can't get clock\n");
		return -EINVAL;
	}

	if (edpc->configs.pclk_parent == 0)
		edpc->edp_clk_parent = devm_clk_get(&pdev->dev, "assist_pll");
	else
		edpc->edp_clk_parent = devm_clk_get(&pdev->dev, "display_pll");

	if (IS_ERR(edpc->edp_clk_parent)) {
		dev_err(dev, "can't get parent clk\n");
		return PTR_ERR(edpc->edp_clk_parent);
	}

	edpc->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(edpc->rst)) {
		dev_err(dev, "can't get reset\n");
		return PTR_ERR(edpc->rst);
	}

	return 0;
}

static const struct of_device_id owl_edpc_match[] = {
	{
		.compatible	= "actions,s900-edp",
	},
};

static int __init owl_edpc_probe(struct platform_device *pdev)
{
	int				ret = 0;

	struct device			*dev = &pdev->dev;
	const struct of_device_id	*match;

	struct edpc_data *edpc;

	dev_info(dev, "%s, pdev = 0x%p\n", __func__, pdev);

	match = of_match_device(of_match_ptr(owl_edpc_match), dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	edpc = devm_kzalloc(dev, sizeof(*edpc), GFP_KERNEL);
	if (edpc == NULL) {
		dev_err(dev, "No Mem for edpc\n");
		return -ENOMEM;
	}
	dev_set_drvdata(dev, edpc);
	edpc->pdev = pdev;

	mutex_init(&edpc->lock);

	if (edpc_parse_configs(edpc) < 0) {
		dev_err(dev, "parse configs failed\n");
		return -EINVAL;
	}

	ret = edpc_get_resources(edpc);
	if (ret < 0) {
		dev_err(dev, "get resources failed: %d\n", ret);
		return ret;
	}

	edpc->ctrl.type = OWL_DISPLAY_TYPE_EDP;
	edpc->ctrl.ops = &owl_edp_ctrl_ops;

	owl_ctrl_set_drvdata(&edpc->ctrl, edpc);

	ret = owl_ctrl_register(&edpc->ctrl);
	if (ret < 0) {
		dev_err(dev, "register edp ctrl failed: %d\n", ret);
		return ret;
	}

	if (edpc_is_enabled(edpc)) {
		edpc->state = OWL_DSS_STATE_ON;
	} else {
		edpc->state = OWL_DSS_STATE_OFF;
		edpc_clk_disable(edpc);
		edpc_reset_assert(edpc);
	}
	dev_info(dev, "%s, OK\n", __func__);	
	return 0;
}

static int __exit owl_edpc_remove(struct platform_device *pdev)
{
	struct edpc_data *edpc = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "%s\n", __func__);

	owl_edpc_disable(&edpc->ctrl);
	return 0;
}

static int owl_edpc_suspend(struct device *dev)
{
	struct edpc_data *edpc = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);

	owl_edpc_disable(&edpc->ctrl);
	return 0;
}

static int owl_edpc_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	/* nothing to do, application will re-enable me later */

	return 0;
}

static UNIVERSAL_DEV_PM_OPS(owl_edpc_pm_ops, owl_edpc_suspend,
			    owl_edpc_resume, NULL);

static struct platform_driver owl_edpc_driver = {
	.probe			= owl_edpc_probe,
	.remove			= __exit_p(owl_edpc_remove),
	.driver = {
		.name		= "owl-edpc",
		.owner		= THIS_MODULE,
		.of_match_table	= owl_edpc_match,
		.pm		= &owl_edpc_pm_ops,
	},
};

int __init owl_edpc_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&owl_edpc_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	return 0;
}

void __exit owl_edpc_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&owl_edpc_driver);
}

module_init(owl_edpc_init);
module_exit(owl_edpc_exit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL EDP Controller Driver");
MODULE_LICENSE("GPL v2");
