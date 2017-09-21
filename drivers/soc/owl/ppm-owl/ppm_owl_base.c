/* linux/arch/arm/mach-owl/ppm_owl_base.c
 *
 * Copyright (c) 2014 actions Electronics Co., Ltd.
 *		http://www.actions-semi.com/
 *
 * gs705a - base module of ppm_owl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/ktime.h>
#include <linux/tick.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/gpio.h>
#include <linux/cpufreq.h>
#include <linux/moduleparam.h>
#include <linux/cpufreq.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <asm-generic/cputime.h>
#include <linux/clk.h>

/*
	hotplug setting:
	1.one core
	2.loading parameter
*/
#define HOT_PLUG 0

#if HOT_PLUG
extern int set_plug_mask(int val);
extern int set_user_lock(int val);
#endif

int	set_plug(u32 mask)
{
	ssize_t ret = 0;
	int cpu;
	int err = 0;
	static struct device *cpu_dev;
	u32 cpu_num;
	int i;

	cpu_hotplug_driver_lock();

	cpu_num = num_possible_cpus();

	/*cpu0 cannot be plugged*/
	for (i = cpu_num; i > 1; i--) {
		cpu = i - 1;

		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			err = -ENODEV;
			goto set_plug_out;
		}

		if (mask & (1 << cpu)) {
			if (!cpu_online(cpu)) {
				pr_debug("cpu_up:%d\n", cpu);
				ret = cpu_up(cpu);
				if (!ret)
					kobject_uevent(&cpu_dev->kobj, KOBJ_ONLINE);
			}

		} else {
			if (cpu_online(cpu)) {
				pr_debug("cpu_down:%d\n", cpu);
				ret = cpu_down(cpu);
				if (!ret)
					kobject_uevent(&cpu_dev->kobj, KOBJ_OFFLINE);
			}
		}
	}

set_plug_out:
	cpu_hotplug_driver_unlock();

	return err;
}

int lock_cpu(int cpu_nr)
{
	int i;
	unsigned int mask = 0;

	pr_debug("%s\n", __func__);

	for (i = CONFIG_NR_CPUS; i > cpu_nr; i--)
		mask |= 1<<(i-1);


	#if HOT_PLUG
	set_user_lock(1);
	set_plug_mask(mask);
	#endif
	/* TODO:delay for last hotplug timer finish */

	mask = 0;
	for (i = 0; i < cpu_nr; i++)
		mask |= 1<<i;

	set_plug(mask);
	return 0;
}

int unlock_cpu(void)
{
	pr_debug("%s\n", __func__);
	#if HOT_PLUG
	set_plug_mask(0);
	set_user_lock(0);
	#endif


	return 0;
}

extern int __cpufreq_set_policy(struct cpufreq_policy *data,
				struct cpufreq_policy *policy);

int min, max;

int set_max_freq_range(void)
{
	unsigned int ret;
	struct cpufreq_policy new_policy;
	struct cpufreq_policy *policy;

	pr_debug("%s\n", __func__);

	policy = cpufreq_cpu_get(0);
	if (policy != NULL) {
		ret = cpufreq_get_policy(&new_policy, policy->cpu);
		if (ret)
			return -EINVAL;

		pr_debug("new_policy.min:%d, new_policy.max:%d\n", policy->cpuinfo.min_freq, policy->cpuinfo.max_freq);
		new_policy.min = min;
		ret = __cpufreq_set_policy(policy, &new_policy);
		policy->user_policy.min = policy->min;

		new_policy.max = policy->cpuinfo.max_freq;
		ret = __cpufreq_set_policy(policy, &new_policy);
		policy->user_policy.max = policy->max;

		cpufreq_cpu_put(policy);
	}

	return 0;
}

int set_max_freq(void)
{
	unsigned int ret;
	struct cpufreq_policy new_policy;
	struct cpufreq_policy *policy;

	pr_debug("%s\n", __func__);

	policy = cpufreq_cpu_get(0);
	if (policy != NULL) {
		ret = cpufreq_get_policy(&new_policy, policy->cpu);
		if (ret)
			return -EINVAL;
		min = policy->min;
		max = policy->max;

		pr_debug("new_policy.min:%d, new_policy.max:%d\n", policy->cpuinfo.max_freq, policy->cpuinfo.max_freq);

		new_policy.max = policy->cpuinfo.max_freq;
		ret = __cpufreq_set_policy(policy, &new_policy);
		policy->user_policy.max = policy->max;

		new_policy.min = policy->cpuinfo.max_freq;
		ret = __cpufreq_set_policy(policy, &new_policy);
		policy->user_policy.min = policy->min;

		cpufreq_cpu_put(policy);
	}

	return 0;
}

int reset_freq_range(void)
{
	unsigned int ret;
	struct cpufreq_policy new_policy;
	struct cpufreq_policy *policy;

	pr_debug("%s\n", __func__);
	policy = cpufreq_cpu_get(0);
	if (policy != NULL) {
		ret = cpufreq_get_policy(&new_policy, policy->cpu);
		if (ret)
			return -EINVAL;

		new_policy.min = min;
		pr_debug("new_policy.min:%d, new_policy.max:%d\n", min, max);
		ret = __cpufreq_set_policy(policy, &new_policy);
		policy->user_policy.min = policy->min;

		new_policy.max = max;
		ret = __cpufreq_set_policy(policy, &new_policy);
		policy->user_policy.max = policy->max;

		cpufreq_cpu_put(policy);
	}

	return 0;
}

/* #define CPUFREQ_TABLE_MAX	12
int mCpuFreqTable[CPUFREQ_TABLE_MAX];

int round_freq(int freq)
{
	int n, exactFreq = freq;
	for (n = 0; n < mCpuFreqCount; n++ ) {
		if (exactFreq <= mCpuFreqTable[n]) {
			exactFreq = mCpuFreqTable[n];
			break;
		}
	}

	pr_info("freq:%d to right points: %d", freq, exactFreq);
	return exactFreq;
}


void freqResetForNow(void)
{
	const size_t count = freqSetings.size();
	size_t i;
	int max, min;
	char tmpbuf[64];

	max = mMaxCpuFreq;
	min = mMinCpuFreq;

	PX_LOG("defaultFreqInfo.min %d", mMinCpuFreq);
	PX_LOG("defaultFreqInfo.max %d", mMaxCpuFreq);
	for (i = 0; i < count; i++) {
		struct FreqInfo freqInfo = freqSetings.valueAt(i);
		PX_LOG("index %d freqInfo.min %d", i, freqInfo.min);
		PX_LOG("index %d freqInfo.max %d", i, freqInfo.max);
		if(max > freqInfo.max) max = freqInfo.max;
		if(min < freqInfo.min) min = freqInfo.min;
	}

	if (max < min)
		max = min;

	sprintf(tmpbuf, "%d", min);
	writeStringToFile( CPU_MIN_FREQ_PATH, tmpbuf);
	sprintf(tmpbuf, "%d", max);
	writeStringToFile( CPU_MAX_FREQ_PATH, tmpbuf);
}

int set_cpufreq_range(int min, int max)
{
	PX_LOG("setCpuFreqRange");
	if(min > max) {
		PX_LOG("setCpuFreqRange min > max");
		return -1;
	}
	struct FreqInfo freqInfo;
	freqInfo.max = roundFreq(max);
	freqInfo.min = roundFreq(min);

	freqSetings.add(freqInfo);
	freqResetForNow();
	return true;
}

int restore_cpufreq_range(int min, int max)
{

}
 */

int ppm_owl_base_init(void)
{
	int ret = 0;

	/* get cpufreq range*/
	pr_info("%s\n", __func__);

	return ret;
}

void ppm_owl_base_exit(void)
{
	pr_info("%s\n", __func__);
}
