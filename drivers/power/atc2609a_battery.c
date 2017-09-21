/*
 * Actions ATC2609A BATTERY driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Terry Chen <chenbo@actions-semi.com>
 *
 * Atc2609a Charger Phy file
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
//#define DEBUG
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/kfifo.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/wakelock.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/atc260x/atc260x.h>
#include <linux/power/atc260x_battery.h>
#include <linux/owl_pm.h>
#include <linux/uaccess.h>

#define ATC2609A_BATTERY_DRV_TIMESTAMP		("20161010173016")
#define ATC2609A_BATTERY_DRV_VERSION		"r5p2"
#define ATC260X_RTC_NAME			("rtc0")

#define CONST_ROUNDING				(5 * 100)
#define CONST_FACTOR				(1000)
#define ADC_LSB_FOR_10mohm			(1144)
#define ADC_LSB_FOR_20mohm			(572)

#define SECONDS_PER_HOUR			(3600)
#define ADC_LSB_FOR_BATV			(732)
#define CONST_SOC_STOP				(1000)
#define FULL_CHARGE_SOC				(100000)
#define EMPTY_CHARGE_SOC			(0)
#define S2_CONSUME_DEFAULT_UA			(15000)
#define S4_CONSUME_DEFAULT_UA			(50)
/*current threshold(mA)*/
#define CHARGE_CURRENT_THRESHOLD		(60) /*if bati more than this, is charging*/
#define DISCHARGE_CURRENT_THRESHOLD		(30) /*if less than this, is discharging*/

#define ATC2609A_CAP_GAUGE_RESET		(1 << 0)

#define PMU_CHARGER_PHASE_PRECHARGE	        (1)
#define PMU_CHARGER_PHASE_CONSTANT_CURRENT	(2)
#define PMU_CHARGER_PHASE_CONSTANT_VOLTAGE	(3)
#define CHARGE_CV_CURRENT_THRESHOLD             (50)

#define ATC2609A_PMU_BASE			(0x00)

/* ATC2609A_CLMT_CTL0*/
#define ATC2609A_CLMT_CTL0			(ATC2609A_PMU_BASE + 0x82)
#define PMU_CLMT_CTL0_CLMT_EN			(1 << 0)
#define PMU_CLMT_CTL0_INIT_DATA_EN		(1 << 1)
#define PMU_CLMT_CTL0_U_STOP_SHIFT		(2)
#define PMU_CLMT_CTL0_U_STOP_MASK		(0xfff << PMU_CLMT_CTL0_U_STOP_SHIFT)
#define PMU_CLMT_CTL0_TIMER_SHIFT		(14)
#define PMU_CLMT_CTL0_TIMER_MASK		(0x3 << PMU_CLMT_CTL0_TIMER_SHIFT)
#define PMU_CLMT_CTL0_TIMER_1H			(0 << PMU_CLMT_CTL0_TIMER_SHIFT)
#define PMU_CLMT_CTL0_TIMER_3H			(1 << PMU_CLMT_CTL0_TIMER_SHIFT)
#define PMU_CLMT_CTL0_TIMER_5H			(2 << PMU_CLMT_CTL0_TIMER_SHIFT)
#define PMU_CLMT_CTL0_TIMER_7H			(3 << PMU_CLMT_CTL0_TIMER_SHIFT)
/* ATC2609A_CLMT_DATA0*/
#define ATC2609A_CLMT_DATA0			(ATC2609A_PMU_BASE + 0x83)
#define PMU_CLMT_DATA0_Q_MAX_SHIFT		(0)
#define PMU_CLMT_DATA0_Q_MAX_MASK		(0xffff << PMU_CLMT_DATA0_Q_MAX_SHIFT)
/*ATC2609A_CLMT_DATA1*/
#define ATC2609A_CLMT_DATA1			(ATC2609A_PMU_BASE + 0x84)
#define PMU_CLMT_DATA1_SOC_R_SHIFT		(8)
#define PMU_CLMT_DATA1_SOC_R_MASK		(0x7f << PMU_CLMT_DATA1_SOC_R_SHIFT)
#define PMU_CLMT_DATA1_SOC_A_SHIFT		(0)
#define PMU_CLMT_DATA1_SOC_A_MASK		(0x7f << PMU_CLMT_DATA1_SOC_A_SHIFT)
/*ATC2609A_CLMT_DATA2*/
#define ATC2609A_CLMT_DATA2			(ATC2609A_PMU_BASE + 0x85)
#define PMU_CLMT_DATA2_SOC_Q_R_SHIFT		(0)
#define PMU_CLMT_DATA2_SOC_Q_R_MASK		(0xffff << PMU_CLMT_DATA2_SOC_Q_R_SHIFT)
/*ATC2609A_CLMT_DATA3*/
#define ATC2609A_CLMT_DATA3			(ATC2609A_PMU_BASE + 0x86)
#define PMU_CLMT_DATA2_SOC_Q_A_SHIFT		(0)
#define PMU_CLMT_DATA2_SOC_Q_A_MASK		(0xffff << PMU_CLMT_DATA2_SOC_Q_A_SHIFT)
/*ATC2609A_CLMT_ADD0*/
/*ATC2609A_CLMT_ADD1*/
#define ATC2609A_CLMT_ADD1			(ATC2609A_PMU_BASE + 0x88)
#define PMU_CLMT_ADD1_SIGN_BIT_SHIFT		(15)
#define PMU_CLMT_ADD1_SIGN_BIT_MASK		(1 << PMU_CLMT_ADD1_SIGN_BIT_SHIFT)
#define PMU_CLMT_ADD1_SIGN_BIT			(1 << PMU_CLMT_ADD1_SIGN_BIT_SHIFT)
/*ATC2609A_CLMT_OCV_TABLE*/
#define ATC2609A_CLMT_OCV_TABLE			(ATC2609A_PMU_BASE + 0x89)
#define PMU_CLMT_OCV_TABLE_SOC_SEL_SHIFT	(12)
#define PMU_CLMT_OCV_TABLE_SOC_SEL_MASK		(0xf << PMU_CLMT_OCV_TABLE_SOC_SEL_SHIFT)
#define PMU_CLMT_OCV_TABLE_OCV_SET_SHIFT	(0)
#define PMU_CLMT_OCV_TABLE_OCV_SET_MASK		(0xfff << PMU_CLMT_OCV_TABLE_OCV_SET_SHIFT)
/*ATC2609A_CLMT_R_TABLE*/
#define ATC2609A_CLMT_R_TABLE			(ATC2609A_PMU_BASE + 0x8A)
#define PMU_CLMT_R_TABLE_SOC_SEL_SHIFT		(12)
#define PMU_CLMT_R_TABLE_SOC_SEL_MASK		(0xf << PMU_CLMT_R_TABLE_SOC_SEL_SHIFT)
#define PMU_CLMT_R_TABLE_R_SET_SHIFT		(0)
#define PMU_CLMT_R_TABLE_R_SET_MASK		(0x3ff << PMU_CLMT_R_TABLE_R_SET_SHIFT)
/*ATC2609A_PMU_ADC12B_I*/
#define ATC2609A_PMU_ADC12B_I			(ATC2609A_PMU_BASE + 0x56)
#define PMU_ADC12B_I_MASK			(0x3fff)
#define PMU_ADC12B_I_SIGN_BIT			(1 << 13)
/*ATC2609A_PMU_ADC12B_V*/
#define ATC2609A_PMU_ADC12B_V			(ATC2609A_PMU_BASE + 0x57)
#define PMU_ADC12B_V_MASK			(0xfff)
/* ATC2609A_PMU_SWCHG_CTL0 */
#define ATC2609A_PMU_SWCHG_CTL0			(ATC2609A_PMU_BASE + 0x16)
#define PMU_SWCHG_CTL0_RSENSEL		(1 << 12)
/* ATC2609A_PMU_SWCHG_CTL1 */
#define ATC2609A_PMU_SWCHG_CTL1			(ATC2609A_PMU_BASE + 0x17)
#define PMU_SWCHG_CTL1_EN_BAT_DET		(1 << 15)
/* ATC2609A_PMU_SWCHG_CTL4 */
#define ATC2609A_PMU_SWCHG_CTL4			(ATC2609A_PMU_BASE + 0x1A)
#define PMU_SWCHG_CTL4_PHASE_SHIFT		(11)
#define PMU_SWCHG_CTL4_PHASE_MASK		(0x3 << PMU_SWCHG_CTL4_PHASE_SHIFT)
#define PMU_SWCHG_CTL4_PHASE_PRECHARGE		(1 << PMU_SWCHG_CTL4_PHASE_SHIFT)
#define PMU_SWCHG_CTL4_BAT_EXT			(1 << 8)
#define PMU_SWCHG_CTL4_BAT_DT_OVER		(1 << 7)
#define PMU_RTC_CTL_VERI                        (1 << 10)

/*the min over-charged protect voltage of battery circuit*/
#define MIN_OVER_CHARGERD_VOL                   (4275)
#define BATTERY_INVALID_SOC			(0xfffff)
#define CLMT_PRE_AMPLIFIER_COMSUMP_UA		(300)
#define CLMT_ADC_COMSUMP_UA			(700)
/*the size of kfifo must be 2^n*/
#define SOC_WEIGHT_FIFO_NUM			(4)

/**
 * bat_dts_items - battery config items from dts.
 *
 * @log_switch :i f 1, store cap gauge log to sdcad.
 * @print_switch : if 1, print clmt info during poll.
 * @boot_cap_threshold : handle over to minicharge,if more than this,reboot  when onoff long pressed.
 * @bl_on_vol : handle over to minicharge,if more than this,can bright backlight during minicharge
 * @taper_cur : taper full current during charging.
 * @taper_vol : taper full voltage during charging.
 * @design_capacity : battery norminal capacity.
 * @term_vol : battery terminal voltage0, sotp voltage namely.
 * @term_vol1: battery terminal voltage1,higher than term_vol.
 * @taper_vol : full charge ocv, if battery ocv is higher than or equil with this,then full charge.
 * @temp_report: whether if report temperature to application or not.
 * @ov_protect : over charged protect-voltage of battery circuit.
 * @sleep_current: s2 consume while clmt is paused.
 * @shutdown_current:s4 consume while clmt is paused.
 */
struct bat_dts_items {
	int table[16];
	int log_switch;
	int print_switch;
	int boot_cap_threshold;
	int bl_on_vol;
	int taper_cur;
	int taper_vol;
	int design_capacity;
	int term_vol;
	int term_vol1;
	int temp_report;
	int ov_protect;
	int sleep_current;
	int shutdown_current;
};

/**
* liner -describe curve, which was composed by current and soc during cv.
*
* @k: the slope of curve
* @b: the offset of curve
* @x: horizontal axis
* @y: vertical axis
* @inited: indicate whether if slope and offset of curve were inited.
*/
struct liner {
	int k;
	int b;
	int x;
	int y;
	int inited;
};
/**
 * atc2609a_clmt - atc2609a clmt information.
 *
 * @atc260x : atc260x parent device.
 * @fifo : store the soc.
 * @delta_clmb : record the delta clmb.
 * @suspend_delta_clmb: delta clmb when suspend.
 * @resume_delta_clmb : delta clmb when resume.
 * @s2_consume_ua : record consumption during suspend(s2), unit:uA.
 * @s2_consume_soc_sum: record consumed soc sum during s2.
 * @clmt_consume_ua : including clmt  consume during s2, unit:uA.
 * @s2_consume_studied : whether if s2 consume is studied or not.
 * @clmb_r : remain clmb.
 * @soc_r : remain soc.
 * @soc_r_calc:calced soc according to remain clmb.
 * @soc_r_boot: remain soc when boot
 * @clmb_a : available clmb.
 * @soc_a : available soc.
 * @soc_a_slope: available soc when slope.
 * @soc_ocv: soc by looking up ocv-soc table.
 * @soc_real : real soc.
 * @soc_stored : stored soc.
 * @soc_cur : current soc.
 * @liner: save the slope and  offset of curve,
 *              which was composed by current and soc during cv.
 * @weight: weight value.
 * @soc_weight : soc weighted.
 * @soc_dis_liner : the soc by batv-soc liner.
 * @soc_filtered : soc filtered.
 * @soc_show : soc showing.
 * @soc_pre : previous soc.
 * @soc_weight_pre : save the previous weight soc.
 * @fcc : full charge capacity, normal capacity.
 */
struct atc2609a_clmt {
	struct atc260x_dev *atc260x;
	struct kfifo fifo;
	int delta_clmb;
	int suspend_delta_clmb;
	int resume_delta_clmb;
	u32 s2_consume_ua;
	int s2_consume_soc_sum;
	int clmt_consume_ua;
	bool s2_consume_studied;
	int clmb_r;
	int soc_r;
	int soc_r_calc;
	int soc_r_boot;
	int clmb_a;
	int soc_a;
	int delta_clmb_boot;
	int soc_a_slope;
	int soc_ocv;
	int soc_real;
	u32 soc_stored;
	int soc_cur;
	struct liner liner;
	int weight;
	int soc_weight;
	int soc_filtered;
	int soc_dis_liner;
	int soc_filtered_liner;
	int soc_filtered_last;
	u32 soc_show;
	int soc_pre;
	int soc_weight_pre;
	int fcc;
};

/**
 * atc2609a_battery - atc2609a battery information.
 *
 * @battery : battery used as power supply.
 * @atc260x : atc260x parent device.
 * @wq : work queue for atc2609a battery only.
 * @init_work : initialized work.
 * @poll_work : main poll work.
 * @bat_complete : battery detect complete.
 * @items : dts config items.
 * @interval : main poll work interval.
 * @interval_sum_r : interval sum value, used for calclating the charging resistor.
 * @interval_sum_pre : interval sum value, used for detecting full previously.
 * @interval_sum_full : interval sum value, used for detecting full.
 * @interval_sum_disch : interval sum value, used for discharging.
 * @bat_mutex : mutex lock.
 * @early_suspend :
 * @charging : record charge state, charing or discharging
 * @online : whether if battery is online.
 * @health : battery is shorted or not.
 * @bat_vol : record bat voltage.
 * @bat_vol_last:last battery voltage.
 * @bat_vol_save: save battery vol during charging.
 * @low_pwr_cnt : the count that battery voltage < 3.4v.
 * @bat_ocv: battery open circuit voltage.
 * @bat_cur : record bat current.
 * @bat_cur_last: last battery current.
 * @bat_cur_save: save battery current during charging.
 * @chg_type : record charge phrase.
 * @bat_temp : record bat temprature.
 * @bat_ch_r: battery charge resistor.
 * @bat_r_calced : indicate whether if charge resistor was calced or not.
 * @clmt : the 2609a gauge.
 */
struct atc2609a_battery {
	struct power_supply battery;
	struct atc260x_dev *atc260x;
	struct workqueue_struct *wq;
	struct delayed_work init_work;
	struct delayed_work poll_work;
	struct completion bat_complete;
	struct bat_dts_items items;
	bool big_current_shutdown;
	int interval;
	int interval_sum_r;
	int interval_sum_pre;
	int interval_sum_full;
	int interval_sum_disch;
	struct mutex  bat_mutex;
	int charging;
	int online;
	int health;
	int bat_vol;
	int bat_vol_last;
	int bat_vol_save;
	int low_pwr_cnt;
	int bat_ocv;
	int bat_cur;
	int bat_cur_last;
	int bat_cur_save;
	int chg_type;
	int bat_temp;
	int bat_ch_r;
	int bat_r_calced;
	int icm_channel;
	int batv_channel;
	int qmax_channel;
	int qrem_channel;
	int qavi_channel;
	struct atc2609a_clmt *clmt;
};
/**
 * battery_data - battery info, measured by BettaTeQ equipment.
 * @soc : the state of charge, unit:%;
 * @ocv : the open circuit voltage of battery, corresponding with soc;
 * @resistor : the inner resistor of battery, corresponding with soc&&ocv.
 */
struct battery_data {
	int soc;
	int ocv;
	int resistor;
};

static struct atc2609a_battery *battery;
/*record sys time before suspend*/
static unsigned long suspend_time_ms;
/*record sys time before shutdown*/
static unsigned long shutdown_time_ms;
/*record interval during suspend(s2)*/
static unsigned long suspend_interval_ms;
/*whether if power on  just or not*/
static bool just_power_on;
/*whether if the 1st store clmt info*/
static bool first_store_log = true;
/*whether if battery full detect launch or not*/
static bool detect_full_launch;
/*if true, means detect_bat_full function detect bat is full charge*/
static bool detect_full;
/*whether if charger was force onoff or not*/
static bool charger_off_force;
static int log_switch;
static struct battery_data bat_info[16] = {
	{0, 3000, 400},  {4, 3620, 400},  {8, 3680, 200},  {12, 3693, 120},
	{16, 3715, 120}, {20, 3734, 120}, {28, 3763, 120}, {36, 3787, 120},
	{44, 3806, 120}, {52, 3832, 120}, {60, 3866, 120}, {68, 3920, 120},
	{76, 3973, 120}, {84, 4028, 120}, {92, 4090, 120}, {100, 4169, 120}
};

static const char *note =
	"terminology:\n"
	"TM--------------------time stamp of sampling\n"
	"VOL-------------------battery voltage\n"
	"OCV-------------------open circuit voltage\n"
	"R(+)------------------resistor of charging\n"
	"CUR------------------ charge current(+)/"
			      "discharge current(-)\n"
	"CLMBd ----------------delta coulomb\n"
	"CLMBr ----------------remained coulomb\n"
	"SOCr -----------------remained soc\n"
	"SOCr(BOOT+)-----------soc when boot, lookup by ocv-soc\n"
	"SOCd(+)---------------delta soc since boot\n"
	"SOCr(CALC+)-----------(SOCr(BOOT+)+SOCd(+))/FCC\n"
	"CLMBa------------------available coulomb\n"
	"SOCa-------------------available soc\n"
	"SOCa(CV+)--------------available soc during CV(charging)\n"
	"SOCocv(+)--------------soc by ocv mapping\n"
	"SOCreal----------------the final calclated soc\n"
	"SOCf-------------------filtered soc\n"
	"SOCl(-)--------------- soc according to vol&soc line\n"
	"SOCfl(-)---------------the 2nd filtered soc(adding SOCL)\n"
	"SOCcur-----------------current soc\n"
	"SOC--------------------state of charge\n"
	"SOCst------------------soc stored into permanent reg\n"
	"FCC--------------------full charge capacity\n"
	"K/B/X1/Y1--------------cur&soc(charging) line/"
				"vol&soc(discharging) line\n"
	"INITED-----------------line was inited\n\n\n\n";

extern int owl_pm_wakeup_flag(void);
extern void atc260x_charger_off_force(void);
extern void atc260x_charger_on_force();
extern void atc260x_charger_off_restore(void);
extern void atc260x_charger_set_cc_force(int cc);
extern void atc260x_charger_set_cc_restore(void);
extern int atc260x_charger_get_cc(void);
extern int atc260x_charger_set_cc_finished(void);
extern bool atc260x_charger_check_capacity(void);
extern int atc260x_chk_bat_online_intermeddle(void);
extern int atc260x_get_charger_online_status(void);
static int atc2609a_bat_get_charging(int bat_cur);
static int atc2609a_bat_calc_ocv(struct atc2609a_battery *battery);
static int  atc2609a_clmt_get_asoc(struct atc2609a_clmt *clmt);
static void bat_update_base_state(struct atc2609a_battery *battery);
static int atc2609a_clmt_get_fcc(struct atc2609a_clmt *clmt);

static void atc2609a_bat_cfg_init(struct device_node *node,
	struct bat_dts_items *items)
{
	const int *value;
	int ret;
	int i;

	/*ocv_table*/
	ret = of_property_read_u32_array(node, "ocv_table",
		(u32 *)items->table, ARRAY_SIZE(items->table));
	if (ret)
		pr_err("[%s] get ocv_table err\n", __func__);
	for (i = 0; i < 16; i++) {
		bat_info[i].ocv = items->table[i];
		pr_info("%s ocv(%dmv)\n", __func__, bat_info[i].ocv);
	}
	/*r_table*/
	ret = of_property_read_u32_array(node, "r_table",
		(u32 *)items->table, ARRAY_SIZE(items->table));
	if (ret)
		pr_err("[%s] get ocv_table err\n", __func__);
	for (i = 0; i < 16; i++) {
		bat_info[i].resistor = items->table[i];
		pr_debug("%s resistor(%dmohm)\n",
			__func__, bat_info[i].resistor);
	}
	/*log_switch*/
	value = of_get_property(node, "log_switch", NULL);
	if (value) {
		items->log_switch = be32_to_cpup(value);
		pr_debug("[%s] log_switch success,vaule(%d)!\n",
			__func__,  items->log_switch);
	} else {
		items->log_switch = 0;
		pr_err("[%s] log_switch not found, %d default\n",
			__func__,  items->log_switch);
	}
	/*print_switch*/
	value = of_get_property(node, "print_switch", NULL);
	if (value) {
		items->print_switch = be32_to_cpup(value);
		pr_debug("[%s] print_switch success,vaule(%d)!\n",
			__func__,  items->print_switch);
	} else {
		items->print_switch = 0;
		pr_err("[%s] print_switch not found, %d default\n",
			__func__,  items->print_switch);
	}
	/*boot_cap_threshold  */
	value = of_get_property(node, "boot_cap_threshold", NULL);
	if (value) {
		items->boot_cap_threshold = be32_to_cpup(value);
		pr_debug("[%s] boot_cap_threshold success,vaule(%d)!\n",
			__func__,  items->boot_cap_threshold);
	} else {
		items->boot_cap_threshold = 3;
		pr_err("[%s] boot_cap_threshold not found, %d default\n",
			__func__,  items->boot_cap_threshold);
	}
	/*bl_on_vol  */
	value = of_get_property(node, "bl_on_vol", NULL);
	if (value) {
		items->bl_on_vol = be32_to_cpup(value);
		pr_debug("[%s] bl_on_vol success,vaule(%d)!\n",
			__func__,  items->bl_on_vol);
	} else {
		items->bl_on_vol = 3300;
		pr_err("[%s] bl_on_vol not found, %dmV default\n",
			__func__,  items->bl_on_vol);
	}
	/*taper_cur */
	value = of_get_property(node, "taper_cur", NULL);
	if (value) {
		items->taper_cur = be32_to_cpup(value);
		pr_debug("[%s] taper_cur success,vaule(%d)!\n",
			__func__,  items->taper_cur);
	} else {
		items->taper_cur = 400;
		pr_err("[%s] taper_cur not found, %dmA default\n",
			__func__,  items->taper_cur);
	}
	/*taper_vol */
	value = of_get_property(node, "taper_vol", NULL);
	if (value) {
		items->taper_vol = be32_to_cpup(value);
		pr_debug("[%s] taper_vol success,vaule(%d)!\n",
			__func__,  items->taper_vol);
	} else {
		items->taper_vol = 4200;
		pr_err("[%s] taper_vol not found, %dmV default\n",
			__func__,  items->taper_vol);
	}
	/*design_capacity */
	value = of_get_property(node, "design_capacity", NULL);
	if (value) {
		items->design_capacity = be32_to_cpup(value);
		pr_debug("[%s] design_capacity success,vaule(%d)!\n",
			__func__,  items->design_capacity);
	} else {
		pr_err("[%s] design_capacity not found\n",
			__func__);
	}
	/*term_vol*/
	value = of_get_property(node, "term_vol", NULL);
	if (value) {
		items->term_vol = be32_to_cpup(value);
		pr_debug("[%s] log_switch success,vaule(%d)!\n",
			__func__,  items->term_vol);
	} else {
		items->term_vol = 3400;
		pr_err("[%s] term_vol not found, %dmV default\n",
			__func__,  items->term_vol);
	}
	/*term_vol1*/
	value = of_get_property(node, "term_vol1", NULL);
	if (value) {
		items->term_vol1 = be32_to_cpup(value);
		pr_debug("[%s] term_vol1 success,vaule(%d)!\n",
			__func__,  items->term_vol1);
	} else {
		items->term_vol1 = items->term_vol + 200;
		pr_info("[%s] term_vol1 not found, %dmV default\n",
			__func__,  items->term_vol + 200);
	}

	/*temp_report*/
	value = of_get_property(node, "temp_report", NULL);
	if (value) {
		items->temp_report = be32_to_cpup(value);
		pr_debug("[%s] temp_report success,vaule(%d)!\n",
			__func__,  items->temp_report);
	} else {
		items->temp_report = 0;
		pr_info("[%s] temp_report not found, %d default\n",
			__func__,  items->temp_report);
	}
	/*ov_protect*/
	value = of_get_property(node, "ov_protect", NULL);
	if (value) {
		items->ov_protect = be32_to_cpup(value);
		pr_debug("[%s] ov_protect success,vaule(%d)!\n",
			__func__,  items->ov_protect);
	} else {
		items->ov_protect = MIN_OVER_CHARGERD_VOL;
		pr_err("[%s] ov_protect not found, %d default\n",
			__func__,  items->ov_protect);
	}
	/*sleep_current*/
	value = of_get_property(node, "sleep_current", NULL);
	if (value) {
		items->sleep_current = be32_to_cpup(value);
		if (items->sleep_current < 0 ||
			items->sleep_current > 20000)
			items->sleep_current = S2_CONSUME_DEFAULT_UA;
			pr_debug("[%s] sleep_current success,vaule(%d)!\n",
				__func__,  items->sleep_current);
	} else {
		items->sleep_current = S2_CONSUME_DEFAULT_UA;
		pr_err("[%s] sleep_current not found, %d default\n",
			__func__,  items->sleep_current);
	}
	/*shutdown_current*/
	value = of_get_property(node, "shutdown_current", NULL);
	if (value) {
		items->shutdown_current = be32_to_cpup(value);
		if (items->shutdown_current < 0 ||
			items->shutdown_current > 50)
			items->shutdown_current = S4_CONSUME_DEFAULT_UA;
			pr_debug("[%s] shutdown_current success,vaule(%d)!\n",
				__func__,  items->shutdown_current);
	} else {
		items->shutdown_current = S4_CONSUME_DEFAULT_UA;
		pr_err("[%s] shutdown_current not found, %d default\n",
			__func__,  items->shutdown_current);
	}
}

static int measure_voltage_avr(void)
{
	int data;
	int sum = 0;
	int i;
	int ret;
	for (i = 0; i < 5; i++) {
		usleep_range(2000, 2200);
		ret = atc260x_auxadc_get_translated(battery->atc260x, battery->batv_channel, &data);
		if (ret) {
			data = 0;
			pr_err("[%s] get battery voltage failed\n",	__func__);
		}
		sum += data;
	}
	data = sum / 5;

	return data;
}

int  measure_current(void)
{
	int bat_curr;
	int ret;

	ret = atc260x_auxadc_get_translated(battery->atc260x, battery->icm_channel, &bat_curr);
	if (ret) {
		bat_curr = 0;
		pr_err("[%s] get battery current failed\n",	__func__);
	}

	if (((bat_curr >= 0) && (bat_curr <= CHARGE_CURRENT_THRESHOLD)) ||
		((bat_curr <= 0) && (abs(bat_curr) <= DISCHARGE_CURRENT_THRESHOLD)))
	{
		return 0;
	}

	return bat_curr;
}

static int measure_current_avr(void)
{
	int sum = 0;
	int i;

	for (i = 0; i < 10; i++) {
		usleep_range(2000, 2200);
		sum += measure_current();
	}

	return sum / 10;
}

/**
 * atc2609a_clmt_get_delta_clmb - get delta coulomb since last clmt reset.
 */
static int atc2609a_clmt_get_delta_clmb(struct atc2609a_clmt *clmt)
{
	unsigned int  ldata;
	unsigned int  hdata;
	unsigned int data;
	int ohm_10 = 0;

	ohm_10 = atc260x_reg_read(clmt->atc260x, ATC2609A_PMU_SWCHG_CTL0);
	ohm_10 = ohm_10 & PMU_SWCHG_CTL0_RSENSEL;

	ldata = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_ADD0);
	hdata = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_ADD1);
	data = ldata | ((hdata & ~PMU_CLMT_ADD1_SIGN_BIT_MASK) << 16);
	data = data / SECONDS_PER_HOUR;
	if (ohm_10)
		data = (data * ADC_LSB_FOR_10mohm + CONST_ROUNDING) / CONST_FACTOR;
	else
		data = (data * ADC_LSB_FOR_20mohm + CONST_ROUNDING) / CONST_FACTOR;

	return (hdata & PMU_CLMT_ADD1_SIGN_BIT_MASK) ? -data : data;
}

static  int  atc2609a_clmt_get_remain_clmb(struct atc2609a_clmt *clmt)
{
	int data;
	int ret;

	ret = atc260x_auxadc_get_translated(battery->atc260x, battery->qrem_channel, &data);
	if (ret) {
		data = 0;
		pr_err("[%s] get remain clmb failed\n",	__func__);
	}
	return data;
}

/**
 * atc2609a_clmt_get_avail_clmb - get available coulomb.
 */
static int  atc2609a_clmt_get_avail_clmb(struct atc2609a_clmt *clmt)
{
	int data;
	int ret;

	ret = atc260x_auxadc_get_translated(battery->atc260x, battery->qavi_channel, &data);
	if (ret) {
		data = 0;
		pr_err("[%s] get available clmb failed\n",	__func__);
	}
	return data;
}

/**
  * atc2609a_clmt_get_ocv - get one ocv matched with one soc from ocv-soc table.
  * @soc : matched with the ocv.
  * @return : ocv, matched with soc.
  */
static int atc2609a_clmt_get_ocv(struct atc260x_dev *atc260x, int soc)
{
	int data;
	int i;

	if (soc > 100)
		data = 100;
	else if (soc < 0)
		data = 0;
	else
		data = soc;

	for (i = 15; i >= 0; i--) {
		if (data > bat_info[i].soc)
			break;
	}

	atc260x_reg_write(atc260x, ATC2609A_CLMT_OCV_TABLE,
		i <<  PMU_CLMT_OCV_TABLE_SOC_SEL_SHIFT);
	data = atc260x_reg_read(atc260x, ATC2609A_CLMT_OCV_TABLE)
		& PMU_CLMT_OCV_TABLE_OCV_SET_MASK;
	data = (data * ADC_LSB_FOR_BATV * 2 + CONST_ROUNDING) /
		CONST_FACTOR;
	pr_info("[%s], %%%d, ocv:%dmv\n",
		__func__, bat_info[i].soc, data);

	return data;
}

static void atc2609a_clmt_dump_ocv(struct atc2609a_clmt *clmt)
{

	int i;
	int data;

	for (i = 0; i < 16; i++) {
		/*
		* Fistr of all, set ocv to  zero,
		* then write into  reg together with soc value.
		* Next to read this reg.
		*/
		atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_OCV_TABLE,
			i <<  PMU_CLMT_OCV_TABLE_SOC_SEL_SHIFT);
		data = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_OCV_TABLE)
			& PMU_CLMT_OCV_TABLE_OCV_SET_MASK;
		data = (data * ADC_LSB_FOR_BATV * 2 + CONST_ROUNDING) /
			CONST_FACTOR;
		pr_info("[%s], %%%d, ocv:%dmv\n",
			__func__, bat_info[i].soc, data);
	}
}

/**
 * atc2609a_clmt_set_ocv_batch - set battery ocv into ocv-soc table,
 * which was saved in clmt reg.
 */
static void atc2609a_clmt_set_ocv_batch(struct atc2609a_clmt *clmt,
		struct battery_data *bat_info)
{
	int i;
	int ocv;

	for (i = 0; i < 16; i++) {
		ocv = (bat_info[i].ocv * CONST_FACTOR + CONST_ROUNDING)
			/ ADC_LSB_FOR_BATV / 2;
		atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_OCV_TABLE,
			ocv | (i << PMU_CLMT_OCV_TABLE_SOC_SEL_SHIFT));
	}

	atc2609a_clmt_dump_ocv(clmt);
}
/**
* atc2609a_clmt_calc_ch_r - calc the charging resistor.
*
* Note: Calclating the charging resistor happened during charging and idle.
* Because we could shutoff charger for resistor relaxtion,
* We must keep calclating ch_r during idle,
* otherwise the charger could not restore.
*/
static int atc2609a_clmt_calc_ch_r(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	int ch_r;

    /*
     * Detect full has higher priority than calclating charge resistor,
     * ohterwise could not detect full state.
     */
	if (detect_full_launch)
		return battery->bat_ch_r;

    /*
     * if charger is off, go to calc resistor, it has highest priority,
     * otherwise charger is off forever due to soc_show change to soc,
     * which is no zero mod 5.
     */
	if (charger_off_force)
		goto calc;

	if (battery->charging == POWER_SUPPLY_STATUS_DISCHARGING)
		return 0;
	if ((clmt->soc_show % 10)) {
		battery->bat_r_calced = 0;
		return battery->bat_ch_r;
	}

	if (battery->bat_r_calced)
		return battery->bat_ch_r;

	/*save vol and current*/
	battery->bat_vol_save = battery->bat_vol;
	battery->bat_cur_save = battery->bat_cur;
	if (battery->bat_cur_save <= 0)
		return battery->bat_ch_r;

	/*force charger off*/
	atc260x_charger_off_force();
	charger_off_force = true;
	/*update charging state for ohters later*/
	bat_update_base_state(battery);

calc:
	battery->interval_sum_r += battery->interval;
	pr_info("[%s] interval_sum_r(%ds)\n",
		__func__, battery->interval_sum_r - battery->interval);
	if (battery->interval_sum_r >= battery->interval + 60 * 1000) {
		battery->interval_sum_r = 0;
		ch_r = (battery->bat_vol_save - battery->bat_vol) * 1000 /
			battery->bat_cur_save;
			pr_info("[%s] ch_r=(batv-ocv)*1000/i=(%d-%d)*1000/%d=%d\n",
				__func__,
				battery->bat_vol_save,
				battery->bat_vol,
				battery->bat_cur_save,
				ch_r);
			if (ch_r > 500)
				ch_r = 500;
			battery->bat_ch_r = ch_r;
			battery->bat_r_calced = 1;
			/*restore charge state*/
			atc260x_charger_off_restore();
			charger_off_force = false;
			/*update charging state for ohters later*/
			bat_update_base_state(battery);
	}

	return battery->bat_ch_r;
}

static void atc2609a_clmt_dump_r(struct atc2609a_clmt *clmt)
{
	int i;

	for (i = 0; i < 16; i++) {
		/* Fistr of all, set r to  zero,
		 * then write into  reg together with soc value.
		 * Next to read this reg.
		 */
		atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_R_TABLE,
			i <<  PMU_CLMT_R_TABLE_SOC_SEL_SHIFT);
		pr_info("[%s],%%%d, r:%dmohm\n",
			__func__, bat_info[i].soc,
			atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_R_TABLE) &
				PMU_CLMT_R_TABLE_R_SET_MASK);
	}

}

/**
 * atc2609a_clmt_set_r_batch - set battery resistor into r-soc table,which was saved in clmt reg.
 */
static void atc2609a_clmt_set_r_batch(struct atc2609a_clmt *clmt)
{
	int i;
	int data;

	for (i = 0; i < 16; i++) {
		/*In order to read r table, need to write zero for r into reg.*/
		data = bat_info[i].resistor &
			~PMU_CLMT_R_TABLE_SOC_SEL_MASK;
		atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_R_TABLE,
			data | (i << PMU_CLMT_R_TABLE_SOC_SEL_SHIFT));
	}
}

static int atc2609a_bat_calc_ocv(struct atc2609a_battery *battery)
{
	if (battery->charging == POWER_SUPPLY_STATUS_CHARGING)
		battery->bat_ocv = battery->bat_vol -
			battery->bat_cur * battery->bat_ch_r / 1000;
	else if (battery->charging == POWER_SUPPLY_STATUS_DISCHARGING)
		battery->bat_ocv = battery->bat_vol +
			abs(battery->bat_cur) * battery->bat_ch_r / 1000;
	else
		battery->bat_ocv = battery->bat_vol;
	pr_debug("[%s] ocv:%dmv\n", __func__, battery->bat_ocv);

	return battery->bat_ocv;
}

/**
 * atc2609a_clmt_get_delta_soc_ch - get delta soc during charging.
 */
static int  atc2609a_clmt_get_delta_soc_ch(struct atc2609a_clmt *clmt)
{
	int fcc;
	int data;
	data = atc2609a_clmt_get_delta_clmb(clmt);
	fcc = atc2609a_clmt_get_fcc(clmt);

	if (data - clmt->delta_clmb_boot >= 0)
		return abs(data - clmt->delta_clmb_boot) *
			100 * 1000 / fcc;
	else {
		pr_err("[%s] delta clmb value err!!\n", __func__);
		return -EINVAL;
	}
}

/**
 * atc2609a_clmt_get_rsoc - get remain soc.
 */
static int  atc2609a_clmt_get_rsoc(struct atc2609a_clmt *clmt)
{
	int data;
	data = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_DATA1);
	data = (data & PMU_CLMT_DATA1_SOC_R_MASK)
		>> PMU_CLMT_DATA1_SOC_R_SHIFT;

	return data * 1000;
}

/**
 * atc2609a_clmt_get_asoc - get available soc.
 */
static int  atc2609a_clmt_get_asoc(struct atc2609a_clmt *clmt)
{
	int data;

	data = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_DATA1);
	data = data & PMU_CLMT_DATA1_SOC_A_MASK;

	return data * 1000;
}

/**
 * atc2609a_clmt_get_asoc_lookup - lookup soc from ocv-soc table by ocv.
 * ocv = batv + ir,  (charging);
 * ocv = batv - ir,  (discharging)
 */
static int atc2609a_clmt_get_asoc_lookup(struct atc2609a_battery *battery)
{
	int soc;
	int i;

	for (i = 0; i < 16; i++)
		if (battery->bat_ocv < bat_info[i].ocv)
			break;

		if (i == 0)
			return 0;
		else if (i > 15)
			return 100000;
		else {
			soc = (bat_info[i].soc - bat_info[i - 1].soc) * 1000 /
				(bat_info[i].ocv - bat_info[i - 1].ocv);
			soc = soc * (battery->bat_ocv - bat_info[i - 1].ocv);
			soc = (soc + CONST_ROUNDING) / 1000 +
				bat_info[i - 1].soc;
		}

	return soc * 1000;
}

static void atc2609a_clmt_calc_liner(struct liner *liner,
	int x1, int y1, int x2, int y2)
{
	liner->k = (y2 - y1) / (x2 - x1);
	liner->b = y2 - liner->k * x2;
	liner->x = x1;
	liner->y = y1;

	if (!liner->inited)
		liner->inited = 1;
}

/**
 * atc2609a_clmt_get_asoc_slope - get available soc use slope.
 *
 * We assume that the charge current is proportional
 * to current soc during constant charging.
 * Use y = k * x + b to get available soc.
 * Here, y: current soc;
*        x: charger current;
*        k: (98000-SOCcur)/(taper_cur-bat_cur)
*        b: 98000-k*taper_cur
 */
static int atc2609a_clmt_get_asoc_slope(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int soc_a_slope;

       /*
	* valid only :
	* 1.charging;
	* 2.power enough,dc5v or usb adapter charging under backlight off;
	* 3.cc set finished;
	* 4.cv stage;
	*/
	if ((battery->charging != POWER_SUPPLY_STATUS_CHARGING) ||
		!atc260x_charger_check_capacity() ||
		!atc260x_charger_set_cc_finished() ||
		(battery->bat_cur >= atc260x_charger_get_cc() -
			CHARGE_CV_CURRENT_THRESHOLD)) {
		clmt->liner.inited = 0;
		pr_err("[%s] can not meet conditon to calc soc using slope!\n",
			__func__);
		return -EINVAL;
	}

	if (!clmt->liner.inited) {
		atc2609a_clmt_calc_liner(&clmt->liner,
			battery->bat_cur,
			clmt->soc_cur,
			items->taper_cur,
			FULL_CHARGE_SOC - 1);
		pr_debug("[%s] (c1,sr1)=(%d,%d),(c2,sr2)=(%d,%d),k(%d),b(%d)\n",
			__func__,
		battery->bat_cur,
		clmt->soc_cur,
		items->taper_cur,
		FULL_CHARGE_SOC - 1,
		clmt->liner.k,
		clmt->liner.b);
		if (clmt->liner.k > 0) {
			pr_err("[%s] k > 0, err!\n", __func__);
			return -EINVAL;
		}
	}

	soc_a_slope = battery->bat_cur * clmt->liner.k +
		clmt->liner.b;
	pr_debug("[%s] soca_slope=bat_cur*k+b=%d*(%d)+%d=%d\n",
		__func__,
		battery->bat_cur,
		clmt->liner.k, clmt->liner.b, soc_a_slope);

	if (soc_a_slope > FULL_CHARGE_SOC - 1)
		return FULL_CHARGE_SOC - 1;
	if (soc_a_slope < 0)
		return 0;

	return soc_a_slope;
}

static int atc2609a_clmt_get_fcc(struct atc2609a_clmt *clmt)
{
	int fcc;
	int ret;

	ret = atc260x_auxadc_get_translated(battery->atc260x, battery->qmax_channel, &fcc);
	if (ret) {
		fcc = 0;
		pr_err("[%s] get qmax failed\n",	__func__);
	}
	return fcc;
}

/**
 * atc2609a_clmt_set_fcc - set full charger coulomb,
 *        norminal capacity initialized.
 */
static int  atc2609a_clmt_set_fcc(struct atc2609a_clmt *clmt, int fcc)
{
	int data = 0;

	data = atc260x_reg_read(clmt->atc260x, ATC2609A_PMU_SWCHG_CTL0);
	data = data & PMU_SWCHG_CTL0_RSENSEL;

	if (data) {
		data = (fcc * CONST_FACTOR + CONST_ROUNDING) /
			ADC_LSB_FOR_10mohm;
	} else {
		data = (fcc * CONST_FACTOR + CONST_ROUNDING) /
			ADC_LSB_FOR_20mohm;
	}

	atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_DATA0, data);

	return 0;
}

static void atc2609a_clmt_check_fcc(struct atc2609a_clmt *clmt)
{
	struct bat_dts_items *items = &battery->items;
	int diff_val;
	int fcc;

	fcc = atc2609a_clmt_get_fcc(clmt);
	diff_val = abs(items->design_capacity - fcc);
	if (diff_val >= items->design_capacity / 2) {
		pr_info("[%s] fcc abnormal, fcc(%d)\n", __func__, fcc);
		atc2609a_clmt_set_fcc(clmt, items->design_capacity);
	}
}

/**
  * atc2609a_clmt_set_stop_v - set stop voltage of battery.
  */
static void atc2609a_clmt_set_stop_v(struct atc2609a_clmt *clmt,
		int terminal)
{
	int data;

	data = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_CTL0) &
		(~PMU_CLMT_CTL0_U_STOP_MASK);
	data |= ((terminal * CONST_FACTOR + CONST_ROUNDING) /
		ADC_LSB_FOR_BATV / 2)
		<< PMU_CLMT_CTL0_U_STOP_SHIFT;
	atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_CTL0, data);
}

static void atc2609a_clmt_set_s3a_steady_time(struct atc2609a_clmt *clmt,
		int time)
{
	int data;

	data = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_CTL0) &
		(~PMU_CLMT_CTL0_TIMER_MASK);
	data |= time;
	atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_CTL0, data);
}

static bool atc2609a_clmt_get_inited(struct atc260x_dev *atc260x)
{
	int inited;

	inited = atc260x_reg_read(atc260x, ATC2609A_CLMT_CTL0) &
		PMU_CLMT_CTL0_INIT_DATA_EN;

	if (inited) {
		pr_info("[%s]:clmt initialized\n", __func__);
		return true;
	} else {
		pr_info("[%s]:CLMT not initialized\n", __func__);
		return false;
	}
}

static void atc2609a_clmt_set_inited(struct atc2609a_clmt *clmt,
		bool inited)
{
	int data;

	data = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_CTL0);
	if (inited)
		atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_CTL0,
			data | PMU_CLMT_CTL0_INIT_DATA_EN);
	else
		atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_CTL0,
			data  & ~PMU_CLMT_CTL0_INIT_DATA_EN);
}

static void atc2609a_clmt_enable(struct atc2609a_clmt *clmt, bool enable)
{
	int data;

	data = atc260x_reg_read(clmt->atc260x, ATC2609A_CLMT_CTL0);
	if (enable)
		data |= PMU_CLMT_CTL0_CLMT_EN;
	else
		data &= ~PMU_CLMT_CTL0_CLMT_EN;

	atc260x_reg_write(clmt->atc260x, ATC2609A_CLMT_CTL0, data);
}

/**
 * atc2609a_bat_check_health - check if battery was ever shoted/over discharged or not.
 */
static int atc2609a_bat_check_health(struct atc2609a_battery *battery)
{
	struct atc260x_dev *atc260x = battery->atc260x;
	int full_ocv;
	int batv;

	batv = measure_voltage_avr();
	if (batv <= 200) {
		atc260x_charger_set_cc_force(50);
		atc260x_charger_on_force();
		msleep(64);
		batv = measure_voltage_avr();
		if (batv <= 200) {
			pr_info("[%s] battery is shorting\n", __func__);
			battery->health = BAT_SHORTING;
		} else {
			battery->health = BAT_NORMAL;
		}
		atc260x_charger_off_force();
		atc260x_charger_set_cc_restore();
		atc260x_charger_off_restore();
	} else {
		full_ocv = atc2609a_clmt_get_ocv(atc260x, 100);
		if (abs(full_ocv - bat_info[15].ocv) > 300) {
			battery->health = BAT_ABNORMAL_EVER;
			pr_info("[%s]:battery is abnormal ever\n", __func__);
		} else {
			battery->health = BAT_NORMAL;
			pr_info("[%s] battery is normal\n", __func__);
		}
	}

	return battery->health;
}

static void atc2609a_bat_update_phrase(struct atc2609a_battery *battery)
{
	int data;
	data = atc260x_reg_read(battery->atc260x, ATC2609A_PMU_SWCHG_CTL4);
	battery->chg_type = (data & PMU_SWCHG_CTL4_PHASE_MASK) >>
		PMU_SWCHG_CTL4_PHASE_SHIFT;
}

static int atc2609a_bat_check_charge_type(struct atc2609a_battery *battery,
	int *type)
{
	switch (battery->chg_type) {
	case PMU_CHARGER_PHASE_PRECHARGE:
	    *type = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	    break;
	case PMU_CHARGER_PHASE_CONSTANT_CURRENT:
	case PMU_CHARGER_PHASE_CONSTANT_VOLTAGE:
	    *type = POWER_SUPPLY_CHARGE_TYPE_FAST;
	    break;
	default:
	    *type = POWER_SUPPLY_CHARGE_TYPE_NONE;
	    break;
	}

	return 0;
}

static int atc2609a_bat_get_charging(int bat_cur)
{
	if (bat_cur < 0)
		return POWER_SUPPLY_STATUS_DISCHARGING;
	else if (bat_cur > 0)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int atc2609a_bat_check_status(struct atc2609a_battery *battery,
	int *status)
{
	struct atc2609a_clmt *clmt = battery->clmt;

	if (!battery->online || (battery->health != BAT_NORMAL)) {
		*status = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;
	}
	if (clmt->soc_show == 100) {
		*status = POWER_SUPPLY_STATUS_FULL;
		return 0;
	}
	if (atc260x_get_charger_online_status())
		*status = POWER_SUPPLY_STATUS_CHARGING;
	else
		*status = POWER_SUPPLY_STATUS_DISCHARGING;

	return 0;
}

static int battery_info_store(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	struct atc260x_dev *atc260x = battery->atc260x;
	struct rtc_device *rtc;
	struct rtc_time tm;
	u8 buf[200];
	struct file *filp;
	mm_segment_t fs;
	int offset = 0;
	bool exist;

	fs = get_fs();
	set_fs(KERNEL_DS);

	/*check if log file exists*/
	filp = filp_open("/sdcard/cap_gauge_info.log",
		O_RDWR, 0);
	if (IS_ERR(filp))
		exist = false;
	else
		exist = true;

	/*if log file absent, create it*/
	if (!exist) {
		filp = filp_open("/sdcard/cap_gauge_info.log",
			O_CREAT | O_RDWR, 0644);
		if (IS_ERR(filp)) {
			pr_info("[%s] can't create log file\n", __func__);
			return -1;
		}
		pr_info("[%s] create log file successful!\n", __func__);
		if (!exist) {
			filp->f_op->llseek(filp, 0, SEEK_END);
			filp->f_op->write(filp, (char *)note,
				strlen(note) + 1, &filp->f_pos);
		}
	}

	/*first time, show  title*/
	if (first_store_log) {
		memset(buf, 0, 200);
		offset = sprintf(buf, "TM,VOL,OCV,R(CH),CUR,CLMBd,"
					"CLMBr,SOCr,SOCr(CALC+),"
					"CLMBa,SOCa,SOCa(CV+),SOCocv(+),"
					"SOCreal,SOCf,SOCl(-),SOCfl(-),"
					"SOCcur,SOC,SOCst,FCC,"
					"K/B/X1/Y1,INITED\n");
		filp->f_op->llseek(filp, 0, SEEK_END);
		filp->f_op->write(filp, (char *)buf, offset + 1, &filp->f_pos);
		first_store_log = false;
	}

	/*sample data and wrte log file*/
	memset(buf, 0, 200);
	rtc = rtc_class_open(ATC260X_RTC_NAME);
	if (rtc == NULL) {
		pr_err("[%s]: unable to open rtc device atc260x\n", __func__);
		return -1;
	}
	rtc_read_time(rtc, &tm);
	rtc_class_close(rtc);
	clmt->delta_clmb = atc2609a_clmt_get_delta_clmb(clmt);
	clmt->clmb_r = atc2609a_clmt_get_remain_clmb(clmt);
	clmt->soc_r = atc2609a_clmt_get_rsoc(clmt);
	clmt->clmb_a = atc2609a_clmt_get_avail_clmb(clmt);
	clmt->soc_a = atc2609a_clmt_get_asoc(clmt);
	atc260x_pstore_get(atc260x, ATC260X_PSTORE_TAG_GAUGE_CAP,
		&clmt->soc_stored);
	clmt->fcc = atc2609a_clmt_get_fcc(clmt);
	offset = sprintf(buf, "%2d-%02d-%02d %02d:%02d:%02d,%d,%04d,%04d,"
		"%04d,%04d,%04d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
		"%d/%d/%d/%d,%d\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec,
		battery->bat_vol,
		battery->bat_ocv,
		battery->bat_ch_r,
		battery->bat_cur,
		clmt->delta_clmb,
		clmt->clmb_r,
		clmt->soc_r,
		clmt->soc_r_calc,
		clmt->clmb_a,
		clmt->soc_a,
		clmt->soc_a_slope,
		clmt->soc_ocv,
		clmt->soc_real,
		clmt->soc_filtered,
		clmt->soc_dis_liner,
		clmt->soc_filtered_liner,
		clmt->soc_cur,
		clmt->soc_show,
		clmt->soc_stored,
		clmt->fcc,
		clmt->liner.k,
		clmt->liner.b,
		clmt->liner.x,
		clmt->liner.y,
		clmt->liner.inited);
	filp->f_op->llseek(filp, 0, SEEK_END);
	filp->f_op->write(filp, (char *)buf, offset + 1, &filp->f_pos);

	set_fs(fs);
	filp_close(filp, NULL);
	return 0;
}

static void atc2609a_bat_dump(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	struct bat_dts_items *items = &battery->items;
	u32 soc;

	pr_info("[%s] VOL:%d\n", __func__,
		measure_voltage_avr());
	pr_info("[%s] OCV:%d\n", __func__,
		battery->bat_ocv);
	pr_info("[%s] R(CH):%d\n", __func__,
		battery->bat_ch_r);
	pr_info("[%s] CUR:%d\n", __func__,
		measure_current());
	pr_info("[%s] CLMBd:%d\n",
		__func__,
		atc2609a_clmt_get_delta_clmb(clmt));
	pr_info("[%s] CLMBd(BOOT+):%d\n",
		__func__,  clmt->delta_clmb_boot);
	pr_info("[%s] CLMBr:%d\n",
		__func__,
		atc2609a_clmt_get_remain_clmb(clmt));
	pr_info("[%s] CLMBa:%d\n",
		__func__,
		atc2609a_clmt_get_avail_clmb(clmt));
	pr_info("[%s] SOCr:%d\n",
		__func__,
		atc2609a_clmt_get_rsoc(clmt));
	pr_info("[%s] SOCr(CALC+):%d\n",
		__func__,  clmt->soc_r_calc);
	pr_info("[%s] SOCr(BOOT+):%d\n",
		__func__,  clmt->soc_r_boot);
	pr_info("[%s] SOCd(+):%d\n",
		__func__,  atc2609a_clmt_get_delta_soc_ch(clmt));
	pr_info("[%s] SOCa:%d\n",
		__func__,  clmt->soc_a);
	pr_info("[%s] SOCa(CV+):%d\n",
		__func__,  clmt->soc_a_slope);
	pr_info("[%s] SOCocv(+):%d\n",
		__func__,  clmt->soc_ocv);
	pr_info("[%s] SOCreal:%d\n",
		__func__,  clmt->soc_real);
	pr_info("[%s] liner: K(%d) B(%d) X(%dmA) Y(%d) INITED(%d)\n",
		__func__,
		clmt->liner.k,
		clmt->liner.b,
		clmt->liner.x,
		clmt->liner.y,
		clmt->liner.inited);
	atc260x_pstore_get(clmt->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_CAP, &soc);
	pr_info("[%s] SOCst:%d\n", __func__, soc);
	pr_info("[%s] FCC:%d\n", __func__,
		atc2609a_clmt_get_fcc(clmt));
	pr_info("[%s] SOCf:%d\n", __func__,
		clmt->soc_filtered);
	pr_info("[%s] SOCl(-):%d\n", __func__,
		clmt->soc_dis_liner);
	pr_info("[%s] SOCfl(-):%d\n", __func__,
		clmt->soc_filtered_liner);
	pr_info("[%s] SOCcur:%d\n", __func__,
		clmt->soc_cur);
	pr_info("[%s] SOC:%d\n", __func__,
		clmt->soc_show);
	pr_info("[%s] POLL TM:%dms\n",
		__func__,  battery->interval);
	pr_info("[%s] ONLINE:%d\n", __func__,  battery->online);
	pr_info("[%s] CH_TYPE:%d\n", __func__,  battery->chg_type);
	pr_info("[%s] TEMPRATURE:%dC\n", __func__,  battery->bat_temp);
	if (battery->health == BAT_SHORTING)
		pr_info("[%s] HEALTH:SHORTING \n", __func__);
	else if (battery->health == BAT_ABNORMAL_EVER)
		pr_info("[%s] HEALTH:ABNORMAL EVER\n", __func__);
	else
		pr_info("[%s] HEALTH:NORMAL\n", __func__);

	pr_info("[%s] BOOT CAP THRESHOLD:%d%%\n",
		__func__, items->boot_cap_threshold);
	pr_info("[%s] S2 CONSUME:%duA\n", __func__,
		clmt->s2_consume_ua);
}

static enum power_supply_property atc2609a_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP,
};

static int atc2609a_bat_get_props(struct power_supply *psy,
		   enum power_supply_property psp,
		   union power_supply_propval *val)
{
	struct atc2609a_battery *battery = dev_get_drvdata(psy->dev->parent);
	struct atc2609a_clmt *clmt = battery->clmt;
	int ret = 0;

	if (just_power_on)
		wait_for_completion(&battery->bat_complete);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = atc2609a_bat_check_status(battery, &val->intval);
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery->online;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = battery->online;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = battery->items.ov_protect;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = measure_voltage_avr() * 1000;/*mV->uV*/
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = measure_current_avr() * 1000;/*mA->uA*/
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (battery->health != BAT_SHORTING)
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		else
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = atc2609a_bat_check_charge_type(battery, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		if (battery->health == BAT_SHORTING)
			val->intval = -99;
		else
			val->intval = clmt->soc_show;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery->bat_temp * 10;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * get_system_tick_ms - get system time
 * @return: system time in ms
 */
static unsigned long get_sys_tick_ms(void)
{

	struct timeval current_tick;
	long tick;

	do_gettimeofday(&current_tick);
	tick = current_tick.tv_sec * 1000 +
		current_tick.tv_usec / 1000;

	return tick;
}

static void atc2609a_clmt_study_s2_consume(struct atc2609a_clmt *clmt)
{
	/*estimate the power consumption of suspend*/
	clmt->resume_delta_clmb =
		atc2609a_clmt_get_delta_clmb(clmt);
	/*unit:uA*/
	clmt->s2_consume_ua =
		(clmt->resume_delta_clmb - clmt->suspend_delta_clmb) * 1000 /
		(suspend_interval_ms / 3600);
	pr_info("[%s] s2_consume_ua=(resume_delta_clmb-suspend_delta_clmb)1000/"
		"(suspend_ms/3600)=(%d-%d)/(%ld/3600)=%d\n",
		__func__,
		clmt->resume_delta_clmb, clmt->suspend_delta_clmb,
		suspend_interval_ms, clmt->s2_consume_ua);

	if (clmt->s2_consume_ua > 25000)
		clmt->s2_consume_ua = 25000;
	if (clmt->s2_consume_ua < 5000)
		clmt->s2_consume_ua = 5000;
}

static int calc_soc_weight_avr(int soc)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	int data[SOC_WEIGHT_FIFO_NUM];
	int discard;
	int ret;
	int sum = 0;
	int i = 0;

	if (kfifo_is_full(&clmt->fifo)) {
		ret = kfifo_out(&clmt->fifo, &discard, sizeof(int));
		kfifo_in(&clmt->fifo, &soc, sizeof(int));
		ret = kfifo_out_peek(&clmt->fifo,
			data, 5 * sizeof(int));
		while (i < SOC_WEIGHT_FIFO_NUM) {
			pr_debug("[%s] data[%d]:%d\n",
				__func__, i, data[i]);
			sum += data[i];
			i++;
		}

		return sum / i;
	} else {
		kfifo_in(&clmt->fifo, &soc, sizeof(int));
		return soc;
	}

}

static void  atc2609a_bat_commit_state(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	clmt->soc_pre = clmt->soc_show;
	just_power_on = false;
}

/*
 * atc2609a_bat_report_state - report battery state to user.
 */
static void  atc2609a_bat_report_state(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	bool changed;

	changed = clmt->soc_pre != clmt->soc_show;

	if (changed || just_power_on ||
		clmt->soc_show == 0 ||
		battery->bat_temp > 55) {
		power_supply_changed(&battery->battery);
	}
}


/*
 * atc2609a_bat_generate_interval - generate the next pool interval
 */
static int atc2609a_bat_generate_interval(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	struct bat_dts_items *items = &battery->items;
	/*during charging or idle*/
	if (battery->charging != POWER_SUPPLY_STATUS_DISCHARGING) {
		battery->interval = 30 * 1000;
		return battery->interval;
	}
	/*during discharging*/
	if (battery->bat_vol > items->taper_vol)
		if (clmt->soc_show == 100)
			battery->interval = 60 * 1000;
		else if (clmt->soc_show == 99)
			battery->interval = 5 * 1000;
		else
			battery->interval = 10 * 1000;
	else if (battery->bat_vol > items->term_vol1)
		battery->interval = 30 * 1000;
	else if (battery->bat_vol > items->term_vol + 100)
		battery->interval = 5 * 1000;
	else if (battery->bat_vol > items->term_vol + 50)
		battery->interval = 1 * 1000;
	else if (battery->bat_vol > items->term_vol)
		battery->interval = 500;
	else
		battery->interval = 100;

	pr_debug("[%s]:bat_vol(%d), interval(%dms)\n",
		__func__, battery->bat_vol, battery->interval);

	return battery->interval;
}


/**
 * atc2609a_bat_post_process - battery post process.
 */
static void atc2609a_bat_post_process(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	struct bat_dts_items *items = &battery->items;

	/*soc boundery handle*/
	if (clmt->soc_cur  > FULL_CHARGE_SOC)
		clmt->soc_cur = FULL_CHARGE_SOC;
	else if (clmt->soc_cur < EMPTY_CHARGE_SOC)
		clmt->soc_cur = EMPTY_CHARGE_SOC;

	clmt->soc_show = clmt->soc_cur / 1000;

	/*save soc to pmu reg*/
	atc260x_pstore_set(battery->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_CAP, clmt->soc_cur);

	atc2609a_bat_update_phrase(battery);

	if (items->print_switch)
		atc2609a_bat_dump(battery);

	if (log_switch)
		battery_info_store(battery);

}

static void bat_common_handle(struct atc2609a_battery *battery)
{
	atc2609a_bat_post_process(battery);
	atc2609a_bat_generate_interval(battery);
	atc2609a_bat_report_state(battery);
	atc2609a_bat_commit_state(battery);
}

/**
 * calc_base_step - calc reduced soc during poll interval
 */
static int calc_base_step(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	int data;
	int step;

	/* calculate the entire battery duration time(s)
	  * according to current system consumption
	  */
	if (battery->bat_cur < 0)
		data = -battery->bat_cur;
	else if (battery->bat_cur > 0)
		data = battery->bat_cur;
	else
		return 0;

	step = (atc2609a_clmt_get_fcc(clmt) * 3600) / data;
	pr_debug("[%s] all_time=Qmax*3600/bat_cur=%d*3600/%d=%d\n",
		__func__, atc2609a_clmt_get_fcc(clmt), data, step);
	/*calc reduced soc during poll interval*/
	step = FULL_CHARGE_SOC / 1000 * battery->interval / step;
	pr_debug("[%s] soc_per_sec=FULL_CHARGE_SOC/1000*interval/all_time="
		"%d*%d/all_time=%d\n",
		__func__, FULL_CHARGE_SOC, battery->interval, step);

	return step;
}

/**
 * calc_step_for_charge_cv - calc up step during charge interval.
 */
static int calc_step_for_charge_cv(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int step_up = 0;
	struct liner liner;
	int soc;

	if (detect_full) {
		step_up = 500;
		goto out;
	}

	pr_debug("[%s] (c1,sf1)=(%d,%d),(c2,sf2)=(%d,%d)\n",
		__func__,
		battery->bat_cur_last,
		clmt->soc_filtered_last,
		battery->bat_cur,
		clmt->soc_filtered);

	if (!clmt->soc_filtered_last ||
		!battery->bat_cur_last) {
		clmt->soc_filtered_last = clmt->soc_filtered;
		battery->bat_cur_last = battery->bat_cur;
		goto out;
	}

	if (battery->bat_cur_last == battery->bat_cur) {
		step_up = (clmt->soc_filtered - clmt->soc_filtered_last) *
			(clmt->soc_filtered - clmt->soc_filtered_last) /
			(clmt->soc_filtered - clmt->soc_cur);
		goto out;
	}
	/*calc k,b for soc filterted line*/
	atc2609a_clmt_calc_liner(&liner,
		battery->bat_cur_last,
		clmt->soc_filtered_last,
		battery->bat_cur,
		clmt->soc_filtered);
	pr_debug("[%s] soc_filtered line:(c1,sf1)=(%d,%d),(c2,sf2)=(%d,%d),k(%d),b(%d)\n",
		__func__,
		battery->bat_cur_last,
		clmt->soc_filtered_last,
		battery->bat_cur,
		clmt->soc_filtered,
		liner.k,
		liner.b);

	soc = liner.k * min(items->taper_cur, battery->bat_cur)
		+ liner.b;
	if (soc > FULL_CHARGE_SOC - 1)
		soc = FULL_CHARGE_SOC - 1;

	/*calc k,b for soc_cur line*/
	atc2609a_clmt_calc_liner(&liner,
		battery->bat_cur_last,
		clmt->soc_cur,
		min(items->taper_cur, battery->bat_cur),
		soc);
	pr_debug("[%s] soc_cur line:(c1,s1)=(%d,%d),(c2,s2)=(%d,%d),k(%d),b(%d)\n",
		__func__,
		battery->bat_cur_last,
		clmt->soc_cur,
		min(items->taper_cur, battery->bat_cur),
		soc,
		liner.k,
		liner.b);

	soc = liner.k * battery->bat_cur + liner.b;
	step_up = soc - clmt->soc_cur;
	pr_debug("[%s] step_up=soc-soc_cur=%d-%d=%d\n",
		__func__, soc, clmt->soc_cur, step_up);
out:
	if (step_up > 500)
		step_up = 500;
	if (step_up < 0)
		step_up = 0;
	clmt->soc_filtered_last = clmt->soc_filtered;
	battery->bat_cur_last = battery->bat_cur;
	return step_up;

}

/**
 * calc_step_for_charge_cc - calc up step during charge interval.
 */
static int calc_step_for_charge_cc(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int step_up = 0;
	struct liner liner;
	int soc;

	if (detect_full) {
		step_up = 500;
		goto out;
	}

	pr_debug("[%s] (v1,sf1)=(%d,%d),(v2,sf2)=(%d,%d)\n",
		__func__,
		battery->bat_vol_last,
		clmt->soc_filtered_last,
		battery->bat_vol,
		clmt->soc_filtered);

	if (!clmt->soc_filtered_last ||
		!battery->bat_vol_last) {
		clmt->soc_filtered_last = clmt->soc_filtered;
		battery->bat_vol_last = battery->bat_vol;
		goto out;
	}

	if (battery->bat_vol_last == battery->bat_vol) {
		step_up = (clmt->soc_filtered - clmt->soc_filtered_last) *
			(clmt->soc_filtered - clmt->soc_filtered_last) /
			(clmt->soc_filtered - clmt->soc_cur);
		goto out;
	}

	/*calc k,b for soc filterted line*/
	atc2609a_clmt_calc_liner(&liner,
		battery->bat_vol_last,
		clmt->soc_filtered_last,
		battery->bat_vol,
		clmt->soc_filtered);
	pr_debug("[%s] soc_filtered line:(v1,sf1)=(%d,%d),(v2,sf2)=(%d,%d),k(%d),b(%d)\n",
		__func__,
		battery->bat_vol_last,
		clmt->soc_filtered_last,
		battery->bat_vol,
		clmt->soc_filtered,
		liner.k,
		liner.b);
	soc = liner.k * max(items->taper_vol, battery->bat_vol)
		+ liner.b;
	if (soc > FULL_CHARGE_SOC)
		soc = FULL_CHARGE_SOC;

	/*calc k,b for soc_cur line*/
	atc2609a_clmt_calc_liner(&liner,
		battery->bat_vol_last,
		clmt->soc_cur,
		max(items->taper_vol, battery->bat_vol),
		soc);
	pr_debug("[%s] soc_cur line:(v1,s1)=(%d,%d),(v2,s2)=(%d,%d),k(%d),b(%d)\n",
		__func__,
		battery->bat_vol_last,
		clmt->soc_cur,
		max(items->taper_vol, battery->bat_vol),
		soc,
		liner.k,
		liner.b);

	soc = liner.k * battery->bat_vol + liner.b;
	step_up = soc - clmt->soc_cur;
	pr_debug("[%s] step_up=soc-soc_cur=%d-%d=%d\n",
		__func__, soc, clmt->soc_cur, step_up);

out:
	if (step_up > 500)
		step_up = 500;
	if (step_up < 0)
		step_up = 0;
	clmt->soc_filtered_last = clmt->soc_filtered;
	battery->bat_vol_last = battery->bat_vol;
	return step_up;
}

static int calc_step_for_charge(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	int cc;

	if (atc260x_charger_check_capacity() &&
		atc260x_charger_set_cc_finished()) {
		cc = atc260x_charger_get_cc();
		if ((battery->bat_cur <= cc - CHARGE_CV_CURRENT_THRESHOLD) &&
			(battery->bat_vol >= items->taper_vol))
			return calc_step_for_charge_cv(battery);
	}

	return calc_step_for_charge_cc(battery);
}

/**
  * soc_filter_for_charge_cc - soc filter for charge during constant current.
  */
static int soc_filter_for_charge_cc(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int bat_vol = battery->bat_vol;
	int delta_soc;

	delta_soc = atc2609a_clmt_get_delta_soc_ch(clmt);
	if (delta_soc >= 0) {
		/*
		 * SOCr could touch the ceiling, so we use saved SOCboot+SOCdelta
		 */
		clmt->soc_r_calc =
			(clmt->soc_r_boot + delta_soc) * 80 / 100;
		pr_debug("[%s] soc_real=soc_r_calc=(soc_r_boot+delta_soc)*80/100"
			"=(%d+%d)*80/100=%d\n",
			__func__,
			clmt->soc_r_boot, delta_soc, clmt->soc_r_calc);
		clmt->soc_real = clmt->soc_r_calc;
	} else {
		clmt->soc_ocv = atc2609a_clmt_get_asoc_lookup(battery);
		clmt->soc_real = clmt->soc_ocv;
		pr_debug("[%s] soc_real=soc_ocv=%d\n",
			__func__, clmt->soc_ocv);
	}

	if (bat_vol > items->taper_vol) {
		clmt->soc_filtered = clmt->soc_real;
		pr_debug("[%s] batv>taper_vol, soc_filtered=soc_real=%d\n",
				__func__, clmt->soc_filtered);
	} else {
		clmt->weight = (bat_vol - items->term_vol1) * 1000 /
			(items->taper_vol - items->term_vol1);
		clmt->soc_weight =
			(clmt->soc_real * clmt->weight +
			clmt->soc_cur * (1000 - clmt->weight)) / 1000;
		pr_debug("[%s] batv<=taper_vol\n"
			"soc_weight=(soc_real*weight+"
			"soc_cur*(1000-weight))/1000\n"
			"=((%d*%d+%d*(1000-%d))/1000=%d\n",
			__func__,
			clmt->soc_real,
			clmt->weight,
			clmt->soc_cur,
			clmt->weight,
			clmt->soc_weight);

		clmt->soc_weight = calc_soc_weight_avr(clmt->soc_weight);
		clmt->soc_filtered = clmt->soc_weight;
		pr_debug("[%s] batv<tapter_vol, soc_filtered:%d\n",
			__func__,  clmt->soc_filtered);
	}

	return clmt->soc_filtered;
}

/**
  * soc_filter_for_charge_cv - soc filter for charge during constant voltage.
  */
static int soc_filter_for_charge_cv(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int bat_cur = battery->bat_cur;
	int cc;

	clmt->soc_a_slope = atc2609a_clmt_get_asoc_slope(battery);
	if (clmt->soc_a_slope >= 0) {
		clmt->soc_real = clmt->soc_a_slope;
		pr_debug("[%s] soc_real=soc_a_slope=%d\n",
			__func__, clmt->soc_a_slope);
	} else {
		clmt->soc_ocv = atc2609a_clmt_get_asoc_lookup(battery);
		clmt->soc_real = clmt->soc_ocv;
		pr_debug("[%s] soc_real=soc_ocv=%d\n",
			__func__, clmt->soc_ocv);
	}

	cc = atc260x_charger_get_cc();
	if (bat_cur < items->taper_cur) {
		clmt->soc_filtered = clmt->soc_real;
		pr_debug("[%s] bat_cur<taper_cur, soc_filtered=soc_real=%d\n",
			__func__, clmt->soc_filtered);
	} else if (bat_cur <= cc - CHARGE_CV_CURRENT_THRESHOLD) {
		clmt->weight = (bat_cur - items->taper_cur) * 1000 /
			(cc - 100 - items->taper_cur);
		clmt->soc_weight =
			(clmt->soc_real * (1000 - clmt->weight) +
			clmt->soc_cur * clmt->weight) / 1000;
		pr_debug("[%s] cc>bat_cur>taper_cur\n"
			 "soc_weight=(soc_real*(1000-weight)+soc_cur*weight)/1000\n"
			 "=(%d*(1000-%d)+%d*%d)/1000=%d\n",
			__func__,
			clmt->soc_real,
			clmt->weight,
			clmt->soc_cur,
			clmt->weight,
			clmt->soc_weight);

		clmt->soc_weight = calc_soc_weight_avr(clmt->soc_weight);
		clmt->soc_filtered = clmt->soc_weight;
		pr_debug("[%s] cc>bat_cur>taper_cur, soc_filtered:%d\n",
			__func__, clmt->soc_filtered);
	} else {
		clmt->soc_filtered = clmt->soc_real;
		pr_debug("[%s] bat_cur>cc, soc_filtered=soc_real=%d\n",
			__func__, clmt->soc_filtered);
	}

	return clmt->soc_filtered;
}

static int soc_filter_for_charge(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	struct bat_dts_items *items = &battery->items;
	int cc;

	if (atc260x_charger_check_capacity() &&
		atc260x_charger_set_cc_finished()) {
		cc = atc260x_charger_get_cc();
		if ((battery->bat_cur <= cc - CHARGE_CV_CURRENT_THRESHOLD) &&
			(battery->bat_vol >= items->taper_vol))
			return soc_filter_for_charge_cv(battery);
	}

	/*when constant current stage, need calc slope again*/
	clmt->liner.inited = 0;

	return soc_filter_for_charge_cc(battery);
}

/**
 * soc_grow_up only happens during charging,including:
 * 1.detect bat is full, then soc grow up to 100 straightly;
 * 2.detect bat is not full, then soc grow up normally, but to 99 only;
 */
static void soc_grow_up(struct atc2609a_battery *battery, int step)
{
	struct atc2609a_clmt *clmt = battery->clmt;

	/*
	* Soc grow up to 99999 only.
	* Don't do such, we could see hop from 100 to 99 when resume
	* and battery is charging.
	*/
	if (clmt->soc_cur >= FULL_CHARGE_SOC - 1)
		return;

	/*
	* if detect bat  full, then soc grow up to 100 straightly;
	* detect bat is not full, then soc grow up normally, but up to 99 only;
	*/
	clmt->soc_cur += step;
	if (!detect_full) {
		if (clmt->soc_cur >= FULL_CHARGE_SOC)
			clmt->soc_cur = FULL_CHARGE_SOC - 1;
	}
	pr_debug("[%s] soc_cur(%d)\n", __func__, clmt->soc_cur);

	return;
}

/**
 * calc_step_for_discharge - calc drop step during discharge interval.
 */
static int calc_step_for_discharge(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int bat_vol = battery->bat_vol;
	int step_down = 0;

	if (bat_vol <= items->term_vol) {
		battery->low_pwr_cnt++;
		if (battery->low_pwr_cnt >= 5) {
			battery->low_pwr_cnt = 0;
			step_down = clmt->soc_cur;
			if (clmt->soc_cur >= 3000)
				battery->big_current_shutdown = true;
			pr_debug("[%s] bat low power,step_down==soc_cur\n",
				__func__);
			goto out;
		}
	} else {
		battery->low_pwr_cnt = 0;
	}

	/*calc the down step during interval*/
	step_down = calc_base_step(battery);
	pr_debug("[%s] base step(%d)\n",
			__func__, step_down);

	/*calc step down according to bat voltage*/
	if (bat_vol > items->term_vol1) {
		if (clmt->soc_cur == FULL_CHARGE_SOC) {
			if (battery->interval_sum_disch < 60 * 1000) {
				battery->interval_sum_disch += battery->interval;
				step_down = 0;
				pr_debug("[%s] batv>Vterminal1\n"
				"soc_cur(100),step_down(%d),interval_sum_disch(%dms)\n",
				__func__,  step_down, battery->interval_sum_disch);
				goto out;
			} else
				battery->interval_sum_disch = 0;
		}

		if (clmt->soc_filtered < clmt->soc_cur) {
			if (step_down > 500)
				step_down = 500;
		} else {
			if (clmt->soc_filtered > clmt->soc_cur)
				step_down = step_down * 1000 /
					(clmt->soc_filtered - clmt->soc_cur);
			if (step_down > 100)
				step_down = 100;
		}
		pr_debug("[%s] batv>Vterminal1, step_down(%d)\n",
			__func__, step_down);

	} else {
		if (clmt->soc_filtered_liner < clmt->soc_cur) {
			step_down = clmt->soc_cur - clmt->soc_filtered_liner;
			if (step_down > 800)
				step_down = 800;
			pr_debug("[%s], Vterminal1>batv>Vterminal0,\n"
				"step_down=%d\n",
				__func__,
				step_down);
		} else {
			if (clmt->soc_filtered_liner > clmt->soc_cur)
				step_down  = step_down *
					abs (clmt->soc_weight - clmt->soc_weight_pre) /
					abs(clmt->soc_filtered_liner - clmt->soc_cur);

			pr_debug("[%s], Vterminal1>batv>Vterminal0,\n"
				"step_down=step_down*|soc_weight_pre-soc_weight|/"
				"|soc_filtered_liner -soc_cur|= \n"
				"step_down*|%d-%d|/|%d-%d|=%d\n",
				__func__,
				clmt->soc_weight_pre,
				clmt->soc_weight,
				clmt->soc_filtered_liner,
				clmt->soc_cur,
				step_down);
			clmt->soc_weight_pre = clmt->soc_weight;
			if (step_down > 100)
				step_down = 100;
			pr_debug("[%s], Vterminal1>batv>Vterminal0, step_down = %d\n",
				__func__,  step_down);
		}
	}

	/*avoid soc up and down when resume*/
	if (clmt->soc_filtered > clmt->soc_cur ||
		clmt->soc_filtered_liner > clmt->soc_cur) {
		battery->interval_sum_disch += battery->interval;
		if (battery->interval_sum_disch < 30 * 1000) {
			step_down = 0;
			pr_debug("[%s] avoid fake drop when resume,step_down(%d)\n",
				__func__, step_down);
		} else
			battery->interval_sum_disch = 0;
	}
out:
	return step_down;

}

/**
  * soc_filter_for_discharge - soc filter for discharge,weight for current soc and available soc.
  */
static int soc_filter_for_discharge(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int bat_vol = battery->bat_vol;

	if (bat_vol > items->term_vol1) {
		clmt->soc_r = atc2609a_clmt_get_rsoc(clmt);
		clmt->soc_real = clmt->soc_r;
		clmt->soc_filtered = clmt->soc_real - CONST_SOC_STOP;
		pr_debug("[%s], batv>Vterminal1, soc_filtered=%d-%d=%d\n",
				__func__,
				clmt->soc_real,  CONST_SOC_STOP,  clmt->soc_filtered);
		clmt->liner.inited = 0;
		return clmt->soc_filtered;
	} else {
		clmt->weight = (bat_vol - items->term_vol) * 1000 /
			(items->term_vol1 - items->term_vol);
		clmt->soc_a = atc2609a_clmt_get_asoc(clmt);
		clmt->soc_real = clmt->soc_a;
		clmt->soc_weight =
			(clmt->soc_cur * clmt->weight +
			clmt->soc_real * (1000 - clmt->weight)) / 1000;
		pr_debug("[%s], batv<Vterminal1,\n"
			"soc_weight=(soc_cur*weight+soc_real*(1000-weight))/1000\n"
			"=((%d*%d+%d*(1000-%d))/1000=%d\n",
			__func__,
			clmt->soc_cur,
			clmt->weight,
			clmt->soc_real,
			clmt->weight,
			clmt->soc_weight);

		clmt->soc_weight = calc_soc_weight_avr(clmt->soc_weight);
		clmt->soc_filtered = clmt->soc_weight;
		pr_debug("[%s], batv<Vterminal1, soc_filtered:%d, soc_cur:%d\n",
			__func__,  clmt->soc_filtered, clmt->soc_cur);

		if (!clmt->liner.inited) {
			atc2609a_clmt_calc_liner(&clmt->liner,
				battery->bat_vol,
				clmt->soc_cur,
				items->term_vol,
				0);
		}
		clmt->soc_dis_liner =
			clmt->liner.k * battery->bat_vol + clmt->liner.b;
		pr_debug("[%s], batv<Vterminal1\n"
			"soc_dis_liner=k*batv+b=%d*%d+%d=%d\n",
			__func__,
			clmt->liner.k,
			battery->bat_vol,
			clmt->liner.b,
			clmt->soc_dis_liner);
		if (clmt->soc_dis_liner < 0)
			clmt->soc_dis_liner = 0;

		clmt->soc_filtered_liner =
			(clmt->soc_filtered * clmt->weight +
			clmt->soc_dis_liner * (1000 - clmt->weight)) / 1000;
		pr_debug("[%s], batv<Vterminal1\n"
			"soc_filtered=(soc_filtered*weight+"
			"soc_dis_liner*(1000-weight))/1000\n"
			"=((soc_filtered*%d+%d*(1000-%d))/1000=%d\n",
			__func__,
			clmt->weight,
			clmt->soc_dis_liner,
			clmt->weight,
			clmt->soc_weight);
		if (clmt->soc_filtered_liner < 0)
			clmt->soc_filtered_liner = 0;

		return clmt->soc_filtered_liner;
	}
}
/*
* curve_smooth - curve smooth for chargeing ,dischaging and idele.
*
* idle-process handle the conditions as follows:
* 1.14s after boot, do nothing;
* 2.detect full launched, charger off, do nothing;
* 3.detect full launched, detect bat is full,
*   then soc grow up to 100 straightly;
* 4.detect full quit, battery is full, do nothing;
* 5.detect_full launched, but detect bat is not full and charger on,
*   then soc grow up to 100 straightly.
*/
static int curve_smooth(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;
	int step = 0;

	if (battery->charging == POWER_SUPPLY_STATUS_CHARGING) {
		soc_filter_for_charge(battery);
		step = calc_step_for_charge(battery);
		soc_grow_up(battery, step);
	} else if (battery->charging == POWER_SUPPLY_STATUS_DISCHARGING) {
		soc_filter_for_discharge(battery);
		step = calc_step_for_discharge(battery);
		clmt->soc_cur = clmt->soc_cur - step;
	} else {
		/*
		* during   idle:
		* 1.charger is on
		*  (1)if detect bat  full, then soc grow up to 100 straightly;
		*  (2)if detect bat not full
		*      a. adapter is not enough, grow up by ocv; TODO
		*      b. adapter is enough, but can't charge to full,
		*	grow up to 100.
		* 2.charger is off
		*      do nothing.
		*/
		if (detect_full) {
			if (clmt->soc_cur < FULL_CHARGE_SOC)
				clmt->soc_cur += 250;
			pr_debug("[%s] charging(%d) bat full,soc up to 100,soc_cur(%d)\n",
				__func__,
				battery->charging, clmt->soc_cur);
			return clmt->soc_cur;
		}

		if (!charger_off_force &&
			(battery->bat_vol >= items->taper_vol - 20) &&
			(clmt->soc_cur < FULL_CHARGE_SOC)) {
				clmt->soc_cur  += 500;
			pr_debug("[%s] charging(%d), can not charge to full,"
				"soc grow up to 100 \n"
				"soc_cur(%d)\n",
				__func__,
				battery->charging,
				clmt->soc_cur);
		}
	}

	return clmt->soc_cur;
}

/**
 * bat_detect_full - detect battery full state.
 */
static void bat_detect_full(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	int ocv;

	if (!detect_full_launch)
		return;

	if (charger_off_force)
		goto detect_ocv;

	battery->interval_sum_full += battery->interval;
	pr_debug("[%s] check charge current, interval_sum(%dms)\n",
		__func__, battery->interval_sum_full - battery->interval);
	if (battery->interval_sum_full >=
		(5 * 60 * 1000 + battery->interval)) {
		/*force charger off*/
		atc260x_charger_off_force();
		pr_debug("[%s] charger turn off\n", __func__);
		charger_off_force = true;
		/*update charging state for ohters later*/
		bat_update_base_state(battery);
		battery->interval_sum_full = 0;
	}

	return;

detect_ocv:
	ocv = measure_voltage_avr();
	if (ocv >= items->taper_vol) {
		battery->interval_sum_full += battery->interval;
		pr_debug("[%s] ocv(%d)>taper_vol,interval_sum_full(%d)\n",
			__func__,
			ocv, battery->interval_sum_full - battery->interval);
		if (battery->interval_sum_full >=
			(60 * 1000 + battery->interval)) {
			pr_info("[%s]:  bat is full charge\n", __func__);
			detect_full = true;
			battery->interval_sum_full = 0;
			/*
			* in order to prevent battery from over-discharging,
			* don't turn on charger at once.
			*/
			return;
		}
	} else {
		pr_debug("[%s] ocv(%d)<taper_vol, charger turn on\n",
			__func__, ocv);
		atc260x_charger_off_restore();
		charger_off_force = false;
		bat_update_base_state(battery);
		detect_full = false;
		battery->interval_sum_full = 0;
	}
}

/**
 * bat_detect_full_pre - detect whether battery is close to full.
 * if yes, then bat_detect_full launch.
 */
static void bat_detect_full_pre(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	struct bat_dts_items *items = &battery->items;

	if (battery->bat_vol >= items->taper_vol) {
		if (atc260x_charger_check_capacity()) {
			if (battery->bat_cur <= items->taper_cur) {
				battery->interval_sum_pre += battery->interval;
				pr_debug("[%s]charger power enough,"
					"bat_cur(%d), interval_sum_pre(%d)\n",
					__func__,
					battery->bat_cur,
					battery->interval_sum_pre - battery->interval);
			} else {
				battery->interval_sum_pre = 0;
			}
		} else {
			if (battery->bat_vol >= items->taper_vol + 20) {
				battery->interval_sum_pre += battery->interval;
				pr_debug("[%s]charger power not enough,"
					"bat_vol(%d),interval_sum_pre(%dms)\n",
					__func__,
					battery->bat_vol, battery->interval_sum_pre);
			} else {
				battery->interval_sum_pre = 0;
			}
		}
	}

	if ((battery->interval_sum_pre >= (60 * 1000 + battery->interval)) ||
		(clmt->soc_cur >= FULL_CHARGE_SOC - 1)) {
		battery->interval_sum_pre = 0;
		detect_full_launch = true;

		/*force charger off*/
		atc260x_charger_off_force();
		pr_debug("[%s] charger turn off\n", __func__);
		charger_off_force = true;
		/*update charging state for ohters later*/
		bat_update_base_state(battery);
	}
}

static void atc2609a_bat_charge_process(struct atc2609a_battery *battery)
{
	curve_smooth(battery);

	/*prepare enter into detecting full process*/
	if (!detect_full_launch)
		bat_detect_full_pre(battery);
}

static void atc2609a_bat_discharge_process(struct atc2609a_battery *battery)
{
	curve_smooth(battery);
}

/**
 * atc2609a_bat_idle_process - the process when battery is under no charge and
 *		no discharge.
*/
static void atc2609a_bat_idle_process(struct atc2609a_battery *battery)
{
	curve_smooth(battery);
}

static  int bat_calc_s2_consumed_soc(struct atc2609a_clmt *clmt)
{
	unsigned long clmb = 0;
	int fcc;
	int suspend_soc;

	/**
	 * clmt is full load during 1 hour after suspend, laterly,
	 * the comsume could be down.
	 * cosume unit:uAh
	 */
	if (suspend_interval_ms <= 3600 * 1000) {
		clmb = clmt->s2_consume_ua * suspend_interval_ms / 1000 / 3600;
		pr_debug("[%s] suspend interval less than 1 hour(%lduAh)\n",
			__func__, clmb);
	} else {
		clmb = clmt->s2_consume_ua * 1;
		pr_debug("[%s] suspend interval more than 1 hour,"
			"precious 1h:%lduAh\n",
			__func__, clmb);
		if (clmt->s2_consume_ua > clmt->clmt_consume_ua) {
			clmb = clmb +
			(clmt->s2_consume_ua - clmt->clmt_consume_ua) *
			(suspend_interval_ms - 3600 * 1000) / 1000 / 3600;
			pr_debug("[%s] clmb=clmb_1h+\n"
				 "(s2_consume_ua-clmt_consume_ua)*"
				 "suspend_interval_ms/1000/3600=\n"
				 "clmb_1h+(%d*-%d)*%ld/1000/3600=%ld\n",
				 __func__,
				 clmt->s2_consume_ua,
				 clmt->clmt_consume_ua,
				 suspend_interval_ms - 3600 * 1000,
				 clmb);
		}
	}

	fcc = atc2609a_clmt_get_fcc(clmt);
	/*
	*clmb unit:uAh, fcc unit:mAh, due to we  use soc*1000,
	*so not devided by 1000,here
	*/
	suspend_soc = clmb * 100 / fcc;
	pr_info("[%s] suspend soc : %d\n", __func__, suspend_soc);

	return suspend_soc;
}
/**
 * bat_detuct_consumed_soc - update soc if resume from suspend,
 * deducting the consumed soc during s2
 */
static int bat_deduct_consumed_soc(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	struct power_supply *psy;
	union power_supply_propval wall_propval;
	union power_supply_propval vbus_propval;
	int wakeup_flag;
	int soc_deduct;

	if (suspend_interval_ms <= 0) {
		suspend_interval_ms = 0;
		return 0;
	}

	/*
	* wall or usb is online, and corresponding wakeup flag is set which imply that
	* device was powered supply by battery during suspend.
	*/
	psy = power_supply_get_by_name("atc260x-wall");
	if (psy)
		psy->get_property(psy,
			POWER_SUPPLY_PROP_ONLINE, &wall_propval);
	psy = power_supply_get_by_name("atc260x-usb");
	if (psy)
		psy->get_property(psy,
			POWER_SUPPLY_PROP_ONLINE, &vbus_propval);
	if (wall_propval.intval || vbus_propval.intval) {
		wakeup_flag = owl_pm_wakeup_flag();
		if (!(wakeup_flag & OWL_PMIC_WAKEUP_SRC_ALARM) ||
			(wakeup_flag & OWL_PMIC_WAKEUP_SRC_ONOFF_SHORT) ||
			(wakeup_flag & OWL_PMIC_WAKEUP_SRC_ONOFF_LONG)) {
			/*if soc current is 100%, then keep*/
			if (clmt->soc_cur == FULL_CHARGE_SOC) {
				suspend_interval_ms = 0;
				return 0;
			}
		} else {
			suspend_interval_ms = 0;
			return 0;
		}
	}

	/*suspend consumption compensation*/
	soc_deduct = bat_calc_s2_consumed_soc(clmt);
	/*can not determind who supplied  power during suspend */
	if (!wall_propval.intval && !vbus_propval.intval)
		soc_deduct /= 2;
	if (clmt->soc_cur >= soc_deduct)
		clmt->soc_cur = clmt->soc_cur - soc_deduct;
	else
		clmt->soc_cur = 0;

	suspend_interval_ms = 0;

	return soc_deduct;
}

static int atc2609a_clmt_get_soc_boot(struct atc2609a_clmt *clmt)
{
	u32 soc;

	atc260x_pstore_get(clmt->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_CAP, &soc);
	pr_info("[%s] soc boot:%d\n", __func__, soc);

	return soc;
}

/**
 * bat_calc_soc_1st - calc soc when power on.
 */
static void bat_calc_soc_1st(struct atc2609a_clmt *clmt)
{
	clmt->soc_cur =
		atc2609a_clmt_get_soc_boot(clmt);
}

/**
 * bat_update_soc - update battery soc,including as follows:
 * 1.update soc if resume, deduct consumed soc during suspend;
 * 2.update soc during charge;
 * 3.update soc during discharge.
 */
static void bat_update_soc(struct atc2609a_battery *battery)
{
	if (just_power_on)
		return;
	/*deduct consumed soc during suspend*/
	if (bat_deduct_consumed_soc(battery))
		return;
	/*calc charge resistor*/
	if (battery->charging != POWER_SUPPLY_STATUS_DISCHARGING)
		atc2609a_clmt_calc_ch_r(battery);
	/*calc ocv*/
	atc2609a_bat_calc_ocv(battery);
	/*update soc normally*/
	if (battery->charging == POWER_SUPPLY_STATUS_CHARGING)
		atc2609a_bat_charge_process(battery);
	else if (battery->charging == POWER_SUPPLY_STATUS_DISCHARGING)
		atc2609a_bat_discharge_process(battery);
	else
		atc2609a_bat_idle_process(battery);
}

static void start_anew(struct atc2609a_battery *battery)
{
	struct atc2609a_clmt *clmt = battery->clmt;

	kfifo_reset(&clmt->fifo);
	battery->interval_sum_r = 0;
	battery->interval_sum_pre = 0;
	battery->interval_sum_full = 0;
	battery->interval_sum_disch = 0;
	clmt->liner.inited = 0;
	detect_full = false;
	detect_full_launch = false;
	clmt->delta_clmb_boot = atc2609a_clmt_get_delta_clmb(clmt);
	/*calc soc by ocv, which will be used to calc remain soc during cc*/
	atc2609a_bat_calc_ocv(battery);
	clmt->soc_r_boot =
		atc2609a_clmt_get_asoc_lookup(battery);
}

/**
 *  bat_update_base_state - detect if charge status changed or not.
*/
static void bat_update_base_state(struct atc2609a_battery *battery)
{
	int pre_status;

	/*Only here update battery current*/
	battery->bat_cur = measure_current_avr();
	battery->bat_vol = measure_voltage_avr();
	pre_status = battery->charging;
	battery->charging =
		atc2609a_bat_get_charging(battery->bat_cur);

	if (pre_status ^ battery->charging) {
		pr_info("[%s] charge state changed(%d->%d)\n",
			__func__, pre_status, battery->charging);
	/*
	* start anew only between idle and discharge or charge and discharge.
	*/
		if ((pre_status != POWER_SUPPLY_STATUS_DISCHARGING) &&
			(battery->charging != POWER_SUPPLY_STATUS_DISCHARGING)) {
			pr_info("[%s] idle<->charge don't start anew!\n",
				__func__);
			return;
		}
		start_anew(battery);
	}
}

/**
  * bat_short_handle - if battery is shortting, warn!!
  */
static void bat_short_handle(struct atc2609a_battery *battery)
{
	if (battery->health == BAT_SHORTING)
		pr_warn("[%s] battery is shorting\n", __func__);
}

/**
 * atc2609a_bat_poll_work - Periodic work for the algorithm
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for the capacity update algorithm
 */
static void atc2609a_bat_poll_work(struct work_struct *work)
{
	struct atc2609a_battery *battery =
		container_of(work, struct atc2609a_battery, poll_work.work);

	bat_short_handle(battery);
	if (battery->health == BAT_SHORTING)
		goto out;

	bat_update_base_state(battery);
	bat_update_soc(battery);
	bat_detect_full(battery);

out:
	bat_common_handle(battery);
	BUG_ON(unlikely((battery->interval < 0) || (battery->interval > 600000)));
	queue_delayed_work(battery->wq,
		&battery->poll_work, msecs_to_jiffies(battery->interval));
}

static ssize_t show_dump(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct atc2609a_battery *battery =
		container_of(psy, struct atc2609a_battery, battery);

	atc2609a_bat_dump(battery);

	return 0;
}
static ssize_t store_dump(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t show_note(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	printk("%s\n", note);

	return 0;
}
static ssize_t store_note(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}

static ssize_t show_ocv_table(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct atc2609a_clmt *clmt = battery->clmt;
	atc2609a_clmt_dump_ocv(clmt);
	return 0;
}
static ssize_t store_ocv_table(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct atc2609a_battery *battery =
		container_of(psy, struct atc2609a_battery, battery);
	struct atc2609a_clmt *clmt = battery->clmt;

	atc2609a_clmt_set_ocv_batch(clmt, bat_info);
	return count;
}

static ssize_t show_r_table(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct atc2609a_battery *battery =
		container_of(psy, struct atc2609a_battery, battery);
	struct atc2609a_clmt *clmt = battery->clmt;

	atc2609a_clmt_dump_r(clmt);
	return 0;
}
static ssize_t store_r_table(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct platform_device *pdev =
		(struct platform_device *)
		container_of(dev, struct platform_device, dev);
	struct atc2609a_battery *battery =
		(struct atc2609a_battery *)platform_get_drvdata(pdev);
	struct atc2609a_clmt *clmt = battery->clmt;

	atc2609a_clmt_set_r_batch(clmt);
	return count;
}

static ssize_t show_reset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}
static ssize_t store_reset(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct atc2609a_battery *battery =
		container_of(psy, struct atc2609a_battery, battery);
	struct atc2609a_clmt *clmt = battery->clmt;
	int options;
	int ret;

	ret = kstrtoint(buf, 0, &options);
	if (options & ATC2609A_CAP_GAUGE_RESET) {
		atc2609a_clmt_set_inited(clmt, false);
		atc2609a_clmt_enable(clmt, false);
	}

	return ret ? ret : count;

}

static ssize_t show_boot_cap_threshold(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		battery->items.boot_cap_threshold);
}

static ssize_t store_boot_cap_threshold(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}

static ssize_t show_bl_on_voltage(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		battery->items.bl_on_vol * 1000);
}

static ssize_t store_bl_on_voltage(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;

}

static ssize_t show_log_switch(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", log_switch);
}

static ssize_t store_log_switch(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 0, &log_switch);

	return ret ? ret : count;
}

static ssize_t show_test_kfifo(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_test_kfifo(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int data;
	int result;

	int ret = kstrtoint(buf, 0, &data);
	result = calc_soc_weight_avr(data);
	pr_info("result:%d\n", result);

	return ret ? ret : count;
}

static struct device_attribute battery_attrs[] = {
	__ATTR(note, S_IRUGO | S_IWUSR, show_note, store_note),
	__ATTR(ocv_table, S_IRUGO | S_IWUSR, show_ocv_table, store_ocv_table),
	__ATTR(r_table, S_IRUGO | S_IWUSR, show_r_table, store_r_table),
	__ATTR(reset, S_IRUGO | S_IWUSR, show_reset, store_reset),
	__ATTR(boot_cap_threshold, S_IRUGO | S_IWUSR,
			show_boot_cap_threshold, store_boot_cap_threshold),
	__ATTR(dump, S_IRUGO | S_IWUSR, show_dump, store_dump),
	__ATTR(bl_on_voltage, S_IRUGO | S_IWUSR,
			show_bl_on_voltage, store_bl_on_voltage),
	__ATTR(log_switch, S_IRUGO | S_IWUSR, show_log_switch, store_log_switch),
	__ATTR(test_kfifo, S_IRUGO | S_IWUSR, show_test_kfifo, store_test_kfifo),
};

static int atc2609a_bat_create_sysfs(struct device *dev)
{
	int r, t;

	for (t = 0; t < ARRAY_SIZE(battery_attrs); t++) {
		r = device_create_file(dev, &battery_attrs[t]);

		if (r) {
			dev_err(dev, "failed to create sysfs file\n");
			return r;
		}
	}
	return 0;
}

static void atc2609a_bat_remove_sysfs(struct device *dev)
{
	int  t;

	for (t = 0; t < ARRAY_SIZE(battery_attrs); t++)
		device_remove_file(dev, &battery_attrs[t]);
}

static int atc2609a_bat_halt_notify(struct notifier_block *nb,
		unsigned long event, void *buf)
{
	switch (event) {
	case SYS_RESTART:
	case SYS_HALT:
	case SYS_POWER_OFF:
		pr_info("[%s] enter\n", __func__);
		cancel_delayed_work_sync(&battery->init_work);
		cancel_delayed_work_sync(&battery->poll_work);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block atc2609a_bat_halt_notifier = {
	.notifier_call = atc2609a_bat_halt_notify,
};

static void atc2609a_clmt_init(struct atc2609a_clmt *clmt)
{
	struct bat_dts_items *items = &battery->items;
	int ocv;

	atc2609a_clmt_enable(clmt, true);

	if (!atc2609a_clmt_get_inited(clmt->atc260x)) {
		pr_info("[%s], atc260x clmt not initialization\n", __func__);

		/*if CLMT_OCV_TABLE is not inited during boot, then init*/
		ocv = atc2609a_clmt_get_ocv(clmt->atc260x, 100);
		atc2609a_clmt_set_ocv_batch(clmt, bat_info);
		atc2609a_clmt_set_r_batch(clmt);
		atc2609a_clmt_set_fcc(clmt, items->design_capacity);
		atc2609a_clmt_set_s3a_steady_time(clmt, PMU_CLMT_CTL0_TIMER_1H);
		atc2609a_clmt_set_stop_v(clmt, items->term_vol);
		atc2609a_clmt_set_inited(clmt, true);
	}

	/*if battery is ablmal ever, need update ocv-soc table to normal value*/
	if (battery->health == BAT_ABNORMAL_EVER) {
		atc2609a_clmt_set_ocv_batch(clmt, bat_info);
		battery->health = BAT_NORMAL;
	}
	/*get delta clmb when boot*/
	clmt->delta_clmb_boot = atc2609a_clmt_get_delta_clmb(clmt);
	/*get boot soc*/
	bat_calc_soc_1st(clmt);
}

static void  atc2609a_battery_init(struct atc2609a_battery *battery)
{
	struct bat_dts_items *items = &battery->items;
	struct atc2609a_clmt *clmt = battery->clmt;

	/*clear last shutdown time*/
	atc260x_pstore_set(battery->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_SHDWN_TIME, 0);

	/*
	 * clear SHDWN_NOT_DEEP flag,
	 * in order to enter s4 when shutdown except upgrade
	 */
	atc260x_pstore_set(battery->atc260x,
		ATC260X_PSTORE_TAG_SHDWN_NOT_DEEP, 0);
	/*init clmt consume during s2*/
	clmt->clmt_consume_ua = CLMT_PRE_AMPLIFIER_COMSUMP_UA
		+ CLMT_ADC_COMSUMP_UA;
	/*
	* init  sum consume  during s2, if s2_consume_ua is 0,
	* indicate s2 consume has not studied, use dts cfg value.
	*/
	atc260x_pstore_get(battery->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_S2_CONSUMP, &clmt->s2_consume_ua);
	clmt->s2_consume_ua *= 1000;
	if (clmt->s2_consume_ua) {
		clmt->s2_consume_studied = true;
	} else {
		clmt->s2_consume_ua =
			items->sleep_current +
			clmt->clmt_consume_ua;
	}
	/*init charge resistor of battery,unit:mohm*/
	battery->bat_ch_r = 150;

#ifdef S2_CONSUME_STUDY_ENABLE

#else
	clmt->s2_consume_studied = true;
#endif
	/*calc soc by ocv, which will be used to calc remain soc during cc*/
	battery->bat_vol = measure_voltage_avr();
	atc2609a_bat_calc_ocv(battery);
	clmt->soc_r_boot = atc2609a_clmt_get_asoc_lookup(battery);
	/*other init*/
	log_switch = items->log_switch;
	just_power_on = true;

	atc2609a_clmt_init(clmt);
}

static void atc2609a_bat_init_work(struct work_struct *work)
{
	struct atc2609a_battery *battery =
		container_of(work, struct atc2609a_battery, init_work.work);
	battery->online = atc260x_chk_bat_online_intermeddle()<=0 ? 0 : 1;
	complete_all(&battery->bat_complete);
	if (!battery->online)
		return;

	atc2609a_bat_check_health(battery);
	atc2609a_battery_init(battery);
	power_supply_changed(&battery->battery);
	queue_delayed_work(battery->wq, &battery->poll_work, 0 * HZ);
}

static  int atc2609a_bat_probe(struct platform_device *pdev)
{
	struct atc260x_dev *atc260x = dev_get_drvdata(pdev->dev.parent);
	struct atc2609a_clmt *clmt;
	struct device_node *node;
	int ret = 0;

	battery = devm_kzalloc(&pdev->dev,
		sizeof(struct atc2609a_battery), GFP_KERNEL);
	if (!battery) {
		pr_err("[%s] devm_kzalloc battery error\n", __func__);
		return -ENOMEM;
	}

	clmt = devm_kzalloc(&pdev->dev,
		sizeof(struct atc2609a_clmt), GFP_KERNEL);
	if (!clmt) {
		pr_err("[%s] devm_kzalloc clmt error\n", __func__);
		return -ENOMEM;
	}

	ret =  kfifo_alloc(&clmt->fifo,
		SOC_WEIGHT_FIFO_NUM * sizeof(int), GFP_KERNEL);
	if (ret) {
		pr_err("[%s]  kfifo_alloc error\n", __func__);
		return ret;
	}

	battery->atc260x = atc260x;
	battery->clmt = clmt;
	clmt->atc260x = atc260x;
	node = pdev->dev.of_node;
	platform_set_drvdata(pdev, battery);
	mutex_init(&battery->bat_mutex);
	init_completion(&battery->bat_complete);

	atc2609a_bat_cfg_init(node, &battery->items);

	/*init battery power supply*/
	battery->battery.name = "battery";
	battery->battery.use_for_apm = 1;
	battery->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->battery.properties = atc2609a_bat_props;
	battery->battery.num_properties = ARRAY_SIZE(atc2609a_bat_props);
	battery->battery.get_property = atc2609a_bat_get_props;
	ret = power_supply_register(&pdev->dev, &battery->battery);
	if (ret) {
		pr_err("[%s]:power_supply_register failed for bat\n",
			__func__);
		goto free_kfifo;
	}

	ret = atc2609a_bat_create_sysfs(&pdev->dev);
	if (ret)
		goto err_battery;

//	register_pm_notifier(&atc2609a_bat_pm_notifier);
	register_reboot_notifier(&atc2609a_bat_halt_notifier);

	battery->icm_channel = atc260x_auxadc_find_chan(atc260x, "ICM");
	battery->batv_channel = atc260x_auxadc_find_chan(atc260x, "BATV");
	battery->qmax_channel = atc260x_auxadc_find_chan(atc260x, "QMAX");
	battery->qrem_channel = atc260x_auxadc_find_chan(atc260x, "QREM");
	battery->qavi_channel = atc260x_auxadc_find_chan(atc260x, "QAVI");
	pr_info("[%s], icm(%d),batv(%d),qmax(%d),qrem(%d),qavi(%d)\n",
		__func__,
		battery->icm_channel,
		battery->batv_channel,
		battery->qmax_channel,
		battery->qrem_channel,
		battery->qavi_channel);

	battery->wq = create_singlethread_workqueue("atc2609a_bat_wq");
	if (battery->wq == NULL) {
		pr_err("[%s]:failed to create work queue\n", __func__);
		goto remove_sysfs;
	}

	INIT_DELAYED_WORK(&battery->init_work,
		atc2609a_bat_init_work);
	INIT_DELAYED_WORK(&battery->poll_work,
		atc2609a_bat_poll_work);
	queue_delayed_work(battery->wq, &battery->init_work, msecs_to_jiffies(2000));

	return 0;
remove_sysfs:
	atc2609a_bat_remove_sysfs(&pdev->dev);
err_battery:
	power_supply_unregister(&battery->battery);
free_kfifo:
	kfifo_free(&clmt->fifo);
	 return ret;
}

static  int atc2609a_bat_remove(struct platform_device *pdev)
{
	struct atc2609a_battery *battery = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&battery->init_work);
	cancel_delayed_work_sync(&battery->poll_work);
	destroy_workqueue(battery->wq);
	atc2609a_bat_remove_sysfs(&pdev->dev);
	power_supply_unregister(&battery->battery);

	return 0;
}

static int atc2609a_bat_prepare(struct device *dev)
{
	struct atc2609a_battery *battery = dev_get_drvdata(dev);
	struct atc2609a_clmt *clmt = battery->clmt;
	suspend_time_ms = get_sys_tick_ms();
	clmt->suspend_delta_clmb =
		atc2609a_clmt_get_delta_clmb(clmt);

	pr_err("[%s], soc_cur(%d),batv(%dmv)\n",
		__func__, clmt->soc_cur, battery->bat_vol);

	cancel_delayed_work_sync(&battery->init_work);
	cancel_delayed_work_sync(&battery->poll_work);

	return 0;
}

static int atc2609a_bat_suspend(struct device *dev)
{
	return 0;
}

static int atc2609a_bat_resume(struct device *dev)
{
	struct atc2609a_battery *battery = dev_get_drvdata(dev);
	struct atc2609a_clmt *clmt = battery->clmt;

	start_anew(battery);
	atc2609a_clmt_check_fcc(clmt);

	return 0;
}

static void atc2609a_bat_complete(struct device *dev)
{
	struct atc2609a_battery *battery = dev_get_drvdata(dev);
	struct atc2609a_clmt *clmt = battery->clmt;

	BUG_ON(unlikely(suspend_time_ms <= 0));
	suspend_interval_ms = get_sys_tick_ms() -
		suspend_time_ms;
	suspend_time_ms = 0;
	if (!clmt->s2_consume_studied) {
		if ((suspend_interval_ms >= 40 * 60 * 1000) &&
			(suspend_interval_ms <= 60 * 60 * 1000)) {
			atc2609a_clmt_study_s2_consume(clmt);
			atc260x_pstore_set(clmt->atc260x,
				ATC260X_PSTORE_TAG_GAUGE_S2_CONSUMP,
				clmt->s2_consume_ua / 1000);
			clmt->s2_consume_studied = true;
		}
	}
	pr_err("[%s], suspend_interval_ms(%ldms)\n",
		__func__, suspend_interval_ms);
	queue_delayed_work(battery->wq, &battery->poll_work, 0 * HZ);
}

static const struct dev_pm_ops atc2609a_bat_pm_ops = {
	.prepare       = atc2609a_bat_prepare,
	.suspend       = atc2609a_bat_suspend,
	.resume        = atc2609a_bat_resume,
	.complete     = atc2609a_bat_complete,
};

static void atc2609a_bat_shutdown(struct platform_device *pdev)
{
	struct atc2609a_battery *battery =
		(struct atc2609a_battery *)platform_get_drvdata(pdev);
	struct atc2609a_clmt *clmt = battery->clmt;
	struct atc260x_dev *atc260x = battery->atc260x;

	cancel_delayed_work_sync(&battery->init_work);
	cancel_delayed_work_sync(&battery->poll_work);
	destroy_workqueue(battery->wq);

	shutdown_time_ms = get_sys_tick_ms();
	atc260x_pstore_set(atc260x,
		ATC260X_PSTORE_TAG_GAUGE_SHDWN_TIME,
			shutdown_time_ms / 1000);
	if (battery->big_current_shutdown) {
		atc260x_pstore_set(battery->atc260x, ATC260X_PSTORE_TAG_GAUGE_CAP, BATTERY_INVALID_SOC);
		battery->big_current_shutdown = false;
	}
	/*disable clmt to enter s4 after shutdown*/
	atc2609a_clmt_set_inited(clmt, false);
	atc2609a_clmt_enable(clmt, false);
	pr_info("[%s] soc(%d%%), batv(%dmv),time(%lds)\n",
		__func__,
		clmt->soc_show, battery->bat_vol, shutdown_time_ms/1000);

	return;
}

static const struct of_device_id atc2609a_battery_match[] = {
	{ .compatible = "actions,atc2609a-battery", },
	{},
};
MODULE_DEVICE_TABLE(of, atc2609a_battery_match);

static struct platform_driver atc2609a_bat_driver = {
	.probe      = atc2609a_bat_probe,
	.remove     = atc2609a_bat_remove,
	.driver     = {
	    .name = "atc2609a-battery",
	    .pm   = &atc2609a_bat_pm_ops,
	    .of_match_table = of_match_ptr(atc2609a_battery_match),
	},
	.shutdown = atc2609a_bat_shutdown,
};

static int __init atc2609a_bat_init(void)
{
	struct device_node *node =
		of_find_compatible_node(NULL, NULL, "actions,atc2609a-battery");
	if (!node) {
		pr_info("%s fail to find atc2609a-battery node\n", __func__);
		return 0;
	}

	pr_info("atc2609a_battery:version(%s), time stamp(%s)\n",
		ATC2609A_BATTERY_DRV_VERSION, ATC2609A_BATTERY_DRV_TIMESTAMP);
	return platform_driver_register(&atc2609a_bat_driver);
}

static void __exit atc2609a_bat_exit(void)
{
	platform_driver_unregister(&atc2609a_bat_driver);
}

#ifdef MODULE
module_int(atc2609a_bat_init);
module_exit(atc2609a_bat_exit);
#else
late_initcall(atc2609a_bat_init);
#endif

/* Module information */
MODULE_AUTHOR("Actions Semi, Inc");
MODULE_DESCRIPTION("Battery driver for ATC2609A");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atc2609a-battery");