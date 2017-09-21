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
 *	2015/11/30: Created by Lipeng.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <video/owl_dss.h>

struct panel_glp_data {
	struct owl_dss_gpio	power_gpio;
	enum owl_dss_state	state;

	struct platform_device	*pdev;
	/* others can be added here */
};

static void __panel_glp_power_on(struct panel_glp_data *glp)
{
	owl_dss_gpio_active(&glp->power_gpio);
}

static void __panel_glp_power_off(struct panel_glp_data *glp)
{
	owl_dss_gpio_deactive(&glp->power_gpio);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_glp_power_on(struct owl_panel *panel)
{
	struct panel_glp_data *glp = panel->pdata;

	dev_info(&glp->pdev->dev, "%s\n", __func__);

	if (glp->state != OWL_DSS_STATE_ON) {
		__panel_glp_power_on(glp);
		glp->state = OWL_DSS_STATE_ON;
	}

	return 0;
}

static int panel_glp_power_off(struct owl_panel *panel)
{
	struct panel_glp_data *glp = panel->pdata;

	dev_info(&glp->pdev->dev, "%s\n", __func__);

	if (glp->state != OWL_DSS_STATE_OFF) {
		glp->state = OWL_DSS_STATE_OFF;
		__panel_glp_power_off(glp);
	}

	return 0;
}

static struct owl_panel_ops panel_glp_panel_ops = {
	.power_on = panel_glp_power_on,
	.power_off = panel_glp_power_off,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_glp_of_match[] = {
	{
		.compatible	= "actions,panel-glp",
	},
	{},
};

static int panel_glp_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_glp_data *glp;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_glp_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	glp = devm_kzalloc(dev, sizeof(*glp), GFP_KERNEL);
	if (!glp) {
		dev_err(dev, "alloc glp failed\n");
		return -ENOMEM;
	}
	glp->pdev = pdev;

	panel = owl_panel_alloc("lcd", OWL_DISPLAY_TYPE_LCD);
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
	panel->desc.ops = &panel_glp_panel_ops;

	ret = owl_dss_gpio_parse(of_node, "power-gpio", &glp->power_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, glp->power_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power_gpio failed: %d\n", ret);
		return ret;
	}
	panel->pdata = glp;

	glp->state = OWL_DSS_STATE_OFF;

	return 0;
}

static int panel_glp_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	panel_glp_power_off(panel);

	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_glp_driver = {
	.probe			= panel_glp_probe,
	.remove			= panel_glp_remove,
	.driver = {
		.name		= "panel-glp",
		.owner		= THIS_MODULE,
		.of_match_table	= panel_glp_of_match,
	},
};

int __init panel_glp_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&panel_glp_driver);
	if (ret)
		pr_err("Failed to register platform driver\n");

	return ret;
}

void __exit panel_glp_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_glp_driver);
}

module_init(panel_glp_init);
module_exit(panel_glp_exit);
MODULE_LICENSE("GPL");
