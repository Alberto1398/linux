/*
 * Actions ATC260X PMICs CHARGER driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Terry Chen <chenbo@actions-semi.com>
 *
 * Power supply driver for Actions atc260x_charger
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
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/fb.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wakelock.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/atc260x/atc260x.h>
#include <linux/bootafinfo.h>
#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <video/owl_dss.h>
#include "atc260x_charger.h"

#define ATC260X_CHARGER_DRV_TIMESTAMP		"20151126133607"
#define ATC260X_CHARGER_DRV_VERSION		"r2p7"

#define GPIO_NAME_WALL_SWITCH			"wall_switch"
#define GPIO_NAME_EXT_CTRL_CHARGER		"ext_charger_ctl"
#define PSY_NAME_WALL				"atc260x-wall"
#define PSY_NAME_USB				"atc260x-usb"

#define ATC260X_WALL_VOLTAGE_THRESHOLD		(3400)
#define ATC260X_VBUS_VOLTAGE_THRESHOLD		(3900)
#define CHARGER_ON_DELAY_SEC			(2)
/*extern buck adjust coefficient*/
#define CONFIG_CURVE_ARG0			416000
#define CONFIG_CURVE_ARG1			3727

#define BATTERY_LOW_SOC				(7)
#define BATTERY_LOW_VOL				(3500)

struct battery_info {
	int online;
	int bat_vol;
	int bat_cur;
	int battery_soc;
	int health;
	int status;
	int changed;
	int ov_protect;
};

/**
 * atc260x_charger : info of atc260x charger
 * @ic_type : atc260x pmu ic type.
 * @ic_ver : ic version for atc2603a.
 * @wall : dc5v adapter power sypply.
 * @usb : usb(pc/adapter) power supply.
 * @atc260x : atc260x parent device.
 * @wq : work queue for atc260x charger only.
 * @work : poll work for atc260x charger.
 * @nb_bl: notifier block for backlight, if backlight changed, notify.
 * @interval : poll work interval.
 * @sum_interval : record poll time.
 * @lock : mutex lock.
 * @charger_wake_lock : apply for wake lock when charging.
 * @delay_lock : apply for wake timeout lock if adapter plugged out.
 * @charger_online : adapter plug in or not.
 * @charge_on : charger is on or off.
 * @vbus_mv : record vbus voltage, unit:mv.
 * @wall_mv : record wall voltage, unit:mv.
 * @vbus_is_otg : whether if vbus used as otg device or not.
 * @charger_cur_status : record which adapter is online currently.
 * @charger_pre_status : record which adapter is online previously.
 * @usb_plugged_type : record usb plugged type.
 * @usb_plugged_type_changed : record whether if usb plugged type change or not.
 * @ext_charger_exist : whether if extern charger exists or not.
 * @gpio_wall_switch_pin : the gpio pin which turn on/turn off wall path.
 * @ wall_switch_exist : whether if the wall switch exists or not.
 * @ops : save the opertation ptr of charger form xxx_charger_phy.c
 * @debug_file :manage debug file node.
 *
 */

struct atc260x_charger {
	int ic_type;
	int ic_ver;
	struct charger_dts_items *items;
	struct device *dev;
	struct power_supply wall;
	struct power_supply usb;
	struct atc260x_dev *atc260x;
	struct workqueue_struct *wq;
	struct delayed_work work;
	unsigned int interval;
	struct mutex lock;
	struct wake_lock charger_wake_lock;
	struct wake_lock delay_lock;

	bool charger_online;
	bool battery_online;
	bool bl_onoff;
	bool charge_on;
	int irq;
	int vbus_mv;
	int wall_mv;
	int vbus_is_otg;
	int vbusv_channel;
	int wallv_channel;
	int temp_channel;

	int charger_cur_status;
	int charger_pre_status;
	enum USB_PLUGGED_TYPE usb_plugged_type;
	bool usb_plugged_type_changed;

	bool ext_charger_exist;
	int gpio_wall_switch_pin;
	bool wall_switch_exist;

	struct battery_info bat_info;
	struct atc260x_charger_ops *ops;
	struct dentry *debug_file;
	struct notifier_block notifier;
	struct led_trigger *power_on_trig;
	char *power_on_trig_name;
};

static enum power_supply_property atc260x_wall_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property atc260x_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static struct atc260x_charger *global_charger_dev;
static struct atc260x_charger *get_atc260x_charger(void)
{
	return global_charger_dev ? global_charger_dev : NULL;
}

struct charger_dts_items items;
/*How long befor enable charger, because gas gauge need  0A correction*/
static int charger_delay_counter = -1;
atomic_t adapter_type = ATOMIC_INIT(0);
atomic_t set_cc_finished = ATOMIC_INIT(0);
/*if other drv set charger current forcely,  set this flag*/
atomic_t force_set_cc = ATOMIC_INIT(0);
/*if other drv turn off charger forcely,  set this flag*/
atomic_t irq_set_cc = ATOMIC_INIT(0);
static bool force_charger_off;
static bool old_charger_onoff;
/*communicate with usb monitor, prevent confliction with usb monitor*/
enum USB_PLUGGED_TYPE usb_plugged_type = USB_NO_PLUGGED;
bool usb_plugged_type_changed = false;
static void atc260x_vbus_set_onoff(struct atc260x_charger *charger);
static int __atc260x_charger_check_online(struct atc260x_charger *charger);

static void update_led_state(struct atc260x_charger *charger)
{
	if (charger->bl_onoff || charger->charger_online)
		led_trigger_event(charger->power_on_trig, LED_FULL);
	else
		led_trigger_event(charger->power_on_trig, LED_OFF);

}

#ifdef CONFIG_DEBUG_FS
static int charger_debug_show(struct seq_file *s, void *data)
{
	struct atc260x_charger *charger = s->private;

	seq_printf(s, "charger is %s\n", charger->charge_on ? "on" : "off");
	seq_printf(s, "wall voltage = %d (mV)\n", charger->wall_mv);
	seq_printf(s, "vbus voltage = %d (mV)\n", charger->vbus_mv);

	return 0;
}

static int debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, charger_debug_show, inode->i_private);
}

static const struct file_operations bat_debug_fops = {
	.open = debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *atc260x_charger_create_debugfs(struct atc260x_charger
						     *charger)
{
	charger->debug_file = debugfs_create_file("charger", 0660, 0, charger,
						  &bat_debug_fops);
	return charger->debug_file;
}

static void atc260x_charger_remove_debugfs(struct atc260x_charger *charger)
{
	debugfs_remove(charger->debug_file);
}
#else
static inline struct dentry *atc260x_charger_create_debugfs(struct
							    atc260x_charger
							    *charger)
{
	return NULL;
}

static inline void
atc260x_charger_remove_debugfs(struct atc260x_charger *charger)
{
}
#endif

static int atc260x_wall_get_prop(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct atc260x_charger *charger = dev_get_drvdata(psy->dev->parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (charger->charger_cur_status & WALL_PLUGGED)
			val->intval = 1;
		else
			val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = charger->wall_mv;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * atc260x_enable_vbusotg - enable vbus otg function or not.
 * @on : if true, disable the path between vbus and syspwr,
 *       otherwise enable the path.
 *
 * note: when enable the otg function,
 * must shutdown the diode between vbusotg and syspwr,
 * to avoiding loop circuit.
 */
int atc260x_enable_vbusotg(int on)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	WARN_ON(charger == NULL);
	power_dbg("\n[power] %s, %d, on: %d, vbus_is_otg: %d",
		  __func__, __LINE__, on, charger->vbus_is_otg);

	if (charger == NULL)
		return -ENODEV;

	mutex_lock(&charger->lock);
	charger->vbus_is_otg = on;
	/* if vbus is connected with otg device,
	* turn off vbus path immediately */
	if (charger->vbus_is_otg) {
		ops->set_vbus_onoff(atc260x, false);
		ops->set_vbus_pd(atc260x, false);
		if (charger->wall_switch_exist)
			__gpio_set_value(charger->gpio_wall_switch_pin, 0);
	} else {
		ops->set_vbus_onoff(atc260x, true);
		ops->set_vbus_pd(atc260x, true);
	}
	mutex_unlock(&charger->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(atc260x_enable_vbusotg);

/**
 * atc260x_vbus_set_onoff - enable/disable vbus id if needed
 *
 * disable vbus path in order to prevent rob large current from vbus,
 * when dc5v and usb plugged in.
 */
static void atc260x_vbus_set_onoff(struct atc260x_charger *charger)
{
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;
	int vbus_is_otg;

	mutex_lock(&charger->lock);
	vbus_is_otg = charger->vbus_is_otg;
	mutex_unlock(&charger->lock);

	if (charger->items->support_adaptor_type == SUPPORT_USB_ONLY) {
		if (!charger->wall_switch_exist) {
			ops->set_vbus_onoff(atc260x, false);
			ops->set_vbus_pd(atc260x, false);
			return;
		}

		if (charger->charger_cur_status & USB_PLUGGED) {
			if (charger->usb_plugged_type == USB_PLUGGED_PC) {
				ops->set_vbus_onoff(atc260x, true);
				ops->set_vbus_pd(atc260x, true);
			}
			else if (charger->usb_plugged_type == USB_PLUGGED_ADP) {
				ops->set_vbus_onoff(atc260x, false);
				ops->set_vbus_pd(atc260x, false);
			}
			else {
				pr_err("%s usb type err!\n", __func__);
				return;
			}
		} else {
			if (vbus_is_otg == 0) {
				ops->set_vbus_onoff(atc260x, true);
				ops->set_vbus_pd(atc260x, true);
			}
		}
		return;
	}

	if (charger->items->support_adaptor_type == SUPPORT_DCIN_ONLY) {
		ops->set_vbus_onoff(atc260x, false);
		ops->set_vbus_pd(atc260x, false);
		return;
	}

	if ((charger->charger_cur_status & WALL_PLUGGED) &&
	    (charger->charger_cur_status & USB_PLUGGED)) {
		if (charger->bat_info.online &&
			charger->bat_info.battery_soc > BATTERY_LOW_SOC &&
			charger->bat_info.bat_vol > BATTERY_LOW_VOL) {
			ops->set_vbus_onoff(atc260x, false);
			ops->set_vbus_pd(atc260x, false);
			return;
		}
	}

	if (vbus_is_otg == 0) {
		ops->set_vbus_onoff(atc260x, true);
		ops->set_vbus_pd(atc260x, true);
	}
}

/**
 * atc260x_wall_set_onoff - enable/disable wall id if needed
 * Sleeping 2ms to ensure wall switch(MOS) is on when gpio is high level.
 */
static void atc260x_wall_set_onoff(struct atc260x_charger *charger)
{
	if (!charger->wall_switch_exist)
		return;

	if (charger->items->support_adaptor_type == SUPPORT_USB_ONLY) {
		if (charger->charger_cur_status & USB_PLUGGED) {
			if (charger->usb_plugged_type == USB_PLUGGED_ADP) {
				__gpio_set_value(charger->gpio_wall_switch_pin, 1);
				usleep_range(2000, 2200);
			} else if (charger->usb_plugged_type == USB_PLUGGED_PC)
				__gpio_set_value(charger->gpio_wall_switch_pin, 0);
			else {
				pr_info("%s usb type err!\n", __func__);
				return;
			}
		}
	}
}

static int atc260x_usb_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct atc260x_charger *charger = dev_get_drvdata(psy->dev->parent);
	int ret = 0;
	int vbus_is_otg;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (charger->charger_cur_status & USB_PLUGGED)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		mutex_lock(&charger->lock);
		vbus_is_otg = charger->vbus_is_otg;
		mutex_unlock(&charger->lock);
		if (vbus_is_otg)
			val->intval = 0;
		else
			val->intval = charger->vbus_mv;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void system_stayawake(struct atc260x_charger *charger)
{
	if (!wake_lock_active(&charger->charger_wake_lock)) {
		wake_lock(&charger->charger_wake_lock);
		pr_info("%s charger wake_lock\n", __func__);
	}
}

static void system_maysleep(struct atc260x_charger *charger)
{

	if (wake_lock_active(&charger->charger_wake_lock)) {
		wake_unlock(&charger->charger_wake_lock);
		pr_info("%s charger wake_unlock\n", __func__);
	}
}

/**
 * request_system_state - request to run continuously or sleep
 *
 * stay awake if bat is not full and adapter is online, also bat is online;
 * may sleep including: 1.bat is full although adapter is online;
 *                      2.adapter is offline;
 *                      3.battery is offline.
 */
static int request_system_state(struct atc260x_charger *charger)
{
	bool battery_full;
	int charger_status_changed;

	charger_status_changed =
	    charger->charger_cur_status != charger->charger_pre_status;

	/*awake 5s when adapter plug in/out in order to light on screen */
	if (charger_status_changed)
		wake_lock_timeout(&charger->delay_lock, 5 * HZ);

	/*not any adapter is connected, we should wake unlock to sleep */
	if (!charger->charger_online) {
		system_maysleep(charger);
		return 0;
	}

	if (charger->bat_info.battery_soc == 100)
		battery_full = true;
	else
		battery_full = false;

	/*no battery , we may wake unlock to sleep for CE certification */
	if (!charger->bat_info.online) {
		system_maysleep(charger);
		return 0;
	}

	/*battery is full, we should wake unlock
	* to sleep for CE certification */
	if (battery_full)
		system_maysleep(charger);
	else
		system_stayawake(charger);

	return 0;
}

static void update_battery_state(struct atc260x_charger *charger)
{
	struct power_supply *psy_battery;
	union power_supply_propval bat_propval;

	psy_battery = power_supply_get_by_name("battery");
	if (psy_battery) {
		psy_battery->get_property(psy_battery,
					  POWER_SUPPLY_PROP_ONLINE,
					  &bat_propval);
		charger->bat_info.online = bat_propval.intval;
		psy_battery->get_property(psy_battery,
					  POWER_SUPPLY_PROP_CURRENT_NOW,
					  &bat_propval);
		charger->bat_info.bat_cur = bat_propval.intval;
		psy_battery->get_property(psy_battery,
					  POWER_SUPPLY_PROP_VOLTAGE_NOW,
					  &bat_propval);
		charger->bat_info.bat_vol = bat_propval.intval;
		psy_battery->get_property(psy_battery,
					  POWER_SUPPLY_PROP_CAPACITY,
					  &bat_propval);
		charger->bat_info.battery_soc = bat_propval.intval;
		psy_battery->get_property(psy_battery,
					  POWER_SUPPLY_PROP_HEALTH,
					  &bat_propval);
		charger->bat_info.health = bat_propval.intval;
		psy_battery->get_property(psy_battery,
					  POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
					  &bat_propval);
		charger->bat_info.ov_protect = bat_propval.intval;
		psy_battery->get_property(psy_battery,
					  POWER_SUPPLY_PROP_STATUS,
					  &bat_propval);
		if (charger->bat_info.status != bat_propval.intval) {
			charger->bat_info.status = bat_propval.intval;
			charger->bat_info.changed = 1;
		} else
			charger->bat_info.changed = 0;
		if (charger->bat_info.changed)
			power_supply_changed(psy_battery);

		pr_debug("%s online(%d)\n", __func__, charger->bat_info.online);
		pr_debug("%s bat_vol(%d)\n",
			 __func__, charger->bat_info.bat_vol);
		pr_debug("%s bat_cur(%d)\n",
			 __func__, charger->bat_info.bat_cur);
		pr_debug("%s battery_soc(%d)\n",
			 __func__, charger->bat_info.battery_soc);
		pr_debug("%s health(%d)\n", __func__, charger->bat_info.health);
		pr_debug("%s ov_protect(%d)\n",
			 __func__, charger->bat_info.ov_protect);
		pr_debug("%s status(%d)\n",
			 __func__, charger->bat_info.status);
	}
}

static void atc260x_charger_check_online(struct atc260x_charger *charger)
{
	charger->charger_cur_status = __atc260x_charger_check_online(charger);

	if (charger->items->support_adaptor_type == SUPPORT_DCIN_ONLY) {
		if (charger->charger_cur_status & WALL_PLUGGED)
			charger->charger_online = true;
		else
			charger->charger_online = false;
	} else {
		charger->charger_online =
		    charger->charger_cur_status != NO_PLUGGED;
	}
}

static void _atc260x_charger_turn_off(struct atc260x_charger *charger)
{
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	/*turn off internal charger, no matter charged or not */
	ops->set_onoff(atc260x, false);
}

/**
 * turn_off_charger_if_needed - turn off charger under some conditions
 *
 * turn off charger under following conditions:
 * 1.battery is offline;
 * 2.all adapters are offline;
 * 3.bat capacity is full.
 */
static void atc260x_charger_turn_off(struct atc260x_charger *charger)
{
	mutex_lock(&charger->lock);
	if (force_charger_off) {
		mutex_unlock(&charger->lock);
		return;
	}
	mutex_unlock(&charger->lock);

	if (!charger->charge_on)
		return;

	if (!charger->charger_online ||
	    !charger->bat_info.online ||
	    charger->bat_info.battery_soc == 100) {
		_atc260x_charger_turn_off(charger);

		charger->charge_on = false;
		pr_info("%s:charger turn off\n", __func__);
	}
}

static void _atc260x_charger_turn_on(struct atc260x_charger *charger)
{
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	ops->set_onoff(atc260x, true);
}

static void atc260x_charger_turn_on(struct atc260x_charger *charger)
{
	/*don't turn on charger when upgrade */
	mutex_lock(&charger->lock);
	if (force_charger_off) {
		mutex_unlock(&charger->lock);
		pr_debug("%s force charger off\n", __func__);
		return;
	}
	mutex_unlock(&charger->lock);

	if (charger->charge_on)
		return;

	if (!charger->charger_online ||
		!charger->bat_info.online ||
		charger->bat_info.battery_soc == 100 ||
		charger->bat_info.health == POWER_SUPPLY_HEALTH_OVERVOLTAGE) {
		pr_debug("%s charger_online(%d),bat_online(%d),soc(%d),health(%d)\n",
			__func__,
			charger->charger_online,
			charger->bat_info.online,
			charger->bat_info.battery_soc,
			charger->bat_info.health);
		charger_delay_counter = -1;
		return;
	}

	charger_delay_counter++;

	if (charger_delay_counter != CHARGER_ON_DELAY_SEC / charger->interval)
		return;

	charger_delay_counter = -1;
	_atc260x_charger_turn_on(charger);
	charger->charge_on = true;
	pr_info("%s:charger turn on\n", __func__);
}

/**
 * atc260x_set_future_current - set future constant current
 *
 * we need write the constant current  into reg.
 */
static int atc260x_charger_match_cc(struct atc260x_charger *charger, int mode)
{
	int cc;

	switch (mode) {
	case WALL_PLUGGED:
		if (owl_panel_status_get())
			cc = charger->items->bl_on_current_wall_adp;
		else
			cc = charger->items->bl_off_current_wall_adp;
		break;

	case USB_PLUGGED:
		if (charger->usb_plugged_type == USB_PLUGGED_PC) {
			if (owl_panel_status_get())
				cc = charger->items->bl_on_current_usb_pc;
			else
				cc = charger->items->bl_off_current_usb_pc;
		} else if (charger->usb_plugged_type == USB_PLUGGED_ADP) {
			if (owl_panel_status_get()) {
				if ((charger->bat_info.bat_vol > 3800000) && \
					(charger->items->bl_on_current_usb_adp_high > \
					charger->items->bl_on_current_usb_adp))
					cc = charger->items->bl_on_current_usb_adp_high;
				else
					cc = charger->items->bl_on_current_usb_adp;
			}
			else
				cc = charger->items->bl_off_current_usb_adp;
		} else {
			if (owl_panel_status_get())
				cc = charger->items->bl_on_current_usb_pc;
			else
				cc = charger->items->bl_off_current_usb_pc;
		}
		break;

	default:
		cc = charger->items->bl_on_current_usb_pc;
		break;

	}

	return cc;

}

static int atc260x_charger_calc_target_cc(struct atc260x_charger *charger)
{
	int dest_cc = 0;

	if (charger->items->support_adaptor_type == SUPPORT_DCIN_ONLY) {
		if (charger->charger_cur_status & WALL_PLUGGED)
			dest_cc = atc260x_charger_match_cc(charger,
							   WALL_PLUGGED);
	} else if (charger->items->support_adaptor_type == SUPPORT_DCIN_USB) {
		if (charger->charger_cur_status & WALL_PLUGGED)
			dest_cc = atc260x_charger_match_cc(charger,
							   WALL_PLUGGED);
		else if (charger->charger_cur_status & USB_PLUGGED)
			dest_cc = atc260x_charger_match_cc(charger,
							   USB_PLUGGED);
		else
			dest_cc = 0;
	} else if (charger->items->support_adaptor_type == SUPPORT_USB_ONLY) {
		dest_cc = atc260x_charger_match_cc(charger, USB_PLUGGED);
	} else {
		pr_err("%s: dont support adapter type?\n", __func__);
		return -1;
	}

	if ((charger->bat_info.ov_protect < 4275) &&
		(charger->bat_info.bat_vol > 4200000) &&
		(charger->bat_info.bat_cur < 700000) &&
		(charger->items->bat_type != BAT_TYPE_4300MV) &&
		(charger->items->bat_type != BAT_TYPE_4350MV)) {
		if (dest_cc != 400) {
			pr_info("%s update cc to 400mv lest battery not full\n",
				__func__);
			dest_cc = 400;
		}
	}

	if (charger->ops->cc_filter)
		dest_cc = charger->ops->cc_filter(charger->atc260x, dest_cc);

	return dest_cc;
}

static void atc260x_charger_update_current(struct atc260x_charger *charger)
{
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;
	int current_cc;
	int dest_cc;

	if (atomic_read(&force_set_cc))
		return;

	if ((charger->charger_cur_status == NO_PLUGGED) ||
	    !charger->charge_on || force_charger_off) {
		mutex_lock(&charger->lock);
		ops->set_cc(atc260x, 0);
		mutex_unlock(&charger->lock);
		return;
	}

	current_cc = ops->get_cc(atc260x);
	dest_cc = atc260x_charger_calc_target_cc(charger);
	if (dest_cc < 0)
		return;
	if (charger->items->cur_rise_enable) {
		ops->set_cc(atc260x, dest_cc);
		return;
	}

	if (current_cc == dest_cc) {
		atomic_set(&set_cc_finished, 1);
		return;
	} else
		atomic_set(&set_cc_finished, 0);

	mutex_lock(&charger->lock);
	if (current_cc < dest_cc)
		ops->set_cc(atc260x, current_cc + 200);
	else if (current_cc > dest_cc)
		ops->set_cc(atc260x, dest_cc);
	mutex_unlock(&charger->lock);
	pr_info("%s current_cc(%dmA), dest_cc(%dmA)\n",
		__func__, ops->get_cc(atc260x), dest_cc);

	return;
}

static void atc260x_charger_report_change(struct atc260x_charger *charger)
{
	/*report wall and usb plugging status */
	if ((charger->charger_pre_status & WALL_PLUGGED) !=
		(charger->charger_cur_status & WALL_PLUGGED))
		power_supply_changed(&charger->wall);
	else if ((charger->charger_pre_status & USB_PLUGGED) !=
		(charger->charger_cur_status & USB_PLUGGED))
		power_supply_changed(&charger->usb);
	charger->charger_pre_status = charger->charger_cur_status;
	/*report usb type */
	if (charger->usb_plugged_type_changed) {
		if (charger->usb_plugged_type == USB_PLUGGED_PC)
			charger->usb.type = POWER_SUPPLY_TYPE_USB;
		else if (charger->usb_plugged_type == USB_PLUGGED_ADP)
			if (charger->items->usb_adapter_as_ac)
				charger->usb.type = POWER_SUPPLY_TYPE_USB_ACA;
			else
				charger->usb.type = POWER_SUPPLY_TYPE_USB;
		else
			charger->usb.type = POWER_SUPPLY_TYPE_UNKNOWN;

		if (!(charger->charger_cur_status & WALL_PLUGGED))
			power_supply_changed(&charger->usb);
	}
	power_supply_changed(&charger->usb);
	charger->usb_plugged_type_changed = false;
}

void atc260x_charger_set_cc_force(int cc)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	atomic_set(&force_set_cc, 1);

	ops->set_cc(atc260x, cc);
}
EXPORT_SYMBOL_GPL(atc260x_charger_set_cc_force);

void atc260x_charger_set_cc_restore(void)
{
	atomic_set(&force_set_cc, 0);
}
EXPORT_SYMBOL_GPL(atc260x_charger_set_cc_restore);

int atc260x_charger_get_cc(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	return ops->get_cc(atc260x);
}
EXPORT_SYMBOL_GPL(atc260x_charger_get_cc);

static void atc260x_charger_current_restore(struct atc260x_charger *charger)
{
	int ret, temprature;
	struct atc260x_dev *atc260x = charger->atc260x;
	int cc;

	cc = atc260x_charger_get_cc();
	ret = atc260x_auxadc_get_translated(atc260x, charger->temp_channel, &temprature);
	if (ret) {
		pr_err("[%s]get temprature translated fail!!\n", __func__);
	}
//	pr_info("[%s]:temprature:%d, cc:%d, bati:%d, batv:%d\n", \
		__func__, temprature, cc, charger->bat_info.bat_cur/1000, \
		charger->bat_info.bat_vol/1000);
	if ((temprature < 70000) && (atomic_read(&irq_set_cc))) {
		pr_err("[%s]: set_cc_restore\n", __func__);
		atc260x_charger_set_cc_restore();
		atomic_set(&irq_set_cc, 0);
	}
}

static void atc260x_charger_monitor(struct work_struct *work)
{
	struct atc260x_charger *charger = container_of(work, struct atc260x_charger, work.work);
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;
	int vbus_is_otg;

	atc260x_charger_current_restore(charger);
	mutex_lock(&charger->lock);
	vbus_is_otg = charger->vbus_is_otg;
	if (usb_plugged_type_changed) {
		charger->usb_plugged_type = usb_plugged_type;
		charger->usb_plugged_type_changed = true;
		usb_plugged_type_changed = false;
	}
	mutex_unlock(&charger->lock);
	update_battery_state(charger);
	atc260x_charger_check_online(charger);
	if (charger->charger_cur_status == NO_PLUGGED) {
		if (vbus_is_otg == 0) {
			ops->set_vbus_onoff(atc260x, true);
			ops->set_vbus_pd(atc260x, true);
		}
		if (charger->wall_switch_exist)
			__gpio_set_value(charger->gpio_wall_switch_pin, 0);
	}
	atc260x_wall_set_onoff(charger);
	atc260x_vbus_set_onoff(charger);
	request_system_state(charger);
	atc260x_charger_turn_off(charger);
	atc260x_charger_turn_on(charger);
	atc260x_charger_update_current(charger);
	atc260x_charger_report_change(charger);
	update_led_state(charger);
	queue_delayed_work(charger->wq, &charger->work,
			   msecs_to_jiffies(charger->interval * 1000));
}

static int atc260x_charger_init_base(struct atc260x_charger *charger)
{
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;
	struct charger_dts_items *items = charger->items;

	ops->init_base(atc260x);
	/*wall init */
	ops->set_wall_pd(atc260x, true);
	/* vbus init */
	/*Use USB PC cfg item, default */
	ops->set_vbus_onoff(atc260x, true);
	ops->set_vbus_pd(atc260x, true);
	if (items->usb_pc_ctl_mode == CANCEL_LIMITED) {
		ops->set_vbus_ctl_en(atc260x, false);
		pr_info("%s %d disable vbus\n", __func__, __LINE__);
	} else if (items->usb_pc_ctl_mode == CURRENT_LIMITED) {
		ops->set_vbus_ctl_en(atc260x, true);
		ops->set_vbus_ctlmode(atc260x, CURRENT_LIMITED);
		pr_info("%s %d enable vbus current\n", __func__, __LINE__);
	} else {
		ops->set_vbus_ctl_en(atc260x, true);
		ops->set_vbus_ctlmode(atc260x, VOLTAGE_LIMITED);
		pr_info("%s %d enable vbus voltage\n", __func__, __LINE__);
	}
	/*syspwr init */
	ops->set_syspwr_steady(atc260x, true);
	/*charger init */
	ops->set_cv(atc260x, charger->items->bat_type);

	charger->charger_cur_status = __atc260x_charger_check_online(charger);
	charger->charge_on = false;
	charger->charger_pre_status = -1;
	charger->usb_plugged_type = USB_NO_PLUGGED;
	charger->usb_plugged_type_changed = false;

	return 0;
}

static int atc260x_charger_otint_en(struct atc260x_charger *charger, bool enable)
{
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	if (enable) {
		ops->set_otint_en(atc260x, enable);
		return 0;
	}
	return -ENODEV;
}

int atc260x_charger_get_status(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	return ops->get_onoff(atc260x);
}
EXPORT_SYMBOL_GPL(atc260x_charger_get_status);

void atc260x_charger_off_force(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	mutex_lock(&charger->lock);
	force_charger_off = true;
	mutex_unlock(&charger->lock);

	old_charger_onoff = ops->get_onoff(atc260x);

	ops->set_onoff(atc260x, false);
}
EXPORT_SYMBOL_GPL(atc260x_charger_off_force);

void atc260x_charger_on_force(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	mutex_lock(&charger->lock);
	force_charger_off = false;
	mutex_unlock(&charger->lock);

	ops->set_onoff(atc260x, true);
}
EXPORT_SYMBOL_GPL(atc260x_charger_on_force);

void atc260x_charger_off_restore(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	mutex_lock(&charger->lock);
	force_charger_off = false;
	mutex_unlock(&charger->lock);

	ops->set_onoff(atc260x, old_charger_onoff);
}
EXPORT_SYMBOL_GPL(atc260x_charger_off_restore);

int atc260x_charger_set_cc_finished(void)
{
	return atomic_read(&set_cc_finished);
}
EXPORT_SYMBOL_GPL(atc260x_charger_set_cc_finished);

int atc260x_get_charger_online_status(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();

	if (charger != NULL)
		return charger->charger_online;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(atc260x_get_charger_online_status);

int atc260x_chk_bat_online_intermeddle(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	if (charger != NULL)
		return ops->chk_bat_online(atc260x);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(atc260x_chk_bat_online_intermeddle);

void atc260x_set_usb_plugin_type(int type)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	const char *note[3] = {"notype", "usbpc", "usbadaptor"};

	pr_err("%s, %s\n", __func__, note[type]);
	if (charger != NULL) {
		mutex_lock(&charger->lock);
		usb_plugged_type = type;
		usb_plugged_type_changed = true;
		mutex_unlock(&charger->lock);
	}
}
EXPORT_SYMBOL_GPL(atc260x_set_usb_plugin_type);

bool atc260x_charger_check_capacity(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();

	if (!owl_panel_status_get()) {
		if (charger->charger_cur_status & WALL_PLUGGED)
			return true;
		if ((charger->charger_cur_status & USB_PLUGGED) &&
		    charger->usb_plugged_type == USB_PLUGGED_ADP)
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(atc260x_charger_check_capacity);

static int atc260x_charger_check_wall_online(struct atc260x_charger *charger)
{
	struct atc260x_dev *atc260x = charger->atc260x;
	int data = 0;
	int sum = 0;
	int count = 0;
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = atc260x_auxadc_get_translated(atc260x, charger->wallv_channel, &data);
		if (ret) {
			pr_err("[%s]get wallv translated fail!!\n", __func__);
			data = -1;
		}
		if (data != -1) {
			sum += data;
			count++;
		}
		usleep_range(2000,2200);
	}
	data = sum / count;
	if (data > ATC260X_WALL_VOLTAGE_THRESHOLD) {
		charger->wall_mv = data;
		return WALL_PLUGGED;
	}
	else {
		charger->wall_mv = 0;
		return NO_PLUGGED;
	}
}
/**
 * check whether if usb is online or offline.
 * usb monitor is responsible for checking usb type, also,
 * tell charger whether if otg devivce is connecting.
 * charger is only responsible for checking usb online status
 */
static int atc260x_charger_check_usb_online(struct atc260x_charger *charger)
{
	struct atc260x_dev *atc260x = charger->atc260x;
	int data;
	int sum = 0;
	int count = 0;
	int i, ret;
	int vbus_is_otg;

	mutex_lock(&charger->lock);
	vbus_is_otg = charger->vbus_is_otg;
	mutex_unlock(&charger->lock);

	if ((charger->items->support_adaptor_type == SUPPORT_DCIN_ONLY) || vbus_is_otg)
		return NO_PLUGGED;

	for (i = 0; i < 3; i++) {
		ret = atc260x_auxadc_get_translated(atc260x, charger->vbusv_channel, &data);
		if (ret) {
			pr_err("[%s]get vbusv translated fail!!\n", __func__);
			data = -1;
		}
		if (data != -1) {
			sum += data;
			count++;
		}
		usleep_range(2000,2200);
	}
	data = sum / count;
	if (data > ATC260X_VBUS_VOLTAGE_THRESHOLD) {
		charger->vbus_mv = data;
		return USB_PLUGGED;
	}
	else {
		charger->vbus_mv = 0;
		return NO_PLUGGED;
	}

	return NO_PLUGGED;
}

static int __atc260x_charger_check_online(struct atc260x_charger *charger)
{
	int chg_mode = NO_PLUGGED;

	chg_mode |= atc260x_charger_check_wall_online(charger);
	chg_mode |= atc260x_charger_check_usb_online(charger);

	return chg_mode;
}

static ssize_t show_config(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct atc260x_charger *charger = dev_get_drvdata(dev);
	struct charger_dts_items *items = charger->items;

	pr_info("0 : bl_on_current_usb_pc :%d\n",
		items->bl_on_current_usb_pc);
	pr_info("1 : bl_off_current_usb_pc :%d\n",
		items->bl_off_current_usb_pc);
	pr_info("2 : bl_on_current_usb_adp :%d\n",
		items->bl_on_current_usb_adp);
	pr_info("3 : bl_off_current_usb_adp :%d\n",
		items->bl_off_current_usb_adp);
	pr_info("4 : bl_on_current_wall_adp :%d\n",
		items->bl_on_current_wall_adp);
	pr_info("5 : bl_off_current_wall_adp :%d\n",
		items->bl_off_current_wall_adp);
	pr_info("8 : support_adaptor_type :%d\n",
		items->support_adaptor_type);
	pr_info("11 : bat_type :%d\n",
		items->bat_type);
	pr_info("12 : usb_adapter_as_ac :%d\n",
		items->usb_adapter_as_ac);
	pr_info("13 : usb_pc_ctl_mode :%d\n",
		items->usb_pc_ctl_mode);

	pr_info("NOTE:\n"
		"echo index=value > config\n"
		"e.g. echo 15=20 > config\n");

	return 0;
}

static ssize_t store_config(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct atc260x_charger *charger = dev_get_drvdata(dev);
	struct charger_dts_items *items = charger->items;
	unsigned int index, mid_token, value = 0;
	int *config;
	char *end_ptr;

	index = simple_strtoul(buf, &end_ptr, 0);
	if ((buf == end_ptr))
		goto out;
	mid_token = *end_ptr++;

	switch (mid_token) {
	case '=':
		value = simple_strtoul(end_ptr, NULL, 0);
		break;
	}
	config = (int *)items + index;
	*config = value;
	if (config)
		pr_info("config[%d]:%d\n", index, *config);
out:
	return count;
}

static ssize_t show_chargeron(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_chargeron(struct device *dev,
								struct device_attribute *attr,
								const char *buf,
								size_t count)
{
	int on;
	int ret;

	ret = kstrtoint(buf, 0, &on);
	if (ret)
		return count;

	if (on)
		atc260x_charger_off_restore();
	else
		atc260x_charger_off_force();

	return count;
}

static ssize_t show_dump(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct atc260x_charger *charger = dev_get_drvdata(dev);
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	pr_info("poll interval:%ds\n", charger->interval);
	pr_info("charger_wake_lock:%d\n",
		wake_lock_active(&charger->charger_wake_lock));
	pr_info("tricle_current:%d\n", ops->get_trick_current(atc260x));
	if (charger->ic_type == ATC260X_ICTYPE_2603A) {
		pr_info("pmu ic : 2603A\n");
		if (charger->ic_ver == ATC260X_ICVER_A)
			pr_info("pmu version : IC_VERSION_A\n");
		else if (charger->ic_ver == ATC260X_ICVER_B)
			pr_info("pmu version :  IC_VERSION_B\n");
		else if (charger->ic_ver == ATC260X_ICVER_C)
			pr_info("pmu version: IC_VERSION_C\n");
		else if (charger->ic_ver == ATC260X_ICVER_D)
			pr_info("pmu version  : IC_VERSION_D\n");
		if (charger->ic_ver == ATC260X_ICVER_A ||
		    charger->ic_ver == ATC260X_ICVER_B ||
		    charger->ic_ver == ATC260X_ICVER_C)
			pr_info("constant current:%dmA\n",
				ops->get_cc(atc260x));
		else
			pr_info("constant current:%dmA\n",
				ops->get_cc(atc260x) * 4 / 3);
	} else if (charger->ic_type == ATC260X_ICTYPE_2609A) {
		pr_info("pmu ic : 2609A\n");
	} else if (charger->ic_type == ATC260X_ICTYPE_2603C) {
		pr_info("pmu ic : 2603C\n");
	} else {
		pr_info("pmu ic : ERROR!\n");
	}

	pr_info("charge_on:%d\n", charger->charge_on);
	pr_info("vbus_mv:%dmv\n", charger->vbus_mv);
	pr_info("wall_mv:%dmv\n", charger->wall_mv);
	pr_info("vbus_is_otg:%d\n", charger->vbus_is_otg);
	pr_info("vbus path on/off:%d\n", ops->get_vbus_onoff(atc260x));

	if (charger->charger_cur_status & WALL_PLUGGED)
		pr_info("chg_mode:WALL\n");
	if (charger->charger_cur_status & USB_PLUGGED)
		pr_info("chg_mode:USB\n");

	if (charger->charger_cur_status & USB_PLUGGED) {
		if (charger->usb_plugged_type == USB_PLUGGED_PC)
			pr_info("usb plugged type:USB PC\n");
		else if (charger->usb_plugged_type == USB_PLUGGED_ADP)
			pr_info("usb plugged type:USB ADAPTER\n");
		else
			pr_info("usb plugged type:ERR\n");
	}
	if (ops->get_vbus_ctl_en(atc260x))
		pr_info("vbus control: enable\n");
	else
		pr_info("vbus control: disable\n");
	if (CURRENT_LIMITED == ops->get_vbus_ctlmode(atc260x)) {
		pr_info("vbus ctl mode:CURRENT_LIMITED(%dmA)\n",
			ops->get_vbus_current_lmt(atc260x));
	} else {
		pr_info("vbus ctl mode:VOLTAGE_LIMITED(%dmV)\n",
			ops->get_vbus_vol_lmt(atc260x));
	}

	pr_info("usb_plugged_type_changed:%d\n",
		charger->usb_plugged_type_changed);
	pr_info("support_adaptor_type:%d\n",
			charger->items->support_adaptor_type);
	pr_info("wall_switch_exist:%d\n", charger->wall_switch_exist);
	if (charger->wall_switch_exist)
		pr_info("wall_switch:%d\n",
				__gpio_get_value(charger->gpio_wall_switch_pin));
	return 0;
}

static ssize_t store_dump(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	return 0;
}

static struct device_attribute atc260x_charger_attrs[] = {
	__ATTR(config, S_IRUGO | S_IWUSR, show_config, store_config),
	__ATTR(dump, S_IRUGO | S_IWUSR, show_dump, store_dump),
	__ATTR(chargeron, S_IRUGO | S_IWUSR, show_chargeron, store_chargeron),

};

int atc260x_charger_create_sysfs(struct device *dev)
{
	int r, t;

	pr_info("create sysfs for atc260x charger\n");

	for (t = 0; t < ARRAY_SIZE(atc260x_charger_attrs); t++) {
		r = device_create_file(dev, &atc260x_charger_attrs[t]);

		if (r) {
			dev_err(dev, "failed to create sysfs file\n");
			return r;
		}
	}

	return 0;
}

void atc260x_charger_remove_sysfs(struct device *dev)
{
	int t;

	pr_info("[%s]remove sysfs for atc260x charger\n", __func__);
	for (t = 0; t < ARRAY_SIZE(atc260x_charger_attrs); t++)
		device_remove_file(dev, &atc260x_charger_attrs[t]);
}

static int atc260x_charger_cfg_init(struct device_node *node,
		struct charger_dts_items *items)
{
	struct atc260x_charger *charger = get_atc260x_charger();
	enum of_gpio_flags flags;
	const int *value;
	int active;
	int ret = 0;

	/*bl_on_current_usb_pc */
	value = of_get_property(node, "bl_on_current_usb_pc", NULL);
	if (value) {
		items->bl_on_current_usb_pc = be32_to_cpup(value);
		pr_debug("[%s] bl_on_current_usb_pc success,vaule(%d)!\n",
			 __func__, items->bl_on_current_usb_pc);
	} else {
		items->bl_on_current_usb_pc = 300;
		pr_err("[%s] bl_on_current_usb_pc not found, %d default\n",
		       __func__, items->bl_on_current_usb_pc);
	}
	/*bl_off_current_usb_pc */
	value = of_get_property(node, "bl_off_current_usb_pc", NULL);
	if (value) {
		items->bl_off_current_usb_pc = be32_to_cpup(value);
		pr_debug("[%s] bl_off_current_usb_pc success,vaule(%d)!\n",
			 __func__, items->bl_off_current_usb_pc);
	} else {
		items->bl_off_current_usb_pc = 500;
		pr_err("[%s] bl_off_current_usb_pc not found, %d default\n",
		       __func__, items->bl_off_current_usb_pc);
	}
	/*bl_on_current_usb_adp */
	value = of_get_property(node, "bl_on_current_usb_adp", NULL);
	if (value) {
		items->bl_on_current_usb_adp = be32_to_cpup(value);
		pr_debug("[%s] bl_on_current_usb_adp success,vaule(%d)!\n",
			 __func__, items->bl_on_current_usb_adp);
	} else {
		items->bl_on_current_usb_adp = 300;
		pr_err("[%s] bl_on_current_usb_pc not found, %d default\n",
		       __func__, items->bl_on_current_usb_adp);
	}
	/*bl_on_current_usb_adp_high */
	value = of_get_property(node, "bl_on_current_usb_adp_high", NULL);
	if (value) {
		items->bl_on_current_usb_adp_high = be32_to_cpup(value);
		pr_debug("[%s] bl_on_current_usb_adp_high success,vaule(%d)!\n",
			 __func__, items->bl_on_current_usb_adp_high);
	} else {
		items->bl_on_current_usb_adp_high = 0;
		pr_err("[%s] bl_on_current_usb_pc not found, %d default\n",
		       __func__, items->bl_on_current_usb_adp_high);
	}
	/*bl_off_current_usb_adp */
	value = of_get_property(node, "bl_off_current_usb_adp", NULL);
	if (value) {
		items->bl_off_current_usb_adp = be32_to_cpup(value);
		pr_debug("[%s] bl_off_current_usb_adp success,vaule(%d)!\n",
			 __func__, items->bl_off_current_usb_adp);
	} else {
		items->bl_off_current_usb_adp = 800;
		pr_err("[%s] bl_off_current_usb_adp not found, %d default\n",
		       __func__, items->bl_off_current_usb_adp);
	}
	/*bl_on_current_wall_adp */
	value = of_get_property(node, "bl_on_current_wall_adp", NULL);
	if (value) {
		items->bl_on_current_wall_adp = be32_to_cpup(value);
		pr_debug("[%s] bl_on_current_wall_adp success,vaule(%d)!\n",
			 __func__, items->bl_on_current_wall_adp);
	} else {
		items->bl_on_current_wall_adp = 300;
		pr_err("[%s] bl_on_current_wall_adp not found, %d default\n",
		       __func__, items->bl_on_current_wall_adp);
	}
	/*bl_off_current_wall_adp */
	value = of_get_property(node, "bl_off_current_wall_adp", NULL);
	if (value) {
		items->bl_off_current_wall_adp = be32_to_cpup(value);
		pr_debug("[%s] bl_off_current_wall_adp success,vaule(%d)!\n",
			 __func__, items->bl_off_current_wall_adp);
	} else {
		items->bl_off_current_wall_adp = 1500;
		pr_err("[%s] bl_off_current_wall_adp not found, %d default\n",
		       __func__, items->bl_off_current_wall_adp);
	}

	/*support_adaptor_type */
	value = of_get_property(node, "support_adaptor_type", NULL);
	if (value) {
		items->support_adaptor_type = be32_to_cpup(value);
		pr_debug("[%s] support_adaptor_type success,vaule(%d)!\n",
			 __func__, items->support_adaptor_type);
	} else {
		items->support_adaptor_type = SUPPORT_DCIN_USB;
		pr_err("[%s] support_adaptor_type not found, %d default\n",
		       __func__, items->support_adaptor_type);
	}
	/*change_current_temp */
	value = of_get_property(node, "change_current_temp", NULL);
	if (value) {
		items->change_current_temp = be32_to_cpup(value);
		pr_debug("[%s] change_current_temp success,vaule(%d)!\n",
			 __func__, items->change_current_temp);
	} else {
		items->change_current_temp = 1;
		pr_info("[%s] change_current_temp not found, %d default\n",
		       __func__, items->change_current_temp);
	}
	/*cur_rise_enable */
	value = of_get_property(node, "cur_rise_enable", NULL);
	if (value) {
		items->cur_rise_enable = be32_to_cpup(value);
		pr_debug("[%s] cur_rise_enable success,vaule(%d)!\n",
			 __func__, items->cur_rise_enable);
	} else {
		items->cur_rise_enable = 0;
		pr_info("[%s] cur_rise_enable not found, %d default\n",
		       __func__, items->cur_rise_enable);
	}
	/*ot_shutoff_enable */
	value = of_get_property(node, "ot_shutoff_enable", NULL);
	if (value) {
		items->ot_shutoff_enable = be32_to_cpup(value);
		pr_debug("[%s] ot_shutoff_enable success,vaule(%d)!\n",
			 __func__, items->ot_shutoff_enable);
	} else {
		items->ot_shutoff_enable = 1;
		pr_info("[%s] ot_shutoff_enable not found, %d default\n",
		       __func__, items->ot_shutoff_enable);
	}
	/*bat_type */
	value = of_get_property(node, "bat_type", NULL);
	if (value) {
		items->bat_type = be32_to_cpup(value);
		pr_debug("[%s] bat_type success,vaule(%d)!\n",
			 __func__, items->bat_type);
	} else {
		items->bat_type = BAT_TYPE_4200MV;
		pr_info("[%s] bat_type not found, %d default\n",
		       __func__, items->bat_type);
	}
	/*usb_adapter_as_ac, usb ada's action is same as ac if batv<3.3v */
	value = of_get_property(node, "usb_adapter_as_ac", NULL);
	if (value) {
		items->usb_adapter_as_ac = be32_to_cpup(value);
		pr_debug("[%s] usb_adapter_as_ac success,vaule(%d)!\n",
			 __func__, items->usb_adapter_as_ac);
	} else {
		items->usb_adapter_as_ac = 1;
		pr_info("[%s] usb_adapter_as_ac not found, %d default\n",
		       __func__, items->usb_adapter_as_ac);
	}
	/*
	 * usb_pc_ctl_mode, USB PC control mode,
	 *  0:disable vbus ctl, 1:current limited, 2:voltage limited
	 */
	value = of_get_property(node, "usb_pc_ctl_mode", NULL);
	if (value) {
		items->usb_pc_ctl_mode = be32_to_cpup(value);
		pr_debug("[%s] usb_pc_ctl_mode success,vaule(%d)!\n",
			 __func__, items->usb_pc_ctl_mode);
	} else {
		items->usb_pc_ctl_mode = 1;
		pr_err("[%s] usb_pc_ctl_mode not found, %d default\n",
		       __func__, items->usb_pc_ctl_mode);
	}

	/* get gpio config for WALL SWITCH */
	charger->gpio_wall_switch_pin =
	    of_get_named_gpio_flags(node, GPIO_NAME_WALL_SWITCH, 0, &flags);
	if (charger->gpio_wall_switch_pin >= 0) {
		active = flags & OF_GPIO_ACTIVE_LOW;
		ret = gpio_request(charger->gpio_wall_switch_pin,
				   GPIO_NAME_WALL_SWITCH);
		if (ret < 0) {
			charger->wall_switch_exist = false;
			pr_warn("%s wall swicth gpio(%d) request failed!\n",
				__func__, charger->gpio_wall_switch_pin);
		} else {
			gpio_direction_output(charger->gpio_wall_switch_pin,
						!active);
			charger->wall_switch_exist = true;
			pr_warn("%s wall swicth gpio output(%d)!\n",
				__func__,
			__gpio_get_value(charger->gpio_wall_switch_pin));
		}
	} else {
		charger->wall_switch_exist = false;
		pr_warn("%s wall swicth gpio not exist!\n", __func__);
	}
	return ret;

}

static struct atc260x_charger_ops *atc260x_charger_get_ops(void)
{
#ifdef CONFIG_CHARGER_ATC2609A
	return &atc2609a_charger_ops;
#elif CONFIG_CHARGER_ATC2603C
	return &atc2603c_charger_ops;
#endif
	return NULL;
}

static int fb_notifier_callback(struct notifier_block *p,
				unsigned long event, void *data)
{
	struct fb_event *fb_event = data;
	int *blank = fb_event->data;
	struct atc260x_charger *charger =
		container_of(p, struct atc260x_charger, notifier);
	charger->bl_onoff = *blank ? false : true;

	switch (event) {
	case FB_EVENT_BLANK:
		update_led_state(charger);
		break;
	}

	return 0;
}

/**
 * OT IRQ hander, run in kernel thread of ATC260X core IRQ dispatcher
 */
static irqreturn_t atc260x_charger_ot_irq(int irq, void *data)
{
	struct atc260x_charger *charger = data;
	struct atc260x_dev *atc260x = charger->atc260x;
	struct atc260x_charger_ops *ops = charger->ops;
	int ret, temprature;

	ops->clear_otint_pending(atc260x);
	ret = atc260x_auxadc_get_translated(atc260x, charger->temp_channel, &temprature);
	if (ret) {
		pr_err("[%s]get temprature translated fail!!\n", __func__);
	}
	pr_debug("[%s]:ot int,temp:%d\n", __func__, temprature);

	atc260x_charger_set_cc_force(200);
	atomic_set(&irq_set_cc, 1);
	return IRQ_HANDLED;
}

static int atc260x_charger_probe(struct platform_device *pdev)
{
	struct atc260x_dev *atc260x = dev_get_drvdata(pdev->dev.parent);
	struct atc260x_charger *charger;
	struct power_supply *usb;
	struct power_supply *wall;
	struct device_node *node;
	int ret = 0;
	int irq;

	charger = devm_kzalloc(&pdev->dev, sizeof(struct atc260x_charger),
			       GFP_KERNEL);
	if (!charger) {
		pr_err(" %s devm_kzalloc charger err\n", __func__);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, charger);

	global_charger_dev = charger;
	node = pdev->dev.of_node;
	charger->atc260x = atc260x;
	charger->items = &items;
	charger->dev = &pdev->dev;
	usb = &charger->usb;
	wall = &charger->wall;
	mutex_init(&charger->lock);

	charger->interval = 2;
	charger->debug_file = atc260x_charger_create_debugfs(charger);
	charger->ic_type = atc260x_get_ic_type(atc260x);
	charger->ops = atc260x_charger_get_ops();
	if (!charger->ops) {
		ret = -EINVAL;
		goto err_remove_debugfs;
	}

	charger->power_on_trig_name = "power-on";
	led_trigger_register_simple(charger->power_on_trig_name,
				    &charger->power_on_trig);

	charger->notifier.notifier_call = fb_notifier_callback;
	ret = fb_register_client(&(charger->notifier));
	if (ret)
		goto err_remove_debugfs;

	/*init wall power supply */
	wall->name = PSY_NAME_WALL;
	wall->type = POWER_SUPPLY_TYPE_MAINS;
	wall->properties = atc260x_wall_props;
	wall->num_properties = ARRAY_SIZE(atc260x_wall_props);
	wall->get_property = atc260x_wall_get_prop;
	ret = power_supply_register(&pdev->dev, wall);
	if (ret) {
		pr_err("%s power_supply_register wall err\n", __func__);
		goto err_remove_debugfs;
	}
	/*init usb power supply */
	usb->name = PSY_NAME_USB, usb->type = POWER_SUPPLY_TYPE_USB;
	usb->properties = atc260x_usb_props;
	usb->num_properties = ARRAY_SIZE(atc260x_usb_props);
	usb->get_property = atc260x_usb_get_prop;
	ret = power_supply_register(&pdev->dev, usb);
	if (ret) {
		pr_err("%s power_supply_register usb err\n", __func__);
		goto err_wall;
	}

	ret = atc260x_charger_create_sysfs(&pdev->dev);
	if (ret)
		goto err_usb;

	/*init auxadc icm and batv channel*/
	charger->vbusv_channel = atc260x_auxadc_find_chan(atc260x, "VBUSV");
	charger->wallv_channel = atc260x_auxadc_find_chan(atc260x, "WALLV");
	charger->temp_channel = atc260x_auxadc_find_chan(atc260x, "ICTEMP");

	atc260x_charger_cfg_init(node, charger->items);
	atc260x_charger_init_base(charger);

	wake_lock_init(&charger->charger_wake_lock,
		       WAKE_LOCK_SUSPEND, "charger_lock");
	wake_lock_init(&charger->delay_lock, WAKE_LOCK_SUSPEND, "delay_lock");

	charger->wq = create_singlethread_workqueue("atc260x_wq");
	if (!charger->wq) {
		pr_err("[%s]:failed to create work queue\n", __func__);
		goto err_destroy_wakelock;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		pr_err("[%s]:No IRQ resource for charger\n", __func__);
		ret = -ENODEV;
		goto err_destroy_wakelock;
	}
	dev_info(&pdev->dev, "atc260x_charter_otint IRQ num : %u\n", irq);
	charger->irq = irq;
	/*
	 *use default primary handle, and atc260x_onoff_irq run in ATC260X core irq kernel thread
	 */
	ret = devm_request_threaded_irq(&pdev->dev, charger->irq, NULL,
					atc260x_charger_ot_irq, IRQF_TRIGGER_HIGH,
					"atc260x_charger_ot_irq", charger);
	if (ret < 0) {
		pr_err("[%s]:Unable to request IRQ: %d\n", __func__, ret);
		goto err_destroy_wakelock;
	}
	atc260x_charger_otint_en(charger, 1);
	INIT_DELAYED_WORK(&charger->work, atc260x_charger_monitor);
	queue_delayed_work(charger->wq, &charger->work, 0 * HZ);

	return 0;

err_destroy_wakelock:
	wake_lock_destroy(&charger->charger_wake_lock);
	wake_lock_destroy(&charger->delay_lock);
	atc260x_charger_remove_sysfs(&pdev->dev);
err_usb:
	power_supply_unregister(usb);
err_wall:
	power_supply_unregister(wall);
err_remove_debugfs:
	atc260x_charger_remove_debugfs(charger);

	return ret;
}

static int atc260x_charger_remove(struct platform_device *pdev)
{
	struct atc260x_charger *charger = platform_get_drvdata(pdev);

	if (!charger) {
		pr_warn("atc260x_charger is null ,probe maybe failed\n");
		return 0;
	}

	cancel_delayed_work_sync(&charger->work);
	atc260x_charger_remove_debugfs(charger);
	power_supply_unregister(&charger->wall);
	power_supply_unregister(&charger->usb);

	return 0;
}

static int atc260x_charger_suspend(struct device *dev)
{
	struct atc260x_charger *charger = dev_get_drvdata(dev);

	dev_info(dev, "atc260x_charger_suspend()\n");
	cancel_delayed_work_sync(&charger->work);
	dev_info(dev, "charger is %s\n", charger->charge_on ? "on" : "off");
	dev_info(dev, "wall voltage = %d (mV)\n", charger->wall_mv);
	dev_info(dev, "vbus voltage = %d (mV)\n", charger->vbus_mv);
	return 0;
}

static int atc260x_charger_resume(struct device *dev)
{
	struct atc260x_charger *charger = dev_get_drvdata(dev);

	queue_delayed_work(charger->wq, &charger->work, 0 * HZ);
	dev_info(dev, "charger is %s\n", charger->charge_on ? "on" : "off");
	dev_info(dev, "wall voltage = %d (mV)\n", charger->wall_mv);
	dev_info(dev, "vbus voltage = %d (mV)\n", charger->vbus_mv);
	if (charger->bat_info.battery_soc == 100)
		wake_lock_timeout(&charger->delay_lock, 3 * HZ);
	return 0;
}

static const struct dev_pm_ops atc260x_charger_pm_ops = {
	.suspend = atc260x_charger_suspend,
	.resume = atc260x_charger_resume,
};

static void atc260x_charger_shutdown(struct platform_device *pdev)
{
	struct atc260x_charger *charger = platform_get_drvdata(pdev);
	struct atc260x_charger_ops *ops = charger->ops;
	struct atc260x_dev *atc260x = charger->atc260x;

	dev_info(&pdev->dev, "atc260x_charger_shutdown()\n");
	dev_info(&pdev->dev, "charger is %s\n", charger->charge_on ? "on" : "off");
	dev_info(&pdev->dev, "wall voltage = %d (mV)\n", charger->wall_mv);
	dev_info(&pdev->dev, "vbus voltage = %d (mV)\n", charger->vbus_mv);
	cancel_delayed_work_sync(&charger->work);

	if (ops->set_wall_pd)
		ops->set_wall_pd(atc260x, true);
	led_trigger_event(charger->power_on_trig, LED_OFF);
	return;
}

static const struct of_device_id atc260x_charger_match[] = {
	{.compatible = "actions,atc2603a-charger",},
	{.compatible = "actions,atc2603c-charger",},
	{.compatible = "actions,atc2609a-charger",},
	{},
};

MODULE_DEVICE_TABLE(of, atc260x_charger_match);

static struct platform_driver atc260x_charger_driver = {
	.probe = atc260x_charger_probe,
	.remove = atc260x_charger_remove,
	.driver = {
		   .name = "atc260x-charger",
		   .pm = &atc260x_charger_pm_ops,
		   .of_match_table = of_match_ptr(atc260x_charger_match),
		   },
	.shutdown = atc260x_charger_shutdown,
};

static int __init atc260x_charger_init(void)
{
	pr_info("atc260x_charger: version(%s), time stamp(%s)\n",
		ATC260X_CHARGER_DRV_VERSION, ATC260X_CHARGER_DRV_TIMESTAMP);
	return platform_driver_register(&atc260x_charger_driver);
}
module_init(atc260x_charger_init);

static void __exit atc260x_charger_exit(void)
{
	struct atc260x_charger *charger = get_atc260x_charger();

	power_supply_unregister(&charger->wall);
	power_supply_unregister(&charger->usb);
	wake_lock_destroy(&charger->charger_wake_lock);
	wake_lock_destroy(&charger->delay_lock);
	platform_driver_unregister(&atc260x_charger_driver);
}
module_exit(atc260x_charger_exit);

MODULE_AUTHOR("Actions Semi, Inc");
MODULE_DESCRIPTION("atc260x charger driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atc260x-charger");
