/* linux/arch/arm/mach-owl/ppm_owl_stats.c
 *
 * Copyright (c) 2014 actions Electronics Co., Ltd.
 *		http://www.actions-semi.com/
 *
 * gs705a - stats module of ppm_owl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sysfs.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <asm/cputime.h>
#include <linux/kobject.h>
#include "ppm_owl_sysfs.h"

static spinlock_t ppm_stats_lock;

struct ppm_stats {
	unsigned int cpu_nr;
	unsigned int freq_count;
	unsigned int total_trans;
	unsigned long long  last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	u64 *time_in_state;
	unsigned int *freq_table;
};

static struct ppm_stats *ppm_stats_table;

struct ppm_stats_attribute {
	struct attribute attr;
	ssize_t(*show) (struct ppm_stats *, char *);
};

static int ppm_stats_update(void)
{
	struct ppm_stats *stat;
	unsigned long long cur_time;

	cur_time = get_jiffies_64();
	spin_lock(&ppm_stats_lock);
	stat = ppm_stats_table;
	if (stat->time_in_state) {
		stat->cpu_nr = num_online_cpus() - 1;
		stat->time_in_state[stat->cpu_nr*stat->freq_count + stat->last_index] +=
			cur_time - stat->last_time;
	}
	stat->last_time = cur_time;
	spin_unlock(&ppm_stats_lock);
	return 0;
}

static ssize_t show_total_trans(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ppm_stats *stat = ppm_stats_table;
	if (!stat)
		return 0;
	return sprintf(buf, "%d\n",
			ppm_stats_table->total_trans);
}

static ssize_t show_time_in_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i, j;
	struct ppm_stats *stat = ppm_stats_table;
	if (!stat)
		return 0;

	ppm_stats_update();
	len += sprintf(buf + len, "cpu:	1	2	3	4\n");
	for (i = 0; i < stat->state_num; i++) {
		len += sprintf(buf + len, "%uMHz\n", stat->freq_table[i]/1000);
		for (j = 0; j < CONFIG_NR_CPUS; j++) {
			len += sprintf(buf + len, "	%llu",
				(unsigned long long)
				cputime64_to_clock_t(stat->time_in_state[j*stat->freq_count + i]));
		}
		len += sprintf(buf + len, "\n");
	}
	return len;
}

static DEVICE_ATTR(total_trans, 0644, show_total_trans,  NULL);
static DEVICE_ATTR(time_in_state, 0644, show_time_in_state,  NULL);

static struct attribute *default_attrs[] = {
	&dev_attr_total_trans.attr,
	&dev_attr_time_in_state.attr,
	NULL
};
static struct attribute_group stats_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

static int freq_table_get_index(struct ppm_stats *stat, unsigned int freq)
{
	int index;
	for (index = 0; index < stat->max_state; index++)
		if (stat->freq_table[index] == freq)
			return index;
	return -1;
}

/* should be called late in the CPU removal sequence so that the stats
 * memory is still available in case someone tries to use it.
 */
static void ppm_stats_free_table(void)
{
	struct ppm_stats *stat = ppm_stats_table;

	if (stat) {
		pr_debug("%s: Free stat table\n", __func__);
		kfree(stat->time_in_state);
		kfree(stat);
		ppm_stats_table = NULL;
	}
}

/* must be called early in the CPU removal sequence (before
 * cpufreq_remove_dev) so that policy is still valid.
 */
static void ppm_stats_free_sysfs(void)
{
	pr_debug("%s: Free sysfs stat\n", __func__);
	sysfs_remove_group(ppm_owl_global_kobject, &stats_attr_group);
}

static int ppm_stats_create_table(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table)
{
	unsigned int i, j, count = 0, ret = 0;
	struct ppm_stats *stat;
	unsigned int alloc_size;

	if (ppm_stats_table)
		return -EBUSY;
	stat = kzalloc(sizeof(struct ppm_stats), GFP_KERNEL);
	if ((stat) == NULL)
		return -ENOMEM;

	stat->cpu_nr = 0;

	ret = sysfs_create_group(ppm_owl_global_kobject, &stats_attr_group);
	if (ret) {
		pr_err("failed at(%s:%d)\n", __func__, __LINE__);
		goto error_out;
	}

	ppm_stats_table = stat;

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		count++;
	}

	stat->freq_count = count;
	alloc_size = count * sizeof(int) + count * sizeof(u64) * CONFIG_NR_CPUS;

	stat->max_state = count;
	stat->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!stat->time_in_state) {
		ret = -ENOMEM;
		goto error_out;
	}
	stat->freq_table = (unsigned int *)(stat->time_in_state + count * CONFIG_NR_CPUS);

	j = 0;
	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if (freq_table_get_index(stat, freq) == -1)
			stat->freq_table[j++] = freq;
	}
	stat->state_num = j;
	spin_lock(&ppm_stats_lock);
	stat->last_time = get_jiffies_64();
	stat->last_index = freq_table_get_index(stat, policy->cur);
	spin_unlock(&ppm_stats_lock);
	return 0;
error_out:
	kfree(stat);
	ppm_stats_table = NULL;
	return ret;
}

static void ppm_stats_update_policy_cpu(struct cpufreq_policy *policy)
{
	pr_debug("Updating stats_table for new_cpu %u from last_cpu %u\n",
			policy->cpu, policy->last_cpu);
}

static int ppm_stat_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	int ret;
	struct cpufreq_policy *policy = data;
	struct cpufreq_frequency_table *table;
	unsigned int cpu = policy->cpu;

	if (ppm_stats_table)
		return 0;

	if (policy->cpu != 0)
		return 0;

	if (val == CPUFREQ_UPDATE_POLICY_CPU) {
		ppm_stats_update_policy_cpu(policy);
		return 0;
	}

	if (val != CPUFREQ_NOTIFY)
		return 0;
	table = cpufreq_frequency_get_table(cpu);
	if (!table)
		return 0;

	ret = ppm_stats_create_table(policy, table);
	if (ret)
		return ret;

	pr_info("[%s,%d]\n", __func__, __LINE__);
	return 0;
}

static int ppm_stat_notifier_trans(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct ppm_stats *stat;
	int old_index, new_index;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	/* only care cpu0 */
	if (freq->cpu != 0)
		return 0;

	stat = ppm_stats_table;
	if (!stat)
		return 0;

	old_index = stat->last_index;
	new_index = freq_table_get_index(stat, freq->new);

	/* We can't do stat->time_in_state[-1]= .. */
	if (old_index == -1 || new_index == -1)
		return 0;

	ppm_stats_update();

	if (old_index == new_index)
		return 0;

	spin_lock(&ppm_stats_lock);
	stat->last_index = new_index;
	stat->total_trans++;
	spin_unlock(&ppm_stats_lock);
	return 0;
}

static int __cpuinit ppm_stat_cpu_callback(struct notifier_block *nfb,
							unsigned long action,
							void *hcpu)
{
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		ppm_stats_update();
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		ppm_stats_update();
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		break;
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		ppm_stats_update();
		break;
	}
	return NOTIFY_OK;
}

/* priority=1 so this will get called before cpufreq_remove_dev */
static struct notifier_block ppm_stat_cpu_notifier __refdata = {
	.notifier_call = ppm_stat_cpu_callback,
	.priority = 1,
};

static struct notifier_block notifier_policy_block = {
	.notifier_call = ppm_stat_notifier_policy
};

static struct notifier_block notifier_trans_block = {
	.notifier_call = ppm_stat_notifier_trans
};

static int ppm_stats_setup(void)
{
	int ret;

	spin_lock_init(&ppm_stats_lock);
	ret = cpufreq_register_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return ret;

	register_hotcpu_notifier(&ppm_stat_cpu_notifier);
	/* cpufreq_update_policy(0); */

	ret = cpufreq_register_notifier(&notifier_trans_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		cpufreq_unregister_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
		unregister_hotcpu_notifier(&ppm_stat_cpu_notifier);
		ppm_stats_free_table();
		return ret;
	}

	return 0;
}

static void ppm_stats_cleanup(void)
{
	cpufreq_unregister_notifier(&notifier_policy_block,
			CPUFREQ_POLICY_NOTIFIER);
	cpufreq_unregister_notifier(&notifier_trans_block,
			CPUFREQ_TRANSITION_NOTIFIER);
	unregister_hotcpu_notifier(&ppm_stat_cpu_notifier);

	ppm_stats_free_table();
	ppm_stats_free_sysfs();
}

int ppm_owl_stats_init(void)
{
	int ret;

	pr_info("%s\n", __func__);
	spin_lock_init(&ppm_stats_lock);
	ret = ppm_stats_setup();
	return ret;
}

void ppm_owl_stats_exit(void)
{
	pr_info("%s\n", __func__);
	ppm_stats_cleanup();
}
