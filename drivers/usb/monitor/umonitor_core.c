/*
 * Actions OWL SoCs usb monitor driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * dengtaiping <dengtaiping@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/poll.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/kallsyms.h>

#include "aotg_regs.h"
#include "umonitor_config.h"
#include "umonitor_core.h"

typedef void (*FUNC)(int);
#define USB_CONNECT_TO_PC	1
#define USB_CONNECT_TO_ADAPTOR		2
FUNC umon_set_usb_plugin_type;

enum {
	USB_DET_NONE = 0,	/* nothing detected, maybe B plus is out. */
	USB_DET_DEVICE_DEBUOUNCING,	/* detected device, debouncing and confirming. */
	USB_DET_DEVICE_PC,	/* detected device confirmed. pc connected. */
	USB_DET_DEVICE_CHARGER	/* detected device confirmed. charger connected. */
};

enum {
	USB_DET_HOST_NONE = 0,	/* nothing detected. maybe udisk is plug out. */
	USB_DET_HOST_DEBOUNCING,	/* detecting host, debouncing and confirming. */
	USB_DET_HOST_UDISK	/* detected udisk confirmed. udisk connected. */
};

#define USB_DEVICE_DETECT_STEPS    4
#define USB_HOST_DETECT_STEPS      4
#define USB_MONITOR_DEF_INTERVAL   500	/* interval to check usb port state, unit: ms. */

umonitor_dev_status_t *umonitor_status;
static int usb_monitor_debug_status_inf(void)
{
#if 0
	umonitor_dev_status_t *pstatus = umonitor_status;

	pr_info(".det_phase %d %d %d %d %d\n",
	       (unsigned int) pstatus->det_phase,
	       (unsigned int) pstatus->vbus_status,
	       (unsigned int) pstatus->timer_steps,
	       (unsigned int) pstatus->host_confirm,
	       (unsigned int) pstatus->message_status);
	pr_info("-----------------------------\n");
	pr_info(".vbus_status %d %x\n", (unsigned int)pstatus->vbus_status,
	       (unsigned int) pstatus->vbus_status);
	pr_info(".vbus_enable_power %d\n", (unsigned int)pstatus->vbus_enable_power);
	pr_info(".det_phase %d\n", (unsigned int)pstatus->det_phase);
	pr_info(".device_confirm %d\n", (unsigned int)pstatus->device_confirm);
	pr_info(".host_confirm %d\n", (unsigned int)pstatus->host_confirm);
	pr_info(".usb_pll_on %d\n", (unsigned int)pstatus->usb_pll_on);
	pr_info(".dp_dm_status %d 0x%x\n", (unsigned int)pstatus->dp_dm_status,
	       (unsigned int) pstatus->dp_dm_status);
	pr_info(".timer_steps %d\n", (unsigned int)pstatus->timer_steps);
	pr_info(".timer_interval %d\n", (unsigned int)pstatus->timer_interval);
	pr_info(".check_cnt %d\n", (unsigned int)pstatus->check_cnt);
	pr_info(".sof_check_times %d\n", (unsigned int)pstatus->sof_check_times);
	pr_info("\n\n");
#endif
	return 0;
}

static int usb_init_monitor_status(umonitor_dev_status_t *pstatus)
{
	pstatus->detect_valid = 0;
	pstatus->detect_running = 0;
	pstatus->vbus_status = 0;
	pstatus->dc5v_status = 0;
	pstatus->det_phase = 0;
	pstatus->device_confirm = 0;
	pstatus->sof_check_times = 0;
	pstatus->host_confirm = 0;
	pstatus->usb_pll_on = 0;
	pstatus->dp_dm_status = 0;
	pstatus->timer_steps = 0;
	pstatus->timer_interval = USB_MONITOR_DEF_INTERVAL;
	pstatus->check_cnt = 0;
	pstatus->message_status = 0;
	pstatus->core_ops = NULL;
	pstatus->vbus_enable_power = 0;
	return 0;
}

unsigned int umonitor_get_timer_step_interval(void)
{
	umonitor_dev_status_t *pstatus;
	pstatus = umonitor_status;

	if ((pstatus->port_config->detect_type == UMONITOR_DISABLE) ||
		(pstatus->detect_valid == 0))
		return 0x70000000;	/* be longer enough that it would not run again. */

	if (pstatus->timer_steps == 0) {
		/*pstatus->timer_interval = USB_MONITOR_DEF_INTERVAL;*/
		pstatus->timer_interval = 30;
		goto out;
	}

	if (pstatus->det_phase == 0) {
		switch (pstatus->timer_steps) {
		case 1:
		case 2:
		case 3:
			pstatus->timer_interval = USB_MONITOR_DEF_INTERVAL;
			break;

		case 4:
			switch (pstatus->device_confirm) {
			case 0:
				pstatus->timer_interval = USB_MONITOR_DEF_INTERVAL;
				break;
			case 1:
				pstatus->timer_interval = 10;	/* 10 ms, 1 tick. */
				break;
			case 2:
				pstatus->timer_interval = 300;
				break;
			case 3:
				pstatus->timer_interval = 30;
				break;
				/* wait sof again time interval, the whole detect sof time is (20 * sof_check_times) msecond. */
			case 4:
				pstatus->timer_interval = 20;
				break;
			default:
				USB_ERR_PLACE;
				pstatus->timer_interval = USB_MONITOR_DEF_INTERVAL;
				break;
			}
			break;
		default:
			USB_ERR_PLACE;
			pstatus->timer_interval = USB_MONITOR_DEF_INTERVAL;
			break;
		}
	} else {
		switch (pstatus->timer_steps) {
		case 1:
			pstatus->timer_interval = 30;
			break;
		case 2:
			pstatus->timer_interval = 600;
			break;
		case 3:
			pstatus->timer_interval = 600;
			break;
		case 4:
			switch (pstatus->host_confirm) {
			case 0:
				pstatus->timer_interval =
					USB_MONITOR_DEF_INTERVAL;
				break;
			case 1:	/* debounce time. */
				pstatus->timer_interval = 10;	/* 10 ms, 1 tick. */
				break;
			default:
				USB_ERR_PLACE;
				pstatus->timer_interval =
					USB_MONITOR_DEF_INTERVAL;
				break;
			}
			break;
		default:
			USB_ERR_PLACE;
			pstatus->timer_interval = USB_MONITOR_DEF_INTERVAL;
			break;
		}
	}
out:
	return pstatus->timer_interval;
}

/*
 * retval:
 * refer to below macro:
 *    USB_DET_NONE,
 *    USB_DET_DEVICE_DEBUOUNCING,
 *    USB_DET_DEVICE_PC,
 *    USB_DET_DEVICE_CHARGER,
 */
static int usb_timer_det_pc_charger(umonitor_dev_status_t *pstatus)
{
	int ret = 0;
	int id;
	unsigned int val = 0;
	usb_hal_monitor_t *p_hal = &pstatus->umonitor_hal;

	MONITOR_PRINTK("entring usb_timer_det_pc_charger\n");

	if (pstatus->device_confirm == 0) {
		/* make sure power off. */
		if (pstatus->vbus_enable_power != 0) {
			p_hal->vbus_power_onoff(p_hal, 0);
			pstatus->vbus_enable_power = 0;
			p_hal->set_soft_id(p_hal, 1, 1);
		}
	}

	pstatus->vbus_status = (unsigned char)p_hal->get_vbus_state(p_hal);
	id = p_hal->get_idpin_state(p_hal);
	if ((pstatus->vbus_status == USB_VBUS_HIGH) && (id != USB_ID_STATE_HOST)) {
		MONITOR_PRINTK("vbus is high!!!!!!!\n");
		if ((pstatus->message_status & (0x1 << MONITOR_B_IN)) != 0) {
#if 0
			/* if pc is connected, and charger is new plug in, we ignore it. */
			if ((pstatus->message_status & (0x1 << MONITOR_CHARGER_IN)) == 0)
#endif
				pstatus->device_confirm = 0;
			pstatus->timer_steps = 0;
			ret = USB_DET_DEVICE_PC;
			goto out2;
		}
		if ((pstatus->message_status & (0x1 << MONITOR_CHARGER_IN)) != 0) {
			pstatus->device_confirm = 0;
			pstatus->timer_steps = 0;
			ret = USB_DET_DEVICE_CHARGER;
			goto out2;
		}
		switch (pstatus->device_confirm) {
		case 0:
			pstatus->timer_steps = USB_DEVICE_DETECT_STEPS;	/* the last timer_steps is to confirm. */
			pstatus->device_confirm = 1;
			ret = USB_DET_DEVICE_DEBUOUNCING;
			goto out2;
		case 1:
			p_hal->set_dp_500k_15k(p_hal, 1, 0);	/* 500k up enable, 15k down disable; */
			pstatus->device_confirm = 2;
			ret = USB_DET_DEVICE_DEBUOUNCING;
			goto out2;
		case 2:
			pstatus->dp_dm_status = p_hal->get_linestates(p_hal);
			pstatus->device_confirm = 3;
			/*pstatus->device_confirm = 2;*/  /* always in get dp dm states, just for test. */
			ret = USB_DET_DEVICE_DEBUOUNCING;
			goto out2;
		case 3:
			val = p_hal->get_linestates(p_hal);	/* get dp dm status */
			pstatus->sof_check_times = 0;
			if (val == pstatus->dp_dm_status) {
				if (val != 0) {
					if (umon_set_usb_plugin_type) {
						pr_info("%s set adapter mode\n", __func__);
						umon_set_usb_plugin_type(USB_CONNECT_TO_ADAPTOR);
					}
					pstatus->device_confirm = 0;
					/* if enable monitor again, it should begin from step 0.  */
					pstatus->timer_steps = 0;
					ret = USB_DET_DEVICE_CHARGER;
				} else {
					pstatus->timer_steps = 0;
					pstatus->device_confirm = 0;
					ret = USB_DET_DEVICE_PC;
				}
				goto out2;
			} else {
				pstatus->device_confirm = 1;
				ret = USB_DET_DEVICE_DEBUOUNCING;
				goto out2;
			}
			/* for detect sof or reset irq. */
		case 4:
			val = p_hal->is_sof(p_hal);
			if (val != 0) {
				/* if enable monitor again, it should begin from step 0. */
				pstatus->timer_steps = 0;
				pstatus->device_confirm = 0;
				pstatus->sof_check_times = 0;
				p_hal->dp_down(p_hal);
				ret = USB_DET_DEVICE_PC;
				goto out2;
			}
			if (pstatus->sof_check_times < MAX_DETECT_SOF_CNT) {	/* 10 * MAX_DETECT_SOF_CNT ms. */
				pstatus->device_confirm = 4;	/* next step still check again. */
				pstatus->sof_check_times++;
				ret = USB_DET_DEVICE_DEBUOUNCING;
				goto out2;
			}
			/* if enable monitor again, it should begin from step 0. */
			pstatus->timer_steps = 0;
			pstatus->device_confirm = 0;
			pstatus->sof_check_times = 0;
			p_hal->dp_down(p_hal);
			/* treated as charger in. */
			ret = USB_DET_DEVICE_CHARGER;
			goto out2;
		default:
			MONITOR_ERR("into device confirm default, err!\n");
			pstatus->device_confirm = 0;
			ret = USB_DET_NONE;
			goto out;
		}
	} else {
		pstatus->device_confirm = 0;
		pstatus->timer_steps = USB_DEVICE_DETECT_STEPS;
		ret = USB_DET_NONE;
		goto out;
	}
out:
	pstatus->timer_steps++;
	if (pstatus->timer_steps > USB_DEVICE_DETECT_STEPS)
		pstatus->timer_steps = 0;

out2:
	return ret;
}

static int usb_timer_det_udisk(umonitor_dev_status_t *pstatus)
{
	unsigned int val;
	usb_hal_monitor_t *p_hal = &pstatus->umonitor_hal;

	if (pstatus->timer_steps == 1) {
		p_hal->set_dp_500k_15k(p_hal, 0, 1);	/* disable 500k, enable 15k. */
		if (pstatus->vbus_enable_power == 0) {
			p_hal->vbus_power_onoff(p_hal, 1);
			pstatus->vbus_enable_power = 1;
			p_hal->set_soft_id(p_hal, 1, 0);
		}
		goto out;
	} else {
		if (pstatus->vbus_enable_power != 1)
			USB_ERR_PLACE;

		val = p_hal->get_linestates(p_hal);
		MONITOR_PRINTK("host debounce!!!, linestate %04x\n", val);
		pstatus->timer_steps = 0;
		pstatus->host_confirm = 0;
		return USB_DET_HOST_UDISK;
		if ((val == 0x1) || (val == 0x2)) {
			switch (pstatus->host_confirm) {
			case 0:
				pstatus->host_confirm = 1;
				/* the last step is always debounce and confirm step. */
				pstatus->timer_steps = USB_HOST_DETECT_STEPS;
				pstatus->dp_dm_status = val;
				return USB_DET_HOST_DEBOUNCING;
			case 1:
				if (val == pstatus->dp_dm_status) {
					/* if enable monitor again, it should begin from step 0. */
					pstatus->timer_steps = 0;
					pstatus->host_confirm = 0;
					return USB_DET_HOST_UDISK;
				} else {
					pstatus->dp_dm_status = val;
					pstatus->host_confirm = 0;
					return USB_DET_HOST_DEBOUNCING;
				}
			default:
				break;
			}
		} else {
			pstatus->host_confirm = 0;
			goto out;
		}
	}
out:
	pstatus->timer_steps++;
	if (pstatus->timer_steps > USB_HOST_DETECT_STEPS) {
		pstatus->timer_steps = 0;
		return USB_DET_HOST_NONE;	/* nothing detect, maybe udisk is plug out. */
	}
	return USB_DET_HOST_DEBOUNCING;
}

static int usb_timer_process_step0(umonitor_dev_status_t *pstatus)
{
	int ret = 0;
	unsigned int status = 0;
	usb_hal_monitor_t *p_hal = &pstatus->umonitor_hal;

	MONITOR_PRINTK("entring usb_timer_process_step0\n");

	if ((pstatus->message_status & (0x1 << MONITOR_B_IN)) != 0) {
		MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);
		pr_debug("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);
		pstatus->core_ops->putt_msg(MON_MSG_USB_B_OUT);
		pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_B_IN));
	}

	if ((pstatus->message_status & (0x1 << MONITOR_A_IN)) != 0) {
		MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_A_OUT\n", __FILE__, __LINE__);
		pr_debug("\n%s--%d, SYS_MSG_USB_A_OUT\n", __FILE__, __LINE__);
		pstatus->core_ops->putt_msg(MON_MSG_USB_A_OUT);
		pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_A_IN));
	}

	if (p_hal->config->detect_type == UMONITOR_DEVICE_ONLY)
		ret = USB_ID_STATE_DEVICE;
	else if (p_hal->config->detect_type == UMONITOR_HOST_ONLY)
		ret = USB_ID_STATE_HOST;
	else
		ret = p_hal->get_idpin_state(p_hal);
	MONITOR_PRINTK("idpin is %d\n", ret);


	if (ret != USB_ID_STATE_INVALID) {
		if (ret == USB_ID_STATE_HOST) {
host_detect:
			MONITOR_PRINTK("host detecting!!!!\n");
			if ((pstatus->message_status & (0x1 << MONITOR_B_IN)) != 0) {
				MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);
				pr_debug("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);
				pstatus->core_ops->putt_msg(MON_MSG_USB_B_OUT);
				pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_B_IN));
			}
			if ((pstatus->message_status & (0x1 << MONITOR_CHARGER_IN)) != 0) {
				MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_CHARGER_OUT\n", __FILE__, __LINE__);
				pstatus->core_ops->putt_msg(MON_MSG_USB_CHARGER_OUT);
				pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_CHARGER_IN));
			}

			p_hal->set_dp_500k_15k(p_hal, 0, 1);	/* disable 500k, enable 15k. */

			if (pstatus->vbus_enable_power == 0) {
				p_hal->vbus_power_onoff(p_hal, 1);
				pstatus->vbus_enable_power = 1;
				p_hal->set_soft_id(p_hal, 1, 0);
			}
			pstatus->det_phase = 1;
		} else {
			MONITOR_PRINTK("device detect prepare!!!!\n");
			if ((pstatus->message_status & (0x1 << MONITOR_A_IN)) != 0) {
				MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_A_OUT\n", __FILE__, __LINE__);
				pr_debug("\n%s--%d, SYS_MSG_USB_A_OUT\n", __FILE__, __LINE__);
				pstatus->core_ops->putt_msg(MON_MSG_USB_A_OUT);
				pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_A_IN));
			}
			if (pstatus->vbus_enable_power) {
				p_hal->vbus_power_onoff(p_hal, 0);
				pstatus->vbus_enable_power = 0;
			}
			p_hal->set_dp_500k_15k(p_hal, 0, 0);	/* disable 500k, disable 15k. */
			p_hal->set_soft_id(p_hal, 1, 1);

			pstatus->det_phase = 0;
		}
		pstatus->device_confirm = 0;
		pstatus->host_confirm = 0;
		pstatus->timer_steps = 1;
		goto out;
	}

	/* the last time check host state before change to device detect phase. */
	if ((pstatus->vbus_enable_power != 0) && (pstatus->det_phase != 0)) {
		pstatus->dp_dm_status = p_hal->get_linestates(p_hal);
		if ((pstatus->dp_dm_status == 0x1) || (pstatus->dp_dm_status == 0x2)) {
			pstatus->timer_steps = USB_HOST_DETECT_STEPS;
			pstatus->host_confirm = 0;
			goto out;
		}
	}

	p_hal->vbus_power_onoff(p_hal, 0);
	pstatus->vbus_enable_power = 0;
	p_hal->set_dp_500k_15k(p_hal, 0, 0);	/* disable 500k, disable 15k. */
	p_hal->set_soft_id(p_hal, 1, 1);
	pstatus->check_cnt++;

	/* if it's the first time to check, must in checking device phase. */
	if ((pstatus->check_cnt == 1) ||
	    (pstatus->port_config->detect_type == UMONITOR_DEVICE_ONLY)) {
		pstatus->det_phase = 0;
	} else {
		/* reverse detect phase. */
		pstatus->det_phase = !pstatus->det_phase;

		/* if it's B_IN status, it needn't to check host in, because there is just one usb port. */
		status = pstatus->message_status & ((0x1 << MONITOR_B_IN) | (0x1 << MONITOR_CHARGER_IN));
		if ((pstatus->det_phase == 1) && (status != 0)) {
			pstatus->det_phase = 0;
			goto out1;
		}
		pstatus->check_cnt = 0;
		goto host_detect;

	}
out1:
	pstatus->device_confirm = 0;
	pstatus->host_confirm = 0;
	pstatus->timer_steps = 1;

out:
	return 0;
}

void umonitor_timer_func(void)
{
	int ret = 0;
	unsigned int status = 0;
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;
	u32 reg;

	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;

	MONITOR_PRINTK("entring umonitor_timer_func\n");

	if ((pstatus->port_config->detect_type == UMONITOR_DISABLE)
			    || (pstatus->detect_valid == 0)) {
		goto out;
	}
	pstatus->detect_running = 1;

	/* err check! */
	if ((pstatus->timer_steps > USB_DEVICE_DETECT_STEPS)
	    && (pstatus->timer_steps > USB_HOST_DETECT_STEPS)) {
		MONITOR_ERR("timer_steps err:%d\n", pstatus->timer_steps);
		pstatus->timer_steps = 0;
		goto out;
	}

	if (pstatus->timer_steps == 0) {	/* power on/off phase. */
		usb_timer_process_step0(pstatus);
		goto out;
	}

	if (pstatus->det_phase == 0) {	/* power off, device detect phase. */
		ret = usb_timer_det_pc_charger(pstatus);
		switch (ret) {
		case USB_DET_NONE:
			if ((pstatus->message_status & (0x1 << MONITOR_B_IN)) != 0) {
				/*MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);*/
				pr_debug("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);
				pstatus->core_ops->putt_msg(MON_MSG_USB_B_OUT);
				pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_B_IN));
			}
			if ((pstatus->message_status & (0x1 << MONITOR_CHARGER_IN)) != 0) {
				pr_debug("\n%s--%d, SYS_MSG_USB_CHARGER_OUT\n", __FILE__, __LINE__);
				/*MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_CHARGER_OUT\n", __FILE__, __LINE__);*/
				pstatus->core_ops->putt_msg(MON_MSG_USB_CHARGER_OUT);
				pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_CHARGER_IN));
			}
			break;

		case USB_DET_DEVICE_DEBUOUNCING:	/* debounce. */
			break;

		case USB_DET_DEVICE_PC:
			if (p_hal->get_idpin_state(p_hal) != USB_ID_STATE_DEVICE) {
				pstatus->device_confirm = 0;
				pstatus->timer_steps = 0;
				goto out;
			}
			status = pstatus->message_status & (0x1 << MONITOR_B_IN);
			if (status != 0)
				goto out;

			p_hal->set_mode(p_hal, USB_IN_DEVICE_MOD);
			/* need to reset dp/dm before dwc3 loading */
			reg = readl(p_hal->usbecs);
			reg &= ~((0x1 << p_hal->usb3_p0_ctl.DPPUEN_P0)|(0x1 << p_hal->usb3_p0_ctl.DMPUEN_P0));
			writel(reg, p_hal->usbecs);
			/*MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_B_IN\n", __FILE__, __LINE__);*/
			pr_debug("\n%s--%d, SYS_MSG_USB_B_IN\n", __FILE__, __LINE__);
			pstatus->core_ops->putt_msg(MON_MSG_USB_B_IN);
			pstatus->message_status |= 0x1 << MONITOR_B_IN;
			pstatus->detect_valid = 0;	/* disable detection */
			goto out;	/* todo stop timer. */

		case USB_DET_DEVICE_CHARGER:
			/* if B_IN message not clear, clear it. B_OUT when adaptor is in. */
			status = pstatus->message_status & (0x1 << MONITOR_B_IN);
			if (status != 0) {
				pr_debug("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);
				/*MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_B_OUT\n", __FILE__, __LINE__);*/
				pstatus->core_ops->putt_msg(MON_MSG_USB_B_OUT);
				pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_B_IN));
			}
			/* if adaptor in is send, it needn't sent again. */
			status = pstatus->message_status & (0x1 << MONITOR_CHARGER_IN);
			if (status != 0)
				goto out;

			p_hal->set_mode(p_hal, USB_IN_DEVICE_MOD);
			MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_CHARGER_IN\n", __FILE__, __LINE__);
			pstatus->core_ops->putt_msg(MON_MSG_USB_CHARGER_IN);
			pstatus->message_status |= 0x1 << MONITOR_CHARGER_IN;
			pstatus->detect_valid = 0;	/* disable detection */
			goto out;	/* todo stop timer. */

		default:
			USB_ERR_PLACE;
			break;
		}
		goto out;
	} else {	/* power on, host detect phase. */

		ret = usb_timer_det_udisk(pstatus);
		status = pstatus->message_status & (0x1 << MONITOR_A_IN);
		if ((status != 0) && (ret == USB_DET_HOST_NONE)) {
			/*MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_A_OUT\n", __FILE__, __LINE__);*/
			pr_debug("\n%s--%d, SYS_MSG_USB_A_OUT\n", __FILE__, __LINE__);
			pstatus->core_ops->putt_msg(MON_MSG_USB_A_OUT);
			pstatus->message_status = pstatus->message_status & (~(0x1 << MONITOR_A_IN));
			goto out;
		}
		if (ret == USB_DET_HOST_UDISK) {
			p_hal->set_mode(p_hal, USB_IN_HOST_MOD);
			/*MONITOR_PRINTK("\n%s--%d, SYS_MSG_USB_A_IN\n", __FILE__, __LINE__);*/
			pr_debug("\n%s--%d, SYS_MSG_USB_A_IN\n", __FILE__, __LINE__);
			pstatus->core_ops->putt_msg(MON_MSG_USB_A_IN);
			pstatus->message_status |= 0x1 << MONITOR_A_IN;
			pstatus->detect_valid = 0;	/*disable detection*/
			goto out;	/* todo stop timer. */
		}
		goto out;
	}

out:
	pstatus->detect_running = 0;
	return;
}

static int set_monitor_detect_flag(umonitor_dev_status_t *pstatus, unsigned int status)
{
	int i;
	unsigned int ms_status = 0;	/* record is a in ? */
	usb_hal_monitor_t *p_hal = &pstatus->umonitor_hal;

	pstatus->check_cnt = 0;
	pstatus->det_phase = 0;
	pstatus->timer_steps = 0;

	if (status != 0) {	/*enable detect flag */
		p_hal->vbus_power_onoff(p_hal, 0);
		pstatus->vbus_enable_power = 0;

		if (pstatus->detect_valid == 0) {
			MONITOR_PRINTK("%s,%d\n", __FUNCTION__, __LINE__);
			pstatus->detect_valid = 1;
			goto out;
		} else {
			MONITOR_PRINTK("usb detection flag is already setted, %s,%d\n", __func__, __LINE__);
		}
	} else {	/*disable detection flag */
		i = 0;
		do {
			if (pstatus->detect_running == 0) {
				pstatus->detect_valid = 0;
				break;
			}
			msleep(20);
			++i;
		} while (i < 1000);
		MONITOR_PRINTK("enable detection flag\n");

		if (ms_status == 0) {
			/* make sure power is off. */
			p_hal->vbus_power_onoff(p_hal, 0);
			pstatus->vbus_enable_power = 0;
			p_hal->set_soft_id(p_hal, 1, 1);
		}
	}

out:
	if (pstatus->core_ops->wakeup_func != NULL) {
		pstatus->core_ops->wakeup_func();
	}
	return 0;
}

int umonitor_detection(unsigned int status)
{
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;

	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;
	MONITOR_PRINTK("umonitor_detection:%d\n", status);

	if (status != 0) {
		p_hal->dwc3_otg_mode_cfg(p_hal);
		p_hal->aotg_enable(p_hal, 1);
		p_hal->set_mode(p_hal, USB_IN_DEVICE_MOD);
		set_monitor_detect_flag(pstatus, 1);
	} else {
		p_hal->aotg_enable(p_hal, 0);
		set_monitor_detect_flag(pstatus, 0);
	}
	return 0;
}

int umonitor_core_init(umonitor_api_ops_t *core_ops,
	umon_port_config_t *port_config , void __iomem  *base, struct monitor_data *monitor_data)
{
	umonitor_dev_status_t *pstatus;
	umon_set_usb_plugin_type = (FUNC)kallsyms_lookup_name("atc260x_set_usb_plugin_type");

	pstatus = kmalloc(sizeof(umonitor_dev_status_t), GFP_KERNEL);
	if (pstatus == NULL)
		return -1;

	umonitor_status = pstatus;

	usb_hal_init_monitor_hw_ops(&pstatus->umonitor_hal, port_config, base, monitor_data);
	usb_init_monitor_status(pstatus);
	pstatus->core_ops = core_ops;
	pstatus->port_config = port_config;

	return 0;
}

int umonitor_core_exit(void)
{
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;

	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;

	p_hal->enable_irq(p_hal, 0);
	if (pstatus != NULL)
		kfree(pstatus);

	umonitor_status = NULL;
	return 0;
}

unsigned int umonitor_get_run_status(void)
{
	umonitor_dev_status_t *pstatus;

	pstatus = umonitor_status;

	return (unsigned int)pstatus->detect_valid;
}

unsigned int umonitor_get_message_status(void)
{
	umonitor_dev_status_t *pstatus;

	pstatus = umonitor_status;

	return (unsigned int)pstatus->message_status;
}

void umonitor_printf_debuginfo(void)
{
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;

	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;

	usb_monitor_debug_status_inf();
	pr_info("in printf_debuginfo\n");
	p_hal->debug(p_hal);

	return;
}

int umonitor_vbus_power_onoff(int value)
{
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;

	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;
	pstatus->vbus_enable_power = value;

	return p_hal->vbus_power_onoff(p_hal, value);
}

int umonitor_core_suspend(void)
{
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;

	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;

	pstatus->detect_valid = 0;

	pr_info("SUSPEND pstatus->message_status is %d!!!!!!!!!!!!!!\n", pstatus->message_status);

	/* if dts config to shut down vbus, do it here; if not ,keep vbus status*/
	if (pstatus->port_config->suspend_keep_vbus == 0) {
		if (pstatus->vbus_enable_power && p_hal->vbus_power_onoff)
			p_hal->vbus_power_onoff(p_hal, 0);
	}
	p_hal->suspend_or_resume(p_hal, 1);

	return 0;
}

int umonitor_core_resume(void)
{
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;
	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;

	pr_info("RESUME pstatus->message_status is %d!!!!!!!!!!!!!!\n", pstatus->message_status);

	if ((pstatus->message_status & (0x1 << MONITOR_B_IN)) != 0) {
		pr_info(KERN_DEBUG"RESUME SNED B_OUT\n");
		pstatus->core_ops->putt_msg(MON_MSG_USB_B_OUT);
		pstatus->message_status &= ~(0x1 << MONITOR_B_IN);
	}
	if ((pstatus->message_status & (0x1 << MONITOR_A_IN)) != 0) {
		pr_info(KERN_DEBUG"RESUME SNED A_OUT\n");
		/*p_hal->vbus_power_onoff(p_hal,  1);
		pstatus->vbus_enable_power = 1;*/
		pstatus->core_ops->putt_msg(MON_MSG_USB_A_OUT);
		pstatus->message_status &= ~(0x1 << MONITOR_A_IN);
	}
	p_hal->suspend_or_resume(p_hal, 0);
	umonitor_detection(1);
	return 0;
}

int umonitor_dwc_otg_init(void)
{
	umonitor_dev_status_t *pstatus;
	usb_hal_monitor_t *p_hal;

	pstatus = umonitor_status;
	p_hal = &pstatus->umonitor_hal;

	p_hal->dwc3_otg_init(p_hal);

	return 0;
}
