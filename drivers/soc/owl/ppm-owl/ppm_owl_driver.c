/* linux/arch/arm/mach-owl/ppm_owl_driver.c
 *
 * Copyright (c) 2014 actions Electronics Co., Ltd.
 *		http://www.actions-semi.com/
 *
 * gs705a - driver module of ppm_owl
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
#include "ppm_owl_driver.h"
#include "ppm_owl_fsm.h"
#include "ppm_owl_base.h"
#include "ppm_owl_stats.h"
#include "ppm_owl_cpufreq_opt.h"
#include "ppm_owl_sysfs.h"
#include "ppm_owl_input.h"
/*
	owl power driver
*/

#define DEVDRV_NAME_PPM_OWL_DRV		"ppm_owl"

/* #ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend early_suspend;
#endif */

long ppm_owl_drv_ioctl_cpu(unsigned int cmd, unsigned long arg)
{
	int ret;
	ppm_owl_user_data_t user_data;

	ret = copy_from_user(&user_data, (void __user *)arg, sizeof(ppm_owl_user_data_t));
	if (ret != 0) {
		pr_err("%s: copy_from_user failed, ret=%d\n", __func__, ret);
		return ret;
	}
	pr_debug("%s, arg:0x%x, timeout:%d\n", __func__, user_data.arg, user_data.timeout);
	switch (cmd) {
	/* cpu related */
	case PPM_OWL_SET_CPU:
		user_scene_set_power(&user_data);
		break;

	case PPM_OWL_RESET_CPU:
		reset_default_power(&user_data);
		break;
	default:
		break;
	}
	return 0;
}

static long ppm_owl_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (!ppm_enable_flag)
		return -1;

	switch (cmd) {
	/* cpu related */
	case PPM_OWL_SET_CPU:
	case PPM_OWL_RESET_CPU:
		ppm_owl_drv_ioctl_cpu(cmd, arg);
		break;

	/* gpu related */
	case PPM_OWL_SET_GPU:
		break;

	case PPM_OWL_RESET_GPU:
		break;

	/* scene related */
	case PPM_OWL_SET_SCENE:
		set_mode_opt(arg);
		break;

	default:
		pr_err("ppm_owl: no such cmd 0x%x\n", cmd);
		return -EIO;
	}
	return 0;
}

#ifdef CONFIG_COMPAT

#define PPM_OWL_SET_CPU_COMPAT		_IOWR(PPM_OWL_MAGIC_NUMBER, 0x10, u32)
#define PPM_OWL_RESET_CPU_COMPAT	_IOWR(PPM_OWL_MAGIC_NUMBER, 0x30, u32)

#define PPM_OWL_SET_GPU_COMPAT		_IOWR(PPM_OWL_MAGIC_NUMBER, 0x50, u32)
#define PPM_OWL_RESET_GPU_COMPAT	_IOWR(PPM_OWL_MAGIC_NUMBER, 0x70, u32)
#define PPM_OWL_SET_SCENE_COMPAT	_IOWR(PPM_OWL_MAGIC_NUMBER, 0x90, u32)

static long ppm_owl_drv_compact_ioctl(struct file *file, unsigned int compact_cmd, unsigned long arg)
{
	unsigned int cmd;

	switch (compact_cmd) {
	/* cpu related */
	case PPM_OWL_SET_CPU_COMPAT:
		return ppm_owl_drv_ioctl(file, PPM_OWL_SET_CPU, (unsigned long) compat_ptr(arg));
	case PPM_OWL_RESET_CPU_COMPAT:
		return ppm_owl_drv_ioctl(file, PPM_OWL_RESET_CPU, (unsigned long) compat_ptr(arg));

	case PPM_OWL_SET_GPU_COMPAT:
		cmd = PPM_OWL_SET_GPU;
	case PPM_OWL_RESET_GPU_COMPAT:
		cmd = PPM_OWL_RESET_GPU;
	case PPM_OWL_SET_SCENE_COMPAT:
		cmd = PPM_OWL_SET_SCENE;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return ppm_owl_drv_ioctl(file, cmd, arg);
}
#endif

static const struct file_operations ppm_owl_drv_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ppm_owl_drv_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ppm_owl_drv_compact_ioctl,
#endif
};

static struct miscdevice ppm_owl_drv_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_PPM_OWL_DRV,
	.fops = &ppm_owl_drv_fops,
};

/* #ifdef CONFIG_HAS_EARLYSUSPEND
static void ppm_owl_early_suspend(struct early_suspend *h)
{
	ppm_owl_user_data_t user_data;

	if (!ppm_enable_flag)
		return ;

	pr_info("%s\n", __func__);
	user_data.arg = AUTO_FREQ_AUTO_CORES;
	user_data.timeout = 0;
	reset_default_power(&user_data);
}

static void ppm_owl_late_resume(struct early_suspend *h)
{
	pr_info("ppm_owl_late_resume\n");
}
#endif */

static int ppm_owl_drv_init(void)
{
	int ret;

	pr_info("%s\n", __func__);

	/* 自动insmod，注册设备 */
	ret = misc_register(&ppm_owl_drv_miscdevice);
	if (ret) {
		pr_err("register ppm_owl_drv misc device failed!\n");
		goto err;
	}
	ppm_owl_base_init();
	ppm_owl_fsm_init();
	ppm_owl_sysfs_init();
	ppm_owl_stats_init();
	ppm_owl_input_init();
	ppm_owl_cpufreq_opt_init();
	/*early_suspend ppm_owl*/
/* #ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.suspend = ppm_owl_early_suspend;
	early_suspend.resume  = ppm_owl_late_resume;
	register_early_suspend(&early_suspend);
#endif */

	return 0;
err:
	misc_deregister(&ppm_owl_drv_miscdevice);
	return ret;
}

static void ppm_owl_drv_exit(void)
{
	ppm_owl_input_exit();
	ppm_owl_stats_exit();
	ppm_owl_sysfs_exit();
	ppm_owl_fsm_exit();
	ppm_owl_base_exit();
	misc_deregister(&ppm_owl_drv_miscdevice);
	pr_info("%s\n", __func__);
}
late_initcall(ppm_owl_drv_init);
module_exit(ppm_owl_drv_exit);
