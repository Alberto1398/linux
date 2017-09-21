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
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <video/owl_dss.h>

static struct of_device_id panel_gdp_of_match[] = {
	{
		.compatible	= "actions,panel-gdp",
	},
	{},
};

static int panel_gdp_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_gdp_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	panel = owl_panel_alloc("dummy", OWL_DISPLAY_TYPE_DUMMY);
	if (panel) {
		owl_panel_parse_panel_info(of_node, panel);

		if (owl_panel_register(dev, panel) < 0) {
			dev_err(dev, "%s, fail to regitser panel\n",
				__func__);
			owl_panel_free(panel);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s, fail to alloc panel\n", __func__);
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, panel);
	return 0;
}

static int panel_gdp_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_gdp_driver = {
	.probe          = panel_gdp_probe,
	.remove         = panel_gdp_remove,
	.driver         = {
		.name   = "panel-gdp",
		.owner  = THIS_MODULE,
		.of_match_table	= panel_gdp_of_match,
	},
};

int __init panel_gdp_init(void)
{
	int r = 0;

	pr_info("%s\n", __func__);

	r = platform_driver_register(&panel_gdp_driver);
	if (r)
		pr_err("Failed to initialize gdp driver\n");

	return r;
}

void __exit panel_gdp_exit(void)
{
	platform_driver_unregister(&panel_gdp_driver);
}

module_init(panel_gdp_init);
module_exit(panel_gdp_exit);
MODULE_LICENSE("GPL");
