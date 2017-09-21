/*
 * Actions OWL SoC Power domain controller driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <wurui@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/pm_clock.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include "pd.h"

#define OWL_POWER_DOMAIN_TIMEOUT	5000	/* us */

static void __iomem *sps_base;
static DEFINE_SPINLOCK(sps_lock);

static struct owl_pm_domain *genpd_to_owl_pm_domain(
		struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct owl_pm_domain, domain);
}

static int owl_pd_powergate(struct owl_pm_domain *opd, bool power_on)
{
	unsigned int ctrl, is_on;
	int timeout = OWL_POWER_DOMAIN_TIMEOUT;
	unsigned long flags;

	pr_debug("%s: before powergate(on:%d) SPS_CTL 0x%x, SPS_ACK 0x%x\n",
		__func__, power_on,
		readl(sps_base + opd->ctrl.offset),
		readl(sps_base + opd->ack.offset));

	spin_lock_irqsave(&sps_lock, flags);

	if (opd->owl_pm_callback) {
		if (power_on)
			/* disable isolation before powergate on */
			opd->owl_pm_callback(opd, false, PM_CB_TYPE_ISO);
		else
			/* disable dev before powergate off */
			opd->owl_pm_callback(opd, false, PM_CB_TYPE_POWER);
	}

	ctrl = readl(sps_base + opd->ctrl.offset);
	if (power_on)
		ctrl |= BIT(opd->ctrl.bit_idx);
	else
		ctrl &= ~BIT(opd->ctrl.bit_idx);
	writel(ctrl, sps_base + opd->ctrl.offset);

	spin_unlock_irqrestore(&sps_lock, flags);

	while (timeout) {
		is_on = readl(sps_base + opd->ack.offset)
				& BIT(opd->ack.bit_idx);
		if ((power_on && is_on) || (!power_on && !is_on))
			break;
		timeout--;
		udelay(1);
	}

	if (!timeout) {
		pr_err("%s: wait power on %s ack timeout\n",
			__func__, opd->domain.name);
		/* restore isolate status for gpu */
		if (power_on && opd->owl_pm_callback)
			opd->owl_pm_callback(opd, true, PM_CB_TYPE_ISO);
		 return -ETIMEDOUT;
	}

	if (opd->owl_pm_callback) {
		if (power_on)
			/* enable dev after powergate on */
			opd->owl_pm_callback(opd, true, PM_CB_TYPE_POWER);
		else
			/* enable isolation after powergate off */
			opd->owl_pm_callback(opd, true, PM_CB_TYPE_ISO);
	}

	if (power_on && opd->owl_pm_callback)
		opd->owl_pm_callback(opd, true, PM_CB_TYPE_POWER);

	pr_debug("%s: after powergate(on:%d) SPS_CTL0x %x, SPS_ACK 0x%x\n",
		__func__, power_on,
		readl(sps_base + opd->ctrl.offset),
		readl(sps_base + opd->ack.offset));

	return 0;
}

static int owl_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct owl_pm_domain *opd;
	struct owl_pm_device_entry *de;
	struct owl_pm_clock_entry *ce;
	struct owl_pm_reset_entry *re;
	int ret;

	pr_debug("%s %d: domain %s, power_on %d\n",
		__func__, __LINE__, domain->name, power_on);

	opd = genpd_to_owl_pm_domain(domain);

	/* no clk, set power domain will fail */
	if (list_empty(&opd->dev_list)) {
		pr_warn("%s: no devices in %s power domain\n",
			__func__, opd->domain.name);
	}

	/* assert reset for the devices in power domain */
	list_for_each_entry(de, &opd->dev_list, node) {
		list_for_each_entry(re, &de->rst_list, node) {
			reset_control_assert(re->rst);
		}
	}

	/* real powergate operation */
	ret = owl_pd_powergate(opd, power_on);

	/* deassert reset for the devices in power domain */
	list_for_each_entry(de, &opd->dev_list, node) {
		/* enable clock for reset */
		list_for_each_entry(ce, &de->clk_list, node) {
			clk_prepare_enable(ce->clk);
		}

		/* deassert reset */
		list_for_each_entry(re, &de->rst_list, node) {
			reset_control_deassert(re->rst);
		}

		if (opd->owl_pm_callback)
			opd->owl_pm_callback(opd, false, PM_CB_TYPE_DEV);
		/* disable clock after reset */
		list_for_each_entry(ce, &de->clk_list, node) {
			clk_disable_unprepare(ce->clk);
		}
	}

	return ret;
}


void owl_pd_attach_dev(struct device *dev)
{
	struct owl_pm_domain *opd;
	struct owl_pm_device_entry *de;
	struct owl_pm_clock_entry *ce;
	struct owl_pm_reset_entry *re;
	struct clk *clk;
	struct reset_control *rst;
	int i;

	pr_debug("%s: attach dev %s\n", __func__, dev_name(dev));

	opd = genpd_to_owl_pm_domain(pd_to_genpd(dev->pm_domain));

	de = kzalloc(sizeof(*de), GFP_KERNEL);
	if (!de) {
		pr_err("%s: Not enough memory for device entry\n", __func__);
		return;
	}

	/* add device to power domain */
	de->dev = dev;
	INIT_LIST_HEAD(&de->clk_list);
	INIT_LIST_HEAD(&de->rst_list);
	mutex_lock(&opd->dev_lock);
	list_add_tail(&de->node, &opd->dev_list);
	mutex_unlock(&opd->dev_lock);

	/* add device clocks of device to power domain */
	i = 0;
	while ((clk = of_clk_get(dev->of_node, i++)) && !IS_ERR(clk)) {
		ce = kzalloc(sizeof(*ce), GFP_KERNEL);
		if (!ce) {
			dev_err(dev, "Not enough memory for clock entry.\n");
			return;
		}

		ce->clk = clk;
		mutex_lock(&opd->dev_lock);
		list_add_tail(&ce->node, &de->clk_list);
		mutex_unlock(&opd->dev_lock);
	}

	/* add reset control of device to power domain */
	i = 0;
	while ((rst = of_reset_control_get_by_index(dev, i++))
				&& !IS_ERR(rst)) {
		re = kzalloc(sizeof(*re), GFP_KERNEL);
		if (!re) {
			dev_err(dev, "Not enough memory for reset entry.\n");
			return;
		}

		re->rst = rst;
		mutex_lock(&opd->dev_lock);
		list_add_tail(&re->node, &de->rst_list);
		mutex_unlock(&opd->dev_lock);
	}
}

void owl_pd_detach_dev(struct device *dev)
{
	struct owl_pm_domain *opd;
	struct owl_pm_device_entry *de, *_de;
	struct owl_pm_clock_entry *ce, *_ce;
	struct owl_pm_reset_entry *re, *_re;

	pr_debug("%s: detach dev %s\n", __func__, dev_name(dev));

	opd = genpd_to_owl_pm_domain(pd_to_genpd(dev->pm_domain));
	mutex_lock(&opd->dev_lock);

	list_for_each_entry_safe(de, _de, &opd->dev_list, node) {
		if (de->dev == dev) {
			list_for_each_entry_safe(ce, _ce, &de->clk_list, node) {
				clk_put(ce->clk);
				list_del(&ce->node);
				kfree(ce);
			}

			list_for_each_entry_safe(re, _re, &de->rst_list, node) {
				reset_control_put(re->rst);
				list_del(&re->node);
				kfree(re);
			}

			list_del(&de->node);
			kfree(de);
		}
	}

	mutex_unlock(&opd->dev_lock);
}

int owl_pd_power_on(struct generic_pm_domain *domain)
{
	return owl_pd_power(domain, true);
}

int owl_pd_power_off(struct generic_pm_domain *domain)
{
	return owl_pd_power(domain, false);
}


struct generic_pm_domain *owl_of_genpd_xlate_onecell(
		struct of_phandle_args *genpdspec,
		void *data)
{
	struct owl_pm_domain_info *pdi = data;
	unsigned int idx = genpdspec->args[0];

	if (genpdspec->args_count != 1)
		return ERR_PTR(-EINVAL);

	if (idx >= pdi->num_domains) {
		pr_err("%s: invalid domain index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	if (!pdi->domains[idx])
		return ERR_PTR(-ENOENT);

	return &pdi->domains[idx]->domain;
}


int owl_pm_domain_register(struct owl_pm_domain_info *pdi)
{
	int i;
	unsigned int regv;
	struct owl_pm_domain *opd;

	sps_base = pdi->sps_base;

	for (i = 0; i < pdi->num_domains; i++) {
		opd = pdi->domains[i];
		if (opd) {
			regv = readl(sps_base + opd->ack.offset);
			regv &= BIT(opd->ack.bit_idx);
			opd->is_off = (regv == 0);
			mutex_init(&opd->dev_lock);
			INIT_LIST_HEAD(&opd->dev_list);
			pm_genpd_init(&opd->domain, NULL, opd->is_off);
		}
	}

	return __of_genpd_add_provider(pdi->of_node,
					owl_of_genpd_xlate_onecell, pdi);
}
