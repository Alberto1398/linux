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
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <video/owl_dss.h>

struct panel_gep_data {
	struct owl_dss_gpio	power_gpio;
	enum owl_dss_state	state;

	struct platform_device	*pdev;

	/* others can be added here */
};

static void __panel_gep_power_on(struct panel_gep_data *gep)
{
	owl_dss_gpio_active(&gep->power_gpio);
}

static void __panel_gep_power_off(struct panel_gep_data *gep)
{
	owl_dss_gpio_deactive(&gep->power_gpio);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_gep_power_on(struct owl_panel *panel)
{
	struct panel_gep_data *gep = panel->pdata;

	dev_info(&gep->pdev->dev, "%s\n", __func__);

	if (gep->state != OWL_DSS_STATE_ON) {
		__panel_gep_power_on(gep);
		gep->state = OWL_DSS_STATE_ON;
	}

	return 0;
}

static int panel_gep_power_off(struct owl_panel *panel)
{
	struct panel_gep_data *gep = panel->pdata;

	dev_info(&gep->pdev->dev, "%s\n", __func__);

	if (gep->state != OWL_DSS_STATE_OFF) {
		gep->state = OWL_DSS_STATE_OFF;
		__panel_gep_power_off(gep);
	}

	return 0;
}

static struct owl_panel_ops panel_gep_panel_ops = {
	.power_on = panel_gep_power_on,
	.power_off = panel_gep_power_off,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_gep_of_match[] = {
	{
		.compatible	= "actions,panel-gep",
	},
	{},
};

static int panel_gep_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_gep_data *gep;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_gep_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	gep = devm_kzalloc(dev, sizeof(*gep), GFP_KERNEL);
	if (!gep) {
		dev_err(dev, "alloc gep failed\n");
		return -ENOMEM;
	}
	gep->pdev = pdev;

	panel = owl_panel_alloc("edp", OWL_DISPLAY_TYPE_EDP);
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
	panel->desc.ops = &panel_gep_panel_ops;

	ret = owl_dss_gpio_parse(of_node, "power-gpio", &gep->power_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, gep->power_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power_gpio failed: %d\n", ret);
		return ret;
	}
	panel->pdata = gep;

	gep->state = OWL_DSS_STATE_OFF;

	return 0;
}

static int panel_gep_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	panel_gep_power_off(panel);

	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_gep_driver = {
	.probe			= panel_gep_probe,
	.remove			= panel_gep_remove,
	.driver = {
		.name		= "panel-gep",
		.owner		= THIS_MODULE,
		.of_match_table	= panel_gep_of_match,
	},
};

int __init panel_gep_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&panel_gep_driver);
	if (ret)
		pr_err("Failed to register platform driver\n");

	return ret;
}

void __exit panel_gep_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_gep_driver);
}

module_init(panel_gep_init);
module_exit(panel_gep_exit);
MODULE_LICENSE("GPL");
