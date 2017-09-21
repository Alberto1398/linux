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
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>

#include "pvrsrv_device.h"
#include "rgxdevice.h"
#include "syscommon.h"

#include "syslocal.h"

/* Conversion helpers for setting up high resolution timers */
#define HR_TIMER_DELAY_MSEC(x) (ns_to_ktime(((u64)(x))*1000000UL))
#define HR_TIMER_DELAY_NSEC(x) (ns_to_ktime(x))
#define RGXFWIF_GPU_STATS_WINDOW_SIZE_US			500000			/*!< Time window considered for active/idle/blocked statistics */


static IMG_HANDLE ghGpuUtilStats = NULL;

/* Timer callback structure used by SysCreateHRTimer */
typedef struct HRTIMER_CALLBACK_DATA_TAG
{
	bool			 bInUse;
	PFN_HRTIMER_FUNC pfnTimerFunc;
	void			 *pvData;

	struct hrtimer sTimer;
	ktime_t        ktime;
	u32			   ui32Delay;

	bool		   bActive;
	bool           bRestart;
	struct workqueue_struct *psTimerWorkQueue;
	struct work_struct	sWork;
} HRTIMER_CALLBACK_DATA;

/* Delaywork callback structure used by SysCreateWork */
typedef struct DELAYWORK_CALLBACK_DATA_TAG
{
	PFN_HRTIMER_FUNC pfnWorkFunc;
	void			 *pvData;

	bool		   bActive;
	struct delayed_work sWork;
	u32			   ui32Delay;
	struct workqueue_struct *psWorkQueue;
} DELAYWORK_CALLBACK_DATA;

static void DelayWorkQueueCallBack(struct work_struct *psWork)
{
	DELAYWORK_CALLBACK_DATA *psCBData =
			container_of((struct delayed_work*)psWork, DELAYWORK_CALLBACK_DATA, sWork);

	if (!psCBData->bActive)
        return;

    psCBData->pfnWorkFunc(psCBData->pvData);
    psCBData->bActive = false;
}

void *SysCreateWork(const char *pszWorkName, PFN_WORK_FUNC pfnWorkFunc, 
				  	void *pvData, u32 ui32MsDelay)
{
	DELAYWORK_CALLBACK_DATA *psCBData;

	if (!pfnWorkFunc) {
		GPU_ERR("%s: passed invalid callback", __func__);
		return NULL;
	}

	psCBData = kzalloc(sizeof(*psCBData), GFP_KERNEL);
	if (!psCBData) {
		GPU_ERR("%s: kmalloc fail", __func__);
		return NULL;
	}

	psCBData->bActive = false;
	psCBData->pfnWorkFunc = pfnWorkFunc;
	psCBData->pvData = pvData;
	psCBData->ui32Delay = (HZ * ui32MsDelay) / 1000;

	if (pszWorkName) {
		psCBData->psWorkQueue = create_singlethread_workqueue(pszWorkName);
		if (!psCBData->psWorkQueue) {
			GPU_ERR("%s: cannot create timer workqueue \"%s\"", __func__, pszWorkName);
			return NULL;
		}
	}

	INIT_DELAYED_WORK(&psCBData->sWork, DelayWorkQueueCallBack); 

	return (void *)&psCBData->sWork;
}

void SysDestroyWork(void *hWork)
{
	DELAYWORK_CALLBACK_DATA *psCBData =
			container_of(hWork, DELAYWORK_CALLBACK_DATA, sWork);

	if (psCBData->bActive) {
		GPU_WARN("%s: hWork %p still active", __func__, hWork);
		SysStopWork(hWork);
	}

	if (psCBData->psWorkQueue) {
		destroy_workqueue(psCBData->psWorkQueue);
		psCBData->psWorkQueue = NULL;
	}

	kfree(psCBData);
}

int SysStartWork(void *hWork)
{
	int res;
	DELAYWORK_CALLBACK_DATA *psCBData =
			container_of(hWork, DELAYWORK_CALLBACK_DATA, sWork);

	if (psCBData->bActive)
		return 0;

	if (psCBData->psWorkQueue)
		res = queue_delayed_work(psCBData->psWorkQueue, &psCBData->sWork, psCBData->ui32Delay);
	else
		res = schedule_delayed_work(&psCBData->sWork, psCBData->ui32Delay);

	if (res == false)
		GPU_DEBUG("SysStartWork: work already queued");

	psCBData->bActive = true;

	return 0;
}

int SysStopWork(void *hWork)
{
	DELAYWORK_CALLBACK_DATA *psCBData =
			container_of(hWork, DELAYWORK_CALLBACK_DATA, sWork);

	if (psCBData->bActive)
		cancel_delayed_work_sync(&psCBData->sWork);

	psCBData->bActive = false;

	return 0;
}


static enum hrtimer_restart HRTimerCallbackWrapper(struct hrtimer *psTimer)
{
	int res;
    HRTIMER_CALLBACK_DATA *psTimerCBData = container_of(psTimer, HRTIMER_CALLBACK_DATA, sTimer);

    psTimerCBData->ktime = psTimer->base->get_time();

    res = queue_work(psTimerCBData->psTimerWorkQueue, &psTimerCBData->sWork);
	if (res == 0)
		GPU_DEBUG("HRTimerCallbackWrapper: work already queued");

	if (psTimerCBData->bRestart && psTimerCBData->bActive) {
		hrtimer_forward(&psTimerCBData->sTimer,
						psTimerCBData->ktime,
						HR_TIMER_DELAY_MSEC(psTimerCBData->ui32Delay));
		return HRTIMER_RESTART;
	} else {
		return HRTIMER_NORESTART;
	}
}

static void HRTimerWorkQueueCallBack(struct work_struct *psWork)
{
    HRTIMER_CALLBACK_DATA *psTimerCBData = container_of(psWork, HRTIMER_CALLBACK_DATA, sWork);

	if (!psTimerCBData->bActive)
        return;

    psTimerCBData->pfnTimerFunc(psTimerCBData->pvData);
}

void *SysCreateHRTimer(const char *pszTimerName, PFN_HRTIMER_FUNC pfnTimerFunc, 
					   void *pvData, u32 ui32MsTimeout, bool bRestart)
{
	HRTIMER_CALLBACK_DATA *psTimerCBData;

	if (!pfnTimerFunc) {
		GPU_ERR("%s: passed invalid callback", __func__);
		return NULL;
	}

	psTimerCBData = kmalloc(sizeof(*psTimerCBData), GFP_KERNEL);
	if (!psTimerCBData) {
		GPU_ERR("%s: kmalloc fail", __func__);
		return NULL;
	}

	psTimerCBData->bInUse = true;
	psTimerCBData->bActive = false;
	psTimerCBData->bRestart = bRestart;
	psTimerCBData->pfnTimerFunc = pfnTimerFunc;
	psTimerCBData->pvData = pvData;
	psTimerCBData->ui32Delay = ui32MsTimeout;

    hrtimer_init(&psTimerCBData->sTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    psTimerCBData->sTimer.function = (void *)HRTimerCallbackWrapper;

	psTimerCBData->psTimerWorkQueue = create_singlethread_workqueue(pszTimerName);
	if (!psTimerCBData->psTimerWorkQueue) {
		GPU_ERR("%s: cannot create timer workqueue \"%s\"", __func__, pszTimerName);
		return NULL;
	}

	INIT_WORK(&psTimerCBData->sWork, HRTimerWorkQueueCallBack);

	return (void *)&psTimerCBData->sTimer;
}

void SysDestroyHRTimer(void *hTimer)
{
	HRTIMER_CALLBACK_DATA *psTimerCBData =
			container_of(hTimer, HRTIMER_CALLBACK_DATA, sTimer);

	if (psTimerCBData->bActive) {
		GPU_WARN("%s: hTimer %p still active", __func__, hTimer);
		SysDisableHRTimer(hTimer);
	}

	psTimerCBData->bInUse = false;

	destroy_workqueue(psTimerCBData->psTimerWorkQueue);
	psTimerCBData->psTimerWorkQueue = NULL;

	kfree(psTimerCBData);
}

int SysEnableHRTimer(void *hTimer)
{
	int err;
	HRTIMER_CALLBACK_DATA *psTimerCBData =
			container_of(hTimer, HRTIMER_CALLBACK_DATA, sTimer);

	if (psTimerCBData->bActive)
		return 0;

	err = hrtimer_start(&psTimerCBData->sTimer,
						HR_TIMER_DELAY_MSEC(psTimerCBData->ui32Delay),
						HRTIMER_MODE_REL);
	if (!err)
		psTimerCBData->bActive = true;

	return err;
}

int SysDisableHRTimer(void *hTimer)
{
	HRTIMER_CALLBACK_DATA *psTimerCBData =
			container_of(hTimer, HRTIMER_CALLBACK_DATA, sTimer);

	if (!psTimerCBData->bActive)
		return 0;

	psTimerCBData->bActive = false;
	smp_mb();

	flush_workqueue(psTimerCBData->psTimerWorkQueue);

	hrtimer_cancel(&psTimerCBData->sTimer);

    /*
     * This second flush is to catch the case where the timer ran
     * before we managed to delete it, in which case, it will have
     * queued more work for the workqueue.  Since the bActive flag
     * has been cleared, this second flush won't result in the
     * timer being rearmed.
     */
    flush_workqueue(psTimerCBData->psTimerWorkQueue);

    return 0;
}

static int DvfsPreClockSpeedChange(bool idle)
{
	SYS_DATA *psSysData = GetSysData();
	PVRSRV_ERROR eError;

	if (!psSysData->bSysClocksOneTimeInit)
		return PVRSRV_OK;

	if (psSysData->psRGXDeviceNode == IMG_NULL)
		return PVRSRV_ERROR_INIT_FAILURE;

	eError = PVRSRVDevicePreClockSpeedChange(psSysData->ui32RGXDeviceID, idle, NULL);

	return eError;
}

static int DvfsPostClockSpeedChange(bool idle)
{
	SYS_DATA *psSysData = GetSysData();

	/** Update the clock info for RGX "pfnGetGpuUtilStats". */
	SysUpdateConfigData(NULL);

	PVRSRVDevicePostClockSpeedChange(psSysData->ui32RGXDeviceID, idle, NULL);

	return PVRSRV_OK;
}

static void DvfsCallbackWrapper(void *pvData)
{
	DVFS_TAG *psDvfs = pvData;
	psDvfs->pfnDvfsFunc(psDvfs->pvData);
}

int SysDvfsInit(DVFS_TAG *psDvfs, PFN_DVFS_FUNC DvfsProcCallback, void *pvData,
				u32 ui32WindowMS)
{
	psDvfs->bEnable = false;

	/* Must not exceed RGXFWIF_GPU_STATS_WINDOW_SIZE_US. */
	if (ui32WindowMS == 0 || ui32WindowMS * 1000 > RGXFWIF_GPU_STATS_WINDOW_SIZE_US) {
		GPU_ERR("%s: invalid dvfs window_ms %u", __func__, ui32WindowMS);
		return -EINVAL;
	}

	psDvfs->window_ms = ui32WindowMS;

	psDvfs->pfnDvfsFunc = DvfsProcCallback;
	psDvfs->pvData = pvData;
	psDvfs->preClockSpeedChange = DvfsPreClockSpeedChange;
	psDvfs->postClockSpeedChange = DvfsPostClockSpeedChange;

	return 0;
}

void SysDvfsDeinit(DVFS_TAG *psDvfs)
{
    SYS_DATA *psSysData = GetSysData();
	PVRSRV_DEVICE_NODE *psDeviceNode = psSysData->psRGXDeviceNode;

	if (psDeviceNode) {
		PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

        if (ghGpuUtilStats != NULL)
		{
			psDevInfo->pfnUnregisterGpuUtilStats(ghGpuUtilStats);
		}
    }
    
	if (psDvfs->bEnable) {
		GPU_WARN("%s: dvfs still enabled", __func__);
		SysDvfsDisable(psDvfs);
	}
}

bool SysDvfsIsEnabled(DVFS_TAG *psDvfs)
{
	return psDvfs->bEnable;
}

int SysDvfsEnable(DVFS_TAG *psDvfs)
{
	int err;
	if (psDvfs->bEnable)
		return 0;

	if (psDvfs->pfnDvfsFunc == NULL) {
		GPU_ERR("%s: pfnDvfsFunc is NULL", __func__);
		return -EINVAL;
	}

	if (psDvfs->hDvfsTimer == NULL) {
		psDvfs->hDvfsTimer = SysCreateHRTimer("gpu_dvfs_timer", DvfsCallbackWrapper,
											  psDvfs, psDvfs->window_ms, true);
		if (psDvfs->hDvfsTimer == NULL) {
			GPU_ERR("%s: SysCreateHRTimer failed", __func__);
			return -ENOMEM;
		}
	}

	err = SysEnableHRTimer(psDvfs->hDvfsTimer);
	if (!err)
		psDvfs->bEnable = true;

	return err;
}

int SysDvfsDisable(DVFS_TAG *psDvfs)
{
	if (!psDvfs->bEnable)
		return 0;

	if (psDvfs->hDvfsTimer) {
		SysDisableHRTimer(psDvfs->hDvfsTimer);
		SysDestroyHRTimer(psDvfs->hDvfsTimer);
		psDvfs->hDvfsTimer = NULL;
	}

	psDvfs->bEnable = false;

	return 0;
}

int SysDvfsUpdateUtilStat(DVFS_TAG *psDvfs, DVFS_UTIL_STAT *psUtilStat)
{
	SYS_DATA *psSysData = GetSysData();
	PVRSRV_DEVICE_NODE *psDeviceNode = psSysData->psRGXDeviceNode;

	if (psDeviceNode) {
		PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
        RGXFWIF_GPU_UTIL_STATS sGpuUtilStats;
        PVRSRV_ERROR eError = PVRSRV_OK;

        if (ghGpuUtilStats == NULL)
		{
			eError = psDevInfo->pfnRegisterGpuUtilStats(&ghGpuUtilStats);
		}

		if (eError == PVRSRV_OK)
		{
			eError = psDevInfo->pfnGetGpuUtilStats(psDeviceNode,
			                                       ghGpuUtilStats,
			                                       &sGpuUtilStats);
		}

		psUtilStat->valid = (sGpuUtilStats.bValid && (eError == PVRSRV_OK));
		if (psUtilStat->valid)
		{
			psUtilStat->active  = (sGpuUtilStats.ui64GpuStatActiveHigh + sGpuUtilStats.ui64GpuStatActiveLow) / 100;
			psUtilStat->blocked = sGpuUtilStats.ui64GpuStatBlocked / 100;
			psUtilStat->idle    = 100 - psUtilStat->active - psUtilStat->blocked;
			psUtilStat->memstall = 100; /* cannot get the stall info right now. */
		}

		/*PVR_DPF((PVR_DBG_MESSAGE, "RGX util: "
				 "activeH %5u, activeL %5u, blocked %5u, idle %5u, slcstall %5u",
			     sRgxStats.ui32GpuStatActiveHigh, sRgxStats.ui32GpuStatActiveLow,
			     sRgxStats.ui32GpuStatBlocked, sRgxStats.ui32GpuStatIdle,
			     sRgxStats.ui32SLCStallsRatio));*/
	} else {
		psUtilStat->valid = 0;
	}

	return (psDeviceNode == IMG_NULL);
}
