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

#if !defined(__SYS_LOCAL_H__)
#define __SYS_LOCAL_H__

#include <linux/platform_device.h>
#include <linux/kernel.h>

#include "img_defs.h"
#include "pvrsrv_error.h"

#define OWL_RGX_POWEROFF_TIMEOUT_MS (1000)

extern struct platform_device *gpsPVRLDMDev;

#define GPU_ERR(format, ...)   dev_err(&gpsPVRLDMDev->dev, "(error): " format "\n", ## __VA_ARGS__)
#define GPU_WARN(format, ...)  dev_warn(&gpsPVRLDMDev->dev, "(warn): " format "\n", ## __VA_ARGS__)
#define GPU_DEBUG(format, ...) dev_dbg(&gpsPVRLDMDev->dev, "(debug): " format "\n", ## __VA_ARGS__)

typedef void (*PFN_WORK_FUNC)(void*);
typedef void (*PFN_HRTIMER_FUNC)(void*);
typedef PFN_HRTIMER_FUNC PFN_DVFS_FUNC;

typedef struct _DVFS_UTIL_STAT_ {
	bool valid;

	/* core utilization
	 * TA, 3D, 2D, Compute
	 */
	u32	active;
	u32	blocked;
	u32	idle;

	/* memory utilization
	 * GPU SLC memory read/write stall ratio
	 * (stall ratio = stall cycles while active / total active cycles)
	 */
	u32 memstall;
} DVFS_UTIL_STAT;

typedef struct _DVFS_TAG_ {
	bool bEnable;

	PFN_DVFS_FUNC pfnDvfsFunc;;
	void  *pvData;

	void *hDvfsTimer;
	u32 window_ms;

	int (*preClockSpeedChange)(bool bIdle);
	int (*postClockSpeedChange)(bool bIdle);
} DVFS_TAG;

typedef struct _SYS_DATA_
{
	bool bClocksEnabled;
	bool bSysClocksOneTimeInit;
	void *psRGXDeviceNode;
	u32 ui32RGXDeviceID;

	void *hPowerWork;
	u32 ui32MSPowerTimeout;
} SYS_DATA;

static inline struct device *GetLDMDevice(void)
{
	return &gpsPVRLDMDev->dev;
}

SYS_DATA *GetSysData(void);
PVRSRV_ERROR SysUpdateConfigData(IMG_HANDLE hSysData);

void *SysCreateWork(const char *pszWorkName, PFN_WORK_FUNC pfnTimerFunc, 
				  	void *pvData, u32 ui32MsDelay);
void SysDestroyWork(void *hWork);
int  SysStartWork(void *hWork);
int  SysStopWork(void *hWork);

void *SysCreateHRTimer(const char *pszTimerName, PFN_HRTIMER_FUNC pfnTimerFunc, 
					   void *pvData, u32 ui32MsTimeout, bool bRestart);
void SysDestroyHRTimer(void *hTimer);
int SysEnableHRTimer(void *hTimer);
int SysDisableHRTimer(void *hTimer);

int SysDvfsInit(DVFS_TAG *psDvfs, PFN_DVFS_FUNC DvfsProcCallback, 
				void *pvData, u32 ui32WindowMS);
void SysDvfsDeinit(DVFS_TAG *psDvfs);
bool SysDvfsIsEnabled(DVFS_TAG *psDvfs);
int SysDvfsEnable(DVFS_TAG *psDvfs);
int SysDvfsDisable(DVFS_TAG *psDvfs);
int SysDvfsUpdateUtilStat(DVFS_TAG *psDvfs, DVFS_UTIL_STAT *psUtilStat);

#endif /* __SYS_LOCAL_H__ */
