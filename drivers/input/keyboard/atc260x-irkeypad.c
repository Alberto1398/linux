/*
 * Asoc  irkeypad driver
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
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mfd/atc260x/atc260x.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/input/owl-irkey.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#define	IR_PROTOCOL_9012	0x0
#define	IR_PROTOCOL_NEC8	0x1
#define	IR_PROTOCOL_RC5		0x2

/*debug related*/
#define	IRKEYPAD_DEBUG			1
#define	IRKEYPAD_REG_PRINT		0
#define	IRKEYPAD_DEBUG_INFO		0
#define	DEBUG_IRQ_HANDLER		0
#define	IRKEYPAD_SUPPORT_MOUSE	1
/*IR Power Key is Suspend Func */
#if (IRKEYPAD_DEBUG_INFO == 1)
	#define IRKEYPAD_INFO(fmt, args...)	\
	printk(KERN_INFO "irkeypad_drv: " fmt, ##args)
#else
	#define IRKEYPAD_INFO(fmt, args...)
#endif

#define res_size(res)	((res)->end - (res)->start + 1)

#define IRC_STAT_UCMP 		(0x1 << 6)
#define IRC_STAT_KDCM 		(0x1 << 5)
#define IRC_STAT_RCD 		(0x1 << 4)
#define IRC_STAT_IIP 		(0x1 << 2)
#define IRC_STAT_IREP 		(0x1 << 0)

#define BOARDINFO_STR_LEN (16)
#define IR_IDX 13
#define IR_LEN 1
#define GPIO_NAME_POWER_LED "power_led"
extern int read_mi_item(char *name, void *buf, unsigned int count);
extern int write_mi_item(char *name, void *buf, unsigned int count);
static int gpio_power_led_pin;
static bool led_blink_enable;

struct atc260x_dev *atc260x_dev_global;
static unsigned long power_timeout;
static bool suspending;
static bool has_canceled_pending;
struct asoc_irkeypad {
	unsigned int size;
	unsigned int ir_ch;
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
	unsigned int ir_val;
	unsigned int old_key_val;
	unsigned int new_key_val;
	unsigned int old_mouse_move;
	unsigned int new_mouse_move;
	unsigned int speed_up;
	unsigned int mouse_speed;
	unsigned int is_mouse_mode;
	unsigned int is_wakeup_mode;
};

#if 0
static  unsigned int asoc_irkeypad_keycode[] = {
	KEY_POWER, KEY_MUTE, KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_1,
	KEY_2, KEY_3, KEY_4, KEY_5, KEY_6,
	KEY_7, KEY_8, KEY_9, KEY_0,/*KEY_INPUT,*/
	KEY_BACKSPACE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_SELECT, KEY_HOMEPAGE, KEY_MENU, KEY_BACK, KEY_MOVE,
	KEY_MENU, KEY_MOVE, KEY_MENU
/*	KEY_PAGEUP,KEY_PAGEDOWN,KEY_MOVE,KEY_F2_VLC,KEY_F3_VLC,
	KEY_SETTING,KEY_LOCAL,KEY_SEARCH,KEY_CYCLE_PLAY,KEY_F4_VLC,
	KEY_HELP,KEY_HDMI,KEY_AV,KEY_YPBPR,KEY_COLLECTION,
	KEY_PLAYPAUSE*/
};

static  unsigned int asoc_irkeypad_ircode[] = {
	0x51, 0x4D, 0xBB, 0xBD, 0x31,
	0x32, 0x33, 0x34, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x30,/*0x58,*/
	0x44, 0x26, 0x28, 0x25, 0x27,
	0x0D, 0x49, 0xBA, 0x1B, 0x4A,
	0x52, 0x46, 0x53
	/*0x56,0x4E,0x48,0x41,0x59,
	0x53,0x42,0x46,0x4C,0x52,
	0x54,0x09,0x11,0x12,0x4F,
	0x50*/
};
#endif

void irkeypad_reg_print(struct asoc_irkeypad *irkeypad)
{
#if (IRKEYPAD_REG_PRINT == 1)
	IRKEYPAD_INFO("\n\nfollowing list all irkeypad register's value!\n");
	IRKEYPAD_INFO("register atc2603_INTS_PD(0x%08x) value is 0x%08x\n",
		ATC2603C_INTS_PD, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_INTS_PD));
	IRKEYPAD_INFO("register atc2603_INTS_MSK(0x%08x) value is 0x%08x\n",
		ATC2603C_INTS_MSK, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_INTS_MSK));
	IRKEYPAD_INFO("register atc2603_PMU_SYS_CTL3(0x%08x) value is 0x%08x\n",
		ATC2603C_PMU_SYS_CTL3, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_PMU_SYS_CTL3));
	IRKEYPAD_INFO("register atc2603_PMU_SYS_CTL0(0x%08x) value is 0x%08x\n",
		ATC2603C_PMU_SYS_CTL0, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_PMU_SYS_CTL0));
	IRKEYPAD_INFO("register atc2603_IRC_CTL(0x%08x) value is 0x%08x\n",
		ATC2603C_IRC_CTL, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_IRC_CTL));
	IRKEYPAD_INFO("register atc2603_IRC_STAT(0x%08x) value is 0x%08x\n",
		ATC2603C_IRC_STAT, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_IRC_STAT));
	IRKEYPAD_INFO("register atc2603_IRC_CC(0x%08x) value is 0x%08x\n",
		ATC2603C_IRC_CC, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_IRC_CC));
	IRKEYPAD_INFO("register atc2603_IRC_KDC(0x%08x) value is 0x%08x\n",
		ATC2603C_IRC_KDC, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_IRC_KDC));
	IRKEYPAD_INFO("register atc2603_IRC_WK(0x%08x) value is 0x%08x\n",
		ATC2603C_IRC_WK, atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_IRC_WK));
#endif
}

static void asoc_irkeypad_enable_irq(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;
	atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CTL,
		0x0004, 0x0004);
}

static void asoc_irkeypad_disable_irq(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;
	atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CTL,
		0x0004, 0x0000);
}

static void asoc_irkeypad_config(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;

	IRKEYPAD_INFO("[%s start]\n", __func__);

	/*IRC disable to clear IR_KDC */
	atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CTL,
		0x0008, 0x0000);

	/*IRC enable*/
	atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CTL,
		0x0008, 0x0008);

	/*irq*/
	atc260x_set_bits(atc260x_dev, ATC2603C_INTS_MSK,
		0x0100, 0x0100);
	/*mfp*/
		if (irkeypad->ir_ch == 1) {
			atc260x_set_bits(atc260x_dev, ATC2603C_PMU_MUX_CTL0,
				0x0003, 0x0002);
		} else {
			atc260x_set_bits(atc260x_dev, ATC2603C_PMU_MUX_CTL0,
				0x3000, 0x1000);
		}

	switch (irkeypad->protocol) {

	case IR_PROTOCOL_9012:
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CTL,
			0x0003, 0x0000);
	/*set IRC cc,NEC default*/
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CC,
			0xffff, irkeypad->user_code);

	/*set IRC wakeup*/
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_WK,
			0xffff, ((~irkeypad->wk_code) << 8) | (irkeypad->wk_code));
		break;

	case IR_PROTOCOL_NEC8:
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CTL,
			0x0003, 0x0001);
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CC,
			0xffff, irkeypad->user_code);
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_WK,
			0xffff, ((~irkeypad->wk_code) << 8) | (irkeypad->wk_code));
		break;

	case IR_PROTOCOL_RC5:
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CTL,
			0x0003, 0x0002);
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_CC,
			0xffff, (irkeypad->user_code) & 0x001f);
		atc260x_set_bits(atc260x_dev, ATC2603C_IRC_WK,
			0xffff, (irkeypad->wk_code) & 0x003f);
		break;

	default:
		break;
	}

	/*set IRC filter*/
	atc260x_set_bits(atc260x_dev, ATC2603C_IRC_FILTER,
		0xffff, 0x000b);

	/*wake up*/
	atc260x_set_bits(atc260x_dev, ATC2603C_PMU_SYS_CTL0,
		0x0020, 0x0020);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
}

static void asoc_irkeypad_convert(unsigned int protocol,
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

	return;
}

static void asoc_irkeypad_scan(struct asoc_irkeypad *irkeypad)
{

	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;

	irkeypad->ir_val = atc260x_reg_read(atc260x_dev, ATC2603C_IRC_KDC);
	asoc_irkeypad_convert(irkeypad->protocol, &(irkeypad->ir_val));

	return;
}

static void asoc_irkeypad_map(struct asoc_irkeypad *irkeypad)
{
	int i;
	unsigned int *ir_val;

	for (i = 0; i < irkeypad->size; i++) {
		ir_val = irkeypad->ir_code + i;
		asoc_irkeypad_convert(irkeypad->protocol, ir_val);
		if (*ir_val == irkeypad->ir_val) {

			irkeypad->new_key_val = *(irkeypad->key_code + i);
			return;
		}

	}

	IRKEYPAD_INFO("[%s]irkeypad map failed, ir_val = 0x%x\n", __func__, irkeypad->ir_val);

	return;
}

static void asoc_irkeypad_report_released(struct asoc_irkeypad *irkeypad)
{
	struct input_dev *input_dev;

#if 0
	input_dev = irkeypad->input_dev;

	 if (irkeypad->old_key_val  != KEY_RESERVED) {
	    input_report_key(input_dev, irkeypad->old_key_val, 0);
	    IRKEYPAD_INFO("key: %d %s\n",
		    irkeypad->old_key_val, "released");
	 }
	 input_sync(input_dev);
	 irkeypad->new_key_val = KEY_RESERVED;
	 irkeypad->old_key_val = irkeypad->new_key_val;

#else

	asoc_irkeypad_disable_irq(irkeypad);

#if DEBUG_IRQ_HANDLER
	IRKEYPAD_INFO("%s start : old_key_val = %d \n", __func__, irkeypad->old_key_val);
#endif

	input_dev = irkeypad->input_dev;
	if (irkeypad->old_key_val  != KEY_RESERVED) {
		input_report_key(input_dev, irkeypad->old_key_val, 0);
		input_sync(input_dev);
			IRKEYPAD_INFO("key: %d %s\n",
				irkeypad->old_key_val, "released");
			if (irkeypad->old_key_val == KEY_POWER)
				power_timeout = jiffies + HZ*5;
			irkeypad->new_key_val = KEY_RESERVED;
			irkeypad->old_key_val = irkeypad->new_key_val;
	}

	asoc_irkeypad_enable_irq(irkeypad);

#if DEBUG_IRQ_HANDLER
	IRKEYPAD_INFO("%s end\n", __func__);
#endif
#endif

}

static void  asoc_irkeypad_report_pressed(struct asoc_irkeypad *irkeypad)
{
	unsigned int changed;
	struct input_dev *input_dev = irkeypad->input_dev;

#if DEBUG_IRQ_HANDLER
	IRKEYPAD_INFO("%s start\n", __func__);
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
			irkeypad->speed_up      = 1;
			break;
		case KEY_DOWN:
			input_report_rel(input_dev, REL_X, 0);
			input_report_rel(input_dev, REL_Y, irkeypad->mouse_speed);
			input_sync(input_dev);
			irkeypad->speed_up      = 1;
			break;
		case KEY_LEFT:
			input_report_rel(input_dev, REL_X, -irkeypad->mouse_speed);
			input_report_rel(input_dev, REL_Y, 0);
			input_sync(input_dev);
			irkeypad->speed_up      = 1;
			break;
		case KEY_RIGHT:
			input_report_rel(input_dev, REL_X, irkeypad->mouse_speed);
			input_report_rel(input_dev, REL_Y, 0);
			input_sync(input_dev);
			irkeypad->speed_up       = 1;
			break;
		default:
			irkeypad->speed_up       = 0;
			irkeypad->mouse_speed    = 10;
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
			  irkeypad->speed_up     = 0;
			  irkeypad->mouse_speed  = 10;
			  if (irkeypad->is_mouse_mode == 0) {
				   IRKEYPAD_INFO("irkeypad mouse mode enable \n");
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
				   IRKEYPAD_INFO("irkeypad key mode enable \n");
			   }
		}

		if (irkeypad->is_mouse_mode == 1) {
			switch (irkeypad->new_key_val) {
			case KEY_SELECT:
				irkeypad->new_key_val = BTN_LEFT;
				break;

			default:
				break;
			}
		}

		if (!(irkeypad->old_key_val ^ irkeypad->new_key_val))
		  return;
#endif
#if 0

		if (irkeypad->old_key_val != KEY_RESERVED) {
			input_report_key(input_dev, irkeypad->old_key_val, 0);
		}

		if (irkeypad->new_key_val != KEY_RESERVED)
			input_report_key(input_dev, irkeypad->new_key_val, 1);

		irkeypad->old_key_val = irkeypad->new_key_val;
		input_sync(input_dev);
#endif

		/*report old val released*/
		if (irkeypad->old_key_val != KEY_RESERVED && has_canceled_pending) {
			/* asoc_irkeypad_report_released(irkeypad); */
			input_report_key(input_dev, irkeypad->old_key_val, 0);
			input_sync(input_dev);
			pr_err("making up key: %d %s\n",
			irkeypad->old_key_val, "released");
		}

		if (irkeypad->new_key_val != KEY_RESERVED) {
			input_report_key(input_dev, irkeypad->new_key_val, 1);
			input_sync(input_dev);
			IRKEYPAD_INFO("key: %d %s\n",
					irkeypad->new_key_val, "pressed");

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
	unsigned int ret;
	ret = (atc260x_reg_read(irkeypad->atc260x_dev,
			ATC2603C_IRC_STAT) & IRC_STAT_IIP ? 1 : 0);
	return ret;
}

static inline unsigned int asoc_irkeypad_get_irep(struct asoc_irkeypad *irkeypad, unsigned int stat)
{
	unsigned int ret;
	ret = (stat & IRC_STAT_IREP) ? 1 : 0;
	return ret;
}

bool is_invalid_power(struct asoc_irkeypad *irkeypad)
{
	int i;
	unsigned int *ir_val;
	unsigned int keycode = 0;

	for (i = 0; i < irkeypad->size; i++) {
		ir_val = irkeypad->ir_code + i;
		asoc_irkeypad_convert(irkeypad->protocol, ir_val);
		if (*ir_val == irkeypad->ir_val) {
			keycode = *(irkeypad->key_code + i);
			break;
		}
	}

	if (suspending) {
		if (keycode == KEY_POWER) {
			return false;
		} else {
			return true;
		}
	} else
		return false;

	if (time_before(jiffies, power_timeout) &&
		keycode == KEY_POWER) {
		return true;
	} else {
		return false;
	}
}

static int wakeup_powerkey_check(struct asoc_irkeypad *irkeypad)
{
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;
	unsigned int wk_src;

	wk_src = atc260x_reg_read(atc260x_dev, ATC2603C_PMU_SYS_CTL1);
	IRKEYPAD_INFO("Suspending of resuming, wk_src = 0x%x\n", wk_src);
	wk_src &= 0x20;
	if (wk_src != 0x20) {
		IRKEYPAD_INFO("Suspending of resuming, no key.");
		return -1;
	}

	asoc_irkeypad_scan(irkeypad);

	if (irkeypad->ir_val != irkeypad->wk_code) {
		pr_err("Suspending of resuming, no power key.\n");
		return -1;
	}

	if (is_invalid_power(irkeypad)) {
		IRKEYPAD_INFO("Suspending of resuming, ignore this power key.");
		return -1;
	}

	return 0;

}

static irqreturn_t asoc_irkeypad_irq_handler(int irq, void *dev_id)
{
	struct asoc_irkeypad *irkeypad = dev_id;
	struct atc260x_dev *atc260x_dev = irkeypad->atc260x_dev;
	unsigned int stat;
	unsigned int old_ir_val;

#if DEBUG_IRQ_HANDLER
	IRKEYPAD_INFO("[%s start]\n", __func__);
#endif

	stat = atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_IRC_STAT);
	atc260x_set_bits(atc260x_dev, ATC2603C_IRC_STAT, 0xffff, 0x0114);

	if (irkeypad->is_wakeup_mode) {
		return IRQ_HANDLED;
	}

	old_ir_val = irkeypad->ir_val;
	asoc_irkeypad_scan(irkeypad);

	if (is_invalid_power(irkeypad)) {
		IRKEYPAD_INFO("Suspending of resuming, ignore this power key.");
		return IRQ_HANDLED;
	}

#if DEBUG_IRQ_HANDLER
	stat = atc260x_reg_read(irkeypad->atc260x_dev, ATC2603C_IRC_STAT);
	IRKEYPAD_INFO("IRC_STAT : 0x%x\n", stat);
#endif

	if (asoc_irkeypad_get_irep(irkeypad, stat)) {
		if ((irkeypad->ir_val == old_ir_val) || (old_ir_val == 0)) {
			IRKEYPAD_INFO("invalide code, old_ir_val = 0x%x, new_ir_val = 0x%x, IRC_STAT = 0x%x\n",
					old_ir_val, irkeypad->ir_val, stat);
#if DEBUG_IRQ_HANDLER
			irkeypad_reg_print(irkeypad);
#endif
			return IRQ_HANDLED;
		}
	}

#if DEBUG_IRQ_HANDLER
	IRKEYPAD_INFO("[%s] : old_key_val = %d, new_key_val = %d\n", __func__,
		irkeypad->old_key_val, irkeypad->new_key_val);
#endif

	if (stat & IRC_STAT_RCD) {
		if ((irkeypad->new_key_val == KEY_RESERVED) && (irkeypad->speed_up == 0)) {
			IRKEYPAD_INFO("invalide repeat, IRC_STAT = 0x%x\n", stat);
		} else {
			has_canceled_pending = cancel_delayed_work(&irkeypad->work);
#if (IRKEYPAD_SUPPORT_MOUSE == 1)
			tasklet_schedule(&irkeypad->tasklet_pressed);
#endif
			queue_delayed_work(irkeypad->wq, &irkeypad->work,
					msecs_to_jiffies(irkeypad->period));
		}

	} else {
		has_canceled_pending = cancel_delayed_work(&irkeypad->work);
		tasklet_schedule(&irkeypad->tasklet_pressed);
		queue_delayed_work(irkeypad->wq, &irkeypad->work,
				msecs_to_jiffies(irkeypad->period));
	}

	return IRQ_HANDLED;
}


static void asoc_irkeypad_start(struct asoc_irkeypad *irkeypad)
{
	IRKEYPAD_INFO("[%s start]\n", __func__);

	asoc_irkeypad_config(irkeypad);
	asoc_irkeypad_enable_irq(irkeypad);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
}

static void asoc_irkeypad_stop(struct asoc_irkeypad *irkeypad)
{

	IRKEYPAD_INFO("[%s start]\n", __func__);

	cancel_delayed_work(&irkeypad->work);
	asoc_irkeypad_disable_irq(irkeypad);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
}

static int asoc_irkeypad_open(struct input_dev *dev)
{
	struct asoc_irkeypad *irkeypad = input_get_drvdata(dev);

	IRKEYPAD_INFO("[%s start]\n", __func__);

	asoc_irkeypad_start(irkeypad);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
	return 0;
}

static void asoc_irkeypad_close(struct input_dev *dev)
{

	struct asoc_irkeypad *irkeypad = input_get_drvdata(dev);

	IRKEYPAD_INFO("[%s start]\n",  __func__);

	asoc_irkeypad_stop(irkeypad);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
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
	int __attribute__((unused)) cmd;
	size_t size;

	cmd = simple_strtoul(buf, &endp, 0);
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
	__ATTR(mousespeed, S_IRUGO | S_IWUSR, atv5201_irkeypad_mousespeed_show, atv5201_irkeypad_mousespeed_store),
	__ATTR(queuesize, S_IRUGO | S_IWUSR, atv5201_irkeypad_show, atv5201_irkeypad_store),
};

/*just for test*/
#if 0
static int set_vendor_id_test()
{
	char *buf = "123456789A08DFE3";
	int ret = write_mi_item("VENDOR", buf, 17);
	printk("[%s]begin...\n", __func__);
	if (ret < 0) {
		pr_err("write board info failed\n");
		return -1;
	}
	return 0;
}
#endif
static int get_irkeypad_bordinfo(char *ir_info)
{
	char buf[20];
	int str_len;
	int ret;
	/* just for test */
	/*set_vendor_id_test();*/
	memset(buf, 0, sizeof(buf));
	str_len = read_mi_item("VENDOR", buf, sizeof(buf));
	pr_info("str_len:%d\n", str_len);

	if (str_len < 0 || str_len < BOARDINFO_STR_LEN) {
		pr_err("read vendor id failed\n");
		ret = -1;
		return ret;
	}
	pr_info("vendor id is:%s", buf);
	/*get irkeypad info*/
	strncpy(ir_info, buf + (char)IR_IDX, IR_LEN);
	*(ir_info + IR_LEN) = '\0';
	pr_info("ir cfg info:%s\n", ir_info);
	ret = 0;

	return ret;
}
static void set_wakeup_config(struct irconfig *irconfig, void *data)
{
	struct asoc_irkeypad *irkeypad = data;
	pr_err("[%s]start...\n");
	irkeypad->wk_code = irconfig->wk_code;
	irkeypad->user_code = irconfig->user_code;
	irkeypad->protocol = irconfig->protocol;
	asoc_irkeypad_config(irkeypad);
}
static int atc260x_irkeypad_get_config(struct platform_device *pdev, struct asoc_irkeypad *irkeypad)
{
	int ret;
	char ir_id[IR_LEN + 1] = "";
	char ircfg_node_name[10];
	char ircfg_default[10];
	struct device_node *dnp_config = NULL;
	struct device_node *dnp = pdev->dev.of_node;
	bool has_vendor_id;
	/* for owl-irkeypad driver
	* when owl-irkeypad driver init over,ir_register_wk_notifier return true,
	* it means that vendor id support multi-controller
	* here only meet the need of enable PMU ir wake up,then this driver won't run again.
	*/
	#if defined (CONFIG_KEYBOARD_OWL_IR) || defined(CONFIG_KEYBOARD_OWL_IR_MODULE)
	if (ir_register_wk_notifier(set_wakeup_config, wakeup_powerkey_check, irkeypad)) {
		pr_err("[%s]owl-irkey work now\n", __func__);
		return -3;
	}
	#endif
	has_vendor_id = (get_irkeypad_bordinfo(ir_id) == 0);
	strcpy(ircfg_node_name, "config");
	if (has_vendor_id)
		strcat(strcat(ircfg_node_name, "-"), ir_id);
	dnp_config = of_find_node_by_name(dnp, ircfg_node_name);
	pr_info("ircfg_node_name:%s\n", ircfg_node_name);
	if (dnp_config == NULL) {
		IRKEYPAD_INFO("get dnp_config failed: %s!\n", ircfg_node_name);
		strcpy(ircfg_default, "config");
		dnp_config = of_find_node_by_name(dnp, ircfg_default);
		if (dnp_config == NULL)
			return -1;
	}

	ret = of_property_read_u32(dnp_config, "size", &(irkeypad->size));
	if ((ret) || (!irkeypad->size)) {
		dev_err(&pdev->dev, "Get size failed ret = %d \r\n", ret);
		return -1;
	}
	dev_info(&pdev->dev, "size = %d\n", irkeypad->size);
	ret = of_property_read_u32(dnp_config, "ir_ch", &(irkeypad->ir_ch));
	if ((ret) || (!irkeypad->ir_ch)) {
			dev_err(&pdev->dev, "Get ir_ch failed ret = %d \r\n", ret);
			irkeypad->ir_ch = 0;
	}
	pr_info("atc260x_irkeypad: ir_ch = %d\n", irkeypad->ir_ch);
	ret = of_property_read_u32(dnp_config, "user_code", &(irkeypad->user_code));
	if ((ret) || (!irkeypad->user_code)) {
		dev_err(&pdev->dev, "Get user_code failed ret = %d \r\n", ret);
		return -1;
	}
	dev_info(&pdev->dev, "user_code = %d\n", irkeypad->user_code);
	ret = of_property_read_u32(dnp_config, "protocol", &(irkeypad->protocol));
	if ((ret) /*|| (!irkeypad->protocol)*/) {
		dev_err(&pdev->dev, "Get protocol failed ret = %d \r\n", ret);
		return -1;
	}
	dev_info(&pdev->dev, "protocol = %d\n", irkeypad->protocol);
	ret = of_property_read_u32(dnp_config, "wk_code", &(irkeypad->wk_code));
	if ((ret) || (!irkeypad->wk_code)) {
		dev_err(&pdev->dev, "Get wk_code failed ret = %d \r\n", ret);
		return -1;
	}
	dev_info(&pdev->dev, "wk_code = %d\n", irkeypad->wk_code);
	ret = of_property_read_u32(dnp_config, "period", &(irkeypad->period));
	if ((ret) || (!irkeypad->period)) {
		dev_err(&pdev->dev, "Get period failed ret = %d \r\n", ret);
		return -1;
	}
	dev_info(&pdev->dev, "period = %d\n", irkeypad->period);
	irkeypad->ir_code = devm_kzalloc(&pdev->dev,
		sizeof(unsigned int) * (irkeypad->size), GFP_KERNEL);
	if (!irkeypad->ir_code)
		return -1;
	ret = of_property_read_u32_array(dnp_config, "ir_code",
		(u32 *)irkeypad->ir_code,
		irkeypad->size);
	if (ret) {
		dev_err(&pdev->dev, "Get ir_code failed ret = %d\r\n", ret);
		return -2;
	}

	irkeypad->key_code = devm_kzalloc(&pdev->dev,
		sizeof(unsigned int) * (irkeypad->size), GFP_KERNEL);
	if (!irkeypad->key_code)
		return -1;

	ret = of_property_read_u32_array(dnp_config, "key_code",
		(u32 *)irkeypad->key_code,
		irkeypad->size);
	if (ret) {
		dev_err(&pdev->dev, "Get key_code failed ret = %d\r\n", ret);
		return -2;
	}
	return 0;
}

static int atc260x_irkeypad_probe(struct platform_device *pdev)
{
	struct atc260x_dev *atc260x_dev = dev_get_drvdata(pdev->dev.parent);
	struct asoc_irkeypad *irkeypad;
	struct device_node *np;
	struct input_dev *input_dev;
	int ret = 0;
	int i;

	atc260x_dev_global = atc260x_dev;

	pr_info("[%s start]\n", __func__);
	np = pdev->dev.of_node;

	irkeypad = kzalloc(sizeof(struct asoc_irkeypad), GFP_KERNEL);
	if (irkeypad == NULL) {
		dev_err(&pdev->dev, "failed to allocate irkeypad driver data\n");
		ret = -ENOMEM;
		return ret;
	}
	platform_set_drvdata(pdev, irkeypad);

	irkeypad->atc260x_dev = atc260x_dev;
	irkeypad->old_key_val = KEY_RESERVED;
	irkeypad->old_mouse_move = KEY_RESERVED;
	irkeypad->mouse_speed  = 10;
    irkeypad->is_wakeup_mode = 0;
#if 0 /*(IRKEYPAD_DEBUG == 1)*/
	irkeypad->size = ARRAY_SIZE(asoc_irkeypad_keycode);
	irkeypad->user_code = 0x80;
	irkeypad->wk_code = 0x51;
	irkeypad->ir_code = asoc_irkeypad_ircode;
	irkeypad->key_code = asoc_irkeypad_keycode;
	irkeypad->protocol = 0x01;
	irkeypad->period   = 130;
#endif

	/*
	 * irq related
	 */
	irkeypad->irq = platform_get_irq(pdev, 0);
	IRKEYPAD_INFO("[%s] get irq: %d\n", __func__, irkeypad->irq);
	if (irkeypad->irq < 0) {
		dev_err(&pdev->dev, "failed to get irkeypad irq\n");
		ret = -ENXIO;
		goto free_key_code;
	}
	ret = request_threaded_irq(irkeypad->irq, NULL, asoc_irkeypad_irq_handler,
		IRQF_TRIGGER_HIGH, "atc260x-irkeypad", irkeypad);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irkeypad IRQ\n");
		goto free_key_code;
	}
	asoc_irkeypad_disable_irq(irkeypad);
    
	ret = atc260x_irkeypad_get_config(pdev, irkeypad);

	if (ret == -1)
		goto of_property_read_err;
	else if (ret == -2)
		goto free_ir_code;
	else if (ret == -3) {
        irkeypad->is_wakeup_mode = 1;
		return ret;
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
	 * input_dev related
	 */
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate  irkeypad input device\n");
		ret = -ENOMEM;
		goto free_irq;
	}
	input_dev->name = pdev->name;
	input_dev->phys = "asoc-irkeypad/input5";
	input_dev->open = asoc_irkeypad_open;
	input_dev->close = asoc_irkeypad_close;
	input_dev->dev.parent = &pdev->dev;
	input_dev->keycodemax = irkeypad->size;
	input_dev->id.bustype = BUS_HOST;

	irkeypad->input_dev = input_dev;
	input_set_drvdata(input_dev, irkeypad);
#if (IRKEYPAD_SUPPORT_MOUSE == 1)
	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	irkeypad->is_mouse_mode = 0;
#else
	input_dev->evbit[0] = BIT(EV_KEY);
#endif

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
	pr_info("[%s finished]\n", __func__);
	return 0;

free_input:
	input_free_device(input_dev);
		input_set_drvdata(input_dev, NULL);
free_irq:
	free_irq(irkeypad->irq, pdev);

free_key_code:
free_ir_code:
		platform_set_drvdata(pdev, NULL);
of_property_read_err:
free:
	kfree(irkeypad);

	return ret;
}

static int atc260x_irkeypad_remove(struct platform_device *pdev)
{
	int i;
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);

	IRKEYPAD_INFO("[%s start]\n", __func__);
	free_irq(irkeypad->irq, irkeypad);
	input_unregister_device(irkeypad->input_dev);
	input_free_device(irkeypad->input_dev);
	input_set_drvdata(irkeypad->input_dev, NULL);

	for (i = 0; i < ARRAY_SIZE(irkeypad_attrs); i++)
		device_remove_file(&pdev->dev, &irkeypad_attrs[i]);

	destroy_workqueue(irkeypad->wq);
	platform_set_drvdata(pdev, NULL);

	kfree(irkeypad);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
	return 0;
}


void atc260x_irkeypad_reset_irc(void)
{
	IRKEYPAD_INFO("%s ,reset IRC protocol!\n", __func__);
	atc260x_set_bits(atc260x_dev_global, ATC2603C_IRC_CTL,
		0x0003, 0x0002);
	msleep(100);
	atc260x_set_bits(atc260x_dev_global, ATC2603C_IRC_CTL,
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
	IRKEYPAD_INFO("[%s finished]\n", __func__);
}

static void irkeypad_late_resume(struct early_suspend *h)
{
	suspending = false;
	power_timeout = jiffies + 5*HZ;
	IRKEYPAD_INFO("[%s finished]\n", __func__);
}

static struct early_suspend irkeypad_early_suspend_desc = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = irkeypad_early_suspend,
	.resume = irkeypad_late_resume,
};
#endif
static int atc260x_irkeypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);

	asoc_irkeypad_stop(irkeypad);
	//atc260x_irkeypad_reset_irc();
	asoc_irkeypad_config(irkeypad);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
	return 0;
}

static int atc260x_irkeypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct asoc_irkeypad *irkeypad = platform_get_drvdata(pdev);

	if (wakeup_powerkey_check(irkeypad) >= 0) {
		input_report_key(irkeypad->input_dev, KEY_POWER, 1);
		input_report_key(irkeypad->input_dev, KEY_POWER, 0);
		input_sync(irkeypad->input_dev);
	}
	asoc_irkeypad_start(irkeypad);

	IRKEYPAD_INFO("[%s finished]\n", __func__);
	return 0;
}

static const struct dev_pm_ops s_atc260x_irkeypad_pm_ops = {
	.suspend = atc260x_irkeypad_suspend,
	.resume = atc260x_irkeypad_resume,
};

static const struct of_device_id atc260x_irkey_of_match[] = {
	{.compatible = "actions,atc2603a-irkeypad",},
	{.compatible = "actions,atc2603c-irkeypad",},
	{.compatible = "actions,atc2609a-irkeypad",},
	{}
};
MODULE_DEVICE_TABLE(of, atc260x_irkey_of_match);

static struct platform_driver atc260x_irkeypad_driver = {
	.driver = {
		.name = "atc260x-irkeypad",
		.owner = THIS_MODULE,
		.pm = &s_atc260x_irkeypad_pm_ops,
		.of_match_table = of_match_ptr(atc260x_irkey_of_match),
	},
	.probe = atc260x_irkeypad_probe,
	.remove = atc260x_irkeypad_remove,
	.shutdown = atc260x_irkeypad_shutdown,
};

static int __init atc260x_irkeypad_init(void)
{
	int ret;
	ret = platform_driver_register(&atc260x_irkeypad_driver);
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&irkeypad_early_suspend_desc);
#endif
	return ret;
}

static void __exit atc260x_irkeypad_exit(void)
{
	platform_driver_unregister(&atc260x_irkeypad_driver);

#ifdef CONFIG_HAS_EARLYSUSPEND
	/*if config for early suspen , unregister it */
	unregister_early_suspend(&irkeypad_early_suspend_desc);
#endif
}

module_init(atc260x_irkeypad_init);
module_exit(atc260x_irkeypad_exit);

MODULE_DESCRIPTION("Asoc irkeypad controller drvier");
MODULE_AUTHOR("ZhigaoNi <zhigaoni@actions-semi.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:asoc-irkeypad");
