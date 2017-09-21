/*
 * LED Activity Trigger
 *
 * Copyright 2006 Openedhand Ltd.
 *
 * Author: Xijian Chen <chenxijian@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/leds-gt7.h>

DEFINE_LED_TRIGGER(ledtrig_red_color);
DEFINE_LED_TRIGGER(ledtrig_blue_color);

void ledtrig_bi_color_activity(int color)
{
	pr_info("[%s]active led:%d", __func__, color);
	if (color == LED_RED) {
		led_trigger_event(ledtrig_blue_color, LED_OFF);
		led_trigger_event(ledtrig_red_color, LED_FULL);
	} else if (color == LED_BLUE) {
		led_trigger_event(ledtrig_red_color, LED_OFF);
		led_trigger_event(ledtrig_blue_color, LED_FULL);
	} else {
		led_trigger_event(ledtrig_red_color, LED_OFF);
		led_trigger_event(ledtrig_blue_color, LED_OFF);
	}
}
EXPORT_SYMBOL(ledtrig_bi_color_activity);

static int __init ledtrig_bi_color_init(void)
{
	pr_info("[%s]init...\n", __func__);
	led_trigger_register_simple("red_trigger", &ledtrig_red_color);
	led_trigger_register_simple("blue_trigger", &ledtrig_blue_color);
	return 0;
}

static void __exit ledtrig_bi_color_exit(void)
{
	pr_info("[%s]exit...\n", __func__);
	led_trigger_unregister_simple(ledtrig_red_color);
	led_trigger_unregister_simple(ledtrig_blue_color);
}

module_init(ledtrig_bi_color_init);
module_exit(ledtrig_bi_color_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("LED IDE Disk Activity Trigger");
MODULE_LICENSE("GPL");
