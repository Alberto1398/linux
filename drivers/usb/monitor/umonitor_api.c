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
#include <linux/of_device.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/switch.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>

#include "umonitor_config.h"
#include "umonitor_core.h"
#include "aotg_regs.h"

#ifdef CONFIG_OWL_USB2PHY
#include "../phy/phy-owl-usb.h"
#endif

/*
 * In mini-charger mode, there is no way to control dwc3 run/stop.
 * In android mode, vold will rule this or monitor do itself.
 * boot_charger_mode is used to simulate monitor-only mode.
 */
static unsigned int boot_charger_mode;

static int __init set_boot_charger_mode(char *str)
{
	boot_charger_mode = 1;
	pr_info("androidboot.mode=charger\n");

	return 1;
}

__setup("androidboot.mode=charger", set_boot_charger_mode);

/*
 * This flag is used to control if usb devices will be resumed.
 * S700 does not try to resume all usb devices during xhci_resume.
 * But we will resume all usb devices when system suspend fails.
 * It is set when monitor resume compltes, and cleared by xhci.
 */
static int usb3_set_device_noattached_flag;

typedef void (*FUNC)(int);
FUNC set_usb_plugin_type;

#define USB_NO_PLUGGED	0
#define CONNECT_USB_PORT	1
#define CONNECT_USB_ADAPTOR	2

extern FUNC umon_set_usb_plugin_type;

struct delayed_work monitor_work;
struct delayed_work monitor_resume_work;
atomic_t wake_lock_atomic;
unsigned int monitor_work_pending;
unsigned int wake_lock_register_cnt;

#if SUPPORT_NOT_RMMOD_USBDRV
extern int dwc3_set_plugstate(int s);
extern int xhci_set_plugstate(int s);

int xhci_set_plugstate(int s)
{
	pr_debug("%s s=%d\n", __func__, s);
	return 0;
}

enum plugstate {
	PLUGSTATE_A_OUT = 0,
	PLUGSTATE_B_OUT,
	PLUGSTATE_A_IN,
	PLUGSTATE_B_IN,
	PLUGSTATE_A_SUSPEND,
	PLUGSTATE_A_RESUME,
	PLUGSTATE_B_SUSPEND,
	PLUGSTATE_B_RESUME,
};
#endif
extern umonitor_dev_status_t *umonitor_status;

struct mon_sysfs_status {
	unsigned int charger_connected;
	unsigned int pc_connected;
	unsigned int udisk_connected;
};

struct port_dev {
	struct switch_dev sdev;
	char state_msg[64];
};

struct monitor_con {
	unsigned int thread_chk_times;
	int run;

	spinlock_t lock;
	struct mutex mon_mutex;
	struct port_dev port_dev;
	umon_port_config_t port_config;
	umonitor_api_ops_t port_ops;
	struct kobject *mon_obj;	/* just for sysfs. */
	struct kobject *mon_port;
	struct mon_sysfs_status port_status;

	atomic_t port_exit;
	struct task_struct *tsk;
	wait_queue_head_t mon_wait;

	volatile unsigned int det_plugin_req;
	volatile unsigned int det_plugout_req;
	struct timer_list port_timer;
	struct timer_list check_timer;
	struct wake_lock monitor_wake_lock;
#if SUPPORT_NOT_RMMOD_USBDRV
	int dwc3_status;
	int xhci_status;
	int dwc3_timeout_cnt;
	int xhci_timeout_cnt;
	int (*dwc3_set_plugstate)(int s);
	int (*xhci_set_plugstate)(int s);
#endif
	void __iomem  *base;
	int probe_fail;
	int resume_status;
};

struct monitor_con monitor_mon_dev;
static struct monitor_con *my_mon = &monitor_mon_dev;
static unsigned long pm_status;

static ssize_t mon_status_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf);
static ssize_t mon_config_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf);
static ssize_t mon_config_store(struct kobject *kobj,
				struct kobj_attribute *attr, const char *instr, size_t bytes);

#define ATTRCMP(x) (0 == strcmp(attr->attr.name, #x))
#define MONITOR_ATTR_LIST(x)    (&x ## _attribute.attr)
#define MON_STATUS_ATTR(x)  static struct kobj_attribute x##_attribute = \
		__ATTR(x, S_IRUGO, mon_status_show,  NULL)
#define MON_CONFIG_ATTR(x)  static struct kobj_attribute x##_attribute = \
		__ATTR(x, (S_IRUGO|S_IWUGO), mon_config_show, mon_config_store)

	/* default just for usb port0. */
static void mon_port_putt_msg(unsigned int msg);
static int usb_detect_plugout_event(void);
int usb_set_vbus_power(int value);
static void monitor_resume_delay_work(struct work_struct *work);

#ifdef SUPPORT_NOT_RMMOD_USBDRV
static void monitor_handle_plug_in_out_msg(char *usb_con_msg)
{
	if (!strncmp(usb_con_msg, "USB_B_IN", 8)) {
		my_mon->dwc3_set_plugstate(PLUGSTATE_B_IN);
		my_mon->xhci_set_plugstate(PLUGSTATE_B_IN);
	} else if (!strncmp(usb_con_msg, "USB_B_OUT", 9)) {
		my_mon->xhci_set_plugstate(PLUGSTATE_B_OUT);
		my_mon->dwc3_set_plugstate(PLUGSTATE_B_OUT);
	} else if (!strncmp(usb_con_msg, "USB_A_IN", 8)) {
		my_mon->dwc3_set_plugstate(PLUGSTATE_A_IN);
		my_mon->xhci_set_plugstate(PLUGSTATE_A_IN);
	} else if (!strncmp(usb_con_msg, "USB_A_OUT", 9)) {
		my_mon->xhci_set_plugstate(PLUGSTATE_A_OUT);
		my_mon->dwc3_set_plugstate(PLUGSTATE_A_OUT);
	} else {
		pr_info("usb monitor handle msg err! %s\n", usb_con_msg);
	}
}
#endif
	/* check in and out message, and check whether it accordant with driver state. */
static int usb_check_msg_and_drv_state(void)
{
	usb_hal_monitor_t *p_hal;
	umonitor_dev_status_t *pstatus;
	pstatus = umonitor_status;
	p_hal = &umonitor_status->umonitor_hal;

	my_mon->thread_chk_times++;
	/* at system startup will do a plugout, make 5 seconds to detect usb status
	 * after that will modify the wrong status by checking udisk_connected & pc_connected
	 */
	if (my_mon->thread_chk_times < 5)
		return 0;

	if (my_mon->port_status.udisk_connected == 0) {
		if ((my_mon->xhci_status == PLUGSTATE_A_IN) ||
		    (my_mon->xhci_status == PLUGSTATE_A_SUSPEND) ||
		    (my_mon->xhci_status == PLUGSTATE_A_RESUME)) {
			my_mon->xhci_timeout_cnt++;
			goto mon_adjust_state;
		}
	} else {
		if (my_mon->xhci_status != PLUGSTATE_A_IN) {
			my_mon->xhci_timeout_cnt++;
			goto mon_adjust_state;
		}
	}

	if (my_mon->port_status.pc_connected == 0) {
		if (my_mon->dwc3_status == PLUGSTATE_B_IN) {
			my_mon->dwc3_timeout_cnt++;
			goto mon_adjust_state;
		}
	} else {
		if (my_mon->dwc3_status != PLUGSTATE_B_IN) {
			my_mon->dwc3_timeout_cnt++;
			goto mon_adjust_state;
		}
	}
	return 0;

mon_adjust_state:
	pr_info("xhci_status:%d, dwc3:%d\n", my_mon->xhci_status, my_mon->dwc3_status);
	pr_info("udisk_con:%d, pc:%d\n", my_mon->port_status.udisk_connected, my_mon->port_status.pc_connected);

	/* process host unnormal state. */
	if (my_mon->xhci_status == PLUGSTATE_A_SUSPEND) {
		p_hal->suspend_or_resume(p_hal, 0);
		my_mon->dwc3_set_plugstate(PLUGSTATE_A_RESUME);
		my_mon->xhci_set_plugstate(PLUGSTATE_A_RESUME);
		wake_lock_timeout(&my_mon->monitor_wake_lock, 12*HZ);
		pr_info("----now lock for 12*HZ\n");
	}
	if (my_mon->port_status.udisk_connected == 0) {
		if ((my_mon->xhci_status == PLUGSTATE_A_IN) || (my_mon->xhci_status == PLUGSTATE_A_RESUME)) {
			if (my_mon->xhci_timeout_cnt > 4) {
				pr_info("%s:%d! \n", __func__, __LINE__);
				my_mon->xhci_timeout_cnt = 0;
				my_mon->xhci_set_plugstate(PLUGSTATE_A_OUT);
				my_mon->dwc3_set_plugstate(PLUGSTATE_A_OUT);
				umonitor_detection(1);
			}
		}
	} else {
		if (my_mon->xhci_status != PLUGSTATE_A_IN) {
			if (my_mon->xhci_timeout_cnt > 10) {
				pr_info("%s:%d!\n", __func__, __LINE__);
				my_mon->xhci_timeout_cnt = 0;
				pstatus->core_ops->putt_msg(MON_MSG_USB_A_OUT);
				pstatus->message_status &= ~(0x1 << MONITOR_A_IN);
				/* umonitor_core_resume(); */
				umonitor_detection(1);
			}
		}
	}

	/* process device unnormal state. */
	if (my_mon->port_status.pc_connected == 0) {
		if (my_mon->dwc3_status == PLUGSTATE_B_IN) {
			if (my_mon->dwc3_timeout_cnt > 10) {
				pr_info("%s:%d!\n", __func__, __LINE__);
				my_mon->dwc3_timeout_cnt = 0;
				pstatus->core_ops->putt_msg(MON_MSG_USB_B_OUT);
				pstatus->message_status &= ~(0x1 << MONITOR_B_IN);
				/*my_mon->xhci_set_plugstate(PLUGSTATE_B_OUT);
				my_mon->dwc3_set_plugstate(PLUGSTATE_B_OUT);*/
				umonitor_detection(1);
			}
		}
	} else {
		if (my_mon->dwc3_status != PLUGSTATE_B_IN) {
			if (my_mon->dwc3_timeout_cnt > 10) {
				pr_info("%s:%d!\n", __func__, __LINE__);
				my_mon->dwc3_timeout_cnt = 0;
				pstatus->core_ops->putt_msg(MON_MSG_USB_B_OUT);
				pstatus->message_status &= ~(0x1 << MONITOR_B_IN);
				umonitor_detection(1);
			}
		}
	}

	return 1;
}

static int mon_thread_port(void *data)
{
	unsigned int mtime = 0;

	while (!kthread_should_stop() && (atomic_read(&my_mon->port_exit) == 0)) {
		if (my_mon->run == 0)
			wait_event_interruptible(my_mon->mon_wait, my_mon->run);

		wait_event_interruptible(my_mon->mon_wait, (my_mon->det_plugin_req != 0) ||
					 kthread_should_stop() ||
					 (my_mon->det_plugout_req != 0));
		mutex_lock(&my_mon->mon_mutex);

		if (usb_check_msg_and_drv_state()) {
			spin_lock(&my_mon->lock);
			if (my_mon->det_plugout_req) {
				my_mon->det_plugout_req = 0;
				mod_timer(&my_mon->check_timer, jiffies + (CHECK_TIMER_INTERVAL*HZ)/2000 + (CHECK_TIMER_INTERVAL*HZ)/1000);
			}
			if (my_mon->det_plugin_req) {
				my_mon->det_plugin_req = 0;
				mod_timer(&my_mon->port_timer, jiffies + 3 + (CHECK_TIMER_INTERVAL*HZ)/2000 + (CHECK_TIMER_INTERVAL*HZ)/1000);
			}
			spin_unlock(&my_mon->lock);
			mutex_unlock(&my_mon->mon_mutex);
			continue;
		}

		if (my_mon->det_plugout_req == 1) {
			usb_detect_plugout_event();
			my_mon->det_plugout_req = 0;
			mutex_unlock(&my_mon->mon_mutex);
			continue;
		}

		if (my_mon->det_plugin_req) {
			umonitor_timer_func();
			mtime = umonitor_get_timer_step_interval();
			spin_lock(&my_mon->lock);
			mod_timer(&my_mon->port_timer, jiffies+(mtime*HZ)/1000);
			my_mon->det_plugin_req = 0;
			spin_unlock(&my_mon->lock);
			mutex_unlock(&my_mon->mon_mutex);
			continue;
		}
	}

	/* mon_thread_port0 already exited */
	MONITOR_PRINTK("monitor: mon_thread_port0 is going to exit.\n");
	atomic_set(&my_mon->port_exit, 0);
	return 0;
}

static void mon_timer_func_port(unsigned long h)
{
	if (!spin_trylock(&my_mon->lock)) {
		MONITOR_PRINTK("timer0 my_mon->lock unavailable!\n");
		mod_timer(&my_mon->port_timer, jiffies+1);
		return;
	}
	my_mon->det_plugin_req = 1;
	spin_unlock(&my_mon->lock);
	wake_up(&my_mon->mon_wait);
	return;
}

static void mon_timer_func_check(unsigned long h)
{
	if (!spin_trylock(&my_mon->lock)) {
		mod_timer(&my_mon->check_timer, jiffies+1);
		return;
	}
	my_mon->det_plugout_req = 1;
	spin_unlock(&my_mon->lock);
	wake_up(&my_mon->mon_wait);
	return;
}

static ssize_t umonitor_switch_state(struct switch_dev *sdev, char *buf)
{
	struct port_dev *port_dev = container_of(sdev, struct port_dev, sdev);
	return sprintf(buf, "%s\n", port_dev->state_msg);
}

/*release wake lock*/
static void monitor_release_wakelock(struct work_struct *work)
{
	if (wake_lock_register_cnt > 0) {
		wake_lock_register_cnt--;
		wake_unlock(&my_mon->monitor_wake_lock);
	}
}

#ifdef CONFIG_OF

static  struct monitor_data  owl_s700_data = {
	.ic_type = OWL_S700,
	.io_base = 0xe0400000,
	.usbecs = 0xE024C000+0x0090,
	.usb3_p0_ctl = {
		.VBUS_P0	= 5,
		.ID_P0	= 11,
		.DPPUEN_P0	= 14,
		.DMPUEN_P0	= 15,
		.DPPDDIS_P0	= 12,
		.DMPDDIS_P0	= 13,
		.SOFTIDEN_P0	= 8,	/*reserved*/
		.SOFTID_P0	= 9,	/*reserved*/
		.SOFTVBUSEN_P0	= 6,
		.SOFTVBUS_P0	= 7,
		.LS_P0_SHIFT	= 3,
		.LS_P0_MASK	= 0x3<<3,
		.ECS_DPPDDIS	= 1<<12,	/*reserved*/
		.ECS_DMPDDIS	= 1<<13,	/*reserved*/
		.ECS_DPPUEN	= 1<<14,
		.ECS_DMPUEN	= 1<<15,
		.VBUS_DET_THRESHOLD = VBUS_DET_THRESHOLD_LEVEL3_S700,
	},
};

static  struct monitor_data  owl_s900_data = {
	.ic_type = OWL_S900,
	.io_base = 0xe0400000,
	.usbecs = 0xE0228000+0x0090,
	.usb3_p0_ctl = {
		.VBUS_P0	= 10,
		.ID_P0	= 12,
		.DPPUEN_P0	= 2,
		.DMPUEN_P0	= 3,
		.DPPDDIS_P0	= 0,
		.DMPDDIS_P0	= 1,
		.SOFTIDEN_P0	= 8,
		.SOFTID_P0	= 9,
		.SOFTVBUSEN_P0	= 24,
		.SOFTVBUS_P0	= 25,
		.LS_P0_SHIFT	= 8,
		.LS_P0_MASK	= 0x3<<8,
		.ECS_DPPDDIS	= 1<<0,
		.ECS_DMPDDIS	= 1<<1,
		.ECS_DPPUEN	= 1<<2,
		.ECS_DMPUEN	= 1<<3,
		.VBUS_DET_THRESHOLD = VBUS_DET_THRESHOLD_LEVEL4_S900,
	},
};


static const struct of_device_id  owl_monitor_of_match[]   = {
	{.compatible = "actions,s700-usbmonitor", .data = &owl_s700_data },
	{.compatible = "actions,s900-usbmonitor", .data = &owl_s900_data },
	{}
};
MODULE_DEVICE_TABLE(of, owl_monitor_of_match);
#else
static void monitor_release(struct device *dev)
{
	MONITOR_PRINTK("monitor_release\n");
}

static struct platform_device monitor_device = {
	.name = "usb_monitor",
	.id = -1,
	.num_resources = 0,
	.resource = NULL,
	.dev = {
		.release = monitor_release,
	},
};
#endif
static int get_configuration_from_dts(struct of_device_id *id)
{
	int ret = 0;
	struct device_node *fdt_node;
	const char *pp;
	enum of_gpio_flags flags;
	umon_port_config_t *port_config = &my_mon->port_config;

	fdt_node = of_find_compatible_node(NULL, NULL,  id->compatible);
	if (NULL == fdt_node) {
		pr_info("<umonitor>err: no usb3-fdt-compatible [%s]\n",  id->compatible);
		goto DEFAULT_SETTING;
	}

	pp = of_get_property(fdt_node, "detect_type", NULL);
	if (!pp) {
		pr_info("<umonitor>err: no config detect_type!use default value:UMONITOR_HOST_AND_DEVICE\n");
		port_config->detect_type = UMONITOR_HOST_AND_DEVICE;
	} else {
		port_config->detect_type = be32_to_cpup((const __be32 *)pp);
	}

	pp = of_get_property(fdt_node, "idpin_type", NULL);
	if (!pp) {
		pr_info("<umonitor>err: no config idpin_type!use default value:UMONITOR_USB_IDPIN\n");
		port_config->idpin_type = UMONITOR_USB_IDPIN;
	} else {
		port_config->idpin_type = be32_to_cpup((const __be32 *)pp);
	}

	if (port_config->idpin_type == UMONITOR_GPIO_IDPIN) {
		if (of_find_property(fdt_node, "idpin_gpio", NULL)) {
			port_config->idpin_gpio_no = of_get_named_gpio_flags(fdt_node, "idpin_gpio", 0, &flags);
			if (gpio_request(port_config->idpin_gpio_no,  id->compatible)) {
				pr_err("<umonitor>err: fail to request idpin gpio [%d]\n", port_config->idpin_gpio_no);
				port_config->idpin_gpio_no = -1;
				return -1;
			} else {
				gpio_direction_input(port_config->idpin_gpio_no);
				pr_info("<umonitor> : idpin_type is GPIO_IDPIN, gpio_no = %d\n", port_config->idpin_gpio_no);
			}
		} else {
			port_config->idpin_type = UMONITOR_USB_IDPIN;
			pr_info("<umonitor>err : idpin_type is GPIO_IDPIN,but can't find idpin gpio,set idpin_type = UMONITOR_USB_IDPIN\n");
		}

	}

	pp = of_get_property(fdt_node, "vbus_type", NULL);
	if (!pp) {
		pr_info("<umonitor>err: no config vbus_type!use default value:UMONITOR_DC5V_VBUS\n");
		port_config->vbus_type = UMONITOR_DC5V_VBUS;
	} else {
		port_config->vbus_type = be32_to_cpup((const __be32 *)pp);
	}

	pp = of_get_property(fdt_node, "idpin_debug", NULL);
	if (!pp) {
		pr_info("<umonitor>err: no config idpin_debug! use default value:0xff\n");
		port_config->idpin_debug = 0xff;
	} else {
		port_config->idpin_debug = be32_to_cpup((const __be32 *)pp);
	}

	pp = of_get_property(fdt_node, "force_detect", NULL);
	if (!pp) {
		pr_info("<umonitor>err: no config force_detect! use default value:0\n");
		port_config->force_detect = 0;
	} else {
		port_config->force_detect = be32_to_cpup((const __be32 *)pp);
	}

	pp = of_get_property(fdt_node, "suspend_keep_vbus", NULL);
	if (!pp) {
		pr_info("<umonitor>err: no config suspend_keep_vbus! use default value:0\n");
		port_config->suspend_keep_vbus = 0;
	} else {
		port_config->suspend_keep_vbus = be32_to_cpup((const __be32 *)pp);
	}

	if (!of_find_property(fdt_node, "vbus_otg_en_gpios", NULL)) {
		pr_info("<umonitor>err: no config vbus_otg_engpio!use default value:GPIOA19 ,active high\n");
		port_config->power_switch_gpio_no = -1;
		port_config->power_switch_active_level = 1;
		goto REQUEST_OTG_VBUS;
	}
	port_config->power_switch_gpio_no = of_get_named_gpio_flags(fdt_node, "vbus_otg_en_gpios", 0, &flags);
	port_config->power_switch_active_level = flags & OF_GPIO_ACTIVE_LOW;

REQUEST_OTG_VBUS:

	pr_info("====otgvbus_gpio: num-%d, active-%s---detect_type=%d,idpin_type=%d,vbus_type=%d---\n",\
		port_config->power_switch_gpio_no, port_config->power_switch_active_level ? "high" : "low", port_config->detect_type,\
		port_config->idpin_type, port_config->vbus_type);
	if (gpio_request(port_config->power_switch_gpio_no,  id->compatible)) {
		pr_err("<umonitor>err: fail to request vbus gpio [%d]\n", port_config->power_switch_gpio_no);
		port_config->power_switch_gpio_no = -1;
		return -1;
	}
	/* power gpio oupru, power switch off */
	if (port_config->power_switch_gpio_no >= 0) {
		gpio_direction_output(port_config->power_switch_gpio_no, port_config->power_switch_active_level);
	}
	return ret;

DEFAULT_SETTING:
	pr_info("<umonitor>err: use default setting...\n");
	port_config->detect_type = UMONITOR_HOST_AND_DEVICE;
	port_config->idpin_type = UMONITOR_USB_IDPIN;
	port_config->vbus_type = UMONITOR_DC5V_VBUS;
	port_config->power_switch_gpio_no = -1;	/*GPIOD1 */
	port_config->power_switch_active_level = 1;	/* 0: low active; 1: high active*/
	goto REQUEST_OTG_VBUS;
}

static int s_dwc3_set_plugstate(int s)
{
	my_mon->dwc3_timeout_cnt = 0;
	if (my_mon->dwc3_status == s)
		return 0;
	my_mon->dwc3_status = s;
	return dwc3_set_plugstate(s);
}

static int s_xhci_set_plugstate(int s)
{
	my_mon->xhci_timeout_cnt = 0;
	if (my_mon->xhci_status == s)
		return 0;
	my_mon->xhci_status = s;
	return xhci_set_plugstate(s);
}

static int monitor_probe(struct platform_device *_dev)
{
	int ret = 0;
	void __iomem  *base;
	struct monitor_data *monitor_owl;
	struct of_device_id *id = (struct of_device_id *)of_match_device(owl_monitor_of_match, &_dev->dev);
    struct device_node *fdt_node;
    
	if (id == NULL) {
		pr_info("<umonitor>err: get dts fail !!\n");
		ret = -EINVAL;
		goto	err_monitor_probe_fail;
	}

	monitor_owl = (struct monitor_data *)id->data;
	platform_set_drvdata(_dev, monitor_owl);
	ret = get_configuration_from_dts(id);
	if (ret != 0)
		goto	err_monitor_probe_fail;

	/*init &my_mon->lock */
	spin_lock_init(&my_mon->lock);
	mutex_init(&my_mon->mon_mutex);
	my_mon->thread_chk_times = 0;
	my_mon->run = 0;
	my_mon->tsk = NULL;

	/* status init. */
	my_mon->port_status.charger_connected = 0;
	my_mon->port_status.pc_connected = 0;
	my_mon->port_status.udisk_connected = 0;

	my_mon->port_config.vbus_debug = 0xff;
	/*
	  res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	  if (!res) {
	  pr_info("missing memory base resource\n");
	  return -EINVAL;
	  }

	  base = devm_ioremap(dev, res->start, resource_size(res));
	*/

	base = ioremap(monitor_owl->io_base, 0xcd00);
	if (!base) {
		MONITOR_ERR("ioremap failed\n");
		my_mon->port_config.port_type = PORT_DWC3;
		ret = -ENOMEM;
		goto	err_monitor_probe_fail;
	}
#if SUPPORT_NOT_RMMOD_USBDRV
	my_mon->dwc3_status = -1;
	my_mon->xhci_status = -1;
	my_mon->dwc3_timeout_cnt = 0;
	my_mon->xhci_timeout_cnt = 0;
	my_mon->dwc3_set_plugstate = s_dwc3_set_plugstate;
	my_mon->xhci_set_plugstate = s_xhci_set_plugstate;
#endif

	my_mon->base = base;
	MONITOR_PRINTK("my_mon->base is %08x\n", (unsigned int)my_mon->base);

	ret = umonitor_core_init(&my_mon->port_ops, &my_mon->port_config, my_mon->base, monitor_owl);
	if (ret != 0)
		goto	err_monitor_probe_fail;

	/* add for switch device. */
	my_mon->port_dev.sdev.name = "monitor_dev";
	my_mon->port_dev.sdev.print_state = umonitor_switch_state;
	ret = switch_dev_register(&my_mon->port_dev.sdev);
	if (ret < 0) {
		MONITOR_ERR("failed to register switch dev0 in umonitor.\n");
		goto err_monitor_probe_fail;
	}

	MONITOR_PRINTK(KERN_EMERG"%s--%d, initalize a wake_lock\n", __FILE__, __LINE__);
	wake_lock_init(&my_mon->monitor_wake_lock, WAKE_LOCK_SUSPEND, "usb_monitor");

	init_waitqueue_head(&my_mon->mon_wait);
	my_mon->det_plugin_req = 0;

	my_mon->tsk =
		kthread_create(mon_thread_port, NULL, "usb_monitor");

	if (IS_ERR(my_mon->tsk)) {
		ret = PTR_ERR(my_mon->tsk);
		my_mon->tsk = NULL;
		MONITOR_ERR("err: create monitor thread failed\n");
		goto out;
	}
	wake_up_process(my_mon->tsk);
	/*register timer and function "mon_timer_func"*/
	setup_timer(&my_mon->port_timer, mon_timer_func_port, 0);
	my_mon->port_timer.expires = jiffies + HZ/2;
	add_timer(&my_mon->port_timer);

	setup_timer(&my_mon->check_timer, mon_timer_func_check, 0);
	my_mon->check_timer.expires = jiffies + HZ/2;
	add_timer(&my_mon->check_timer);

	monitor_work_pending = 0;
	wake_lock_register_cnt = 0;
	INIT_DELAYED_WORK(&monitor_work, monitor_release_wakelock);
	INIT_DELAYED_WORK(&monitor_resume_work, monitor_resume_delay_work);
#ifdef CONFIG_USB_PLATFORM_LINUX
	//if (owl_get_boot_mode() != OWL_BOOT_MODE_UPGRADE) {
	fdt_node = of_find_compatible_node(NULL, NULL,  id->compatible);
	if (NULL != fdt_node) {
		umonitor_detection(1);
		my_mon->dwc3_status = PLUGSTATE_B_IN;
		umonitor_status->message_status |= 0x1 << MONITOR_B_IN;
		my_mon->port_status.pc_connected = 1;
		my_mon->port_status.charger_connected = CONNECT_USB_PORT;
		umonitor_status->detect_valid = 0;
		my_mon->run = 1;
	}
#endif
	my_mon->resume_status = -1;
	return 0;
out:
	switch_dev_unregister(&my_mon->port_dev.sdev);
err_monitor_probe_fail:
	my_mon->probe_fail = 1;
	return ret;
}

static int monitor_remove(struct platform_device *_dev)
{
	del_timer_sync(&my_mon->port_timer);
	del_timer_sync(&my_mon->check_timer);
	cancel_delayed_work_sync(&monitor_resume_work);
	/*wake_unlock(&my_mon->monitor_wake_lock);*/
	wake_lock_destroy(&my_mon->monitor_wake_lock);

	/* wait for mon_thread exit */
	if (my_mon->tsk != NULL) {
		atomic_set(&my_mon->port_exit, 1);
		kthread_stop(my_mon->tsk);
	}

	msleep(20);
	if (atomic_read(&my_mon->port_exit) != 0)
		MONITOR_ERR("error unable to stop mon_thread_port\n");

	my_mon->tsk = NULL;

	switch_dev_unregister(&my_mon->port_dev.sdev);
	MONITOR_PRINTK("monitor driver remove successufully\n");
	return 0;
}

static int monitor_suspend(struct platform_device *pdev, pm_message_t state)
{
	unsigned int mtime;

	if (pm_status == PM_HIBERNATION_PREPARE)
		return 0;

	my_mon->resume_status = 0;
	/*
	  usb charger plugin will triger PLUGSTATE_B_IN	;
	  if usb charger plugin &battery is full,will go into suspend.
	  So here need to plugout before suspend;
	*/
	if (my_mon->xhci_status == PLUGSTATE_A_IN) {
		my_mon->xhci_set_plugstate(PLUGSTATE_A_SUSPEND);
		my_mon->dwc3_set_plugstate(PLUGSTATE_A_SUSPEND);
	}
	if (my_mon->dwc3_status == PLUGSTATE_B_IN) {
		my_mon->xhci_set_plugstate(PLUGSTATE_B_SUSPEND);
		my_mon->dwc3_set_plugstate(PLUGSTATE_B_SUSPEND);
	}
	mtime = 0x70000000;
	umonitor_core_suspend();

	/* delay timer for a long time;may reset timer in resume */
	mod_timer(&my_mon->port_timer, jiffies+(mtime*HZ)/1000);
	mod_timer(&my_mon->check_timer, jiffies+(mtime*HZ)/1000);

	return 0;
}
static void monitor_resume_delay_work(struct work_struct *work)
{
	umonitor_core_resume();
	/*mod timer*/
	mod_timer(&my_mon->port_timer, jiffies+(3100*HZ)/1000);
	mod_timer(&my_mon->check_timer, jiffies+(4*CHECK_TIMER_INTERVAL*HZ)/1000);
}

bool monitor_resume_complete_need_set_noattached(void)
{
	return usb3_set_device_noattached_flag == 1 ? true : false;
}

void clear_monitor_resume_complete_need_set_noattached(void)
{
	usb3_set_device_noattached_flag = 0;
}

static int monitor_resume(struct platform_device *pdev)
{
	my_mon->resume_status = 1;
	if (pm_status == PM_HIBERNATION_PREPARE)
		return 0;

	pr_info("=====monitor resume!\n");
#ifndef CONFIG_USB_PLATFORM_LINUX
	umonitor_core_resume();
	/*mod timer*/
	mod_timer(&my_mon->port_timer, jiffies+(3100*HZ)/1000);
	mod_timer(&my_mon->check_timer, jiffies+(4*CHECK_TIMER_INTERVAL*HZ)/1000);
#endif

	if (usb3_need_set_device_noattached())
		usb3_set_device_noattached_flag = 1;

	return 0;
}

static void monitor_shutdown(struct platform_device *pdev)
{
	umonitor_core_suspend();
}

static struct platform_driver monitor_driver = {
	.probe = monitor_probe,
	.remove = monitor_remove,
	.suspend = monitor_suspend,
	.resume = monitor_resume,
	.shutdown = monitor_shutdown,
	.driver = {
		.owner = THIS_MODULE,
		.name = "usb_monitor",
		.of_match_table = owl_monitor_of_match,
	},
};

static int monitor_pm_notify(struct notifier_block *nb, unsigned long event, void *dummy)
{
	unsigned int mtime = 0x70000000;

	if (event == PM_HIBERNATION_PREPARE) {
		pm_status = PM_HIBERNATION_PREPARE;
		umonitor_core_suspend();
		/* delay timer for a long time;may reset timer in resume */
		mod_timer(&my_mon->port_timer, jiffies+(mtime*HZ)/1000);
		mod_timer(&my_mon->check_timer, jiffies+(mtime*HZ)/1000);
	} else if (event == PM_POST_HIBERNATION) {
		pm_status = 0;
		MONITOR_PRINTK(KERN_INFO "monitor resume!\n");
		umonitor_core_resume();
		/* mod timer */
		mod_timer(&my_mon->port_timer, jiffies+(500*HZ)/1000);
		mod_timer(&my_mon->check_timer, jiffies+(CHECK_TIMER_INTERVAL*HZ)/1000);
	}
#ifdef CONFIG_USB_PLATFORM_LINUX
	if (event == PM_POST_SUSPEND) {
		pr_info("\n monitor_pm_notify %d\n", __LINE__);
		if (my_mon->resume_status == 1)
			schedule_delayed_work(&monitor_resume_work, msecs_to_jiffies(500));
	}
#endif
	return NOTIFY_OK;
}

static struct notifier_block monitor_pm_notifier = {
	.notifier_call = monitor_pm_notify,
};

#if 0
static int owl_hibernate_notifier_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{

	switch (event) {
	case PM_HIBERNATION_PREPARE:
		break;
	case PM_POST_HIBERNATION:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block owl_hibernate_notifier = {
	.notifier_call = owl_hibernate_notifier_event,
};
#endif

#if (1)	/* add sysfs info. */
MON_STATUS_ATTR(charger_connected);
MON_STATUS_ATTR(pc_connected);
MON_STATUS_ATTR(udisk_connected);

MON_CONFIG_ATTR(run);
MON_CONFIG_ATTR(detect_type);
MON_CONFIG_ATTR(port_type);
/* for idpin config. */
MON_CONFIG_ATTR(idpin_type);
MON_CONFIG_ATTR(idpin_gpio_group);
MON_CONFIG_ATTR(idpin_gpio_no);
MON_CONFIG_ATTR(force_detect);
/* for vbus config. */
MON_CONFIG_ATTR(vbus_type);
MON_CONFIG_ATTR(vbus_gpio_group);
MON_CONFIG_ATTR(vbus_gpio_no);
/* in host state, if vbus power switch onoff use gpio, set it. */
MON_CONFIG_ATTR(power_switch_gpio_group);
MON_CONFIG_ATTR(power_switch_gpio_no);
MON_CONFIG_ATTR(power_switch_active_level);
MON_CONFIG_ATTR(idpin_debug);
MON_CONFIG_ATTR(vbus_debug);
#ifdef SUPPORT_NOT_RMMOD_USBDRV
MON_CONFIG_ATTR(usb_con_msg);
#endif
static struct attribute *mon_status_attrs[] = {
	MONITOR_ATTR_LIST(charger_connected),
	MONITOR_ATTR_LIST(pc_connected),
	MONITOR_ATTR_LIST(udisk_connected),
	NULL,	/* terminator */
};

static struct attribute_group mon_port_status = {
	.name = "status",
	.attrs = mon_status_attrs,
};

static struct attribute *mon_config_attrs[] = {
	MONITOR_ATTR_LIST(run),
	MONITOR_ATTR_LIST(detect_type),
	MONITOR_ATTR_LIST(port_type),
	/* for idpin config. */
	MONITOR_ATTR_LIST(idpin_type),
	MONITOR_ATTR_LIST(idpin_gpio_group),
	MONITOR_ATTR_LIST(idpin_gpio_no),
	/* for vbus config. */
	MONITOR_ATTR_LIST(vbus_type),
	MONITOR_ATTR_LIST(vbus_gpio_group),
	MONITOR_ATTR_LIST(vbus_gpio_no),
	/* in host state, if vbus power switch onoff use gpio, set it. */
	MONITOR_ATTR_LIST(power_switch_gpio_group),
	MONITOR_ATTR_LIST(power_switch_gpio_no),
	MONITOR_ATTR_LIST(power_switch_active_level),
	MONITOR_ATTR_LIST(idpin_debug),
	MONITOR_ATTR_LIST(vbus_debug),
	MONITOR_ATTR_LIST(force_detect),
#ifdef SUPPORT_NOT_RMMOD_USBDRV
	MONITOR_ATTR_LIST(usb_con_msg),
#endif
	NULL,	/* terminator */
};

static struct attribute_group mon_port_config = {
	.name = "config",
	.attrs = mon_config_attrs,
};


static ssize_t mon_status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct mon_sysfs_status *p_status;
	if (my_mon->probe_fail == 1) {
		pr_err("monitor: probe_fail =1!!\n");
		return -ENOENT;
	}
	if (kobj == my_mon->mon_port)
		p_status = &my_mon->port_status;
	else
		return -ENOENT;

	if (ATTRCMP(charger_connected))
		return sprintf(buf, "%d\n", p_status->charger_connected);
	else if (ATTRCMP(pc_connected))
		return sprintf(buf, "%d\n", p_status->pc_connected);
	else if (ATTRCMP(udisk_connected))
		return sprintf(buf, "%d\n", p_status->udisk_connected);
	else {
	}
	return -ENOENT;
}

static ssize_t mon_config_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	umon_port_config_t *pconfig;
	if (my_mon->probe_fail == 1) {
		pr_err("monitor: probe_fail =1!!\n");
		return -ENOENT;
	}
	if (kobj == my_mon->mon_port)
		pconfig = &my_mon->port_config;
	else
		return -ENOENT;

	if (ATTRCMP(run)) {
		/*return sprintf(buf, "%d\n", umonitor_get_run_status());*/
		return sprintf(buf, "%d\n", my_mon->run);
	} else if (ATTRCMP(detect_type)) {
		return sprintf(buf, "%d\n", pconfig->detect_type);
	} else if (ATTRCMP(idpin_type)) {
		return sprintf(buf, "%d\n", pconfig->idpin_type);
	} else if (ATTRCMP(port_type)) {
		*buf = pconfig->port_type;
		return sizeof(char);
	} else if (ATTRCMP(idpin_gpio_group)) {
		return sprintf(buf, "%d\n", pconfig->idpin_gpio_group);
	} else if (ATTRCMP(idpin_gpio_no)) {
		return sprintf(buf, "%d\n", pconfig->idpin_gpio_no);
	} else if (ATTRCMP(vbus_type)) {
		return sprintf(buf, "%d\n", pconfig->vbus_type);
	} else if (ATTRCMP(vbus_gpio_group)) {
		return sprintf(buf, "%d\n", pconfig->vbus_gpio_group);
	} else if (ATTRCMP(vbus_gpio_no)) {
		return sprintf(buf, "%d\n", pconfig->vbus_gpio_no);
	} else if (ATTRCMP(power_switch_gpio_group)) {
		return sprintf(buf, "%d\n", pconfig->power_switch_gpio_group);
	} else if (ATTRCMP(power_switch_gpio_no)) {
		return sprintf(buf, "%d\n", pconfig->power_switch_gpio_no);
	} else if (ATTRCMP(power_switch_active_level)) {
		return sprintf(buf, "%d\n", pconfig->power_switch_active_level);
	} else if (ATTRCMP(idpin_debug)) {
		return sprintf(buf, "%d\n", pconfig->idpin_debug);
	} else if (ATTRCMP(vbus_debug)) {
		return sprintf(buf, "%d\n", pconfig->vbus_debug);
	} else if (ATTRCMP(force_detect)) {
		return sprintf(buf, "%d\n", pconfig->force_detect);
	}
#ifdef SUPPORT_NOT_RMMOD_USBDRV
	else if (ATTRCMP(usb_con_msg)) {
		return sprintf(buf, "%s\n", pconfig->usb_con_msg);
	}
#endif
	else {
		/* do nothing. */
	}

	return -ENOENT;
}

static ssize_t mon_config_store(struct kobject *kobj, struct kobj_attribute *attr, const char *instr, size_t bytes)
{
	unsigned long val;
	unsigned int ret, plugstatus;
	umon_port_config_t *pconfig;
	if (my_mon->probe_fail == 1) {
		pr_info("monitor: probe_fail =1!!\n");
		return -ENOENT;
	}
	mutex_lock(&my_mon->mon_mutex);

	if (kobj == my_mon->mon_port) {
		pconfig = &my_mon->port_config;
	} else {
		MONITOR_PRINTK("store no err!\n");
		goto out;
	}

	ret = strict_strtoul(instr, 0, &val);
	MONITOR_PRINTK("strict_strtoul:%d\n", (unsigned int)val);

	if (ATTRCMP(run)) {
		my_mon->run = val;
		if (val == 1) {
#ifndef CONFIG_USB_PLATFORM_LINUX
			umonitor_detection(val);
#endif
		} else if (val == 2) {
			MONITOR_PRINTK("vbus on off\n");
			usb_set_vbus_power(0);
			mdelay(500);
			plugstatus = umonitor_get_message_status();
			if ((plugstatus & (0x1 << MONITOR_A_IN)) != 0)
				usb_set_vbus_power(1);

		} else if (val == 3) {
			mon_port_putt_msg(MON_MSG_USB_B_OUT);
		} else if (val == 4) {
			mon_port_putt_msg(MON_MSG_USB_B_IN);
		} else if (val == 5) {
#ifndef CONFIG_USB_PLATFORM_LINUX
#if SUPPORT_NOT_RMMOD_USBDRV
			my_mon->dwc3_set_plugstate(PLUGSTATE_B_OUT);
			my_mon->xhci_set_plugstate(PLUGSTATE_B_OUT);
#endif
#endif
		} else {
		}
	} else if (ATTRCMP(detect_type)) {
		pconfig->detect_type = val;
	} else if (ATTRCMP(idpin_type)) {
		pconfig->idpin_type = val;
	} else if (ATTRCMP(idpin_gpio_group)) {
		pconfig->idpin_gpio_group = val;
	} else if (ATTRCMP(idpin_gpio_no)) {
		pconfig->idpin_gpio_no = val;
	} else if (ATTRCMP(vbus_type)) {
		pconfig->vbus_type = val;
	} else if (ATTRCMP(vbus_gpio_group)) {
		pconfig->vbus_gpio_group = val;
	} else if (ATTRCMP(vbus_gpio_no)) {
		pconfig->vbus_gpio_no = val;
	} else if (ATTRCMP(power_switch_gpio_group)) {
		pconfig->power_switch_gpio_group = val;
	} else if (ATTRCMP(power_switch_gpio_no)) {
		pconfig->power_switch_gpio_no = val;
		} else if (ATTRCMP(force_detect)) {
		pconfig->force_detect = val;
	} else if (ATTRCMP(power_switch_active_level)) {
		/* backdoor for debug. */
		if (val == 2)
			umonitor_printf_debuginfo();
		else
			pconfig->power_switch_active_level = val;
	} else if (ATTRCMP(idpin_debug)) {
		pconfig->idpin_debug = val;
		if ((val == 0) || (val == 1))
			pr_info("\n debug idpin set =%d !!\n", pconfig->idpin_debug);
		else
			pr_info("\n debug idpin clear!!\n");
	} else if (ATTRCMP(vbus_debug)) {
		pconfig->vbus_debug = val;
		if ((val == 0) || (val == 1))
			pr_info("\n debug vbus set =%d!!\n", pconfig->vbus_debug);
		else
			pr_info("\n debug vbus clear!!\n");
	}
#ifndef CONFIG_USB_PLATFORM_LINUX
#ifdef SUPPORT_NOT_RMMOD_USBDRV
	else if (ATTRCMP(usb_con_msg)) {
		strcpy(pconfig->usb_con_msg, instr);
		monitor_handle_plug_in_out_msg((char *)instr);
		pr_info("\n write usb_con_msg %s\n", instr);

		/*
		 * In some cases, such as USB-to-SATA chip is integrated in the
		 * board, it may need to reset VBUS to make it work properly.
		 * NOTICE: 400ms is needed.
		 * FIXME: This is not a good solution really.
		 */
		if (usb_need_fix_vbus_reset() && !strncmp((char *)instr, "USB_A_IN", 8)) {
			umonitor_vbus_power_onoff(0);
			/* FIXME: the value seems fine, need to optimize */
			msleep(400);
			umonitor_vbus_power_onoff(1);
		}
	}
#endif
#endif
	else {
		/* do nothing. */
		MONITOR_PRINTK("store attr err!\n");
	}

out:
	mutex_unlock(&my_mon->mon_mutex);
	return bytes;
}

int mon_sysfs_init(void)
{
	int ret = 0;

	my_mon->mon_obj = kobject_create_and_add("monitor", NULL);
	if (!my_mon->mon_obj)
		MONITOR_ERR("unable to create monitor kobject\n");

	my_mon->mon_port = kobject_create_and_add("usb_port", my_mon->mon_obj);
	if (!my_mon->mon_port)
		MONITOR_ERR("unable to create usb_port kobject\n");

	ret = sysfs_create_group(my_mon->mon_port, &mon_port_status);
	if (ret != 0)
		MONITOR_ERR("create usb_port status group failed\n");

	ret = sysfs_create_group(my_mon->mon_port, &mon_port_config);
	if (ret != 0)
		MONITOR_ERR("create usb_port cofig group failed\n");

	/* ignore failed case. */
	return 0;
}

int mon_sysfs_exit(void)
{
	sysfs_remove_group(my_mon->mon_port, &mon_port_status);
	sysfs_remove_group(my_mon->mon_port, &mon_port_config);
	kobject_del(my_mon->mon_port);
	kobject_del(my_mon->mon_obj);
	return 0;
}
#endif	/* add sysfs info. */

static int usb_detect_plugout_event(void)
{
	usb_hal_monitor_t *p_hal;
	unsigned int val;
	unsigned int message;

	p_hal = &umonitor_status->umonitor_hal;
	message = umonitor_status->message_status;

	if (umonitor_status->detect_valid == 0) {
		if (umonitor_status->det_phase == 0) {
			if (((message & (0x1 << MONITOR_B_IN)) != 0) ||
				((message & (0x1 << MONITOR_CHARGER_IN)) != 0)) {
				if ((my_mon->port_status.charger_connected == CONNECT_USB_ADAPTOR) &&
					(wake_lock_register_cnt > 0)) {
					pr_info("wakelock release!!!!!!!!\n");
					wake_lock_register_cnt--;
					wake_unlock(&my_mon->monitor_wake_lock);
				}
				umonitor_status->vbus_status = (unsigned char) p_hal->get_vbus_state(p_hal);
				if (umonitor_status->vbus_status == USB_VBUS_LOW) {
					my_mon->port_status.charger_connected = 0;
					umonitor_detection(1);
					if (umon_set_usb_plugin_type)
						umon_set_usb_plugin_type(USB_NO_PLUGGED);
					pr_info("\n========usb_detect_plugout_event===start det!!========\n");
					/*wake_up(&my_mon->mon_wait);*/
				}
			}
		} else {
			/*host set monitor flag*/
			if ((message & (0x1 << MONITOR_A_IN)) != 0) {
				val = p_hal->get_idpin_state(p_hal);
				if (val != USB_ID_STATE_HOST) {
					umonitor_detection(1);
					/*wake_up(&my_mon->mon_wait);*/
				}
			}
		}

		mod_timer(&my_mon->check_timer, jiffies + (CHECK_TIMER_INTERVAL*HZ)/1000);
	} else {
		mod_timer(&my_mon->check_timer, jiffies + 3 + (2 * CHECK_TIMER_INTERVAL * HZ)/1000);
	}

	return 0;
}

int notify_driver_state(int driver_state, int driver_type)
{
	return 0;
}
EXPORT_SYMBOL(notify_driver_state);

static void mon_port_wakeup_func(void)
{
	my_mon->det_plugin_req = 1;
	wake_up(&my_mon->mon_wait);
	return;
}

static void mon_port_putt_msg(unsigned int msg)
{
	struct mon_sysfs_status *pstatus;
	usb_hal_monitor_t *p_hal;

	pstatus = &my_mon->port_status;
	p_hal = &umonitor_status->umonitor_hal;

	switch (msg) {
	case MON_MSG_USB_B_IN:
		/*pr_info("%s--%d, wake_lock !!!\n", __FILE__, __LINE__);*/
		if (monitor_work_pending == 1) {
			cancel_delayed_work_sync(&monitor_work);
			monitor_work_pending = 0;
		}
		if (!wake_lock_register_cnt) {
			wake_lock(&my_mon->monitor_wake_lock);
			wake_lock_register_cnt++;
		}
		pstatus->pc_connected = 1;
		pstatus->charger_connected = CONNECT_USB_PORT; /*set usb pc first,it'll jude by dwc3 interrupt */
		sprintf(my_mon->port_dev.state_msg, "USB_B_IN");
		p_hal->set_soft_vbus(p_hal, 1, 1);

		if (boot_charger_mode)
			monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);

#ifdef CONFIG_USB_PLATFORM_LINUX	/*handle usb msg in monitor*/
		monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);
#else	/*handle usb msg through vold*/
		switch_set_state(&my_mon->port_dev.sdev, msg);
#endif
		break;
	case MON_MSG_USB_B_OUT:
		/*pr_info("%s--%d, wake_unlock !!!\n", __FILE__, __LINE__);*/
		if (wake_lock_register_cnt) {
			schedule_delayed_work(&monitor_work, msecs_to_jiffies(1000));
			monitor_work_pending = 1;
		}
		pstatus->pc_connected = 0;
		pstatus->charger_connected = 0;
		sprintf(my_mon->port_dev.state_msg, "USB_B_OUT");
		p_hal->set_soft_vbus(p_hal, 0, 0);

		if (boot_charger_mode)
			monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);

#ifdef CONFIG_USB_PLATFORM_LINUX
		monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);
#else
		switch_set_state(&my_mon->port_dev.sdev, msg);
#endif
		break;
	case MON_MSG_USB_A_IN:
		pstatus->udisk_connected = 1;
		pr_info("%s--%d, wake_lock !!!\n", __FILE__, __LINE__);
		/*wake_lock(&my_mon->monitor_wake_lock);*/
		sprintf(my_mon->port_dev.state_msg, "USB_A_IN");

		if (boot_charger_mode)
			monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);

#ifdef CONFIG_USB_PLATFORM_LINUX
		monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);
#else
		switch_set_state(&my_mon->port_dev.sdev, msg);
#endif
		wake_lock_timeout(&my_mon->monitor_wake_lock, 10*HZ);
		pr_info("----monitor_wake_lock for 10s\n");
		break;
	case MON_MSG_USB_A_OUT:
		pstatus->udisk_connected = 0;
		pr_info("%s--%d, wake_unlock !!!\n", __FILE__, __LINE__);
		/*wake_unlock(&my_mon->monitor_wake_lock);*/
		sprintf(my_mon->port_dev.state_msg, "USB_A_OUT");

		if (boot_charger_mode)
			monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);

#ifdef CONFIG_USB_PLATFORM_LINUX
		monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);
#else
		switch_set_state(&my_mon->port_dev.sdev, msg);
#endif
		wake_lock_timeout(&my_mon->monitor_wake_lock, 10*HZ);
		break;
	case MON_MSG_USB_CHARGER_IN:
		pstatus->charger_connected = 1;
		/*pr_info("%s--%d, wake_lock !!!\n", __FILE__, __LINE__);*/
		wake_lock_timeout(&my_mon->monitor_wake_lock, 10*HZ);
		sprintf(my_mon->port_dev.state_msg, "USB_CHARGER_IN");

		if (boot_charger_mode)
			monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);

#ifdef CONFIG_USB_PLATFORM_LINUX
		monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);
#else
		switch_set_state(&my_mon->port_dev.sdev, msg);
#endif
		break;
	case MON_MSG_USB_CHARGER_OUT:
		pstatus->charger_connected = 0;
		pr_info("%s--%d, wake_unlock !!!\n", __FILE__, __LINE__);
		wake_unlock(&my_mon->monitor_wake_lock);
		sprintf(my_mon->port_dev.state_msg, "USB_CHARGER_OUT");
#ifdef CONFIG_USB_PLATFORM_LINUX
		monitor_handle_plug_in_out_msg(my_mon->port_dev.state_msg);
#else
		switch_set_state(&my_mon->port_dev.sdev, msg);
#endif
		break;
	default:
		MONITOR_ERR("err msg:%0x\n", msg);
		break;
	}
	return;
}

int usb_set_vbus_power(int value)
{
	return umonitor_vbus_power_onoff(value);
}
EXPORT_SYMBOL_GPL(usb_set_vbus_power);

void monitor_set_usb_plugin_type(int value)
{
	my_mon->port_status.charger_connected = value;
	if ((wake_lock_active(&my_mon->monitor_wake_lock) == false) &&
			(value == CONNECT_USB_PORT) && (my_mon->run == 1)) {
		pr_info("\n usb reset interrupt,get wakelock!!\n");
		wake_lock_register_cnt = 1;
		wake_lock(&my_mon->monitor_wake_lock);
	}
}
EXPORT_SYMBOL_GPL(monitor_set_usb_plugin_type);
static int __init monitor_init(void)
{
	int ret;

	atomic_set(&my_mon->port_exit, 0);

	my_mon->port_ops.wakeup_func = mon_port_wakeup_func;
	my_mon->port_ops.putt_msg = mon_port_putt_msg;
	my_mon->probe_fail = 0;
#ifndef CONFIG_OF
	ret = platform_device_register(&monitor_device);
	if (ret < 0)
		MONITOR_ERR("Can't register monitor platform device, ret:%d\n", ret);
#endif

	ret = platform_driver_register(&monitor_driver);
	if (ret < 0)
		MONITOR_ERR("monitor driver register failed,err is %d\n", ret);

	/*create director "/sys/monitor", "/sys/usb_port" and attribute file: */
	ret = mon_sysfs_init();
	if (ret < 0)
		MONITOR_ERR("mon_sysfs_init failed,err is %d\n", ret);

	set_usb_plugin_type = (FUNC)kallsyms_lookup_name("atc260x_set_usb_plugin_type");
	register_pm_notifier(&monitor_pm_notifier);
	/*register_hibernate_notifier(&owl_hibernate_notifier);*/

	return ret;
}

static void monitor_exit(void)
{
	/*unregister_hibernate_notifier(&owl_hibernate_notifier);*/
	unregister_pm_notifier(&monitor_pm_notifier);
	platform_driver_unregister(&monitor_driver);
#ifndef CONFIG_OF
	platform_device_unregister(&monitor_device);
#endif

	umonitor_core_exit();

	mon_sysfs_exit();

	gpio_free(my_mon->port_config.power_switch_gpio_no);

	MONITOR_PRINTK("end of monitor_exit\n");
	return;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dengtaiping");

late_initcall(monitor_init);
module_exit(monitor_exit);

