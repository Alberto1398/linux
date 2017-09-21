/*
 * Actions OWL SoCs usb2.0 controller driver
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>
#include <asm/irq.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
#include <asm/prom.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#include "aotg_hcd.h"
#include "aotg_regs.h"
#include "aotg_plat_data.h"
#include "aotg.h"

int aotg_device_init(int dev_id);
int aotg_device_exit(int dev_id);
void aotg_power_onoff(int pin_no, int status);

/* usbecs register. */
#define	USB2_ECS_VBUS_P0		10
#define	USB2_ECS_ID_P0			12
#define USB2_ECS_LS_P0_SHIFT	8
#define USB2_ECS_LS_P0_MASK		(0x3<<8)
#define USB2_ECS_DPPUEN_P0     3
#define USB2_ECS_DMPUEN_P0     2
#define USB2_ECS_DMPDDIS_P0    1
#define USB2_ECS_DPPDDIS_P0    0
#define USB2_ECS_SOFTIDEN_P0   (1<<26)
#define USB2_ECS_SOFTID_P0     27
#define USB2_ECS_SOFTVBUSEN_P0 (1<<24)
#define USB2_ECS_SOFTVBUS_P0   25
#define USB2_PLL_EN0           (1<<12)
#define USB2_PLL_EN1           (1<<13)

extern struct aotg_plat_data aotg_data[2];
extern int is_ls_device[2];
extern unsigned int aotg_wake_lock[2];
extern enum ic_type_e ic_type[2];
struct aotg_uhost_mon_t *aotg_uhost_mon[2];
void aotg_dev_plugout_msg(int id);

int usb2_set_dp_500k_15k(struct aotg_uhost_mon_t *umon, int enable_500k_up, int enable_15k_down)
{
	unsigned int val;

	val = readl(aotg_data[umon->id].usbecs) & (~((1 << USB2_ECS_DPPUEN_P0) |
			(1 << USB2_ECS_DMPUEN_P0) |
			(1 << USB2_ECS_DMPDDIS_P0) |
			(1 << USB2_ECS_DPPDDIS_P0)));

	if (enable_500k_up != 0)
		val |= (1 << USB2_ECS_DPPUEN_P0)|(1 << USB2_ECS_DMPUEN_P0);

	if (enable_15k_down == 0) {
		val |= (1 << USB2_ECS_DPPDDIS_P0)|(1 << USB2_ECS_DMPDDIS_P0);
	}

	writel(val, aotg_data[umon->id].usbecs);
	return 0;
}

/* return dp, dm state. */
static inline unsigned int usb_get_linestates(struct aotg_uhost_mon_t *umon)
{
	unsigned int state;

	state = ((readl(aotg_data[umon->id].usbecs) & USB2_ECS_LS_P0_MASK) >> USB2_ECS_LS_P0_SHIFT);
	return state;
}

static  unsigned int usb_get_vbusstat(struct aotg_uhost_mon_t *umon)
{
	unsigned int state;

	state = !!(readl(aotg_data[umon->id].usbecs) & (1<<USB2_ECS_VBUS_P0));
	return state;
}
static  unsigned int usb_get_idpinstat(struct aotg_uhost_mon_t *umon)
{
	unsigned int state;

	state = !!(readl(aotg_data[umon->id].usbecs) & (1<<USB2_ECS_ID_P0));
	return state;
}
static  unsigned int usb_get_stat(struct aotg_uhost_mon_t *umon)
{
	unsigned int state;

	/*if not enble udc mode,only detect linestate */
	if (!aotg_udc_enable[umon->id])
		state = usb_get_linestates(umon);
	else {
		if (umon->det_mode == HCD_MODE) {
			aotg_power_onoff(umon->id, 1);
			state = usb_get_idpinstat(umon);
		} else if (umon->det_mode == UDC_MODE) {
			state = usb_get_vbusstat(umon);
			aotg_power_onoff(umon->id, 0);
		}
	}
	return state;
}

static  void usb_disable_soft_vbus_idpin(struct aotg_uhost_mon_t *umon)
{
	writel(readl(aotg_data[umon->id].usbecs) & (~(USB2_ECS_SOFTVBUSEN_P0 | USB2_ECS_SOFTIDEN_P0)),
		aotg_data[umon->id].usbecs);
}

/*
*	if enalbe udc mode :we detect idpin & vbus state to identify host/device mode
*					  in this case ,hardware need to connect idpin & vbus pin to IC
*	if udc mode not enable(host mode only): just checkout dp/dm state for plugin .
*	This monitor only detect usb plug in, the plug out action will be detected by controller itself.
*/
static void aotg_uhost_mon_timer(unsigned long data)
{
	struct aotg_uhost_mon_t *umon = (struct aotg_uhost_mon_t *)data;

	if ((!umon) || (!umon->aotg_det))
		return;

	if ((aotg_udc_enable[umon->id]) && (umon->aotg_det == 2)) {
		umon->state = usb_get_stat(umon);
		if (((umon->det_mode == HCD_MODE) && (umon->state == 0)) ||
			((umon->det_mode == UDC_MODE) && ((umon->state == 1) && (usb_get_idpinstat(umon) == 1)))) {
			/*not plug out,continue to check*/
			mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(1000));
			umon->det_step = 0;
			return;
		}
		/*detect pluged out,close controller*/
		umon->aotg_det = 0;
		umon->det_step = 0;
		aotg_dev_plugout_msg(umon->id);
		return;
	}

	if (umon->det_step == 0) { /*step 0*/
		/*if  udc mode not enbled, before detect linestate ,pull down 15k*/
		if (!aotg_udc_enable[umon->id])
			if (umon->det_mode == HCD_MODE)
				usb2_set_dp_500k_15k(umon, 0, 1);

		umon->det_step++;
		mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(100));

	} else if (umon->det_step == 1) { /*step 1*/
		umon->det_step++;
		umon->state = usb_get_stat(umon);
		umon->old_state = umon->state;
		mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(400));
		if (aotg_udc_enable[umon->id]) {
			if (((umon->det_mode == HCD_MODE) && (umon->state == 1)) || \
				  ((umon->det_mode == UDC_MODE) && (umon->state == 0)) ||
				  ((umon->det_mode == UDC_MODE) && usb_get_idpinstat(umon) == 0)) {
					/*wrong stats! change det_mode & re-detect again*/
					umon->det_mode = umon->det_mode % 2 + 1;
					umon->det_step = 0;
			}
		} else {
			/*if not enble udc mode,check linestate */
			if (umon->state == 0 ||  umon->state == 3)
				umon->det_step = 0;
		}

	} else {  /*step 2 : double checkout stat for debounce*/
		umon->state = usb_get_stat(umon);
		if (umon->state == umon->old_state) {
			if (umon->det_mode == HCD_MODE)
				if (usb_get_linestates(umon) == 2)
					is_ls_device[umon->id] = 1;
			if (umon->det_mode == UDC_MODE) {
				aotg_power_onoff(umon->id, 0);
				/* check if a device plug in with 5v on vbus*/
				if (!usb_get_idpinstat(umon))
					goto detect_again;
			}
			if (aotg_udc_enable[umon->id]) {
				umon->aotg_det = 2;
				mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(1000));
			} else
				umon->aotg_det = 0;
			umon->det_step = 0;
			queue_delayed_work(umon->aotg_dev_onoff, &umon->aotg_dev_init, msecs_to_jiffies(1));
		} else {
detect_again:
			mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(400));
			umon->aotg_det = 1;
			umon->det_step = 0;
		}
	}
	return;
}

static void aotg_dev_register(struct work_struct *w)
{
	struct aotg_uhost_mon_t *umon = container_of(w, struct aotg_uhost_mon_t, aotg_dev_init.work);
	pm_runtime_put_sync(aotg_data[umon->id].dev);
	pm_runtime_disable(aotg_data[umon->id].dev);
	if (umon->det_mode == UDC_MODE)
		wake_lock(&umon->aotg_wake_lock);
	else if (!aotg_wake_lock[umon->id])
		wake_lock_timeout(&umon->aotg_wake_lock, 10*HZ);
	aotg_mode[umon->id] = umon->det_mode;
	aotg_device_init(umon->id);
	return;
}

static void aotg_dev_unregister(struct work_struct *w)
{
	struct aotg_uhost_mon_t *umon = container_of(w, struct aotg_uhost_mon_t, aotg_dev_exit.work);
	int ret = 0;

	lock_system_sleep();
	if (!aotg_wake_lock[umon->id])
		wake_lock_timeout(&umon->aotg_wake_lock, 10*HZ);
	unlock_system_sleep();

	/* ret < 0 if aotg_device_exit has been called before or
	 * no need to call right now.
	 * For example, when unplug OTG line, timer and interrupt
	 * handler maybe come here both.
	 * No need to do the following twice, it'll make aotg weird!
	 */
	ret = aotg_device_exit(umon->id);
	if (ret < 0)
		return;
	usb_disable_soft_vbus_idpin(umon);
	umon->aotg_det = 1;
	umon->det_mode = HCD_MODE;
	pm_runtime_enable(aotg_data[umon->id].dev);
	pm_runtime_get_sync(aotg_data[umon->id].dev);
	is_ls_device[umon->id] = 0;
	mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(100));
	return;
}

void aotg_dev_plugout_msg(int id)
{
	pr_debug("usb%d had been plugged out!\n", id);

	aotg_uhost_mon[id]->old_state = 0;
	queue_delayed_work(aotg_uhost_mon[id]->aotg_dev_onoff, &aotg_uhost_mon[id]->aotg_dev_exit, msecs_to_jiffies(500));
	return;
}

static struct aotg_uhost_mon_t *aotg_uhost_mon_alloc(void)
{
	struct aotg_uhost_mon_t *umon = NULL;

	umon = kzalloc(sizeof(*umon), GFP_KERNEL);
	if (!umon)
		return NULL;

	init_timer(&umon->hotplug_timer);
	umon->hotplug_timer.function = aotg_uhost_mon_timer;
	umon->hotplug_timer.data = (unsigned long)umon;

	INIT_DELAYED_WORK(&umon->aotg_dev_init, aotg_dev_register);
	INIT_DELAYED_WORK(&umon->aotg_dev_exit, aotg_dev_unregister);

	umon->aotg_det = 1;
	umon->det_mode = HCD_MODE;
	return umon;
}

void aotg_uhost_mon_init(int id)
{
	if (aotg_udc_enable[id]) {
		if (!aotg_udc_enable[1-id])
			aotg_device_exit(id);
		else
			pr_err("err:only one aotg_udc can be enable!\n");
	}
	/*in device mode close vbus; in host mode open vbus*/
	aotg_power_onoff(id, !aotg_udc_enable[id]);

	if (!port_host_plug_detect[id])
		return;

	aotg_uhost_mon[id] = aotg_uhost_mon_alloc();
	aotg_uhost_mon[id]->id = id;
	if (id)
		aotg_uhost_mon[id]->aotg_dev_onoff = create_singlethread_workqueue("aotg_dev1_onoff");
	else
		aotg_uhost_mon[id]->aotg_dev_onoff = create_singlethread_workqueue("aotg_dev0_onoff");
	pm_runtime_enable(aotg_data[id].dev);
	pm_runtime_get_sync(aotg_data[id].dev);
	if (ic_type[id] == S700)
		clk_prepare_enable(aotg_data[id].clk_usbh_pllen);

	wake_lock_init(&aotg_uhost_mon[id]->aotg_wake_lock, WAKE_LOCK_SUSPEND, "aotg_wake_lock");
	if (aotg_wake_lock[id])
		wake_lock(&aotg_uhost_mon[id]->aotg_wake_lock);
	pr_info("start mon %d ......\n", id);
	mod_timer(&aotg_uhost_mon[id]->hotplug_timer, jiffies + msecs_to_jiffies(HZ));
	return;
}

static int aotg_uhost_mon_free(int id)
{
	clk_disable_unprepare(aotg_data[id].clk_usbh_pllen);
	clk_disable_unprepare(aotg_data[id].clk_usbh_phy);
	if (ic_type[id] != S700)
		clk_disable_unprepare(aotg_data[id].clk_usbh_cce);
	pm_runtime_put_sync(aotg_data[id].dev);
	pm_runtime_disable(aotg_data[id].dev);

	if (aotg_uhost_mon[id]->aotg_dev_onoff) {
		cancel_delayed_work_sync(&aotg_uhost_mon[id]->aotg_dev_init);
		cancel_delayed_work_sync(&aotg_uhost_mon[id]->aotg_dev_exit);
		flush_workqueue(aotg_uhost_mon[id]->aotg_dev_onoff);
		destroy_workqueue(aotg_uhost_mon[id]->aotg_dev_onoff);
	}
	del_timer_sync(&aotg_uhost_mon[id]->hotplug_timer);
	wake_unlock(&aotg_uhost_mon[id]->aotg_wake_lock);
	kfree(aotg_uhost_mon[id]);
	return 0;
}

void aotg_uhost_mon_exit(void)
{
	if (port_host_plug_detect[0] && (port_host_plug_detect[0] != 2))
		aotg_power_onoff(0, 0);

	if (port_host_plug_detect[1] && (port_host_plug_detect[1] != 2))
		aotg_power_onoff(1, 0);

	if (aotg_uhost_mon[0])
		aotg_uhost_mon_free(0);
	if (aotg_uhost_mon[1])
		aotg_uhost_mon_free(1);
	return;
}
