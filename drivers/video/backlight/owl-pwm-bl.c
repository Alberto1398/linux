/*
 * linux/drivers/video/backlight/pwm_bl.c
 *
 * simple PWM based backlight control, board code has to setup
 * 1) pin configuration so PWM waveforms can output
 * 2) platform_data casts to the PWM id (0/1/2/3 on PXA)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#define BACKLIGHT_NAME	"owl_backlight"

extern int owl_bl_notifier_call_chain(unsigned long val);

struct pwm_en_gpio {
	int			gpio;
	int			active_low;
};

/*
 * period,          PWM period in ns
 * total_steps,     the total brightness levels in ns, max brightness
 * duty(ns) = brightness / total steps *  period
 */
struct owl_pwm_bl_pdata {
	struct pwm_en_gpio	en_gpio;

	struct pwm_device	*pwm;
	u32			period;

	u32			total_steps;
	u32			max_brightness;
	u32			min_brightness;
	u32			dft_brightness;

	u32			delay_before_pwm;
	u32			delay_after_pwm;
};

struct owl_pwm_bl {
	struct backlight_device	*bl;

	struct owl_pwm_bl_pdata	data;

	/* 0: disabled, 1: enabled */
	int			power_state;

	/*
	 * a snapshot of bl->props.state,
	 * used for detect the state changing.
	 */
	unsigned int		state;
};

static struct backlight_device *owl_pwm_bl_device;

static inline void __pwm_en_gpio_active(struct pwm_en_gpio *gpio)
{
	if (gpio_is_valid(gpio->gpio))
		gpio_direction_output(gpio->gpio, !gpio->active_low);
}

static inline void __pwm_en_gpio_deactive(struct pwm_en_gpio *gpio)
{
	if (gpio_is_valid(gpio->gpio))
		gpio_direction_output(gpio->gpio, gpio->active_low);
}


/*============================================================================
			 backlight driver
 *==========================================================================*/
static int owl_pwm_bl_update_status(struct backlight_device *bl)
{
	struct owl_pwm_bl *pb = dev_get_drvdata(&bl->dev);
	struct owl_pwm_bl_pdata *data = &pb->data;

	struct backlight_properties *props = &bl->props;

	int brightness = props->brightness;
	int total_steps  = data->total_steps;

	/* the percent of backlight, used to adjust charging cuurent */
	int brightness_percent;

	dev_dbg(&bl->dev, "owl pwm bl update status\n");

	if ((props->state & BL_CORE_FBBLANK) != (pb->state & BL_CORE_FBBLANK)) {
		/*
		 * state of FBBLAK changed, 'power' should be changed
		 * along with it
		 */
		if ((props->state & BL_CORE_FBBLANK) == 0)
			props->power = FB_BLANK_UNBLANK;
		else
			props->power = FB_BLANK_POWERDOWN;

		pb->state = props->state;
	}
	/*
	 * in other case, if we change 'props->power' directly from sysfs,
	 * let 'props->power' act upon the setting.
	 */

	if (props->power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (brightness > 0)
		brightness += data->min_brightness;

	dev_dbg(&bl->dev, "bl->props.power = %d\n", bl->props.power);
	dev_dbg(&bl->dev, "bl->props.fb_blank = %d\n", bl->props.fb_blank);

	if (brightness > total_steps)
		return -EINVAL;

	if (brightness > 0)
		brightness_percent = bl->props.brightness * 100
					/ bl->props.max_brightness;
	else
		brightness_percent = 0;

	owl_bl_notifier_call_chain(brightness_percent);

	dev_dbg(&bl->dev, "%s: brightness = %d, power_state = %d\n",
		__func__, brightness, pb->power_state);

	if (brightness == 0) {
		if (pb->power_state == 0)
			return 0;

		__pwm_en_gpio_deactive(&data->en_gpio);

		/* polarity ? IC bug ??? be careful, TODO */
		pwm_config(data->pwm, data->period, data->period);
		msleep(20);

		pwm_disable(data->pwm);
		pb->power_state = 0;
	} else {
		dev_dbg(&bl->dev, "duty_ns = %x\n",
			brightness * data->period / total_steps);
		dev_dbg(&bl->dev, "period_ns = %x\n", data->period);

		if (pb->power_state == 0) {
			dev_dbg(&bl->dev, "delay %d ms before pwm cfg\n",
				data->delay_before_pwm);
			msleep(data->delay_before_pwm);
		}

		pwm_config(data->pwm, brightness * data->period / total_steps,
			   data->period);
		pwm_enable(data->pwm);

		if (pb->power_state == 0) {
			dev_dbg(&bl->dev, "delay %d ms after pwm cfg\n",
				data->delay_after_pwm);
			msleep(data->delay_after_pwm);

			__pwm_en_gpio_active(&data->en_gpio);
		}

		pb->power_state = 1;
	}

	return 0;
}

static int owl_pwm_bl_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static const struct backlight_ops owl_pwm_bl_ops = {
	.update_status  = owl_pwm_bl_update_status,
	.get_brightness = owl_pwm_bl_get_brightness,
};

/*
 * return backlight's on/off status,
 * 0 is off, > 0 is on
 */
int owl_pwm_backlight_is_on(void)
{
	struct backlight_device *bl = owl_pwm_bl_device;
	struct owl_pwm_bl *pb;

	if (NULL == bl) {
		return -ENODEV;
	}
	pb = dev_get_drvdata(&bl->dev);

	dev_dbg(&bl->dev, "%s: power_state = %d\n", __func__, pb->power_state);

	return pb->power_state;
}
EXPORT_SYMBOL(owl_pwm_backlight_is_on);

/*
 * set backlight on/off, 0 is off, 1 is on.
 *
 * NOTE: this interface SHOULD ONLY change the on/off
 *	status, CAN NOT change the brightness value.
 */
int owl_pwm_backlight_set_onoff(int onoff)
{
	struct backlight_device *bl = owl_pwm_bl_device;

	if (NULL == bl) {
		return -ENODEV;
	}

	dev_dbg(&bl->dev, "%s: onoff = %d\n", __func__, onoff);

	bl->props.power = (onoff == 0 ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);

	backlight_update_status(bl);
	return 0;
}
EXPORT_SYMBOL(owl_pwm_backlight_set_onoff);

/*============================================================================
				platform driver
 *==========================================================================*/

static int __of_parse_pwm_gpio(struct device_node *of_node,
			       const char *propname, struct pwm_en_gpio *gpio)
{
	enum of_gpio_flags flags;
	int gpio_num;

	gpio_num = of_get_named_gpio_flags(of_node, propname, 0, &flags);
	if (gpio_num >= 0)
		gpio->gpio = gpio_num;
	else
		gpio->gpio = -1;

	gpio->active_low = flags & OF_GPIO_ACTIVE_LOW;

	pr_debug("%s, gpio = %d\n", __func__, gpio->gpio);
	pr_debug("%s, active low = %d\n", __func__, gpio->active_low);

	return 0;
}

static int __pwm_bl_parse_pdata(struct platform_device *pdev,
				struct owl_pwm_bl_pdata *data)
{
	struct device_node *of_node = pdev->dev.of_node;

	memset(data, 0, sizeof(struct owl_pwm_bl_pdata));

	if (of_property_read_u32(of_node, "total_steps", &data->total_steps))
		return -EINVAL;

	if (of_property_read_u32(of_node, "max_brightness",
				 &data->max_brightness))
		return -EINVAL;

	if (of_property_read_u32(of_node, "min_brightness",
				 &data->min_brightness))
		return -EINVAL;

	if (of_property_read_u32(of_node, "dft_brightness",
				 &data->dft_brightness))
		return -EINVAL;

	if (of_property_read_u32(of_node, "delay_bf_pwm",
				 &data->delay_before_pwm))
		data->delay_before_pwm = 0;

	if (of_property_read_u32(of_node, "delay_af_pwm",
				 &data->delay_after_pwm))
		data->delay_after_pwm = 0;

	__of_parse_pwm_gpio(of_node, "en-gpios", &data->en_gpio);

	return 0;
}

static int owl_pwm_bl_probe(struct platform_device *pdev)
{
	int ret;

	struct backlight_properties props;

	struct owl_pwm_bl *pb;
	struct owl_pwm_bl_pdata *data;

	struct device *dev = &pdev->dev;

	dev_info(dev, "%s: backlight name is %s\n", __func__, pdev->name);

	pb = devm_kzalloc(dev, sizeof(struct owl_pwm_bl), GFP_KERNEL);
	if (!pb)
		return -ENOMEM;

	data = &pb->data;
	ret = __pwm_bl_parse_pdata(pdev, data);
	if (ret < 0) {
		dev_err(dev, "failed to find platform data\n");
		return ret;
	}

	data->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(data->pwm)) {
		dev_err(dev, "unable to request PWM, trying legacy API\n");
		return PTR_ERR(data->pwm);
	}
	data->period = pwm_get_period(data->pwm);

	ret = devm_gpio_request(dev, data->en_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request en_gpio failed: %d\n", ret);
		return ret;
	}

	/*
	 * let power state be "off".
	 *
	 * If the backlight is enabled at boot stage, kernel need enable it
	 * after booting, driver just need re-enable it and change the state
	 * to "on".
	 *
	 * If the backlight is enabled at boot stage, kernel do not need
	 * enable it after booting, it will keep "on", which is boot stage's
	 * continuity.
	 * NOTE: in the case, 'owl_backlight_is_on' will return wrong value.
	 */
	pb->power_state = 0;

	/*
	 * regiter backlight device
	 */
	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.brightness = data->dft_brightness - data->min_brightness;
	props.max_brightness  = data->max_brightness - data->min_brightness;

	/* same as 'pb->power_state' */
	props.power = FB_BLANK_POWERDOWN;
	props.state |= BL_CORE_FBBLANK;
	pb->state = props.state;

	pb->bl = backlight_device_register(BACKLIGHT_NAME, dev, pb,
			&owl_pwm_bl_ops, &props);
	if (IS_ERR(pb->bl)) {
		dev_err(dev, "failed to register backlight\n");
		return PTR_ERR(pb->bl);
	}
	owl_pwm_bl_device = pb->bl;
	platform_set_drvdata(pdev, pb);

	return 0;
}

static int owl_pwm_bl_remove(struct platform_device *pdev)
{
	struct owl_pwm_bl *pb = platform_get_drvdata(pdev);
	struct owl_pwm_bl_pdata *data = &pb->data;
	struct backlight_device *bl = pb->bl;

	owl_pwm_bl_device = NULL;

	backlight_device_unregister(bl);

	devm_gpio_free(&pdev->dev, data->en_gpio.gpio);

	pwm_config(data->pwm, 0, data->period);
	pwm_disable(data->pwm);
	devm_pwm_put(&pdev->dev, data->pwm);

	devm_kfree(&pdev->dev, pb);

	return 0;
}

static void owl_pwm_bl_shutdown(struct platform_device *pdev)
{
	struct owl_pwm_bl *pb = platform_get_drvdata(pdev);
	struct owl_pwm_bl_pdata *data = &pb->data;

	dev_info(&pdev->dev, "%s\n", __func__);

	/* power off backlight */
	gpio_set_value(data->en_gpio.gpio, data->en_gpio.active_low);
}

static struct of_device_id owl_pwm_bl_of_match[] = {
	{ .compatible = "actions,owl-pwm-backlight" },
	{ }
};

static struct platform_driver owl_pwm_bl_driver = {
	.driver = {
		.name		= "owl_pwm_backlight",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(owl_pwm_bl_of_match),
	},
	.probe			= owl_pwm_bl_probe,
	.remove			= owl_pwm_bl_remove,
	.shutdown		= owl_pwm_bl_shutdown,
};

static int __init owl_pwm_bl_init(void)
{
	return platform_driver_register(&owl_pwm_bl_driver);
}

static void __exit owl_pwm_bl_exit(void)
{
	platform_driver_unregister(&owl_pwm_bl_driver);
}

#ifdef MODULE
module_init(owl_pwm_bl_init);
module_exit(owl_pwm_bl_exit);

#else
subsys_initcall(owl_pwm_bl_init);
#endif


MODULE_AUTHOR("lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL PWM Backlight Driver");
MODULE_LICENSE("GPL");
