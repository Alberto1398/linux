/*
 * bq27441 battery driver
 *
 * Copyright (C) 2008 Rodolfo Giometti <giometti@linux.it>
 * Copyright (C) 2008 Eurotech S.p.A. <info@eurotech.it>
 * Copyright (C) 2010-2011 Lars-Peter Clausen <lars@metafoo.de>
 * Copyright (C) 2011 Pali Rohár <pali.rohar@gmail.com>
 * Copyright (C) 2015 Actions
 *
 * Based on a previous work by Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

/*
 * Datasheets:
 * http://www.ti.com/lit/ds/symlink/bq27441-g1.pdf
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/bootafinfo.h>
#include <linux/idr.h>
#include <linux/string.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <asm/unaligned.h>
#include <linux/mfd/atc260x/atc260x.h>

#define DRIVER_VERSION			"1.0.0"
#define BQ27441_DEVTYPE			0x0421
#define BQ27441_DMCODE_G1A		0x48
#define BQ27441_DMCODE_G1B		0x58

#define TI_CONFIG_RSENSE		10
#define TI_CONFIG_RESERVEDCAP		0x0
#define TI_CONFIG_SLEEPCUR		14 /*mA*/
#define TI_CONFIG_TERMVDATLE		100
#define TI_CONFIG_CAP			5700
#define TI_CONFIG_TERMVOL		3400
#define TI_CONFIG_TAPCUR		350
#define TI_CONFIG_TAPERVOL		4200
#define TI_CONFIG_QMAXCELL		17299
#define TI_CONFIG_LOADSEL		0x81
#define TI_CONFIG_DESIGNENERGY		22200
#define TI_CONFIG_DSGCUR		167
#define TI_CONFIG_CHGCUR		100
#define TI_CONFIG_QUITCUR		40
#define TI_CONFIG_DODEOC_DELTAT		0xc8


/**bq27441 sub command of control*/
#define CONTROL_STATUS			0x0000
#define DEVICE_TYPE			0x0001
#define FW_VERSION			0x0002
#define DM_CODE				0x0004
#define PREV_MACWRITE			0x0007
#define CHEM_ID				0x0008
#define BAT_INSERT			0x000c
#define BAT_REMOVE			0x000d
#define SET_HIBERNATE			0x0011
#define CLEAR_HIBERNATE			0x0012
#define SET_CFGUPDATE			0x0013
#define SHUTDOWN_ENABLE			0x001b
#define SHUTDOWN			0x001c
#define SEALED				0x0020
#define TOGGLE_GPOUT			0x0023
#define RESET				0x0041
#define SOFT_RESET			0x0042
#define UNSEALED			0x8000
/*CONTROL_STATUS*/
#define CONTROL_STATUS_SS		BIT(13)
/*bq27441 reg addr*/
#define BQ27441_REG_CONTROL		0x00
#define BQ27441_REG_TEMP		0x02
#define BQ27441_REG_VOLT		0x04
#define BQ27441_REG_FLAGS		0x06
#define BQ27441_REG_NAC			0x08 /*Nominal Available Capacity*/
#define BQ27441_REG_FAC			0x0a /*Full Available Capacity*/
#define BQ27441_REG_RM			0x0c /*Remaining Capacity*/
#define BQ27441_REG_FCC			0x0e /*Full Charge Capacity*/
#define BQ27441_REG_AC			0x10 /*Average Current*/
#define BQ27441_REG_SC			0x12 /*Standby Current*/
#define BQ27441_REG_MLC			0x14 /*Max Load Current*/
#define BQ27441_POWER_AVG		0x18 /*Average Power*/
#define BQ27441_REG_SOC			0x1c /*State of Charge*/
#define BQ27441_REG_INTTEMP		0x1e /*Internal Temprature, unit:0.1K*/
#define BQ27441_REG_SOH			0x20 /*State of Health*/
/*bq27441 flags reg  bit*/
#define BQ27441_FLAG_DSG		BIT(0) /*Discharge Detect*/
#define BQ27441_FLAG_SOCF		BIT(1)
#define BQ27441_FLAG_SOC1		BIT(2)
#define BQ27441_FLAG_BATDET		BIT(3)
#define BQ27441_FLAG_CFGUPMODE		BIT(4)
#define BQ27441_FLAG_ITPOR		BIT(5)
#define BQ27441_FLAG_OCVTAKEN		BIT(7)
#define BQ27441_FLAG_CHG		BIT(8)
#define BQ27441_FLAG_FC			BIT(9)
#define BQ27441_FLAG_UT			BIT(14)
#define BQ27441_FLAG_OT			BIT(15)
/*bq27441 extend reg addr,0x62~0x7f reserved*/
#define BQ27441_EXT_REG_OPCONFIG	0x3a
#define BQ27441_EXT_REG_DESIGNCAPACITY	0x3c
#define BQ27441_EXT_REG_DATACLASS	0x3e
#define BQ27441_EXT_REG_DATABLOCK	0x3f
#define BQ27441_EXT_REG_BLOCKDATA_BEGIN 0x40
#define BQ27441_EXT_REG_BLOCKDATA_END	0x5f
#define BQ27441_EXT_REG_BLOCKDATACHKSUM 0x60
#define BQ27441_EXT_REG_BLOCKDATACTL	0x61
/*bq27441 data memory sub class id*/
#define BQ27441_DM_SC_SAFETY		0x02
#define BQ27441_DM_SC_CHTERM		0x24
#define BQ27441_DM_SC_CURTHRESH		0x51
#define BQ27441_DM_SC_STATE		0x52
#define BQ27441_DM_SC_RARAM		0x59
#define BQ27441_DM_SC_DATA			0x68
#define BQ27441_DM_SC_CC_CAL			0x69
/*bq27441 working voltage ,unit:mv*/
#define BQ27441_WORKING_VOLTAGE	2850

struct bq27441_device_info;
struct bq27441_access_methods {
	int (*read)(struct bq27441_device_info *di, u8 reg,
		bool single, bool le);
	int (*write)(struct bq27441_device_info *di, u8 reg,
		u16 val, bool single, bool le);
};
enum bq27xxx_chip {BQ27441};
enum bq27441_chip {BQ27441_G1A, BQ27441_G1B};

struct bq27441_calibration_offset {
	u32 board_offset;
};

struct bq27441_calibration_cc {
	u32 cc_gain;
	u32 cc_delta;
};

struct bq27441_sc_state {
	u32 qmax_cell;
	u32 load_sel;
	u32 design_capacity;
	u32 term_vol;
	u32 term_vdatle;
	u32 taper_rate;
	u32 taper_vol;
	u32 sleep_current;
	u32 reserve_cap;
	u32 design_energy;
};

struct bq27441_sc_cur_thresholds {
	u32 dsg_cur;
	u32 chg_cur;
	u32 quit_cur;
};

struct	bq27441_cfg_items {
	u32 *r_table;
	u32 taper_cur;
	u32 rsense;

	struct bq27441_sc_state state;
	struct bq27441_sc_cur_thresholds cur_thresholds;
	struct bq27441_calibration_offset offset;
	struct bq27441_calibration_cc	cc;
	int log_switch;
};

struct bq27441_reg_cache {
	int temperature;
	int charge_full;
	int capacity;
	int flags;
	int power_avg;
	int health;
};

struct bq27441_device_info {
	struct device		*dev;
	int			id;

	struct	bq27441_cfg_items items;

	int poll_interval;

	struct workqueue_struct *wq;
	struct delayed_work init_work;

	struct bq27441_reg_cache cache;
	int charge_design_full;
	int bat_curr;
	int bat_vol;
	bool first_detect;
	int online;

	unsigned long last_update;
	struct delayed_work work;

	struct power_supply	bat;

	struct bq27441_access_methods bus;

	struct mutex lock;
};

static enum power_supply_property bq27441_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
};

u32 r_table[15] = {
	99,
	99,
	98,
	106,
	71,
	58,
	61,
	62,
	52,
	47,
	59,
	69,
	139,
	365,
	583
};
static bool first_store_log = true;
struct bq27441_device_info *di;
extern int atc260x_chk_bat_online_intermeddle(void);
static int bq27441_battery_store_info(struct bq27441_device_info *di);
static void bq27441_battery_cfg_init(struct bq27441_device_info *di);
static int bq27441_battery_update_config(struct bq27441_device_info *di);
static int bq27441_battery_get_pwron_state(struct bq27441_device_info *di);

/*
  *  Bq27441 read function.
  *  Note: reg use little endian mode;data memory use big endian.
  */
static inline int bq27441_read(struct bq27441_device_info *di, u8 reg,
		bool single)
{
	if (reg >= BQ27441_EXT_REG_BLOCKDATA_BEGIN
		&& reg <= BQ27441_EXT_REG_BLOCKDATA_END)
		return di->bus.read(di, reg, single, false);
	else
		return di->bus.read(di, reg, single, true);

}
/*
  *  Bq27441 write function.
  *  Note: reg use little endian mode;data memory use big endian.
  */
static inline int bq27441_write(struct bq27441_device_info *di, u8 reg,
		u16 val, bool single)
{
	if (reg >= BQ27441_EXT_REG_BLOCKDATA_BEGIN
		&& reg <= BQ27441_EXT_REG_BLOCKDATA_END)
		return di->bus.write(di, reg, val, single, false);
	else
		return di->bus.write(di, reg, val, single, true);
}

/*
  *  Bq27441 set subcmd function.
  *  Note: subcmd use little endian mode fixed.
  */
static inline int bq27441_set_subcmd(struct bq27441_device_info *di, u16 val)
{
	return di->bus.write(di, BQ27441_REG_CONTROL, val, false, true);
}

/*
 * Read a power avg register.
 * Return < 0 if something fails.
 */
static int bq27441_battery_read_pwr_avg(struct bq27441_device_info *di)
{
	int tval;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD)
		return 0;

	tval = bq27441_read(di, BQ27441_POWER_AVG, false);
	if (tval < 0) {
		dev_err(di->dev, "error reading BQ27441_POWER_AVG: %d\n",
			tval);
		return tval;
	}

	return tval;
}
/*
 * Read	 health state of battery.
 * Return < 0 if something fails.
 */
static int bq27441_battery_read_health(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_read(di, BQ27441_REG_FLAGS, false);
	if (tval < 0) {
		dev_err(di->dev, "error reading flag register:%d\n",
			tval);
		tval = POWER_SUPPLY_HEALTH_DEAD;
		return tval;
	}

	if (tval & BQ27441_FLAG_OT)
		tval = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (tval & BQ27441_FLAG_UT)
		tval = POWER_SUPPLY_HEALTH_COLD;
	else
		tval = POWER_SUPPLY_HEALTH_GOOD;

	return tval;
}

/*
 * Read	 remaining soc(available soc) of battery.
 * Return < 0 if something fails.
 */
static int bq27441_battery_read_rsoc(struct bq27441_device_info *di)
{
	int rsoc;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD)
		return 0;

	rsoc = bq27441_read(di, BQ27441_REG_SOC, false);
	if (rsoc < 0) {
		dev_err(di->dev, "error reading soc\n");
		return rsoc;
	}

	return rsoc;
}

static int bq27441_battery_read_temperature(struct bq27441_device_info *di)
{
	int temp;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD)
		return 0;

	temp = bq27441_read(di, BQ27441_REG_INTTEMP, false);
	if (temp < 0) {
		dev_err(di->dev, "error reading temperature\n");
		return temp;
	}

	return temp;
}

static void bq27441_update(struct bq27441_device_info *di)
{
	struct bq27441_reg_cache cache = {0, };

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD)
		goto dead;
	cache.flags = bq27441_read(di, BQ27441_REG_FLAGS, false);
	if (cache.flags > 0) {
		cache.temperature = bq27441_battery_read_temperature(di);
		cache.charge_full = cache.flags & BQ27441_FLAG_FC;
		cache.capacity = bq27441_battery_read_rsoc(di);
		cache.health = bq27441_battery_read_health(di);
		cache.power_avg = bq27441_battery_read_pwr_avg(di);

		if (di->items.log_switch)
			bq27441_battery_store_info(di);

		if (memcmp(&di->cache, &cache, sizeof(cache)) != 0) {
			di->cache = cache;
			power_supply_changed(&di->bat);
		}
	}

dead:
	di->last_update = jiffies;
}

static void bq27441_battery_poll(struct work_struct *work)
{
	struct bq27441_device_info *di =
		container_of(work, struct bq27441_device_info, work.work);
	int channel, batv;
	int state = 0;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD) {
		channel = atc260x_ex_auxadc_find_chan("BATV");
		atc260x_ex_auxadc_read(channel, &batv);
		if (batv >= BQ27441_WORKING_VOLTAGE)
			state = bq27441_battery_get_pwron_state(di);
			if (state > 0)
				if (!bq27441_battery_update_config(di))
					di->cache.health = bq27441_battery_read_health(di);
	}


	bq27441_update(di);

	if (di->poll_interval > 0) {
		/* The timer does not have to be accurate. */
		set_timer_slack(&di->work.timer, di->poll_interval * HZ / 4);
		schedule_delayed_work(&di->work, di->poll_interval * HZ);
	}
}

/*
 *  Read NominalAvailableCapacity in mAh
 */
static int bq27441_battery_read_nac(struct bq27441_device_info *di,
	union power_supply_propval *val)
{
	int charge;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD) {
		val->intval = 0;
		return 0;
	}

	charge = bq27441_read(di, BQ27441_REG_NAC, false);
	if (charge < 0) {
		dev_err(di->dev, "error reading NominalAvailableCapacity\n");
		return charge;
	}

	val->intval = charge;

	return 0;
}

/*
 *  read battery FullChargeCapacity value in mAh
 */
static int bq27441_battery_read_fac(struct bq27441_device_info *di,
	union power_supply_propval *val)
{
	int charge;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD) {
		val->intval = 0;
		return 0;
	}

	charge = bq27441_read(di, BQ27441_REG_FAC, false);
	if (charge < 0) {
		dev_err(di->dev, "error reading FullChargeCapacity\n");
		return charge;
	}

	val->intval = charge;

	return 0;
}

/*
 * Return the battery Voltage in uV
 * Or < 0 if something fails.
 */
static int bq27441_battery_voltage(struct bq27441_device_info *di,
	union power_supply_propval *val)
{
	int channel;
	int volt;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD) {
		channel = atc260x_ex_auxadc_find_chan("BATV");
		atc260x_ex_auxadc_read(channel, &volt);
		val->intval = volt * 1000;
		return 0;
	}

	volt = bq27441_read(di, BQ27441_REG_VOLT, false);
	if (volt < 0) {
		dev_err(di->dev, "error reading voltage\n");
		return volt;
	}

	val->intval = volt * 1000;

	return 0;
}

/*
 * Return the battery average current in uA
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int bq27441_battery_current(struct bq27441_device_info *di,
	union power_supply_propval *val)
{
	int curr;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD) {
		val->intval = 0;
		return 0;
	}

	curr = bq27441_read(di, BQ27441_REG_AC, false);
	if (curr < 0) {
		dev_err(di->dev, "error reading current\n");
		return curr;
	}


	if (curr & 0x8000)
		curr = curr | 0xffff0000;

	val->intval = curr * 1000;

	return 0;
}

static int bq27441_battery_status(struct bq27441_device_info *di,
	union power_supply_propval *val)
{
	int status;

	if (di->cache.flags & BQ27441_FLAG_FC)
		status = POWER_SUPPLY_STATUS_FULL;
	else if (di->cache.flags & BQ27441_FLAG_DSG)
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		status = POWER_SUPPLY_STATUS_CHARGING;

	val->intval = status;

	return 0;
}

static int bq27441_battery_online(struct bq27441_device_info *di,
	union power_supply_propval *val)
{
	if (di->first_detect) {
		val->intval = atc260x_chk_bat_online_intermeddle();
		di->online = val->intval;
		di->first_detect = false;
	} else {
		val->intval = di->online;
	}

	return 0;
}

static int bq27441_battery_capacity_level(struct bq27441_device_info *di,
	union power_supply_propval *val)
{
	int level;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD) {
		val->intval = 0;
		return 0;
	}

	if (di->cache.flags & BQ27441_FLAG_FC)
		level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (di->cache.flags & BQ27441_FLAG_SOC1)
		level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (di->cache.flags & BQ27441_FLAG_SOCF)
		level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else
		level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;

	val->intval = level;

	return 0;
}

static int bq27441_simple_value(int value,
	union power_supply_propval *val)
{
	if (value < 0)
		return value;

	val->intval = value;

	return 0;
}

static int bq27441_battery_store_info(struct bq27441_device_info *di)
{
	union power_supply_propval val;
	struct rtc_device *rtc;
	struct rtc_time tm;
	u8 buf[200];
	struct file *filp;
	mm_segment_t fs;
	int offset = 0;

	fs = get_fs();
	set_fs(KERNEL_DS);

	filp = filp_open("/mnt/sdcard/cap_gauge_info.log",
		O_NONBLOCK | O_CREAT | O_RDWR, 0644);
	if (IS_ERR(filp)) {
		dev_info(di->dev, "[store bat info]can't access cap_gauge_info.log\n");
		return -1;
	}

	if (first_store_log) {
		memset(buf, 0, 200);
		offset = sprintf(buf, "time,current,voltage,fac,asoc,full,flags,"
			"health,pwravg,temperature\t\n");
		filp->f_op->llseek(filp, 0, SEEK_END);
		filp->f_op->write(filp, (char *)buf, offset + 1, &filp->f_pos);
		first_store_log = false;
	}

	rtc = rtc_class_open("rtc0");
	if (rtc == NULL) {
		dev_err(di->dev, "[store bat info] unable to open rtc device\n");
		return -1;
	}
	rtc_read_time(rtc, &tm);
	rtc_class_close(rtc);

	filp->f_op->llseek(filp, 0, SEEK_END);

	memset(buf, 0, 200);

	bq27441_battery_current(di, &val);
	di->bat_curr = val.intval;
	bq27441_battery_voltage(di, &val);
	di->bat_vol = val.intval;
	bq27441_battery_read_fac(di, &val);
	di->charge_design_full = !!val.intval;

	offset = sprintf(buf, "%2d-%02d-%02d %02d:%02d:%02d,"
		"%04d,%04d,%04d,%d,%d,%d,%d,%d,%d\t\n",
		tm.tm_year + 1900,
		tm.tm_mon + 1,
		tm.tm_mday,
		(tm.tm_hour + 24 - 4) % 24,
		tm.tm_min,
		tm.tm_sec,

		di->bat_curr,
		di->bat_vol,
		di->charge_design_full,
		di->cache.capacity,
		di->cache.charge_full,
		di->cache.flags,
		di->cache.health,
		di->cache.power_avg,
		di->cache.temperature);

	filp->f_op->write(filp, (char *)buf, offset + 1, &filp->f_pos);

	set_fs(fs);
	filp_close(filp, NULL);

	return 0;
}

#define to_bq27441_device_info(x) container_of((x), \
				struct bq27441_device_info, bat);

static int bq27441_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int ret = 0;
	struct bq27441_device_info *di = to_bq27441_device_info(psy);
#if 0
	mutex_lock(&di->lock);
	if (time_is_before_jiffies(di->last_update + 5 * HZ)) {
		cancel_delayed_work_sync(&di->work);
		bq27441_battery_poll(&di->work.work);
	}
	mutex_unlock(&di->lock);

	if (psp != POWER_SUPPLY_PROP_PRESENT && di->cache.flags < 0)
		return -ENODEV;
#endif

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq27441_battery_status(di, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bq27441_battery_voltage(di, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq27441_battery_online(di, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = bq27441_battery_current(di, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = bq27441_simple_value(di->cache.capacity, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		ret = bq27441_battery_capacity_level(di, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = bq27441_simple_value(di->cache.temperature, val);
		if (ret == 0)
			val->intval -= 2731;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = bq27441_battery_read_nac(di, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = bq27441_simple_value(di->cache.charge_full, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = bq27441_battery_read_fac(di, val);
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		ret = bq27441_simple_value(di->cache.power_avg, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq27441_simple_value(di->cache.health, val);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int bq27441_battery_get_control_status(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, CONTROL_STATUS);
	if (tval < 0) {
		dev_err(di->dev, "[ctl status]set CONTROL_STATUS err(%d)",
				tval);
		return tval;
	}

	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0) {
		dev_err(di->dev, "[ctl status]read CONTROL_STATUS err(%d)",
			tval);
		return tval;
	}

	dev_info(di->dev, "[ctl status]get CONTROL_STATUS success(0x%x)\n",
		tval);

	return 0;
}

static int bq27441_battery_get_device_type(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, DEVICE_TYPE);
	if (tval < 0) {
		dev_err(di->dev, "[dev type]set DEVICE_TYPE err(%d)",
			tval);
		return tval;
	}

	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0) {
		dev_err(di->dev, "[dev type]read DEVICE_TYPE err(%d)",
			tval);
		return tval;
	}

	dev_info(di->dev, "[dev type]get device_type success(0x%x)\n", tval);

	return tval;
}

static int bq27441_battery_get_fw_version(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, FW_VERSION);
	if (tval < 0) {
		dev_err(di->dev, "[fw version]set FW_VERSION err(%d)",
			tval);
		return tval;
	}

	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0) {
		dev_err(di->dev, "[fw version]read FW_VERSION err(%d)",
			tval);
		return tval;
	}

	dev_info(di->dev, "[fw version]get FW_VERSION success(0x%x)\n",
		tval);

	return 0;
}

static int bq27441_battery_get_dm_code(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, DM_CODE);
	if (tval < 0) {
		dev_err(di->dev, "[dm code]set DM_CODE err(%d)",
			tval);
		return tval;
	}

	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0) {
		dev_err(di->dev, "[dm code]read DM_CODE err(%d)",
			tval);
		return tval;
	}

	dev_info(di->dev, "[dm code]get DM_CODE success(0x%x)\n",
		tval);

	return tval;
}

static int bq27441_battery_get_prev_macwrite(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, PREV_MACWRITE);
	if (tval < 0) {
		dev_err(di->dev, "[PREV_MACWRITE]set PREV_MACWRITE err(%d)",
			tval);
		return tval;
	}

	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0) {
		dev_err(di->dev, "[PREV_MACWRITE]read PREV_MACWRITE err(%d)",
			tval);
		return tval;
	}

	dev_info(di->dev, "[PREV_MACWRITE]get PREV_MACWRITE success(0x%x)\n",
		tval);

	return tval;
}

static int bq27441_battery_get_chem_id(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, CHEM_ID);
	if (tval < 0) {
		dev_err(di->dev, "[CHEM_ID]set CHEM_ID err(%d)", tval);
		return tval;
	}

	tval = bq27441_read(di, CHEM_ID, false);
	if (tval < 0) {
		dev_err(di->dev, "[CHEM_ID]read CHEM_ID err(%d)", tval);
		return tval;
	}

	dev_info(di->dev, "[CHEM_ID]get CHEM_ID success(0x%x)\n", tval);

	return tval;
}


static int bq27441_battery_set_sealed(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, CONTROL_STATUS);
	if (tval < 0) {
		dev_err(di->dev, "[set sealed]send CONTROL_STATUS subcmd err(%d)",
			tval);
		return tval;
	}

	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0) {
		dev_err(di->dev, "[set sealed]read BQ27441_REG_CONTROL err(%d)",
			tval);
		return tval;
	}
	dev_info(di->dev, "[set sealed]read BQ27441_REG_CONTROL(0x%x)\n", tval);

	if (tval & CONTROL_STATUS_SS) {
		dev_info(di->dev, "[set sealed]already sealed mode\n");
		return 0;
	}

	tval = bq27441_set_subcmd(di, SEALED);
	if (tval < 0) {
		dev_err(di->dev, "[set sealed]send SEALED subcmd err(%d)",
			tval);
		return tval;
	}
	msleep(1000);

	return 0;
}

static int bq27441_battery_set_unsealed(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, CONTROL_STATUS);
	if (tval < 0) {
		dev_err(di->dev, "[set unsealed]send CONTROL_STATUS subcmd err(%d)",
			tval);
		return tval;
	}

	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0) {
		dev_err(di->dev, "[set unsealed]read BQ27441_REG_CONTROL err(%d)",
			tval);
		return tval;
	}
	dev_info(di->dev, "[set unsealed]read BQ27441_REG_CONTROL(0x%x)\n", tval);

	if (!(tval & CONTROL_STATUS_SS)) {
		dev_info(di->dev, "[set unsealed]already unsealed mode\n");
		return 0;
	}

	tval = bq27441_set_subcmd(di, UNSEALED);
	if (tval < 0) {
		dev_err(di->dev, "[set unsealed]send SEALED subcmd 1st err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_set_subcmd(di, UNSEALED);
	if (tval < 0) {
		dev_err(di->dev, "[set unsealed]send SEALED subcmd 2st err(%d)",
			tval);
		return tval;
	}

	return 0;
}

static int bq27441_battery_cfgupdate_finished(struct bq27441_device_info *di)
{
	int tval;

	tval = bq27441_set_subcmd(di, SOFT_RESET);
	if (tval < 0) {
		dev_err(di->dev, "[cfg finish]SOFT RESET err(%d)",
			tval);
		return tval;
	}

	msleep(2000);

	tval = bq27441_read(di, BQ27441_REG_FLAGS, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg finish] BQ27441_REG_FLAGS err(%d)",
			tval);
		return tval;
	} else {
		if (tval & BQ27441_FLAG_CFGUPMODE) {
			dev_err(di->dev, "[cfg finish] CFGUPDATE mode unclear! 0x%x,\n",
				tval);
			return -EINVAL;
		}
	}

	bq27441_battery_set_sealed(di);

	return 0;
}

static int bq27441_battery_select_subclass(struct bq27441_device_info *di,
	u8 subclass_id, u8 index)
{
	int tval;

	/*select state subclass*/
	tval = bq27441_write(di, BQ27441_EXT_REG_BLOCKDATACTL, 0x00, true);
	if (tval < 0) {
		dev_err(di->dev, "[sel scid]enable data memory access err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, BQ27441_EXT_REG_DATACLASS, subclass_id, true);
	if (tval < 0) {
		dev_err(di->dev, "[sel scid]set data memory subclass id(%d) err(%d)",
			subclass_id, tval);
		return tval;
	}
	tval = bq27441_write(di, BQ27441_EXT_REG_DATABLOCK, index, true);
	if (tval < 0) {
		dev_err(di->dev, "[sel scid]set index of data block err(%d)",
			tval);
		return tval;
	}

	return 0;
}
/*
 * calibration step as follows:
 * 1.cc offset calibration:
 *   set machine to 0A environment, calibrated by bq27441 automatically;
 * 2.board offset calibration:
 *   using bqstido software;
 * 3.cc gain calibration:
 *   factor = (avgRawCurrent - (adj_ccOffset + adj_boardOffset)) / realCurrent;
 *   cc_gain = cc_gain_default / factor;
 *   here, adj_ccOffset = ccOffset / 16;
 *         adj_boardOffset = boardOffset / 16;
 * 4.cc_delta calibration:
 *   cc_delta = cc_delta_default / factor
 */
static int bq27441_battery_calibrate_cc(struct bq27441_device_info *di)
{
	struct bq27441_cfg_items *items = &di->items;
	struct bq27441_calibration_cc *new_cc = &items->cc;
	struct bq27441_calibration_cc old_cc;
	int tval;
	u8 old_chksum = 0;
	u8 new_chksum = 0;

	if (new_cc->cc_gain == -1 || new_cc->cc_delta == -1)
		return -EINVAL;

	/*select subclass calibration cc*/
	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_CC_CAL, 0x00);
	if (tval < 0)
		return -EINVAL;
	mdelay(50);
	/*get old chksum*/
	tval = bq27441_read(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, true);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]get block data check sum err(%d)",
			tval);
		return tval;
	} else {
		old_chksum = tval;
		dev_info(di->dev, "[calibrate cc]get old_chksum success:%d\n",
			old_chksum);
	}

	/*calc new checksum*/
	tval = bq27441_read(di, 0x44, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]get old cc gain err(%d)",
			tval);
		return tval;
	} else {
		old_cc.cc_gain = tval;
		dev_info(di->dev, "[calibrate cc]old cc_gain1:0x%x\n",
			old_cc.cc_gain);
	}
	tval = bq27441_read(di, 0x46, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]get old cc gain2 err(%d)",
			tval);
		return tval;
	} else {
		old_cc.cc_gain = (old_cc.cc_gain << 16) | tval;
		old_chksum += (old_cc.cc_gain & 0xff) +
			((old_cc.cc_gain >> 8) & 0xff) +
			((old_cc.cc_gain >> 16) & 0xff) +
			((old_cc.cc_gain >> 24) & 0xff);
		dev_info(di->dev, "[calibrate cc]old cc_gain2:0x%x\n",
			old_cc.cc_gain);
	}

	tval = bq27441_read(di, 0x48, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]get old cc delta1 err(%d)",
			tval);
		return tval;
	} else {
		old_cc.cc_delta = tval;
		dev_info(di->dev, "[calibrate cc]old cc_delta1:0x%x\n",
			old_cc.cc_delta);
	}
	tval = bq27441_read(di, 0x4a, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]get old cc delta2 err(%d)",
			tval);
		return tval;
	} else {
		old_cc.cc_delta = (old_cc.cc_delta << 16) | tval;
		old_chksum += (old_cc.cc_delta & 0xff) +
			((old_cc.cc_delta >> 8) & 0xff) +
			((old_cc.cc_delta >> 16) & 0xff) +
			((old_cc.cc_delta >> 24) & 0xff);
		dev_info(di->dev, "[calibrate cc]old cc_delta2:0x%x\n",
			old_cc.cc_delta);
	}

	new_chksum = ((new_cc->cc_gain >> 8) & 0xFF) +
		((new_cc->cc_gain >> 16) & 0xFF) +
		((new_cc->cc_gain >> 24) & 0xFF) +
		(new_cc->cc_gain & 0xFF);
	new_chksum += ((new_cc->cc_delta >> 8) & 0xFF) +
		((new_cc->cc_delta >> 16) & 0xFF) +
		((new_cc->cc_delta >> 24) & 0xFF) +
		(new_cc->cc_delta & 0xFF);
	new_chksum = ~((~(old_chksum & 0xff) + new_chksum) & 0xff);
	dev_info(di->dev, "[calibrate cc] new_chksum : 0x%x !\n", new_chksum);

	/*cfg update*/
	tval = bq27441_write(di, 0x44, new_cc->cc_gain >> 16, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]write cc_gain1 err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x46, new_cc->cc_gain & 0xffff, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]write cc_gain2 err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x48, new_cc->cc_delta >> 16, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]write cc_delta1 err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x4a, new_cc->cc_delta & 0xffff, false);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]write cc_delta2 err(%d)",
			tval);
		return tval;
	}

	/*write new chksum*/
	tval = bq27441_write(di, 0x60, new_chksum, true);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate cc]write new_chksum err(%d)",
			tval);
		return tval;
	}
	mdelay(50);

	dev_info(di->dev, "[calibrate cc] success\n");

	return 0;
}

/*
  * config calibration for board offset.
  */
static int bq27441_battery_calibrate_offset(struct bq27441_device_info *di)
{
	struct bq27441_cfg_items *items = &di->items;
	struct bq27441_calibration_offset *new_offset = &items->offset;
	struct bq27441_calibration_offset old_offset;
	int tval;
	u8 old_chksum = 0;
	u8 new_chksum = 0;

	if (new_offset->board_offset == -1)
		return -EINVAL;

	/*select subclass calibration data*/
	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_DATA, 0x00);
	if (tval < 0)
		return -EINVAL;
	mdelay(50);
	/*get old chksum*/
	tval = bq27441_read(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, true);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate offset]get block data check sum err(%d)",
			tval);
		return tval;
	} else {
		old_chksum = tval;
		dev_info(di->dev, "[calibrate offset]get old_chksum success:%d\n",
			old_chksum);
	}

	/*calc new checksum*/
	tval = bq27441_read(di, 0x40, true);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate offset]get dsg_cur err(%d)",
			tval);
		return tval;
	} else {
		old_offset.board_offset = tval;
		old_chksum += (old_offset.board_offset & 0xff);
		dev_info(di->dev, "[calibrate offset]get board offset success:%d\n",
			old_offset.board_offset);
	}

	new_chksum = new_offset->board_offset & 0xFF;
	new_chksum = ~((~(old_chksum & 0xff) + new_chksum) & 0xff);
	dev_info(di->dev, "[calibrate offset] new_chksum : 0x%x !\n", new_chksum);

	/*cfg update*/
	tval = bq27441_write(di, 0x40, new_offset->board_offset, true);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate offset]write board_offset err(%d)",
			tval);
		return tval;
	}

	/*write new chksum*/
	tval = bq27441_write(di, 0x60, new_chksum, true);
	if (tval < 0) {
		dev_err(di->dev, "[calibrate offset]write new_chksum err(%d)",
			tval);
		return tval;
	}
	mdelay(50);

	dev_info(di->dev, "[calibrate offset]cfgupdate success\n");

	return 0;
}

/*
  * config resistor table
  */
static int bq27441_battery_cfgupdate_r_aram(struct bq27441_device_info *di)
{

	struct bq27441_cfg_items *items = &di->items;
	u16 old_rtable[15];
	int index;
	int addr;
	int tval;
	u8 old_chksum = 0;
	u8 new_chksum = 0;

	/*select subclass current thresholds*/
	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_RARAM, 0x00);
	if (tval < 0)
		return -EINVAL;

	mdelay(50);

	/*get old chksum*/
	tval = bq27441_read(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg rtable]get block data check sum err(%d)",
			tval);
		return tval;
	} else {
		old_chksum = tval;
		dev_info(di->dev, "[cfg rtable]get old_chksum success:0x%x\n",
			old_chksum);
	}

	/*calc new checksum*/
	for (index = 0, addr = 0x40; index < 15; index++, addr += 2) {
		tval = bq27441_read(di, addr, false);
		if (tval < 0) {
			dev_err(di->dev, "[cfg rtable]get rtable[%d[ err, tval:%d\n",
				index, tval);
			return tval;
		} else {
			old_rtable[index] = tval & 0xffff;
			old_chksum += (old_rtable[index] & 0xff) +
				((old_rtable[index] >> 8) & 0xff);
			dev_info(di->dev, "[cfg rtable]old_rtable[%d]:%d\n",
				index, old_rtable[index]);
		}
	}

	for (index = 0, new_chksum = 0; index < 15; index++)
		new_chksum += ((items->r_table[index] >> 8) & 0xFF) +
			(items->r_table[index] & 0xFF);

	new_chksum = ~((~(old_chksum & 0xff) + new_chksum) & 0xff);
	dev_info(di->dev, "[cfg rtable] new_chksum : 0x%x !\n", new_chksum);

	/*cfg update*/
	for (index = 0, addr = 0x40; index < 15; index++, addr += 0x2) {
		mdelay(100);
		tval = bq27441_write(di, addr, items->r_table[index], false);
		if (tval < 0) {
			dev_err(di->dev, "[cfg rtable]write rtable[%d] err(%d)",
				index, tval);
			return tval;
		}
	}

	/*write new chksum*/
	tval = bq27441_write(di, 0x60, new_chksum, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg rtable]write new_chksum err, tval:%d\n",
			tval);
		return tval;
	}
	mdelay(50);

	dev_info(di->dev, "[cfg rtable]update_r_aram success\n");

	return 0;
}

/*
  * config current threshold
  */
static int bq27441_battery_cfgupdate_curthresh(struct bq27441_device_info *di)
{
	struct bq27441_cfg_items *items = &di->items;
	struct bq27441_sc_cur_thresholds *new_cur_threshods = &items->cur_thresholds;
	struct bq27441_sc_cur_thresholds old_cur_threshods;
	int tval;
	u8 old_chksum = 0;
	u8 new_chksum = 0;

	/*select subclass current thresholds*/
	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_CURTHRESH, 0x00);
	if (tval < 0)
		return -EINVAL;
	mdelay(50);
	/*get old chksum*/
	tval = bq27441_read(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]get block data check sum err(%d)",
			tval);
		return tval;
	} else {
		old_chksum = tval;
		dev_info(di->dev, "[cfg curthresh]get old_chksum success:%d\n",
			old_chksum);
	}

	/*calc new checksum*/
	tval = bq27441_read(di, 0x40, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]get dsg_cur err(%d)",
			tval);
		return tval;
	} else {
		old_cur_threshods.dsg_cur = tval;
		old_chksum += (old_cur_threshods.dsg_cur & 0xff) +
			((old_cur_threshods.dsg_cur >> 8) & 0xff);
		dev_info(di->dev, "[cfg curthresh]get dsg_cur success dsg_cur:%d\n",
			old_cur_threshods.dsg_cur);
	}
	tval = bq27441_read(di, 0x42, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]get chg_cur err, tval:%d\n",
			tval);
		return tval;
	} else {
		old_cur_threshods.chg_cur = tval;
		old_chksum += (old_cur_threshods.chg_cur & 0xff) +
			((old_cur_threshods.chg_cur >> 8) & 0xff);
		dev_info(di->dev, "[cfg curthresh]chg_cur:%d\n",
			old_cur_threshods.chg_cur);
	}
	tval = bq27441_read(di, 0x44, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]get quit_cur err(%d)",
			tval);
		return tval;
	} else {
		old_cur_threshods.quit_cur = tval;
		old_chksum = (old_cur_threshods.quit_cur & 0xff) +
			((old_cur_threshods.quit_cur >> 8) & 0xff);
		dev_info(di->dev, "[cfg curthresh]quit_cur:%d\n",
			old_cur_threshods.quit_cur);
	}

	new_chksum = ((new_cur_threshods->dsg_cur >> 8) & 0xFF) +
		(new_cur_threshods->dsg_cur & 0xFF);
	new_chksum += ((new_cur_threshods->chg_cur >> 8) & 0xFF) +
		(new_cur_threshods->chg_cur & 0xFF);
	new_chksum += ((new_cur_threshods->quit_cur >> 8) & 0xFF) +
		(new_cur_threshods->quit_cur & 0xFF);
	new_chksum = ~((~(old_chksum & 0xff) + new_chksum) & 0xff);
	dev_info(di->dev, "[cfg curthresh] new_chksum : 0x%x !\n", new_chksum);

	/*cfg update*/
	tval = bq27441_write(di, 0x40, new_cur_threshods->dsg_cur, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]write dsg_cur err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x42, new_cur_threshods->chg_cur, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]write chg_cur err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x44, new_cur_threshods->quit_cur, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]write quit_cur err(%d)",
			tval);
		return tval;
	}

	/*write new chksum*/
	tval = bq27441_write(di, 0x60, new_chksum, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg curthresh]write new_chksum err(%d)",
			tval);
		return tval;
	}
	mdelay(50);

	dev_info(di->dev, "[cfg curthresh]cfgupdate_curthresh success\n");

	return 0;
}
static int bq27441_battery_cfgupdate_state(struct bq27441_device_info *di)
{
	struct bq27441_cfg_items *items = &di->items;
	struct bq27441_sc_state *new_state = &items->state;
	struct bq27441_sc_state old_state;
	int tval;
	u8 old_chksum = 0;
	u8 new_chksum = 0;

	/**************************************
	* select state subclass , index 0
	*************************************/
	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_STATE, 0x00);
	if (tval < 0)
		return -EINVAL;
	mdelay(50);

	tval = bq27441_read(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get block data check sum err(%d)",
			tval);
		return tval;
	} else {
		old_chksum = tval;
		dev_info(di->dev, "[cfg state]get old_chksum success:0x%x\n",
			old_chksum);
	}

	/*calc new checksum*/
	tval = bq27441_read(di, 0x40, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get qmax err(%d)",
			tval);
		return tval;
	} else {
		old_state.qmax_cell = tval;
		old_chksum +=  (old_state.qmax_cell & 0xff) +
			((old_state.qmax_cell >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]qmax:%d\n",
			old_state.qmax_cell);
	}

	/*tval = bq27441_read(di, 0x43, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get reserve_cap err, tval:%d\n", tval);
		return tval;
	} else {
		old_state.reserve_cap = tval;
		old_chksum += (old_state.reserve_cap & 0xff) +
			((old_state.reserve_cap >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]reserve_cap:%d\n",
			old_state.reserve_cap);
	}*/

	tval = bq27441_read(di, 0x45, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get load_sle err(%d)",
			tval);
		return tval;
	} else {
		old_state.load_sel = tval;
		old_chksum += old_state.load_sel & 0xff;
		dev_info(di->dev, "[cfg state]load_sel:%d\n",
			old_state.load_sel);
	}

	tval = bq27441_read(di, 0x4a, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get design_capacity err(%d)",
			tval);
		return tval;
	} else {
		old_state.design_capacity = tval;
		old_chksum += (old_state.design_capacity & 0xff) +
			((old_state.design_capacity >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]design_capacity:%d\n",
			old_state.design_capacity);
	}

	tval = bq27441_read(di, 0x4c, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get design_energy err(%d)",
			tval);
		return tval;
	} else {
		old_state.design_energy = tval;
		old_chksum += (old_state.design_energy & 0xff) +
			((old_state.design_energy >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]design_energy:%d\n",
			old_state.design_energy);
	}

	tval = bq27441_read(di, 0x50, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get term_vol err(%d)",
			tval);
		return tval;
	} else {
		old_state.term_vol = tval;
		old_chksum += (old_state.term_vol & 0xff) +
			((old_state.term_vol >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]term_vol:%d\n",
			old_state.term_vol);
	}

	/*tval = bq27441_read(di, 0x52, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get term_vdatle err(%d)",
			tval);
		return tval;
	} else {
		old_state.term_vdatle = tval;
		old_chksum += (old_state.term_vdatle & 0xff) +
			((old_state.term_vdatle >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]term_vdatle:%d\n",
			old_state.term_vdatle);
	}*/

	tval = bq27441_read(di, 0x5b, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get taper_rate err(%d)",
			tval);
		return tval;
	} else {
		old_state.taper_rate = tval;
		old_chksum += (old_state.taper_rate & 0xff) +
			((old_state.taper_rate >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]taper_rate:%d\n",
			old_state.taper_rate);
	}

	tval = bq27441_read(di, 0x5d, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get taper_vol err(%d)",
			tval);
		return tval;
	} else {
		old_state.taper_vol = tval;
		old_chksum += (old_state.taper_vol & 0xff) +
			((old_state.taper_vol >> 8) & 0xff);
		dev_info(di->dev, "[cfg state]taper_vol:%d\n",
			old_state.taper_vol);
	}

	new_chksum = ((new_state->qmax_cell >> 8) & 0xFF) +
		(new_state->qmax_cell & 0xFF);
	/*new_chksum += ((new_state->reserve_cap >> 8) & 0xff) +
		(new_state->reserve_cap & 0xFF);*/
	new_chksum += new_state->load_sel & 0xff;
	new_chksum += ((new_state->design_capacity >> 8) & 0xFF) +
		(new_state->design_capacity & 0xFF);
	new_chksum += ((new_state->design_energy >> 8) & 0xFF) +
		(new_state->design_energy & 0xFF);
	new_chksum += ((new_state->term_vol >> 8) & 0xFF) +
		(new_state->term_vol & 0xFF);
	/*new_chksum += new_state->term_vdatle & 0xff;*/
	new_chksum += ((new_state->taper_rate >> 8) & 0xFF) +
		(new_state->taper_rate & 0xFF);
	new_chksum += ((new_state->taper_vol >> 8) & 0xFF) +
		(new_state->taper_vol & 0xFF);

	new_chksum = ~((~(old_chksum & 0xff) + new_chksum) & 0xff);
	dev_info(di->dev, "[cfg state] new_chksum : 0x%x !\n", new_chksum);

	/*cfg update*/
	/*select state subclass*/
	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_STATE, 0x00);
	if (tval < 0)
		return -EINVAL;
	mdelay(50);
	tval = bq27441_write(di, 0x40, new_state->qmax_cell, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write qmax_cell err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x45, new_state->load_sel, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write load_sel err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x4a, new_state->design_capacity, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write design_capacity err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x4c, new_state->design_energy, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write design_energy err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x50, new_state->term_vol, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write term_vol err(%d)",
			tval);
		return tval;
	}
	/*tval = bq27441_write(di, 0x52, new_state->term_vdatle, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write term_vdatle err(%d)",
			tval);
		return tval;
	}*/
	tval = bq27441_write(di, 0x5b, new_state->taper_rate, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write taper_rate err(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, 0x5d, new_state->taper_vol, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write taper_vol err(%d)",
			tval);
		return tval;
	}

	/*tval = bq27441_write(di, 0x43, new_state->reserve_cap, false);
		if (tval < 0) {
			dev_err(di->dev, "[cfg state]write reserve_cap err(%d)",
				tval);
			return tval;
		}*/
	/*write new chksum*/
	tval = bq27441_write(di, 0x60, new_chksum, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write new_chksum err(%d)",
			tval);
		return tval;
	}
	mdelay(50);

	/**************************************
	* select state subclass , index 1
	*************************************/
	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_STATE, 0x01);
	if (tval < 0)
		return -EINVAL;
	mdelay(50);
	tval = bq27441_read(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get block data check sum err(%d)",
			tval);
		return tval;
	} else {
		old_chksum = tval;
		dev_info(di->dev, "[cfg state]old_chksum:0x%x\n", old_chksum);
	}
	tval = bq27441_read(di, 0x40, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]get sleep_current err(%d)",
			tval);
		return tval;
	} else {
		old_state.sleep_current = tval;
		old_chksum += old_state.sleep_current & 0xff;
		dev_info(di->dev, "[cfg state]old sleep_current:%d\n",
			old_state.sleep_current);
	}

	new_chksum = new_state->sleep_current & 0xFF;
	new_chksum = ~((~(old_chksum & 0xff) + new_chksum) & 0xff);
	 /*cfg update*/
	tval = bq27441_write(di, 0x40, new_state->sleep_current, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write sleep_current err(%d)",
			tval);
		return tval;
	}
	/*write new chksum*/
	tval = bq27441_write(di, 0x60, new_chksum, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg state]write new_chksum err(%d)",
			tval);
		return tval;
	}
	mdelay(50);
	dev_info(di->dev, "[cfg state]cfgupdate_state success\n");

	return 0;
}
/*
  * config charge terminal
  */
static int bq27441_battery_cfgupdate_chterm(struct bq27441_device_info *di)
{
	int tval;
	u8 old_chksum = 0;
	u8 new_chksum = 0;

	tval = bq27441_battery_select_subclass(di, BQ27441_DM_SC_CHTERM, 0x00);
	if (tval < 0)
		return -EINVAL;

	mdelay(50);

	old_chksum = bq27441_read(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, true);
	if (old_chksum < 0) {
		dev_err(di->dev, "[cfg chterm]old_chksum:%d\n", old_chksum);
		return tval;
	}

	tval = bq27441_read(di, 0x47, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg chterm]get DODatEOC Delta T err(%d)",
			tval);
		return tval;
	} else {
		old_chksum += ((tval >> 8) & 0xff) + (tval & 0xff);
		dev_info(di->dev, "[cfg chterm]old DODatEOC Delta T (%d)\n", tval);
	}

	new_chksum = (TI_CONFIG_DODEOC_DELTAT & 0xFF) +
		((TI_CONFIG_DODEOC_DELTAT >> 8) & 0xFF);
	new_chksum = ~((~(old_chksum & 0xff) + new_chksum) & 0xff);

	/* set DODatEOC Delta T*/
	tval = bq27441_write(di, 0x47, TI_CONFIG_DODEOC_DELTAT, false);
	if (tval) {
		dev_err(di->dev, "[cfg chterm] set DODatEOC Delta T failed(%d)",
			tval);
		return tval;
	}
	tval = bq27441_write(di, BQ27441_EXT_REG_BLOCKDATACHKSUM, new_chksum, true);
	if (tval) {
		dev_err(di->dev, "[cfg chterm] set data chk sum err(%d)",
			tval);
		return tval;
	}

	dev_info(di->dev, "[cfg chterm]cfgupdate_chterm success\n");

	return 0;
}

static int bq27441_battery_get_pwron_state(struct bq27441_device_info *di)
{
	int tval = 0;

	/*check sealed/unsealed mode*/
	tval = bq27441_read(di, BQ27441_REG_FLAGS, false);
	if (tval < 0) {
		dev_err(di->dev, "[pwron state]read BQ27441_REG_FLAGS err(%d)",
			tval);
		return tval;
	}
	if (!(tval & BQ27441_FLAG_ITPOR)) {
		dev_info(di->dev, "[pwron state]don't need updata config,"
			"flags:0x%x\n", tval);
		return 0;
	} else
		return 1;
}

static int bq27441_battery_force_reset(struct bq27441_device_info *di)
{
	int tval;
	tval = bq27441_set_subcmd(di, RESET);
	if (tval < 0) {
		dev_err(di->dev, "[cfg update]send RESET subcmd err(%d)",
			tval);
		return tval;
	}
	dev_info(di->dev, "%s success\n", __func__);
	return 0;
}

/*
  * config parameters for gauge by writing into data memory.
  * following steps:
  * 1.set unsealed mode;
  * 2.set config update mode;
  * 3.config update;
  * 4.config finished;
  * 5.set sealed.
  */
static int bq27441_battery_update_config(struct bq27441_device_info *di)
{
	int tval = 0;

	/*if sealed, set unsealed*/
	if (bq27441_battery_set_unsealed(di))
		return -EINVAL;

	/*set cfgupdate mode*/
	tval = bq27441_set_subcmd(di, SET_CFGUPDATE);
	if (tval < 0) {
		dev_err(di->dev, "[cfg update]send SET_CFGUPDATE subcmd err(%d)",
			tval);
		return tval;
	}

	msleep(1200);
	tval = bq27441_read(di, BQ27441_REG_FLAGS, false);
	if (tval < 0) {
		dev_err(di->dev, "[cfg update]read flags err(%d)",
			tval);
		return tval;
	}
	if (tval & BQ27441_FLAG_CFGUPMODE)
		dev_info(di->dev, "[cfg update] set cfgupdate success\n");

	bq27441_battery_cfgupdate_chterm(di);
	bq27441_battery_cfgupdate_state(di);
	bq27441_battery_cfgupdate_curthresh(di);
	bq27441_battery_cfgupdate_r_aram(di);
	bq27441_battery_calibrate_offset(di);
	bq27441_battery_calibrate_cc(di);
	bq27441_battery_cfgupdate_finished(di);

	return 0;
}

static int bq27441_battery_check_id(struct bq27441_device_info *di)
{
	int tval;

	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD)
		return -ENODEV;

	if (bq27441_battery_get_device_type(di) != BQ27441_DEVTYPE) {
		dev_err(di->dev, "[chk id] get dev type err!\n");
		return -EINVAL;
	} else {
		tval = bq27441_battery_get_dm_code(di);
		if (tval == BQ27441_DMCODE_G1A) {
			di->id = BQ27441_G1A;
		} else if (tval == BQ27441_DMCODE_G1B) {
			di->id = BQ27441_G1B;
		} else {
			dev_err(di->dev, "[init work] get dm code err!\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void bq27441_bat_init_work(struct work_struct *work)
{
	struct bq27441_device_info *di =
		container_of(work, struct bq27441_device_info, init_work.work);
	int ret;

	di->cache.health = bq27441_battery_read_health(di);
	if (di->cache.health == POWER_SUPPLY_HEALTH_DEAD)
		goto dead;

	ret = bq27441_battery_get_pwron_state(di);
	if (ret > 0) {
		if (bq27441_battery_update_config(di) < 0)
			return;
	}
dead:
	schedule_delayed_work(&di->work, 0 * HZ);

}

static int bq27441_powersupply_init(struct bq27441_device_info *di)
{
	int ret;

	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = bq27441_battery_props;
	di->bat.num_properties = ARRAY_SIZE(bq27441_battery_props);
	di->bat.get_property = bq27441_battery_get_property;

	INIT_DELAYED_WORK(&di->init_work, bq27441_bat_init_work);
	INIT_DELAYED_WORK(&di->work, bq27441_battery_poll);
	mutex_init(&di->lock);

	ret = power_supply_register(di->dev, &di->bat);
	if (ret) {
		dev_err(di->dev, "failed to register battery: %d\n", ret);
		return ret;
	}

	bq27441_battery_check_id(di);
	bq27441_battery_cfg_init(di);
	schedule_delayed_work(&di->init_work, 0 * HZ);

	dev_info(di->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	return 0;
}

static void bq27441_battery_cfg_init(struct bq27441_device_info *di)
{
	struct device_node *node = di->dev->of_node;
	struct bq27441_cfg_items *items = &di->items;
	struct bq27441_sc_state *state = &items->state;
	struct bq27441_sc_cur_thresholds *cur_thresholds = &items->cur_thresholds;
	struct bq27441_calibration_offset *offset = &items->offset;
	struct bq27441_calibration_cc	*cc = &items->cc;
	int ret;
	int i;

	/*resistor_table*/
	items->r_table = (u32 *)r_table;
	ret = of_property_read_u32_array(node, "r_table",
		(u32 *)r_table, ARRAY_SIZE(r_table));
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]r_table success!\n");
		for (i = 0; i < 15; i++)
			dev_info(di->dev, "[cfg init] rtable[%d]:%d\n",
				i, r_table[i]);
	} else {
		dev_err(di->dev, "[cfg init]r_table fail, default!\n");
	}
	/*rsense*/
	ret = of_property_read_u32_array(node, "rsense", &items->rsense, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]rsense(%d) success!\n",
			items->rsense);
	} else {
		items->rsense = TI_CONFIG_RSENSE;
		dev_err(di->dev, "[cfg init]rsense not found,default(%d)!\n",
			 items->rsense);
	}
	/*taper_cur*/
	ret = of_property_read_u32_array(node, "taper_cur", &items->taper_cur, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]qmax_cell(%d) success!\n",
			items->taper_cur);
	} else {
		items->taper_cur = TI_CONFIG_TAPCUR;
		dev_err(di->dev, "[cfg init]taper_cur not found,default(%d)!\n",
			 items->taper_cur);
	}
	/*
		    * STATE
		    */
	/*qmax_cell*/
	ret = of_property_read_u32_array(node, "qmax_cell", &state->qmax_cell, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]qmax_cell(%d) success!\n",
		       state->qmax_cell);
	} else {
		state->qmax_cell = TI_CONFIG_QMAXCELL;
		dev_err(di->dev, "[cfg init]qmax_cell not found,default(%d)!\n",
			state->qmax_cell);
	}
	/*load_sel*/
	ret = of_property_read_u32_array(node, "load_sel", &state->load_sel, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]load_sel(%d) success!\n",
			state->load_sel);
	} else {
		state->load_sel = TI_CONFIG_LOADSEL;
		dev_info(di->dev, "[cfg init]load_sel not found,default(%d)!\n",
			state->load_sel);
	}
	/*design_capacity*/
	ret = of_property_read_u32_array(node, "design_capacity", &state->design_capacity, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]design_capacity(%d) success!\n",
			state->design_capacity);
	} else {
		state->design_capacity = TI_CONFIG_CAP;
		dev_err(di->dev, "[cfg init]design_capacity not found,default(%d)!\n",
			state->design_capacity);
	}
	/*desig_energy*/
	ret = of_property_read_u32_array(node, "design_energy", &state->design_energy, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]design_energy(%d) success!\n",
			state->design_energy);
	} else {
		if (di->id == BQ27441_DMCODE_G1A)
			state->design_energy = TI_CONFIG_CAP * 3700 / 1000;
		if (di->id == BQ27441_DMCODE_G1B)
			state->design_energy = TI_CONFIG_CAP * 3800 / 1000;
		dev_info(di->dev, "[cfg init]design_energy not found,default(%d)!\n",
			state->design_energy);
	}
	/*term_vol*/
	ret = of_property_read_u32_array(node, "term_vol", &state->term_vol, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]term_vol(%d) success!\n",
			state->term_vol);
	} else {
		state->term_vol = TI_CONFIG_TERMVOL;
		dev_err(di->dev, "[cfg init]design_capacity not found,default(%d)!\n",
			state->term_vol);
	}
	/*taper_rate*/
	state->taper_rate = (state->design_capacity * 10) / items->taper_cur;
	/*term_vdatle*/
	ret = of_property_read_u32_array(node, "term_vdatle", &state->term_vdatle, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]term_vdatle(%d) success!\n",
			state->term_vdatle);
	} else {
		state->term_vdatle = TI_CONFIG_TERMVDATLE;
		dev_info(di->dev, "[cfg init]term_vdatle not found,default(%d)!\n",
			state->term_vdatle);
	}
	/*taper_vol*/
	ret = of_property_read_u32_array(node, "taper_vol", &state->taper_vol, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]taper_vol(%d) success!\n",
			 state->taper_vol);
	} else {
		if (di->id == BQ27441_DMCODE_G1A)
			state->taper_vol = TI_CONFIG_TAPERVOL;
		if (di->id == BQ27441_DMCODE_G1B)
			state->taper_vol = TI_CONFIG_TAPERVOL + 100;
		dev_info(di->dev, "[cfg init]taper_vol not found,default(%d)!\n",
			state->taper_vol);
	}
	/*sleep_current*/
	ret = of_property_read_u32_array(node, "sleep_current", &state->sleep_current, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]sleep_current(%d) success!\n",
			 state->sleep_current);
	} else {
		state->sleep_current = TI_CONFIG_SLEEPCUR;
		dev_info(di->dev, "[cfg init]sleep_current not found,default(%d)!\n",
			state->sleep_current);
	}
	 /*reserve_cap*/
	ret = of_property_read_u32_array(node, "reserve_cap", &state->reserve_cap, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]reserve_cap(%d) success!\n",
			 state->reserve_cap);
	} else {
		state->reserve_cap = TI_CONFIG_RESERVEDCAP;
		dev_info(di->dev, "[cfg init]reserve_cap not found,default(%d)!\n",
			state->reserve_cap);
	}
	/*
	 * CUR THRESHOLD
	 */
	/*dsg_cur*/
	ret = of_property_read_u32_array(node, "dsg_cur", &cur_thresholds->dsg_cur, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]dsg_cur(%d) success!\n",
			 cur_thresholds->dsg_cur);
	} else {
		cur_thresholds->dsg_cur = TI_CONFIG_DSGCUR;
		dev_info(di->dev, "[cfg init]dsg_cur not found,default(%d)!\n",
			cur_thresholds->dsg_cur);
	}
	/*chg_cur*/
	ret = of_property_read_u32_array(node, "chg_cur", &cur_thresholds->chg_cur, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]chg_cur(%d) success!\n",
			 cur_thresholds->chg_cur);
	} else {
		cur_thresholds->chg_cur = TI_CONFIG_CHGCUR;
		dev_info(di->dev, "[cfg init]chg_cur not found,default(%d)!\n",
			cur_thresholds->chg_cur);
	}
	/*quit_cur*/
	ret = of_property_read_u32_array(node, "quit_cur", &cur_thresholds->chg_cur, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]quit_cur(%d) success!\n",
			 cur_thresholds->quit_cur);
	} else {
		cur_thresholds->quit_cur = TI_CONFIG_QUITCUR;
		dev_info(di->dev, "[cfg init]quit_cur not found,default(%d)!\n",
			cur_thresholds->quit_cur);
	}
	/*
	 * CALIBRATION
	 */
	 /*board offset*/
	ret = of_property_read_u32_array(node, "board_offset", &offset->board_offset, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]board_offset(0x%x) success!\n",
			 offset->board_offset);
	} else {
		offset->board_offset = -1;
		dev_info(di->dev, "[cfg init]board_offset not found,default(%d)!\n",
			offset->board_offset);
	}
	 /*cc gain*/
	ret = of_property_read_u32_array(node, "cc_gain", &cc->cc_gain, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]cc_gain(0x%x) success!\n",
			 cc->cc_gain);
	} else {
		cc->cc_gain = -1;
		dev_info(di->dev, "[cfg init]cc_gain not found,default(%d)!\n",
			cc->cc_gain);
	}
	/*cc delta*/
	ret = of_property_read_u32_array(node, "cc_delta", &cc->cc_delta, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]cc_delta(0x%x) success!\n",
			 cc->cc_delta);
	} else {
		cc->cc_delta = -1;
		dev_info(di->dev, "[cfg init]cc_delta not found,default(%d)!\n",
			cc->cc_delta);
	}
	/*
	 * OTHERWISE
	 */
	ret = of_property_read_u32_array(node, "log_switch", &items->log_switch, 1);
	if (ret == 0) {
		dev_info(di->dev, "[cfg init]log_switch(%d) success!\n",
			 items->log_switch);
	} else {
		items->log_switch = 0;
		dev_info(di->dev, "[cfg init]log_switch not found,default(%d)!\n",
			items->log_switch);
	}

}

static unsigned	 long get_sys_tick_ms(void)
{

	struct timeval current_tick;
	long tick;

	do_gettimeofday(&current_tick);
	tick = current_tick.tv_sec * 1000 + current_tick.tv_usec/1000;

	return tick;
}

static int bq27441_read_i2c(struct bq27441_device_info *di, u8 reg,
	bool single, bool le)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg[2];
	u8 data[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;
	if (single)
		msg[1].len = 1;
	else
		msg[1].len = 2;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	if (!single) {
		if (le)
			ret = data[0] | (data[1] << 8);
		else
			ret = (data[0] << 8) | data[1];
	} else
		ret = data[0];

	return ret;
}

static int bq27441_write_i2c(struct bq27441_device_info *di,
	u8 reg, u16 value, bool single, bool le)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg;
	u8 data[3];
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags;
	data[0] = reg;
	if (single) {
		data[1] = value & 0xff;
		msg.len = 2;
	} else {
		if (le) {
			data[1] = value & 0xff;
			data[2] = (value >> 8) & 0xff;
		} else {
			data[1] = (value >> 8) & 0xff;
			data[2] = value & 0xff;
		}
		msg.len = 3;
	}

	msg.buf = data;
	ret = i2c_transfer(client->adapter, &msg, 1);

	return ret;
}

static ssize_t show_datamem(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return 0;
}
/*echo id,index > datamem*/
static ssize_t store_datamem(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);
	u8 subclass;
	u8 index;
	int addr;
	int tval;

	char *p = (char *)buf;

	tval = kstrtou8(strsep(&p, ","), 0, &subclass);
	if (tval)
		return count;

	tval = kstrtou8(p, 0, &index);
	if (tval)
		return count;


	tval = bq27441_battery_set_unsealed(di);
	if (tval < 0)
		return count;

	tval = bq27441_write(di, BQ27441_EXT_REG_BLOCKDATACTL, 0x00, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg chterm]enable data memory access err(%d)",
			tval);
		return count;
	}
	tval = bq27441_write(di, BQ27441_EXT_REG_DATACLASS, subclass, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg chterm]set data memory subclass id err(%d)",
			tval);
		return count;
	}
	tval = bq27441_write(di, BQ27441_EXT_REG_DATABLOCK, index, true);
	if (tval < 0) {
		dev_err(di->dev, "[cfg chterm]set index of data block err(%d)",
			tval);
		return count;
	}

	mdelay(50);

	for (addr = 0x40; addr <= 0x5f; addr++) {
		tval = bq27441_read(di, addr, true);
		printk("%d : 0x%x\n", addr - 0x40, tval);
	}

	return count;
}

static ssize_t show_cfgupdate(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_cfgupdate(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	bq27441_battery_update_config(di);

	return count;
}


static ssize_t show_reg(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_reg(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);
	u8 addr;
	int tval;

	tval = kstrtou8(buf, 0, &addr);
	if (tval)
		return count;

	tval = bq27441_read(di, addr, false);
	dev_info(di->dev, "0x%x : 0x%x\n", addr, tval);

	return count;
}

static ssize_t show_sealed(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_sealed(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);
	u16 sealed;
	int tval;

	tval = kstrtou16(buf, 0, &sealed);
	if (tval)
		return tval;

	if (sealed)
		bq27441_battery_set_sealed(di);
	else
		bq27441_battery_set_unsealed(di);

	return count;
}

static ssize_t show_reset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_reset(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	bq27441_battery_set_unsealed(di);
	bq27441_battery_force_reset(di);

	return count;
}


static ssize_t show_subcmd(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	bq27441_battery_get_control_status(di);
	bq27441_battery_get_device_type(di);
	bq27441_battery_get_fw_version(di);
	bq27441_battery_get_dm_code(di);
	bq27441_battery_get_prev_macwrite(di);
	bq27441_battery_get_chem_id(di);

	return 0;
}

static ssize_t store_subcmd(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);
	u16 subcmd;
	int tval;

	tval = kstrtou16(buf, 0, &subcmd);
	if (tval)
		return tval;

	tval = bq27441_set_subcmd(di, subcmd);
	if (tval < 0)
		return tval;
	tval = bq27441_read(di, BQ27441_REG_CONTROL, false);
	if (tval < 0)
		return tval;

	dev_info(di->dev, "0x%x : 0x%x\n", subcmd, tval);

	return tval ? tval : count;
}

static ssize_t show_dump_regs(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);
	int addr = BQ27441_REG_CONTROL;
	int tval;

	for (; addr <= BQ27441_REG_SOH; addr += 0x2) {
		tval = bq27441_read(di, addr, false);
		dev_info(di->dev, "0x%x:0x%x\n", addr, tval);
	}

	return 0;
}
static ssize_t store_dump_regs(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t show_dump(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	dev_info(di->dev, "temperature:%d\n", di->cache.temperature);
	dev_info(di->dev, "charge full:%d\n", di->cache.charge_full);
	dev_info(di->dev, "flags:%d\n", di->cache.flags);
	dev_info(di->dev, "health:%d\n", di->cache.health);
	dev_info(di->dev, "power avg:%d\n", di->cache.power_avg);
	dev_info(di->dev, "capacity:%d\n", di->cache.capacity);

	return 0;
}
static ssize_t store_dump(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t show_log_switch(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", di->items.log_switch);
}

static ssize_t store_log_switch(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	int ret = kstrtoint(buf, 0, &di->items.log_switch);

	return ret ? ret : count;
}

static ssize_t show_interval(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", di->poll_interval);
}

static ssize_t store_interval(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	int ret = kstrtoint(buf, 0, &di->poll_interval);

	return ret ? ret : count;
}

static struct device_attribute bq27441_bat_attrs[] = {
	__ATTR(log_switch, S_IRUGO | S_IWUSR, show_log_switch, store_log_switch),
	__ATTR(dump, S_IRUGO | S_IWUSR, show_dump, store_dump),
	__ATTR(subcmd, S_IRUGO | S_IWUSR, show_subcmd, store_subcmd),
	__ATTR(dump_regs, S_IRUGO | S_IWUSR, show_dump_regs, store_dump_regs),
	__ATTR(reg, S_IRUGO | S_IWUSR, show_reg, store_reg),
	__ATTR(datamem, S_IRUGO | S_IWUSR, show_datamem, store_datamem),
	__ATTR(cfgupdate, S_IRUGO | S_IWUSR, show_cfgupdate, store_cfgupdate),
	__ATTR(sealed, S_IRUGO | S_IWUSR, show_sealed, store_sealed),
	__ATTR(reset, S_IRUGO | S_IWUSR, show_reset, store_reset),
	__ATTR(interval, S_IRUGO | S_IWUSR, show_interval, store_interval),

};

static int bq27441_battery_create_sysfs(struct device *dev)
{
	int r, t;

	for (t = 0; t < ARRAY_SIZE(bq27441_bat_attrs); t++) {
		r = device_create_file(dev, &bq27441_bat_attrs[t]);

		if (r) {
			dev_err(dev, "failed to create sysfs file\n");
			return r;
		}
	}

	return 0;
}

static void bq27441_battery_remove_sysfs(struct device *dev)
{
	int  t;

	for (t = 0; t < ARRAY_SIZE(bq27441_bat_attrs); t++)
		device_remove_file(dev, &bq27441_bat_attrs[t]);
}

static int bq27441_battery_pm_notify(struct notifier_block *nb,
	unsigned long event, void *dummy)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		pr_info("[%s] : PM_SUSPEND_PREPARE\n", __func__);
		cancel_delayed_work_sync(&di->init_work);
		cancel_delayed_work_sync(&di->work);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block bq27441_bat_pm_notifier = {
	.notifier_call = bq27441_battery_pm_notify,
};

static int bq27441_battery_halt_notify(struct notifier_block *nb,
		unsigned long event, void *buf)
{
	switch (event) {
	case SYS_RESTART:
	case SYS_HALT:
	case SYS_POWER_OFF:
		pr_info("[%s] enter\n", __func__);
		cancel_delayed_work_sync(&di->init_work);
		cancel_delayed_work_sync(&di->work);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}


static struct notifier_block bq27441_bat_halt_notifier = {
	.notifier_call = bq27441_battery_halt_notify,
};


static int bq27441_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int retval = 0;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		return retval;
	}

	di->dev = &client->dev;
	di->bat.name = "battery";
	di->poll_interval = 5;
	di->first_detect = true;
	di->bus.read = &bq27441_read_i2c;
	di->bus.write = &bq27441_write_i2c;

	retval = bq27441_battery_create_sysfs(di->dev);
	if (retval)
		return retval;

	register_pm_notifier(&bq27441_bat_pm_notifier);
	register_reboot_notifier(&bq27441_bat_halt_notifier);

	retval = bq27441_powersupply_init(di);
	if (retval)
		goto fail;

	i2c_set_clientdata(client, di);
	return 0;

fail:
	bq27441_battery_remove_sysfs(di->dev);
	return retval;

}

static int bq27441_battery_remove(struct i2c_client *client)
{
	struct bq27441_device_info *di = i2c_get_clientdata(client);

	bq27441_battery_remove_sysfs(di->dev);
	cancel_delayed_work_sync(&di->init_work);
	cancel_delayed_work_sync(&di->work);
	power_supply_unregister(&di->bat);
	mutex_destroy(&di->lock);

	return 0;
}

static void bq27441_battery_shutdown(struct i2c_client *client)
{

	struct bq27441_device_info *di = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&di->init_work);
	cancel_delayed_work_sync(&di->work);

	dev_info(di->dev, "[shutdown]soc(%d%%), batv(%dmv),time(%lds)\n",
		di->cache.capacity, di->bat_vol / 1000,
		get_sys_tick_ms() / 1000);

	return;
}

static int bq27441_battery_suspend(struct device *dev)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&di->init_work);
	cancel_delayed_work_sync(&di->work);

	dev_info(di->dev, "[suspend]soc(%d%%), batv(%dmv),time(%lds)\n",
		di->cache.capacity,
		di->bat_vol / 1000,
		get_sys_tick_ms() / 1000);

	return 0;
}

static int bq27441_battery_resume(struct device *dev)
{
	return 0;
}

static void bq27441_battery_complete(struct device *dev)
{
	struct bq27441_device_info *di = dev_get_drvdata(dev);

	schedule_delayed_work(&di->work, 0 * HZ);
}

static const struct dev_pm_ops bq27441_battery_pm_ops = {
	.suspend       = bq27441_battery_suspend,
	.resume	       = bq27441_battery_resume,
	.complete      = bq27441_battery_complete,
};


static const struct i2c_device_id bq27441_id[] = {
	{ "bq27441", BQ27441 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27441_id);
static const struct of_device_id bq27441_battery_match[] = {
	{ .compatible = "ti,bq27441-battery", },
	{},
};
MODULE_DEVICE_TABLE(of, bq27441_battery_match);

static struct i2c_driver bq27441_battery_driver = {
	.driver = {
		.name = "bq27441-battery",
		.pm   = &bq27441_battery_pm_ops,
		.of_match_table = of_match_ptr(bq27441_battery_match),
	},
	.probe = bq27441_battery_probe,
	.remove = bq27441_battery_remove,
	.shutdown = bq27441_battery_shutdown,
	.id_table = bq27441_id,
};

static inline int bq27441_battery_i2c_init(void)
{
	int ret = i2c_add_driver(&bq27441_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ27441 i2c driver\n");

	return ret;
}

static inline void bq27441_battery_i2c_exit(void)
{
	i2c_del_driver(&bq27441_battery_driver);
}

/*
 * Module stuff
 */
static int __init bq27441_battery_init(void)
{
	int ret;

	struct device_node *node =
		of_find_compatible_node(NULL, NULL, "ti,bq27441-battery");

	if (!node) {
		printk("%s fail to find bq27441-battery node\n", __func__);
		return 0;
	}

	ret = bq27441_battery_i2c_init();
	if (ret)
		return ret;

	return ret;
}


static void __exit bq27441_battery_exit(void)
{
	bq27441_battery_i2c_exit();
}

#ifdef MODULE
module_init(bq27441_battery_init);
module_exit(bq27441_battery_exit);
#else
late_initcall(bq27441_battery_init);
#endif

MODULE_AUTHOR("Actions");
MODULE_DESCRIPTION("BQ27441 battery monitor driver");
MODULE_LICENSE("GPL");
