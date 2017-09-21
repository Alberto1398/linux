/*
 * drivers/pwm/pwm-owl.c
 *
 * Copyright (C) 2014 Actions Corporation
 * Author: lipeng <lipeng@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_device.h>
#include <linux/pwm.h>

#define NS_IN_HZ				(1000000000UL)

#define MAX_NUM_PWM				6

struct owl_pwm_channel {
	struct clk				*clk;

	/*
	 * PWM polarity, it will be filled while DTS init,
	 * in that case of_pwm_xlate_with_flags will parse
	 * polarity information from DTS and set polarity
	 * by calling owl_pwm_set_polarity.
	 * DO NOT change its value unless you know you must
	 * do it!!!!!
	 */
	enum pwm_polarity			polarity;

	unsigned int				required_period_ns;
	unsigned int				period_ns;
	unsigned int				duty_ns;

	struct device				dev;
	struct pinctrl				*pinctrl;

	bool valid;				/* TODO */
};

struct owl_pwm_chip_data {
	/*
	 * s500 & s700: 1, s900: 2
	 */
	int					chip_type;

	/*
	 * s500 & s900 & s700, 6
	 */
	int					pwm_num;

	const uint32_t				*reg_array;
};

struct owl_pwm_chip {
	struct pwm_chip				chip;

	struct platform_device			*pdev;
	void __iomem				*base;

	const struct owl_pwm_chip_data		*chip_data;

	struct owl_pwm_channel			channels[MAX_NUM_PWM];
};

static const struct owl_pwm_chip_data *g_chip_data;
static const struct owl_pwm_chip *pc_temp;

static inline struct owl_pwm_chip *to_owl_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct owl_pwm_chip, chip);
}

static inline int get_pwm_clk_info(struct pwm_chip *chip, int hwpwm,
				   struct clk **clk)
{
	const char *clk_name;
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;

	switch (hwpwm) {
	case 0:
		clk_name = "pwm0";
		break;

	case 1:
		clk_name = "pwm1";
		break;

	case 2:
		clk_name = "pwm2";
		break;

	case 3:
		clk_name = "pwm3";
		break;

	case 4:
		clk_name = "pwm4";
		break;

	case 5:
		clk_name = "pwm5";
		break;

	default:
		dev_warn(dev, "bad pwm id %d\n", hwpwm);
		return -EINVAL;
	}

	*clk = clk_get(NULL, clk_name);
	return 0;
}

/*============================================================================
				PWM ops
 *==========================================================================*/
static void owl_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm);

static int owl_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int ret = 0;
	int hwpwm;
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;
	struct owl_pwm_channel *channel;

	dev_dbg(dev, "%s\n", __func__);

	hwpwm = pwm->hwpwm;
	channel = &pc->channels[hwpwm];

	if (!channel->valid)
		return -EINVAL;

	dev_dbg(dev, "%s REQ ID = %d\n", __func__, hwpwm);

	ret = get_pwm_clk_info(chip, hwpwm, &channel->clk);
	if (ret || (IS_ERR(channel->clk))) {
		dev_err(&channel->dev, "can't get fck\n");
		return -EINVAL;
	}

	channel->pinctrl = pinctrl_get_select_default(&channel->dev);
	if (IS_ERR(channel->pinctrl))
		return PTR_ERR(channel->pinctrl);

	dev_dbg(dev, "%s END\n", __func__);

	return 0;
}

static void owl_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int hwpwm;
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;
	struct owl_pwm_channel *channel;

	dev_dbg(dev, "%s\n", __func__);

	hwpwm = pwm->hwpwm;
	channel = &pc->channels[hwpwm];

	if (test_bit(PWMF_ENABLED, &pwm->flags))
		owl_pwm_disable(chip, pwm);

	clk_put(channel->clk);

	pinctrl_put(channel->pinctrl);

	dev_dbg(dev, "%s END\n", __func__);

	return;
}

static int owl_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	unsigned int tmp = 0, val = 0;
	s32 rate = 0;
	int hwpwm;
	u32 pwm_ctl_reg;
	struct clk *parent_clk;
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;
	struct owl_pwm_channel *channel;

	/* We currently avoid using 64bit arithmetic by using the
	 * fact that anything faster than 1Hz is easily representable
	 * by 32bits. */

	dev_dbg(dev, "%s\n", __func__);

	if (period_ns > NS_IN_HZ || duty_ns > NS_IN_HZ)
		return -ERANGE;

	hwpwm = pwm->hwpwm;

	channel = &pc->channels[hwpwm];

	if (period_ns != channel->required_period_ns) {
		channel->required_period_ns = period_ns;

		rate = NS_IN_HZ / period_ns;
		if (rate < 512) {
			parent_clk = clk_get(NULL, "losc");	/* TODO */
		} else if (rate <= 375000) {
			parent_clk = clk_get(NULL, "hosc");
		} else {
			rate = 375000;
			parent_clk = clk_get(NULL, "hosc");
			dev_dbg(dev, "pwm freq will be 375kHZ at most!\n");
		}

		/* round up or down */
		clk_set_parent(channel->clk, parent_clk);
		clk_set_rate(channel->clk, rate * 64);
		channel->period_ns = ((u64)NS_IN_HZ)* 64 / clk_get_rate(channel->clk);
	}

	/* round up or down */
	val = DIV_ROUND_UP(duty_ns*64, period_ns);
	channel->duty_ns = duty_ns;

	dev_dbg(dev, "pwm:duty = %d\n", channel->duty_ns);
	dev_dbg(dev, "pwm:period_ns = %d\n", channel->period_ns);

	pwm_ctl_reg = pc->chip_data->reg_array[hwpwm];
	tmp = readl(pc->base + pwm_ctl_reg);

	/*
	 * till now, val = duty * 64 / period,
	 * duty / preriod (T_active) = val / 64;
	 */
	if (pc->chip_data->chip_type == 2) {
		/*
		 * s900, TODO, DIV is variable, FIXME
		 * T_active = (DUTY + 1) / DIV
		 * DIV = 64, DUTY = (val - 1)
		 */
		val = (val > 64) ? 64 : val;
		//val = (val == 0) ? 0 : val - 1; /* to reg value */

		tmp &= ~(0x1FF80000);	/* DUTY, 28:19 */
		tmp &= ~(0x7FE00);	/* DIV, 18:9 */
		tmp |= (val << 19) | (64U << 9);
	} else if (pc->chip_data->chip_type == 1) {
		/*
		 * T_active = (DUTY + 1) / DIV
		 * DIV = 64, DUTY = (val - 1)
		 */
		val = (val > 64) ? 64 : val;
		//val = (val == 0) ? 0 : val - 1; /* to reg value */

		tmp &= ~((1U << 20) - 1U);
		tmp |= (val << 10) | 64U;
	} else {
		val = (val) ? (val - 1) : 0;
		tmp &= (~0x3f);
		tmp |= (val & 0x3f);
	}

	writel(tmp, pc->base + pwm_ctl_reg);

	dev_dbg(dev, "%s END\n", __func__);

	return 0;
}
int owl_pwm_get_duty_cfg(struct pwm_device *pwm, uint *comparator_steps, uint *counter_steps)
{
	uint hwpwm;
	uint32_t pwm_ctl_reg, reg_val;

	if (pwm == NULL || comparator_steps == NULL || counter_steps == NULL)
		return -EINVAL;

	hwpwm = pwm->hwpwm;
	pwm_ctl_reg = g_chip_data->reg_array[hwpwm];
	printk(KERN_WARNING"pwm_ctl_reg_array[1]= 0x%x\n", g_chip_data->reg_array[1]);
	printk(KERN_WARNING"pwm_ctl_reg_array[2]= 0x%x\n", g_chip_data->reg_array[2]);
	printk(KERN_WARNING"pc_temp->base= 0x%p\n", pc_temp->base);
	reg_val = readl(pc_temp->base + pwm_ctl_reg);
	printk(KERN_WARNING"reg_val= 0x%d\n", reg_val);
	if(g_chip_data->chip_type == 1) {
		*comparator_steps = ((reg_val >> 10) & ((1U << 10) -1U)) + 1U;
		*counter_steps = (reg_val & ((1U << 10) -1U)) + 1U;
	} else {
		*comparator_steps = (reg_val & 0x3f) + 1U;
		*counter_steps = 64;
	}
	return 0;
}
EXPORT_SYMBOL(owl_pwm_get_duty_cfg);
/* the real polarity set interface, just write registers */
static void __owl_pwm_set_polarity(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   enum pwm_polarity polarity)
{
	int hwpwm;
	u32 pwm_ctl_reg, val;
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;

	dev_dbg(dev, "%s\n", __func__);

	hwpwm = pwm->hwpwm;

	pwm_ctl_reg = pc->chip_data->reg_array[hwpwm];
	val = readl(pc->base + pwm_ctl_reg);

	if (pc->chip_data->chip_type == 1) {
		if (polarity == PWM_POLARITY_INVERSED) {
			dev_dbg(dev, "pwm inverse\n");
			val &= ~(1U << 20);
			writel(val, pc->base + pwm_ctl_reg);
		} else {
			dev_dbg(dev, "pwm not inverse\n");
			/* Duty cycle defines HIGH period of PWM */
			val |= (1U << 20);
			writel(val, pc->base + pwm_ctl_reg);
		}
	} else {
		if (polarity == PWM_POLARITY_INVERSED) {
			dev_dbg(dev, "pwm inverse\n");
			val &= ~(1U << 8);
			writel(val, pc->base + pwm_ctl_reg);
		} else {
			dev_dbg(dev, "pwm not inverse\n");
			/* Duty cycle defines HIGH period of PWM */
			val |= (1U << 8);
			writel(val, pc->base + pwm_ctl_reg);
		}
	}
	dev_dbg(dev, "%s END\n", __func__);
}

/*
 * it will be called by DTS core, we will save polarity info,
 * so DO NOT call owl_pwm_set_polarity or pwm_set_polarity
 * unless you really need change the polarity.
 */
static int owl_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				enum pwm_polarity polarity)
{
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;
	struct owl_pwm_channel *channel;

	dev_dbg(dev, "%s\n", __func__);

	channel = &pc->channels[pwm->hwpwm];

	channel->polarity = polarity;

	__owl_pwm_set_polarity(chip, pwm, polarity);

	return 0;
}

static int owl_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int ret = 0;
	int hwpwm;
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;
	struct owl_pwm_channel *channel;

	dev_dbg(dev, "%s\n", __func__);

	hwpwm = pwm->hwpwm;
	channel = &pc->channels[pwm->hwpwm];

	/* restore the polarity, it may be modified while disable */
	__owl_pwm_set_polarity(chip, pwm, channel->polarity);

	ret = clk_prepare_enable(pc->channels[hwpwm].clk);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "%s END\n", __func__);

	return 0;
}

static void owl_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int hwpwm;
	enum pwm_polarity polarity;
	struct owl_pwm_chip *pc = to_owl_pwm_chip(chip);
	struct device *dev = &pc->pdev->dev;
	struct owl_pwm_channel *channel;

	dev_dbg(dev, "%s\n", __func__);

	hwpwm = pwm->hwpwm;
	channel = &pc->channels[pwm->hwpwm];

	/* reverse PWM polarity, needed ?? */
	if (channel->polarity == PWM_POLARITY_NORMAL)
		polarity = PWM_POLARITY_INVERSED;
	else
		polarity = PWM_POLARITY_NORMAL;
	__owl_pwm_set_polarity(chip, pwm, polarity);

	clk_disable_unprepare(pc->channels[hwpwm].clk);

	dev_dbg(dev, "%s END\n", __func__);

	return;
}

static const struct pwm_ops owl_pwm_ops = {
	.request = owl_pwm_request,
	.free = owl_pwm_free,
	.config = owl_pwm_config,
	.set_polarity = owl_pwm_set_polarity,
	.enable = owl_pwm_enable,
	.disable = owl_pwm_disable,
	.owner = THIS_MODULE,
};

/*============================================================================
				platform driver
 *==========================================================================*/

/* PWM control regitser offset */

static const uint32_t pwm_ctl_reg_array_s700[] = {
	0x50, 0x54, 0x58, 0x5c, 0x78, 0x7c,
};

static const uint32_t pwm_ctl_reg_array_s900[] = {
	0x50, 0x54, 0x58, 0x5c, 0x520, 0x524,
};

static struct owl_pwm_chip_data pwm_chip_data[] = {
	{ .chip_type = 1, .pwm_num = 6, .reg_array = pwm_ctl_reg_array_s700 }, /* s500 & s700 */
	{ .chip_type = 2, .pwm_num = 6, .reg_array = pwm_ctl_reg_array_s900 }, /* s900 */
};

static struct of_device_id owl_pwm_of_match[] = {
	{ .compatible = "actions,s500-pwm", .data = &pwm_chip_data[0] },
	{ .compatible = "actions,s700-pwm", .data = &pwm_chip_data[0] },
	{ .compatible = "actions,s900-pwm", .data = &pwm_chip_data[1] },
	{ }
};

static int __pwm_channels_init(struct platform_device *pdev)
{
	int hwpwm;
	struct device_node *child;
	struct owl_pwm_chip *pc = platform_get_drvdata(pdev);
	struct owl_pwm_channel *channel;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	for_each_child_of_node(pdev->dev.of_node, child) {
		if (of_property_read_u32(child, "id", &hwpwm))
			return -EINVAL;

		dev_dbg(&pdev->dev, "%s, child id = %d\n", __func__, hwpwm);

		if (hwpwm >= pc->chip_data->pwm_num)
			return -EINVAL;

		channel = &pc->channels[hwpwm];

		device_initialize(&channel->dev);
		dev_set_name(&channel->dev, "owl-pwm%d", hwpwm);
		channel->dev.of_node = child;
		channel->valid = true;
	}

	dev_dbg(&pdev->dev, "%s OK\n", __func__);
	return 0;
}

static int owl_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;

	struct resource *res;

	struct owl_pwm_chip *pc;

	int ret;

	dev_info(dev, "%s\n", __func__);

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	pc->pdev = pdev;

	of_id = of_match_device(owl_pwm_of_match, dev);
	if (of_id)
		pc->chip_data = of_id->data;
	else
		return -EINVAL;

	platform_set_drvdata(pdev, pc);
	g_chip_data = pc->chip_data;
	pc_temp = pc;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "can't get IORESOURCE_MEM\n");
		return -EINVAL;
	}

	pc->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!pc->base) {
		dev_err(dev, "can't ioremap\n");
		return -ENOMEM;
	}
	dev_info(dev, "pc->base%p\n", pc->base);

	pc->chip.dev = dev;
	pc->chip.ops = &owl_pwm_ops;
	pc->chip.of_xlate = of_pwm_xlate_with_flags;
	pc->chip.of_pwm_n_cells = 3;
	pc->chip.base = -1;
	pc->chip.npwm = pc->chip_data->pwm_num;

	if (__pwm_channels_init(pdev))
		return -EINVAL;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int owl_pwm_remove(struct platform_device *pdev)
{
	struct owl_pwm_chip *pc = platform_get_drvdata(pdev);

	if (WARN_ON(!pc))
		return -ENODEV;

	return pwmchip_remove(&pc->chip);
}

static struct platform_driver owl_pwm_driver = {
	.driver = {
		.name = "pwm-owl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(owl_pwm_of_match),
	},
	.probe = owl_pwm_probe,
	.remove = owl_pwm_remove,
};

static int __init owl_pwm_init(void)
{
	return platform_driver_register(&owl_pwm_driver);
}

static void __exit owl_pwm_exit(void)
{
	platform_driver_unregister(&owl_pwm_driver);
}

arch_initcall(owl_pwm_init);
module_exit(owl_pwm_exit);

MODULE_AUTHOR("lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL PWM Driver");
MODULE_LICENSE("GPL v2");
