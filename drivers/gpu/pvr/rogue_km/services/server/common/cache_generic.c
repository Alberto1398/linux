/*************************************************************************/ /*!
@File
@Title          CPU generic cache management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements server side code for CPU cache management in a
                CPU agnostic manner.
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
#if defined(CONFIG_SW_SYNC)
#include <linux/version.h>
#include <linux/seq_file.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
#include <linux/sw_sync.h>
#else
#include <../drivers/staging/android/sw_sync.h>
#endif
#include <linux/file.h>
#include <linux/fs.h>
#endif

#include "cache.h"
#include "device.h"
#include "pvr_debug.h"
#include "pvrsrv.h"
#include "osfunc.h"
#include "pmr.h"

PVRSRV_ERROR CacheOpExec (PMR *psPMR,
						  IMG_DEVMEM_OFFSET_T uiOffset,
						  IMG_DEVMEM_SIZE_T uiSize,
						  PVRSRV_CACHE_OP uiCacheOp)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	return CacheOpQueue(uiCacheOp);
}

PVRSRV_ERROR CacheOpQueue(PVRSRV_CACHE_OP uiCacheOp)
{
	PVRSRV_DATA *psData = PVRSRVGetPVRSRVData();
	psData->uiCacheOp = SetCacheOp(psData->uiCacheOp, uiCacheOp);
	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpFence (RGXFWIF_DM eFenceOpType,
						   IMG_UINT32 ui32OpSeqNum)
{
	PVRSRV_DATA *psData = PVRSRVGetPVRSRVData();

	PVR_UNREFERENCED_PARAMETER(eFenceOpType);
	PVR_UNREFERENCED_PARAMETER(ui32OpSeqNum);

	if (OSCPUOperation(psData->uiCacheOp) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "CacheOpFence: OSCPUOperation failed"));
		PVR_ASSERT(0);
	}

	psData->uiCacheOp = PVRSRV_CACHE_OP_NONE;
	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpSetTimeline (IMG_INT32 i32Timeline)
{
	PVRSRV_ERROR eError = CacheOpFence(0, 0);
#if defined(CONFIG_SW_SYNC)
	struct file *file;

	if (i32Timeline < 0)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	file = fget(i32Timeline);
	if (!file || !file->private_data)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	sw_sync_timeline_inc(file->private_data, 1);
	fput(file);
#endif
	return eError;
}

PVRSRV_ERROR CacheOpInit()
{
	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpDeInit()
{
	return PVRSRV_OK;
}
