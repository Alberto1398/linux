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

#include <video/owl_dss.h>

static struct owl_display_ctrl dummy_dispc;

static struct of_device_id owl_dummy_dispc_of_match[] = {
	{
		.compatible	= "actions,s900-dummy-dispc",
	},
	{},
};

static int owl_dummy_dispc_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(owl_dummy_dispc_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	dummy_dispc.type = OWL_DISPLAY_TYPE_DUMMY;
	ret = owl_ctrl_register(&dummy_dispc);
	if (ret < 0) {
		dev_err(dev, "register dummy dispc failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int owl_dummy_dispc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver owl_dummy_dispc_driver = {
	.probe          = owl_dummy_dispc_probe,
	.remove         = owl_dummy_dispc_remove,
	.driver         = {
		.name   = "owl_dummy_dispc",
		.owner  = THIS_MODULE,
		.of_match_table	= owl_dummy_dispc_of_match,
	},
};

int __init owl_dummy_dispc_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&owl_dummy_dispc_driver);
	if (ret)
		pr_err("Failed to initialize dummy dispc driver\n");

	return ret;
}

void __exit owl_dummy_dispc_exit(void)
{
	platform_driver_unregister(&owl_dummy_dispc_driver);
}

module_init(owl_dummy_dispc_init);
module_exit(owl_dummy_dispc_exit);
MODULE_LICENSE("GPL");
