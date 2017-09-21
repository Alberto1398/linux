/* linux/arch/arm/mach-owl/ppm_owl_input.c
 *
 * Copyright (c) 2014 actions Electronics Co., Ltd.
 *		http://www.actions-semi.com/
 *
 * gs705a - input module of ppm_owl
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

/*
	owl power input handler
*/
#define INPUT_MODE_DETECT 1

#if INPUT_MODE_DETECT
#if 0
static unsigned long input_interval;
#endif

extern void cpufreq_interactive_power_opt_input(void);

static void ppm_owl_event(struct input_handle *handle,
						unsigned int type,
						unsigned int code, int value)
{
#if 0
	input_interval = jiffies + 30*HZ;
	if (type == EV_SYN && code == SYN_REPORT)
		pr_info("%s, %d\n", __func__, __LINE__);
#endif

	cpufreq_interactive_power_opt_input();
}

static int ppm_owl_connect(struct input_handler *handler,
						struct input_dev *dev,
						const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "owl-power";

	error = input_register_handle(handle);
	if (error) {
		pr_err("Failed to register input power handler, error %d\n",
		       error);
		kfree(handle);
		return error;
	}

	error = input_open_device(handle);
	if (error) {
		pr_err("Failed to open input power device, error %d\n", error);
		input_unregister_handle(handle);
		kfree(handle);
		return error;
	}

	return 0;
}

static void ppm_owl_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id cpuplug_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			    BIT_MASK(ABS_MT_POSITION_X) |
			    BIT_MASK(ABS_MT_POSITION_Y) },
	}, /* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			    BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	}, /* touchpad */
	{ },
};

static struct input_handler ppm_owl_handler = {
	.event          = ppm_owl_event,
	.connect       = ppm_owl_connect,
	.disconnect   = ppm_owl_disconnect,
	.name          = "owl-power",
	.id_table      = cpuplug_ids,
};
#endif

/*inits*/
int ppm_owl_input_init(void)
{
#if INPUT_MODE_DETECT
	if (input_register_handler(&ppm_owl_handler))
		pr_err("%s: failed to register input handler\n", __func__);
#endif
	return 0;
}

void ppm_owl_input_exit(void)
{
#if INPUT_MODE_DETECT
	input_unregister_handler(&ppm_owl_handler);
#endif
}
