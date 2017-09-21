/*************************************************************************/ /*!
@Title          System Description Header
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
@Description    This header provides system-specific declarations and macros
*/ /**************************************************************************/

#if !defined(__GPU_DVFS_H__)
#define __GPU_DVFS_H__

#define RELATION_L 0 /* highest frequency below or at target */
#define RELATION_H 1 /* lowest frequency at or above target */

enum GPUFREQ_THERMAL_STAT
{
	GPUFREQ_THERMAL_NORMAL = 0,
	GPUFREQ_THERMAL_HOT,
	GPUFREQ_THERMAL_CRITICAL,

	NUM_GPUFREQ_THERMAL_STAT,
};

enum GPUFREQ_GOVERNOR
{
	GPUFREQ_GOVERNOR_UNKNOWN = -1,
	GPUFREQ_GOVERNOR_POWERSAVE = 0,
	GPUFREQ_GOVERNOR_PERFORMANCE = 1,
	GPUFREQ_GOVERNOR_CONSERVATIVE = 2,
	GPUFREQ_GOVERNOR_USER = 3,

	NUM_GPUFREQ_GOVERNOR,
};

struct gpufreq_governor
{
	enum GPUFREQ_GOVERNOR governor;
	int (*get_target_level)(DVFS_UTIL_STAT *psUtil);
};

struct gpufreq_driver {
	int (*init)(void);
	void (*uninit)(void);

	int (*get_freqtable)(unsigned long **freqtable);
	unsigned long (*get_clockspeed)(void);
	int (*set_clockspeed)(unsigned long rate);
};

int register_gpufreq_dvfs(struct gpufreq_driver *driver);
int unregister_gpufreq_dvfs(void);

int gpu_dvfs_set_governor(enum GPUFREQ_GOVERNOR eGovernor);

bool gpu_dvfs_is_enabled(void);
int gpu_dvfs_enable(void);
void gpu_dvfs_disable(void);

int gpu_dvfs_get_current_level(void);
int gpu_dvfs_get_maximum_level(void);
int gpu_dvfs_get_level(unsigned long freq);

int gpu_dvfs_set_level(int level);
int gpu_dvfs_get_level_range(int *minlevel, int *maxlevel);
int gpu_dvfs_set_level_range(int minlevel, int maxlevel);

#if defined(CONFIG_THERMAL)
int gpu_dvfs_set_thermal_stat(enum GPUFREQ_THERMAL_STAT eStat);
#endif

#endif /* __GPU_DVFS_H__ */
