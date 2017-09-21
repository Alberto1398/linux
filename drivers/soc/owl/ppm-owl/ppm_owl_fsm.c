/* linux/arch/arm/mach-owl/ppm_owl_fsm.c
 *
 * Copyright (c) 2014 actions Electronics Co., Ltd.
 *		http://www.actions-semi.com/
 *
 * gs705a - fsm module of ppm_owl
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

#include "ppm_owl_base.h"
#include "ppm_owl_driver.h"
#include "ppm_owl_fsm.h"
/*
	owl power state machine module
	PPM_OWL_STATE_KERNEL_DEFAULT: defualt
	PPM_OWL_STATE_USER_SCENE:
	PPM_OWL_STATE_KERNEL_THERMAL:
*/
extern int set_second_max_freq(void);
enum ppm_owl_state power_state;
enum ppm_owl_sub_cmd latest_sub_cmd = CMD_MAX;

struct workqueue_struct *state_user_timeout_wq;
struct delayed_work state_user_timeout_work;

static DEFINE_MUTEX(ppm_owl_set_state_lock);

static void state_user_timeout_worker(struct work_struct *work)
{
	ppm_owl_user_data_t user_data;

	pr_debug("%s\n", __func__);
	user_data.arg = AUTO_FREQ_AUTO_CORES;
	user_data.timeout = 0;
	reset_default_power(&user_data);
}

int reset_default_power(ppm_owl_user_data_t *user_data)
{
	int ret = 0;

	pr_debug("%s\n", __func__);
	if (!((power_state == PPM_OWL_STATE_USER_SCENE) || (power_state == PPM_OWL_STATE_KERNEL_DEFAULT)))
		return -1;

	mutex_lock(&ppm_owl_set_state_lock);
	switch (user_data->arg) {
	case AUTO_FREQ_AUTO_CORES:
	/*
		enable hotplug
		set cpufreq governor as interactive
	*/
	unlock_cpu();
	reset_freq_range();
	break;

	default:
	ret = -1;
	break;
	}

	if (ret == 0) {
		cancel_delayed_work(&state_user_timeout_work);
		latest_sub_cmd = CMD_MAX;
		power_state = PPM_OWL_STATE_KERNEL_DEFAULT;
	}
	mutex_unlock(&ppm_owl_set_state_lock);

	return ret;
}

/*
	TODO:check cmd prio
*/
int check_cmd_prio(void)
{
	int ret = 0;

	return ret;
}

int user_scene_set_power(ppm_owl_user_data_t *user_data)
{
	int ret = 0;

	pr_debug("%s,power_state:%d\n", __func__, power_state);

	if (!((power_state == PPM_OWL_STATE_KERNEL_DEFAULT) || (power_state == PPM_OWL_STATE_USER_SCENE)))
		return -1;

	pr_debug("\n %s,%d, cmd:%d, timeout:%d\n", __func__, __LINE__, user_data->arg, user_data->timeout);
	mutex_lock(&ppm_owl_set_state_lock);

	ret = check_cmd_prio();
	if (ret < 0)
		goto out;

	if (latest_sub_cmd == user_data->arg)
		goto out1;

	switch (user_data->arg) {
	case MAX_FREQ_MAX_CORES:
	/*
		disable hotplug & up all cpu
		set cpufreq governor as performance
	*/
	lock_cpu(4);
	set_max_freq();
	break;

	case AUTO_FREQ_MAX_CORES:
	/*
		disable hotplug & up all cpu
		set cpufreq governor as interactive
	*/
	lock_cpu(4);
	reset_freq_range();
	break;

	case MAX_FREQ_FIXED_CORES:
	/*
		disable hotplug & set fix cpu
		set cpufreq governor as performance
	*/
	lock_cpu(2);
	set_max_freq();
	break;

	case AUTO_FREQ_FIXED_CORES:
	/*
		disable hotplug & set fix cpu
		set cpufreq governor as interactive
	*/
	lock_cpu(2);
	reset_freq_range();
	break;

	case BENCHMARK_FREQ_RANGE:
	unlock_cpu();
	set_max_freq_range();
	break;
#ifdef CONFIG_ARM_OWLS700_CPUFREQ
	case APK_FREQ_START:
	lock_cpu(4);
	/* set_second_max_freq(); */
	break;
#endif
	default:
	ret = -1;
	break;
	}

out1:
	if (ret == 0) {
		latest_sub_cmd = user_data->arg;
		power_state = PPM_OWL_STATE_USER_SCENE;
		/*
			restore PPM_OWL_STATE_KERNEL_DEFAULT after timeout
		*/
		pr_debug("cancel_delayed_work\n");
		cancel_delayed_work(&state_user_timeout_work);

		if (user_data->timeout != -1) {
			pr_debug("queue_delayed_work\n");
			queue_delayed_work(state_user_timeout_wq, &state_user_timeout_work, msecs_to_jiffies(user_data->timeout));
		}
	}
out:
	mutex_unlock(&ppm_owl_set_state_lock);
	return ret;
}

int set_power_thermal_state(void)
{
	int ret = 0;

	pr_debug("%s\n", __func__);

	mutex_lock(&ppm_owl_set_state_lock);
/*	if (power_state == PPM_OWL_STATE_USER_SCENE) {
		disable workqueue
	} */
	/*
		tune cores & vdd-cpu/cpu_freq_max
		tune vdd-gpu/gpu_freq_max
	*/
	power_state = PPM_OWL_STATE_KERNEL_THERMAL;

	mutex_unlock(&ppm_owl_set_state_lock);
	return ret;
}

int ppm_owl_fsm_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	power_state = PPM_OWL_STATE_KERNEL_DEFAULT;

	state_user_timeout_wq = create_singlethread_workqueue("ppm_owl_timeout_work");
	INIT_DELAYED_WORK(&state_user_timeout_work, state_user_timeout_worker);

	/* register hotplug function & cpufreq function */
	return ret;
}

void ppm_owl_fsm_exit(void)
{
	pr_info("%s\n", __func__);

	cancel_delayed_work_sync(&state_user_timeout_work);
	destroy_workqueue(state_user_timeout_wq);
}
