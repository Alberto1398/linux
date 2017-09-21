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
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include "pd.h"

#include <dt-bindings/pm-domains/pm-domains-s900.h>

/* SPS Registers Offset */
#define SPS_PG_CTL			0x0000
#define SPS_PG_ACK			0x0004
#define SPS_LDO_CTL			0x0014

#define NOC_BASE			0xE0500000

extern bool usb3_need_set_device_noattached(void);

static void __iomem *noc_base;

static int owl_pd_callback(struct owl_pm_domain *domain, bool enable,
			enum pm_callback_type type);

static struct owl_pm_domain owl_pm_domain_gpu_b =
	OWL_DOMAIN(POWER_DOMAIN_GPU_B, "gpu_b", SPS_PG_CTL, 3, SPS_PG_ACK, 3, owl_pd_callback);
static struct owl_pm_domain owl_pm_domain_vce =
	OWL_DOMAIN(POWER_DOMAIN_VCE, "vce", SPS_PG_CTL, 4, SPS_PG_ACK, 4, NULL);
static struct owl_pm_domain owl_pm_domain_sensor =
	OWL_DOMAIN(POWER_DOMAIN_SENSOR, "sensor", SPS_PG_CTL, 5, SPS_PG_ACK, 5, NULL);
static struct owl_pm_domain owl_pm_domain_vde =
	OWL_DOMAIN(POWER_DOMAIN_VDE, "vde", SPS_PG_CTL, 6, SPS_PG_ACK, 6, NULL);
static struct owl_pm_domain owl_pm_domain_hde =
	OWL_DOMAIN(POWER_DOMAIN_HDE, "hde", SPS_PG_CTL, 7, SPS_PG_ACK, 7, NULL);
static struct owl_pm_domain owl_pm_domain_usb3 =
	OWL_DOMAIN(POWER_DOMAIN_USB3, "usb3", SPS_PG_CTL, 8, SPS_PG_ACK, 8, owl_pd_callback);
static struct owl_pm_domain owl_pm_domain_ddr0 =
	OWL_DOMAIN(POWER_DOMAIN_DDR0, "ddr0", SPS_PG_CTL, 9, SPS_PG_ACK, 9, NULL);
static struct owl_pm_domain owl_pm_domain_ddr1 =
	OWL_DOMAIN(POWER_DOMAIN_DDR1, "ddr1", SPS_PG_CTL, 10, SPS_PG_ACK, 10, NULL);
static struct owl_pm_domain owl_pm_domain_de =
	OWL_DOMAIN(POWER_DOMAIN_DE, "de", SPS_PG_CTL, 13, SPS_PG_ACK, 11, NULL);
static struct owl_pm_domain owl_pm_domain_nand =
	OWL_DOMAIN(POWER_DOMAIN_NAND, "nand", SPS_PG_CTL, 14, SPS_PG_ACK, 12, NULL);
static struct owl_pm_domain owl_pm_domain_usb2h0 =
	OWL_DOMAIN(POWER_DOMAIN_USB2H0, "usb2h0", SPS_PG_CTL, 15, SPS_PG_ACK, 13, NULL);
static struct owl_pm_domain owl_pm_domain_usb2h1 =
	OWL_DOMAIN(POWER_DOMAIN_USB2H1, "usb2h1", SPS_PG_CTL, 16, SPS_PG_ACK, 14, NULL);

static struct owl_pm_domain *owl_pm_domains[] = {
	[POWER_DOMAIN_GPU_B] = &owl_pm_domain_gpu_b,
	[POWER_DOMAIN_VCE] = &owl_pm_domain_vce,
	[POWER_DOMAIN_SENSOR] = &owl_pm_domain_sensor,
	[POWER_DOMAIN_VDE] = &owl_pm_domain_vde,
	[POWER_DOMAIN_HDE] = &owl_pm_domain_hde,
	[POWER_DOMAIN_USB3] = &owl_pm_domain_usb3,
	[POWER_DOMAIN_DDR0] = &owl_pm_domain_ddr0,
	[POWER_DOMAIN_DDR1] = &owl_pm_domain_ddr1,
	[POWER_DOMAIN_DE] = &owl_pm_domain_de,
	[POWER_DOMAIN_NAND] = &owl_pm_domain_nand,
	[POWER_DOMAIN_USB2H0] = &owl_pm_domain_usb2h0,
	[POWER_DOMAIN_USB2H1] = &owl_pm_domain_usb2h1,
};
static struct owl_pm_domain_info owl_pm_domain_info = {
	.num_domains = ARRAY_SIZE(owl_pm_domains),
	.domains = owl_pm_domains,
};


static int owl_usb3_avdd_pm(struct owl_pm_domain *opd, bool enable)
{
	unsigned int ctrl;

	if (usb3_need_set_device_noattached() == false)
		return 0;

	if (opd->id != POWER_DOMAIN_USB3)
		return 0;

	ctrl = readl(owl_pm_domain_info.sps_base + SPS_LDO_CTL);
	if (enable)
		ctrl |= BIT(17);
	else
		ctrl &= ~BIT(17);
	writel(ctrl, owl_pm_domain_info.sps_base + SPS_LDO_CTL);

	return 0;
}
static int owl_gpu_isolate(struct owl_pm_domain *opd, bool enable_isolation)
{
	unsigned int ctrl;

	/* only GPU need isolation */
	if (opd->id != POWER_DOMAIN_GPU_B)
		return 0;

	ctrl = readl(owl_pm_domain_info.sps_base + SPS_PG_CTL);
	/* software control GPU powergate */
	ctrl &= ~BIT(11);
	if (enable_isolation)
		/* enable isolation */
		ctrl &= ~BIT(12);
	else
		/* disable isolation */
		ctrl |= BIT(12);
	writel(ctrl, owl_pm_domain_info.sps_base + SPS_PG_CTL);

	return 0;
}

static int owl_pd_callback(struct owl_pm_domain *domain, bool enable,
			enum pm_callback_type type)
{

	if (type == PM_CB_TYPE_POWER)
		return owl_usb3_avdd_pm(domain, enable);
	else if (type == PM_CB_TYPE_ISO)
		return owl_gpu_isolate(domain, enable);
	else if (type == PM_CB_TYPE_DEV) {
		/* set noc parameter for 4K video decoder performace */
		if (domain->id == POWER_DOMAIN_GPU_B) {
			if (noc_base) { /* s900 */
				writel(0x0, noc_base + 0x908);
				writel(0x0, noc_base + 0xa08);
			}
		}
	}

	return 0;
}

static int owl_pm_domain_probe(struct platform_device *pdev)
{
	pr_info("s900 owl_pm_domain init\n");
	owl_pm_domain_info.sps_base = of_iomap(pdev->dev.of_node, 0);
	owl_pm_domain_info.of_node = pdev->dev.of_node;
	noc_base = ioremap(NOC_BASE, 0x1000);

	return owl_pm_domain_register(&owl_pm_domain_info);
}

static const struct of_device_id owl_of_match[] = {
	{ .compatible = "actions,s900-pm-domains" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, owl_of_match);

static struct platform_driver owl_pd_platform_driver = {
	.driver = {
		.name = "s900-pm-domains",
		.owner = THIS_MODULE,
		.of_match_table = owl_of_match,
	},
	.probe = owl_pm_domain_probe,
};

static __init int owl_pm_domain_init(void)
{
	return platform_driver_register(&owl_pd_platform_driver);
}
arch_initcall(owl_pm_domain_init);
