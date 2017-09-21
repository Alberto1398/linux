/*************************************************************************/ /*!
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
@Description    System Configuration functions
*/ /**************************************************************************/

#include <linux/kernel.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/errno.h>

#include "syslocal.h"
#include "gpudvfs.h"

#define DVFS_SAMPLE_WINDOW_MS (200)
#define SCALING_UP_THRESHOLD (98)
#define SCALING_UP_THRESHOLD_CRITICAL (99)
#define SCALING_DOWN_THRESHOLD (50)
#define SCALING_DOWN_THRESHOLD_NODELAY (3)
#define SCALING_DOWN_FACTOR (20)

#define GPU_KOBJ_ATTR(_name, _mode, _show, _store) \
	struct kobj_attribute gpu_attr_##_name = \
	__ATTR(_name, _mode, _show, _store)

struct gpufreq_dvfs
{
	DVFS_TAG dvfs;
	struct gpufreq_governor *governor;

	int cur_level;
	int max_level_lock;
	int min_level_lock;
#if defined(CONFIG_THERMAL)
	int thermal_level_lock;
#endif

	unsigned long *freq_table;
	int num_level;

	DVFS_UTIL_STAT *psUtil;

	struct gpufreq_driver *driver;
	struct kobject *sysfs_kobj;

	struct rw_semaphore dvfs_rwsem;
	struct mutex sysfs_mutex;
	struct mutex dvfs_enable_mutex;
};

static DVFS_UTIL_STAT gpuutil = {
	.valid = false,
	.active = 100,
	.blocked = 0,
	.idle    = 0,
	.memstall = 100,
};

static struct gpufreq_dvfs gpudvfs = {
	.governor = NULL,
	.psUtil = &gpuutil,
};

static int target_level_powersave(DVFS_UTIL_STAT *psUtil);
static int target_level_performance(DVFS_UTIL_STAT *psUtil);
static int target_level_conservative(DVFS_UTIL_STAT *psUtil);
static int target_level_user(DVFS_UTIL_STAT *psUtil);

static struct gpufreq_governor dvfs_governor[NUM_GPUFREQ_GOVERNOR] = {
	{
		.governor = GPUFREQ_GOVERNOR_POWERSAVE,
		.get_target_level = target_level_powersave,
	},
	{
		.governor = GPUFREQ_GOVERNOR_PERFORMANCE,
		.get_target_level = target_level_performance,
	},
	{
		.governor = GPUFREQ_GOVERNOR_CONSERVATIVE,
		.get_target_level = target_level_conservative,
	},
	{
		.governor = GPUFREQ_GOVERNOR_USER,
		.get_target_level = target_level_user,
	},	
};

// relation: 1 higher >=, 0 lower <=
static int match_clocklevel(unsigned long freq, int relation)
{
	int i;
	for (i = 0; i < gpudvfs.num_level; i++)
		if (freq >= gpudvfs.freq_table[i])
			break;

	if (i == gpudvfs.num_level)
		return gpudvfs.num_level - 1;
	else if (i == 0 || freq == gpudvfs.freq_table[i])
		return i;
	else if (relation == RELATION_L)
		return i;
	else
		return i - 1;
}

static int gpu_dvfs_get_utilisation(void)
{
	int utilization;

	down_read(&gpudvfs.dvfs_rwsem);

	if (!gpudvfs.dvfs.bEnable)
		SysDvfsUpdateUtilStat(&gpudvfs.dvfs, gpudvfs.psUtil);

	utilization = gpudvfs.psUtil->valid ? gpudvfs.psUtil->active : 0;

	up_read(&gpudvfs.dvfs_rwsem);

	return utilization;
}

int gpu_dvfs_get_current_level(void)
{
	return gpudvfs.cur_level;
}

int gpu_dvfs_get_maximum_level(void)
{
	return gpudvfs.num_level - 1;
}

int gpu_dvfs_get_level(unsigned long freq)
{
	return match_clocklevel(freq, RELATION_L);
}

static int set_level(int level)
{
	int err = 0;

	if (gpudvfs.dvfs.preClockSpeedChange) {
		err = gpudvfs.dvfs.preClockSpeedChange(true);
		if (err)
			return err;
	}

	err = gpudvfs.driver->set_clockspeed(gpudvfs.freq_table[level]);
	if (!err)
		gpudvfs.cur_level = level;

	if (gpudvfs.dvfs.postClockSpeedChange)
		gpudvfs.dvfs.postClockSpeedChange(true);

	return err;
}

int gpu_dvfs_set_level(int level)
{
	int err = 0;

	down_write(&gpudvfs.dvfs_rwsem);

	if (level < gpudvfs.min_level_lock ||
		level > gpudvfs.max_level_lock) {
		err = -EINVAL;
		goto err_up_rwsem;
	}

#if defined(CONFIG_THERMAL)
	if (level < gpudvfs.thermal_level_lock)
		level = gpudvfs.thermal_level_lock;
#endif

	if (level != gpudvfs.cur_level)
		err = set_level(level);

err_up_rwsem:
	up_write(&gpudvfs.dvfs_rwsem);

	return err;
}

int gpu_dvfs_get_level_range(int *minlevel, int *maxlevel)
{
	down_read(&gpudvfs.dvfs_rwsem);

	*minlevel = gpudvfs.min_level_lock;
	*maxlevel = gpudvfs.max_level_lock;

	up_read(&gpudvfs.dvfs_rwsem);

	return 0;
}

int gpu_dvfs_set_level_range(int minlevel, int maxlevel)
{
	int err = 0, level;

	if (minlevel < 0 ||
		maxlevel >= gpudvfs.num_level ||
		minlevel > maxlevel)
		return -EINVAL;

	down_write(&gpudvfs.dvfs_rwsem);

	gpudvfs.min_level_lock = minlevel;
	gpudvfs.max_level_lock = maxlevel;

	level = gpudvfs.cur_level;
	if (level < minlevel)
		level = minlevel;
	else if (level > maxlevel)
		level = maxlevel;

#if defined(CONFIG_THERMAL)
	if (level < gpudvfs.thermal_level_lock)
		level = gpudvfs.thermal_level_lock;
#endif

	if (level != gpudvfs.cur_level)
		err = set_level(level);

	up_write(&gpudvfs.dvfs_rwsem);

	return err;
}

#if defined(CONFIG_THERMAL)
int gpu_dvfs_set_thermal_stat(enum GPUFREQ_THERMAL_STAT eStat)
{
	int err = 0, tlevel;

	if (eStat < 0 || eStat >= NUM_GPUFREQ_THERMAL_STAT)
		return -EINVAL;

	down_write(&gpudvfs.dvfs_rwsem);

	switch (eStat) {
	default:
	case GPUFREQ_THERMAL_NORMAL:
		tlevel = 0;
		break;
	case GPUFREQ_THERMAL_HOT:
		if (gpudvfs.governor->governor == GPUFREQ_GOVERNOR_POWERSAVE    ||
			gpudvfs.governor->governor == GPUFREQ_GOVERNOR_PERFORMANCE ||
			gpudvfs.governor->governor == GPUFREQ_GOVERNOR_USER)
			tlevel = gpudvfs.cur_level + 1;
		else
			tlevel = gpudvfs.thermal_level_lock + 1;
		tlevel = (tlevel >= gpudvfs.num_level) ? (gpudvfs.num_level - 1) : tlevel;
		break;
	case GPUFREQ_THERMAL_CRITICAL:
		tlevel = gpudvfs.num_level - 1;
		break;
	}

	gpudvfs.thermal_level_lock = tlevel;

	if (tlevel > gpudvfs.cur_level)
		err = set_level(tlevel);

	up_write(&gpudvfs.dvfs_rwsem);

	return err ? -EINVAL : tlevel;	
}
#endif

static int target_level_powersave(DVFS_UTIL_STAT *psUtil)
{
	return gpudvfs.num_level - 1;
}

static int target_level_performance(DVFS_UTIL_STAT *psUtil)
{
#if defined(CONFIG_THERMAL)
	return gpudvfs.thermal_level_lock;
#else
	return 0;
#endif
}

static int target_level_user(DVFS_UTIL_STAT *psUtil)
{
#if defined(CONFIG_THERMAL)
	return (gpudvfs.cur_level > gpudvfs.thermal_level_lock) ?
			gpudvfs.cur_level : gpudvfs.thermal_level_lock;
#else
	return gpudvfs.cur_level;
#endif
}

static int target_level_conservative(DVFS_UTIL_STAT *psUtil)
{
	static int level_down_acc = 0;
	int load = psUtil->active;
	int level = gpudvfs.cur_level;

#if defined(CONFIG_THERMAL)
	if (load >= SCALING_UP_THRESHOLD &&
		level > gpudvfs.min_level_lock &&
		level > gpudvfs.thermal_level_lock
	   ) {
	   	if (load >= SCALING_UP_THRESHOLD_CRITICAL)
	   		level = (gpudvfs.min_level_lock > gpudvfs.thermal_level_lock) ?
	   				gpudvfs.min_level_lock : gpudvfs.thermal_level_lock;
		else
			level--;
	} else if (load <= SCALING_DOWN_THRESHOLD &&
			   level < gpudvfs.max_level_lock) {
		level++;
	}
#else
	if (load >= SCALING_UP_THRESHOLD &&
		level > gpudvfs.min_level_lock
	   ) {
	   	if (load >= SCALING_UP_THRESHOLD_CRITICAL)
	   		level = gpudvfs.min_level_lock;
		else
			level--;
	} else if (load <= SCALING_DOWN_THRESHOLD &&
			   level < gpudvfs.max_level_lock) {
		level++;
	}
#endif

	if (level > gpudvfs.cur_level) {
		level_down_acc++;
		if (load > SCALING_DOWN_THRESHOLD_NODELAY &&
			level_down_acc < SCALING_DOWN_FACTOR) {
				level = gpudvfs.cur_level;
		}
	} else {
		level_down_acc = 0;
	}

	return level;
}

static void dvfs_proc_func(void * pvData)
{
	DVFS_UTIL_STAT *psUtil = gpudvfs.psUtil;

	down_write(&gpudvfs.dvfs_rwsem);

	SysDvfsUpdateUtilStat(&gpudvfs.dvfs, psUtil);
	if (psUtil->valid) {
		int level = gpudvfs.governor->get_target_level(psUtil);
		if (level != gpudvfs.cur_level) {
			GPU_DEBUG("dvfs: load %3u, freq -> %9lu",
					  psUtil->active, gpudvfs.freq_table[level]);

			if (set_level(level))
				GPU_ERR("%s: fail to set dvfs level(%d)", __func__, level);
		}
	}

	up_write(&gpudvfs.dvfs_rwsem);
}

bool gpu_dvfs_is_enabled(void)
{
	bool enable;

	mutex_lock(&gpudvfs.dvfs_enable_mutex);

	enable = SysDvfsIsEnabled(&gpudvfs.dvfs);

	mutex_unlock(&gpudvfs.dvfs_enable_mutex);

	return enable;
}

int gpu_dvfs_enable(void)
{
	int ret;

	mutex_lock(&gpudvfs.dvfs_enable_mutex);

	ret = SysDvfsEnable(&gpudvfs.dvfs);

	mutex_unlock(&gpudvfs.dvfs_enable_mutex);

	return ret;
}

void gpu_dvfs_disable(void)
{
	mutex_lock(&gpudvfs.dvfs_enable_mutex);

	SysDvfsDisable(&gpudvfs.dvfs);

	mutex_unlock(&gpudvfs.dvfs_enable_mutex);
}

int gpu_dvfs_set_governor(enum GPUFREQ_GOVERNOR eGovernor)
{
	int err;

	if (eGovernor < 0 || eGovernor >= NUM_GPUFREQ_GOVERNOR) {
		GPU_ERR("invalid governor %d", eGovernor);
		return -EINVAL;
	}

	mutex_lock(&gpudvfs.sysfs_mutex);

	down_write(&gpudvfs.dvfs_rwsem);

	gpudvfs.governor = &dvfs_governor[eGovernor];

	up_write(&gpudvfs.dvfs_rwsem);

	/* Reset clock level range. */
	err = gpu_dvfs_set_level_range(0, gpudvfs.num_level - 1);

	mutex_unlock(&gpudvfs.sysfs_mutex);

	return err;
}

static ssize_t sys_show_freq(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", gpudvfs.freq_table[gpudvfs.cur_level]);
}

static ssize_t sys_set_freq(struct kobject *kobj, struct kobj_attribute *attr,
						const char *buf, size_t count)
{
	unsigned long freq;
	int level;
	if (kstrtoul(buf, 0, &freq))
		goto exit_out;

	level = gpu_dvfs_get_level(freq);

	mutex_lock(&gpudvfs.sysfs_mutex);

	if (gpudvfs.governor->governor == GPUFREQ_GOVERNOR_USER)
		gpu_dvfs_set_level(level);

	mutex_unlock(&gpudvfs.sysfs_mutex);

exit_out:
	return count;
}

static ssize_t sys_show_dvfs(struct kobject *kobj, struct kobj_attribute *attr,
						 char *buf)
{
	int ret;

	ret = sprintf(buf, "%d\n", gpu_dvfs_is_enabled());

	return ret;
}

static ssize_t sys_set_dvfs(struct kobject *kobj, struct kobj_attribute *attr,
						const char *buf, size_t count)
{
	int enabled;
	if (kstrtoint(buf, 0, &enabled))
		goto exit_out;

	mutex_lock(&gpudvfs.sysfs_mutex);

	if (gpudvfs.governor->governor == GPUFREQ_GOVERNOR_USER) {
		if (enabled)
			gpu_dvfs_enable();
		else
			gpu_dvfs_disable();
	}

	mutex_unlock(&gpudvfs.sysfs_mutex);

exit_out:
	return count;
}

static ssize_t sys_show_dvfs_table(struct kobject *kobj, struct kobj_attribute *attr,
							   char *buf)
{
	int i, size = 0;

	for (i = 0; i < gpudvfs.num_level; i++)
		size += snprintf(buf + size, PAGE_SIZE - size - 2, "%lu ", 
						 gpudvfs.freq_table[i]);

	/* Truncate the trailing space */
	if (size)
		size--;
	size += sprintf(buf + size, "\n");

	return size;
}

static ssize_t sys_show_utilization(struct kobject *kobj, struct kobj_attribute *attr,
								char *buf)
{
	return sprintf(buf, "%d\n", gpu_dvfs_get_utilisation());
}

static ssize_t sys_show_max_lock(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int min_lock, max_lock;
	gpu_dvfs_get_level_range(&min_lock, &max_lock);

	return sprintf(buf, "%d\n", max_lock);
}

static ssize_t sys_set_max_lock(struct kobject *kobj, struct kobj_attribute *attr,
						const char *buf, size_t count)
{
	int min_lock, max_lock, temp;
	if (kstrtoint(buf, 0, &max_lock))
		goto exit_out;

	mutex_lock(&gpudvfs.sysfs_mutex);

	if (gpudvfs.governor->governor == GPUFREQ_GOVERNOR_USER) {
		gpu_dvfs_get_level_range(&min_lock, &temp);
		gpu_dvfs_set_level_range(min_lock, max_lock);
	}

	mutex_unlock(&gpudvfs.sysfs_mutex);

exit_out:
	return count;
}

static ssize_t sys_show_min_lock(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int min_lock, max_lock;
	gpu_dvfs_get_level_range(&min_lock, &max_lock);

	return sprintf(buf, "%d\n", min_lock);
}

static ssize_t sys_set_min_lock(struct kobject *kobj, struct kobj_attribute *attr,
						const char *buf, size_t count)
{
	int min_lock, max_lock, temp;
	if (kstrtoint(buf, 0, &min_lock))
		goto exit_out;

	mutex_lock(&gpudvfs.sysfs_mutex);

	if (gpudvfs.governor->governor == GPUFREQ_GOVERNOR_USER) {
		gpu_dvfs_get_level_range(&temp, &max_lock);
		gpu_dvfs_set_level_range(min_lock, max_lock);
	}

	mutex_unlock(&gpudvfs.sysfs_mutex);

exit_out:
	return count;
}


static GPU_KOBJ_ATTR(freq, S_IRUGO|S_IWUSR, sys_show_freq, sys_set_freq);
static GPU_KOBJ_ATTR(dvfs, S_IRUGO|S_IWUSR, sys_show_dvfs, sys_set_dvfs);
static GPU_KOBJ_ATTR(dvfs_table, S_IRUGO, sys_show_dvfs_table, NULL);
static GPU_KOBJ_ATTR(utilization, S_IRUGO, sys_show_utilization, NULL);
static GPU_KOBJ_ATTR(max_lock, S_IRUGO|S_IWUSR, sys_show_max_lock, sys_set_max_lock);
static GPU_KOBJ_ATTR(min_lock, S_IRUGO|S_IWUSR, sys_show_min_lock, sys_set_min_lock);

static int gpufreq_create_sysfs(struct kobject *kobj)
{
	int err = -1;
	gpudvfs.sysfs_kobj = kobject_create_and_add("dvfs", kobj);
	if (gpudvfs.sysfs_kobj) {
		err  = sysfs_create_file(gpudvfs.sysfs_kobj, &gpu_attr_freq.attr);
		err |= sysfs_create_file(gpudvfs.sysfs_kobj, &gpu_attr_dvfs.attr);
		err |= sysfs_create_file(gpudvfs.sysfs_kobj, &gpu_attr_dvfs_table.attr);
		err |= sysfs_create_file(gpudvfs.sysfs_kobj, &gpu_attr_utilization.attr);
		err |= sysfs_create_file(gpudvfs.sysfs_kobj, &gpu_attr_max_lock.attr);
		err |= sysfs_create_file(gpudvfs.sysfs_kobj, &gpu_attr_min_lock.attr);	
	}

	return err;
}

static void gpufreq_remove_sysfs(void)
{
	if (gpudvfs.sysfs_kobj) {
		sysfs_remove_file(gpudvfs.sysfs_kobj, &gpu_attr_freq.attr);
		sysfs_remove_file(gpudvfs.sysfs_kobj, &gpu_attr_dvfs.attr);
		sysfs_remove_file(gpudvfs.sysfs_kobj, &gpu_attr_dvfs_table.attr);
		sysfs_remove_file(gpudvfs.sysfs_kobj, &gpu_attr_utilization.attr);
		sysfs_remove_file(gpudvfs.sysfs_kobj, &gpu_attr_max_lock.attr);
		sysfs_remove_file(gpudvfs.sysfs_kobj, &gpu_attr_min_lock.attr);

		kobject_del(gpudvfs.sysfs_kobj);
		gpudvfs.sysfs_kobj = NULL;
	}
}

int register_gpufreq_dvfs(struct gpufreq_driver *driver)
{
	int err;

	gpudvfs.driver = driver;

	/* Initialize the power and clock. */
	err = gpudvfs.driver->init();
	if (err) {
		GPU_ERR("gpu init failed.");
		return err;
	}

	gpudvfs.num_level = gpudvfs.driver->get_freqtable(&gpudvfs.freq_table);
	gpudvfs.min_level_lock = 0;
	gpudvfs.max_level_lock = gpudvfs.num_level - 1;
#if defined(CONFIG_THERMAL)
	gpudvfs.thermal_level_lock = 0;
#endif

	init_rwsem(&gpudvfs.dvfs_rwsem);
	mutex_init(&gpudvfs.sysfs_mutex);
	mutex_init(&gpudvfs.dvfs_enable_mutex);

	/* Set default level. */
	err = set_level(gpudvfs.num_level - 1);
	if (err) {
		GPU_ERR("set_level failed.");
		return err;		
	}

	err = gpu_dvfs_set_governor(GPUFREQ_GOVERNOR_POWERSAVE);
	if (err) {
		GPU_ERR("set_mode failed.");
		return err;		
	}

	err = SysDvfsInit(&gpudvfs.dvfs, dvfs_proc_func, NULL, DVFS_SAMPLE_WINDOW_MS);
	if (err) {
		GPU_ERR("dvfs init failed.");
		return err;		
	}

	if (gpufreq_create_sysfs(&GetLDMDevice()->kobj))
		GPU_ERR("fail to create sysfs files");

	return err;
}

int unregister_gpufreq_dvfs(void)
{
	SysDvfsDeinit(&gpudvfs.dvfs);

	gpudvfs.driver->uninit();

	gpufreq_remove_sysfs();

	return 0;
}
