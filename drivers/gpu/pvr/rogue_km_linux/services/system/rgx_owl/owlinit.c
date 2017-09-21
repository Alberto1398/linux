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

#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/rwsem.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/opp.h>
#include <linux/slab.h>

#if defined(CONFIG_PM_RUNTIME)
#include <linux/pm_runtime.h>
#endif

#if defined(CONFIG_OWL_THERMAL)
#include <linux/cpu_cooling.h>
#endif

#include "syslocal.h"
#include "gpudvfs.h"
#include "owlinit.h"

#define CONFIG_DTS_FREQ_TABLE

/*
 * Defining thie macro may lead to dead lock, since SysDvfsUpdateUtilStat
 * require "PVRSRVPowerLock" to compute utilization in DDK_1.4.
 */
#undef OWL_DISABLE_DVFS_WHEN_POWEROFF

#define VOLTAGE_TOL (20000)
#define DEFAULT_CLOCK_SPEED (352000000)

struct owl_regulator {
	struct regulator *regulator;
	char *name;
};

struct owl_clk {
	struct clk *clk;
	char *name;
};

static struct device *gpu_device = NULL;

static struct owl_regulator gpu_regulator = {
	.name = "gpu",
};

static struct owl_clk gpu_clk      = { .name = "gpu" };
static struct owl_clk gpu_core_clk = { .name = "gpu_core" };
static struct owl_clk gpu_mem_clk  = { .name = "gpu_mem" };
static struct owl_clk gpu_sys_clk  = { .name = "gpu_sys" };

/* Prefer "display_pll" */
static struct owl_clk gpu_parent_clk[] = {
	{ .name = "display_pll" },
	{ .name = "dev_clk" },
};

static unsigned long *gpu_freqtable = NULL;
static int nent_of_freqtable = 0;

static atomic_t clock_en = { 0 };
static atomic_t power_en = { 0 };

static int owl_clock_is_enabled(void)
{
	return atomic_read(&clock_en);
}

static int owl_power_is_enabled(void)
{
	return atomic_read(&power_en);
}

static int owl_set_clockenabled(bool enabled)
{
	int err = 0;

	if (enabled == owl_clock_is_enabled())
		return 0;

	if (enabled) {
		err = (clk_prepare_enable(gpu_clk.clk)      ||
	   		   clk_prepare_enable(gpu_core_clk.clk) ||
	   		   clk_prepare_enable(gpu_mem_clk.clk)  ||
	   		   clk_prepare_enable(gpu_sys_clk.clk));
	} else {
		clk_disable_unprepare(gpu_clk.clk);
		clk_disable_unprepare(gpu_core_clk.clk);
		clk_disable_unprepare(gpu_mem_clk.clk);
		clk_disable_unprepare(gpu_sys_clk.clk);
	}

	if (!err)
		atomic_set(&clock_en, enabled);

	return err;
}

static int owl_set_powerenabled(bool enabled)
{
	int err = 0;

	if (enabled == owl_power_is_enabled())
		return 0;

	if (!gpu_regulator.regulator)
		goto exit_out;

	if (enabled)
		err = regulator_enable(gpu_regulator.regulator);
	else
		err = regulator_disable(gpu_regulator.regulator);

exit_out:
	if (!err)
		atomic_set(&power_en, enabled);

	return err;
}

static int owl_get_freqtable(unsigned long **freqtable)
{
	*freqtable = gpu_freqtable;
	return nent_of_freqtable;
}

static unsigned long owl_get_clockspeed(void)
{
	return clk_get_rate(gpu_core_clk.clk);
}

static int setClockRate(unsigned long rate)
{
	int err, i;
	int best_parent = 0;
	unsigned long best_rate = 0;

	for (i = 0; i < ARRAY_SIZE(gpu_parent_clk); i++) {
		long round_rate;
		err = clk_set_parent(gpu_core_clk.clk, gpu_parent_clk[i].clk);
		if (err)
			return err;

		/* round_rate <= rate*/
		round_rate = clk_round_rate(gpu_core_clk.clk, rate);
		if (round_rate < 0)
			return (int)round_rate;

		if (round_rate > best_rate) {
			best_rate = round_rate;
			best_parent = i;

			if (round_rate == rate)
				break;
		}
	}

	err = (clk_set_parent(gpu_core_clk.clk, gpu_parent_clk[best_parent].clk) ||
		   clk_set_parent(gpu_mem_clk.clk, gpu_parent_clk[best_parent].clk)  ||
		   clk_set_parent(gpu_sys_clk.clk, gpu_parent_clk[best_parent].clk));

	if (!err)
		err = (clk_set_rate(gpu_core_clk.clk, rate) ||
			   clk_set_rate(gpu_mem_clk.clk, rate)  || 
			   clk_set_rate(gpu_sys_clk.clk, rate));

	return err;
}

/* Device should be idle when changing frequency. */
static int owl_set_clockspeed(unsigned long rate)
{
	struct opp *opp;
	int oldvol, newvol = 0;
	int err;

	rcu_read_lock();

	opp = opp_find_freq_ceil(gpu_device, &rate);
	if (IS_ERR(opp)) {
		opp = opp_find_freq_floor(gpu_device, &rate);
		if (IS_ERR(opp))
			goto rcu_unlock;
	}

	newvol = (int)opp_get_voltage(opp);

rcu_unlock:
	rcu_read_unlock();

	/* if regulator is NULL, bypass regulator volatage get/set. */
	oldvol = gpu_regulator.regulator ?
			regulator_get_voltage(gpu_regulator.regulator) : newvol;

	if (newvol == oldvol) {
		err  = setClockRate(rate);
	} else if (newvol < oldvol) {
		err  = setClockRate(rate);
		err |= regulator_set_voltage(gpu_regulator.regulator, newvol, newvol + VOLTAGE_TOL);
	} else {
		err  = regulator_set_voltage(gpu_regulator.regulator, newvol, newvol + VOLTAGE_TOL);
		err |= setClockRate(rate);
	}

	return err;
}

static int owl_init_gpufreq_table(void)
{
	unsigned long freq = 0;
	int err, cnt = 0;

#if defined(CONFIG_DTS_FREQ_TABLE)
	err = of_init_opp_table(gpu_device);
#else
	err  = opp_add(gpu_device, 352000000,  875000);
	err |= opp_add(gpu_device, 528000000, 1000000);
#endif

	if (err)
		return err;

	rcu_read_lock();

	nent_of_freqtable = opp_get_opp_count(gpu_device);
	gpu_freqtable = kmalloc(
			sizeof(unsigned long) * nent_of_freqtable, GFP_KERNEL);
	if (!gpu_freqtable) {
		GPU_ERR("%s: kmaloc failed", __func__);
		goto rcu_unlock;
	}

	cnt = nent_of_freqtable;
	do {
		struct opp *opp = opp_find_freq_ceil(gpu_device, &freq);
		if (IS_ERR(opp))
			break;

		/* frequency order: from high to low. */
		gpu_freqtable[--cnt] = opp_get_freq(opp);

		freq++;
	} while (1);

rcu_unlock:
	rcu_read_unlock();

	return err;
}

static void owl_free_gpufreq_table(void)
{
	kfree(gpu_freqtable);
	nent_of_freqtable = 0;
}

static int owl_gpu_freq_init(void)
{
	int i, j;

	gpu_regulator.regulator = devm_regulator_get(gpu_device, gpu_regulator.name);
	if (IS_ERR(gpu_regulator.regulator)) {
		GPU_ERR("devm_regulator_get failed");
		return -ENODEV;
	}

	/* Check whether regulator is normal or not. */
	if (regulator_get_voltage(gpu_regulator.regulator) < 0) {
		devm_regulator_put(gpu_regulator.regulator);
		gpu_regulator.regulator = NULL;
		GPU_ERR("regulator_get_voltage failed");
	}

	gpu_clk.clk      = devm_clk_get(gpu_device, gpu_clk.name);
	gpu_core_clk.clk = devm_clk_get(gpu_device, gpu_core_clk.name);
	gpu_mem_clk.clk  = devm_clk_get(gpu_device, gpu_mem_clk.name);
	gpu_sys_clk.clk  = devm_clk_get(gpu_device, gpu_sys_clk.name);
	if (IS_ERR(gpu_clk.clk) ||
		IS_ERR(gpu_core_clk.clk) ||
		IS_ERR(gpu_mem_clk.clk) ||
		IS_ERR(gpu_sys_clk.clk)) {
		GPU_ERR("devm_clk_get failed");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(gpu_parent_clk); i++) {
		gpu_parent_clk[i].clk = clk_get(NULL, gpu_parent_clk[i].name);
		if (IS_ERR_OR_NULL(gpu_parent_clk[i].clk)) {
			GPU_ERR("Failed to get %s", gpu_parent_clk[i].name);

			for (j = 0; j < i; j++)
				clk_put(gpu_parent_clk[i].clk);

			return -ENODEV;
		}
	}

	return owl_init_gpufreq_table();
}

static void owl_gpu_freq_deinit(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gpu_parent_clk); i++)
		clk_put(gpu_parent_clk[i].clk);

	owl_free_gpufreq_table();
}

static struct gpufreq_driver gpufreq_driver = {
	.init = owl_gpu_freq_init,
	.uninit = owl_gpu_freq_deinit,
	.get_freqtable = owl_get_freqtable,
	.get_clockspeed = owl_get_clockspeed,
	.set_clockspeed = owl_set_clockspeed,
};

static ssize_t device_show_policy(struct device *dev, struct device_attribute *attr,
			char *buf);
static ssize_t device_store_policy(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);

static struct {
	int policy;
	struct device_attribute dev_attr_policy;
	struct rw_semaphore rwsem;
} gpupolicy = {
	.policy = -1,
	.dev_attr_policy = __ATTR(policy, S_IRUGO|S_IWUSR, device_show_policy, device_store_policy),
};

static int get_policy(void)
{
 	return gpupolicy.policy;
}

static int set_policy(int policy)
{
	enum GPUFREQ_GOVERNOR governor = GPUFREQ_GOVERNOR_UNKNOWN;
	int level = -1;
	int err;

	switch (policy) {
	case GPUFREQ_POLICY_POWERSAVE:
		governor = GPUFREQ_GOVERNOR_POWERSAVE;
		level = gpu_dvfs_get_maximum_level();
		break;
	case GPUFREQ_POLICY_PERFORMANCE:
		governor = GPUFREQ_GOVERNOR_PERFORMANCE;
		level = 0;
		break;
	case GPUFREQ_POLICY_NORMAL:
#if defined(SUPPORT_GPU_DVFS)
		governor = GPUFREQ_GOVERNOR_CONSERVATIVE;
#else
		governor = GPUFREQ_GOVERNOR_USER;
		level = gpu_dvfs_get_level(DEFAULT_CLOCK_SPEED);
#endif
		break;
	case GPUFREQ_POLICY_USERSPACE:
		governor = GPUFREQ_GOVERNOR_USER;
		break;
	default:
		GPU_ERR("%s: unknown gpu policy %d", __func__, policy);
		err = -EINVAL;
		goto err_out;
	}

	err = gpu_dvfs_set_governor(governor);
	if (err)
		goto err_out;

#if defined(OWL_DISABLE_DVFS_WHEN_POWEROFF)
	if (!owl_power_is_enabled()) 
	{
		gpu_dvfs_disable();
	} 
	else
#endif
	{
		gpu_dvfs_enable();
	}

	if (level >= 0)
		err = gpu_dvfs_set_level(level);

	if (!err)
		gpupolicy.policy = policy;

err_out:
	return err;
}

static ssize_t device_show_policy(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int policy;
	char *policy_str[GPUFREQ_NUMBER_OF_POLICY] = {
		"normal", "powersave", "performance", "userspace",
	};

	down_read(&gpupolicy.rwsem);

	policy = get_policy();

	up_read(&gpupolicy.rwsem);

	return sprintf(buf, "%s\n", policy_str[policy]);
}

static ssize_t device_store_policy(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int policy;

	if (sysfs_streq("3007", buf) || sysfs_streq("normal", buf))
		policy = GPUFREQ_POLICY_NORMAL;
	else if (sysfs_streq("2006", buf) || sysfs_streq("powersave", buf))
		policy = GPUFREQ_POLICY_POWERSAVE;
	else if (sysfs_streq("4008", buf) || sysfs_streq("performance", buf))
		policy = GPUFREQ_POLICY_PERFORMANCE;
	else if (sysfs_streq("userspace", buf))
		policy = GPUFREQ_POLICY_USERSPACE;
	else {
		GPU_ERR("invalid gpu policy %s", buf);
		goto exit_out;
	}

	down_write(&gpupolicy.rwsem);

	if (policy != get_policy()) {
		if (set_policy(policy))
			GPU_ERR("set poliy failed");
	}

	up_write(&gpupolicy.rwsem);

exit_out:
	return count;	
}

#if defined(CONFIG_OWL_THERMAL)
static int thermal_notifier_callback(struct notifier_block *nb,
									 unsigned long action, void *data)
{
	int ret = NOTIFY_OK;
	switch (action) {
	case CPUFREQ_COOLING_START:
		if (gpu_dvfs_set_thermal_stat(GPUFREQ_THERMAL_HOT) < 0)
			GPU_ERR("thermal_notifier_callback failed");
		break;
	case CPUFREQ_COOLING_STOP:
		gpu_dvfs_set_thermal_stat(GPUFREQ_THERMAL_NORMAL);

		/* Recover the frequency for policy setting */
		down_write(&gpupolicy.rwsem);
		if (gpupolicy.policy != GPUFREQ_POLICY_USERSPACE
			&&
#if defined(SUPPORT_GPU_DVFS)
			gpupolicy.policy != GPUFREQ_POLICY_NORMAL
#else
			1
#endif
		) {
			set_policy(get_policy());
		}
		up_write(&gpupolicy.rwsem);
		break;
	default:
		ret = NOTIFY_DONE;
		break;
	}

	return ret;
}

static struct notifier_block owl_thermal_notifier = {
	.notifier_call = thermal_notifier_callback,
};
#endif /* defined(CONFIG_OWL_THERMAL) */


/*
 * Should set regulator voltage before enabling regulator for the first time,
 * since the default voltage after soc boot up is too high.
 */
int owl_gpu_init(struct device *dev)
{
	int err;

	gpu_device = dev;

	/* also set default clock speed and related voltage (regulator not enabled yet). */
	err = register_gpufreq_dvfs(&gpufreq_driver);
	if (err) {
		GPU_ERR("%s: register_gpufreq_dvfs failed", __func__);
		goto err_out;
	}

	init_rwsem(&gpupolicy.rwsem);

	err = set_policy(GPUFREQ_POLICY_NORMAL);
	if (err) {
		GPU_ERR("%s: set_policy failed", __func__);
		goto err_out;
	}

	if (device_create_file(gpu_device, &gpupolicy.dev_attr_policy))
		GPU_ERR("%s: device_create_file failed", __func__);

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_enable(gpu_device);
#endif

#if defined(CONFIG_OWL_THERMAL)
	cputherm_register_notifier(&owl_thermal_notifier, CPUFREQ_COOLING_START);
#endif

err_out:
	return err;
}

void owl_gpu_deinit(void)
{
	/* Make sure the clock and power are off. */
	owl_gpu_set_clock_enable(false);
	owl_gpu_set_power_enable(false);

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_disable(gpu_device);
#endif

#if defined(CONFIG_OWL_THERMAL)
	cputherm_unregister_notifier(&owl_thermal_notifier, CPUFREQ_COOLING_START);
#endif

	device_remove_file(gpu_device, &gpupolicy.dev_attr_policy);

	unregister_gpufreq_dvfs();
}

/*
 * Assume that owl_gpu_set_power_enable is called after being idle for a long time.
 */
int owl_gpu_set_power_enable(bool enabled)
{
	int res, err = 0;

	down_write(&gpupolicy.rwsem);

	if (owl_power_is_enabled() != enabled) {
		if (enabled) {
			err = owl_set_powerenabled(true);
			if (err)
				goto err_out;
#if defined(CONFIG_PM_RUNTIME)
			res = pm_runtime_get_sync(gpu_device);
			if (res < 0) {
				GPU_ERR("%s: pm_runtime_get_sync failed (%d)", __func__, -res);
				err = -ENODEV;
				goto err_out;
			}
#endif
		} else {
#if defined(CONFIG_PM_RUNTIME)
			res = pm_runtime_put_sync(gpu_device);
			if (res < 0)
				GPU_ERR("%s: pm_runtime_put_sync failed (%d)",  __func__, -res);
#endif
			owl_set_powerenabled(false);
		}

#if defined(SUPPORT_GPU_DVFS) && defined(OWL_DISABLE_DVFS_WHEN_POWEROFF)
		if (gpupolicy.policy != GPUFREQ_POLICY_USERSPACE) {
			if (enabled)
				gpu_dvfs_enable();
			else
				gpu_dvfs_disable();
		}
#endif
	}

err_out:
	up_write(&gpupolicy.rwsem);

	return err;
}

int owl_gpu_set_clock_enable(bool enabled)
{
	int err;

	down_write(&gpupolicy.rwsem);

	err = owl_set_clockenabled(enabled);

	up_write(&gpupolicy.rwsem);

	return err;
}

unsigned long owl_gpu_get_clock_speed(void)
{
	return owl_get_clockspeed();
}
