/*
 * Driver model for leds and led triggers
 *
 * Copyright (C) 2005 John Lenz <lenz@cs.wisc.edu>
 * Copyright (C) 2005 Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LEDS_GT7
#define __LEDS_GT7

enum led_colours{
	LED_RED,
	LED_GREEN,
	LED_BLUE,
};

/* Trigger specific functions */

#if defined(CONFIG_LEDS_TRIGGER_COLOUR)|| defined(CONFIG_LEDS_TRIGGER_COLOUR_MODULE)
extern void ledtrig_bi_color_activity(int led_colour);
#else
static inline void ledtrig_bi_color_activity(int led_color){}
#endif

#endif		/* __LEDS_GT7 */
