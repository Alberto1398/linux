/* linux/arch/arm/mach-owl/ppm_owl_cpufreq_opt.c
 *
 * Copyright (c) 2014 actions Electronics Co., Ltd.
 *		http://www.actions-semi.com/
 *
 * gs705a - cpufreq opt module of ppm_owl
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
//#include <linux/earlysuspend.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <asm-generic/cputime.h>
#include <linux/clk.h>
#include <linux/kobject.h>
#include "ppm_owl_sysfs.h"

enum MODE_SCENE {
	SCENE_LOCAL_VIDEO = 0x00000001,
	SCENE_IDLE_FORCE = 0x00000010,
};

enum MODE_CPU_POWER {
	CPU_POWER_NORMAL,
	CPU_POWER_SAVING,
	MODE_CPU_POWER_END,
};

enum MODE_SCENE_BITS {
	BIT_SINGLE_VIDEO = 0x1,
	BIT_MAX_SCREEN = 0x2,
	BIT_INTERACTIVE_MODE = 0x4,
};

struct power_policy_work {
	int flag;
	struct work_struct policy_change_work;
};

struct cpufreq_interactive_opt_info {
	int enable;
	int mode_scene; /* for video scene */
	int mode_opt;	/* for special apk：fish & stability */
	int mode_cpu_power;	/* private cpu power mode */
#define MAX_COUNT 10
	unsigned int counts_limit;
#define	BEST_FREQ	624000	/* KHz */
#define SINGLE_CPU_MAX_FREQ 1116000		/* KHz */
	unsigned int best_freq;
	unsigned int thermal_freq;
	struct power_policy_work gpu_policy_down;
	struct power_policy_work gpu_policy_up;
#define POWER_OPT_DELAY_SECOND 60
	unsigned int delay_time;
	unsigned long last_input_jiffies;
	int perforemance_first;
};

struct cpufreq_interactive_opt_info opt_info;

int (*gpu_policy_set)(int policy) = NULL;
EXPORT_SYMBOL_GPL(gpu_policy_set);

int gpu_policy_set_func_test(int policy)
{
	return 0;
}

int get_mode_scene(void)
{
	return opt_info.mode_scene;
}

int vde_status_changed(int status)
{
	switch (status) {
	case 1:/*单路视频播放*/
		opt_info.mode_scene |=  BIT_SINGLE_VIDEO;
		opt_info.last_input_jiffies = jiffies;
		break;
	default:
		opt_info.mode_scene &=  ~(unsigned int)BIT_SINGLE_VIDEO;
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(vde_status_changed);

int power_mode_changed(int mode)
{
	switch (mode) {
	case 0:/*均衡模式才做优化*/
		opt_info.mode_scene |=  BIT_INTERACTIVE_MODE;
		opt_info.last_input_jiffies = jiffies;
		break;
	default:
		opt_info.mode_scene &=  ~(unsigned int)BIT_INTERACTIVE_MODE;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(power_mode_changed);

int screen_mode_changed(int mode)
{
	switch (mode) {
	case 1:/*全屏模式才做优化*/
		opt_info.mode_scene |=  BIT_MAX_SCREEN;
		opt_info.last_input_jiffies = jiffies;
		break;
	default:
		opt_info.mode_scene &=  ~(unsigned int)BIT_MAX_SCREEN;
	}
	return 0;
}

int get_mode_opt(void)
{
	return opt_info.mode_opt;
}

/* max screen video:set mode_opt = 0x1, update screen_mode
special apk:set mode_opt = 0x10, */
int set_mode_opt(int mode)
{
	opt_info.mode_opt = mode;
	screen_mode_changed(mode & 0x1);

	return 0;
}

int get_mode_cpu_power(void)
{
	return opt_info.mode_cpu_power;
}

int set_mode_cpu_power(int mode)
{
	opt_info.mode_cpu_power = mode;
	return 0;
}

static void back_to_balance_mode(void)
{
	opt_info.last_input_jiffies = jiffies;
	if ((opt_info.perforemance_first == 0) && gpu_policy_set) {
		/* 切换到性能均衡 */
		schedule_work(&(opt_info.gpu_policy_up.policy_change_work));
		set_mode_cpu_power(CPU_POWER_NORMAL);
		/* set_io_wait_time_in_idle(policy_work->flag); */
		opt_info.perforemance_first = 1;
	}
}

static void switch_to_powersave_mode(void)
{
	if (time_after(jiffies, opt_info.last_input_jiffies + opt_info.delay_time * HZ)
		&& (opt_info.perforemance_first == 1)
		&& gpu_policy_set) {
		/* 切换到省电优先 */
		schedule_work(&(opt_info.gpu_policy_down.policy_change_work));
		set_mode_cpu_power(CPU_POWER_SAVING);
		/* set_io_wait_time_in_idle(policy_work->flag); */
		opt_info.perforemance_first = 0;
	}
}

int thermal_set_max_freq(unsigned int freq)
{
	opt_info.thermal_freq = freq;
	pr_debug("---[%s],freq:%d\n", __func__, freq);
	return 0;
}

struct cpufreq_interactive_cpuinfo_opt {
	int count_over_best;
};

static DEFINE_PER_CPU(struct cpufreq_interactive_cpuinfo_opt, cpuinfo_opt);

unsigned int cpufreq_interactive_power_opt(unsigned int target_freq, unsigned int new_freq)
{
	unsigned int permitted_freq = new_freq;
	struct cpufreq_interactive_cpuinfo_opt *pcpu =
		&per_cpu(cpuinfo_opt, smp_processor_id());

	if (!opt_info.enable)
		goto out;

	if (((get_mode_scene() | BIT_INTERACTIVE_MODE)  == (BIT_SINGLE_VIDEO|BIT_MAX_SCREEN|BIT_INTERACTIVE_MODE))
		|| (get_mode_opt() & SCENE_IDLE_FORCE)) {
		switch_to_powersave_mode();
	} else {
		back_to_balance_mode();
	}

	switch (get_mode_cpu_power()) {
	case CPU_POWER_SAVING:
		if (get_mode_opt() & SCENE_IDLE_FORCE) {
			if (permitted_freq > opt_info.best_freq)
				permitted_freq = opt_info.best_freq;
/*		} else if((new_freq > opt_info.best_freq) &&
					target_freq <= opt_info.best_freq) { */
		} else if (new_freq > opt_info.best_freq) {
			if (++(pcpu->count_over_best) < opt_info.counts_limit)
				permitted_freq = opt_info.best_freq;
		} else {
			pcpu->count_over_best = 0;
		}
		break;
	default:
		break;
	}
	/* limit max_freq of single cpu to 936000 KHz*/
	if (num_online_cpus() == 1) {
		if (permitted_freq >= SINGLE_CPU_MAX_FREQ)
			permitted_freq = SINGLE_CPU_MAX_FREQ;
	}
	if ((opt_info.thermal_freq) && (permitted_freq > opt_info.thermal_freq))
		permitted_freq = opt_info.thermal_freq;
out:
	return permitted_freq;
}

void cpufreq_interactive_power_opt_input(void)
{
	if (!opt_info.enable)
		return;

	return back_to_balance_mode();
}

static void gpu_policy_work(struct work_struct *work)
{
	struct power_policy_work *policy_work = container_of(work, struct power_policy_work, policy_change_work);

	switch (policy_work->flag) {
	case 0:/* 关闭省功耗设置 */
		pr_info("+");
		gpu_policy_set(0);
		break;
	case 1:/* 打开所有省功耗的设置 */
		pr_info("-");
		gpu_policy_set(1);
		break;
	default:
		break;
	}
	pr_info("policy_change mode: %d\n", policy_work->flag);
}

static ssize_t show_mode_opt(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%u\n", opt_info.mode_opt);
}

static ssize_t __ref store_mode_opt(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	int val;

	ret = sscanf(buf, "%u", &val);
	if (ret != 1)
		return -EINVAL;
	set_mode_opt(val);
	return count;
}
static DEVICE_ATTR(mode_opt, 0644, show_mode_opt,  store_mode_opt);

static ssize_t show_mode_scene(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%u\n", opt_info.mode_scene);
}

static ssize_t __ref store_mode_scene(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	int val;

	ret = sscanf(buf, "%u", &val);
	if (ret != 1)
		return -EINVAL;
	opt_info.mode_scene = val;
	return count;
}
static DEVICE_ATTR(mode_scene, 0644, show_mode_scene,  store_mode_scene);

static ssize_t show_mode_cpu_power(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%u\n", opt_info.mode_cpu_power);
}
static DEVICE_ATTR(mode_cpu_power, 0644, show_mode_cpu_power,  NULL);

static ssize_t show_enable(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%u\n", opt_info.enable);
}

static ssize_t __ref store_enable(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	int val;

	ret = sscanf(buf, "%u", &val);
	if (ret != 1)
		return -EINVAL;
	opt_info.enable = val;
	return count;
}
static DEVICE_ATTR(enable, 0644, show_enable,  store_enable);

static ssize_t show_delay_time(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%u\n", opt_info.delay_time);
}

static ssize_t __ref store_delay_time(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	int val;

	ret = sscanf(buf, "%u", &val);
	if (ret != 1)
		return -EINVAL;
	opt_info.delay_time = val;
	return count;
}
static DEVICE_ATTR(delay_time, 0644, show_delay_time, store_delay_time);

static struct attribute *ppm_owl_cpufreq_opt_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_mode_scene.attr,
	&dev_attr_mode_cpu_power.attr,
	&dev_attr_mode_opt.attr,
	&dev_attr_delay_time.attr,
	NULL
};

static struct attribute_group ppm_owl_cpufreq_opt_attr_group = {
	.attrs = ppm_owl_cpufreq_opt_attrs,
	.name = "cpufreq_opt",
};

int ppm_owl_cpufreq_opt_init(void)
{
	int ret = 0;

	gpu_policy_set = gpu_policy_set_func_test;

	opt_info.enable = 1;

	opt_info.counts_limit = MAX_COUNT;
	opt_info.perforemance_first = 1;
	opt_info.best_freq = BEST_FREQ;

	opt_info.delay_time = POWER_OPT_DELAY_SECOND;
	opt_info.last_input_jiffies = jiffies + HZ*opt_info.delay_time;

	opt_info.gpu_policy_down.flag = 1;
	opt_info.gpu_policy_up.flag = 0;
	INIT_WORK(&(opt_info.gpu_policy_down.policy_change_work), gpu_policy_work);
	INIT_WORK(&(opt_info.gpu_policy_up.policy_change_work), gpu_policy_work);

	ret = sysfs_create_group(ppm_owl_global_kobject, &ppm_owl_cpufreq_opt_attr_group);
	if (ret)
		pr_err("failed at(%s:%d)\n", __func__, __LINE__);

	return 0;
}
