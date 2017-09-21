/* linux/drivers/video/backlight/owl_common_bl.c
 *
 * Actions SoC backlight common driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#define DEBUGX
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/backlight.h>

enum owl_backlight_type {
	OWL_BACKLIHGT_TYPE_PWM = 0,
	OWL_BACKLIHGT_TYPE_CMD,
};

#ifdef CONFIG_VIDEO_OWL_AMOLED
extern int owl_oled_backlight_set_onoff(int onoff);
extern int owl_oled_backlight_is_on(void);
#endif

#ifdef CONFIG_PWM_OWL
extern int owl_pwm_backlight_set_onoff(int onoff);
extern int owl_pwm_backlight_is_on(void);
#endif

int owl_bl_register(struct backlight_device *bl_device)
{
	pr_info("start\n");

	if (bl_device == NULL) {
		pr_err("bl_device is NULL\n");
		return -EINVAL;
	}
	/*TODO*/
	return 0;
}
EXPORT_SYMBOL(owl_bl_register);

/*
 * Backlight on/off transition notifications,
 * Someone, such as charger driver, will care about the
 * backlight status. Use the notifier to notify them.
 */
static BLOCKING_NOTIFIER_HEAD(owl_bl_chain_head);

int owl_bl_notifier_call_chain(unsigned long val)
{
	int ret = blocking_notifier_call_chain(&owl_bl_chain_head, val, NULL);

	return notifier_to_errno(ret);
}
EXPORT_SYMBOL(owl_bl_notifier_call_chain);

/*=============================================================================
			BL APIs provide to others
 *============================================================================*/
/*
 * backlight on/off notifier, the notifier value is
 * brightness percent, while 0 is off, and 100 is full on
 */
int owl_backlight_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&owl_bl_chain_head, nb);
}
EXPORT_SYMBOL_GPL(owl_backlight_notifier_register);

int owl_backlight_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&owl_bl_chain_head, nb);
}
EXPORT_SYMBOL_GPL(owl_backlight_notifier_unregister);

int owl_backlight_is_on(void)
{
#ifdef CONFIG_PWM_OWL
	if (owl_pwm_backlight_is_on() >= 0)
		return owl_pwm_backlight_is_on();
#endif

#ifdef CONFIG_VIDEO_OWL_AMOLED
	if (owl_oled_backlight_is_on() >= 0)
		return owl_oled_backlight_is_on();
#endif
		return -ENODEV;
}
EXPORT_SYMBOL(owl_backlight_is_on);

/*
 * set backlight on/off, 0 is off, 1 is on.
 *
 * NOTE: this interface SHOULD ONLY change the on/off
 *	status, CAN NOT change the brightness value.
 */
void owl_backlight_set_onoff(int onoff)
{
#ifdef CONFIG_PWM_OWL
	owl_pwm_backlight_set_onoff(onoff);
	return;
#endif

#ifdef CONFIG_VIDEO_OWL_AMOLED
	owl_oled_backlight_set_onoff(onoff);
	return;
#endif
}
EXPORT_SYMBOL(owl_backlight_set_onoff);

