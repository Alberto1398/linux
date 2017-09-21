/*
 * atc260x_cap_gauge.c  --  fuel gauge  driver for ATC260X
 *
 * Copyright 2011 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
/* #define DEBUG */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/gpio.h>
#include <linux/kfifo.h>
#include <linux/rtc.h>
#include <linux/inotify.h>
#include <linux/suspend.h>
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <linux/owl_pm.h>
#include <asm/div64.h>

#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/mfd/atc260x/atc260x.h>

#ifdef DEBUG
#define GAUGE_DBG(...)			printk(KERN_ERR "ATC260X_GAUGE: " __VA_ARGS__)
#else
#define GAUGE_DBG(...)			do {} while (0)
#endif
#define ATC2603C_BATTERY_DRV_TIMESTAMP		("20160713173016")
#define ATC2603C_BATTERY_DRV_VERSION		"r5p2"

#define GAUGE_ERR(...)				printk(KERN_ERR "ATC260X_GAUGE: " __VA_ARGS__);
#define GAUGE_WARNING(...)		printk(KERN_WARNING "ATC260X_GAUGE: " __VA_ARGS__);
#define GAUGE_NOTICE(...)			printk(KERN_NOTICE "ATC260X_GAUGE: " __VA_ARGS__);
#define GAUGE_INFO(...)			printk(KERN_INFO "ATC260X_GAUGE: " __VA_ARGS__);

/* ATC2603C_PMU_CHARGER_CTL1 */
#define     PMU_CHARGER_CTL1_BAT_EXIST_EN           (1 << 5)
#define     PMU_CHARGER_CTL1_BAT_DT_OVER            (1 << 9)
#define     PMU_CHARGER_CTL1_BAT_EXIST              (1 << 10)
#define     PMU_CHARGER_CTL1_PHASE_SHIFT            (13)
#define     PMU_CHARGER_CTL1_PHASE_MASK             (0x3 << PMU_CHARGER_CTL1_PHASE_SHIFT)

#define ATC260X_BAT_DETECT_DELAY_MS		300

#define PMU_CHARGER_PHASE_PRECHARGE		(1)
#define PMU_CHARGER_PHASE_CONSTANT_CURRENT	(2)
#define PMU_CHARGER_PHASE_CONSTANT_VOLTAGE	(3)

/*current threshold*/
#define CHARGE_CURRENT_THRESHOLD			(60)/*ma*/
#define DISCHARGE_CURRENT_THRESHOLD		(30)/*ma*/

/*full charge, full discharge*/
#define FULL_CHARGE_SOC						(100000)
#define EMPTY_DISCHARGE_SOC				(0)

/*adc up and down float value*/
#define BAT_VOL_VARIANCE_THRESHOLD		(20)/*mv*/
#define BAT_CUR_VARIANCE_THRESHOLD		(100)/*ma*/

/*update resistor, batv range */
#define BATV_RESISTOR_MIN					(3600)/*mv*/
#define BATV_RESISTOR_MAX					(4000)/*mv*/

/*update resistor batv threshold value, when discharging*/
#define UPDATE_RESISTOR_VOL_DIFF_THRESHOLD		(70)/*mv*/

#define CONST_ROUNDING_500					(500)
#define CONST_ROUNDING						(1000)

/*added by cxj@2014-11-26:the max number of current or voltage samples*/
#define SAMPLES_COUNT				10

#define SOC_THRESHOLD 				1000
#define NOT_BEYOND_SOC_THRESHOLD_TIME	30

#define SAMPLE_OUT 1
#define SAMPLE_TIME 120
#define BATTERY_INVALID_SOC			(0xfffff)
#define TERMINAL_VOL_ADD 50
#define CHARGE_CV_CURRENT_THRESHOLD             (50)

/*the size of kfifo must be 2^n*/
#define SOC_WEIGHT_FIFO_NUM			(4)

extern bool atc260x_charger_check_capacity(void);
extern int atc260x_charger_set_cc_finished(void);

enum INFO_TYPE {
	CURRENT_TYPE,
	VOLTAGE_TYPE,
};

/**
 * dts_config_items - dts config items information.user can change these items to meet their
 *                            need.
 * @ capacity : nominal chemical capacity.
 * @ taper_vol : the voltage that is colsed to full charge.
 * @ taper_cur : the current that is colsed to full charge.
 * @ stop_vol : the minimum voltage that tablet works normally.
 * @ print_switch : whether if turn on print switch or not, if true, then print to screen every interval.
 * @ log_switch : whether if turn on log switch or not, if true the log will save in sdcard.
 */
struct dts_config_items {
	int capacity;
	int taper_vol;
	int taper_current;
	int terminal_vol;
	int terminal_vol1;
	int min_over_chg_protect_voltage;
	int suspend_current;
	int shutdown_current;
	int print_switch;
	int log_switch;
	int ocv_soc_config;
	int support_battery_temperature;
};

/**
 * data_group -  divide into  groups for gathered battery info, current/voltage.
 * @ index : the head index of every group.
 * @ num : the number of every group member.
 * @ count : the number of valid data within the group.
 * @ sum : the sum of all the data within the group.
 * @ avr_data : the average value of this group.
 *
 * data of every group have similar range, not excessive shaking.
 */
struct data_group {
	int index;
	int num;
	int count;
	int sum;
	int avr_data;
};

/**
  * kfifo_data - the data will store into kfifo.
  * @ bat_vol : battery voltage.
  * @ bat_curr : battery current.
  * @ timestamp : current time stamp.
  */
struct kfifo_data {
	int bat_vol;
	int bat_curr;
	struct timeval timestamp;
};

/**
 * atc260x_gauge_info - atc260x soft fuel gauge information
 *
 * @ atc260x : come from parent device, atc260x.
 * @ node : device node, in order to get property from dts file.
 * @ cfg_items : config items from dts file.
 * @ lock : prevent the concurrence for soc reading and soc writting.
 * @ firtst_product : whether if the first product.
 * @ ch_resistor : battery impedence, dc resistor mainly, under charging.
 * @ disch_resistor : battery impedence, dc resistor mainly, under discharging.
 * @ curr_buf : save gathered battery current, charge/discharge.
 * @ vol_buf : save gathered battery voltage, charge/discharge.
 * @ ibatt_avg : the average value of curr_buf.
 * @ vbatt_avg : the average value of vol_buf.
 * @ status : there are 3 charge status:CHARGING, DISCHARGING,
 *				NO_CHARGEING_NO_DISCHARGING.
 * @ online : whether battery is online/offline.
 * @ health : battery's health status.
 * @ chg_type : charger type, cc/cv.
 * @ bat_temp : battery temperature.
 * @ ocv : the open circut voltage of battery.
 * @ ocv_stop : the open circut voltage of battery, corresponding with terminal voltage.
 * @ soc_last : save last soc.
 * @ soc_now : current  state of charge.
 * @ soc_real : the calclated state of charge really.
 * @ soc_show : the state of charge showing in UI, substracting down_step by soc_now.
 * @ index : indicate the position of soc_queue.
 * @ wq : create work queue for atc260x fuel gauge only.
 * @ work : be responsibe to update battery capacity.
 * @ interval : delayed work poll time interval.
 */
struct atc260x_gauge_info {
	struct power_supply battery;
	struct atc260x_dev *atc260x;
	struct device_node *node;
	struct dts_config_items cfg_items;
	struct mutex lock;
	int store_gauge_info_switch_sav;
	int ch_resistor;
	int disch_resistor;
	bool dich_r_change;

	int curr_buf[SAMPLES_COUNT];
	int vol_buf[SAMPLES_COUNT];
	int ibatt_avg;
	int ibatt_last;
	int vbatt_avg;
	int vbatt_last;

	int status;
	int online;
	int health;
	int chg_type;
	int bat_temp;
	int pre_temp;
	int icm_channel;
	int batv_channel;
	int remcon_channel;
	int low_pwr_cnt;
	int ocv;
	int ocv_stop;

	struct kfifo fifo;
	struct kfifo weight_fifo;
	struct kfifo down_curve;
	struct kfifo temp_down_curve;

	int soc_last;
	int soc_now;
	int soc_real;
	int soc_show;
	int soc_pre;
	int soc_weight;
	int soc_transpoint;
	int weight;

	int i_average;
	int end_time;
	int discharge_clum;
	int real_capacity;
#ifdef GAUGE_SOC_AVERAGE
	int index;
	soc_queue[5];
#endif
	struct workqueue_struct *wq;
	struct delayed_work work;
	int interval;
	int (*filter_algorithm)(int *buf,  int len, int type);

};

int get_cap_gauge(void);

extern int atc260x_set_charger_current(int new, int *old);
extern void atc260x_charger_off_force(void);
extern void atc260x_charger_on_force(void);
extern int atc260x_charger_get_status(void);
extern int atc260x_charger_get_cc(void);
extern void atc260x_charger_set_cc_force(int cc);
extern void atc260x_charger_set_cc_restore(void);
extern int atc260x_get_charger_online_status(void);
extern int atc260x_chk_bat_online_intermeddle(void);
extern int owl_pm_wakeup_flag(void);

static struct atc260x_gauge_info *global_gauge_info_ptr = NULL;
static int first_store_gauge_info;
static int taper_interval;
static struct timeval current_tick;
static int ch_resistor_calced = 0;
/* for full power schedule*/
bool full_power_dealing = false;
bool full_power_flag = false;
int next_do_count = 0;
int cycle_count = 0;
/*for suspend consume*/
int pre_charge_status;
static int first_calc_resistor = -1;

/**
 *  ocv_soc_table : ocv soc mapping table
 */
static  int ocv_soc_table[][2] =
{
	{3477, 1}, {3534, 2}, {3591, 3}, {3624, 4}, {3637, 5},
	{3649, 6}, {3661, 7},{3667, 8}, {3673, 9},{3677, 10},
	{3682, 11}, {3685, 12}, {3690, 13}, {3693, 14}, {3700, 15},
	{3706, 16}, {3712, 17}, {3716, 18}, {3722, 19}, {3728, 20},
	{3732, 21}, {3736, 22}, {3739, 23}, {3744, 24}, {3747, 25},
	{3751, 26}, {3755, 27}, {3758, 28}, {3761, 29}, {3765, 30},
	{3768, 31}, {3771, 32}, {3775, 33}, {3777, 34}, {3782, 35},
	{3784, 36}, {3788, 37}, {3791, 38}, {3793, 39}, {3794, 40},
	{3800, 41}, {3801, 42}, {3804, 43}, {3807, 44}, {3812, 45},
	{3815, 46}, {3819, 47}, {3823, 48}, {3825, 49}, {3830, 50},
	{3834, 51}, {3838, 52}, {3841, 53}, {3845, 54}, {3850, 55},
	{3854, 56}, {3858, 57}, {3864, 58}, {3870, 59}, {3874, 60},
	{3880, 61}, {3889, 62}, {3895, 63}, {3902, 64}, {3908, 65},
	{3916, 66}, {3926, 67}, {3933, 68}, {3940, 69}, {3947, 70},
	{3954, 71}, {3961, 72}, {3968, 73}, {3972, 74}, {3979, 75},
	{3985, 76}, {3992, 77}, {3997, 78}, {4005, 79}, {4012, 80},
	{4019, 81}, {4028, 82}, {4036, 83}, {4046, 84}, {4054, 85},
	{4061, 86}, {4068, 87}, {4075, 88}, {4084, 89}, {4090, 90},
	{4099, 91}, {4107, 92}, {4115, 93}, {4126, 94}, {4132, 95},
	{4141, 96}, {4152, 97}, {4160, 98}, {4170, 99}, {4180, 100},
};

static const int remcon_adc_temp[] =
{
	0x5B, 0x5D, 0x5F, 0x61, 0x63, 0x65, 0x67, 0x6A, 0x6D, 0x71,
	0x74, 0x77, 0x7A, 0x7D, 0x81, 0x84, 0x87, 0x8A, 0x8D, 0x91,
	0x94, 0x97, 0x9A, 0x9D, 0xA1, 0xA4, 0xA9, 0xAD, 0xB3, 0xB8,
	0xBD, 0xC2, 0xC7, 0xCC, 0xD1, 0xD6, 0xDB, 0xE0, 0xE5, 0xEA,
	0xEF, 0xF4, 0xFA, 0x102, 0x108, 0x10E, 0x114, 0x11A, 0x122, 0x129,
	0x130, 0x137, 0x13E, 0x145, 0x14D, 0x154, 0x15B, 0x162, 0x16A, 0x172,
	0x17A, 0x182, 0x18A, 0x192, 0x19B, 0x1A4, 0x1AC, 0x1B5, 0x1BE, 0x1C7,
	0x1D0, 0x1D9, 0x1E2, 0x1EC, 0x1F6, 0x200, 0x20A, 0x214, 0x21E, 0x228,
	0x232,
};

#define TABLE_LEN 10
static int ocv_soc_table_config[100][2];
static unsigned int get_battery_temperature(void)
{
	int ret;
	int remcon = 0x232;
	int i;

	if (global_gauge_info_ptr->cfg_items.support_battery_temperature) {
		ret = atc260x_auxadc_get_translated(global_gauge_info_ptr->atc260x,
						global_gauge_info_ptr->remcon_channel, &remcon);
		if (ret) {
			GAUGE_ERR("get icm translated fail!!\n");
			return 0;
		}
	}
	for(i = 0; i < sizeof(remcon_adc_temp) / sizeof(int); i++) {
		if (remcon <= remcon_adc_temp[i])
			break;
	}

	return 100 - i;
}

static int ocv_soc_table_init(struct atc260x_gauge_info *info)
{
	int i, j;
	int ret;
	u32 ocv[TABLE_LEN];
	int config_items_count, item_number;
	char *config_node_name;
	char temp_item_number[3];
	char *src_item_number;
	temp_item_number[2] = '\0';

	for (config_items_count = 0; config_items_count < 10; config_items_count++) {
		char start_item_name[11] = "ocv_soc_";
		config_node_name = start_item_name;

		item_number = config_items_count*10;
		temp_item_number[0] = item_number/10+'0';
		temp_item_number[1] = item_number%10+'0';
		src_item_number = temp_item_number;

		config_node_name = strcat(start_item_name, src_item_number);

		ret = of_property_read_u32_array(info->node, config_node_name, ocv, TABLE_LEN);
		if (ret) {
			GAUGE_ERR("get ocv from dts fail!!\n");
			return 0;
		}
		for (i = config_items_count*10, j = 0; i < config_items_count * 10 + 10; i++,j++) {
			ocv_soc_table_config[i][0] = ocv[j];
			ocv_soc_table_config[i][1] = i+1;
			GAUGE_DBG("%d  %d\n", ocv_soc_table_config[i][0], ocv_soc_table_config[i][1]);
		}
	}
	info->cfg_items.ocv_soc_config = 1;
	return 1;
}

static int store_batt_info(struct atc260x_gauge_info *info)
{
	u8 buf[200];
	struct file *filp;
	mm_segment_t fs;
	int offset = 0;
	int h, m, s;
	struct timeval current_tick;
	static struct timeval last_tick = {0,0};
	int day_s;

	do_gettimeofday(&current_tick);
	day_s = current_tick.tv_sec % (3600*24);
	h = day_s / 3600;
	m = (day_s % 3600) / 60;
	s = day_s % 60;

	if (current_tick.tv_sec - last_tick.tv_sec < 9) {
		return 0;
	}
	last_tick.tv_sec = current_tick.tv_sec;

	fs = get_fs();
	set_fs(KERNEL_DS);

	filp = filp_open("/sdcard/cap_gauge_info.log", O_CREAT | O_RDWR, 0644);
	if (IS_ERR(filp)) {
		GAUGE_ERR("\n[cap_gauge] can't accessed sd card cap_gauge_info.log, exiting");
		return 0;
	}

	if (first_store_gauge_info == 1) {
		memset(buf, 0, 200);
		offset = sprintf(buf, "time,status,bat_v,bat_i,r_ch,r_disch,bat_ocv,soc_real,soc_now,soc_filter,soc_show,soc_ref\t\n");
		filp->f_op->llseek(filp, 0, SEEK_END);
		filp->f_op->write(filp, (char *)buf, offset + 1, &filp->f_pos);
		first_store_gauge_info = 0;
	}

	memset(buf, 0, 200);
	offset = sprintf(buf, "%02d:%02d:%02d,%d,%04d,%04d,%04d,%d,%d,%d,%d,%d\t\n",
	h, m, s,
	info->status,
	info->vbatt_avg,
	info->ibatt_avg,
	info->ch_resistor,
	info->disch_resistor,
	info->ocv,
	info->soc_real,
	info->soc_now,
	info->soc_show);

	filp->f_op->llseek(filp, 0, SEEK_END);
	filp->f_op->write(filp, (char *)buf, offset + 1, &filp->f_pos);
	set_fs(fs);
	filp_close(filp, NULL);

	return 0;
}

static int  get_cfg_items(struct atc260x_gauge_info *info)
{
	const __be32 *property;
	int len;

	/*capacity*/
	property = of_get_property(info->node, "capacity", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.capacity = be32_to_cpup(property);
		if (info->cfg_items.capacity < 0) {
			GAUGE_ERR("[%s] cfg_items.capacity =%d\n", __func__, info->cfg_items.capacity);
			return -EINVAL;
		}
	} else {
		GAUGE_ERR("[%s] cfg_items.capacity not config\n", __func__);
		return -EINVAL;
	}

	/*taper_vol*/
	property = of_get_property(info->node, "taper_voltage", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.taper_vol = be32_to_cpup(property);
		if (info->cfg_items.taper_vol < 0) {
			GAUGE_WARNING("cfg_items.taper_vol =%d\n", info->cfg_items.taper_vol);
			info->cfg_items.taper_vol = 4180;
		}
	} else
		GAUGE_WARNING("cfg_items.taper_vol not config\n");
	/*taper_current*/
	property = of_get_property(info->node, "taper_current", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.taper_current = be32_to_cpup(property);
		if (info->cfg_items.taper_current < 0) {
			GAUGE_WARNING("cfg_items.taper_current =%d\n", info->cfg_items.taper_current);
			info->cfg_items.taper_current = info->cfg_items.capacity * 5 / 100;/*0.05C*/
		}
	} else
		GAUGE_WARNING("cfg_items.taper_current not config\n");
	/*terminal_vol*/
	property = of_get_property(info->node, "terminal_voltage", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.terminal_vol = be32_to_cpup(property);
		if (info->cfg_items.terminal_vol < 0) {
			GAUGE_WARNING("cfg_items.terminal_vol =%d\n", info->cfg_items.terminal_vol);
			info->cfg_items.terminal_vol = 3450;
		}
	} else
		GAUGE_WARNING("cfg_items.terminal_vol not config\n");
	/*terminal_vol1*/
	property = of_get_property(info->node, "terminal_voltage1", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.terminal_vol1 = be32_to_cpup(property);
		if (info->cfg_items.terminal_vol1 < 0) {
			GAUGE_WARNING("cfg_items.terminal_vol1 =%d\n", info->cfg_items.terminal_vol1);
			info->cfg_items.terminal_vol1 = info->cfg_items.terminal_vol +200;
		}
	} else
		info->cfg_items.terminal_vol1 = info->cfg_items.terminal_vol +200;
	/* min_over_chg_protect_voltage */
	property = of_get_property(info->node, "min_over_chg_protect_voltage", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.min_over_chg_protect_voltage = be32_to_cpup(property);
		if (info->cfg_items.min_over_chg_protect_voltage < 0) {
			GAUGE_WARNING("cfg_items.min_over_chg_protect_voltage =%d\n", info->cfg_items.min_over_chg_protect_voltage);
			info->cfg_items.min_over_chg_protect_voltage = 4275;
		}
	} else
		GAUGE_WARNING("cfg_items.min_over_chg_protect_voltage not config\n");

	/* support_battery_temperature */
	property = of_get_property(info->node, "support_battery_temperature", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.support_battery_temperature = be32_to_cpup(property);
		if (info->cfg_items.support_battery_temperature < 0) {
			GAUGE_WARNING("cfg_items.support_battery_temperature =%d\n", info->cfg_items.support_battery_temperature);
			info->cfg_items.support_battery_temperature = 0;
		}
	} else {
		info->cfg_items.support_battery_temperature = 0;
		GAUGE_WARNING("cfg_items.min_over_chg_protect_voltage not config\n");
	}

	/* shutdown_current */
	property = of_get_property(info->node, "shutdown_current", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.shutdown_current = be32_to_cpup(property);
		if (info->cfg_items.shutdown_current < 0) {
			GAUGE_WARNING("cfg_items.shutdown_current =%d\n", info->cfg_items.shutdown_current);
			info->cfg_items.shutdown_current = 50;
		}
	} else
		GAUGE_WARNING("cfg_items.shutdown_current not config\n");
	/* suspend_current */
	property = of_get_property(info->node, "suspend_current", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.suspend_current = be32_to_cpup(property);
		if (info->cfg_items.suspend_current < 0) {
			GAUGE_WARNING("cfg_items.suspend_current =%d\n", info->cfg_items.suspend_current);
			info->cfg_items.suspend_current = 50;
		}
	} else
		GAUGE_WARNING("cfg_items.suspend_current not config\n");
	/*print_switch*/
	property = of_get_property(info->node, "print_switch", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.print_switch = be32_to_cpup(property);
		if (info->cfg_items.print_switch < 0) {
			GAUGE_WARNING("cfg_items.print_switch =%d \n", info->cfg_items.print_switch);
			info->cfg_items.print_switch = 0;
		}
	} else
		GAUGE_WARNING("cfg_items.print_switch not config\n");

	/*log_switch*/
	property = of_get_property(info->node, "log_switch", &len);
	if (property && len == sizeof(int)) {
		info->cfg_items.log_switch = be32_to_cpup(property);
		if (info->cfg_items.log_switch < 0) {
			GAUGE_WARNING("cfg_items.log_switch =%d \n", info->cfg_items.log_switch);
			info->cfg_items.log_switch = 0;
		}
	} else
		GAUGE_WARNING("cfg_items.log_switch not config\n");

	return 0;
}

static unsigned long get_time_hour(struct atc260x_gauge_info *info)
{
	struct timeval current_tick;
	unsigned long tick;

	do_gettimeofday(&current_tick);
	GAUGE_DBG("[%s]timeofday =%lu hours\n", __func__, current_tick.tv_sec/3600);
	tick = current_tick.tv_sec / 3600;

	return tick;
}
static void store_time_pre_shutdown(struct atc260x_gauge_info *info)
{
	unsigned long systime_hour;
	systime_hour = get_time_hour(info);
	GAUGE_DBG("[%s]hours=%lu\n", __func__, systime_hour);
	atc260x_pstore_set(info->atc260x,
				ATC260X_PSTORE_TAG_GAUGE_SHDWN_TIME, systime_hour&0x3fff);
}

int  measure_current(void)
{
	int bat_curr;
	int ret;

	ret = atc260x_auxadc_get_translated(global_gauge_info_ptr->atc260x, global_gauge_info_ptr->icm_channel, &bat_curr);
	if (ret) {
		GAUGE_ERR("get icm translated fail!!\n");
		return 0;
	}

	if (((bat_curr >= 0) && (bat_curr <= CHARGE_CURRENT_THRESHOLD)) ||
		((bat_curr <= 0) && (abs(bat_curr) <= DISCHARGE_CURRENT_THRESHOLD)))
		return 0;

	return bat_curr;
}

int  measure_vbatt(void)
{
	int bat_v;
	int ret;

	ret = atc260x_auxadc_get_translated(global_gauge_info_ptr->atc260x,
					global_gauge_info_ptr->batv_channel, &bat_v);
	if (ret) {
		GAUGE_ERR("[%s]get icm translated fail!!\n", __func__);
		return 0;
	}
	return bat_v;
}

/**
 * @brief atc260x_check_bat_status
 * check if battery is healthy
 * @return BATTERY_HEALTHY_STATE
 */
static int atc2603c_bat_check_health_pre(struct atc260x_dev *atc260x)
{
	int batv = measure_vbatt();
	if (batv <= 200) {
		atc260x_charger_set_cc_force(50);
		atc260x_charger_on_force();
		msleep(64);
		batv = measure_vbatt();
		atc260x_charger_set_cc_restore();
		if (batv <= 200) {
			atc260x_charger_off_force();
			return POWER_SUPPLY_HEALTH_UNKNOWN;/*modified by cxj@2014-11-06*/
		}
	}

	return POWER_SUPPLY_HEALTH_GOOD;
}

static void atc2603c_gauge_update_charge_type(struct atc260x_gauge_info *info)
{
	int ret = atc260x_reg_read(info->atc260x, ATC2603C_PMU_CHARGER_CTL1);
	info->chg_type = (ret & PMU_CHARGER_CTL1_PHASE_MASK) >>
		PMU_CHARGER_CTL1_PHASE_SHIFT;
}

static int atc260x_bat_check_type(struct atc260x_gauge_info *info, int *type)
{
	switch (info->chg_type) {
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

static int measure_vbatt_average(void)
{
	int vol_buf[SAMPLES_COUNT];
	int sum = 0;
	int i;

	for (i = 0; i < SAMPLES_COUNT; i++ ) {
		vol_buf[i] = measure_vbatt();
		sum += vol_buf[i];
		usleep_range(2000,2200);
	}

	return sum / SAMPLES_COUNT;
}

static int measure_current_avr(void)
{
	int curr_buf[SAMPLES_COUNT];
	int sum = 0;
	int i;

	for (i = 0; i < SAMPLES_COUNT; i++) {
		curr_buf[i] = measure_current();
		sum += curr_buf[i];
		usleep_range(2000, 2200);
	}
	return sum / SAMPLES_COUNT;
}

static void get_charge_status(int *status)
{
	int data;

	data = measure_current();

	if (data < 0)
		*status =  POWER_SUPPLY_STATUS_DISCHARGING;
	else if (data > 0)
		*status = POWER_SUPPLY_STATUS_CHARGING;
	else
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;

	GAUGE_DBG("[%s] charge status:%d\n", __func__, *status);
}

void batt_info_dump(struct atc260x_gauge_info *info)
{
	printk(KERN_INFO"\n======================debug information========================\n");
	/* dts config */
	printk(KERN_INFO"[%s] capacity:%dmAh\n", __func__, info->cfg_items.capacity);
	printk(KERN_INFO"[%s] real_capacity:%dmAh\n", __func__, info->real_capacity);
	printk(KERN_INFO"[%s] taper_vol:%dmV\n", __func__, info->cfg_items.taper_vol);
	printk(KERN_INFO"[%s] taper_current:%dmA\n", __func__, info->cfg_items.taper_current);
	printk(KERN_INFO"[%s] terminal_vol:%dmV\n", __func__, info->cfg_items.terminal_vol);
	printk(KERN_INFO"[%s] min_over_chg_protect_voltage:%dmV\n", __func__, info->cfg_items.min_over_chg_protect_voltage);
	printk(KERN_INFO"[%s] suspend_current:%duA\n", __func__, info->cfg_items.suspend_current);
	printk(KERN_INFO"[%s] shutdown_current:%duA\n\n", __func__, info->cfg_items.shutdown_current);
	/* interval */
	printk(KERN_INFO"[%s] interval:%ds\n", __func__, info->interval);
	/* charge status */
	if (info->status == POWER_SUPPLY_STATUS_NOT_CHARGING)
		printk(KERN_INFO"[%s] charger status:POWER_SUPPLY_STATUS_NOT_DISCHARGING\n", __func__);
	else if (info->status == POWER_SUPPLY_STATUS_CHARGING)
		printk(KERN_INFO"[%s] charger status:POWER_SUPPLY_STATUS_CHARGING\n", __func__);
	else if (info->status == POWER_SUPPLY_STATUS_DISCHARGING)
		printk(KERN_INFO"[%s] charger status:POWER_SUPPLY_STATUS_DISCHARGING\n", __func__);
	/* battery */
	printk(KERN_INFO"[%s] measure bat voltage:%dmv\n", __func__, measure_vbatt());
	printk(KERN_INFO"[%s] measure charger/discharge current:%dmA\n", __func__, measure_current());
	printk(KERN_INFO"[%s] charge resistor:%dmohm\n", __func__, info->ch_resistor);
	printk(KERN_INFO"[%s] discharge resistor:%dmohm\n", __func__, info->disch_resistor);
	printk(KERN_INFO"[%s] ocv:%dmv\n", __func__, info->ocv);
	printk(KERN_INFO"[%s] ocv stop:%dmv\n", __func__, info->ocv_stop);
	printk(KERN_INFO"[%s] real soc:%d\n", __func__, info->soc_real);
	printk(KERN_INFO"[%s] now soc:%d\n", __func__, info->soc_now);
	printk(KERN_INFO"[%s] show soc:%d\n", __func__, info->soc_show);
	printk(KERN_INFO"\n=================================\n");
}
static int get_responding_ocv(struct atc260x_gauge_info *info, int soc)
{
	int i;
	int count;
	int ocv_finded = 0;
	int soc_finded;
	int ocv_add;
	int (*ocv_soc_table_p)[2];

	if (info->cfg_items.ocv_soc_config)
		ocv_soc_table_p = ocv_soc_table_config;
	else
		ocv_soc_table_p = ocv_soc_table;

	count = ARRAY_SIZE(ocv_soc_table);
	if (soc == 0)
		return (*(ocv_soc_table_p + 0))[0];
	for (i = count - 1; i >= 0; i--) {
		soc_finded = (*(ocv_soc_table_p + i))[1];

		if (soc >= soc_finded*1000) {
			if (i == count - 1) {
				ocv_finded = info->cfg_items.taper_vol;
				break;
			}
			ocv_finded = (*(ocv_soc_table_p + i))[0];
			ocv_add = (soc - soc_finded*1000) * ((*(ocv_soc_table_p + i + 1))[0] - ocv_finded)/1000;
			ocv_finded += ocv_add;
			GAUGE_DBG("ocv_finded=%d,ocv_add=%d\n", ocv_finded, ocv_add);
			break;
		}
	}

	return ocv_finded;
}
static void update_discharge_resistor(struct atc260x_gauge_info *info, int soc)
{
	int batv = measure_vbatt_average();
	int bati = -measure_current_avr();
	int resp_ocv = get_responding_ocv(info, soc);

	GAUGE_DBG("batv=%d,bati=%d\n", batv, bati);
	/*if bati is too small, the result of calculating resistor will be bad*/
	if (bati <= 100) {
		GAUGE_DBG("current is too small!do not calc R_dich\n");
		goto out;
	}

	GAUGE_DBG("resp_ocv=%d,batv=%d,bati=%d\n", resp_ocv, batv, bati);
	if (soc == FULL_CHARGE_SOC)
	{
		if (info->ocv >= info->cfg_items.taper_vol + 20)
		{
			info->disch_resistor = (info->cfg_items.taper_vol + 20 - batv)*1000/bati;
			GAUGE_DBG("[soc=100%]disch_r=%d\n",info->disch_resistor);
		}
		goto out;
	}
	if (resp_ocv > batv)
		info->disch_resistor = (resp_ocv - batv)*1000/bati;
	else
		GAUGE_ERR("[%s]err:resp_ocv=%d,batv=%d!!",__func__,resp_ocv,batv);
out:
	if (info->disch_resistor > 500)
		info->disch_resistor = 500;
	else if (info->disch_resistor < 80)
		info->disch_resistor = 80;
	GAUGE_DBG("[%s]disch_resistor=%d\n", __func__, info->disch_resistor);
}

static int calc_down_step(struct atc260x_gauge_info *info, int bati_avg)
{
	int down_step;
	if (!info->real_capacity)
		down_step = ((bati_avg * info->interval * 1000) / 36) / info->cfg_items.capacity;
	else
		down_step = ((bati_avg * info->interval * 1000) / 36) / info->real_capacity;
	return down_step;
}

/*smooth for discharge curve*/
static void down_curve_smooth(struct atc260x_gauge_info *info, int *soc)
{
	int down_step;
	int vbatt_avg;
	int bati_avg;
	int ocv;
	static int lost_capacity;

	/*calc down step, when battery voltage is low*/
	vbatt_avg = measure_vbatt_average();
	bati_avg = -measure_current_avr();
	ocv = vbatt_avg + bati_avg * 150 / 1000;
	if (bati_avg <= 0) {
		down_step = 0;
		goto out;
	}
	down_step = calc_down_step(info, bati_avg);
	if ((info->soc_now == FULL_CHARGE_SOC) || (info->discharge_clum != 0))
		info->discharge_clum += bati_avg * info->interval;
	if ((ocv <= info->cfg_items.terminal_vol + 50) ||
		(vbatt_avg <= info->cfg_items.terminal_vol - 50)) {
		info->low_pwr_cnt++;
		if (info->low_pwr_cnt >= 5) {
			info->low_pwr_cnt = 0;
			if (info->discharge_clum)
				atc260x_pstore_set(info->atc260x,
								ATC260X_PSTORE_TAG_CAPACITY,
								info->discharge_clum / 3600);
			down_step = *soc - 1; /*avoid to recalculate soc in soc_update_after_reboot function*/
			atc260x_pstore_set(info->atc260x,
								ATC260X_PSTORE_TAG_GAUGE_CAP,
								1);
			GAUGE_ERR("%s, real_capacity:%d\n",__func__, info->discharge_clum / 3600);
			goto out;
		}
	} else {
		info->low_pwr_cnt = 0;
	}

	if (info->soc_now == FULL_CHARGE_SOC && lost_capacity < 1000) {
		update_discharge_resistor(info, info->soc_now);
		lost_capacity += down_step;
		down_step = 0;
		GAUGE_DBG("lost_capacity:%d\n",lost_capacity);
		goto out;
	}

	if (lost_capacity != 0)
		lost_capacity = 0;

	if (info->soc_now > info->soc_real + 250) {
		GAUGE_DBG("soc_now>soc_real:R increase!!");
		update_discharge_resistor(info, info->soc_now);
		info->dich_r_change = true;
	}

	if (down_step < 0)
		down_step = 0;
	else if (info->soc_now - down_step < 1000)
		down_step = 0;

out:
	*soc = *soc - down_step;
	GAUGE_DBG("[%s] down_step=%d, soc:%d\n", __func__, down_step, *soc);
}

static void soc_post_process(struct atc260x_gauge_info *info)
{
	int soc_last;

	if (info->soc_now  > FULL_CHARGE_SOC)
		info->soc_now = FULL_CHARGE_SOC;
	else if (info->soc_now < EMPTY_DISCHARGE_SOC)
		info->soc_now = EMPTY_DISCHARGE_SOC;

	mutex_lock(&info->lock);
	info->soc_show = info->soc_now / 1000;
	mutex_unlock(&info->lock);

	if (info->soc_pre != info->soc_show) {
		info->soc_pre = info->soc_show;
		power_supply_changed(&info->battery);
	}

	info->bat_temp = get_battery_temperature();
	if (info->pre_temp!= info->bat_temp) {
		info->pre_temp = info->bat_temp;
		power_supply_changed(&info->battery);
	}
	/*
	 * chenbo@20150514
	 * save last soc
	 */

	atc260x_pstore_get(info->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_CAP, &soc_last);
	GAUGE_DBG("[%s, %d], soc_get:%d\n",__func__, __LINE__, soc_last);
	if ((soc_last >= 0) &&
		soc_last - info->soc_now > 1000)
		info->soc_last = soc_last;

	atc260x_pstore_set(info->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_CAP, info->soc_now);
	GAUGE_DBG("[%s, %d], soc_set:%d\n",__func__, __LINE__, info->soc_now);
	if (info->soc_show % 5)
		ch_resistor_calced = 0;

	if (info->cfg_items.print_switch)
		batt_info_dump(info);

	if (info->cfg_items.log_switch)
		store_batt_info(info);
}

static int get_threshold(int type)
{
	int threshold = 0;

	if (type == CURRENT_TYPE)
		threshold = BAT_CUR_VARIANCE_THRESHOLD;
	else if (type == VOLTAGE_TYPE)
		threshold = BAT_VOL_VARIANCE_THRESHOLD;
	else
		GAUGE_INFO("the parameter of 'type' do not support!\n");

	return threshold;
}

/*
 * filter for gathered batt current and batt voltage, including 2 steps:
 * step1 : divide the buf which lengh is len into several groups,
 *            every group have similar data range;
 * step2: filterd for ervery group.
 */
static int filter_algorithm1(int *buf,  int len, int type)
{
	struct data_group group[10];
	int threshold = 0;
	int i;
	int j;
	int k;

    if (!buf)
		return -EINVAL;

	threshold = get_threshold(type);
	/*divided the data into several group*/
	group[0].index = 0;
	group[0].num = 1;
    for (i = 1, j = 0; i < len; i++) {
		if (abs(buf[i] - buf[i - 1]) > threshold)
			group[++j].index = i;
		group[j].num = (i + 1) - group[j].index;
	}

	/*handle  every group*/
	for (i = 0; i < sizeof(group) / sizeof(struct data_group); i++) {
		GAUGE_DBG("group[%d].index=%d, group[%d].num=%d\n", i, group[i].index, i, group[i].num);

		if (group[i].num >= 5) {
			for (j = group[i].index; j <= (group[i].index + group[i].num + 1)/2; j++) {
				group[i].sum = buf[j];
				group[i].count = 1;
				for (k = j; k < group[i].index + group[i].num; k++) {
					if (abs(buf[k + 1] - buf[j]) < threshold) {
						group[i].sum += buf[k + 1];
						group[i].count++;
						GAUGE_DBG("buf[%d]:%d\n", k + 1,  buf[k + 1]);
					}
				}

				if (group[i].count >= (group[i].num + 1) / 2) {
					group[i].avr_data = group[i].sum / group[i].count;
					GAUGE_DBG("[%s] Average cur/vol=%d/%d=%d\n",
						__func__, group[i].sum, group[i].count, group[i].avr_data);

					return group[i].avr_data;
				}
			}
		}
	}
	return -EINVAL;
}

/*by testing ,we know that the current change depend the scene,but always not hardly
 *we just calculate the data near the average value
 *the one apart from the average by 100 will be discarded
 */

 static int filter_algorithm3(int *buf, int len, int type)
{
	int threshold = 0;
	int avr_data;
	int count = 0;
	int sum = 0;
	int j;
	int k;

	if (!buf)
        return -EINVAL;

	threshold = get_threshold(type);

	for (j = 0; j < len; j++)
		sum += buf[j];
	avr_data = sum / len;
	for(k = 0;k < len; k++) {
		if (abs(buf[k] - avr_data) < threshold)
			count++;
		else
			sum -= buf[k];
	}
	GAUGE_DBG("available data count:%d\n", count);
	if (count > len * 2 / 3)
		avr_data = sum / count;
	else
		return -EINVAL;

	return avr_data;
}

static int filter_process(struct atc260x_gauge_info *info)
{
	struct kfifo_data fifo_data;

	fifo_data.bat_vol =
		info->filter_algorithm(info->vol_buf, SAMPLES_COUNT, VOLTAGE_TYPE);
	fifo_data.bat_curr =
		info->filter_algorithm(info->curr_buf, SAMPLES_COUNT, CURRENT_TYPE);
	fifo_data.timestamp.tv_sec = current_tick.tv_sec;
	fifo_data.timestamp.tv_usec = current_tick.tv_usec;
	GAUGE_DBG("[%s] the latest value: %d(vol), %d(cur)\n",
		__func__, fifo_data.bat_vol, fifo_data.bat_curr);

	/* modified by cxj@20141029 */
	if ((fifo_data.bat_vol == -EINVAL) ||
		(fifo_data.bat_curr == -EINVAL))
		return -EINVAL;

	kfifo_in(&info->fifo, &fifo_data, sizeof(struct kfifo_data));

	return 0;
}

/*
 * gather battery info, including voltage and current.
 */
static void  gather_battery_info(struct atc260x_gauge_info *info)
{
	int i;

	do_gettimeofday(&current_tick);

	for (i = 0; i < SAMPLES_COUNT; i++) {
		info->vol_buf[i] = measure_vbatt();
		info->curr_buf[i] = measure_current();
		usleep_range(2000, 2200);
	}
}

/* calculate resistor in charging case */
static int calc_charge_resistor(struct atc260x_gauge_info *info)
{
	struct kfifo_data fifo_data[2];
	int data;
	int chg_current;
	int ret;

	chg_current = measure_current_avr();
	GAUGE_DBG("chg_current=%d\n", chg_current);
	if (chg_current < 0) {
		GAUGE_ERR("measure_current :data <0");
		goto out;
	}

	if (chg_current >= 500) {
		atc260x_charger_set_cc_force(500);
		msleep(2000);
		gather_battery_info(info);
		ret = filter_process(info);

		if (ret) {
			GAUGE_ERR("[%s]filter_process failed!\n", __func__);
			goto out;
		}

		atc260x_charger_set_cc_force(100);
		msleep(1500);

		gather_battery_info(info);
		ret = filter_process(info);

		if (ret) {
			GAUGE_ERR("[%s]filter_process failed!\n", __func__);
			goto out;
		}

		atc260x_charger_set_cc_restore();
		msleep(500);
	} else {
		gather_battery_info(info);
		ret = filter_process(info);
		GAUGE_DBG("[%s]turn off charger!\n", __func__);
		atc260x_charger_off_force();
		msleep(500);

		gather_battery_info(info);
		ret = filter_process(info);

		if (ret) {
			GAUGE_ERR("[%s]filter_process failed!\n", __func__);
			atc260x_charger_on_force();
			goto out;
		}

		atc260x_charger_set_cc_restore();
		GAUGE_DBG("[%s]turn on charger!\n", __func__);
		atc260x_charger_on_force();
		msleep(500);
	}

	ret = kfifo_out(&info->fifo, &fifo_data[0], sizeof(struct kfifo_data));
	GAUGE_DBG("[%s] %d(vol), %d(cur), dequeue len:%d\n",
		__func__, fifo_data[0].bat_vol, fifo_data[0].bat_curr, ret);
	ret = kfifo_out(&info->fifo, &fifo_data[1], sizeof(struct kfifo_data));
	GAUGE_DBG("[%s] %d(vol), %d(cur), dequeue len:%d\n",
		__func__, fifo_data[1].bat_vol, fifo_data[1].bat_curr, ret);

	if ((fifo_data[0].bat_vol > fifo_data[1].bat_vol) &&
		(fifo_data[0].bat_curr > fifo_data[1].bat_curr)) {
		/* calculate resistor :mohm*/
		data = 1000 * (fifo_data[0].bat_vol  - fifo_data[1].bat_vol)
			/ (fifo_data[0].bat_curr - fifo_data[1].bat_curr);

		GAUGE_DBG("fifo_data[0].bat_vol = %d, fifo_data[1].bat_vol = %d\n", fifo_data[0].bat_vol, fifo_data[1].bat_vol);
		GAUGE_DBG("fifo_data[0].bat_curr = %d, fifo_data[1].bat_curr = %d\n", fifo_data[0].bat_curr, fifo_data[1].bat_curr);
		GAUGE_DBG("here ch_resistor = %d\n", data);

		if (data <= 500)
			info->ch_resistor = data;
		else
			info->ch_resistor = 500;
		GAUGE_DBG("[%s] the latest charge resistor is %d\n", __func__, info->ch_resistor);

		return 0;
	}

out:
	kfifo_reset(&info->fifo);
	return -EINVAL;
}

/* Calculate Open Circuit Voltage */
static int calc_ocv(struct atc260x_gauge_info *info)
{
	int i;
	int vbatt_sum;
	int ibatt_sum;
	int count = 0;

	switch (info->status) {
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
		for (i = 0, vbatt_sum = 0; i < SAMPLES_COUNT; i++)
			vbatt_sum += info->vol_buf[i];

		info->vbatt_avg = vbatt_sum / SAMPLES_COUNT;
		info->ocv = info->vbatt_avg;
		info->ocv_stop = info->cfg_items.terminal_vol+TERMINAL_VOL_ADD;
		info->ibatt_avg = 0;
		break;
	case POWER_SUPPLY_STATUS_CHARGING:
	case POWER_SUPPLY_STATUS_DISCHARGING:
		for (i = 0, ibatt_sum = 0, vbatt_sum = 0; i < SAMPLES_COUNT; i++) {
			vbatt_sum += info->vol_buf[i];
			if (info->status == POWER_SUPPLY_STATUS_CHARGING) {
				if (info->curr_buf[i] > 0) {
					ibatt_sum += info->curr_buf[i];
					count++;
				}
			}
			else if (info->status == POWER_SUPPLY_STATUS_DISCHARGING) {
				if (info->curr_buf[i] < 0) {
					ibatt_sum += -info->curr_buf[i];
					count++;
				}
			}
		}
		info->vbatt_avg = vbatt_sum / SAMPLES_COUNT;
		/*added by cxj@20141101:division cannot be zero*/
		if(count != 0)
			info->ibatt_avg = ibatt_sum / count;
		else {/*if zero ,battery is of full power*/
	      /*modified by cxj@20141113:cannot return -EINVAL,because of full capacity*/
			info->ocv = info->vbatt_avg;
			info->ibatt_avg = 0;
			return 0;
		}

		if (info->status == POWER_SUPPLY_STATUS_CHARGING) {
			info->ocv = info->vbatt_avg - info->ibatt_avg * info->ch_resistor / 1000;
			GAUGE_DBG("[%s] ocv:%d\n", __func__, info->ocv);
		}
		else if (info->status == POWER_SUPPLY_STATUS_DISCHARGING) {
			info->ocv = info->vbatt_avg + info->ibatt_avg * info->disch_resistor / 1000;
			info->ocv_stop = info->cfg_items.terminal_vol + TERMINAL_VOL_ADD + info->ibatt_avg * info->disch_resistor / 1000;
			GAUGE_DBG("[%s] ocv:%d, ocv stop:%d\n",
				__func__, info->ocv, info->ocv_stop);
		}
		break;
	default:
		GAUGE_WARNING("[%s] charging status err!\n" , __func__);
		return -EINVAL;
	}

	return 0;

}

/* Calculate State of Charge (percent points) */
static void calc_soc(struct atc260x_gauge_info *info, int ocv, int *soc)
{
	int i;
	int count;
	int soc_finded;
	int (*ocv_soc_table_p)[2];

	if (info->cfg_items.ocv_soc_config)
		ocv_soc_table_p = ocv_soc_table_config;
	else
		ocv_soc_table_p = ocv_soc_table;

	if (ocv < (*ocv_soc_table_p)[0]) {
		*soc = 0;
		GAUGE_DBG("[%s] ocv:%d, soc:0(ocv is less than the minimum value, set soc zero)\n",
			__func__, ocv);
		return;
	}
	count = ARRAY_SIZE(ocv_soc_table);

	for (i = count - 1; i >= 0; i--) {
		if (ocv >= (*(ocv_soc_table_p + i))[0]) {
			if (i == count - 1) {
				*soc = FULL_CHARGE_SOC;
				break;
			}
			soc_finded = (*(ocv_soc_table_p + i))[1];
			GAUGE_DBG("soc_finded=%d\n", soc_finded);
			*soc = soc_finded*1000 + (ocv - (*(ocv_soc_table_p + i))[0])*
					((*(ocv_soc_table_p + i + 1))[1]-soc_finded)*1000/
					(((*(ocv_soc_table_p + i + 1))[0]) - (*(ocv_soc_table_p + i))[0]);
			GAUGE_DBG("[%s]ocv:%d, calc soc is %d\n", __func__, ocv, *soc);
			break;
		}
	}
}

#ifdef GAUGE_SOC_AVERAGE
static int soc_average(struct atc260x_gauge_info *info,  int soc)
{
	int soc_sum = 0;
	int soc_avr;
	int i;
	int size;

	size = sizeof(info->soc_queue) / sizeof(int);

	info->soc_queue[info->index++ % size] = soc;

	if (info->index < size - 1)
		return soc;

	for (i = 0; i < size; i++) {
		soc_sum += info->soc_queue[i];
		GAUGE_DBG("[%s] info->soc_queue[%d] = %d\n", __func__, i, info->soc_queue[i]);
	}

	info->index = 0;
	soc_avr = soc_sum / size;
	GAUGE_DBG("[%s] average soc = %d /%d = %d\n", __func__, soc_sum, size, soc_avr);

	return soc_avr;
}
#endif

static int generate_poll_interval(struct atc260x_gauge_info *info)
{
	int data;

	data = measure_vbatt_average();

	if (data >= info->cfg_items.terminal_vol + 100) {
		if (info->soc_show == 99 || info->soc_real == FULL_CHARGE_SOC || full_power_dealing)
			info->interval = 5;
		else
			info->interval = 10;
	} else if (data >= info->cfg_items.terminal_vol + 50)
		info->interval = 5;
	else
		info->interval = 2;

	GAUGE_DBG("[%s] bat_vol :%d, interval = %d\n", __func__, data, info->interval);

	return info->interval;
}

static void start_anew(struct atc260x_gauge_info *info)
{
	kfifo_reset(&info->fifo);
	ch_resistor_calced = 0;
	first_calc_resistor = -1;
	taper_interval = 0;
	kfifo_reset(&info->down_curve);
}
static void soc_grow_up(struct atc260x_gauge_info *info, int grow_step)
{
	if (info->soc_now == FULL_CHARGE_SOC)
		return;

	if (info->soc_real > info->soc_now) {
		info->soc_now +=  grow_step;
		GAUGE_DBG("soc_now add up %d\n", grow_step);
	}
	if (info->soc_now > FULL_CHARGE_SOC)
		info->soc_now = FULL_CHARGE_SOC;
}

static void soc_now_compensation(struct atc260x_gauge_info *info)
{
	int step = calc_down_step(info, info->ibatt_avg);
	info->soc_now += step;
	if (info->soc_now >= FULL_CHARGE_SOC)
		info->soc_now = FULL_CHARGE_SOC - 1;
	GAUGE_DBG("%s, up_step:%d, soc_now:%d\n",__func__, step, info->soc_now);
}

static void full_power_schedule(struct atc260x_gauge_info *info)
{
	if (atc260x_charger_get_status()) {
		atc260x_charger_off_force();
		GAUGE_DBG("[%s]now turn off charger...\n", __func__);
		msleep(5000);
	}
	/* detecting battery voltage as ocv*/
	get_charge_status(&info->status);/* added by cxj @2015-01-28*/
	info->vbatt_avg = measure_vbatt_average();
	info->ocv = info->vbatt_avg;
	calc_soc(info, info->ocv, &info->soc_real);
	GAUGE_DBG("after 5 secs,bat_vol(ocv)= %d\n", info->vbatt_avg);

	if (info->soc_real == FULL_CHARGE_SOC) {
		GAUGE_DBG("[%s]soc_real has been FULL_CHARGE_SOC!now minus 1000\n", __func__);
		info->soc_real = FULL_CHARGE_SOC - 1000;
	}

	if (info->vbatt_avg >= (info->cfg_items.taper_vol)) {
		taper_interval += info->interval;
		GAUGE_DBG("[%s]taper_interval = %d\n", __func__, taper_interval);
		if (taper_interval >= 60) {
			GAUGE_DBG("Time's up,power is full!\n");
			info->soc_real = FULL_CHARGE_SOC;

			/* the right of finally turning off the charger is owned by charger driver
			 * now the soc_real is just up to FULL_CHARGE_SOC,so the it must be more than
			 * soc_now.meanwhile,calling the following function is needed,as the operation
			 * to recover the flag of turn_off_force.
			 */
			GAUGE_DBG("turn on charger finally!\n");
			atc260x_charger_on_force();
			full_power_flag = true;
			full_power_dealing = false;
			taper_interval = 0;
		}
		else
		{
			full_power_flag = false;
		}
	} else {
		full_power_flag = false;
		cycle_count = taper_interval / info->interval + 1;
		GAUGE_DBG("info->interval=%d,taper_interval=%d,cycle_count = %d\n",
			info->interval, taper_interval, cycle_count);
		atc260x_charger_on_force();
	}
	return;
}
static bool emphasize_dealing(struct atc260x_gauge_info *info)
{
	GAUGE_DBG("next_do_count = %d,cycle_count = %d\n", next_do_count, cycle_count);
	if (cycle_count == 0)
		return true;
	else {
		if (next_do_count++ == (60 / info->interval - cycle_count)) {
			next_do_count = 0;
			cycle_count = 0;
			return true;
		} else
			return false;
	}
}
static bool pre_full_power_schedule(struct atc260x_gauge_info *info)
{
	int bat_curr;
	int bat_vol;
	int current_set_now;
	bool full_power_test = false;

	bat_curr = measure_current_avr();
	bat_vol = measure_vbatt_average();
	GAUGE_DBG("bat_curr=%d,bat_vol=%d\n", bat_curr, bat_vol);

	current_set_now = atc260x_charger_get_cc();
	GAUGE_DBG("current_set_now = %d\n", current_set_now);

	if (info->cfg_items.min_over_chg_protect_voltage >= 4275) {
		if (bat_vol > 4200) {
			if (current_set_now > 400) {
				if (bat_curr < info->cfg_items.taper_current) {
					GAUGE_DBG("current_set_now > 300 && bat_cur < %d:enter full power dealing\n",
							info->cfg_items.taper_current);
					full_power_test = true;
					taper_interval = 0;
				}
			} else {
				if (info->ocv >= info->cfg_items.taper_vol) {
					GAUGE_DBG("current_set_now<300 && ocv >= %d:enter full power dealing\n",
								info->cfg_items.taper_vol);
					full_power_test = true;
					taper_interval = 0;
				}
			}
		}
	} else {
		if (bat_vol > 4200) {
			if (current_set_now > 400) {
				if (bat_curr < 700) {
					GAUGE_DBG("now set charge current to be 400mA");
					atc260x_charger_set_cc_force(400);
				}
			} else if (current_set_now == 400) {
				if (bat_curr < info->cfg_items.taper_current) {
					GAUGE_DBG("current_set_now = 400 && bat_curr < %d:enter full power\n",
								info->cfg_items.taper_current);
					full_power_test = true;
					taper_interval = 0;
				}
			} else {
				if (info->ocv >= info->cfg_items.taper_vol) {
					GAUGE_DBG("current_set_now < 400 && ocv >= %d:enter full power\n",
								info->cfg_items.taper_vol);
					full_power_test = true;
					taper_interval = 0;
				}
			}
		}
	}

	/*for test*/
	if (info->ocv >= info->cfg_items.taper_vol &&
		info->soc_now == (FULL_CHARGE_SOC-1) &&
		bat_curr < info->cfg_items.taper_current) {
		full_power_test = true;
		taper_interval = 0;
	}

	GAUGE_DBG("[%s]full_power_test =%d\n", __func__, full_power_test);
	full_power_dealing = full_power_test;
	return full_power_test;
}

static int  charge_process(struct atc260x_gauge_info *info)
{
	int ret;

	static int count;
	static int first_flag = 1;
	int soc_real_test;

	info->discharge_clum = 0;
	if (info->soc_real == FULL_CHARGE_SOC) {
		if (info->cfg_items.log_switch) {
			gather_battery_info(info);
			calc_ocv(info);/*calc batv,bati,ocv for log*/
		}
		soc_grow_up(info, 500);
		return 0;
	}

	/*calc ch_resistor*/
	if ((!(info->soc_show % 5) && !ch_resistor_calced) || first_calc_resistor == -1) {
		ret = calc_charge_resistor(info);
		count = 0;
		if (ret)
			GAUGE_ERR("[%s] calc charge resistor err\n", __func__);
		if (first_calc_resistor == -1) {
			GAUGE_DBG("calc resistor over!\n");
			first_calc_resistor = 0;
		}
		ch_resistor_calced = 1;
	}

	if (info->ch_resistor == 500 && count != -1)
	{
		if (count == 2)
			count = -1;
		calc_charge_resistor(info);
		count ++;
	}

	/*calc ocv*/
	gather_battery_info(info);
	ret = calc_ocv(info);
	if (ret) {
		GAUGE_ERR("[%s]calc ocv fail!\n", __func__);
		return ret;
	}

	/*calc soc*/
	/*calc_soc(info, info->ocv, &info->soc_real);*/
	/*avoid soc_real go up and down frequently*/
	if (first_flag)
	{
		calc_soc(info, info->ocv, &info->soc_real);
		soc_real_test = info->soc_real;
		first_flag = 0;
	}
	else
		calc_soc(info, info->ocv, &soc_real_test);

	if (soc_real_test > info->soc_real)
	{
		info->soc_real = soc_real_test;
		GAUGE_DBG("soc_real:%d\n",info->soc_real);
	}
	soc_now_compensation(info);

	/*if the calc_soc has been FULL_CHARGE_SOC, we do not make it as it should be
	 * because the full power schedule will do it
	*/
	if ((info->soc_real == FULL_CHARGE_SOC) && (full_power_flag == false)) {
		GAUGE_DBG("[%s]soc_real has been FULL_CHARGE_SOC!now minus 1000\n", __func__);
		info->soc_real = FULL_CHARGE_SOC - 1000;
	}
	if (pre_full_power_schedule(info)) {
		info->interval = 5;
		if (emphasize_dealing(info))
			full_power_schedule(info);
	}
	return 0;
}

static int  discharge_process(struct atc260x_gauge_info *info)
{
	int ret;
	int soc_real_test;
	static int first_flag = 1;

	gather_battery_info(info);

	if(full_power_flag != false)
		full_power_flag = false;

	ret = calc_ocv(info);
	if (ret) {
		GAUGE_DBG("[%s]calc_ocv failed!\n", __func__);
		return ret;
	}

	if (first_flag) {
		calc_soc(info, info->ocv, &info->soc_real);
		soc_real_test = info->soc_real;
		first_flag = 0;
	} else
		calc_soc(info, info->ocv, &soc_real_test);/*not allow soc_real go up*/

	if (soc_real_test < info->soc_real) {
		info->soc_real = soc_real_test;

	} else {
		if (info->dich_r_change) {
			info->soc_real = soc_real_test;
			info->dich_r_change = false;
		}
	}
	GAUGE_DBG("soc_real:%d\n", info->soc_real);

	down_curve_smooth(info, &info->soc_now);

	return 0;
}

/*added by cxj @20141117*/
static int notcharging_process(struct atc260x_gauge_info *info)
{
	/* when step into full_power_schedule(this time,bat_vol>=4180mV),charger may be turned off,and
	 * status of charge will turn to be NOTDISCHARGING,and cannot dealing
	 * full power status in charge_process
	 * soc_now >0:avoid poor healthy status of battery,such as no battery ,short circuit.
	 * soc_now <FULL_CHARGE_SOC:avoid still stepping into full power dealing when capacity is 100%,
	*/
	if (info->soc_now == FULL_CHARGE_SOC || info->soc_now <= 0) {
		if (info->cfg_items.log_switch) {
			gather_battery_info(info);
			calc_ocv(info);/*calc batv,bati,ocv for log*/
		}
		return 0;
	}

	gather_battery_info(info);
	calc_ocv(info);
	calc_soc(info, info->ocv, &info->soc_real);
	/* 1.when the load capacity of adapter cannot support charging with more than 60mA,
	 * 2.when full_power_dealing finished,and then turn on charger ,but also,the load capacity is not enough,
	 * 3.when battery is being full,but did not ever step into full_power_dealing,and charging current<60mA .
	 * all above,contribute to going into this function,meanwhile charger is on.
	 */
	if (atc260x_charger_get_status()) {
		GAUGE_DBG("charger is on,but current<60mA\n");
		if (info->ocv >= (info->cfg_items.taper_vol))
			info->soc_now += 500;/* for 2&3*/
		else{
			if (info->soc_real > info->soc_now + 500) {
				if (info->soc_now + 500 < FULL_CHARGE_SOC)  /*for 1*/
					info->soc_now += 500;
			} else if (info->soc_real < info->soc_now - 500) {
				if (info->soc_now - 500 >= 1000)
					info->soc_now -= 500;
			}
		}
		return 0;
	}

	if (info->soc_real == FULL_CHARGE_SOC) {
		GAUGE_DBG("[%s]soc_real has been FULL_CHARGE_SOC!now minus 1000\n", __func__);
		info->soc_real = FULL_CHARGE_SOC - 1000;
	}
	/* here,if charger is not on,it should be:
	 * 1.being in full_power_dealing
	 * 2.just plug in adapter a moment
	*/
	if (info->ocv >= (info->cfg_items.taper_vol)) {
		soc_grow_up(info, 500);
		GAUGE_DBG("[%s]interval=%d\n", __func__, info->interval);
		full_power_schedule(info);
	} else {
		if (!atc260x_charger_get_status()) {
			GAUGE_DBG("[%s]now turn on charger...\n", __func__);
			atc260x_charger_on_force();
		}
		cycle_count = taper_interval / info->interval + 1;
		GAUGE_DBG("[%s]taper_interval=%d,cycle_count=%d", __func__, taper_interval, cycle_count);
	}
	return 0;
}

/* uA */
/*record the time stamp of suspend*/
static struct timespec suspend_ts;
static int soc_update_after_reboot(struct atc260x_gauge_info *info)
{
	unsigned int soc_stored;
	unsigned int real_capacity;
	int ret;

	atc260x_pstore_get(info->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_CAP, &soc_stored);
	GAUGE_DBG("[%s, %d], soc_get:%d\n",__func__, __LINE__, soc_stored);
	if ((soc_stored == 0) || (soc_stored > FULL_CHARGE_SOC)) {
		gather_battery_info(info);
		ret = calc_ocv(info);
		if (ret) {
			GAUGE_ERR("[%s]calculate ocv failed!!\n", __func__);
			return ret;
		}
		calc_soc(info, info->ocv, &info->soc_real);
		if (info->soc_real < FULL_CHARGE_SOC)
			info->soc_now = info->soc_real;
		else
			info->soc_now = FULL_CHARGE_SOC - 1;
		atc260x_pstore_set(info->atc260x,
		ATC260X_PSTORE_TAG_GAUGE_CAP, info->soc_now);
		GAUGE_DBG("[%s, %d], soc_set:%d\n",__func__, __LINE__, info->soc_now);
	}else {
		info->soc_now = soc_stored;
	}
	atc260x_pstore_get(info->atc260x, ATC260X_PSTORE_TAG_CAPACITY, &real_capacity);
	GAUGE_ERR("[%s, %d], real_capacity:%d\n",__func__, __LINE__, real_capacity);
	if (real_capacity)
		info->real_capacity = real_capacity;

	return 0;
}
static int  init_capacity(struct atc260x_gauge_info *info)
{
	gather_battery_info(info);
	get_charge_status(&info->status);
	soc_update_after_reboot(info);

	return 0;
}

static enum power_supply_property atc260x_gauge_props[] = {
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

static int atc260x_gauge_get_props(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct atc260x_gauge_info *info = dev_get_drvdata(psy->dev->parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		/*get_charge_status(&val->intval);*/
		if (atc260x_get_charger_online_status() && (info->soc_show < 100))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = info->online;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->online;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = info->cfg_items.min_over_chg_protect_voltage;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = measure_vbatt() * 1000;/*mV->uV*/
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = measure_current() * 1000;/*mA->uA*/
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->health == POWER_SUPPLY_HEALTH_UNKNOWN)
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = atc260x_bat_check_type(info, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		if (info->health != POWER_SUPPLY_HEALTH_GOOD)
			val->intval = -99;
		else
			val->intval = get_cap_gauge();
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = info->bat_temp * 10;
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static void gauge_update(struct work_struct *work)
{
	int charge_status;
	struct atc260x_gauge_info *info;
	info = container_of(work, struct atc260x_gauge_info, work.work);
	charge_status = info->status;
	atc2603c_gauge_update_charge_type(info);
	/* if the status has been changed,we discard the kfifo data*/
	get_charge_status(&info->status);
	if (charge_status ^ info->status) {
		GAUGE_INFO("charge status changed!\n");
		start_anew(info);
		info->low_pwr_cnt = 0;
		if (info->status == POWER_SUPPLY_STATUS_DISCHARGING)
			update_discharge_resistor(info, info->soc_now);
	}

	if (info->status == POWER_SUPPLY_STATUS_CHARGING)
		charge_process(info);
	else if (info->status == POWER_SUPPLY_STATUS_DISCHARGING)
		discharge_process(info);
	else    /* POWER_SUPPLY_STATUS_NOT_CHARGING */
		notcharging_process(info);

	soc_post_process(info);
	generate_poll_interval(info);
	queue_delayed_work(info->wq, &info->work, info->interval * HZ);
}

static void resistor_init(struct atc260x_gauge_info *info)
{
	info->ch_resistor = 150;
	info->disch_resistor = 250;
	GAUGE_INFO("[%s] inited ch_resistor : %d, inited disch_resistor:%d\n",
		__func__, info->ch_resistor, info->disch_resistor);
}
static int  init_gauge(struct atc260x_gauge_info *info)
{
	int ret;

	info->online = atc260x_chk_bat_online_intermeddle()<=0 ? 0 : 1;;
	info->health = atc2603c_bat_check_health_pre(info->atc260x);

	start_anew(info);
	info->low_pwr_cnt = 0;
	info->interval = 5;

	resistor_init(info);
	info->filter_algorithm = filter_algorithm3;

	ret = init_capacity(info);
	if (ret) {
		GAUGE_ERR("[%s]init_capacity failed!\n", __func__);
		return ret;
	}
	return 0;
}

int get_cap_gauge(void)
{
	return global_gauge_info_ptr->soc_show;
}

void gauge_reset(struct atc260x_gauge_info *info)
{
	atc260x_pstore_set(info->atc260x, ATC260X_PSTORE_TAG_GAUGE_SHDWN_TIME, 0x3fff);
	atc260x_pstore_set(info->atc260x, ATC260X_PSTORE_TAG_GAUGE_CAP, 0);
	GAUGE_DBG("[%s, %d], soc_set:%d\n",__func__, __LINE__, 0);
}

static ssize_t show_reset(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
static ssize_t store_reset(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct platform_device *pdev = (struct platform_device *)container_of(dev, struct platform_device, dev);
	struct atc260x_gauge_info *info = (struct atc260x_gauge_info *)platform_get_drvdata(pdev);

	gauge_reset(info);
	return count;
}

static ssize_t show_dump(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = (struct platform_device *)container_of(dev, struct platform_device, dev);
	struct atc260x_gauge_info *info = (struct atc260x_gauge_info *)platform_get_drvdata(pdev);
	batt_info_dump(info);
	return 0;
}
static ssize_t store_dump(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

static ssize_t show_test_kfifo(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = (struct platform_device *)container_of(dev, struct platform_device, dev);
	struct atc260x_gauge_info *info = (struct atc260x_gauge_info *)platform_get_drvdata(pdev);
	struct kfifo_data kfifo_data;
	struct kfifo_data test;
	int ret, i;

	kfifo_data.bat_curr = 1000;
	kfifo_data.bat_vol = 3800;
	kfifo_in(&info->fifo, &kfifo_data, sizeof(struct kfifo_data));
	kfifo_data.bat_curr = 1500;
	kfifo_data.bat_vol = 4000;
	kfifo_in(&info->fifo, &kfifo_data, sizeof(struct kfifo_data));
	i = 2;
	while (!kfifo_is_full(&info->fifo)) {
		kfifo_in(&info->fifo, &kfifo_data, sizeof(struct kfifo_data));
		i++;
	}
	printk(KERN_INFO"i:%d, fifo is full\n", i);
	ret = kfifo_out(&info->fifo, &test, sizeof(struct kfifo_data));
	printk(KERN_INFO"%s : test.current :%d\n", __func__, test.bat_curr);
	printk(KERN_INFO"%s : test.voltage :%d\n", __func__, test.bat_vol);
	ret = kfifo_out_peek(&info->fifo, &test, sizeof(struct kfifo_data));
	printk(KERN_INFO"%s : peer,test.current :%d\n", __func__, test.bat_curr);
	printk(KERN_INFO"%s : peer,test.voltage :%d\n", __func__, test.bat_vol);
	return 0;
}
static ssize_t store_test_kfifo(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

static ssize_t show_test_filter(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;

	int test_buf[10] = {550, 560, 600, 610, 619, 605, 608, 700, 710, 720};
	ret = filter_algorithm1(test_buf, 10, CURRENT_TYPE);
	printk(KERN_INFO"ret = %d\n", ret);

	return 0;
}
static ssize_t store_test_filter(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

static ssize_t store_log_switch(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int log_switch;

	sscanf(buf, "%d\n", &log_switch);
	if (log_switch == 1 || log_switch == 0) {
		global_gauge_info_ptr->cfg_items.log_switch = log_switch;
		printk(KERN_INFO"now log_switch=%d\n", log_switch);
	} else
		printk(KERN_INFO"wrong parameter!\n");
	return count;
}
static ssize_t show_log_switch(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", global_gauge_info_ptr->cfg_items.log_switch);
}

static ssize_t store_print_switch(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int print_switch;

	sscanf(buf, "%d\n", &print_switch);
	if (print_switch == 1 || print_switch == 0) {
		global_gauge_info_ptr->cfg_items.print_switch = print_switch;
		printk(KERN_INFO"now print_switch=%d\n", print_switch);
	} else
		printk(KERN_INFO"wrong parameter!\n");
	return count;
}
static ssize_t show_print_switch(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", global_gauge_info_ptr->cfg_items.print_switch);
}

static ssize_t show_real_capacity(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", global_gauge_info_ptr->real_capacity);
}

static ssize_t store_real_capacity(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

static struct device_attribute gauge_attrs[] = {
	__ATTR(test_filter, S_IRUGO | S_IWUSR, show_test_filter, store_test_filter),
	__ATTR(test_kfifo, S_IRUGO | S_IWUSR, show_test_kfifo, store_test_kfifo),
	__ATTR(reset, S_IRUGO | S_IWUSR, show_reset, store_reset),
	__ATTR(dump, S_IRUGO | S_IWUSR, show_dump, store_dump),
	__ATTR(log_switch, S_IRUGO | S_IWUSR, show_log_switch, store_log_switch),
	__ATTR(print_switch, S_IRUGO | S_IWUSR, show_print_switch, store_print_switch),
	__ATTR(real_capacity, S_IRUGO | S_IWUSR, show_real_capacity, store_real_capacity),
};

int gauge_create_sysfs(struct device *dev)
{
	int r, t;

	GAUGE_INFO("[%s] create sysfs for gauge\n", __func__);

	for (t = 0; t < ARRAY_SIZE(gauge_attrs); t++) {
		r = device_create_file(dev, &gauge_attrs[t]);
		if (r) {
			dev_err(dev, "failed to create sysfs file\n");
			return r;
		}
	}
	return 0;
}

void gauge_remove_sysfs(struct device *dev)
{
	int  t;

	GAUGE_INFO("[%s]remove sysfs for gauge\n", __func__);
	for (t = 0; t < ARRAY_SIZE(gauge_attrs); t++)
		device_remove_file(dev, &gauge_attrs[t]);
}

static  int atc260x_gauge_probe(struct platform_device *pdev)
{

	struct atc260x_dev *atc260x = dev_get_drvdata(pdev->dev.parent);
	struct atc260x_gauge_info *info;
	int ret;

	info = kzalloc(sizeof(struct atc260x_gauge_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	ret =  kfifo_alloc(&info->fifo, 2 * sizeof(struct kfifo_data), GFP_KERNEL);
	if (ret) {
		GAUGE_ERR("[%s]error kfifo_alloc\n", __func__);
		goto free;
	}

	ret =  kfifo_alloc(&info->weight_fifo, SOC_WEIGHT_FIFO_NUM * sizeof(int), GFP_KERNEL);
	if (ret) {
		GAUGE_ERR("[%s]  kfifo_alloc error\n", __func__);
		goto free_weitht_fifo;
	}

	info->atc260x = atc260x;
	info->node = pdev->dev.of_node;
	global_gauge_info_ptr = info;
	first_store_gauge_info = 1;

	mutex_init(&info->lock);

	platform_set_drvdata(pdev, info);

	/*init auxadc icm and batv channel*/
	info->icm_channel = atc260x_auxadc_find_chan(info->atc260x, "ICM");
	info->batv_channel = atc260x_auxadc_find_chan(info->atc260x, "BATV");
	info->remcon_channel = atc260x_auxadc_find_chan(info->atc260x, "REMCON");

	/*init battery power supply*/
	info->battery.name = "battery";
	info->battery.use_for_apm = 1;
	info->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	info->battery.properties = atc260x_gauge_props;
	info->battery.num_properties = ARRAY_SIZE(atc260x_gauge_props);
	info->battery.get_property = atc260x_gauge_get_props;
	ret = power_supply_register(&pdev->dev, &info->battery);
	if (ret) {
		GAUGE_ERR("[%s]:power_supply_register failed for bat\n",
			__func__);
		goto err_battery;
	}

	ret = gauge_create_sysfs(&pdev->dev);
	if (ret) {
		GAUGE_ERR("gauge_create_sysfs failed!\n");
		goto free_fifo;
	}

	if (get_cfg_items(info)) {
		GAUGE_ERR("get_cfg_items failed!\n");
		goto remove_sysfs;
	}
	/*modified @2014-12-20*/
	ocv_soc_table_init(info);

	if (init_gauge(info)) {
		GAUGE_ERR("init_gauge failed!\n");
		goto free_fifo;
	}

	/*Create a work queue for fuel gauge*/
	info->wq =
		create_singlethread_workqueue("atc260x_gauge_wq");
	if (info->wq == NULL) {
		GAUGE_ERR("[%s] failed to create work queue\n", __func__);
		goto remove_sysfs;
	}

	INIT_DELAYED_WORK(&info->work, gauge_update);
	queue_delayed_work(info->wq, &info->work, 0 * HZ);

	return 0;

remove_sysfs:
	gauge_remove_sysfs(&pdev->dev);
err_battery:
	power_supply_unregister(&info->battery);
free_weitht_fifo:
	kfifo_free(&info->weight_fifo);
free_fifo:
	kfifo_free(&info->fifo);
free:
	kfree(info);

	return ret;
}

static  int atc260x_gauge_remove(struct platform_device *pdev)
{
	struct atc260x_gauge_info *info = platform_get_drvdata(pdev);

	kfifo_free(&info->fifo);
	kfree(info);
	power_supply_unregister(&info->battery);

	return 0;
}

static int atc260x_bat_prepare(struct device *dev)
{
	struct atc260x_gauge_info *info = dev_get_drvdata(dev);
	if (info->cfg_items.log_switch == 1) {
		info->store_gauge_info_switch_sav = info->cfg_items.log_switch;
		info->cfg_items.log_switch = 0;
	}
	cancel_delayed_work_sync(&info->work);
	getnstimeofday(&suspend_ts);
	GAUGE_DBG("%s suspend_ts.tv_sec(%ld)\n", __func__, suspend_ts.tv_sec);
	GAUGE_INFO("[%s],battery prepare done!\n", __func__);
	return 0;
}

static int atc260x_bat_suspend(struct device *dev)
{
	struct atc260x_gauge_info *info = dev_get_drvdata(dev);
	get_charge_status(&pre_charge_status);
	GAUGE_DBG("pre_charge_status:%d[0:NOT_CHARGE,else:DISCHARGE]",
			pre_charge_status^POWER_SUPPLY_STATUS_NOT_CHARGING);
	GAUGE_ERR("[%s]soc:%d, batv:%dmV\n", __func__, info->soc_show, info->vbatt_avg);
	start_anew(info);

	return 0;
}

static int atc260x_bat_resume(struct device *dev)
{
	struct atc260x_gauge_info *info = dev_get_drvdata(dev);

	info->cfg_items.log_switch = info->store_gauge_info_switch_sav;
	GAUGE_INFO("=====resume:switch= %d\n", info->cfg_items.log_switch);
	GAUGE_ERR("[%s]soc:%d, batv:%dmV\n", __func__, info->soc_show, info->vbatt_avg);

	return 0;
}

static void atc260x_bat_complete(struct device *dev)
{
	struct atc260x_gauge_info *info = dev_get_drvdata(dev);
	struct timespec resume_ts;
	struct timespec sleeping_ts;
	unsigned int soc_consume;
	unsigned int soc_to_show;

	getnstimeofday(&resume_ts);
	GAUGE_DBG("%s resume_ts.tv_sec(%ld)\n", __func__, resume_ts.tv_sec);
	sleeping_ts = timespec_sub(resume_ts, suspend_ts);

	soc_consume = (unsigned int)sleeping_ts.tv_sec * info->cfg_items.suspend_current / (info->cfg_items.capacity * 36);
	GAUGE_DBG("sleeping_ts.tv_sec(%ld)*FULL_CHARGE_SOC(%d)*SUSPEND_CURRENT(%d)/1000/(capacity(%d)*3600)=%u\n",
				sleeping_ts.tv_sec, FULL_CHARGE_SOC, info->cfg_items.suspend_current, info->cfg_items.capacity, soc_consume);

	if ((soc_consume < 1000 && info->soc_now == FULL_CHARGE_SOC) || pre_charge_status == POWER_SUPPLY_STATUS_NOT_CHARGING)
		soc_consume = 0;

	soc_to_show = info->soc_now - soc_consume;
	if (soc_to_show > 0)
		info->soc_now = soc_to_show;
	else
		info->soc_now = 0;

	queue_delayed_work(info->wq, &info->work, 0 * HZ);
}

static const struct dev_pm_ops atc260x_bat_pm_ops = {
	.prepare       = atc260x_bat_prepare,
	.suspend       = atc260x_bat_suspend,
	.resume        = atc260x_bat_resume,
	.complete     = atc260x_bat_complete,
};

static void atc260x_gauge_shutdown(struct platform_device *pdev)
{
	struct atc260x_gauge_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&info->work);
	store_time_pre_shutdown(info);
	if (info->cfg_items.log_switch == 1) {
		info->store_gauge_info_switch_sav = info->cfg_items.log_switch;
		info->cfg_items.log_switch = 0;
	}
	GAUGE_INFO("log_switch = %d\n", info->cfg_items.log_switch);

	GAUGE_ERR("[%s]soc:%d, batv:%dmV\n", __func__, info->soc_show, info->vbatt_avg);
}
/* added by cxj @20141009 */
static const struct of_device_id atc260x_cap_gauge_match[] = {
	{ .compatible = "actions,atc2603a-battery", },
	{ .compatible = "actions,atc2603c-battery", },
	{},
};
MODULE_DEVICE_TABLE(of, atc260x_cap_gauge_match);
static struct platform_driver atc260x_gauge_driver = {
	.probe      = atc260x_gauge_probe,
	.remove     = atc260x_gauge_remove,
	.driver     = {
		.name = "atc2603c-battery",
		.pm = &atc260x_bat_pm_ops,
		.of_match_table = of_match_ptr(atc260x_cap_gauge_match), /* by cxj */
	},
	.shutdown   = atc260x_gauge_shutdown,
};

static int __init atc260x_gauge_init(void)
{
	struct device_node *node =
		of_find_compatible_node(NULL, NULL, "actions,atc2603c-battery");
	if (!node) {
		GAUGE_INFO("%s fail to find atc2603c-battery node\n", __func__);
		return 0;
	}
	GAUGE_INFO("atc2603c_battery:version(%s), time stamp(%s)\n",
		ATC2603C_BATTERY_DRV_VERSION, ATC2603C_BATTERY_DRV_TIMESTAMP);
	return platform_driver_register(&atc260x_gauge_driver);
}
module_init(atc260x_gauge_init);

static void __exit atc260x_gauge_exit(void)
{
	platform_driver_unregister(&atc260x_gauge_driver);
}
module_exit(atc260x_gauge_exit);

/* Module information */
MODULE_AUTHOR("Actions Semi, Inc");
MODULE_DESCRIPTION("ATC260X gauge drv");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atc260x-cap-gauge");
