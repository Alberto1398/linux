/*
 * reset controller driver for Actions SOC
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/reset-controller.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

struct owl_reset_controller {
	struct reset_controller_dev rcdev;
	void __iomem *base;
	spinlock_t lock;
};

#define to_owl_reset(r) \
	container_of(r, struct owl_reset_controller, rcdev)

static int owl_clk_reset_assert(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	struct owl_reset_controller *reset = to_owl_reset(rcdev);
	void __iomem *reg;
	unsigned long flags = 0;
	unsigned int val;

	reg = reset->base + (id / 32) * 4;

	spin_lock_irqsave(&reset->lock, flags);

	val = readl(reg);
	val &= ~BIT(id % 32);
	writel(val, reg);

	spin_unlock_irqrestore(&reset->lock, flags);

	return 0;
}

static int owl_clk_reset_deassert(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	struct owl_reset_controller *reset = to_owl_reset(rcdev);
	void __iomem *reg;
	unsigned long flags = 0;
	unsigned int val;

	reg = reset->base + (id / 32) * 4;

	spin_lock_irqsave(&reset->lock, flags);

	val = readl(reg);
	val |= BIT(id % 32);
	writel(val, reg);

	spin_unlock_irqrestore(&reset->lock, flags);

	return 0;
}

static int owl_clk_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	rcdev->ops->assert(rcdev, id);
	udelay(1);
	rcdev->ops->deassert(rcdev, id);
	return 0;
}

static struct reset_control_ops owl_reset_ops = {
	.reset = owl_clk_reset,
	.assert = owl_clk_reset_assert,
	.deassert = owl_clk_reset_deassert,
};

static int owl_reset_probe(struct platform_device *pdev)
{
	struct owl_reset_controller *reset;
	struct resource *res;
	int ret;

	pr_info("[OWL] register reset controller\n");

	reset = devm_kzalloc(&pdev->dev, sizeof(*reset), GFP_KERNEL);
	if (!reset) {
		dev_err(&pdev->dev, "faild to allocate reset controller\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reset->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(reset->base))
		return PTR_ERR(reset->base);

	reset->rcdev.of_node = pdev->dev.of_node;
	reset->rcdev.owner = THIS_MODULE;
	reset->rcdev.ops = &owl_reset_ops;
	reset->rcdev.of_reset_n_cells = 1;
	reset->rcdev.nr_resets = resource_size(res) * 32;
	spin_lock_init(&reset->lock);

	ret = reset_controller_register(&reset->rcdev);
	if (ret) {
		dev_err(&pdev->dev, "faild to register reset controller\n");
		return ret;
	}

	return 0;
}

static struct of_device_id owl_reset_match[] = {
	{ .compatible = "actions,s900-reset", },
	{ .compatible = "actions,s700-reset", },
	{},
};

static struct platform_driver owl_reset_driver = {
	.probe = owl_reset_probe,
	.driver = {
		.name = "reset-owl",
		.of_match_table = owl_reset_match,
	},
};

static int __init owl_reset_init(void)
{
	return platform_driver_register(&owl_reset_driver);
}
arch_initcall(owl_reset_init);
