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

#include <dt-bindings/pm-domains/pm-domains-s700.h>

/* SPS Registers Offset */
#define SPS_PG_CTL			0x0000
#define SPS_PG_ACK			0x0018

static int owl_gpu_isolation(struct owl_pm_domain *opd, bool enable)
{
	if (opd->id != POWER_DOMAIN_GPU_B)
		return 0;

	if (enable) {
		udelay(100);
	}

	return 0;
}

static int owl_pd_callback(struct owl_pm_domain *domain, bool power_on,
			enum pm_callback_type type)
{
	if (type == PM_CB_TYPE_ISO || type == PM_CB_TYPE_POWER)
		return owl_gpu_isolation(domain, power_on);

	return 0;
}

static struct owl_pm_domain owl_pm_domain_gpu =
	OWL_DOMAIN(POWER_DOMAIN_GPU_B, "gpu", SPS_PG_CTL, 3, SPS_PG_CTL, 3, owl_pd_callback);
static struct owl_pm_domain owl_pm_domain_vce =
	OWL_DOMAIN(POWER_DOMAIN_VCE, "vce", SPS_PG_CTL, 1, SPS_PG_ACK, 1, NULL);
static struct owl_pm_domain owl_pm_domain_sensor =
	OWL_DOMAIN(POWER_DOMAIN_SENSOR, "sensor", SPS_PG_CTL, 5, SPS_PG_ACK, 5, NULL);
static struct owl_pm_domain owl_pm_domain_vde =
	OWL_DOMAIN(POWER_DOMAIN_VDE, "vde", SPS_PG_CTL, 0, SPS_PG_ACK, 0, NULL);
static struct owl_pm_domain owl_pm_domain_hde =
	OWL_DOMAIN(POWER_DOMAIN_HDE, "hde", SPS_PG_CTL, 7, SPS_PG_ACK, 7, NULL);
static struct owl_pm_domain owl_pm_domain_usb3 =
	OWL_DOMAIN(POWER_DOMAIN_USB3, "usb3", SPS_PG_CTL, 10, SPS_PG_ACK, 10, NULL);
static struct owl_pm_domain owl_pm_domain_ds =
	OWL_DOMAIN(POWER_DOMAIN_DS, "ds", SPS_PG_CTL, 9, SPS_PG_ACK, 9, NULL);
static struct owl_pm_domain owl_pm_domain_dma =
	OWL_DOMAIN(POWER_DOMAIN_DMA, "dma", SPS_PG_CTL, 8, SPS_PG_ACK, 8, NULL);
static struct owl_pm_domain owl_pm_domain_usb2h0 =
	OWL_DOMAIN(POWER_DOMAIN_USB2H0, "usb2h0", SPS_PG_CTL, 11, SPS_PG_ACK, 11, NULL);
static struct owl_pm_domain owl_pm_domain_usb2h1 =
	OWL_DOMAIN(POWER_DOMAIN_USB2H1, "usb2h1", SPS_PG_CTL, 2, SPS_PG_ACK, 2, NULL);

static struct owl_pm_domain *owl_pm_domains[] = {
	[POWER_DOMAIN_GPU_B] = &owl_pm_domain_gpu,
	[POWER_DOMAIN_VCE] = &owl_pm_domain_vce,
	[POWER_DOMAIN_SENSOR] = &owl_pm_domain_sensor,
	[POWER_DOMAIN_VDE] = &owl_pm_domain_vde,
	[POWER_DOMAIN_HDE] = &owl_pm_domain_hde,
	[POWER_DOMAIN_USB3] = &owl_pm_domain_usb3,
	[POWER_DOMAIN_DMA] = &owl_pm_domain_dma,
	[POWER_DOMAIN_DS] = &owl_pm_domain_ds,
	[POWER_DOMAIN_USB2H0] = &owl_pm_domain_usb2h0,
	[POWER_DOMAIN_USB2H1] = &owl_pm_domain_usb2h1,
};

static struct owl_pm_domain_info owl_pm_domain_info = {
	.num_domains = ARRAY_SIZE(owl_pm_domains),
	.domains = owl_pm_domains,
};

static int owl_pm_domain_probe(struct platform_device *pdev)
{
	pr_info("s700 owl_pm_domain init\n");
	owl_pm_domain_info.sps_base = of_iomap(pdev->dev.of_node, 0);
	owl_pm_domain_info.of_node = pdev->dev.of_node;
	return owl_pm_domain_register(&owl_pm_domain_info);
}

static const struct of_device_id owl_of_match[] = {
	{ .compatible = "actions,s700-pm-domains" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, owl_of_match);

static struct platform_driver owl_pd_platform_driver = {
	.driver = {
		.name = "s700-pm-domains",
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
