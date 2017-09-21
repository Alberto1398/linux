/*
 * Asoc	 irkeypad driver
 *
 * Copyright (C) 2011 Actions Semiconductor, Inc
 * Author:	chenbo <chenbo@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include "atc260x-core.h"
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pwm.h>
#include <linux/owl_pm.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#define IR_PROTOCOL_9012	0x0
#define IR_PROTOCOL_NEC8	0x1
#define IR_PROTOCOL_RC5		0x2

/*debug related*/
#define IRKEYPAD_REG_PRINT		1
#define IRKEYPAD_DEBUG_INFO		0
#define	DEBUG_IRQ_HANDLER		0
#define	IRKEYPAD_SUPPORT_MOUSE	1
/*IR Power Key is Suspend Func */
#define irkeypad_log(fmt, args...) \
	printk(KERN_INFO "[irkeypad#%s] " fmt, __func__, ##args)
#if (IRKEYPAD_DEBUG_INFO == 1)
	#define irkeypad_debug_log(fmt, args...)	\
	printk(KERN_INFO "[irkeypad#%s] " fmt, __func__, ##args)
#else
	#define irkeypad_debug_log(fmt, args...)
#endif

#define res_size(res)	((res)->end - (res)->start + 1)

#define IRC_STAT_UCMP		(0x1 << 6)
#define IRC_STAT_KDCM		(0x1 << 5)
#define IRC_STAT_RCD		(0x1 << 4)
#define IRC_STAT_IIP		(0x1 << 2)
#define IRC_STAT_IREP		(0x1 << 0)

#define VOL_BASE BTN_TRIGGER_HAPPY
#define VOL_LEN (20)

#define BOARDINFO_STR_LEN (16)
#define VENDOR_CONFIG_LEN (1024)
#define IR_IDX 13
#define IR_LEN 1
#define VENDOR_ID_PATH "/sys/miscinfo/infos/boardinfo"
#define DTS_NODE_PATH "/spi@b0204000/atc2603a@00/irkeypad/config"
#define DTS_NODE_PATH_GT5 "/i2c@b0178000/atc2603c@65/irkeypad/config"
#define DTS_NODE_PATH_GT9 "/i2c@e0176000/atc2609a@65/irkeypad/config"
#define VENDOR_CONFIG_PATH "/vendor/custom/irconfig/config"

#define GPIO_NAME_POWER_LED "led_gpios"

static unsigned int reg_irc_base;
#define REG_IRC_CTL (reg_irc_base + 0x0)
#define REG_IRC_STAT (reg_irc_base + 0x1)
#define REG_IRC_CC (reg_irc_base + 0x2)
#define REG_IRC_KDC (reg_irc_base + 0x3)
#define REG_IRC_WK (reg_irc_base + 0x4)

static int power_led_gpio;
static int power_led_gpio_flag;
static bool power_led_exist;
static struct delayed_work led_blink;
static int blink_count;
static bool blinkflag;
extern int get_config(const char *key, char *buff, int len);
extern int owl_pm_wakeup_flag(void);
static char *ir_config[] = {
	"size",
	"protocol",
	"user_code",
	"wk_code",
	"period",
};

struct atc260x_dev *atc260x_dev_global;
static unsigned long power_timeout;
static bool suspending;

struct asoc_irkeypad {
	unsigned int size;
	unsigned int protocol;
	unsigned int user_code;
	unsigned int wk_code;
	unsigned int period;

	unsigned int *ir_code;
	unsigned int *key_code;

	struct atc260x_dev *atc260x_dev;

	struct input_dev *input_dev;

	struct workqueue_struct *wq;
	struct delayed_work work;
	struct tasklet_struct tasklet_pressed;

	int irq;
	int suspend_state;
	unsigned int ir_val;
	unsigned int old_key_val;
	unsigned int new_key_val;
	unsigned int old_mouse_move;
	unsigned int new_mouse_move;
	unsigned int speed_up;
	unsigned int mouse_speed;
	unsigned int is_mouse_mode;
	unsigned int abs_vol_base;
};

void irkeypad_reg_print(struct asoc_irkeypad *irkeypad)
{
#if (IRKEYPAD_REG_PRINT == 1)
	irkeypad_debug_log("register REG_IRC_CTL(0x%08x) value is 0x%08x\n",
		REG_IRC_CTL, atc260x_reg_read(irkeypad->atc260x_dev, REG_IRC_CTL));
	irkeypad_debug_log("register REG_IRC_STAT(0x%08x) value is 0x%08x\n",
		REG_IRC_STAT, atc260x_reg_read(irkeypad->atc260x_dev, REG_IRC_STAT));
	irkeypad_debug_log("register REG_IRC_CC(0x%08x) value is 0x%08x\n",
		REG_IRC_CC, atc260x_reg_read(irkeypad->atc260x_dev, REG_IRC_CC));
	irkeypad_debug_log("register REG_IRC_KDC(0x%08x) value is 0x%08x\n",
		REG_IRC_KDC, atc260x_reg_read(irkeypad->atc260x_dev, REG_IRC_KDC));
	irkeypad_debug_log("register REG_IRC_WK(0x%08x) value is 0x%08x\n",
		REG_IRC_WK, atc260x_reg_read(irkeypad->atc260x_dev, REG_IRC_WK));
#endif
}

static ssize_t store_led_stat(struct device *device, struct device_attribute *attr, const char *buf, size_t count)
{
	char *endp;
	int stat = simple_strtoul(buf, &endp, 0);
	pr_err("%s, stat=%d\n", __func__, stat);

	switch (stat) {
	case 0:
		cancel_delayed_work(&led_blink);
		break;
	case 1:
		schedule_delayed_work(&led_blink, msecs_to_jiffies(100));
		break;
	case 2:
		gpio_direction_output(power_led_gpio, 1);
		break;
	case 3:
		gpio_direction_output(power_led_gpio, 0);
		break;
	default:
		pr_err("stat=%d isinvalid parament!\n", stat);
	}
	return count;
}

struct pwm_device *pwm;

static void led_blink_control(struct work_struct *work)
{
	int delay_ns;

	delay_ns = blink_count*10000000/64;
	pwm_config(pwm, delay_ns, 10000000);

	if (0 == blink_count || 64 == blink_count)
		blinkflag = !blinkflag;

	if (blinkflag)
		blink_count = blink_count + 2;
	else
		blink_count = blink_count - 2;

	schedule_delayed_work(&led_blink, msecs_to_jiffies(100));
}

static DEVICE_ATTR(show_led, S_IRUGO|S_IWUSR, NULL, store_led_stat);


static void asoc_irkeypad_enable_irq(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;
	atc260x_reg_setbits(atc260x_dev, REG_IRC_CTL,
		0x0004, 0x0004);
}

static void asoc_irkeypad_disable_irq(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;
	atc260x_reg_setbits(atc260x_dev, REG_IRC_CTL,
		0x0004, 0x0000);
}

static inline void enable_pmu_irc(struct atc260x_dev *atc260x_dev)
{
	switch (atc260x_dev->ic_type) {
	case ATC260X_ICTYPE_2603A:
		/*irq*/
		atc260x_reg_setbits(atc260x_dev, ATC2603A_INTS_MSK,
			0x0400, 0x0400);

		/*mfp*/
		atc260x_reg_setbits(atc260x_dev, ATC2603A_PMU_SYS_CTL3,
			0x0008, 0x0000);

		/*wake up*/
		atc260x_reg_setbits(atc260x_dev, ATC2603A_PMU_SYS_CTL0,
			0x0020, 0x0020);
		break;
	case ATC260X_ICTYPE_2603C:
		/*irq*/
		atc260x_reg_setbits(atc260x_dev, ATC2603C_INTS_MSK,
			0x0100, 0x0100);
		/*wake up*/
		atc260x_reg_setbits(atc260x_dev, ATC2603C_PMU_SYS_CTL0,
			0x0020, 0x0020);
		/*mfp*/
		atc260x_reg_setbits(atc260x_dev, ATC2603C_PMU_MUX_CTL0,
			0x3000, 0x1000);
		break;
	case ATC260X_ICTYPE_2609A:
		/*irq*/
		atc260x_reg_setbits(atc260x_dev, ATC2609A_INTS_MSK,
			0x0100, 0x0100);
		/*mfp*/
		atc260x_reg_setbits(atc260x_dev, ATC2609A_PMU_SYS_CTL4,
			0x301C, 0x000C);
		/*wake up*/
		atc260x_reg_setbits(atc260x_dev, ATC2609A_PMU_SYS_CTL0,
			0x0020, 0x0020);
		break;
	}
}

static void asoc_irkeypad_config(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;

	enable_pmu_irc(atc260x_dev);
	/*IRC enable*/
	atc260x_reg_setbits(atc260x_dev, REG_IRC_CTL,
		0x0008, 0x0008);

	atc260x_reg_setbits(atc260x_dev, REG_IRC_STAT,
		0x0014, 0x0014);

	/*set IRC code mode,NEC default*/
	atc260x_reg_setbits(atc260x_dev, REG_IRC_CTL,
		0x0003, irkeypad->protocol);
	/*set IRC cc,NEC default*/
	atc260x_reg_setbits(atc260x_dev, REG_IRC_CC,
		0xffff, irkeypad->user_code);

	/*set IRC wakeup*/
	atc260x_reg_setbits(atc260x_dev, REG_IRC_WK,
		0xffff, ((~irkeypad->wk_code) << 8) | (irkeypad->wk_code));
}

static inline int asoc_irkeypad_convert(unsigned int protocol,
		unsigned int *val)
{
	switch (protocol) {
	case IR_PROTOCOL_9012:
		*val &= 0x00ff;
		break;

	case IR_PROTOCOL_NEC8:
		*val &= 0x00ff;
		break;

	case IR_PROTOCOL_RC5:
		*val &= 0x003f;
		break;

	default:
		break;
	}

	return 0;
}

static void asoc_irkeypad_scan(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;

	irkeypad->ir_val = atc260x_reg_read(atc260x_dev, REG_IRC_KDC);

	asoc_irkeypad_convert(irkeypad->protocol, &(irkeypad->ir_val));

	return;
}

static void asoc_irkeypad_map(struct asoc_irkeypad *irkeypad)
{
	int i;
	unsigned int *ir_val;

	if (irkeypad->ir_val >= (irkeypad->abs_vol_base)) {
		irkeypad->new_key_val = VOL_BASE + ((irkeypad->ir_val - irkeypad->abs_vol_base) / 5);
		return;
	} else {
		for (i = 0; i < irkeypad->size; i++) {
			ir_val = irkeypad->ir_code + i;
			asoc_irkeypad_convert(irkeypad->protocol, ir_val);
			if (*ir_val == irkeypad->ir_val) {
				irkeypad->new_key_val = *(irkeypad->key_code + i);
				return;
			}
		}
	}

	irkeypad_debug_log("irkeypad map failed, ir_val = 0x%x\n", irkeypad->ir_val);

	return;
}

static void asoc_irkeypad_report_released(struct asoc_irkeypad *irkeypad)
{
	struct input_dev *input_dev;

	asoc_irkeypad_disable_irq(irkeypad);
#if DEBUG_IRQ_HANDLER
	irkeypad_debug_log("start : old_key_val = %d \n", irkeypad->old_key_val);
#endif
	input_dev = irkeypad->input_dev;
	if (irkeypad->old_key_val  != KEY_RESERVED) {
		input_report_key(input_dev, irkeypad->old_key_val, 0);
		input_sync(input_dev);
		irkeypad_log("key: %d %s\n", irkeypad->old_key_val, "released");
		if (irkeypad->old_key_val == KEY_POWER) {
			power_timeout = jiffies + HZ*5;
		}
		irkeypad->new_key_val = KEY_RESERVED;
		irkeypad->old_key_val = irkeypad->new_key_val;
	}

	asoc_irkeypad_enable_irq(irkeypad);

#if DEBUG_IRQ_HANDLER
	irkeypad_debug_log("end\n");
#endif
}

static void	 asoc_irkeypad_report_pressed(struct asoc_irkeypad *irkeypad)
{
	unsigned int changed;
	struct input_dev *input_dev = irkeypad->input_dev;

#if DEBUG_IRQ_HANDLER
	irkeypad_debug_log("start\n");
#endif

	asoc_irkeypad_map(irkeypad);

#if (IRKEYPAD_SUPPORT_MOUSE == 1)
	if (irkeypad->is_mouse_mode == 1) {
		if (irkeypad->speed_up == 1) {
			irkeypad->new_mouse_move = irkeypad->new_key_val;
			if (irkeypad->old_mouse_move == irkeypad->new_mouse_move) {
				irkeypad->mouse_speed += 2;
				if (irkeypad->mouse_speed >= 50) {
					irkeypad->mouse_speed = 50;
				}
			} else {
				irkeypad->mouse_speed = 10;
			}
		}
		switch (irkeypad->new_key_val) {
		case KEY_UP:
				input_report_rel(input_dev, REL_X, 0);
				input_report_rel(input_dev, REL_Y, -irkeypad->mouse_speed);
				input_sync(input_dev);
				irkeypad->speed_up = 1;
				break;
		case KEY_DOWN:
				input_report_rel(input_dev, REL_X, 0);
				input_report_rel(input_dev, REL_Y, irkeypad->mouse_speed);
				input_sync(input_dev);
				irkeypad->speed_up = 1;
				break;
		case KEY_LEFT:
				input_report_rel(input_dev, REL_X, -irkeypad->mouse_speed);
				input_report_rel(input_dev, REL_Y, 0);
				input_sync(input_dev);
				irkeypad->speed_up = 1;
				break;
		case KEY_RIGHT:
				input_report_rel(input_dev, REL_X, irkeypad->mouse_speed);
				input_report_rel(input_dev, REL_Y, 0);
				input_sync(input_dev);
				irkeypad->speed_up = 1;
				break;
		default:
				irkeypad->speed_up = 0;
				irkeypad->mouse_speed = 10;
				break;
		}

		if (irkeypad->speed_up == 1) {
			irkeypad->old_mouse_move = irkeypad->new_key_val;
			irkeypad->new_key_val = KEY_RESERVED;
			irkeypad->old_key_val = irkeypad->new_key_val ;
		}
	}
#endif

	changed = irkeypad->old_key_val ^ irkeypad->new_key_val;
	if (changed) {
#if (IRKEYPAD_SUPPORT_MOUSE == 1)
		if (irkeypad->new_key_val == KEY_MOVE) {
			  irkeypad->speed_up	 = 0;
			  irkeypad->mouse_speed	 = 10;
			  if (irkeypad->is_mouse_mode == 0) {
				   irkeypad_debug_log("irkeypad mouse mode enable \n");
				   irkeypad->is_mouse_mode = 1;
				   input_report_rel(input_dev, REL_X, 1);
				   input_report_rel(input_dev, REL_Y, 1);
				   input_report_key(input_dev, BTN_LEFT, 0);
				   input_report_key(input_dev, BTN_RIGHT, 0);
				   irkeypad->old_key_val = irkeypad->new_key_val ;
				   input_sync(input_dev);
				   return;
			   } else {
				   irkeypad->is_mouse_mode = 0;
				   irkeypad_debug_log("irkeypad key mode enable \n");
			   }
		}

		if (irkeypad->is_mouse_mode == 1) {
			switch (irkeypad->new_key_val) {
			case KEY_SELECT:
				irkeypad->new_key_val = BTN_LEFT;
				break;

			case KEY_BACK:
				irkeypad->new_key_val = BTN_RIGHT;
				break;

			default:
				break;
			}
		}

		if (!(irkeypad->old_key_val ^ irkeypad->new_key_val))
		  return;
#endif

		if (irkeypad->new_key_val != KEY_RESERVED) {
			input_report_key(input_dev, irkeypad->new_key_val, 1);
			input_sync(input_dev);
			irkeypad_log("key: %d %s\n", irkeypad->new_key_val, "pressed");
		}

		irkeypad->old_key_val = irkeypad->new_key_val;
	}
	return;
}

static void asoc_irkeypad_tasklet_pressed(unsigned long data)
{
	struct asoc_irkeypad *irkeypad = (struct asoc_irkeypad *)data;

	asoc_irkeypad_report_pressed(irkeypad);
}

static void asoc_irkeypad_work_released(struct work_struct *work)
{
	struct asoc_irkeypad *irkeypad =
		container_of(work, struct asoc_irkeypad, work.work);

	asoc_irkeypad_report_released(irkeypad);
}

static inline unsigned int asoc_irkeypad_get_pend(struct asoc_irkeypad *irkeypad)
{
	unsigned int ret = (atc260x_reg_read(irkeypad->atc260x_dev,
			REG_IRC_STAT) & IRC_STAT_IIP ? 1 : 0);
	return ret;
}

static inline unsigned int asoc_irkeypad_get_irep
	(struct asoc_irkeypad *irkeypad, unsigned int stat)
{
	unsigned int ret = (stat & IRC_STAT_IREP) ? 1 : 0;
	return ret;
}

/*ignor keys (except POWER_KEY) when suspending*/
static int key_filter(struct asoc_irkeypad *irkeypad)
{
	int i;
	unsigned int *ir_val;
	unsigned int keycode = 0;

	if (!suspending)
		return 0;
	/*suspending time is too short ( < 1 second)*/
	if (time_before(jiffies, power_timeout))
		return -1;

	for (i = 0; i < irkeypad->size; i++) {
		ir_val = irkeypad->ir_code + i;
		asoc_irkeypad_convert(irkeypad->protocol, ir_val);
		if (*ir_val == irkeypad->ir_val) {
			keycode = *(irkeypad->key_code + i);
			if (keycode == KEY_POWER)
				return 0;
			break;
		}
	}

	return -1;
}


static irqreturn_t asoc_irkeypad_irq_handler(int irq, void *dev_id)
{
	struct asoc_irkeypad *irkeypad = dev_id;
	unsigned int stat, old_ir_val;

#if DEBUG_IRQ_HANDLER
	irkeypad_debug_log("[start]\n");
#endif

	stat = atc260x_reg_read(irkeypad->atc260x_dev, REG_IRC_STAT);
	atc260x_reg_write(irkeypad->atc260x_dev, REG_IRC_STAT, stat);/*Attention!!!Use write here instead of set_bits*/

	old_ir_val = irkeypad->ir_val;
	asoc_irkeypad_scan(irkeypad);

	if (key_filter(irkeypad))
		return IRQ_HANDLED;

#if DEBUG_IRQ_HANDLER
	irkeypad_debug_log("IRC_STAT : 0x%x\n", stat);
#endif

	if (asoc_irkeypad_get_irep(irkeypad, stat)) {
		if ((irkeypad->ir_val == old_ir_val) || (old_ir_val == 0)) {
			irkeypad_log("invalide code, old_ir_val = 0x%x, new_ir_val = 0x%x, IRC_STAT = 0x%x\n",
					old_ir_val, irkeypad->ir_val, stat);
#if DEBUG_IRQ_HANDLER
			irkeypad_reg_print(irkeypad);
#endif
			return IRQ_HANDLED;
		}
	}

#if DEBUG_IRQ_HANDLER
	irkeypad_debug_log("old_key_val = %d, new_key_val = %d \n",
				irkeypad->old_key_val, irkeypad->new_key_val);
#endif

	if (stat & IRC_STAT_RCD) {
		if ((irkeypad->new_key_val == KEY_RESERVED) && (irkeypad->speed_up == 0)) {
			irkeypad_log("invalide repeat, IRC_STAT = 0x%x\n", stat);
		} else {
			cancel_delayed_work(&irkeypad->work);
#if (IRKEYPAD_SUPPORT_MOUSE == 1)
			tasklet_schedule(&irkeypad->tasklet_pressed);
#endif
			queue_delayed_work(irkeypad->wq, &irkeypad->work,
					msecs_to_jiffies(irkeypad->period));
		}
	} else {
		cancel_delayed_work(&irkeypad->work);
		if (irkeypad->new_key_val != KEY_RESERVED)
			asoc_irkeypad_report_released(irkeypad);
		tasklet_schedule(&irkeypad->tasklet_pressed);
		queue_delayed_work(irkeypad->wq, &irkeypad->work,
				msecs_to_jiffies(irkeypad->period));
	}

	return IRQ_HANDLED;
}


static void asoc_irkeypad_start(struct asoc_irkeypad *irkeypad)
{
	asoc_irkeypad_config(irkeypad);
	asoc_irkeypad_enable_irq(irkeypad);
}

static void asoc_irkeypad_stop(struct asoc_irkeypad *irkeypad)
{
	cancel_delayed_work(&irkeypad->work);
	asoc_irkeypad_disable_irq(irkeypad);
}

static int asoc_irkeypad_open(struct input_dev *dev)
{
	struct asoc_irkeypad *irkeypad = input_get_drvdata(dev);

	asoc_irkeypad_start(irkeypad);

	return 0;
}

static void asoc_irkeypad_close(struct input_dev *dev)
{

	struct asoc_irkeypad *irkeypad = input_get_drvdata(dev);

	asoc_irkeypad_stop(irkeypad);

	return;
}

static ssize_t atv5201_irkeypad_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	char *endp;
	struct asoc_irkeypad *irkeypad = dev_get_drvdata(dev);
	int cmd = simple_strtoul(buf, &endp, 0);
	size_t size = endp - buf;

	if (*endp && isspace(*endp))
		size++;
	if (size != count)
		return -EINVAL;

	switch (cmd) {
	case 0:
		irkeypad_reg_print(irkeypad);
		break;

	default:
		break;
	}

	return count;
}

static ssize_t atv5201_irkeypad_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	ssize_t ret_size = 0;

	return ret_size;
}

static ssize_t atv5201_irkeypad_mousespeed_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	char *endp;
	size_t size;

	simple_strtoul(buf, &endp, 0);
	size = endp - buf;

	if (*endp && isspace(*endp))
		size++;
	if (size != count)
		return -EINVAL;

	return count;
}

static ssize_t atv5201_irkeypad_mousespeed_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct asoc_irkeypad *irkeypad = dev_get_drvdata(dev);
	unsigned int offset = 0;

	offset += snprintf(&buf[offset], PAGE_SIZE - offset, "%d\n",
					irkeypad->mouse_speed);
	return offset;
}

static struct device_attribute irkeypad_attrs[] = {
	__ATTR(mouseSpeed, S_IRUGO | S_IWUSR, atv5201_irkeypad_mousespeed_show, atv5201_irkeypad_mousespeed_store),
	__ATTR(queueSize, S_IRUGO | S_IWUSR, atv5201_irkeypad_show, atv5201_irkeypad_store),
};

ssize_t irkeypad_read_file(char *path, char *buf, ssize_t len)
{
	ssize_t ret = 0;
	struct file *filp = NULL;
	mm_segment_t fs;
	loff_t pos = 0;
	filp = filp_open(path, O_RDONLY, 0644);
	if (IS_ERR(filp)) {
		irkeypad_log("open [%s] failed!\n", path);
		ret = -ENOENT;
		goto READ_FILE_OUT;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_read(filp, buf, len, &pos);
	if (ret > 0) {
		*(buf + ret) = '\0';
	} else {
		*buf = '\0';
	}

	filp_close(filp, NULL);
	set_fs(fs);
READ_FILE_OUT:
	return ret;
}

ssize_t asoc_irkeypad_get_vendorid(char *ir_info)
{
	ssize_t ret = 0, str_len;
	int val = -1;

	char *str_boardinfo = kzalloc(BOARDINFO_STR_LEN + 1, GFP_KERNEL);
	if (str_boardinfo == NULL) {
		irkeypad_log("kzalloc str_boardinfo failed!!!, try to use default config.\n");
		ret = -1;
		goto VENDORID_OUT;
	}

	str_len = irkeypad_read_file(VENDOR_ID_PATH,
				str_boardinfo, BOARDINFO_STR_LEN);
	irkeypad_log("get str_boardinfo str: %s\n", str_boardinfo);
	if ((str_len != BOARDINFO_STR_LEN)) {
		irkeypad_log("get vid failed, use default config.\n");
		ret = -1;
		goto VENDORID_FREE;
	}

	strncpy(ir_info, str_boardinfo + (char)IR_IDX, IR_LEN);
	*(ir_info + IR_LEN) = '\0';
	irkeypad_log("ir_info is : %s\n", ir_info);
	ret = kstrtoint(ir_info, 0, &val);
	if (val <= 0)
		ret = -1;
VENDORID_FREE:
	kfree(str_boardinfo);
VENDORID_OUT:
	return ret;

}

ssize_t irkeypad_get_int(const char *file,
	const char *key, int *value)
{
	ssize_t ret = -1;
	char *start;
	char *str = kzalloc(VENDOR_CONFIG_LEN, GFP_KERNEL);
	if (str == NULL) {
		irkeypad_log("alloc mem failed\n");
		return ret;
	}

	memcpy(str, file, VENDOR_CONFIG_LEN);
	start = strstr(str, key);
	if (start) {
		char *line = strsep(&start, "\n");
		strsep(&line, "=");
		if (line) {
			ret = kstrtoint(line, 0, value);
		}
	}
	kfree(str);
	return ret;
}

ssize_t irkeypad_get_int_array(const char *file,
	const char *key, u32 *value, int size)
{
	ssize_t ret = -1;
	char *start;
	char *str = kzalloc(VENDOR_CONFIG_LEN, GFP_KERNEL);
	if (str == NULL) {
		irkeypad_log("alloc mem failed\n");
		return ret;
	}

	memcpy(str, file, VENDOR_CONFIG_LEN);
	start = strstr(str, key);
	if (start) {
		char *line = strsep(&start, "\n");
		char *token;
		int cnt = 0;
		strsep(&line, "=");
		while (line) {
			token = strsep(&line, " ");
			ret = kstrtou32(strstrip(token), 0, value);
			if (ret == 0) {
				value++;
				cnt++;
			}
		}
		if (cnt != size) {
			irkeypad_log("get int array error, size not matched!\n");
			ret = -1;
		}
	}
	kfree(str);
	return ret;
}

static inline int get_irc_base(unsigned char ic_type)
{
	switch (ic_type) {
	case ATC260X_ICTYPE_2603A:
		return 0x60;
	case ATC260X_ICTYPE_2603C:
		return 0x80;
	case ATC260X_ICTYPE_2609A:
		return 0x90;
	default:
		printk(KERN_ERR"Fatal error! Unkown pmu ic type!\n");
		return 0;
	}
}

char *get_dts_path(u8 ic_type)
{
	switch (ic_type) {
	case ATC260X_ICTYPE_2603A:
		return DTS_NODE_PATH_GT5;
	case ATC260X_ICTYPE_2603C:
		return DTS_NODE_PATH;
	case ATC260X_ICTYPE_2609A:
		return DTS_NODE_PATH_GT9;
	default:
		printk(KERN_ERR"Fatal error! Unkown pmu ic type!\n");
		return NULL;
	}
}

static int asoc_irkeypad_probe(struct platform_device *pdev)
{
	struct atc260x_dev *atc260x_dev = dev_get_drvdata(pdev->dev.parent);
	struct asoc_irkeypad *irkeypad;
	struct input_dev *input_dev;
	struct device_node *dnp = pdev->dev.of_node;
	int ret = 0;
	int i;
	unsigned int blink;

	struct device_node *dnp_config = NULL;
	char ir_id[IR_LEN + 1] = "";
	char irconfig_path[IR_LEN + sizeof(DTS_NODE_PATH) + 10] = VENDOR_CONFIG_PATH;

	bool has_vendor_config = false;
	bool has_vendor_id = (asoc_irkeypad_get_vendorid(ir_id) == 0);
	char *vendor_config = kzalloc(VENDOR_CONFIG_LEN + 1, GFP_KERNEL);

	atc260x_dev_global = atc260x_dev;
	reg_irc_base = get_irc_base(atc260x_dev->ic_type);
	if (has_vendor_id)
		strcat(strcat(irconfig_path, "-"), ir_id);
	strcat(irconfig_path, ".cfg");
	irkeypad_log("vendor config path is %s\n", irconfig_path);

	if (vendor_config) {
		ssize_t str_len = irkeypad_read_file(irconfig_path,
						vendor_config, VENDOR_CONFIG_LEN);
		if (str_len < 0)
			irkeypad_log("get vendor config failed, try dts config!\n");
		else
			has_vendor_config = true;
	} else {
		dev_err(&pdev->dev, "failed to allocate vendor config\n");
		ret = -ENOMEM;
		return ret;
	}

	if (!has_vendor_config) {
		strcpy(irconfig_path, get_dts_path(atc260x_dev->ic_type));
		if (has_vendor_id)
			strcat(strcat(irconfig_path, "-"), ir_id);

		dnp_config = of_find_node_by_path(irconfig_path);
		if (dnp_config == NULL) {
			irkeypad_log("get dnp_config failed: %s!\n",
							irconfig_path);
			dnp_config = dnp;
		}
	}

	blink = 0;
	ret = of_property_read_u32(dnp, "led_blink", &blink);
	if (ret != 0) {
		pr_err("[%s]get led_blink fail!\n", __func__);
		ret = 0;
	}

	pr_debug("blink = %d\n", blink);

	if (blink) {
		int enableres = 0;
		int configres = 0;
		pr_debug("###	 will request pwm ####\n");
		pwm = pwm_request(3, "Breathing LED");

		pr_debug("### cr##  request res 0x%lx\n", (unsigned long)pwm);
		if (IS_ERR(pwm))
			return -1;

		enableres = pwm_enable(pwm);
		configres = pwm_config(pwm, 900000000, 1000000000);
		writel_relaxed(readl_relaxed((void __iomem *)0xb01c0010) & ~(0x1 << 8),
						(void __iomem *)0xb01c0010);
		pr_debug("### configres ### %d  enableres is %d\n", configres, enableres);

		INIT_DELAYED_WORK(&led_blink, led_blink_control);
		schedule_delayed_work(&led_blink, msecs_to_jiffies(100));

	#if 0
		if (sysfs_create_file(power_kobj, &dev_attr_show_led.attr) < 0)
			printk(KERN_ERR"sys create file error.func:%s\n", __func__);
	#endif

		power_led_gpio = of_get_named_gpio_flags(dnp, GPIO_NAME_POWER_LED, 0,
						(enum of_gpio_flags *)&power_led_gpio_flag);
		if (power_led_gpio < 0)
			irkeypad_debug_log("get gpio [%s] fail\n",
				GPIO_NAME_POWER_LED);
		power_led_gpio_flag	 &= OF_GPIO_ACTIVE_LOW;

		if (gpio_request(power_led_gpio, GPIO_NAME_POWER_LED) < 0) {
			irkeypad_debug_log("power led exist\n");
			power_led_exist = false;
		} else {
			irkeypad_debug_log("power led is not exist\n");
			power_led_exist = true;
		}
	}

	irkeypad = kzalloc(sizeof(struct asoc_irkeypad), GFP_KERNEL);
	if (irkeypad == NULL) {
		dev_err(&pdev->dev, "failed to allocate irkeypad driver data\n");
		ret = -ENOMEM;
		goto free_config;
	}
	platform_set_drvdata(pdev, irkeypad);

	irkeypad->atc260x_dev = atc260x_dev;
	irkeypad->old_key_val = KEY_RESERVED;
	irkeypad->old_mouse_move = KEY_RESERVED;
	irkeypad->mouse_speed  = 10;

	for (i = 0; i < ARRAY_SIZE(ir_config); i++) {
		if (has_vendor_config)
			ret = irkeypad_get_int(vendor_config,
					ir_config[i], (&(irkeypad->size) + i));
		else
			ret = of_property_read_u32(dnp_config,
					ir_config[i], (&(irkeypad->size) + i));
		if (ret != 0) {
			irkeypad_log("get %d fail!\n", i);
			ret = -EINVAL;
			goto free;
		}
	}

	irkeypad->ir_code =
		kzalloc(sizeof(unsigned int) * (irkeypad->size), GFP_KERNEL);
	if (has_vendor_config)
		ret = irkeypad_get_int_array(vendor_config,
			"ir_code", (u32 *)irkeypad->ir_code,
			(irkeypad->size));
	else
		ret = of_property_read_u32_array(dnp_config,
			"ir_code", (u32 *)irkeypad->ir_code,
			(irkeypad->size));

	if (ret != 0) {
		irkeypad_log("get ir_code fail!\n");
		ret = -EINVAL;
		goto free;
	}


	irkeypad->key_code =
		kzalloc(sizeof(unsigned int) * (irkeypad->size), GFP_KERNEL);
	if (has_vendor_config) {
		ret = irkeypad_get_int_array(vendor_config,
			"key_code", (u32 *)irkeypad->key_code,
			(irkeypad->size));
	} else {
		ret = of_property_read_u32_array(dnp_config,
			"key_code", (u32 *)irkeypad->key_code,
			(irkeypad->size));
	}
	if (ret != 0) {
		irkeypad_log("get  key_code fail!\n");
		ret = -EINVAL;
		goto free_ir_code;
	}

	/*get absolute volume config for haixin remote.*/
	if (has_vendor_config)
		ret = irkeypad_get_int(vendor_config,
			"abs_vol_base", &(irkeypad->abs_vol_base));
	else
		ret = of_property_read_u32(dnp,
			"abs_vol_base", &(irkeypad->abs_vol_base));
	if (ret != 0) {
		irkeypad_log("no config for abs_vol_base.!\n");
		irkeypad->abs_vol_base = 0xFFFF;
		ret = 0;
	}

	/*
	 * the WORK work is in charge of reporting key release and
	 * the WORK work_pressed reports key pressed.
	 */
	tasklet_init(&irkeypad->tasklet_pressed,
		asoc_irkeypad_tasklet_pressed, (unsigned long)irkeypad);
	INIT_DELAYED_WORK(&irkeypad->work, asoc_irkeypad_work_released);
	irkeypad->wq = create_workqueue("atv5201-IRKEYPAD");

	/*
	 * irq related
	 */
	irkeypad->irq = platform_get_irq(pdev, 0);
	irkeypad_log("get irq: %d\n", irkeypad->irq);
	if (irkeypad->irq < 0) {
		dev_err(&pdev->dev, "failed to get irkeypad irq\n");
		ret = -ENXIO;
		goto free_key_code;

	}
	ret = request_threaded_irq(irkeypad->irq,
			NULL, asoc_irkeypad_irq_handler,
		IRQF_TRIGGER_HIGH, "atc260x-irkeypad", irkeypad);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irkeypad IRQ\n");
		goto free_key_code;
	}
	/*
	 * input_dev related
	 */
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate	 irkeypad input device\n");
		ret = -ENOMEM;
		goto free_irq;
	}
	input_dev->name = pdev->name;
	input_dev->phys = "asoc-irkeypad/input0";
	input_dev->open = asoc_irkeypad_open;
	input_dev->close = asoc_irkeypad_close;
	input_dev->dev.parent = &pdev->dev;
	input_dev->keycodemax = irkeypad->size;
	input_dev->id.bustype = BUS_HOST;

	irkeypad->input_dev = input_dev;
	input_set_drvdata(input_dev, irkeypad);
#if (IRKEYPAD_SUPPORT_MOUSE == 1)
	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] =
		BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	irkeypad->is_mouse_mode = 0;
#else
	input_dev->evbit[0] = BIT(EV_KEY);
#endif

	if (irkeypad->abs_vol_base != 0xffff) {
		for (i = 0; i < VOL_LEN; i++)
			__set_bit(VOL_BASE + i, input_dev->keybit);
	}

	for (i = 0; i < input_dev->keycodemax; i++) {
		if (irkeypad->key_code[i] != KEY_RESERVED)
			__set_bit(irkeypad->key_code[i],
				input_dev->keybit);

	}
	ret = input_register_device(input_dev);
	if (ret) {

		dev_err(&pdev->dev, "failed to register input device\n");
		goto free_input;

	}

	for (i = 0; i < ARRAY_SIZE(irkeypad_attrs); i++) {
		ret = device_create_file(&pdev->dev, &irkeypad_attrs[i]);
		if (ret) {
			pr_err("failed to create sysfs file\n");
			break;
		}
	}

	power_timeout = jiffies;
	asoc_irkeypad_open(input_dev);
	return 0;

free_input:
	input_free_device(input_dev);

free_irq:
	free_irq(irkeypad->irq, pdev);
	platform_set_drvdata(pdev, NULL);

free_key_code:
	kfree(irkeypad->key_code);

free_ir_code:
	kfree(irkeypad->ir_code);

free:
	kfree(irkeypad);

free_config:
	kfree(vendor_config);

	return ret;
}

static int asoc_irkeypad_remove(struct platform_device *pdev)
{
	int i;
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);

	asoc_irkeypad_close(irkeypad->input_dev);
	free_irq(irkeypad->irq, irkeypad);
	input_unregister_device(irkeypad->input_dev);
	input_free_device(irkeypad->input_dev);
	input_set_drvdata(irkeypad->input_dev, NULL);

	for (i = 0; i < ARRAY_SIZE(irkeypad_attrs); i++)
		device_remove_file(&pdev->dev, &irkeypad_attrs[i]);
	destroy_workqueue(irkeypad->wq);
	platform_set_drvdata(pdev, NULL);

	kfree(irkeypad->key_code);
	kfree(irkeypad->ir_code);
	kfree(irkeypad);

	return 0;
}


void atc260x_irkeypad_reset_irc(void)
{
	irkeypad_log("reset IRC protocol!\n");
	atc260x_reg_setbits(atc260x_dev_global, REG_IRC_CTL,
		0x0003, 0x0002);
	msleep(100);
	atc260x_reg_setbits(atc260x_dev_global, REG_IRC_CTL,
		0x0003, 0x0001);
}

static void atc260x_irkeypad_shutdown(struct platform_device *pdev)
{
	atc260x_irkeypad_reset_irc();
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void irkeypad_early_suspend(struct early_suspend *h)
{
	suspending = true;
	irkeypad_debug_log("[finished]\n");
}

static void irkeypad_late_resume(struct early_suspend *h)
{
	suspending = false;
	power_timeout = jiffies + HZ*5;
	irkeypad_debug_log("[finished]\n");
}

static struct early_suspend irkeypad_early_suspend_desc = {
		.level = 0,
		.suspend = irkeypad_early_suspend,
		.resume = irkeypad_late_resume,
};
#endif

#ifdef CONFIG_PM
static int asoc_irkeypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);
	asoc_irkeypad_stop(irkeypad);
	irkeypad->suspend_state = 1;
	atc260x_irkeypad_reset_irc();
	return 0;
}
static int	asco_irkeypad_suspend_prepare(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);
	pr_info("%s() enter\n", __func__);
	irkeypad->suspend_state = 0;
	return 0;
}
static void asoc_irkeypad_resume_complete(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = irkeypad->input_dev;
	if (irkeypad->suspend_state != 0) {
		uint wakeup_flag = owl_pm_wakeup_flag();
		pr_info("wakeup_flag=%u\n",
			wakeup_flag & OWL_PMIC_WAKEUP_SRC_IR);
		if (wakeup_flag & OWL_PMIC_WAKEUP_SRC_IR) {
			input_report_key(input_dev, KEY_POWER, 1);
			input_report_key(input_dev, KEY_POWER, 0);
			input_sync(input_dev);
		}
	}
}

static int asoc_irkeypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);

	asoc_irkeypad_start(irkeypad);

	return 0;
}

static const struct dev_pm_ops asoc_irkeypad_pm_ops = {
	.suspend	= asoc_irkeypad_suspend,
	.resume		= asoc_irkeypad_resume,
	.prepare	= asco_irkeypad_suspend_prepare,
	.complete	= asoc_irkeypad_resume_complete,
};
#endif


static const struct of_device_id atc260x_ir_match[] = {
	{ .compatible = "actions,atc2603a-ir", },
	{ .compatible = "actions,atc2603c-ir", },
	{ .compatible = "actions,atc2609a-ir", },
	{},
};
MODULE_DEVICE_TABLE(of, atc260x_ir_match);

static struct platform_driver asoc_irkeypad_driver = {
	.probe		= asoc_irkeypad_probe,
	.remove		= asoc_irkeypad_remove,
	.driver		= {
		.name	= "atc260x-ir",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(atc260x_ir_match),
#ifdef CONFIG_PM
		.pm	= &asoc_irkeypad_pm_ops,
#endif
	},
	.shutdown = atc260x_irkeypad_shutdown,
};

static int __init asoc_irkeypad_init(void)
{
	int ret;

	ret = platform_driver_register(&asoc_irkeypad_driver);
	if (ret != 0)
		pr_err("Failed to register ATC260X irkeypad driver: %d\n", ret);

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&irkeypad_early_suspend_desc);
#endif
	return ret;
}

static void __exit asoc_irkeypad_exit(void)
{
	platform_driver_unregister(&asoc_irkeypad_driver);

#ifdef CONFIG_HAS_EARLYSUSPEND
	  /*if config for early suspen , unregister it */
	unregister_early_suspend(&irkeypad_early_suspend_desc);
#endif
}

module_init(asoc_irkeypad_init);
module_exit(asoc_irkeypad_exit);

MODULE_DESCRIPTION("Asoc irkeypad controller drvier");
MODULE_AUTHOR("ZhigaoNi <zhigaoni@actions-semi.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:asoc-irkeypad");
