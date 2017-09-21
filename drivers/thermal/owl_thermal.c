/*
 * owl_thermal.c - Actions OWL TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2014 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/platform_data/owl_thermal.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/bootafinfo.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/atc260x/atc260x.h>

static int debug_flag;

#define CORELOGIC_REGULATOR_NAME	"dcdc0"
struct regulator *vdd_core;

/* In-kernel thermal framework related macros & definations */
#define THERMAL_CLOCK_RATE_S700 24000000
#define THERMAL_CLOCK_RATE_S900 12000000
#define SENSOR_NAME_LEN	20
#define MAX_TRIP_COUNT	8
#define MAX_COOLING_DEVICE 4

#define ACTIVE_INTERVAL 500
#define IDLE_INTERVAL 2000
#define MCELSIUS	1000

/* CPU Zone information */
#define PANIC_ZONE      4
#define WARN_ZONE       3
#define MONITOR_ZONE    2
#define SAFE_ZONE       1

#define GET_ZONE(trip) (trip + 2)
#define GET_TRIP(zone) (zone - 2)

#define OWL_ZONE_COUNT	3

#define MAX_THERMAL_NUM 9
struct owl_tmu_data {
	struct owl_tmu_platform_data *pdata;
	struct resource *mem;
	void __iomem *base;
	int id;
	enum soc_type soc;
	/* struct work_struct irq_work; */
	struct delayed_work thermal_wait_cpufreq_ready_work;
	struct mutex lock;
	struct clk *tmu_clk;
};

struct thermal_trip_point_conf {
	int trip_val[MAX_TRIP_COUNT];
	int trip_count;
	u8 trigger_falling;
};

struct thermal_cooling_conf {
	struct freq_clip_table freq_data[MAX_TRIP_COUNT];
	int freq_clip_count;
};

struct thermal_sensor_conf {
	char name[SENSOR_NAME_LEN];
	int (*read_temperature) (void *data);
	int (*write_emul_temp) (void *drv_data, unsigned long temp);
	struct thermal_trip_point_conf trip_data;
	struct thermal_cooling_conf cooling_data;
	void *private_data;
};

struct owl_thermal_zone {
	enum thermal_device_mode mode;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cool_dev[MAX_COOLING_DEVICE];
	unsigned int cool_dev_size;
	struct platform_device *owl_dev;
	struct thermal_sensor_conf *sensor_conf;
	bool bind;
};

static struct owl_thermal_zone *th_zone[MAX_THERMAL_NUM];
static struct thermal_sensor_conf *owl_sensor_conf[MAX_THERMAL_NUM];
static struct chipid_data sensor_afi_data;
static const char *devname[10] = {
	"CPU0",
	"CPU1",
	"CPU2",
	"CPU3",
	"GPU0",
	"GPU1",
	"HDE",
	"VDE",
	"CoreLogic",
	"Unknown",
};
enum thermal_id {
	CPU0 = 0x0,
	CPU1 = 0x1,
	CPU2 = 0x2,
	CPU3 = 0x3,
	GPU0 = 0x4,
	GPU1 = 0x5,
	HDE = 0x6,
	VDE = 0x7,
	CoreLogic = 0x8,
	Unknown = 0x9,
};

static void owl_unregister_thermal(int device_id);
static int owl_register_thermal(struct thermal_sensor_conf *sensor_conf);
static int tmu_clk_enable(struct owl_tmu_data *data, bool enable);

/* Get mode callback functions for thermal zone */
static int owl_get_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode *mode)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;
	if (th_zone[index])
		*mode = th_zone[index]->mode;
	return 0;
}

/* Set mode callback functions for thermal zone */
static int owl_set_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode mode)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (!th_zone[index]->therm_dev) {
		pr_notice("thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&th_zone[index]->therm_dev->lock);

	if (mode == THERMAL_DEVICE_ENABLED)
		th_zone[index]->therm_dev->polling_delay = IDLE_INTERVAL;
	else
		th_zone[index]->therm_dev->polling_delay = 0;

	mutex_unlock(&th_zone[index]->therm_dev->lock);

	th_zone[index]->mode = mode;
	thermal_zone_device_update(th_zone[index]->therm_dev);
	pr_info("thermal polling set for duration=%d msec\n",
		th_zone[index]->therm_dev->polling_delay);
	return 0;
}

/* Get trip type callback functions for thermal zone */
static int owl_get_trip_type(struct thermal_zone_device *thermal, int trip,
			     enum thermal_trip_type *type)
{
	switch (GET_ZONE(trip)) {
	case MONITOR_ZONE:
	case WARN_ZONE:
		*type = THERMAL_TRIP_ACTIVE;
		break;
	case PANIC_ZONE:
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int owl_get_trip_temp(struct thermal_zone_device *thermal, int trip,
			     unsigned long *temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	*temp = th_zone[index]->sensor_conf->trip_data.trip_val[trip];

	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

/* Set trip temperature callback functions for thermal zone */
static int owl_set_trip_temp(struct thermal_zone_device *thermal, int trip,
			     unsigned long temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	pr_notice("set temp%d= %ld\n", trip, temp);
	th_zone[index]->sensor_conf->trip_data.trip_val[trip] = temp / MCELSIUS;
	return 0;
}

/* Get trip hyst temperature callback functions for thermal zone */
static int owl_get_trip_hyst(struct thermal_zone_device *thermal, int trip,
			     unsigned long *temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	*temp = th_zone[index]->sensor_conf->trip_data.trigger_falling;
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int owl_get_crit_temp(struct thermal_zone_device *thermal,
			     unsigned long *temp)
{
	int ret;

	/* Panic zone */
	pr_info("owl_get_crit_temp\n");
	ret = owl_get_trip_temp(thermal, GET_TRIP(PANIC_ZONE), temp);
	return ret;
}

/* Bind callback functions for thermal zone */
static int owl_bind(struct thermal_zone_device *thermal,
		    struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size, level;
	int index;
	struct owl_tmu_data *data_tmu;
	struct freq_clip_table *tab_ptr, *clip_data;
	struct thermal_sensor_conf *data;

	data_tmu = (struct owl_tmu_data *)(thermal->devdata);
	index = data_tmu->id;

	data = th_zone[index]->sensor_conf;
	tab_ptr = (struct freq_clip_table *)data->cooling_data.freq_data;
	tab_size = data->cooling_data.freq_clip_count;

	if (tab_ptr == NULL || tab_size == 0)
		return -EINVAL;

	/* find the cooling device registered */
	for (i = 0; i < th_zone[index]->cool_dev_size; i++)
		if (cdev == th_zone[index]->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone[index]->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		clip_data = (struct freq_clip_table *)&(tab_ptr[i]);
		pr_debug("clip_data->freq_clip_max = %d\n",
						clip_data->freq_clip_max);
		level = cpufreq_cooling_get_level(0, clip_data->freq_clip_max);
		if (level == THERMAL_CSTATE_INVALID)
			return 0;
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_bind_cooling_device
			    (thermal, i, cdev, level, 0)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
			}
			th_zone[index]->bind = true;
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Unbind callback functions for thermal zone */
static int owl_unbind(struct thermal_zone_device *thermal,
		      struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size;
	int index;
	struct owl_tmu_data *data_tmu;
	struct thermal_sensor_conf *data;

	data_tmu = (struct owl_tmu_data *)(thermal->devdata);
	index = data_tmu->id;
	data = th_zone[index]->sensor_conf;

	if (th_zone[index]->bind == false)
		return 0;

	tab_size = data->cooling_data.freq_clip_count;

	if (tab_size == 0)
		return -EINVAL;

	/* find the cooling device registered */
	for (i = 0; i < th_zone[index]->cool_dev_size; i++)
		if (cdev == th_zone[index]->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone[index]->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_unbind_cooling_device
			    (thermal, i, cdev)) {
				pr_err("error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone[index]->bind = false;
			break;
		default:
			ret = -EINVAL;
		}
	}
	return ret;
}

extern unsigned long debug_temp;
/* Get temperature callback functions for thermal zone */
static int owl_get_temp(struct thermal_zone_device *thermal,
			unsigned long *temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (!th_zone[index]->sensor_conf) {
		pr_info("Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone[index]->sensor_conf->private_data;
	*temp = th_zone[index]->sensor_conf->read_temperature(data);
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;
	
	if (debug_temp)
		*temp = debug_temp;
	return 0;
}

/* Get the temperature trend */
static int owl_get_trend(struct thermal_zone_device *thermal, int trip,
			 enum thermal_trend *trend)
{
	int ret;
	unsigned long trip_temp;

	ret = owl_get_trip_temp(thermal, trip, &trip_temp);
	if (ret < 0)
		return ret;

	if (thermal->temperature >= trip_temp)
		*trend = THERMAL_TREND_RAISE_FULL;
	else
		*trend = THERMAL_TREND_DROP_FULL;

	return 0;
}

/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops owl_dev_ops = {
	.bind = owl_bind,
	.unbind = owl_unbind,
	.get_temp = owl_get_temp,
	.get_trend = owl_get_trend,
	.get_mode = owl_get_mode,
	.set_mode = owl_set_mode,
	.get_trip_type = owl_get_trip_type,
	.get_trip_temp = owl_get_trip_temp,
	.set_trip_temp = owl_set_trip_temp,
	.get_trip_hyst = owl_get_trip_hyst,
	.get_crit_temp = owl_get_crit_temp,
};

/* Register with the in-kernel thermal management */
static int owl_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret;
	struct cpumask mask_val;
	struct owl_tmu_data *data = sensor_conf->private_data;
	if (!sensor_conf || !sensor_conf->read_temperature) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone[data->id] =
	    kzalloc(sizeof(struct owl_thermal_zone), GFP_KERNEL);
	if (!th_zone[data->id])
		return -ENOMEM;

	th_zone[data->id]->sensor_conf = sensor_conf;
	cpumask_set_cpu(0, &mask_val);
	th_zone[data->id]->cool_dev[0] = cpufreq_cooling_register(&mask_val);
	if (IS_ERR(th_zone[data->id]->cool_dev[0])) {
		pr_err("Failed to register cpufreq cooling device\n");
		ret = -EINVAL;
		goto err_unregister;
	} else {
		pr_info("OK cooling device register OK!\n");
	}
	th_zone[data->id]->cool_dev_size++;
	pr_info("Begin to register thermal zone ,id = %d\n", data->id);

	th_zone[data->id]->therm_dev =
	    thermal_zone_device_register(sensor_conf->name, OWL_ZONE_COUNT, 0x7,
					 (void *)(sensor_conf->private_data),
					 &owl_dev_ops, NULL, 0, IDLE_INTERVAL);

	if (IS_ERR(th_zone[data->id]->therm_dev)) {
		pr_err("Failed to register thermal zone device\n");
		ret = PTR_ERR(th_zone[data->id]->therm_dev);
		goto err_unregister;
	}

	th_zone[data->id]->mode = THERMAL_DEVICE_ENABLED;

	pr_info("OWL: Kernel Thermal management registered\n");

	return 0;

err_unregister:
	owl_unregister_thermal(data->id);
	return ret;
}

/* Un-Register with the in-kernel thermal management */
static void owl_unregister_thermal(int device_id)
{
	int i;

	if (!th_zone[device_id])
		return;

	if (th_zone[device_id]->therm_dev)
		thermal_zone_device_unregister(th_zone[device_id]->therm_dev);

	for (i = 0; i < th_zone[device_id]->cool_dev_size; i++) {
		if (th_zone[device_id]->cool_dev[i])
			cpufreq_cooling_unregister(th_zone[device_id]->
						   cool_dev[i]);
	}

	kfree(th_zone[device_id]);
	pr_info("OWL: Kernel Thermal management unregistered\n");
}

static int tmu_clk_enable(struct owl_tmu_data *data, bool enable)
{
	if (enable)
		clk_prepare_enable(data->tmu_clk);
	else
		clk_disable_unprepare(data->tmu_clk);

	return 0;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 * T = 838.45*7.894/(1024*12/count+7.894)-275+offset
 */
/*static int offset;*/
static int code_to_temp_s700(struct owl_tmu_data *data, u32 temp_code)
{
	long long temp1, temp2, temp3;
	int tmp_data, tmp_flag;
	unsigned short temp;

	temp = *(unsigned short *)(sensor_afi_data.vdd_sensor);

	tmp_data = temp & 0x7f;
	tmp_flag = (temp >> 7) & 0x1;

	if (tmp_flag == 1)
		temp3 = temp_code - tmp_data;
	else
		temp3 = temp_code + tmp_data;

	if (debug_flag)
		pr_info("tmp_data = %d\n", tmp_data);

/*	pr_info("tmp code=%d, sensor_afi_data=%d sum=%d\n",
		temp_code, sensor_afi_data, temp3); */

/*	D= A+B
	T=5.6534D2/10000 + 1.1513D - 447.1272 */
	temp1 = 11513 * temp3;
	temp2 = temp3 * temp3;
	temp3 = temp1 - 4471272;

	temp2 = 56534*temp2;
	temp2 = temp2/10000;
	temp1 = temp3 - temp2;
	temp2 = temp1/10000;
	if (temp >> 15) {
		temp2 = temp2 - 10;
		return temp2;
	} else {
		return temp2;
	}
}

static int code_to_temp_s900(struct owl_tmu_data *data, u32 temp_code)
{
	enum thermal_id current_id;
	int temp1, temp2, sensor_data;
	/*4562 25 centigrade 330，substitute 2890 to chipid */
	current_id = data->id;
	sensor_data = 0;
	switch (current_id) {
	case CPU0:
		sensor_data = *(unsigned short *)sensor_afi_data.cpu_sensor;
		if (debug_flag) {
			pr_info("%s:sensor date = %d\n ",
					devname[data->id], sensor_data);
		}
		break;
	case CPU1:
		break;
	case CPU2:
		break;
	case CPU3:
		break;
	case GPU0:
		/*sensor_data = *(unsigned short *)sensor_afi_data.gpu_sensor;
		if (debug_flag) {
			pr_info("%s:sensor date = %d\n ",
					devname[data->id],sensor_data);
		}*/
		break;
	case GPU1:
		break;
	case HDE:
		break;
	case VDE:
		break;
	case CoreLogic:
		sensor_data = *(unsigned short *)sensor_afi_data.vdd_sensor;
		if (debug_flag) {
			pr_info("%s:sensor date = %d\n ",
				devname[data->id], sensor_data);
		}
		break;
	case Unknown:
		break;
	default:
		break;
	}

	if (!sensor_data) {
		temp1 = 25;
		return temp1;
	}
	temp1 = 318326 * sensor_data;
	temp2 = temp_code * 10000;
	temp1 = temp1 / temp2;
	temp1 = 296 - temp1;
	temp1 = 950 * temp1 - 1243;
	temp1 = temp1/1000;
	return temp1;
}

static int owl_tmu_initialize(struct platform_device *pdev)
{
	return 0;
}

static void owl_tmu_control(struct platform_device *pdev, bool on)
{
	return;
}


#define TRY_TEMP_CNT	5
#define TRY_FAIL_COUNT  5
/*clk=24M,divide = 2,temp data = 500,
so readtime=(1/(24M/2) )*500 *10= 0.4ms,
we set 1ms*/
#define TIME_OUT 1
static u32 owl_tmu_temp_code_read_s700(struct owl_tmu_data *data)
{
	u32 tmp, temp_code, temp_arry[TRY_TEMP_CNT];
	int ready, i, j, k;
	void __iomem *TSCR0;

	TSCR0 = data->base;
	temp_code = 0;
	tmu_clk_enable(data, true);
	for (i = 0; i < TRY_TEMP_CNT; i++) {
		tmp = readl(TSCR0);
		/* enable tsensor */
		tmp = tmp | (1 << 21) | (0x21 << 12) | (1 << 28) | (1 << 19);
		writel(tmp, TSCR0);
		ready = 0;
		k = 0;
		do {
			tmp = readl(TSCR0);
			ready = tmp & (1 << 11);
			k++;
			usleep_range(1500, 2000);
			if (k == TRY_FAIL_COUNT)
				break;
			/*pr_info("HUANGXU:ready= %d\n ",ready); */
			/*pr_info("HUANGXU:k= %d\n ",k); */
		} while ((ready == 0));

		if (k == TRY_FAIL_COUNT) {
			if (debug_flag)
				pr_info
				    ("%s not ready, use default tempurature\n ",
				     devname[data->id]);
			/*25 centigrade */
			temp_code = 340;
			goto finish;
		} else {
			if (debug_flag) {
				pr_info("%s ready, after fail count %d\n ",
					devname[data->id], k);
			}
		}
		temp_arry[i] = tmp & 0x3ff;
		tmp = readl(TSCR0);
		tmp = tmp & (~0xffffffff);	/* disable tsensor */
		writel(tmp, TSCR0);
		if (debug_flag)
			pr_info("cpu0 tmp arrry[%d] = %d\n", i,  temp_arry[i]);
	}
#if 1
	/* sort temp arry */
	for (i = 0; i < TRY_TEMP_CNT - 1; i++) {
		for (j = i + 1; j < TRY_TEMP_CNT; j++) {
			if (temp_arry[j] < temp_arry[i]) {
				tmp = temp_arry[i];
				temp_arry[i] = temp_arry[j];
				temp_arry[j] = tmp;
			}
		}
	}

	/* discard min & max, then take ther average */
	for (i = 1; i < TRY_TEMP_CNT - 1; i++)
		temp_code += temp_arry[i];

	temp_code = temp_code / (TRY_TEMP_CNT - 2);
	if (debug_flag)
		pr_info("%s temp_code:%d\n", devname[data->id], temp_code);
#endif

finish:
	tmu_clk_enable(data, false);
	return temp_code;
}

static u32 owl_tmu_temp_code_read_s900(struct owl_tmu_data *data)
{
	u32 tmp, temp_code, temp_arry[TRY_TEMP_CNT];
	int ready, i, j, k;
	void __iomem *TSCR0;

	TSCR0 = data->base;
	temp_code = 0;
	tmu_clk_enable(data, true);
	for (i = 0; i < TRY_TEMP_CNT; i++) {
		tmp = readl(TSCR0);
		tmp = tmp | (1 << 24);	/* enable tsensor */
		writel(tmp, TSCR0);
		ready = 0;
		k = 0;
		do {
			tmp = readl(TSCR0);
			ready = tmp & (1 << 12);
			k++;
			usleep_range(800, 1000);
			if (k == TRY_FAIL_COUNT)
				break;
			/*pr_info("HUANGXU:ready= %d\n ",ready); */
			/*pr_info("HUANGXU:k= %d\n ",k); */
		} while ((ready == 0));
		if (k == TRY_FAIL_COUNT) {
			if (debug_flag)
				pr_info
				    ("%s not ready, use default tempurature\n ",
				     devname[data->id]);
			/*25 centigrade */
			temp_code = 340;
			goto finish;
		} else {
			if (debug_flag) {
				pr_info("%s ready, after fail count %d\n ",
					devname[data->id], k);
			}
		}
		temp_arry[i] = tmp & 0x3ff;
		tmp = readl(TSCR0);
		tmp = tmp & (~(1 << 24));	/* disable tsensor */
		writel(tmp, TSCR0);
	}
#if 1
	/* sort temp arry */
	for (i = 0; i < TRY_TEMP_CNT - 1; i++) {
		for (j = i + 1; j < TRY_TEMP_CNT; j++) {
			if (temp_arry[j] < temp_arry[i]) {
				tmp = temp_arry[i];
				temp_arry[i] = temp_arry[j];
				temp_arry[j] = tmp;
			}
		}
	}

	/* discard min & max, then take ther average */
	for (i = 1; i < TRY_TEMP_CNT - 1; i++)
		temp_code += temp_arry[i];

	temp_code = temp_code / (TRY_TEMP_CNT - 2);
	if (debug_flag)
		pr_info("%s temp_code:%d\n", devname[data->id], temp_code);
#endif

finish:
	tmu_clk_enable(data, false);
	return temp_code;
}

static int owl_tmu_read_s700(struct owl_tmu_data *data)
{
	int temp_code;
	int temp;

	mutex_lock(&data->lock);

	temp_code = owl_tmu_temp_code_read_s700(data);
	temp = code_to_temp_s700(data, temp_code);

	if (debug_flag) {
		pr_info("%s:Current tempurature is %d centigrade\n",
			devname[data->id], temp);
	}

	mutex_unlock(&data->lock);
	return temp;
}

static int owl_tmu_read_s900(struct owl_tmu_data *data)
{
	int temp_code;
	int temp;
	int temp_save;

	mutex_lock(&data->lock);
	temp_save = regulator_get_voltage(vdd_core);
	if (debug_flag)
		pr_info("vdd temp_save =%d\n", temp_save);

	temp = regulator_set_voltage(vdd_core, 1000000, 1000000);
	if (temp) {
		pr_err("Failed to set vdd_core to 1V\n");
		mutex_unlock(&data->lock);
		return temp;
	}
	if (debug_flag)
		pr_info("vdd =%d\n", regulator_get_voltage(vdd_core));

	temp_code = owl_tmu_temp_code_read_s900(data);
	temp = regulator_set_voltage(vdd_core, temp_save, INT_MAX);
	if (temp) {
		pr_err("Failed to set vdd_core to 1V\n");
		mutex_unlock(&data->lock);
		return temp;
	}

	temp = code_to_temp_s900(data, temp_code);
	if (debug_flag) {
		pr_info("%s:Current tempurature is %d centigrade\n",
			devname[data->id], temp);
	}
	mutex_unlock(&data->lock);
	return temp;
}

static ssize_t show_debug(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return sprintf(buf, "%d\n", debug_flag);
}

static ssize_t store_debug(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	if (sscanf(buf, "%d", &debug_flag) != 1)
		return -EINVAL;
	return count;
}

static DEVICE_ATTR(debug, 0644, show_debug, store_debug);

static struct owl_tmu_platform_data s700_default_tmu_data = {
	.threshold_falling = 10,
	.threshold = 100,
	.trigger_levels[0] = 0,
	.trigger_levels[1] = 10,
	.trigger_levels[2] = 20,
	.trigger_level0_en = 1,
	.trigger_level1_en = 1,
	.trigger_level2_en = 1,
	.trigger_level3_en = 0,
	.freq_tab_count = 2,
	.type = SOC_ARCH_OWL_S700,
};

#define S700_TMU_DRV_DATA (&s700_default_tmu_data)

static struct owl_tmu_platform_data s900_default_tmu_data = {
	.threshold_falling = 10,
	.threshold = 100,
	.trigger_levels[0] = 0,
	.trigger_levels[1] = 10,
	.trigger_levels[2] = 20,
	.trigger_level0_en = 1,
	.trigger_level1_en = 1,
	.trigger_level2_en = 1,
	.trigger_level3_en = 0,
	.freq_tab_count = 2,
	.type = SOC_ARCH_OWL_S900,
};

#define S900_TMU_DRV_DATA (&s900_default_tmu_data)

#ifdef CONFIG_OF
static const struct of_device_id owl_tmu_match[] = {
	{
	 .compatible = "actions,gt7-thermal",
	 .data = (void *)S700_TMU_DRV_DATA,
	 },
	{
	 .compatible = "actions,s900-thermal",
	 .data = (void *)S900_TMU_DRV_DATA,
	 },
	{},
};

MODULE_DEVICE_TABLE(of, owl_tmu_match);
#endif

static struct platform_device_id owl_tmu_driver_ids[] = {
	{
	 .name = "s700-tmu",
	 .driver_data = (kernel_ulong_t) S700_TMU_DRV_DATA,
	 },
	{
	 .name = "s900-tmu",
	 .driver_data = (kernel_ulong_t) S900_TMU_DRV_DATA,
	 },
	{},
};

MODULE_DEVICE_TABLE(platform, owl_tmu_driver_ids);

static inline struct owl_tmu_platform_data *owl_get_driver_data(struct
								platform_device
								*pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(owl_tmu_match, pdev->dev.of_node);
		if (!match)
			return NULL;
		return (struct owl_tmu_platform_data *)match->data;
	}
#endif
	return (struct owl_tmu_platform_data *)
	    platform_get_device_id(pdev)->driver_data;
}

/*
wait for cpufreq ready
 */
static void thermal_wait_cpufreq_ready(struct work_struct *work)
{
	int ret = 0;
	int i;
	struct owl_tmu_data *data;
	struct owl_tmu_platform_data *pdata;
	struct cpufreq_frequency_table *table;

	data = container_of(work, struct owl_tmu_data,
					thermal_wait_cpufreq_ready_work.work);
	table = cpufreq_frequency_get_table(0);
	if (NULL == table) {
		schedule_delayed_work(
				&data->thermal_wait_cpufreq_ready_work,
				HZ);
		pr_notice("[%s], cpufreq not ready\n", __func__);
		return;
	}
	pr_notice("[%s], cpufreq ready\n", __func__);
	pdata = data->pdata;
	pdata->freq_tab[0].freq_clip_max = table[1].frequency;
	pdata->freq_tab[1].freq_clip_max = table[0].frequency;
	for (i = 0; i < pdata->freq_tab_count; i++) {
		pr_notice("[%s], freq_tab[%d].freq_clip_max:%d\n", __func__,
					i, pdata->freq_tab[i].freq_clip_max);
		owl_sensor_conf[data->id]->cooling_data.freq_data[i].
		    freq_clip_max = pdata->freq_tab[i].freq_clip_max;
	}

	ret = owl_register_thermal(owl_sensor_conf[data->id]);
	if (ret)
		pr_err("Failed to register thermal interface\n");
	return;
}


/*
set tmu threshold according to opt_flag
 */
int set_tmu_threshold(struct owl_tmu_platform_data *pdata)
{
#if 1
	if (pdata->type == SOC_ARCH_OWL_S700) {
		pdata->threshold = 105;
		pdata->trigger_levels[0] = 0;	/* 95° */
		pdata->trigger_levels[1] = 10;	/* 105° */
		pdata->trigger_levels[2] = 15;	/* 110° */
	} else if (pdata->type == SOC_ARCH_OWL_S900) {
		pdata->threshold = 105;
		pdata->trigger_levels[0] = 0;	/* 110° */
		pdata->trigger_levels[1] = 10;	/* 115° */
		pdata->trigger_levels[2] = 15;	/* 120° */
	}
#else
	pdata->threshold = INT_MAX;
	pdata->trigger_levels[0] = 0;	/* 50° */
	pdata->trigger_levels[1] = 0;	/* 60° */
	pdata->trigger_levels[2] = 0;	/* 70° */
#endif

	return 0;
}

static int owl_tmu_probe(struct platform_device *pdev)
{
	struct owl_tmu_data *data;
	struct owl_tmu_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	int ret = 0, i;
	if (!pdata)
		pdata = owl_get_driver_data(pdev);

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	if (!(pdata->type == SOC_ARCH_OWL_S700
			|| pdata->type == SOC_ARCH_OWL_S900)) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Platform not supported\n");
		goto err_clk;
	}

	owl_afi_get_sensor(&sensor_afi_data);
	pr_info("sensor_afi_data.cpu_sensor = 0x%x\n",
		*(unsigned short *)sensor_afi_data.cpu_sensor);
	pr_info("sensor_afi_data.vdd_sensor = 0x%x\n",
		*(unsigned short *)sensor_afi_data.vdd_sensor);
	set_tmu_threshold(pdata);

	data =
	    devm_kzalloc(&pdev->dev, sizeof(struct owl_tmu_data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	if (of_property_read_u32(np, "id", &pdev->id)) {
		dev_err(&pdev->dev, "Failed to get pdev->id!\n");
		return -EINVAL;
	}
	pr_info("PDEVID = %d\n", pdev->id);

	data->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pr_info("tmu-mem:start=0x%llu\n", data->mem->start);
#if 0
	data->base = devm_ioremap_resource(&pdev->dev, data->mem);
#else
	/* reg of thermal conflict with pinctrl in dts */
	data->base = ioremap_nocache(data->mem->start,
					data->mem->end - data->mem->start + 1);
#endif
	pr_info("tmu-mem:data->base=0x%p\n", data->base);

	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->id = pdev->id;
	data->soc = pdata->type;

	data->tmu_clk = devm_clk_get(&pdev->dev, "thermal_sensor");
	if (IS_ERR(data->tmu_clk))
		return PTR_ERR(data->tmu_clk);

	if (pdata->type == SOC_ARCH_OWL_S700)
		clk_set_rate(data->tmu_clk, THERMAL_CLOCK_RATE_S700);
	else if (pdata->type == SOC_ARCH_OWL_S900)
		clk_set_rate(data->tmu_clk, THERMAL_CLOCK_RATE_S900);

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);
	ret = owl_tmu_initialize(pdev);

	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	owl_tmu_control(pdev, true);

	owl_sensor_conf[data->id] =
	    kzalloc(sizeof(struct thermal_sensor_conf), GFP_KERNEL);

	snprintf(owl_sensor_conf[data->id]->name, SENSOR_NAME_LEN,
		 "owl-thermal-%s", devname[data->id]);

	if (pdata->type == SOC_ARCH_OWL_S700)
		owl_sensor_conf[data->id]->read_temperature =
			(int (*)(void *))owl_tmu_read_s700;
	else if (pdata->type == SOC_ARCH_OWL_S900)
		owl_sensor_conf[data->id]->read_temperature =
			(int (*)(void *))owl_tmu_read_s900;

	(owl_sensor_conf[data->id])->private_data = data;

	owl_sensor_conf[data->id]->trip_data.trip_count =
	    pdata->trigger_level0_en + pdata->trigger_level1_en +
	    pdata->trigger_level2_en + pdata->trigger_level3_en;

	for (i = 0; i < owl_sensor_conf[data->id]->trip_data.trip_count; i++)
		owl_sensor_conf[data->id]->trip_data.trip_val[i] =
		    pdata->threshold + pdata->trigger_levels[i];

	owl_sensor_conf[data->id]->trip_data.trigger_falling =
		pdata->threshold_falling;

	owl_sensor_conf[data->id]->cooling_data.freq_clip_count =
		pdata->freq_tab_count;
	for (i = 0; i < pdata->freq_tab_count; i++) {
		owl_sensor_conf[data->id]->cooling_data.freq_data[i].
			freq_clip_max = pdata->freq_tab[i].freq_clip_max;
		owl_sensor_conf[data->id]->cooling_data.freq_data[i].
			temp_level = pdata->freq_tab[i].temp_level;
	}

	if (pdata->type == SOC_ARCH_OWL_S900) {
		vdd_core = regulator_get(NULL, CORELOGIC_REGULATOR_NAME);
		if (IS_ERR(vdd_core)) {
			ret = PTR_ERR(vdd_core);
			pr_err("%s: regulator_get failed: %d\n", __func__, ret);
			return -ENODEV;
		}
	}

	INIT_DELAYED_WORK(
			&data->thermal_wait_cpufreq_ready_work,
			thermal_wait_cpufreq_ready);
	schedule_delayed_work(&data->thermal_wait_cpufreq_ready_work, HZ);

	ret = device_create_file(&pdev->dev, &dev_attr_debug);
	if (ret) {
		pr_err("Failed to register thermal platform sysfs interface\n");
		return ret;
	}

	return 0;

err_clk:
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int owl_tmu_remove(struct platform_device *pdev)
{
	owl_tmu_control(pdev, false);

	owl_unregister_thermal(pdev->id);

	device_remove_file(&pdev->dev, &dev_attr_debug);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int owl_tmu_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	/*struct owl_tmu_data *data = dev_get_drvdata(dev); */

	owl_tmu_initialize(pdev);
	/*tmu_clk_enable(data, false); */
	owl_tmu_control(pdev, false);

	return 0;
}

static int owl_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	/*struct owl_tmu_data *data = dev_get_drvdata(dev); */

	owl_tmu_initialize(pdev);
	/*tmu_clk_enable(data, true); */
	owl_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(owl_tmu_pm, owl_tmu_suspend, owl_tmu_resume);
#define OWL_TMU_PM	(&owl_tmu_pm)
#else
#define OWL_TMU_PM	NULL
#endif

static struct platform_driver owl_tmu_driver = {
	.driver = {
		   .name = "owl-tmu",
		   .owner = THIS_MODULE,
		   .pm = OWL_TMU_PM,
		   .of_match_table = of_match_ptr(owl_tmu_match),
		   },
	.probe = owl_tmu_probe,
	.remove = owl_tmu_remove,
	.id_table = owl_tmu_driver_ids,
};

module_platform_driver(owl_tmu_driver);

MODULE_DESCRIPTION("OWL TMU Driver");
MODULE_AUTHOR("Actions (Zhuhai) Technology Co., Limited");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:owl-tmu");
