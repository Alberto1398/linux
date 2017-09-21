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
#include <linux/init.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/switch.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/regulator/consumer.h>
#include <linux/export.h>

#include <video/owl_dss.h>

#include "hdmi.h"


/*===========================================================================
 *			macro definitions and data structures
 *=========================================================================*/

#define OWL_HDMI_ANDROID_COMPATIBLE

//#define OWL_HDMI_SUPPORT_GT7_LED
#ifdef OWL_HDMI_SUPPORT_GT7_LED
#include <linux/leds-gt7.h>
#endif

/* cable check period in jiffies */
#define HDMI_CABLE_CHECK_PERIOD	(msecs_to_jiffies(2000))

/* max number of supportted video mode */
#define HDMI_MAX_VIDEOMODES	(16)

struct hdmi_property {
	int			hdcp_onoff;
	int			channel_invert;
	int			bit_invert;

	int			skip_hpd;
};

struct hdmi_data {
	struct owl_display_ctrl	ctrl;

	/* equal to &ip->pdev->dev */
	struct device		*dev;

	struct hdmi_ip		*ip;
	struct hdmi_property	property;
	struct hdmi_edid	edid;

	struct delayed_work	cable_check_dwork;

	bool			hpd_en;
	bool			cable_status;

	struct mutex		lock;
};

/*===========================================================================
 *				static variables
 *=========================================================================*/

#ifdef OWL_HDMI_ANDROID_COMPATIBLE
static struct switch_dev sdev_hdmi = {
	.name = "hdmi",
};

static struct switch_dev sdev_hdmi_audio = {
	.name = "audio_hdmi",
};
#endif

static int test_hpd_skip;
module_param(test_hpd_skip, int, 0644);

static int test_hdcp_onoff;
module_param(test_hdcp_onoff, int, 0644);

static int test_support_dvi;
module_param(test_support_dvi, int, 0644);

static int test_force_output;
module_param(test_force_output, int, 0644);

/*
 * default video mode, format is XRESxYRES@REFRSH_RATE, such as 1920x1080@60
 */
static char boot_videomode[64];
module_param_string(boot_videomode, boot_videomode, 64, 0644);

/*
 * Logic for the below structure :
 * user enters the CEA or VESA timings by specifying the HDMI code.
 * There is a correspondence between CEA/VESA timing and code, please
 * refer to section 6.3 in HDMI 1.3 specification for timing code.
 *
 * NOTE: Please sort it using resolution size, HDMI resolution selecting
 *	algorithm will choose from index ARRAY_SIZE(cea_timings) - 1 to 0
 */
static const struct hdmi_config	cea_timings[] = {
	{
		/* a specail VID for DVI mode, refresh(61) is used to
		 * distinguish it from VID1280x720P_60_16VS9 */
		VID1280x720P_60_DVI,
		{ 1280, 720, 61, 13468, 110, 220, 5, 20, 40, 5,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID720x480P_60_4VS3,
		{ 720, 480, 60, 37000, 16, 60, 9, 30, 62, 6, 0, 0 },
		false, 7, 0,
	},
	{
		VID720x576P_50_4VS3,
		{ 720, 576, 50, 37037, 12, 68, 5, 39, 64, 5, 0, 0 },
		false, 1, 0,
	},
	{
		VID1280x720P_50_16VS9,
		{ 1280, 720, 50, 13468, 440, 220, 5, 20, 40, 5,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID1280x720P_60_16VS9,
		{ 1280, 720, 60, 13468, 110, 220, 5, 20, 40, 5,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID1280x1024p_60,
		{ 1280, 1024, 60, 9259, 248, 48, 1, 38, 112, 3,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID1920x1080P_50_16VS9,
		{ 1920, 1080, 50, 6734, 528, 148, 4, 36, 44, 5,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID1920x1080P_60_16VS9,
		{ 1920, 1080, 60, 6734, 88, 148, 4, 36, 44, 5,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID2560x1024p_60,
		{ 2560, 1024, 60, 4630, 496, 96, 1, 38, 224, 3,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID2560x1024p_75,
		{ 2560, 1024, 75, 4630, 496, 96, 1, 38, 224, 3,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID3840x1080p_60,
		{ 3840, 1080, 60, 3367, 176, 296, 4, 36, 88, 5,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID3840x2160p_30,
		{ 3840, 2160, 30, 3367, 176, 296, 8, 72, 88, 10,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
	{
		VID4096x2160p_30,
		{ 4096, 2160, 30, 3367, 88, 128, 8, 72, 88, 10,
		DSS_SYNC_HOR_HIGH_ACT | DSS_SYNC_VERT_HIGH_ACT, 0 },
		false, 1, 0,
	},
};
#define CEA_TIMINGS_LEN		(ARRAY_SIZE(cea_timings))

#define CEA_DEFAULT_MODE_IDX	(4)	/* VID1280x720P_60_16VS9 */

static const
struct hdmi_config *hdmic_videomode_to_cfg(struct owl_videomode *mode)
{
	int i;

	const struct hdmi_config *cfg;

	for (i = 0; i < CEA_TIMINGS_LEN; i++) {
		cfg = &cea_timings[i];
		if (cfg->mode.xres == mode->xres &&
		    cfg->mode.yres == mode->yres &&
		    cfg->mode.refresh == mode->refresh)
			return cfg;
	}

	return NULL;
}

/*===========================================================================
 *				HDMI controller
 *=========================================================================*/

static void hdmic_update_cfg(struct hdmi_data *hdmi)
{
	struct hdmi_ip *ip = hdmi->ip;

	struct owl_panel *panel = hdmi->ctrl.panel;

	struct owl_videomode mode;
	const struct hdmi_config *cfg;

	dev_dbg(hdmi->dev, "%s\n", __func__);

	owl_panel_get_mode(panel, &mode);
	cfg = hdmic_videomode_to_cfg(&mode);

	/*
	 * update panel mode & ip cfg & ip settings
	 */
	ip->cfg = cfg;

	if (mode.refresh == 61)	/* DVI, TODO */
		ip->settings.hdmi_mode = HDMI_DVI;
	else if (hdmi->edid.read_ok)
		ip->settings.hdmi_mode = hdmi->edid.hdmi_mode;
	else
		ip->settings.hdmi_mode = HDMI_HDMI;

	ip->settings.prelines = owl_panel_get_preline_num(panel);
}

static int hdmic_parse_property(struct hdmi_data *hdmi)
{
	struct hdmi_property *property = &hdmi->property;
	struct device_node *of_node = hdmi->dev->of_node;

	dev_dbg(hdmi->dev, "%s\n", __func__);

	#define OF_READ_U32(name, p) \
		of_property_read_u32(of_node, (name), (p))

	if (OF_READ_U32("hdcp_onoff", &property->hdcp_onoff))
		property->hdcp_onoff = 0;

	if (OF_READ_U32("channel_invert", &property->channel_invert))
		property->channel_invert = 0;

	if (OF_READ_U32("bit_invert", &property->bit_invert))
		property->bit_invert = 0;

	if (OF_READ_U32("skip_hpd", &property->skip_hpd))
		property->skip_hpd = 0;

	dev_dbg(hdmi->dev, "hdcp_onoff %d, channel_invert %d, bit_invert %d\n",
		property->hdcp_onoff, property->channel_invert,
		property->bit_invert);
	dev_dbg(hdmi->dev, "skip_hpd %d\n", property->skip_hpd);

	#undef OF_READ_U32

	return 0;
}


static void hdmic_send_uevent(struct hdmi_data *hdmi, bool data)
{
	struct owl_panel *panel = hdmi->ctrl.panel;

	owl_panel_hotplug_notify(panel, data);

#ifdef OWL_HDMI_ANDROID_COMPATIBLE
	if (data) {
		switch_set_state(&sdev_hdmi, 1);
	} else {
		switch_set_state(&sdev_hdmi, 0);
		switch_set_state(&sdev_hdmi_audio, 2);
	}
#endif
}

static void hdmic_update_mode_list(struct hdmi_data *hdmi)
{
	int i;
	int n_modes;

	struct hdmi_ip *ip = hdmi->ip;

	struct owl_panel *panel = hdmi->ctrl.panel;
	struct owl_videomode default_mode;
	struct owl_videomode modes[HDMI_MAX_VIDEOMODES];

	const struct hdmi_config *cfg;

	dev_dbg(hdmi->dev, "%s\n", __func__);

	if (panel == NULL) {
		dev_err(hdmi->dev, "%s, panel is NULL\n", __func__);
		BUG();
	}

	/* search from cea_timings in inverted order */
	n_modes = 0;
	for (i = CEA_TIMINGS_LEN - 1; i >= 0; i--) {
		if (n_modes >= HDMI_MAX_VIDEOMODES)
			break;

		cfg = &cea_timings[i];
		if ((cfg->vid == VID1280x720P_60_DVI &&
		     test_support_dvi == 1) ||
		    (cfg->vid < VID1280x720P_60_DVI &&
		     hdmi->edid.device_support_vic[cfg->vid] == 1)) {
			/* got one */

			memcpy(&modes[n_modes], &cfg->mode,
			       sizeof(struct owl_videomode));
			n_modes++;
		}
	}

	dev_info(hdmi->dev, "%s, n_modes %d\n", __func__, n_modes);

	owl_panel_set_mode_list(panel, modes, n_modes);

	/*
	 * if hdmi is not enabled in u-boot,
	 * choose the largest one or default videomode as current mode,
	 * or, if it is powered on, using 'boot_videomode' assigned in
	 * 'owl_hdmic_add_panel', which was passed by u-boot.
	 */
	if (ip->ops->is_video_enabled && !ip->ops->is_video_enabled(ip)) {
		if (n_modes > 0 && PANEL_NEED_EDID(panel) &&
		    hdmi->edid.read_ok) {
			owl_panel_set_mode(panel, &modes[0]);
		} else {
			owl_panel_get_default_mode(panel, &default_mode);
			cfg = hdmic_videomode_to_cfg(&default_mode);
			if (cfg != NULL)
				owl_panel_set_mode(panel,
					(struct owl_videomode *)&cfg->mode);
			else
				dev_warn(hdmi->dev, "default_mode invalid!\n");
		}

		/* update hdmi cfg after videomode being updated */
		hdmic_update_cfg(hdmi);
	}
}

static void hdmic_hotplug_handle(struct hdmi_data *hdmi, bool status)
{
	/* if 'status == true', read edid at least once */
	static bool is_edid_read = false;

	if (test_hpd_skip != 0)
		return;

	if (status && (!hdmi->cable_status || !is_edid_read)) {
		hdmi_edid_parse(&hdmi->edid);
		hdmic_update_mode_list(hdmi);
		is_edid_read = true;
	}

	if (status != hdmi->cable_status) {
		hdmi->cable_status = status;
		hdmic_send_uevent(hdmi, status);
	}
}

static void hdmic_cable_check_cb(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct hdmi_data *hdmi = container_of(dwork, struct hdmi_data,
					      cable_check_dwork);
	struct hdmi_ip *ip = hdmi->ip;

	dev_dbg(hdmi->dev, "%s: hpd_en %d, cable_status %d\n", __func__,
		hdmi->hpd_en, ip->ops->cable_status(ip));

	if (hdmi->hpd_en && (ip->ops->cable_status(ip) ||
			     test_force_output == 1))
		hdmic_hotplug_handle(hdmi, true);
	else
		hdmic_hotplug_handle(hdmi, false);

	/* re-schedule check_cb, in case TV do not send hotplug IRQ */
	if (hdmi->hpd_en)
		schedule_delayed_work(&hdmi->cable_check_dwork,
				      HDMI_CABLE_CHECK_PERIOD);
}

/* now HDMI IRQ is not used. TODO */
#if 1
extern void hdmi_cec_irq_handler();
static irqreturn_t hdmic_hpd_handler(int irq, void *data)
{
	struct hdmi_data *hdmi = data;
	struct hdmi_ip *ip = hdmi->ip;

	dev_dbg(hdmi->dev, "%s: start\n", __func__);

	if (ip->ops->hpd_is_pending(ip)) {
		/* clear pending status */
		ip->ops->hpd_clear_pending(ip);

		/*
		 * NOTE: now, we have 'cable_check_dwork' to check cable
		 *	status, so, HPD IRQ is not necessary. So, we will
		 *	do nothing in HPD handler. TODO
		 */
	} else {
		/*TODO*/
		hdmi_cec_irq_handler();
	}

	/* HDCP, TODO */

	dev_dbg(hdmi->dev, "%s: end\n", __func__);
	return IRQ_HANDLED;
}
#endif

static int __init boot_videomode_setup(char *str)
{
	strcpy(boot_videomode, str);

	return 1;
}
__setup("actions.hdmi.mode=", boot_videomode_setup);

static int owl_hdmic_add_panel(struct owl_display_ctrl *ctrl,
			       struct owl_panel *panel)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);

	const struct hdmi_config *cfg;
	struct owl_videomode mode;

	/*
	 * set a default videomode
	 */

	sscanf(boot_videomode, "%dx%d@%d",
	       &mode.xres, &mode.yres, &mode.refresh);

	cfg = hdmic_videomode_to_cfg(&mode);
	if (cfg == NULL) {
		dev_info(hdmi->dev, "%s, default video mode(%s) invalid\n",
			 __func__, boot_videomode);

		cfg = &cea_timings[CEA_DEFAULT_MODE_IDX];
	}

	dev_dbg(hdmi->dev, "%s, video mode: %dx%d@%d\n", __func__,
		cfg->mode.xres, cfg->mode.yres, cfg->mode.refresh);

	owl_panel_set_mode(panel, (struct owl_videomode *)&cfg->mode);

	/* update hdmi cfg after videomode being updated */
	hdmic_update_cfg(hdmi);

	return 0;
}

static int owl_hdmic_enable(struct owl_display_ctrl *ctrl)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);
	struct hdmi_ip *ip = hdmi->ip;
	const struct hdmi_ip_ops *ip_ops = ip->ops;

	dev_info(hdmi->dev, "%s\n", __func__);

#ifdef OWL_HDMI_SUPPORT_GT7_LED
	ledtrig_bi_color_activity(LED_BLUE);
#endif

	if (!ip_ops)
		return 0;

	if (ip_ops->is_video_enabled && ip_ops->is_video_enabled(ip))
		return 0;

	ip_ops->video_enable(ip);

#ifdef OWL_HDMI_ANDROID_COMPATIBLE
	/* audio must be enabled after HDMI on */
	if(ip->settings.hdmi_mode == HDMI_HDMI)
		switch_set_state(&sdev_hdmi_audio, 1);
#endif
		/* make sure audio is enabled */
		ip_ops->audio_enable(ip);
		mdelay(1);

	if (hdmi->property.hdcp_onoff || test_hdcp_onoff == 1)
		hdmi_hdcp_enable(ip, true);

	return 0;
}

static int owl_hdmic_disable(struct owl_display_ctrl *ctrl)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);
	struct hdmi_ip *ip = hdmi->ip;
	const struct hdmi_ip_ops *ip_ops = ip->ops;

	dev_info(hdmi->dev, "%s\n", __func__);

#ifdef OWL_HDMI_SUPPORT_GT7_LED
	ledtrig_bi_color_activity(LED_RED);
#endif

	if (!ip_ops)
		return 0;

	if (hdmi->property.hdcp_onoff || test_hdcp_onoff == 1)
		hdmi_hdcp_enable(ip, false);

	if (ip_ops->is_video_enabled && !ip_ops->is_video_enabled(ip))
		return 0;

#ifdef OWL_HDMI_ANDROID_COMPATIBLE
	/* tell audio driver */
	if(ip->settings.hdmi_mode == HDMI_HDMI)
		switch_set_state(&sdev_hdmi_audio, 0);
#endif
	/* make sure audio is disabled */
	ip_ops->audio_disable(ip);
	mdelay(1);

	ip_ops->video_disable(ip);
	msleep(500);	/* delay to prevent disable and enable frequently */

	return 0;
}

static bool owl_hdmic_hpd_is_panel_connected(struct owl_display_ctrl *ctrl)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);

	dev_dbg(hdmi->dev, "%s, %d\n", __func__, hdmi->cable_status);

	return hdmi->cable_status;
}

static void owl_hdmic_hpd_enable(struct owl_display_ctrl *ctrl, bool enable)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);

	dev_dbg(hdmi->dev, "%s, enable %d, hpd_en %d\n",
		__func__, enable, hdmi->hpd_en);

	if (enable && !hdmi->hpd_en) {
		hdmi->hpd_en = true;

		/*
		 * schedule cable check delayed work,
		 * and check cable status immediately
		 */
		schedule_delayed_work(&hdmi->cable_check_dwork, 0);
	} else if (!enable && hdmi->hpd_en) {
		hdmi->hpd_en = false;

		/* delay work will kill itself */
	}
}

static bool owl_hdmic_hpd_is_enabled(struct owl_display_ctrl *ctrl)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);

	dev_dbg(hdmi->dev, "%s, hpd_en %d\n", __func__, hdmi->hpd_en);

	/* should check IP status ?? */
	return hdmi->hpd_en;
}

static int owl_hdmic_3d_mode_set(struct owl_display_ctrl *ctrl,
				 enum owl_3d_mode mode)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);

	dev_dbg(hdmi->dev, "%s, mode %d\n", __func__, mode);

	if (mode == hdmi->ip->settings.mode_3d)
		return 0;

	hdmi->ip->settings.mode_3d = mode;

	hdmi->ip->ops->audio_disable(hdmi->ip);

	hdmi->ip->ops->refresh_3d_mode(hdmi->ip);

	hdmi->ip->ops->audio_enable(hdmi->ip);

	return 0;
}

static enum owl_3d_mode owl_hdmic_3d_mode_get(struct owl_display_ctrl *ctrl)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);

	return hdmi->ip->settings.mode_3d;
}

static int owl_hdmic_3d_modes_get(struct owl_display_ctrl *ctrl)
{
	int modes = OWL_3D_MODE_2D;

	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);
	struct hdmi_ip *ip = hdmi->ip;

	if (!ip || !ip->cfg)
		return modes;

	/* a simple 3D modes support rules, TODO*/
	switch (ip->cfg->vid) {
		case VID720x480P_60_4VS3:
		case VID720x576P_50_4VS3:
		case VID1280x720P_60_16VS9:
		case VID1280x720P_50_16VS9:
		case VID1920x1080P_60_16VS9:
		case VID1920x1080P_50_16VS9:
		case VID1920x1080I_50_16VS9:
		case VID1920x1080I_60_16VS9:
			modes |= OWL_3D_MODE_LR_HALF;
			modes |= OWL_3D_MODE_TB_HALF;
			modes |= OWL_3D_MODE_FRAME;
			break;

		default:
			break;
	}

	return modes;
}

static void owl_hdmic_regs_dump(struct owl_display_ctrl *ctrl)
{
	struct hdmi_data *hdmi = owl_ctrl_get_drvdata(ctrl);

	if (hdmi->ip->ops && hdmi->ip->ops->regs_dump)
		hdmi->ip->ops->regs_dump(hdmi->ip);
}

static struct owl_display_ctrl_ops owl_hdmi_ctrl_ops = {
	.add_panel = owl_hdmic_add_panel,

	.enable = owl_hdmic_enable,
	.disable = owl_hdmic_disable,

	.hpd_is_panel_connected = owl_hdmic_hpd_is_panel_connected,
	.hpd_enable = owl_hdmic_hpd_enable,
	.hpd_is_enabled = owl_hdmic_hpd_is_enabled,

	.set_3d_mode = owl_hdmic_3d_mode_set,
	.get_3d_mode = owl_hdmic_3d_mode_get,
	.get_3d_modes = owl_hdmic_3d_modes_get,

	.regs_dump = owl_hdmic_regs_dump,
};

/*===========================================================================
 *				HDMI IP
 *=========================================================================*/
static int hdmi_ip_get_resource(struct hdmi_ip *ip)
{
	struct platform_device *pdev = ip->pdev;
	struct device *dev = &pdev->dev;

	struct resource *res;

	dev_dbg(dev, "%s\n", __func__);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res)) {
		dev_err(dev, "%s: can't get regs\n", __func__);
		return PTR_ERR(res);
	}

	ip->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ip->base)) {
		dev_err(dev, "%s: ipremap error\n", __func__);
		return PTR_ERR(ip->base);
	}
	dev_dbg(dev, "%s: base is 0x%p\n", __func__, ip->base);

	ip->irq = platform_get_irq(pdev, 0);
	if (ip->irq < 0) {
		dev_err(dev, "%s: can't get irq\n", __func__);
		return ip->irq;
	}

	return 0;
}

/*
 * some basic initialisation for HDMI IP
 */
static int hdmi_ip_init(struct hdmi_ip *ip)
{
	int ret = 0;

	struct device *dev = &ip->pdev->dev;
	struct hdmi_data *hdmi = ip->pdata;
	struct hdmi_ip_settings *settings = &ip->settings;

	ret = hdmi_ip_get_resource(ip);
	if (ret < 0) {
		dev_err(dev, "%s: get IP resource failed(%d)\n", __func__, ret);
		return ret;
	}

	/* now HDMI IRQ is not used. TODO */
#if 1
	if (devm_request_irq(dev, ip->irq, hdmic_hpd_handler, 0,
			     dev_name(dev), hdmi)) {
		dev_err(dev, "%s: Request interrupt %d failed\n",
			__func__, ip->irq);
		return -EBUSY;
	}
#endif

	/*
	 * HDMI IP settings
	 */
	settings->hdmi_src = DE;
	settings->vitd_color = 0xff0000;
	settings->mode_3d = OWL_3D_MODE_2D;
	settings->pixel_encoding = RGB444;
	settings->color_xvycc = 0;
	settings->deep_color = color_mode_24bit;
	settings->hdmi_mode = HDMI_HDMI;

	settings->channel_invert = hdmi->property.channel_invert;
	settings->bit_invert = hdmi->property.bit_invert;

	/*
	 * HDMI IPxxxx's specail init
	 */
	if (ip->ops && ip->ops && ip->ops->init) {
		ret = ip->ops->init(ip);
		if (ret < 0) {
			dev_err(dev, "%s: IP init failed(%d)\n", __func__, ret);
			return ret;
		}
	}

	return 0;
}

int hdmi_ip_register(struct hdmi_ip *ip)
{
	int ret = 0;

	struct device *dev;
	struct hdmi_data *hdmi;

	pr_info("%s\n", __func__);

	if (!ip || !ip->pdev) {
		pr_err("%s: ip or ip->pdev is NULL!\n", __func__);
		return -1;
	}

	dev = &ip->pdev->dev;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (hdmi == NULL) {
		dev_err(dev, "%s: No Mem for hdmi_data\n", __func__);
		return -ENOMEM;
	}

	hdmi->dev = dev;
	hdmi->ip = ip;
	ip->pdata = hdmi;

	INIT_DELAYED_WORK(&hdmi->cable_check_dwork, hdmic_cable_check_cb);

#ifdef OWL_HDMI_ANDROID_COMPATIBLE
	ret = switch_dev_register(&sdev_hdmi);
	ret |= switch_dev_register(&sdev_hdmi_audio);
	if (ret < 0) {
		dev_err(dev, "%s: register siwtch failed(%d)\n", __func__, ret);
		return ret;
	}
#endif
	mutex_init(&hdmi->lock);

	hdmi->hpd_en = false;
	hdmi->cable_status = false;

	hdmic_parse_property(hdmi);

	ret = hdmi_ip_init(ip);
	if (ret < 0) {
		dev_err(dev, "%s: hdmi ip init failed(%d)\n", __func__, ret);
		return ret;
	}

	/*
	 * register HDMI controller
	 */
	hdmi->ctrl.type = OWL_DISPLAY_TYPE_HDMI;
	hdmi->ctrl.ops = &owl_hdmi_ctrl_ops;

	owl_ctrl_set_drvdata(&hdmi->ctrl, hdmi);

	ret = owl_ctrl_register(&hdmi->ctrl);
	if (ret < 0) {
		dev_err(dev, "%s: register hdmi ctrl failed(%d)\n",
			__func__, ret);
		return ret;
	}

	/* power on hdmi controller at boot */
	if (ip->ops->power_on && ip->ops->power_on(ip) < 0) {
		dev_err(dev, "%s: power on failed\n", __func__);
		return -EINVAL;
	}

	hdmi_ddc_init();

	hdmi_hdcp_init(ip);

	hdmi_cec_init(ip);
	/*
	 * Update cable_status to true if HDMI is enabled at u-boot,
	 * and do not care about the REAL connected status.
	 * If cable was plugged out during booting, we will
	 * check and report it later.
	 */
	if (ip->ops->is_video_enabled && ip->ops->is_video_enabled(ip)) {
		hdmi->cable_status = true;

	#ifdef OWL_HDMI_ANDROID_COMPATIBLE
		/* switch should set to the real status too */
		switch_set_state(&sdev_hdmi, 1);
		switch_set_state(&sdev_hdmi_audio, 1);
	#endif
	}

	return 0;
}
EXPORT_SYMBOL(hdmi_ip_register);

void hdmi_ip_unregister(struct hdmi_ip *ip)
{
	dev_info(&ip->pdev->dev, "%s\n", __func__);

	hdmi_hdcp_exit(ip);
}
EXPORT_SYMBOL(hdmi_ip_unregister);

int hdmi_ip_generic_suspend(struct hdmi_ip *ip)
{
	struct hdmi_data *hdmi = ip->pdata;

	dev_info(&ip->pdev->dev, "%s\n", __func__);

	cancel_delayed_work(&hdmi->cable_check_dwork);
	flush_delayed_work(&hdmi->cable_check_dwork);

	if (ip->ops->power_off)
		ip->ops->power_off(ip);

	return 0;
}
EXPORT_SYMBOL(hdmi_ip_generic_suspend);

int hdmi_ip_generic_resume(struct hdmi_ip *ip)
{
	struct hdmi_data *hdmi = ip->pdata;

	dev_info(&ip->pdev->dev, "%s\n", __func__);

	if (ip->ops->power_on)
		ip->ops->power_on(ip);

	if (hdmi->hpd_en)
		schedule_delayed_work(&hdmi->cable_check_dwork,
				      HDMI_CABLE_CHECK_PERIOD);
	return 0;
}
EXPORT_SYMBOL(hdmi_ip_generic_resume);
