/*************************************************************************/ /*!
@File
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
*/ /**************************************************************************/

#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <drm/drm_mm.h>

#include "drm_pdp.h"

#define PDP_PHYS_HEAP_ID 1

#define PDP_MM_CLEANUP_DELAY_MSECS 1000

struct pdp_mm {
	void *heap;
	struct drm_mm page_manager;
	struct mutex lock;

	uint64_t cpu_phys_base;
	uint64_t dev_phys_base;
	size_t size;

	struct delayed_work cleanup_work;
};

struct pdp_mm_allocation {
	struct pdp_mm *mm;
	struct drm_mm_node *page_offset;
};


static struct drm_mm_node *mm_page_offset_get(struct pdp_mm *mm,
					      uint64_t page_count)
{
	struct drm_mm_node *page_offset;
	int err;

	page_offset = kzalloc(sizeof(*page_offset), GFP_KERNEL);
	if (!page_offset) {
		DRM_ERROR("failed to allocate page offset\n");
		return NULL;
	}

	mutex_lock(&mm->lock);
	err = drm_mm_insert_node(&mm->page_manager,
				 page_offset,
				 page_count,
				 0
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)) || \
	defined(CHROMIUMOS_WORKAROUNDS_KERNEL310)
				 , DRM_MM_SEARCH_BEST
#endif
		);
	mutex_unlock(&mm->lock);

	if (err) {
		DRM_ERROR("failed to insert page offset (err=%d)\n", err);
		kfree(page_offset);
		return NULL;
	}

	return page_offset;
}

static void mm_page_offset_put(struct pdp_mm *mm,
			       struct drm_mm_node *page_offset)
{
	mutex_lock(&mm->lock);
	drm_mm_remove_node(page_offset);
	mutex_unlock(&mm->lock);

	kfree(page_offset);
}

int pdp_mm_alloc(struct pdp_display_device *display_dev,
		 size_t size,
		 struct pdp_mm_allocation **alloc_out)
{
	struct pdp_mm_allocation *allocation;
	uint64_t page_count = PAGE_ALIGN(size) >> PAGE_SHIFT;

	DRM_DEBUG_DRIVER("mm: allocating memory (size = %zu)\n", size);

	if (!display_dev->mm) {
		DRM_ERROR("memory manager not initialised\n");
		return -EINVAL;
	}

	allocation = kmalloc(sizeof(*allocation), GFP_KERNEL);
	if (!allocation)
		return -ENOMEM;

	allocation->mm = display_dev->mm;
	allocation->page_offset = mm_page_offset_get(display_dev->mm,
						     page_count);
	if (!allocation->page_offset) {
		kfree(allocation);
		return -ENOMEM;
	}

	*alloc_out = allocation;

	return 0;
}

void pdp_mm_free(struct pdp_mm_allocation *alloc)
{
	mm_page_offset_put(alloc->mm, alloc->page_offset);

	kfree(alloc);
}

int pdp_mm_cpu_paddr(struct pdp_mm_allocation *alloc, uint64_t *addr_out)
{
	if (!alloc || !addr_out)
		return -EINVAL;

	*addr_out = alloc->mm->cpu_phys_base +
		(alloc->page_offset->start << PAGE_SHIFT);

	return 0;
}

int pdp_mm_dev_paddr(struct pdp_mm_allocation *alloc, uint64_t *addr_out)
{
	if (!alloc || !addr_out)
		return -EINVAL;

	*addr_out = alloc->mm->dev_phys_base +
		(alloc->page_offset->start << PAGE_SHIFT);

	return 0;
}

int pdp_mm_size(struct pdp_mm_allocation *alloc, size_t *size_out)
{
	if (!alloc || !size_out)
		return -EINVAL;

	*size_out = (alloc->page_offset->size << PAGE_SHIFT);

	return 0;
}

static void pdp_mm_delayed_cleanup(struct work_struct *work)
{
	struct delayed_work *delayedWork = to_delayed_work(work);
	struct pdp_mm *mm =
		container_of(delayedWork, struct pdp_mm, cleanup_work);

	if (list_empty(&mm->page_manager.head_node.node_list)) {
		DRM_INFO("all outstanding allocations freed (cleaning up)\n");

		drm_mm_takedown(&mm->page_manager);

		kfree(mm);
	} else {
		unsigned long delay_jiffies =
			msecs_to_jiffies(PDP_MM_CLEANUP_DELAY_MSECS);

		if (schedule_delayed_work(&mm->cleanup_work,
					  delay_jiffies))
			return;

		DRM_ERROR("failed to delay clean up\n");
	}

	module_put(THIS_MODULE);
}

int pdp_mm_init(struct pdp_display_device *display_dev)
{
	struct pdp_mm *mm;
	int err;

	if (display_dev->mm) {
		DRM_ERROR("already initialised\n");
		return -EINVAL;
	}

	mm = kmalloc(sizeof(*mm), GFP_KERNEL);
	if (!mm)
		return -ENOMEM;

	mutex_init(&mm->lock);

	err = pvr_drm_heap_acquire(display_dev->dev,
				   PDP_PHYS_HEAP_ID,
				   &mm->heap);
	if (err) {
		DRM_ERROR("failed to acquire heap with ID %d (err=%d)\n",
			  PDP_PHYS_HEAP_ID, err);
		goto error_free_mm;
	}

	err = pvr_drm_heap_info(display_dev->dev,
				mm->heap,
				&mm->cpu_phys_base,
				&mm->dev_phys_base,
				&mm->size);
	if (err) {
		DRM_ERROR("failed to get heap info (err=%d)\n", err);
		goto err_heap_release;
	}

	(void)drm_mm_init(&mm->page_manager, 0, mm->size >> PAGE_SHIFT);

	INIT_DELAYED_WORK(&mm->cleanup_work,
			  pdp_mm_delayed_cleanup);

	display_dev->mm = mm;

	DRM_DEBUG_DRIVER("mm: initialised\n");

	return 0;

err_heap_release:
	pvr_drm_heap_release(display_dev->dev, mm->heap);
error_free_mm:
	kfree(mm);
	return err;
}

void pdp_mm_cleanup(struct pdp_display_device *display_dev)
{
	DRM_DEBUG_DRIVER("mm: cleaning up\n");

	if (!display_dev->mm) {
		DRM_ERROR("memory manager not initialised\n");
		return;
	}

	pvr_drm_heap_release(display_dev->dev, display_dev->mm->heap);

	/* If the page manager still has allocated pages then delay clean up */
	if (list_empty(&display_dev->mm->page_manager.head_node.node_list)) {
		drm_mm_takedown(&display_dev->mm->page_manager);

		kfree(display_dev->mm);
	} else {
		unsigned long delay_jiffies =
			msecs_to_jiffies(PDP_MM_CLEANUP_DELAY_MSECS);

		DRM_INFO("outstanding allocations exist (delaying clean up)\n");

		__module_get(THIS_MODULE);

		if (!schedule_delayed_work(&display_dev->mm->cleanup_work,
					   delay_jiffies)) {
			DRM_ERROR("failed to delay clean up\n");
			module_put(THIS_MODULE);
		}
	}

	display_dev->mm = NULL;

	DRM_DEBUG_DRIVER("mm: finished cleaning up\n");
}
