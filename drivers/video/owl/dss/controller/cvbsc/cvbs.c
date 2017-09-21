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
#include <linux/switch.h>
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

#include <video/owl_dss.h>

#include "cvbs_reg.h"
#include "cvbs.h"
struct switch_dev cdev = {
	.name = "cvbs",
};
static struct switch_dev sdev_cvbs_audio = {
	 .name = "cvbs_audio",
};
enum {
	CVBS_PAL = 0,
	CVBS_NTSC,
};
struct cvbs_config {
	int			vid;

	struct owl_videomode	mode;
};
static const struct cvbs_config cvbs_timings[] = {
	{
		CVBS_PAL,
		{720, 576, 25, 13500, 16, 39, 64, 5, 64, 5,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 1}
	},
	{
		CVBS_NTSC,
		{720, 480, 30, 27000, 16, 30, 60, 62, 6, 9,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 1}
	},
};
#define CEA_TIMINGS_LEN		(ARRAY_SIZE(cvbs_timings))

#define OWL_DUAL_DISPLAY_FOR_S700

inline void cvbs_write_reg(struct cvbs_data *cvbs, const u32 index, u32 val)
{
	writel(val, cvbs->base+index);
}
inline u32 cvbs_read_reg(struct cvbs_data *cvbs, const u32 index)
{
	return readl(cvbs->base + index);
}

static void dump_reg(struct cvbs_data *cvbs)
{
#define DUMPREG(name, r) printk("%s %08x\n", name, cvbs_read_reg(cvbs, r))
	DUMPREG("TVOUT_EN	value is ", TVOUT_EN);
	DUMPREG("TVOUT_OCR	value is ", TVOUT_OCR);
	DUMPREG("TVOUT_STA	value is ", TVOUT_STA);
	DUMPREG("TVOUT_CCR	value is ", TVOUT_CCR);
	DUMPREG("TVOUT_BCR	value is ", TVOUT_BCR);
	DUMPREG("TVOUT_CSCR	value is ", TVOUT_CSCR);
	DUMPREG("TVOUT_PRL	value is ", TVOUT_PRL);
	DUMPREG("TVOUT_VFALD	value is ", TVOUT_VFALD);
	DUMPREG("CVBS_MSR	value is ", CVBS_MSR);
	DUMPREG("CVBS_AL_SEPO	value is ", CVBS_AL_SEPO);
	DUMPREG("CVBS_AL_SEPE	value is ", CVBS_AL_SEPE);
	DUMPREG("CVBS_AD_SEP	value is ", CVBS_AD_SEP);
	DUMPREG("CVBS_HUECR	value is ", CVBS_HUECR);
	DUMPREG("CVBS_SCPCR	value is ", CVBS_SCPCR);
	DUMPREG("CVBS_SCFCR	value is ", CVBS_SCFCR);
	DUMPREG("CVBS_CBACR	value is ", CVBS_CBACR);
	DUMPREG("CVBS_SACR	value is ", CVBS_SACR);
	DUMPREG("TVOUT_DCR	value is ", TVOUT_DCR);
	DUMPREG("TVOUT_DDCR	value is ", TVOUT_DDCR);
	DUMPREG("TVOUT_DCORCTL	value is ", TVOUT_DCORCTL);
	DUMPREG("TVOUT_DRCR	value is ", TVOUT_DRCR);
}
static void owl_cvbs_regs_dump(struct owl_display_ctrl *ctrl)
{
	struct cvbs_data *cvbs = owl_ctrl_get_drvdata(ctrl);
	dump_reg(cvbs);
}
static void auto_detect_bit(struct cvbs_data *cvbs, int flag)
{
	u32 val = 0;
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	dev_dbg(dev, "%s\n", __func__);
	if (flag == CVBS_IN) {
		val = cvbs_read_reg(cvbs, TVOUT_OCR);
		val = REG_SET_VAL(val, 1, 8, 8);
		cvbs_write_reg(cvbs, TVOUT_OCR, val);
	} else if (flag == CVBS_OUT) {
		val = cvbs_read_reg(cvbs,  TVOUT_OCR);
		val = REG_SET_VAL(val, 1, 9, 9);
		cvbs_write_reg(cvbs,  TVOUT_OCR, val);
	}
}

static void cvbs_clear_pending(struct cvbs_data *cvbs, int flag)
{
	u32 val;
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_dbg(dev, "%s\n", __func__);
	if (flag == CVBS_IN) {
		val = cvbs_read_reg(cvbs, TVOUT_STA);
		val = REG_SET_VAL(val, 1, 3, 3);
		cvbs_write_reg(cvbs, val, TVOUT_STA);
	} else if (flag == CVBS_OUT) {
		val = cvbs_read_reg(cvbs, TVOUT_STA);
		val = REG_SET_VAL(val, 1, 7, 7);
		cvbs_write_reg(cvbs, TVOUT_STA, val);
	}
}
static bool cvbs_pending(struct cvbs_data *cvbs, int flag)
{
	u32 val;
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_dbg(dev, "%s\n", __func__);
	if (flag == CVBS_IN) {
		val = cvbs_read_reg(cvbs, TVOUT_STA);
		return (REG_GET_VAL(val, 3, 3) == 1);
	}
	if (flag == CVBS_OUT) {
		val = cvbs_read_reg(cvbs, TVOUT_STA);
		return (REG_GET_VAL(val, 7, 7) == 1);
	}
	return true;
}
static void cvbs_irq_enable(struct cvbs_data *cvbs, int flag, bool enable)
{
	u32 val;
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_dbg(dev, "%s\n", __func__);

	if (flag == CVBS_IN) {
		val = cvbs_read_reg(cvbs, TVOUT_OCR);
		val = REG_SET_VAL(val, enable, 12, 12);
		cvbs_write_reg(cvbs, TVOUT_OCR, val);
	} else if (flag == CVBS_OUT) {
		val = cvbs_read_reg(cvbs, TVOUT_OCR);
		val = REG_SET_VAL(val, enable, 11, 11);
		cvbs_write_reg(cvbs,  TVOUT_OCR, val);
	}

}
static void cvbs_update_mode_list(struct cvbs_data *cvbs)
{
	int i;
	struct owl_panel *panel = cvbs->ctrl.panel;
	struct owl_videomode mode[2];
	struct cvbs_config *cfg;
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_dbg(dev, "%s\n", __func__);

	for (i = 0; i < CEA_TIMINGS_LEN; i++) {
		cfg = &cvbs_timings[i];
		memcpy(&mode[i], &cfg->mode, sizeof(struct owl_videomode));
	}
	owl_panel_set_mode_list(panel, mode, CEA_TIMINGS_LEN);
}
static irqreturn_t cvbs_irq_handler(int irq, void *data)
{
	struct cvbs_data *cvbs = data;
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	dev_dbg(dev, "[%s start]\n", __func__);
	if (cvbs_pending(cvbs, CVBS_IN)) {
		cvbs->is_connected = true;
		dev_dbg(dev, "CVBS is in\n");
		cvbs_irq_enable(cvbs, CVBS_IN, false);
		cvbs_irq_enable(cvbs, CVBS_OUT, true);

		cvbs_clear_pending(cvbs, CVBS_IN);
		auto_detect_bit(cvbs, CVBS_OUT);

		schedule_work(&cvbs->cvbs_in_work);
	}
	if (cvbs_pending(cvbs, CVBS_OUT)) {
		cvbs->is_connected = false;
		dev_dbg(dev, "CVBS is out\n");
		cvbs_irq_enable(cvbs, CVBS_OUT, false);
		cvbs_irq_enable(cvbs, CVBS_IN, true);

		cvbs_clear_pending(cvbs, CVBS_OUT);
		auto_detect_bit(cvbs, CVBS_IN);

		schedule_work(&cvbs->cvbs_out_work);
	}

	dev_dbg(dev, "[%s end]\n", __func__);
	return IRQ_HANDLED;
}
static struct of_device_id owl_cvbs_of_match[] = {
	{
		.compatible = "actions,s700-cvbs",
	},
	{
	},
};

static int cvbs_parse_params(struct platform_device *pdev,
		struct cvbs_data *cvbs)
{
	struct device	*dev = &pdev->dev;
	struct device_node *of_node;

	dev_dbg(dev, "%s\n", __func__);

	of_node = dev->of_node;

	return 0;
}

static bool owl_cvbs_tvout_enabled(struct cvbs_data *cvbs)
{

	clk_prepare_enable(cvbs->tvout);
	clk_prepare_enable(cvbs->cvbs_pll);

	return (cvbs_read_reg(cvbs, TVOUT_EN) & 0x01) != 0;
}

static void set_cvbs_status(struct cvbs_data *cvbs,
		struct switch_dev *cdev, int state)
{
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	struct owl_panel *panel = cvbs->ctrl.panel;
	const struct cvbs_config *cfg;
	struct owl_videomode mode;
	int i;
	dev_dbg(dev, "%s, state:%d\n", __func__, state);

	if (state) {
		/* get default_mod */
		owl_panel_get_default_mode(panel, &mode);
		dev_dbg(dev, "%s, default mode: %dx%d@%d\n", __func__,
				mode.xres, mode.yres, mode.refresh);
		/* convert default_mode to current mode */
		for (i = 0; i < CEA_TIMINGS_LEN; i++) {
			cfg = &cvbs_timings[i];
			cvbs->current_vid = cfg->vid;
			if (cfg->mode.xres == mode.xres &&
					cfg->mode.yres == mode.yres &&
					cfg->mode.refresh == mode.refresh)
				break;
		}
		dev_dbg(dev, "%s, video mode %dx%d@%d\n", __func__,
			cfg->mode.xres, cfg->mode.yres, cfg->mode.refresh);
		dev_dbg(dev, "%s, current_vid %d", __func__, cvbs->current_vid);
		/*set mode*/
		owl_panel_set_mode(panel, (struct owl_videomode *)&cfg->mode);
	}

	owl_panel_hotplug_notify(panel, state);

	if (state && cvbs->is_connected) {
		switch_set_state(cdev, 1);
	} else {
		switch_set_state(cdev, 0);
		switch_set_state(&sdev_cvbs_audio, 2);
	}
}
static void disable_cvbs_output_configs(struct cvbs_data *cvbs)
{
	unsigned int tmp;

	tmp = cvbs_read_reg(cvbs, TVOUT_OCR);
	tmp &= ~TVOUT_OCR_DAC3;		/* hdac cvsb disable */
	tmp &= ~TVOUT_OCR_INREN;	/* cvbs internal 75ohm disable */
	cvbs_write_reg(cvbs, TVOUT_OCR, tmp);
}
static void enable_cvbs_output_configs(struct cvbs_data *cvbs)
{
	unsigned int tmp;

	tmp = cvbs_read_reg(cvbs, TVOUT_OCR);
	tmp |= TVOUT_OCR_DAC3;		/* hdac cvsb enable */
	tmp |= TVOUT_OCR_INREN;		/* cvbs internal 75ohm enable */
	tmp &= ~TVOUT_OCR_DACOUT;	/* disable color bar */
	cvbs_write_reg(cvbs, TVOUT_OCR, tmp);

}
static void do_cvbs_out(struct work_struct *work)
{
	struct cvbs_data *cvbs;
	struct device	*dev;
	struct platform_device  *pdev;

	cvbs = container_of(work, struct cvbs_data, cvbs_out_work);
	pdev = cvbs->pdev;
	dev = &pdev->dev;

	dev_dbg(dev, "%s, hpd_en %d\n", __func__, cvbs->hpd_en);
	if (cvbs->hpd_en)
		set_cvbs_status(cvbs, &cdev, 0);

#ifdef OWL_DUAL_DISPLAY_FOR_S700
	/* disable cvbs output configs*/
	disable_cvbs_output_configs(cvbs);
#endif
}

static void do_cvbs_in(struct work_struct *work)
{
	struct cvbs_data *cvbs;
	struct platform_device  *pdev;
	struct device	*dev;
	unsigned int tmp;

	cvbs = container_of(work, struct cvbs_data, cvbs_in_work);
	pdev = cvbs->pdev;
	dev = &pdev->dev;

	dev_dbg(dev, "%s, hpd_en %d\n", __func__, cvbs->hpd_en);
	if (cvbs->hpd_en)
		set_cvbs_status(cvbs, &cdev, 1);

#ifdef OWL_DUAL_DISPLAY_FOR_S700
	/* enable cvbs output configs */
	enable_cvbs_output_configs(cvbs);
#endif
}

static void cvbs_check_status(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct cvbs_data *cvbs = container_of(dwork, struct cvbs_data,
				cvbs_check_work);
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_dbg(dev, "%s\n", __func__);

	/*in auto detect */
}
static void cvbs_output_enable(struct cvbs_data *cvbs)
{
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	int tmp;

	dev_dbg(dev, "%s\n", __func__);

	tmp = cvbs_read_reg(cvbs, TVOUT_EN);
	tmp |= TVOUT_EN_CVBS_EN;
	cvbs_write_reg(cvbs, TVOUT_EN, tmp);

#ifndef OWL_DUAL_DISPLAY_FOR_S700
	/* hdac cvsb enable
	 * cvbs internal 75ohm enable*/
	cvbs_write_reg(cvbs, TVOUT_OCR, (cvbs_read_reg(cvbs, TVOUT_OCR) |
			TVOUT_OCR_DAC3 | TVOUT_OCR_INREN) & ~TVOUT_OCR_DACOUT);
#endif
}
static void cvbs_output_disable(struct cvbs_data *cvbs)
{
	int tmp;

#ifndef OWL_DUAL_DISPLAY_FOR_S700
	/* hdac cvsb disable
	 * cvbs internal 75ohm disable*/
	cvbs_write_reg(cvbs, TVOUT_OCR, cvbs_read_reg(cvbs, TVOUT_OCR) &
			~(TVOUT_OCR_DAC3 | TVOUT_OCR_INREN));
#endif
	tmp = cvbs_read_reg(cvbs, TVOUT_EN);
	tmp &= ~TVOUT_EN_CVBS_EN;
	cvbs_write_reg(cvbs, TVOUT_EN, tmp);
}

/*ntsc(480i),pll1:432M,pll0:594/1.001*/
static void configure_ntsc(struct cvbs_data *cvbs)
{
	cvbs_write_reg(cvbs, CVBS_MSR, CVBS_MSR_CVBS_NTSC_M | CVBS_MSR_CVCKS);
	/*0xfe  0x106*/
	cvbs_write_reg(cvbs, CVBS_AL_SEPO, (cvbs_read_reg(cvbs, CVBS_AL_SEPO) &
			(~CVBS_AL_SEPO_ALEP_MASK)) | CVBS_AL_SEPO_ALEP(0x104));
	cvbs_write_reg(cvbs, CVBS_AL_SEPO, (cvbs_read_reg(cvbs, CVBS_AL_SEPO) &
			(~CVBS_AL_SEPO_ALSP_MASK)) | CVBS_AL_SEPO_ALSP(0x15));
	/*0x20b 0x20d*/
	cvbs_write_reg(cvbs, CVBS_AL_SEPE, (cvbs_read_reg(cvbs, CVBS_AL_SEPE) &
			(~CVBS_AL_SEPE_ALEPEF_MASK)) |
				CVBS_AL_SEPE_ALEPEF(0x20b));

	cvbs_write_reg(cvbs, CVBS_AL_SEPE, (cvbs_read_reg(cvbs, CVBS_AL_SEPE) &
			(~CVBS_AL_SEPE_ALSPEF_MASK)) |
				CVBS_AL_SEPE_ALSPEF(0x11c));

	cvbs_write_reg(cvbs, CVBS_AD_SEP, (cvbs_read_reg(cvbs, CVBS_AD_SEP) &
			(~CVBS_AD_SEP_ADEP_MASK)) | CVBS_AD_SEP_ADEP(0x2cf));
	cvbs_write_reg(cvbs, CVBS_AD_SEP, (cvbs_read_reg(cvbs, CVBS_AD_SEP) &
			(~CVBS_AD_SEP_ADSP_MASK)) | CVBS_AD_SEP_ADSP(0x0));
}

/* pal(576i),pll1:432M,pll0:594M */
static void configure_pal(struct cvbs_data *cvbs)
{
	/*set PAL_D and clk*/
	cvbs_write_reg(cvbs, CVBS_MSR, CVBS_MSR_CVBS_PAL_D | CVBS_MSR_CVCKS);

	cvbs_write_reg(cvbs, CVBS_AL_SEPO, (cvbs_read_reg(cvbs, CVBS_AL_SEPO) &
			(~CVBS_AL_SEPO_ALEP_MASK)) | CVBS_AL_SEPO_ALEP(0x136));

	cvbs_write_reg(cvbs, CVBS_AL_SEPO, (cvbs_read_reg(cvbs, CVBS_AL_SEPO) &
			(~CVBS_AL_SEPO_ALSP_MASK)) | CVBS_AL_SEPO_ALSP(0x17));

	cvbs_write_reg(cvbs, CVBS_AL_SEPE, (cvbs_read_reg(cvbs, CVBS_AL_SEPE) &
			(~CVBS_AL_SEPE_ALEPEF_MASK)) |
			 CVBS_AL_SEPE_ALEPEF(0x26f));

	cvbs_write_reg(cvbs, CVBS_AL_SEPE, (cvbs_read_reg(cvbs, CVBS_AL_SEPE) &
			(~CVBS_AL_SEPE_ALSPEF_MASK)) |
			 CVBS_AL_SEPE_ALSPEF(0x150));

	cvbs_write_reg(cvbs, CVBS_AD_SEP, (cvbs_read_reg(cvbs, CVBS_AD_SEP) &
			(~CVBS_AD_SEP_ADEP_MASK)) | CVBS_AD_SEP_ADEP(0x2cf));

	cvbs_write_reg(cvbs, CVBS_AD_SEP, (cvbs_read_reg(cvbs, CVBS_AD_SEP) &
			(~CVBS_AD_SEP_ADSP_MASK)) | CVBS_AD_SEP_ADSP(0x0));

}
static void cvbs_preline_config(struct cvbs_data *cvbs)
{
	int preline;
	uint32_t val = 0;
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	struct owl_panel *panel = cvbs->ctrl.panel;

	preline = owl_panel_get_preline_num(panel);

	dev_dbg(dev, "%s, cvbs_preline %d\n", __func__, preline);

	preline = (preline <= 0 ? 1 : preline);
	preline = (preline > 16 ? 16 : preline);

	val = cvbs_read_reg(cvbs, TVOUT_PRL);
	val = REG_SET_VAL(val, preline - 1, 11, 8);
	cvbs_write_reg(cvbs, TVOUT_PRL, val);
}

static int cvbs_configure(struct cvbs_data *cvbs)
{
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_dbg(dev, "%s, current_vid %d\n", __func__, cvbs->current_vid);

	switch (cvbs->current_vid) {
	case OWL_TV_MOD_PAL:
		configure_pal(cvbs);
		break;
	case OWL_TV_MOD_NTSC:
		configure_ntsc(cvbs);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void cvbs_power_off(struct cvbs_data *cvbs)
{
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	int val;
	dev_dbg(dev, "%s\n", __func__);

	/* disable tvout clk*/
	clk_disable_unprepare(cvbs->tvout);
	/* disable cvbs_pll clk */
	clk_disable_unprepare(cvbs->cvbs_pll);
	reset_control_assert(cvbs->rst);

}
static void cvbs_power_on(struct cvbs_data *cvbs)
{
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	int val;
	dev_dbg(dev, "%s\n", __func__);

	/*assert reset*/
	if (!owl_cvbs_tvout_enabled(cvbs))
		reset_control_assert(cvbs->rst);

	/* enable tvout*/
	clk_prepare_enable(cvbs->tvout);
	mdelay(10);

	/* set cvbs_pll and clk enable */
	clk_set_rate(cvbs->cvbs_pll, 432000000);
	clk_prepare_enable(cvbs->cvbs_pll);
	mdelay(50);

	/*deasert rest*/
	if (!owl_cvbs_tvout_enabled(cvbs))
		reset_control_deassert(cvbs->rst);
}
static void cvbs_plugin_auto_detect(struct cvbs_data *cvbs, bool enable)
{
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_dbg(dev, "%s\n", __func__);

	auto_detect_bit(cvbs, CVBS_IN);
	cvbs_irq_enable(cvbs, CVBS_IN, enable);
}
static int owl_cvbs_display_enable(struct owl_display_ctrl *ctrl)
{

	struct cvbs_data *cvbs = owl_ctrl_get_drvdata(ctrl);
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	dev_info(dev, "%s\n", __func__);

	cvbs_update_mode_list(cvbs);

	cvbs_preline_config(cvbs);

	cvbs_configure(cvbs);

	cvbs_output_enable(cvbs);

	return 0;
}
static int owl_cvbs_display_disable(struct owl_display_ctrl *ctrl)
{
	struct cvbs_data *cvbs = owl_ctrl_get_drvdata(ctrl);
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	dev_info(dev, "%s\n", __func__);

	cvbs_output_disable(cvbs);

	return 0;
}

static void owl_cvbs_hpd_enable(struct owl_display_ctrl *ctrl, bool enable)
{
	struct cvbs_data *cvbs = owl_ctrl_get_drvdata(ctrl);
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	dev_dbg(dev, "%s, enable %d, hpd_en %d, is_connected %d\n", __func__,
		enable, cvbs->hpd_en, cvbs->is_connected);

	if (enable && !cvbs->hpd_en) {
		cvbs->hpd_en = true;

		if (!cvbs->tvout_enabled)
			cvbs_power_on(cvbs);
		/*
		 * enable to detect cvbs plugin
		 * */
		cvbs_plugin_auto_detect(cvbs, true);

		/*
		 * Switching cvbs resolution, use this interface,
		 * ensure tvaout is connected when switching the resolution
		 * */
		if (cvbs->is_connected)
			set_cvbs_status(cvbs, &cdev, 1);
	} else if (!enable && cvbs->hpd_en) {
		cvbs->hpd_en = false;

		/*
		 * Switching cvbs resolution, use this interface
		 * */
		set_cvbs_status(cvbs, &cdev, 0);
	}

}
static void owl_cvbs_add_panel(struct owl_display_ctrl *ctrl,
		struct owl_panel *panel)
{
	int i;

	struct cvbs_data *cvbs = owl_ctrl_get_drvdata(ctrl);
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	const struct cvbs_config *cfg;
	struct owl_videomode mode;

	/*
	 * set a default videomode
	 */

	/* get default_mod */
	owl_panel_get_default_mode(panel, &mode);
	dev_dbg(dev, "%s, default mode: %dx%d@%d\n", __func__,
		mode.xres, mode.yres, mode.refresh);

	/* convert default_mode to current mode */
	for (i = 0; i < CEA_TIMINGS_LEN; i++) {
		cfg = &cvbs_timings[i];
		cvbs->current_vid = cfg->vid;
		if (cfg->mode.xres == mode.xres &&
				cfg->mode.yres == mode.yres &&
				cfg->mode.refresh == mode.refresh)
			break;
	}
	dev_dbg(dev, "%s, video mode: %dx%d@%d\n", __func__,
		cfg->mode.xres, cfg->mode.yres, cfg->mode.refresh);
	dev_dbg(dev, "%s, current_vid: %d", __func__, cvbs->current_vid);

	owl_panel_set_mode(panel, (struct owl_videomode *)&cfg->mode);

	return 0;
}
static bool owl_cvbs_hpd_is_enabled(struct owl_display_ctrl *ctrl)
{
	struct cvbs_data *cvbs = owl_ctrl_get_drvdata(ctrl);
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;

	dev_dbg(dev, "%s: hpd_is_enabled %d\n", __func__, cvbs->hpd_en);
	return cvbs->hpd_en;
}
static bool owl_cvbs_hpd_is_panel_connected(struct owl_display_ctrl *ctrl)
{
	struct cvbs_data *cvbs = owl_ctrl_get_drvdata(ctrl);
	struct platform_device  *pdev = cvbs->pdev;
	struct device	*dev = &pdev->dev;
	int status;

	dev_dbg(dev, "%s: connected is:%d\n", __func__, cvbs->is_connected);
	return cvbs->is_connected;
}
static struct owl_display_ctrl_ops owl_cvbs_ctrl_ops = {
	.add_panel = owl_cvbs_add_panel,

	.enable = owl_cvbs_display_enable,
	.disable = owl_cvbs_display_disable,

	.hpd_is_panel_connected = owl_cvbs_hpd_is_panel_connected,
	.hpd_enable = owl_cvbs_hpd_enable,
	.hpd_is_enabled = owl_cvbs_hpd_is_enabled,

	.regs_dump = owl_cvbs_regs_dump,
};
static int cvbs_get_resources(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cvbs_data *cvbs = dev_get_drvdata(&pdev->dev);
	struct resource *res;

	dev_dbg(dev, "%s\n", __func__);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res)
		return -ENODEV;
	cvbs->base = devm_ioremap_resource(dev, res);

	if (IS_ERR(cvbs->base)) {
		dev_err(dev, "map registers error\n");
		return -ENODEV;
	}

	cvbs->tvout = devm_clk_get(dev, "tvout");
	if (IS_ERR(cvbs->tvout)) {
		dev_err(dev, "can't get cvbs clk\n");
		return -EINVAL;
	}
	cvbs->cvbs_pll = devm_clk_get(dev, "cvbs_pll");
	if (IS_ERR(cvbs->cvbs_pll)) {
		dev_err(dev, "can't get tvout clk\n");
		return -EINVAL;
	}

	cvbs->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(cvbs->rst)) {
		dev_err(dev, "can't get the reset\n");
		return PTR_ERR(cvbs->rst);
	}
	return 1;
}

static int owl_cvbs_probe(struct platform_device *pdev)
{
	int ret = 0;

	struct device *dev = &pdev->dev;
	const struct of_device_id *match;

	struct cvbs_data *cvbs;

	dev_dbg(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(owl_cvbs_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	cvbs = devm_kzalloc(dev, sizeof(*cvbs), GFP_KERNEL);
	if (cvbs == NULL)
		return -ENOMEM;

	cvbs->pdev = pdev;
	dev_set_drvdata(dev, cvbs);
	mutex_init(&cvbs->lock);

	ret = cvbs_get_resources(pdev);
	if (ret < 0) {
		dev_err(dev, "get resouse failed!\n");
		return ret;
	}

	/*TO DO*/
	/*clear interrup*/
	cvbs_irq_enable(cvbs, CVBS_IN, false);
	cvbs->irq = platform_get_irq(pdev, 0);
	if (cvbs->irq < 0) {
		dev_err(dev, "%s: can't get irq\n", __func__);
		return cvbs->irq;
	}
	if (devm_request_irq(dev, cvbs->irq, cvbs_irq_handler, 0,
				dev_name(dev), cvbs)) {
		dev_err(dev, "%s: Request interrupt %d failed\n",
				__func__, cvbs->irq);
		return -EBUSY;
	}

	/*parse params*/
	ret = cvbs_parse_params(pdev, cvbs);
	if (ret < 0) {
		dev_err(dev, "%s,parse dsi params error: %d\n", __func__, ret);
		return ret;
	}

	cvbs->hpd_en = false;
	cvbs->is_connected = false;

	cvbs->wq = create_workqueue("s700-cvbs");
	INIT_WORK(&cvbs->cvbs_in_work, do_cvbs_in);
	INIT_WORK(&cvbs->cvbs_out_work, do_cvbs_out);
	INIT_DELAYED_WORK(&cvbs->cvbs_check_work, cvbs_check_status);

	queue_delayed_work(cvbs->wq, &cvbs->cvbs_check_work,
				msecs_to_jiffies(3000));
	ret = switch_dev_register(&cdev);
	ret |= switch_dev_register(&sdev_cvbs_audio);
	if (ret < 0) {
		dev_err(dev, "%s: register siwtch failed(%d)\n", __func__, ret);
		return ret;
	}

	/*
	 * register cvbs controller
	 * */
	cvbs->ctrl.type = OWL_DISPLAY_TYPE_CVBS;
	cvbs->ctrl.ops = &owl_cvbs_ctrl_ops;
	owl_ctrl_set_drvdata(&cvbs->ctrl, cvbs);
	ret = owl_ctrl_register(&cvbs->ctrl);
	if (ret < 0) {
		dev_err(dev, "%s: register cvbs ctrl failed(%d)\n",
			__func__, ret);
		return ret;
	}

	cvbs->tvout_enabled = owl_cvbs_tvout_enabled(cvbs);
	dev_dbg(dev, "%s, uboot_enabled %x\n", __func__, cvbs->tvout_enabled);

	/*
	 * enable cvbs at uboot, do this operations,
	 * cvbs really status will be updated after.
	 */
	if (cvbs->tvout_enabled) {
		cvbs->is_connected = true;

		/* power on TO DO */
		cvbs_power_on(cvbs);

		/* switch state ON */
		switch_set_state(&cdev, 1);
		switch_set_state(&sdev_cvbs_audio, 1);
	} else {
		clk_disable_unprepare(cvbs->tvout);
		clk_disable_unprepare(cvbs->cvbs_pll);
	}
	return 0;
}

static int owl_cvbs_remove(struct platform_device *pdev)
{
	struct cvbs_data *cvbs = dev_get_drvdata(&pdev->dev);

	pr_info("%s\n", __func__);
	cvbs_output_disable(cvbs);
	return 0;
}

int owl_cvbs_suspend(struct device *dev)
{
	struct cvbs_data *cvbs;
	dev_info(dev, "%s\n", __func__);
	cvbs = dev_get_drvdata(dev);

	cvbs_irq_enable(cvbs, CVBS_IN, false);
	cvbs_irq_enable(cvbs, CVBS_OUT, false);

	cancel_work_sync(&cvbs->cvbs_in_work);
	cancel_work_sync(&cvbs->cvbs_out_work);
	cancel_delayed_work_sync(&cvbs->cvbs_check_work);

	/* power off */
	cvbs_power_off(cvbs);
	return 0;
}
int owl_cvbs_resume(struct device *dev)
{
	struct cvbs_data *cvbs;
	dev_info(dev, "%s\n", __func__);
	cvbs = dev_get_drvdata(dev);

	cvbs_power_on(cvbs);

	cvbs_plugin_auto_detect(cvbs, true);

	return 0;
}
static UNIVERSAL_DEV_PM_OPS(owl_cvbs_pm_ops, owl_cvbs_suspend,
			    owl_cvbs_resume, NULL);

static struct platform_driver owl_cvbs_driver = {
	.driver         = {
		.name   = "owl_cvbs",
		.owner  = THIS_MODULE,
		.of_match_table = owl_cvbs_of_match,
		.pm		= &owl_cvbs_pm_ops,
	},
	.probe		= owl_cvbs_probe,
	.remove         = owl_cvbs_remove,
};

int __init owl_cvbs_platform_init(void)
{
	pr_info("%s\n", __func__);

	if (platform_driver_register(&owl_cvbs_driver) < 0) {
		pr_err("failed to register owl_cvbs_driver\n");
		return -ENODEV;
	}

	return 0;
}

void __exit owl_cvbs_platform_exit(void)
{
	pr_info("%s\n", __func__);

	platform_driver_unregister(&owl_cvbs_driver);
}

module_init(owl_cvbs_platform_init);
module_exit(owl_cvbs_platform_exit);

MODULE_AUTHOR("huanghaiyu<huanghaiyu@actions-semi.com>");
MODULE_DESCRIPTION("OWL S700 CVBS Driver");
MODULE_LICENSE("GPL v2");
