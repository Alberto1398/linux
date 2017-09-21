/* linux/arch/arm/mach-owl/ppm_owl_sysfs.c
 *
 * Copyright (c) 2014 actions Electronics Co., Ltd.
 *		http://www.actions-semi.com/
 *
 * gs705a - sysfs module of ppm_owl
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
#include <asm-generic/cputime.h>
#include <linux/clk.h>
#include <linux/kobject.h>
#include "ppm_owl_driver.h"
#include "ppm_owl_fsm.h"
#include "ppm_owl_base.h"
/*
	owl power sysfs interface
*/
static DEFINE_MUTEX(ppm_owl_lock);

int ppm_enable_flag = 1;

static ssize_t show_enable(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return sprintf(buf, "%u\n", ppm_enable_flag);
}

int set_ppm_enable(int val)
{
	int ret = 0;
	if (val == 0) {
		mutex_lock(&ppm_owl_lock);
		ppm_enable_flag = 0;
		mutex_unlock(&ppm_owl_lock);
		pr_info("%s,ppm_enable_flag: %d\n", __func__, ppm_enable_flag);
	} else if (val == 1) {
		mutex_lock(&ppm_owl_lock);
		ppm_enable_flag = 1;
		mutex_unlock(&ppm_owl_lock);
		pr_info("%s,ppm_enable_flag: %d\n", __func__, ppm_enable_flag);
	} else
		ret = -EINVAL;
	return ret;
}
EXPORT_SYMBOL(ppm_enable_flag);

static ssize_t __ref store_enable(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	ssize_t ret = count;
	int val = 0;

	switch (buf[0]) {
	case '0':
	case '1':
		set_ppm_enable(val);
		break;
	default:
		pr_info("%s: ppm_enable_flag: %d\n", __func__, ppm_enable_flag);
		ret = -EINVAL;
		break;
	}
	return count;
}
static DEVICE_ATTR(enable, 0644, show_enable,  store_enable);

struct kobject *ppm_owl_global_kobject;

int ppm_owl_sysfs_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ppm_owl_global_kobject = kobject_create_and_add("ppm_owl", &cpu_subsys.dev_root->kobj);
	ret = sysfs_create_file(ppm_owl_global_kobject, &dev_attr_enable.attr);
	if (ret) {
		pr_err("failed at(%s:%d)\n", __func__, __LINE__);
		return ret;
	}

	return ret;
}

void ppm_owl_sysfs_exit(void)
{
}
