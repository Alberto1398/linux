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

#include "interrupt_support.h"
#include "pvrsrv_device.h"
#include "syscommon.h"
#if defined(SUPPORT_PVRSRV_GPUVIRT)
#include "vz_support.h"
#endif
#include "sysinfo.h"
#include "sysconfig.h"
#include "physheap.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif
#include "pvrsrv.h"
#include "syslocal.h"
#include "owlinit.h"

static RGX_TIMING_INFORMATION	gsRGXTimingInfo;
static RGX_DATA					gsRGXData;
static PVRSRV_DEVICE_CONFIG 	gsDevices[1];
static PHYS_HEAP_FUNCTIONS		gsPhysHeapFuncs;

#if defined(SUPPORT_PVRSRV_GPUVIRT)
static PHYS_HEAP_CONFIG			gsPhysHeapConfig[2];
#else
static PHYS_HEAP_CONFIG			gsPhysHeapConfig[1];
#endif


extern struct platform_device *gpsPVRLDMDev;

static SYS_DATA gsSysData = {
	.bClocksEnabled = false,
	.bSysClocksOneTimeInit = false,
	.psRGXDeviceNode = NULL,

	.hPowerWork = NULL,
	.ui32MSPowerTimeout = OWL_RGX_POWEROFF_TIMEOUT_MS,
};


SYS_DATA *GetSysData(void)
{
	return &gsSysData;
}


static PVRSRV_ERROR GetRGXDeviceID(SYS_DATA *psSysData)
{
	if (psSysData->psRGXDeviceNode == NULL)
	{
		PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
		PVRSRV_DEVICE_NODE *psDeviceNode = psPVRSRVData->psDeviceNodeList;
		while (psDeviceNode)
		{
			if (psDeviceNode->sDevId.ui32DeviceIndex == 0)
			{
				psSysData->psRGXDeviceNode = psDeviceNode;
				psSysData->ui32RGXDeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
				break;
			}

			psDeviceNode = psDeviceNode->psNext;
		}

		if (psSysData->psRGXDeviceNode == NULL)
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
		psSysData->hPowerWork = NULL;
	}

	psSysData->bSysClocksOneTimeInit = false;
}

/*
	CPU to Device physcial address translation
*/
static
void UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
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
void UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
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

static IMG_UINT32 OwlRGXGetGpuClockFreq()
{
	return (IMG_UINT32)owl_gpu_get_clock_speed();
}

static PVRSRV_ERROR OwlRGXDevPrePowerState(IMG_HANDLE hSysData,PVRSRV_DEV_POWER_STATE eNewPowerState,
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

static PVRSRV_ERROR OwlRGXDevPostPowerState(IMG_HANDLE hSysData,PVRSRV_DEV_POWER_STATE eNewPowerState,
                                            PVRSRV_DEV_POWER_STATE eCurrentPowerState,
									        IMG_BOOL bForced)
{
	PVR_UNREFERENCED_PARAMETER(eCurrentPowerState);

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
	{
		if (!gsSysData.bClocksEnabled)
			return PVRSRV_OK;

		owl_gpu_set_clock_enable(false);
		
		if(bForced){
			
			OWLRGXPowerOffTimerDisable(&gsSysData);
			
			owl_gpu_set_power_enable(false);
			
		}else{
			if (OWLRGXPowerOffTimerEnable(&gsSysData))
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


PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	IMG_UINT32 ui32NextPhysHeapID = 0;

	if (gsDevices[0].pvOSDevice)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* owl Init */
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

	gsPhysHeapConfig[ui32NextPhysHeapID].ui32PhysHeapID = ui32NextPhysHeapID;
	gsPhysHeapConfig[ui32NextPhysHeapID].pszPDumpMemspaceName = "SYSMEM";
	gsPhysHeapConfig[ui32NextPhysHeapID].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[ui32NextPhysHeapID].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[ui32NextPhysHeapID].hPrivData = NULL;
	ui32NextPhysHeapID += 1;

	/*
	 * Setup RGX specific timing data
	 */
	gsRGXTimingInfo.ui32CoreClockSpeed        = OwlRGXGetGpuClockFreq();
	gsRGXTimingInfo.bEnableActivePM           = IMG_TRUE;
	gsRGXTimingInfo.bEnableRDPowIsland        = IMG_FALSE;
	gsRGXTimingInfo.ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/*
	 *Setup RGX specific data
	 */
	gsRGXData.psRGXTimingInfo = &gsRGXTimingInfo;

	/*
	 * Setup device
	 */
	gsDevices[0].pvOSDevice				= pvOSDevice;
	gsDevices[0].pszName                = "owl";
	gsDevices[0].pszVersion             = NULL;
	gsDevices[0].hSysData               = pvOSDevice;

	/* Device setup information */
	gsDevices[0].sRegsCpuPBase.uiAddr   = RGX_OWL_REGS_PHYS_BASE;
	gsDevices[0].ui32RegsSize           = RGX_OWL_REGS_SIZE;
	gsDevices[0].ui32IRQ                = RGX_OWL_IRQ;
	//gsDevices[0].bIRQIsShared           = IMG_FALSE;
	gsDevices[0].eCacheSnoopingMode     = PVRSRV_DEVICE_SNOOP_NONE;

	/* Device's physical heaps */
	gsDevices[0].pasPhysHeaps = gsPhysHeapConfig;
	gsDevices[0].ui32PhysHeapCount = IMG_ARR_NUM_ELEMS(gsPhysHeapConfig);

	/* Device's physical heap IDs */
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] = 0;
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] = 0;
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL] = 0;

	gsDevices[0].pui32BIFTilingHeapConfigs = gauiBIFTilingHeapXStrides;
	gsDevices[0].ui32BIFTilingHeapCount = IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides);

	/* Power management on SUNXI system */
	gsDevices[0].pfnPrePowerState       = OwlRGXDevPrePowerState;
	gsDevices[0].pfnPostPowerState      = OwlRGXDevPostPowerState;

	/* No clock frequency either */
	gsDevices[0].pfnClockFreqGet        = OwlRGXGetGpuClockFreq;

	/* No interrupt handled either */
	//gsDevices[0].pfnInterruptHandled    = NULL;

	//gsDevices[0].pfnCheckMemAllocSize   = SysCheckMemAllocSize;

	gsDevices[0].hDevData               = &gsRGXData;

#if defined(PVR_DVFS)
	gsDevices[0].sDVFS.sDVFSDeviceCfg.ui32PollMs = 100;
	gsDevices[0].sDVFS.sDVFSDeviceCfg.bIdleReq = IMG_FALSE;

	gsDevices[0].sDVFS.sDVFSGovernorCfg.ui32UpThreshold = 90;
	gsDevices[0].sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;
#endif
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

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	/* Virtualization support services needs to know which heap ID corresponds to FW */
	PVR_ASSERT(ui32NextPhysHeapID < IMG_ARR_NUM_ELEMS(gsPhysHeapConfig));
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL] = ui32NextPhysHeapID;
	gsPhysHeapConfig[ui32NextPhysHeapID].ui32PhysHeapID = ui32NextPhysHeapID;
	gsPhysHeapConfig[ui32NextPhysHeapID].pszPDumpMemspaceName = "SYSMEM";
	gsPhysHeapConfig[ui32NextPhysHeapID].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[ui32NextPhysHeapID].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[ui32NextPhysHeapID].hPrivData = NULL;
	SysVzDevInit(&gsDevices[0]);
#endif

	*ppsDevConfig = &gsDevices[0];

	return PVRSRV_OK;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
#if defined(SUPPORT_PVRSRV_GPUVIRT)
	SysVzDevDeInit(psDevConfig);
#endif

	/* owl DeInit */
	owl_gpu_deinit();

#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	psDevConfig->pvOSDevice = NULL;
}

PVRSRV_ERROR SysUpdateConfigData(void)
{
	gsRGXTimingInfo.ui32CoreClockSpeed = OwlRGXGetGpuClockFreq();

	return PVRSRV_OK;
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
								  IMG_UINT32 ui32IRQ,
								  const IMG_CHAR *pszName,
								  PFN_LISR pfnLISR,
								  void *pvData,
								  IMG_HANDLE *phLISRData)
{
	IMG_UINT32 ui32IRQFlags = SYS_IRQ_FLAG_TRIGGER_DEFAULT;

	PVR_UNREFERENCED_PARAMETER(hSysData);

#if defined(PVRSRV_GPUVIRT_MULTIDRV_MODEL)
	ui32IRQFlags |= SYS_IRQ_FLAG_SHARED;
#endif

	return OSInstallSystemLISR(phLISRData, ui32IRQ, pszName, pfnLISR, pvData,
							   ui32IRQFlags);
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	return OSUninstallSystemLISR(hLISRData);
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	IMG_UINT32			ui32CurrentCoreClockSpeed = OwlRGXGetGpuClockFreq();                                          
	IMG_UINT32 ui32CurrentVoltage= owl_gpu_get_clock_voltage(ui32CurrentCoreClockSpeed);        
	PVR_DUMPDEBUG_LOG("Current CoreClockSpeed,voltage: (%lld,%lld)",ui32CurrentCoreClockSpeed,ui32CurrentVoltage);                 
	return PVRSRV_OK;
} 
/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/
