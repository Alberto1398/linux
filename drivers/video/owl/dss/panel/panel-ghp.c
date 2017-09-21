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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <video/owl_dss.h>

struct panel_ghp_data {
	enum owl_dss_state	state;

	struct platform_device	*pdev;

	/* others can be added here */
};

static struct owl_panel_ops owl_panel_ghp_ops = {
};

/*==============================================================
			platform device ops
 *============================================================*/

static struct of_device_id panel_ghp_of_match[] = {
	{
		.compatible	= "actions,panel-ghp",
	},
	{},
};

static int panel_ghp_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_ghp_data *ghp;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_ghp_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	ghp = devm_kzalloc(dev, sizeof(*ghp), GFP_KERNEL);
	if (!ghp) {
		dev_err(dev, "alloc ghp failed\n");
		return -ENOMEM;
	}
	ghp->pdev = pdev;

	panel = owl_panel_alloc("hdmi", OWL_DISPLAY_TYPE_HDMI);
	if (panel) {
		dev_set_drvdata(dev, panel);

		owl_panel_parse_panel_info(of_node, panel);

		if (owl_panel_register(dev, panel) < 0) {
			dev_err(dev, "%s, fail to regitser dss device\n",
				__func__);
			owl_panel_free(panel);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s, fail to alloc panel\n", __func__);
		return -ENOMEM;
	}
	panel->desc.ops = &owl_panel_ghp_ops;

	panel->pdata = ghp;

	return 0;
}

static int panel_ghp_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_ghp_driver = {
	.probe			= panel_ghp_probe,
	.remove			= panel_ghp_remove,
	.driver = {
		.name		= "panel-ghp",
		.owner		= THIS_MODULE,
		.of_match_table	= panel_ghp_of_match,
	},
};

int __init panel_ghp_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&panel_ghp_driver);
	if (ret)
		pr_err("Failed to register platform driver\n");

	return ret;
}

void __exit panel_ghp_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_ghp_driver);
}

module_init(panel_ghp_init);
module_exit(panel_ghp_exit);
MODULE_LICENSE("GPL");
