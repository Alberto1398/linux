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
 *	2015/8/20: Created by Lipeng.
 */
#define DEBUGX
#define pr_fmt(fmt) "owl_panel: %s, " fmt, __func__

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

#include <video/owl_dss.h>

static LIST_HEAD(g_panel_list);
static DEFINE_MUTEX(g_panel_list_lock);

static int g_panel_nums;

static struct class owl_panel_class;

static void panel_update_path_info(struct owl_panel *panel)
{
	struct owl_de_path *path = panel->path;
	struct owl_de_path_info p_info;

	owl_de_path_get_info(path, &p_info);

	p_info.type = PANEL_TYPE(panel);

	/* panel must have a valid resolution, which is used for path size */
	owl_panel_get_resolution(panel, &p_info.width, &p_info.height);
	if (p_info.width <= 0 || p_info.height <= 0) {
		pr_debug("path size(%dx%d) invalid\n",
			 p_info.width, p_info.height);
	}
	p_info.vmode = owl_panel_get_vmode(panel);


	switch (PANEL_BPP(panel)) {
	case 16:
		p_info.dither_mode = DITHER_24_TO_16;
		break;

	case 18:
		p_info.dither_mode = DITHER_24_TO_18;
		break;

	default:
		p_info.dither_mode = DITHER_DISABLE;
		break;
	}

	owl_panel_get_gamma(panel, &p_info.gamma_r_val,
				&p_info.gamma_g_val, &p_info.gamma_b_val);

	owl_de_path_set_info(path, &p_info);
}

/*
 * Update panel's timing info, include preline number, preline time
 * VB time and so on.
 *
 * Calculate preline numbers according to the recommended preline time
 * and panel's timings.
 * Also, for some panel, will not get the recommended preline time,
 * so we should re-calculate and save the real preline time, which
 * will be used while wait VB valid.
 *
 * Calculate VB time according to panel's timings.
 *
 * Formula for preline number is:
 *	(recommended preline time(us) * 1000000)
 *	/ (x_res + hfp + hbp + hsw)) / pixel_clock - vfp)
 *
 * Formula for preline time(us) is:
 *	(vfp + preline_num) * (x_res + hfp + hbp + hsw) * pixel_clock / 1000000
 *
 * Formula for VB time(us) is:
 *	vsw * (x_res + hfp + hbp + hsw) * pixel_clock / 1000000
 *
 */
static void panel_update_timing_info(struct owl_panel *panel)
{
	int preline_num;
	int tmp;

	struct owl_videomode *mode;

	if (panel == NULL)
		return;

	mode = &panel->mode;

	tmp = (mode->xres + mode->hfp + mode->hbp + mode->hsw) * mode->pixclock;

	/* caculate preline number */
	preline_num = tmp;
	if (preline_num != 0)
		preline_num = (DSS_RECOMMENDED_PRELINE_TIME * 1000000
			+ preline_num / 2) / preline_num; /* round */

	preline_num -= mode->vfp;
	preline_num = (preline_num <= 0 ? 1 : preline_num);

	panel->desc.preline_num = preline_num;

	/* caculate preline time */
	panel->desc.preline_time = (preline_num + mode->vfp) * tmp / 1000000;
	if (panel->desc.preline_time > DSS_RECOMMENDED_PRELINE_TIME * 2 ||
	    panel->desc.preline_time < DSS_RECOMMENDED_PRELINE_TIME / 2)
		pr_warn("preline time(%dus) is abnormal\n",
			panel->desc.preline_time);

	/* caculate VB time */
	panel->desc.vb_time = mode->vsw * tmp / 1000000;
}

int owl_panel_init(void)
{
	int ret = 0;

	pr_info("start\n");

	ret = class_register(&owl_panel_class);
	if (ret < 0) {
		pr_err("register owl panel class failed(%d)\n", ret);
		return ret;
	}

	g_panel_nums = 0;

	return 0;
}
EXPORT_SYMBOL(owl_panel_init);

struct owl_panel *owl_panel_alloc(const char *name, enum owl_display_type type)
{
	struct owl_panel *panel;

	panel = kzalloc(sizeof(*panel), GFP_KERNEL);
	if (panel == NULL)
		return NULL;

	panel->desc.type = type;
	panel->desc.name = kzalloc(strlen(name), GFP_KERNEL);
	if (panel->desc.name)
		strcpy(panel->desc.name, name);

	return panel;
}
EXPORT_SYMBOL(owl_panel_alloc);

void owl_panel_free(struct owl_panel *panel)
{
	kzfree(panel->desc.name);
	kzfree(panel);
}
EXPORT_SYMBOL(owl_panel_free);

int owl_panel_register(struct device *parent, struct owl_panel *panel)
{
	int ret = 0;

	struct device *dev;

	pr_info("start\n");

	if (panel == NULL) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	if (owl_panel_get_by_type(panel->desc.type) != NULL) {
		/* TODO */
		pr_err("type(%d) was already registered!\n", panel->desc.type);
		return -EBUSY;
	}

	/*
	 * some initialization
	 */
	panel->state = OWL_DSS_STATE_OFF;

	/* TODO */
	panel->desc.is_dummy = (PANEL_TYPE(panel) == OWL_DISPLAY_TYPE_DUMMY);

	panel->desc.overscan_left = 0;
	panel->desc.overscan_top = 0;
	panel->desc.overscan_right = 0;
	panel->desc.overscan_bottom = 0;

	panel->desc.brightness = 100;
	panel->desc.contrast = 100;
	panel->desc.saturation = 100;

	/*
	 * register to owl panel class
	 */

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	device_initialize(dev);

	dev->class = &owl_panel_class;
	dev->parent = parent;
	dev_set_drvdata(dev, panel);
	panel->dev = dev;

	ret = kobject_set_name(&dev->kobj, "%s", panel->desc.name);
	if (ret)
		goto kobject_set_name_failed;

	ret = device_add(dev);
	if (ret)
		goto device_add_failed;

	/*
	 * match with DE path
	 */
	ret = owl_de_path_add_panel(panel);
	if (ret < 0) {
		pr_err("add panel to DE path failed!\n");
		goto add_to_de_path_failed;
	}
	panel_update_path_info(panel);

	/*
	 * match with display controller
	 */
	ret = owl_ctrl_add_panel(panel);
	if (ret < 0) {
		pr_err("add panel to ctrl failed!\n");
		goto add_to_ctrl_failed;
	}

	mutex_lock(&g_panel_list_lock);
	list_add(&panel->list, &g_panel_list);
	g_panel_nums++;
	mutex_unlock(&g_panel_list_lock);

	return 0;

add_to_ctrl_failed:
add_to_de_path_failed:
	owl_ctrl_remove_panel(panel);
device_add_failed:
kobject_set_name_failed:
	put_device(dev);

	return ret;
}
EXPORT_SYMBOL(owl_panel_register);

void owl_panel_unregister(struct owl_panel *panel)
{
	owl_de_path_remove_panel(panel);
	owl_ctrl_remove_panel(panel);
	put_device(panel->dev);

	g_panel_nums--;
}
EXPORT_SYMBOL(owl_panel_unregister);

int owl_panel_get_panel_num(void)
{
	return g_panel_nums;
}
EXPORT_SYMBOL(owl_panel_get_panel_num);

bool owl_panel_get_pri_panel_resolution(int *width, int *height)
{
    struct owl_panel *panel;
    struct owl_de_path *path;

    panel = NULL;
    path = NULL;
    owl_panel_for_each(panel) {
        if (PANEL_IS_PRIMARY(panel)) {
            path = panel->path;
            pr_debug("primay display width %d , height %d\n", path->info.width, path->info.height);
            *width = path->info.width;
            *height = path->info.height;

            /* S700, HDMI 4k scaling need limit draw size */
            if (owl_de_is_s700()) {
                if (*width >= 3840)
                    *width /= 2;
                if (*height >= 2160)
                    *height /= 2;
            }

            return true;
        }
    }
    return false;
}
EXPORT_SYMBOL(owl_panel_get_pri_panel_resolution);

struct owl_panel *owl_panel_get_by_name(const char *name)
{
	struct owl_panel *p;

	mutex_lock(&g_panel_list_lock);

	list_for_each_entry(p, &g_panel_list, list) {
		if (strcmp(p->desc.name, name) == 0) {
			mutex_unlock(&g_panel_list_lock);
			return p;
		}
	}
	mutex_unlock(&g_panel_list_lock);

	return NULL;
}
EXPORT_SYMBOL(owl_panel_get_by_name);

struct owl_panel *owl_panel_get_by_type(enum owl_display_type type)
{
	struct owl_panel *p;

	mutex_lock(&g_panel_list_lock);

	list_for_each_entry(p, &g_panel_list, list) {
		if (p->desc.type == type) {
			mutex_unlock(&g_panel_list_lock);
			return p;
		}
	}

	mutex_unlock(&g_panel_list_lock);

	return NULL;
}
EXPORT_SYMBOL(owl_panel_get_by_type);

struct owl_panel *owl_panel_get_next_panel(struct owl_panel *from)
{
	if (from == NULL) {
		return list_first_entry_or_null(&g_panel_list,
						struct owl_panel, list);
	} else {
		if (list_is_last(&from->list, &g_panel_list))
			return NULL;
		else
			return list_next_entry(from, list);
	}
}
EXPORT_SYMBOL(owl_panel_get_next_panel);

void owl_panel_get_default_mode(struct owl_panel *panel,
				struct owl_videomode *mode)
{
	memcpy(mode, &panel->default_mode, sizeof(struct owl_videomode));
}
EXPORT_SYMBOL(owl_panel_get_default_mode);

void owl_panel_get_mode(struct owl_panel *panel, struct owl_videomode *mode)
{
	memcpy(mode, &panel->mode, sizeof(struct owl_videomode));
}
EXPORT_SYMBOL(owl_panel_get_mode);

int owl_panel_set_mode(struct owl_panel *panel, struct owl_videomode *mode)
{
	memcpy(&panel->mode, mode, sizeof(struct owl_videomode));
	panel_update_path_info(panel);
	panel_update_timing_info(panel);

	return 0;
}
EXPORT_SYMBOL(owl_panel_set_mode);

void owl_panel_set_mode_list(struct owl_panel *panel,
			     struct owl_videomode *modes, int n_modes)
{
	int i;

	if (panel == NULL || modes == NULL || n_modes <= 0)
		return;

	if (n_modes > OWL_PANEL_MAX_VIDEOMODES)
		n_modes = OWL_PANEL_MAX_VIDEOMODES;

	panel->n_modes = n_modes;
	for (i = 0; i < n_modes; i++)
		memcpy(&panel->mode_list[i], &modes[i],
		       sizeof(struct owl_videomode));
}
EXPORT_SYMBOL(owl_panel_set_mode_list);

int owl_panel_enable(struct owl_panel *panel)
{
	struct owl_dss_panel_desc *desc = &panel->desc;

	pr_info("state %d, power_on_delay %d, enable_delay %d\n",
		panel->state, desc->power_on_delay, desc->enable_delay);

	if (panel->state == OWL_DSS_STATE_ON)
		return 0;

	/* panel power on */
	if (desc->ops && desc->ops->power_on) {
		desc->ops->power_on(panel);
		if (desc->power_on_delay > 0)
			mdelay(desc->power_on_delay);
	}
	/* controller power on */
	owl_ctrl_power_on(panel->ctrl);

	/* panel enable */
	if (desc->ops && desc->ops->enable)
		desc->ops->enable(panel);

	/* controller enable */
	owl_ctrl_enable(panel->ctrl);

	if (desc->enable_delay > 0)
		mdelay(desc->enable_delay);

	panel->state = OWL_DSS_STATE_ON;

	return 0;
}
EXPORT_SYMBOL(owl_panel_enable);

void owl_panel_disable(struct owl_panel *panel)
{
	struct owl_dss_panel_desc *desc = &panel->desc;

	pr_info("state %d, power_off_delay %d, disable_delay %d\n",
		panel->state, desc->power_off_delay, desc->disable_delay);

	if (panel->state == OWL_DSS_STATE_OFF)
		return;
	/*
	 * This special condition for no Ram mipi oled. TODO
	 * */
	if (panel->desc.type == OWL_DISPLAY_TYPE_DSI) {
		/* panel disable, send 'sleep in' command */
		if (desc->ops && desc->ops->disable)
			desc->ops->disable(panel);
		/* ctrl video disable */
		owl_ctrl_disable(panel->ctrl);

		if (desc->disable_delay > 0)
			mdelay(desc->disable_delay);

		/* ctrl power off */
		owl_ctrl_power_off(panel->ctrl);

		/* panel power off*/
		if (desc->ops && desc->ops->power_off) {
			desc->ops->power_off(panel);
			if (desc->power_off_delay > 0)
				mdelay(desc->power_off_delay);
		}
	} else {
		/* ctrl video disable */
		owl_ctrl_disable(panel->ctrl);

		if (desc->disable_delay > 0)
			mdelay(desc->disable_delay);

		/* panel disable */
		if (desc->ops && desc->ops->disable)
			desc->ops->disable(panel);

		/* ctrl power off */
		owl_ctrl_power_off(panel->ctrl);

		/* panel power off*/
		if (desc->ops && desc->ops->power_off) {
			desc->ops->power_off(panel);
			if (desc->power_off_delay > 0)
				mdelay(desc->power_off_delay);
		}
	}
	panel->state = OWL_DSS_STATE_OFF;
}
EXPORT_SYMBOL(owl_panel_disable);

extern int owl_backlight_is_on(void);
/*
 * owl_panel_status_get
 * Get all panels' on/off status, the return value is constructed
 * as following:
 *	bit16			bit15:0
 *	is backlight on		enabled panels, which is a bit map
 *				of 'enum owl_display_type'
 * for example:
 * 0x10002 means:
 *	dsi is on, and backlight is on.
 * 0x10024 means:
 *	epd and hdmi is on, and backlight is on.
 * 0x0 means:
 *	all panels are off, backlight is off too.
 */
int owl_panel_status_get(void)
{
	int is_backlight_on = 0;
	int panels_on = 0;

	struct owl_panel *p;

	mutex_lock(&g_panel_list_lock);
	list_for_each_entry(p, &g_panel_list, list) {
		if (p->state == OWL_DSS_STATE_ON)
			panels_on |= p->desc.type;
	}
	mutex_unlock(&g_panel_list_lock);

	if (owl_backlight_is_on() > 0)
		is_backlight_on = 1;

	return (is_backlight_on << 16) | panels_on;
}
EXPORT_SYMBOL(owl_panel_status_get);

void owl_panel_get_resolution(struct owl_panel *panel, int *xres, int *yres)
{
	if (panel == NULL)
		return;

	*xres = panel->mode.xres;
	*yres = panel->mode.yres;
}
EXPORT_SYMBOL(owl_panel_get_resolution);

/*
 * get valid display area
 * fromat is coor(x, y), widthxheight
 * for overscan reason, display area may be small than resolution
 */
void owl_panel_get_disp_area(struct owl_panel *panel, int *x, int *y,
			     int *width, int *height)
{
	*x = panel->desc.overscan_left;
	*y = panel->desc.overscan_top;

	*width = panel->mode.xres - panel->desc.overscan_left
			- panel->desc.overscan_right;
	*height = panel->mode.yres - panel->desc.overscan_top
			- panel->desc.overscan_bottom;
}
EXPORT_SYMBOL(owl_panel_get_disp_area);

int owl_panel_get_vmode(struct owl_panel *panel)
{
	if (panel == NULL)
		return 0;

	return panel->mode.vmode;
}
EXPORT_SYMBOL(owl_panel_get_vmode);

int owl_panel_get_refresh_rate(struct owl_panel *panel)
{
	if (panel == NULL)
		return 0;

	return panel->mode.refresh;
}
EXPORT_SYMBOL(owl_panel_get_refresh_rate);

void owl_panel_get_draw_size(struct owl_panel *panel, int *width, int *height)
{
	if (panel == NULL)
		return;

	if (panel->draw_width == 0 || panel->draw_height == 0) {
		if (panel->mode.xres != 0 && panel->mode.yres != 0) {
			pr_debug("use 'mode' as draw size\n");
			*width = panel->mode.xres;
			*height = panel->mode.yres;
		} else {
			pr_debug("use default size as draw size\n");
			*width = 1920;
			*height = 1080;
		}
	} else {
		*width = panel->draw_width;
		*height = panel->draw_height;
	}

	/* S700 OTT, 4K point-to-point, no need limit draw size */
	if (!(owl_de_is_s700() && owl_de_is_ott())) {
		/*if (*width > 2560)
			*width /= 2;
		if (*height > 2048)
			*height /= 2;*/
	}

	/* S700, HDMI 4k scaling need limit draw size */
	if (owl_de_is_s700()) {
		if (*width >= 3840)
			*width /= 2;
		if (*height >= 2160)
			*height /= 2;
	}
}
EXPORT_SYMBOL(owl_panel_get_draw_size);

int owl_panel_get_bpp(struct owl_panel *panel)
{
	return panel == NULL ? 0 : panel->desc.bpp;
}
EXPORT_SYMBOL(owl_panel_get_bpp);

char *owl_panel_get_name(struct owl_panel *panel)
{
	return panel == NULL ? NULL : panel->desc.name;
}
EXPORT_SYMBOL(owl_panel_get_name);

enum owl_display_type owl_panel_get_type(struct owl_panel *panel)
{
	return panel == NULL ? OWL_DISPLAY_TYPE_NONE : panel->desc.type;
}
EXPORT_SYMBOL(owl_panel_get_type);

int owl_panel_parse_panel_info(struct device_node *of_node,
			       struct owl_panel *panel)
{
	uint32_t val;

	struct device_node *entry = of_node;

	struct owl_videomode *mode = &panel->mode;
	struct owl_dss_panel_desc *desc = &panel->desc;

	pr_info("start\n");

	#define OF_READ_U32(name, p) \
		of_property_read_u32(entry, (name), (p))

	/*
	 * parse standard panel descriptors, can be zero
	 */
	OF_READ_U32("width_mm", &desc->width_mm);
	OF_READ_U32("height_mm", &desc->height_mm);
	OF_READ_U32("bpp", &desc->bpp);

	OF_READ_U32("power_on_delay", &desc->power_on_delay);
	OF_READ_U32("power_off_delay", &desc->power_off_delay);
	OF_READ_U32("enable_delay", &desc->enable_delay);
	OF_READ_U32("disable_delay", &desc->disable_delay);
	OF_READ_U32("vsync_off_us", &desc->vsync_off_us);

	/* parameter of gamma correction */
	OF_READ_U32("gamma_r_val", &desc->gamma_r_val);
	OF_READ_U32("gamma_g_val", &desc->gamma_g_val);
	OF_READ_U32("gamma_b_val", &desc->gamma_b_val);

	if (OF_READ_U32("is_primary", &val))
		desc->is_primary = false;
	else
		desc->is_primary = (val == 1 ? true : false);

	if (OF_READ_U32("skip_edid", &val))
		desc->need_edid = true;
	else
		desc->need_edid = (val == 0 ? true : false);

	pr_debug("is_primary %d, need_edid %d\n",
		 desc->is_primary, desc->need_edid);

	pr_debug("%dmm x %dmm, bpp %d\n", desc->width_mm,
		 desc->height_mm, desc->bpp);

	pr_debug("power_on_delay %d, power_off_delay %d\n",
		 desc->power_on_delay, desc->power_off_delay);
	pr_debug("enable_delay %d, disable_delay %d\n",
		 desc->enable_delay, desc->disable_delay);

	OF_READ_U32("draw_width", &panel->draw_width);
	OF_READ_U32("draw_height", &panel->draw_height);
	pr_debug("draw size %dx%d\n", panel->draw_width, panel->draw_height);

	/*
	 * parse panel brightness info
	 * */
	OF_READ_U32("brightness", &panel->desc.brightness);
	OF_READ_U32("max_brightness", &panel->desc.max_brightness);
	OF_READ_U32("min_brightness", &panel->desc.min_brightness);
	pr_debug("brightness %d, max_brightness %d, min_brightness %d\n",
			panel->desc.brightness, panel->desc.max_brightness,
			panel->desc.min_brightness);

	/*
	 * parse default video mode, some can be zero
	 */
	entry = of_parse_phandle(of_node, "videomode-0", 0);
	if (!entry) {
		pr_info("no entry for 'videomode-0'\n");
	} else {
		OF_READ_U32("xres", &mode->xres);
		OF_READ_U32("yres", &mode->yres);
		OF_READ_U32("refresh_rate", &mode->refresh);
		OF_READ_U32("pixel_clock", &mode->pixclock);
		OF_READ_U32("hsw", &mode->hsw);
		OF_READ_U32("hbp", &mode->hbp);
		OF_READ_U32("hfp", &mode->hfp);
		OF_READ_U32("vsw", &mode->vsw);
		OF_READ_U32("vfp", &mode->vfp);
		OF_READ_U32("vbp", &mode->vbp);
		OF_READ_U32("vmode", &mode->vmode);
	}

	pr_debug("%d x %d, refresh %d\n", mode->xres, mode->yres,
		 mode->refresh);

	pr_debug("pixclock %d\n", mode->pixclock);
	pr_debug("hfp %d, hbp %d, vfp %d, vbp %d, hsw %d, vsw %d\n",
		 mode->hfp, mode->hbp, mode->vfp, mode->vbp,
		 mode->hsw, mode->vsw);
	panel_update_timing_info(panel);

	/* set default_mode */
	if (mode->xres != 0 && mode->yres != 0 && mode->refresh != 0) {
		panel->default_mode.xres = mode->xres;
		panel->default_mode.yres = mode->yres;
		panel->default_mode.refresh = mode->refresh;
	} else {
		panel->default_mode.xres = 1280;
		panel->default_mode.yres = 720;
		panel->default_mode.refresh = 60;
	}

	#undef OF_READ_U32

	return 0;
}
EXPORT_SYMBOL(owl_panel_parse_panel_info);

void owl_panel_hotplug_cb_set(struct owl_panel *panel, owl_panel_cb_t cb,
			      void *data)
{
	panel->hotplug_cb = cb;
	panel->hotplug_data = data;
}

void owl_panel_vsync_cb_set(struct owl_panel *panel, owl_panel_cb_t cb,
			    void *data)
{
	panel->vsync_cb = cb;
	panel->vsync_data = data;
}

void owl_panel_hotplug_notify(struct owl_panel *panel, bool is_connected)
{
	if (panel->hotplug_cb)
		panel->hotplug_cb(panel, panel->vsync_data, is_connected);
}
EXPORT_SYMBOL(owl_panel_hotplug_notify);

void owl_panel_vsync_notify(struct owl_panel *panel)
{
	if (panel->vsync_cb)
		panel->vsync_cb(panel, panel->vsync_data, 0);
}
EXPORT_SYMBOL(owl_panel_vsync_notify);

/*
 * check panel's connect status, the rule is:
 *	if panel's .hpd_is_connected is not NULL, using it;
 *	or, if its controller's .hpd_is_panel_connected is not NULL, using it;
 *	or, return TRUE.
 */
bool owl_panel_hpd_is_connected(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->hpd_is_connected)
		return panel->desc.ops->hpd_is_connected(panel);
	if (ctrl && ctrl->ops && ctrl->ops->hpd_is_panel_connected)
		return ctrl->ops->hpd_is_panel_connected(ctrl);
	else
		return true;
}
EXPORT_SYMBOL(owl_panel_hpd_is_connected);

void owl_panel_hpd_enable(struct owl_panel *panel, bool enable)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->hpd_enable)
		panel->desc.ops->hpd_enable(panel, enable);
	if (ctrl && ctrl->ops && ctrl->ops->hpd_enable)
		ctrl->ops->hpd_enable(ctrl, enable);
	else
		return;
}
EXPORT_SYMBOL(owl_panel_hpd_enable);

bool owl_panel_hpd_is_enabled(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->hpd_is_enabled)
		return panel->desc.ops->hpd_is_enabled(panel);
	if (ctrl && ctrl->ops && ctrl->ops->hpd_is_enabled)
		return ctrl->ops->hpd_is_enabled(ctrl);
	else
		return true;
}
EXPORT_SYMBOL(owl_panel_hpd_is_enabled);

int owl_panel_get_preline_num(struct owl_panel *panel)
{
	return (panel == NULL ? 0 : panel->desc.preline_num);
}
EXPORT_SYMBOL(owl_panel_get_preline_num);

int owl_panel_get_preline_time(struct owl_panel *panel)
{
	return (panel == NULL ? 0 : panel->desc.preline_time);
}
EXPORT_SYMBOL(owl_panel_get_preline_time);

int owl_panel_get_vb_time(struct owl_panel *panel)
{
	return (panel == NULL ? 0 : panel->desc.vb_time);
}
EXPORT_SYMBOL(owl_panel_get_vb_time);

int owl_panel_3d_mode_set(struct owl_panel *panel, enum owl_3d_mode mode)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->set_3d_mode)
		return panel->desc.ops->set_3d_mode(panel, mode);
	if (ctrl && ctrl->ops && ctrl->ops->set_3d_mode)
		return ctrl->ops->set_3d_mode(ctrl, mode);

	return 0;
}
EXPORT_SYMBOL(owl_panel_3d_mode_set);

enum owl_3d_mode owl_panel_3d_mode_get(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->get_3d_mode)
		return panel->desc.ops->get_3d_mode(panel);
	if (ctrl && ctrl->ops && ctrl->ops->get_3d_mode)
		return ctrl->ops->get_3d_mode(ctrl);

	return OWL_3D_MODE_2D;
}
EXPORT_SYMBOL(owl_panel_3d_mode_get);

int owl_panel_3d_modes_get(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->get_3d_modes)
		return panel->desc.ops->get_3d_modes(panel);
	if (ctrl && ctrl->ops && ctrl->ops->get_3d_modes)
		return ctrl->ops->get_3d_modes(ctrl);

	return 0;
}
EXPORT_SYMBOL(owl_panel_3d_modes_get);

int owl_panel_brightness_get(struct owl_panel *panel)
{
	return panel->desc.brightness;
}
EXPORT_SYMBOL(owl_panel_brightness_get);

int owl_panel_contrast_get(struct owl_panel *panel)
{
	return panel->desc.contrast;
}
EXPORT_SYMBOL(owl_panel_contrast_get);

int owl_panel_saturation_get(struct owl_panel *panel)
{
	return panel->desc.saturation;
}
EXPORT_SYMBOL(owl_panel_saturation_get);

void owl_panel_get_gamma(struct owl_panel *panel, int *gamma_r_val,
				int *gamma_g_val, int *gamma_b_val)
{
	*gamma_r_val = panel->desc.gamma_r_val;
	*gamma_g_val = panel->desc.gamma_g_val;
	*gamma_b_val = panel->desc.gamma_b_val;
}
EXPORT_SYMBOL(owl_panel_get_gamma);

void owl_panel_regs_dump(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (ctrl && ctrl->ops && ctrl->ops->regs_dump)
		ctrl->ops->regs_dump(ctrl);
}
EXPORT_SYMBOL(owl_panel_regs_dump);

/*=========================================================
 *		owl panel class
 *=======================================================*/

static void panel_dev_release(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	kfree(dev);
}

static ssize_t panel_name_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", panel->desc.name);
}

static ssize_t panel_size_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return sprintf(buf, "%ux%umm\n", panel->desc.width_mm,
		       panel->desc.height_mm);
}

static ssize_t panel_default_mode_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode = &panel->default_mode;

	if (mode->refresh == 61)	/* DVI, TODO */
		return snprintf(buf, PAGE_SIZE, "DVI\n");
	else
		return VIDEOMODE_TO_STR(mode, buf);
}

static ssize_t panel_default_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode = &panel->default_mode;

	if (strcmp(buf, "DVI\n") == 0) {
		/* DVI, TODO */
		mode->xres = 1280;
		mode->yres = 720;
		mode->refresh = 61;
	} else {
		STR_TO_VIDEOMODE(mode, buf);
	}

	/* clear 'need_edid' flag once someone write '/default_mode' file */
	panel->desc.need_edid = false;

	return count;
}

static ssize_t panel_mode_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode = &panel->mode;

	if (mode->refresh == 61)	/* DVI, TODO */
		return snprintf(buf, PAGE_SIZE, "DVI\n");
	else
		return VIDEOMODE_TO_STR(mode, buf);
}

static ssize_t panel_mode_list_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int i, ret = 0;

	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode;

	for (i = 0; i < panel->n_modes; i++) {
		mode = &panel->mode_list[i];

		if (mode->refresh == 61)	/* DVI, TODO */
			ret += snprintf(&buf[ret], PAGE_SIZE, "DVI\n");
		else
			ret += VIDEOMODE_TO_STR(mode, &buf[ret]);
	}

	return ret;
}

static ssize_t panel_prelines_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d, %dus\n",
			panel->desc.preline_num,
			panel->desc.preline_time);
}

static ssize_t panel_vb_time_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%dus\n",
			panel->desc.vb_time);
}

static ssize_t panel_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	pr_info("0x%x\n", owl_panel_status_get());

	return snprintf(buf, PAGE_SIZE, "%d\n",
			panel->state == OWL_DSS_STATE_ON);
}

static ssize_t panel_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (!!val)
		owl_panel_enable(panel);
	else
		owl_panel_disable(panel);

	return count;
}

static ssize_t panel_is_primary_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", panel->desc.is_primary);
}

static ssize_t panel_need_edid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", panel->desc.need_edid);
}

static ssize_t panel_need_edid_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	panel->desc.need_edid = !!val;

	return count;
}

static ssize_t panel_hpd_enable_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			owl_panel_hpd_is_enabled(panel));
}

static ssize_t panel_hpd_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	owl_panel_hpd_enable(panel, !!val);

	return count;
}

static ssize_t panel_connected_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			owl_panel_hpd_is_connected(panel));
}

static ssize_t panel_overscan_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d\n",
			panel->desc.overscan_left,
			panel->desc.overscan_top,
			panel->desc.overscan_right,
			panel->desc.overscan_bottom);
}

static ssize_t panel_overscan_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int disp_width = 0, disp_height = 0;
	int left, top, right, bottom;

	struct owl_panel *panel = dev_get_drvdata(dev);
	owl_panel_get_resolution(panel, &disp_width, &disp_height);

	sscanf(buf, "%d,%d,%d,%d\n", &left, &top, &right, &bottom);

	if (left < 0 || left > disp_width)
		left = 0;

	if (top < 0 || top > disp_height)
		top = 0;

	if (right < 0 || right > disp_width)
		right = 0;

	if (bottom < 0 || bottom > disp_height)
		bottom = 0;

	if (left + right > disp_width) {
		left = 0;
		right = 0;
	}

	if (top + bottom > disp_height) {
		top = 0;
		bottom = 0;
	}

	panel->desc.overscan_left = left;
	panel->desc.overscan_top = top;
	panel->desc.overscan_right = right;
	panel->desc.overscan_bottom = bottom;

	return count;
}

static ssize_t panel_brightness_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	int brightness;

	brightness = panel->desc.brightness - panel->desc.min_brightness;
	return snprintf(buf, PAGE_SIZE, "%d\n", brightness);
}

static ssize_t panel_brightness_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	val += panel->desc.min_brightness;
	panel->desc.brightness = val;

	return count;
}

static ssize_t panel_max_brightness_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	int max_brightness;

	struct owl_panel *panel = dev_get_drvdata(dev);

	max_brightness = panel->desc.max_brightness - panel->desc.min_brightness;

	return snprintf(buf, PAGE_SIZE, "%d\n", max_brightness);
}

static ssize_t panel_max_brightness_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	val += panel->desc.min_brightness;
	panel->desc.max_brightness = val;

	return count;
}


static ssize_t panel_contrast_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", panel->desc.contrast);
}

static ssize_t panel_contrast_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	panel->desc.contrast = val;

	return count;
}

static ssize_t panel_saturation_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", panel->desc.saturation);
}

static ssize_t panel_saturation_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	panel->desc.saturation = val;

	return count;
}

static ssize_t panel_3d_mode_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	enum owl_3d_mode mode = owl_panel_3d_mode_get(panel);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			owl_dss_3d_mode_to_string(mode));
}

static ssize_t panel_3d_mode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	owl_panel_3d_mode_set(panel, owl_dss_string_to_3d_mode(buf));

	return count;
}

static ssize_t panel_3d_modes_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	int mode = owl_panel_3d_modes_get(panel);
	int ret = 0;

	if ((mode & OWL_3D_MODE_2D) != 0)
		ret += snprintf(&buf[ret], PAGE_SIZE, "%s\n",
				owl_dss_3d_mode_to_string(OWL_3D_MODE_2D));

	if ((mode & OWL_3D_MODE_LR_HALF) != 0)
		ret += snprintf(&buf[ret], PAGE_SIZE, "%s\n",
				owl_dss_3d_mode_to_string(OWL_3D_MODE_LR_HALF));

	if ((mode & OWL_3D_MODE_TB_HALF) != 0)
		ret += snprintf(&buf[ret], PAGE_SIZE, "%s\n",
				owl_dss_3d_mode_to_string(OWL_3D_MODE_TB_HALF));

	if ((mode & OWL_3D_MODE_FRAME) != 0)
		ret += snprintf(&buf[ret], PAGE_SIZE, "%s\n",
				owl_dss_3d_mode_to_string(OWL_3D_MODE_FRAME));

	return ret;
}

static ssize_t panel_regs_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	/* TODO */
	owl_panel_regs_dump(panel);

	return 0;
}

/*
 * These are the only attributes are present for all panel.
 */
static struct device_attribute panel_dev_attrs[] = {
	__ATTR(name, S_IRUGO, panel_name_show, NULL),
	__ATTR(size, S_IRUGO, panel_size_show, NULL),
	__ATTR(default_mode, S_IRUGO | S_IWUSR, panel_default_mode_show,
	       panel_default_mode_store),
	__ATTR(mode, S_IRUGO, panel_mode_show, NULL),
	__ATTR(mode_list, S_IRUGO, panel_mode_list_show, NULL),
	__ATTR(prelines, S_IRUGO, panel_prelines_show, NULL),
	__ATTR(vb_time, S_IRUGO, panel_vb_time_show, NULL),
	__ATTR(enable, S_IRUGO | S_IWUSR, panel_enable_show,
	       panel_enable_store),
	__ATTR(is_primary, S_IRUGO, panel_is_primary_show, NULL),
	__ATTR(need_edid, S_IRUGO | S_IWUSR, panel_need_edid_show,
	       panel_need_edid_store),
	__ATTR(hpd_enable, S_IRUGO | S_IWUSR, panel_hpd_enable_show,
	       panel_hpd_enable_store),
	__ATTR(connected, S_IRUGO, panel_connected_show, NULL),
	__ATTR(overscan, S_IRUGO | S_IWUSR, panel_overscan_show,
	       panel_overscan_store),
	__ATTR(3d_mode, S_IRUGO | S_IWUSR, panel_3d_mode_show,
	       panel_3d_mode_store),
	__ATTR(3d_modes, S_IRUGO, panel_3d_modes_show, NULL),
	__ATTR(brightness, S_IRUGO | S_IWUGO, panel_brightness_show,
	       panel_brightness_store),
	__ATTR(max_brightness, S_IRUGO | S_IWUGO, panel_max_brightness_show,
	       panel_max_brightness_store),
	__ATTR(contrast, S_IRUGO | S_IWUSR, panel_contrast_show,
	       panel_contrast_store),
	__ATTR(saturation, S_IRUGO | S_IWUSR, panel_saturation_show,
	       panel_saturation_store),
	__ATTR(regs, S_IRUGO, panel_regs_show, NULL),
	__ATTR_NULL,
};

static struct class owl_panel_class = {
	.name = "owl_panel",
	.dev_release = panel_dev_release,
	.dev_attrs = panel_dev_attrs,
};
