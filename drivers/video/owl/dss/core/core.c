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
#define pr_fmt(fmt) "owl_dss_core: %s, " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_gpio.h>

#include <video/owl_dss.h>

int owl_dss_get_color_bpp(enum owl_color_mode color)
{
	int bpp;

	switch (color) {
	case OWL_DSS_COLOR_BGR565:
	case OWL_DSS_COLOR_RGB565:
	case OWL_DSS_COLOR_BGRA1555:
	case OWL_DSS_COLOR_RGBA1555:
	case OWL_DSS_COLOR_ABGR1555:
	case OWL_DSS_COLOR_ARGB1555:
	case OWL_DSS_COLOR_BGRX1555:
	case OWL_DSS_COLOR_RGBX1555:
	case OWL_DSS_COLOR_XBGR1555:
	case OWL_DSS_COLOR_XRGB1555:
		bpp = 16;
		break;
	default:
		bpp = 32;
		break;
	}

	return bpp;
}
EXPORT_SYMBOL(owl_dss_get_color_bpp);

bool owl_dss_color_is_rgb(enum owl_color_mode color)
{
	switch (color) {
	case OWL_DSS_COLOR_NV12:
	case OWL_DSS_COLOR_NV21:
	case OWL_DSS_COLOR_YVU420:
	case OWL_DSS_COLOR_YUV420:
		return false;

	default:
		return true;
	}
}
EXPORT_SYMBOL(owl_dss_color_is_rgb);

char *owl_dss_3d_mode_to_string(enum owl_3d_mode mode)
{
	switch (mode) {
	case OWL_3D_MODE_LR_HALF:
		return "LR";

	case OWL_3D_MODE_TB_HALF:
		return "TB";

	case OWL_3D_MODE_FRAME:
		return "FRAME";

	case OWL_3D_MODE_2D:
	default:
		return "2D";
	}
}
EXPORT_SYMBOL(owl_dss_3d_mode_to_string);

enum owl_3d_mode owl_dss_string_to_3d_mode(char *mode)
{
	if (strncmp(mode, "LR", 2) == 0)
		return OWL_3D_MODE_LR_HALF;
	else if (strncmp(mode, "TB", 2) == 0)
		return OWL_3D_MODE_TB_HALF;
	else if (strncmp(mode, "FRAME", 5) == 0)
		return OWL_3D_MODE_FRAME;
	else
		return OWL_3D_MODE_2D;
}
EXPORT_SYMBOL(owl_dss_string_to_3d_mode);

int owl_dss_gpio_parse(struct device_node *of_node, const char *propname,
		       struct owl_dss_gpio *gpio)
{
	enum of_gpio_flags flags;
	int gpio_num;

	gpio_num = of_get_named_gpio_flags(of_node, propname, 0, &flags);
	if (gpio_num >= 0)
		gpio->gpio = gpio_num;
	else
		gpio->gpio = -1;

	gpio->active_low = flags & OF_GPIO_ACTIVE_LOW;

	pr_debug("gpio %d, active low? %d\n", gpio->gpio, gpio->active_low);

	return (gpio->gpio == -1 ? -EINVAL : 0);
}
EXPORT_SYMBOL(owl_dss_gpio_parse);

static int __init owl_dss_init(void)
{
	pr_info("start\n");

	/*
	 * core init
	 */
	owl_ctrl_init();
	owl_panel_init();

	return 0;
}

static void __exit owl_dss_exit(void)
{
}

module_init(owl_dss_init);
module_exit(owl_dss_exit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL Display Subsystem Core");
MODULE_LICENSE("GPL v2");
