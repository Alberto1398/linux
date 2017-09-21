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

#ifndef __OWL_PD_H
#define __OWL_PD_H

struct owl_pm_clock_entry {
	struct list_head node;
	struct clk *clk;
};

struct owl_pm_reset_entry {
	struct list_head node;
	struct reset_control *rst;
};

struct owl_pm_device_entry {
	struct list_head node;
	struct device *dev;

	struct list_head clk_list;
	struct list_head rst_list;
};

struct owl_pm_sps_ctl {
	unsigned short offset;
	u8 bit_idx;
};

/* Device IDs */
enum pm_callback_type {
	PM_CB_TYPE_POWER,
	PM_CB_TYPE_ISO,
	PM_CB_TYPE_DEV
};

struct owl_pm_domain {
	struct generic_pm_domain domain;
	char const *name;
	int id;
	bool is_off;
	int (*owl_pm_callback)(struct owl_pm_domain *domain, bool enable,
			enum pm_callback_type type);
	struct owl_pm_sps_ctl ctrl;
	struct owl_pm_sps_ctl ack;
	struct list_head dev_list;
	struct mutex dev_lock;
};

struct owl_pm_domain_info {
	unsigned int	num_domains;
	struct owl_pm_domain **domains;
	struct device_node *of_node;
	void __iomem *sps_base;
};


int owl_pd_power_off(struct generic_pm_domain *domain);
int owl_pd_power_on(struct generic_pm_domain *domain);
void owl_pd_attach_dev(struct device *dev);
void owl_pd_detach_dev(struct device *dev);

#define OWL_DOMAIN(_id, _name, _en_offset, _en_bit, _ack_offset, _ack_bit, _cb_fun)	\
{									\
	.domain	= {							\
			.name = _name,					\
			.power_on_latency_ns = 500000,			\
			.power_off_latency_ns = 200000,			\
			.power_off = owl_pd_power_off,			\
			.power_on = owl_pd_power_on,			\
			.attach_dev = owl_pd_attach_dev,		\
			.detach_dev = owl_pd_detach_dev,		\
		},							\
	.id = _id,							\
	.owl_pm_callback = _cb_fun,			\
	.ctrl = {							\
		.offset = _en_offset,			\
		.bit_idx = _en_bit,				\
	},								\
	.ack = {							\
		.offset = _ack_offset,			\
		.bit_idx = _ack_bit,			\
	},								\
	.is_off = true,					\
}

int owl_pm_domain_register(struct owl_pm_domain_info *pdi);

#endif	/* __OWL_PD_H */
