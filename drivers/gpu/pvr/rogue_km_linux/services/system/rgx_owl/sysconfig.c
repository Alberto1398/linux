/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
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
*/ /**************************************************************************/

#include <linux/platform_device.h>

#include "pvrsrv_device.h"
#include "syscommon.h"
#include "sysconfig.h"
#include "physheap.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif

#include "syslocal.h"
#include "owlinit.h"

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
#error "SUPPORT_SYSTEM_INTERRUPT_HANDLING is not supported on OWL system."
#endif


extern struct platform_device *gpsPVRLDMDev;

static SYS_DATA gsSysData = {
	.bClocksEnabled = false,
	.bSysClocksOneTimeInit = false,
	.psRGXDeviceNode = NULL,

	.hPowerWork = NULL,
	.ui32MSPowerTimeout = OWL_RGX_POWEROFF_TIMEOUT_MS,
};

static RGX_TIMING_INFORMATION	gsRGXTimingInfo;
static RGX_DATA                 gsRGXData;
static PVRSRV_DEVICE_CONFIG 	gsDevices[1];
static PVRSRV_SYSTEM_CONFIG 	gsSysConfig;

static PHYS_HEAP_FUNCTIONS      gsPhysHeapFuncs;
static PHYS_HEAP_CONFIG         gsPhysHeapConfig[1];

SYS_DATA *GetSysData(void)
{
	return &gsSysData;
}

static PVRSRV_ERROR GetRGXDeviceID(SYS_DATA *psSysData)
{
	if (psSysData->psRGXDeviceNode == IMG_NULL)
	{
		PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
		PVRSRV_DEVICE_NODE *psDeviceNode = psPVRSRVData->psDeviceNodeList;
		while (psDeviceNode)
		{
			if (psDeviceNode->sDevId.eDeviceType == PVRSRV_DEVICE_TYPE_RGX)
			{
				psSysData->psRGXDeviceNode = psDeviceNode;
				psSysData->ui32RGXDeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
				break;
			}

			psDeviceNode = psDeviceNode->psNext;
		}

		if (psSysData->psRGXDeviceNode == IMG_NULL)
			return PVRSRV_ERROR_INIT_FAILURE;
	}

	return PVRSRV_OK;
}

static int OWLRGXPowerOffTimerEnable(SYS_DATA *psSysData)
{
	return SysStartWork(psSysData->hPowerWork);
}

static void OWLRGXPowerOffTimerDisable(SYS_DATA *psSysData)
{
	SysStopWork(psSysData->hPowerWork);
}

static void PowerOffTimerCallBack(void *pvData)
{
	PVR_DPF((PVR_DBG_MESSAGE, "%s: power supply off\n", __func__));

	owl_gpu_set_power_enable(false);
}

static PVRSRV_ERROR OWLRGXSysDataInit(SYS_DATA *psSysData)
{
	if (psSysData->bSysClocksOneTimeInit)
		return PVRSRV_OK;

	if (GetRGXDeviceID(psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: GetRGXDeviceID failed", __func__));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	psSysData->hPowerWork = SysCreateWork(NULL, PowerOffTimerCallBack, psSysData, 
										  psSysData->ui32MSPowerTimeout);
	if (!psSysData->hPowerWork)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: SysCreateWork failed", __func__));
		return PVRSRV_ERROR_UNABLE_TO_ADD_TIMER;
	}

	psSysData->bSysClocksOneTimeInit = true;

	return PVRSRV_OK;
}

static void OWLRGXSysDataDeInit(SYS_DATA *psSysData)
{
	if (psSysData->hPowerWork)
	{
		SysDestroyWork(psSysData->hPowerWork);
		psSysData->hPowerWork = IMG_NULL;
	}

	psSysData->bSysClocksOneTimeInit = false;
}


/*
	CPU to Device physcial address translation
*/
static
IMG_VOID UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
									   IMG_UINT32 ui32NumOfAddr,
									   IMG_DEV_PHYADDR *psDevPAddr,
									   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);
	
	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
		}
	}
}

/*
	Device to CPU physcial address translation
*/
static
IMG_VOID UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
									   IMG_UINT32 ui32NumOfAddr,
									   IMG_CPU_PHYADDR *psCpuPAddr,
									   IMG_DEV_PHYADDR *psDevPAddr)				  
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);
	
	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr;
		}
	}
}

static IMG_UINT32 OwlRGXGetGpuClockFreq(IMG_HANDLE hSysData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	return (IMG_UINT32)owl_gpu_get_clock_speed();
}

static PVRSRV_ERROR OwlRGXDevPrePowerState(PVRSRV_DEV_POWER_STATE eNewPowerState,
                                           PVRSRV_DEV_POWER_STATE eCurrentPowerState,
									       IMG_BOOL bForced)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_UNREFERENCED_PARAMETER(eCurrentPowerState);

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
	{
		if (gsSysData.bClocksEnabled)
			return PVRSRV_OK;

		if ((eError = OWLRGXSysDataInit(&gsSysData)) != PVRSRV_OK)
			return eError;

		OWLRGXPowerOffTimerDisable(&gsSysData);

		if (owl_gpu_set_power_enable(true))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: power_enable failed", __func__));
			return PVRSRV_ERROR_DEVICE_POWER_CHANGE_FAILURE;
		}

		if (owl_gpu_set_clock_enable(true))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: clock_enable failed", __func__));
			return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;
		}

		gsSysData.bClocksEnabled = true;
	}

	return eError;
}

static PVRSRV_ERROR OwlRGXDevPostPowerState(PVRSRV_DEV_POWER_STATE eNewPowerState,
                                            PVRSRV_DEV_POWER_STATE eCurrentPowerState,
									        IMG_BOOL bForced)
{
	PVR_UNREFERENCED_PARAMETER(eCurrentPowerState);

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
	{
		if (!gsSysData.bClocksEnabled)
			return PVRSRV_OK;

		owl_gpu_set_clock_enable(false);

		if (OWLRGXPowerOffTimerEnable(&gsSysData)){
			 PVR_DPF((PVR_DBG_ERROR, "%s: poweroff timer enable failed", __func__));
		}

		gsSysData.bClocksEnabled = false;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR OwlRGXSysPrePowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (eNewPowerState == PVRSRV_SYS_POWER_STATE_ON)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: entering state power_on\n", __func__));
	}

	return eError;
}

static PVRSRV_ERROR OwlRGXSysPostPowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	if (eNewPowerState == PVRSRV_SYS_POWER_STATE_OFF)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: entering state power_off\n", __func__));
	}

	return PVRSRV_OK;
}

/*
	SysCreateConfigData
*/
PVRSRV_ERROR SysCreateConfigData(PVRSRV_SYSTEM_CONFIG **ppsSysConfig, void *hDevice)
{
	/* Owl Init */
	if (owl_gpu_init(&gpsPVRLDMDev->dev))
	{
		PVR_DPF((PVR_DBG_ERROR, "owl_gpu_init failed."));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/*
	 * Setup information about physical memory heap(s) we have
	 */
	gsPhysHeapFuncs.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr;
	gsPhysHeapFuncs.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr;

	gsPhysHeapConfig[0].ui32PhysHeapID = 0;
	gsPhysHeapConfig[0].pszPDumpMemspaceName = "SYSMEM";
	gsPhysHeapConfig[0].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[0].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[0].hPrivData = IMG_NULL;

	gsSysConfig.pasPhysHeaps = &(gsPhysHeapConfig[0]);
	gsSysConfig.ui32PhysHeapCount = IMG_ARR_NUM_ELEMS(gsPhysHeapConfig);

	gsSysConfig.pui32BIFTilingHeapConfigs = gauiBIFTilingHeapXStrides;
	gsSysConfig.ui32BIFTilingHeapCount = IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides);

	/*
	 * Setup RGX specific timing data
	 */
	gsRGXTimingInfo.ui32CoreClockSpeed        = OwlRGXGetGpuClockFreq(NULL);
	gsRGXTimingInfo.bEnableActivePM           = IMG_TRUE;
	gsRGXTimingInfo.bEnableRDPowIsland        = IMG_FALSE;
	gsRGXTimingInfo.ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/*
	 *Setup RGX specific data
	 */
	gsRGXData.psRGXTimingInfo = &gsRGXTimingInfo;

	/*
	 * Setup RGX device
	 */
	gsDevices[0].eDeviceType            = PVRSRV_DEVICE_TYPE_RGX;
	gsDevices[0].pszName                = "RGX";

	/* Device setup information */
	gsDevices[0].sRegsCpuPBase.uiAddr   = RGX_OWL_REGS_PHYS_BASE;
	gsDevices[0].ui32RegsSize           = RGX_OWL_REGS_SIZE;
	gsDevices[0].ui32IRQ                = RGX_OWL_IRQ;
	gsDevices[0].bIRQIsShared           = IMG_FALSE;
	gsDevices[0].eIRQActiveLevel        = PVRSRV_DEVICE_IRQ_ACTIVE_SYSDEFAULT;

	/* Device's physical heap IDs */
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] = 0;
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] = 0;

	/* Power management on OWL system */
	gsDevices[0].pfnPrePowerState       = OwlRGXDevPrePowerState;
	gsDevices[0].pfnPostPowerState      = OwlRGXDevPostPowerState;

	/* No clock frequency either */
	gsDevices[0].pfnClockFreqGet        = OwlRGXGetGpuClockFreq;

	/* No interrupt handled either */
	gsDevices[0].pfnInterruptHandled    = IMG_NULL;

	gsDevices[0].pfnCheckMemAllocSize   = SysCheckMemAllocSize;

	gsDevices[0].hDevData               = &gsRGXData;

	/* OWL System specific data */
	gsDevices[0].hSysData               = &gsSysData;

	/*
	 * Setup system config
	 */
	gsSysConfig.pszSystemName = RGX_OWL_SYSTEM_NAME;
	gsSysConfig.uiDeviceCount = IMG_ARR_NUM_ELEMS(gsDevices);
	gsSysConfig.pasDevices = &gsDevices[0];

	/* No power management on no HW system */
	gsSysConfig.pfnSysPrePowerState = OwlRGXSysPrePowerState;
	gsSysConfig.pfnSysPostPowerState = OwlRGXSysPostPowerState;

	/* no cache snooping */
	gsSysConfig.eCacheSnoopingMode = PVRSRV_SYSTEM_SNOOP_NONE;

#if defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
	{
		struct resource *res_mem;
		res_mem = platform_get_resource(gpsPVRLDMDev, IORESOURCE_MEM, 0);

		gsDevices[0].ui32IRQ = platform_get_irq(gpsPVRLDMDev, 0);
		gsDevices[0].sRegsCpuPBase.uiAddr = res_mem->start;
		gsDevices[0].ui32RegsSize = resource_size(res_mem);		
	}
#endif

	/* Setup other system specific stuff */
#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

	*ppsSysConfig = &gsSysConfig;

	return PVRSRV_OK;
}

/*
	SysDestroyConfigData
*/
IMG_VOID SysDestroyConfigData(PVRSRV_SYSTEM_CONFIG *psSysConfig)
{
	PVR_UNREFERENCED_PARAMETER(psSysConfig);

	OWLRGXSysDataDeInit(&gsSysData);

#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	/* Owl DeInit */
	owl_gpu_deinit();
}

PVRSRV_ERROR SysUpdateConfigData(IMG_HANDLE hSysData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	gsRGXTimingInfo.ui32CoreClockSpeed = OwlRGXGetGpuClockFreq(hSysData);

	return PVRSRV_OK;
}

PVRSRV_ERROR SysAcquireSystemData(IMG_HANDLE hSysData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	return PVRSRV_OK;
}

PVRSRV_ERROR SysReleaseSystemData(IMG_HANDLE hSysData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	return PVRSRV_OK;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_SYSTEM_CONFIG *psSysConfig, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	PVR_UNREFERENCED_PARAMETER(psSysConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);

	return PVRSRV_OK;
}

/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/
