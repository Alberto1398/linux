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

#include <linux/module.h>
#include <linux/version.h>

#if defined(LMA)
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#else
#include <linux/vmalloc.h>
#endif

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include <pvr_drm_display.h>
#include <pvr_drm_display_external.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
#define kmalloc_array(n, size, flags) kmalloc((n) * (size), flags)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
#define connector_name(connector) (connector)->name
#define encoder_name(encoder) (encoder)->name
#else
#define connector_name(connector) "unknown"
#define encoder_name(encoder) "unknown"
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
#define NULLDISP_CRTC_FB(crtc) ((crtc)->fb)
#else
#define NULLDISP_CRTC_FB(crtc) ((crtc)->primary->fb)
#endif

#define NULLDISP_FB_WIDTH_MIN 0
#define NULLDISP_FB_WIDTH_MAX 2048
#define NULLDISP_FB_HEIGHT_MIN 0
#define NULLDISP_FB_HEIGHT_MAX 2048

#if defined(LMA)
#define NULLDISP_PHYS_HEAP_ID 1
#define NULLDISP_MM_DEINIT_DELAY_MSECS 1000

struct nulldisp_mm {
	void *heap;
	struct drm_mm page_manager;
	struct mutex lock;

	uint64_t cpu_phys_base_addr;
	uint64_t dev_phys_base_addr;
	size_t size;

	struct delayed_work delayed_deinit_work;
};

struct nulldisp_mm_allocation {
	struct nulldisp_mm *mm;
	struct drm_mm_node page_offset;
};
#endif /* defined(LMA) */

struct pvr_drm_display_buffer {
	void *mem;
	size_t size;
	struct kref refcount;
};

struct nulldisp_display_device {
	struct drm_device *dev;

#if defined(LMA)
	struct nulldisp_mm *mm;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	struct drm_property *mem_layout_prop;
	struct drm_property *fbc_format_prop;
#endif
};

struct nulldisp_crtc {
	struct drm_crtc base;
	struct nulldisp_display_device *display_dev;
};

struct nulldisp_connector {
	struct drm_connector base;
	struct nulldisp_display_device *display_dev;
};

struct nulldisp_encoder {
	struct drm_encoder base;
	struct nulldisp_display_device *display_dev;
};

struct nulldisp_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

#define to_nulldisp_crtc(crtc) \
	container_of(crtc, struct nulldisp_crtc, base)
#define to_nulldisp_connector(connector) \
	container_of(connector, struct nulldisp_connector, base)
#define to_nulldisp_encoder(encoder) \
	container_of(encoder, struct nulldisp_encoder, base)
#define to_nulldisp_framebuffer(framebuffer) \
	container_of(framebuffer, struct nulldisp_framebuffer, base)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
#define PARAM_STRING_LEN 128

static const struct drm_prop_enum_list nulldisp_mem_layout_enum_list[] = {
	{ FB_MEMLAYOUT_STRIDED,		"strided" },
	{ FB_MEMLAYOUT_COMPRESSED,	"compressed" },
	{ FB_MEMLAYOUT_BIF_PAGE_TILED,	"bif_page_tiled" }
};

static char param_mem_layout[PARAM_STRING_LEN + 1] = "strided";

module_param_string(mem_layout, param_mem_layout, PARAM_STRING_LEN, S_IRUGO);
MODULE_PARM_DESC(mem_layout,
		 "Preferred memory layout (strided, compressed or bif_page_tiled)");

static const struct drm_prop_enum_list nulldisp_fbc_format_enum_list[] = {
	{ FB_COMPRESSION_NONE,			"none" },
	{ FB_COMPRESSION_DIRECT_8x8,		"direct_8x8" },
	{ FB_COMPRESSION_DIRECT_16x4,		"direct_16x4" },
	{ FB_COMPRESSION_DIRECT_32x2,		"direct_32x2" },
	{ FB_COMPRESSION_INDIRECT_8x8,		"indirect_8x8" },
	{ FB_COMPRESSION_INDIRECT_16x4,		"indirect_16x4" },
	{ FB_COMPRESSION_INDIRECT_4TILE_8x8,	"indirect_4tile_8x8" },
	{ FB_COMPRESSION_INDIRECT_4TILE_16x4,	"indirect_4tile_16x4" }
};

static char param_fbc_format[PARAM_STRING_LEN + 1] = "none";

module_param_string(fbc_format, param_fbc_format, PARAM_STRING_LEN, S_IRUGO);
MODULE_PARM_DESC(fbc_format,
		 "Specifies the preferred framebuffer compression format "
		 "(none, direct_8x8, direct_16x4, direct_32x2, indirect_8x8, "
		 "indirect_16x4, indirect_4tile_8x8 or indirect_4tile_16x4)");
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) */


#if defined(LMA)
static int nulldisp_mm_alloc_cpu_phys_addr(void *mem, uint64_t *addr)
{
	struct nulldisp_mm_allocation *allocation =
		(struct nulldisp_mm_allocation *)mem;

	if (!mem || !addr)
		return -EINVAL;

	*addr = allocation->mm->cpu_phys_base_addr +
		(allocation->page_offset.start << PAGE_SHIFT);

	return 0;
}

static int nulldisp_mm_alloc_dev_phys_addr(void *mem, uint64_t *addr)
{
	struct nulldisp_mm_allocation *allocation =
		(struct nulldisp_mm_allocation *)mem;

	if (!mem || !addr)
		return -EINVAL;

	*addr = allocation->mm->dev_phys_base_addr +
		(allocation->page_offset.start << PAGE_SHIFT);

	return 0;
}

static void *nulldisp_mm_alloc(struct nulldisp_mm *mm, size_t size)
{
	struct nulldisp_mm_allocation *allocation;
	int err;

	allocation = kzalloc(sizeof(*allocation), GFP_KERNEL);
	if (!allocation) {
		DRM_ERROR("allocation failed\n");
		return NULL;
	}

	mutex_lock(&mm->lock);
	err = drm_mm_insert_node(&mm->page_manager,
				 &allocation->page_offset,
				 PAGE_ALIGN(size) >> PAGE_SHIFT,
				 0
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
				 , DRM_MM_SEARCH_BEST
#endif
		);
	mutex_unlock(&mm->lock);

	if (err) {
		DRM_ERROR("failed to insert page offset (%d)\n", err);
		kfree(allocation);
		return NULL;
	}

	allocation->mm = mm;

	return (void *)allocation;
}

static void nulldisp_mm_free(void *mem)
{
	struct nulldisp_mm_allocation *allocation =
		(struct nulldisp_mm_allocation *)mem;

	if (allocation) {
		mutex_lock(&allocation->mm->lock);
		drm_mm_remove_node(&allocation->page_offset);
		mutex_unlock(&allocation->mm->lock);

		kfree(allocation);
	}
}

static void nulldisp_mm_delayed_deinit(struct work_struct *work)
{
	struct delayed_work *delayedWork = to_delayed_work(work);
	struct nulldisp_mm *mm = container_of(delayedWork,
					      struct nulldisp_mm,
					      delayed_deinit_work);

	if (list_empty(&mm->page_manager.head_node.node_list)) {
		DRM_INFO("outstanding allocations freed (cleaning up)\n");
		drm_mm_takedown(&mm->page_manager);
		kfree(mm);
	} else {
		unsigned long delay_jiffies =
			msecs_to_jiffies(NULLDISP_MM_DEINIT_DELAY_MSECS);

		if (schedule_delayed_work(&mm->delayed_deinit_work,
					  delay_jiffies))
			return;
		DRM_ERROR("Failed to delay clean up\n");
	}

	module_put(THIS_MODULE);
}

static int nulldisp_mm_init(struct nulldisp_display_device *display_dev)
{
	struct nulldisp_mm *mm;
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
				   NULLDISP_PHYS_HEAP_ID,
				   &mm->heap);
	if (err) {
		DRM_ERROR("failed to acquire heap with ID %d (%d)\n",
			  NULLDISP_PHYS_HEAP_ID, err);
		goto error_free_mm;
	}

	err = pvr_drm_heap_info(display_dev->dev,
				mm->heap,
				&mm->cpu_phys_base_addr,
				&mm->dev_phys_base_addr,
				&mm->size);
	if (err) {
		DRM_ERROR("failed to get heap info (%d)\n", err);
		goto err_heap_release;
	}

	(void)drm_mm_init(&mm->page_manager, 0, mm->size >> PAGE_SHIFT);

	INIT_DELAYED_WORK(&mm->delayed_deinit_work, nulldisp_mm_delayed_deinit);

	display_dev->mm = mm;

	return 0;

err_heap_release:
	pvr_drm_heap_release(display_dev->dev, mm->heap);
error_free_mm:
	kfree(mm);
	return err;
}

static void nulldisp_mm_deinit(struct nulldisp_display_device *display_dev)
{
	struct nulldisp_mm *mm = display_dev->mm;

	if (!mm) {
		DRM_ERROR("not initialised\n");
		return;
	}

	pvr_drm_heap_release(display_dev->dev, mm->heap);

	/* If the page manager still has allocated pages then delay clean up */
	if (list_empty(&mm->page_manager.head_node.node_list)) {
		drm_mm_takedown(&mm->page_manager);

		kfree(mm);
	} else {
		unsigned long delay_jiffies =
			msecs_to_jiffies(NULLDISP_MM_DEINIT_DELAY_MSECS);

		DRM_INFO("outstanding allocations exist (delaying clean up)\n");

		__module_get(THIS_MODULE);

		if (!schedule_delayed_work(&mm->delayed_deinit_work,
					   delay_jiffies)) {
			DRM_ERROR("Failed to delay clean up\n");
			module_put(THIS_MODULE);
		}
	}

	display_dev->mm = NULL;
}
#endif /* defined(LMA) */


static void display_buffer_destroy(struct kref *kref)
{
	struct pvr_drm_display_buffer *buffer =
		container_of(kref, struct pvr_drm_display_buffer, refcount);

#if defined(LMA)
	nulldisp_mm_free(buffer->mem);
#else
	vfree(buffer->mem);
#endif

	kfree(buffer);
}

static inline void display_buffer_ref(struct pvr_drm_display_buffer *buffer)
{
	kref_get(&buffer->refcount);
}

static inline void display_buffer_unref(struct pvr_drm_display_buffer *buffer)
{
	kref_put(&buffer->refcount, display_buffer_destroy);
}


/******************************************************************************
 * CRTC functions
 ******************************************************************************/

static void nulldisp_crtc_helper_dpms(struct drm_crtc *crtc,
				      int mode)
{
	/*
	 * Change the power state of the display/pipe/port/etc. If the mode
	 * passed in is unsupported, the provider must use the next lowest
	 * power level.
	 */
}

static void nulldisp_crtc_helper_prepare(struct drm_crtc *crtc)
{
	/*
	 * Prepare the display/pipe/port/etc for a mode change e.g. put them
	 * in a low power state/turn them off
	 */
}

static void nulldisp_crtc_helper_commit(struct drm_crtc *crtc)
{
	/* Turn the display/pipe/port/etc back on */
}

static bool
nulldisp_crtc_helper_mode_fixup(struct drm_crtc *crtc,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
				struct drm_display_mode *mode,
#else
				const struct drm_display_mode *mode,
#endif
				struct drm_display_mode *adjusted_mode)
{
	/*
	 * Fix up mode so that it's compatible with the hardware. The results
	 * should be stored in adjusted_mode (i.e. mode should be untouched).
	 */
	return true;
}

static int
nulldisp_crtc_helper_mode_set_base_atomic(struct drm_crtc *crtc,
					  struct drm_framebuffer *fb,
					  int x, int y,
					  enum mode_set_atomic atomic)
{
	/* Set the display base address or offset from the base address */
	return 0;
}

static int nulldisp_crtc_helper_mode_set_base(struct drm_crtc *crtc,
					      int x, int y,
					      struct drm_framebuffer *old_fb)
{
	return nulldisp_crtc_helper_mode_set_base_atomic(crtc,
							 NULLDISP_CRTC_FB(crtc),
							 x,
							 y,
							 0);
}

static int
nulldisp_crtc_helper_mode_set(struct drm_crtc *crtc,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode,
			      int x, int y,
			      struct drm_framebuffer *old_fb)
{
	/* Setup the the new mode and/or framebuffer */
	return nulldisp_crtc_helper_mode_set_base(crtc, x, y, old_fb);
}

static void nulldisp_crtc_helper_load_lut(struct drm_crtc *crtc)
{
}

static void nulldisp_crtc_destroy(struct drm_crtc *crtc)
{
	struct nulldisp_crtc *nulldisp_crtc = to_nulldisp_crtc(crtc);

	DRM_DEBUG_DRIVER("[CRTC:%d]\n", crtc->base.id);

	drm_crtc_cleanup(crtc);
	kfree(nulldisp_crtc);
}

static int nulldisp_crtc_set_config(struct drm_mode_set *mode_set)
{
	return drm_crtc_helper_set_config(mode_set);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)) && \
	!defined(CHROMIUMOS_WORKAROUNDS_KERNEL310)
static int nulldisp_crtc_page_flip(struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   struct drm_pending_vblank_event *event)
#else
static int nulldisp_crtc_page_flip(struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   struct drm_pending_vblank_event *event,
				   uint32_t page_flip_flags)
#endif
{
	/* Set the crtc to point to the new framebuffer */
	NULLDISP_CRTC_FB(crtc) = fb;

	if (event) {
		/* There are no interrupts so just set these values to 0 */
		event->event.sequence = 0;
		event->event.tv_sec = 0;
		event->event.tv_usec = 0;

		list_add_tail(&event->base.link,
			      &event->base.file_priv->event_list);
		wake_up_interruptible(&event->base.file_priv->event_wait);
	}

	return 0;
}

static const struct drm_crtc_helper_funcs nulldisp_crtc_helper_funcs = {
	.dpms = nulldisp_crtc_helper_dpms,
	.prepare = nulldisp_crtc_helper_prepare,
	.commit = nulldisp_crtc_helper_commit,
	.mode_fixup = nulldisp_crtc_helper_mode_fixup,
	.mode_set = nulldisp_crtc_helper_mode_set,
	.mode_set_base = nulldisp_crtc_helper_mode_set_base,
	.load_lut = nulldisp_crtc_helper_load_lut,
	.mode_set_base_atomic = nulldisp_crtc_helper_mode_set_base_atomic,
	.disable = NULL,
};

static const struct drm_crtc_funcs nulldisp_crtc_funcs = {
	.save = NULL,
	.restore = NULL,
	.reset = NULL,
	.cursor_set = NULL,
	.cursor_move = NULL,
	.gamma_set = NULL,
	.destroy = nulldisp_crtc_destroy,
	.set_config = nulldisp_crtc_set_config,
	.page_flip = nulldisp_crtc_page_flip,
};

static struct nulldisp_crtc *
nulldisp_crtc_create(struct nulldisp_display_device *display_dev)
{
	struct nulldisp_crtc *nulldisp_crtc;

	nulldisp_crtc = kzalloc(sizeof(*nulldisp_crtc), GFP_KERNEL);
	if (!nulldisp_crtc)
		return NULL;

	drm_crtc_init(display_dev->dev,
		      &nulldisp_crtc->base,
		      &nulldisp_crtc_funcs);
	drm_crtc_helper_add(&nulldisp_crtc->base,
			    &nulldisp_crtc_helper_funcs);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	if (display_dev->mem_layout_prop) {
		int value = FB_MEMLAYOUT_STRIDED;
		int i;

		for (i = 0; i < ARRAY_SIZE(nulldisp_mem_layout_enum_list); i++) {
			if (strncmp(nulldisp_mem_layout_enum_list[i].name,
				    param_mem_layout,
				    PARAM_STRING_LEN) == 0) {
				DRM_INFO("set default mem_layout to '%s'\n",
					 param_mem_layout);
				value = nulldisp_mem_layout_enum_list[i].type;
				break;
			}
		}

		if (i == ARRAY_SIZE(nulldisp_mem_layout_enum_list))
			DRM_INFO("mem_layout unrecognised value '%s'\n",
				 param_mem_layout);

		drm_object_attach_property(&nulldisp_crtc->base.base,
					   display_dev->mem_layout_prop,
					   value);
	}

	if (display_dev->fbc_format_prop) {
		int value = FB_COMPRESSION_NONE;
		int i;

		for (i = 0; i < ARRAY_SIZE(nulldisp_fbc_format_enum_list); i++) {
			if (strncmp(nulldisp_fbc_format_enum_list[i].name,
				    param_fbc_format,
				    PARAM_STRING_LEN) == 0) {
				DRM_INFO("set default fbc_format to '%s'\n",
					 param_fbc_format);
				value = nulldisp_fbc_format_enum_list[i].type;
				break;
			}
		}

		if (i == ARRAY_SIZE(nulldisp_fbc_format_enum_list))
			DRM_INFO("fbc_format unrecognised value '%s'\n",
				 param_fbc_format);

		drm_object_attach_property(&nulldisp_crtc->base.base,
					   display_dev->fbc_format_prop,
					   value);
	}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) */

	nulldisp_crtc->display_dev = display_dev;

	DRM_DEBUG_DRIVER("[CRTC:%d]\n", nulldisp_crtc->base.base.id);

	return nulldisp_crtc;
}


/******************************************************************************
 * Connector functions
 ******************************************************************************/

static int
nulldisp_connector_helper_get_modes(struct drm_connector *connector)
{
	/*
	 * Gather modes. Here we can get the EDID data from the monitor and
	 * turn it into drm_display_mode structures.
	 */
	return 0;
}

static int
nulldisp_connector_helper_mode_valid(struct drm_connector *connector,
				     struct drm_display_mode *mode)
{
	/*
	 * This function is called on each gathered mode (e.g. via EDID)
	 * and gives the driver a chance to reject it if the hardware
	 * cannot support it.
	 */
	return MODE_OK;
}

static struct drm_encoder *
nulldisp_connector_helper_best_encoder(struct drm_connector *connector)
{
	/* Pick the first encoder we find */
	if (connector->encoder_ids[0] != 0) {
		struct drm_mode_object *mode_object;

		mode_object = drm_mode_object_find(connector->dev,
						   connector->encoder_ids[0],
						   DRM_MODE_OBJECT_ENCODER);
		if (mode_object) {
			struct drm_encoder *encoder =
				obj_to_encoder(mode_object);

			DRM_DEBUG_DRIVER("[ENCODER:%d:%s] best for "
					 "[CONNECTOR:%d:%s]\n",
					 encoder->base.id,
					 encoder_name(encoder),
					 connector->base.id,
					 connector_name(connector));
			return encoder;
		}
	}

	return NULL;
}

static enum drm_connector_status
nulldisp_connector_detect(struct drm_connector *connector,
			  bool force)
{
	/* Return whether or not a monitor is attached to the connector */
	return connector_status_connected;
}

static int
nulldisp_connector_set_property(struct drm_connector *connector,
				struct drm_property *property,
				uint64_t value)
{
	return -ENOSYS;
}

static void nulldisp_connector_destroy(struct drm_connector *connector)
{
	struct nulldisp_connector *nulldisp_connector =
		to_nulldisp_connector(connector);

	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s]\n",
			 connector->base.id,
			 connector_name(connector));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	drm_connector_unregister(connector);
#endif
	drm_mode_connector_update_edid_property(connector, NULL);
	drm_connector_cleanup(connector);

	kfree(nulldisp_connector);
}

static void nulldisp_connector_force(struct drm_connector *connector)
{
}

static const struct drm_connector_helper_funcs
nulldisp_connector_helper_funcs = {
	.get_modes = nulldisp_connector_helper_get_modes,
	.mode_valid = nulldisp_connector_helper_mode_valid,
	.best_encoder = nulldisp_connector_helper_best_encoder,
};

static const struct drm_connector_funcs nulldisp_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = NULL,
	.restore = NULL,
	.reset = NULL,
	.detect = nulldisp_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nulldisp_connector_set_property,
	.destroy = nulldisp_connector_destroy,
	.force = nulldisp_connector_force,
};

static struct nulldisp_connector *
nulldisp_connector_create(struct nulldisp_display_device *display_dev,
			  int type)
{
	struct nulldisp_connector *nulldisp_connector;

	nulldisp_connector = kzalloc(sizeof(*nulldisp_connector), GFP_KERNEL);
	if (!nulldisp_connector)
		return NULL;

	drm_connector_init(display_dev->dev,
			   &nulldisp_connector->base,
			   &nulldisp_connector_funcs,
			   type);
	drm_connector_helper_add(&nulldisp_connector->base,
				 &nulldisp_connector_helper_funcs);

	nulldisp_connector->display_dev = display_dev;
	nulldisp_connector->base.dpms = DRM_MODE_DPMS_OFF;
	nulldisp_connector->base.interlace_allowed = false;
	nulldisp_connector->base.doublescan_allowed = false;
	nulldisp_connector->base.display_info.subpixel_order = SubPixelUnknown;

	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s]\n",
			 nulldisp_connector->base.base.id,
			 connector_name(&nulldisp_connector->base));

	return nulldisp_connector;
}


/******************************************************************************
 * Encoder functions
 ******************************************************************************/

static void nulldisp_encoder_helper_dpms(struct drm_encoder *encoder,
					 int mode)
{
	/*
	 * Set the display power state or active encoder based on the mode. If
	 * the mode passed in is unsupported, the provider must use the next
	 * lowest power level.
	 */
}

static bool
nulldisp_encoder_helper_mode_fixup(struct drm_encoder *encoder,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
				   struct drm_display_mode *mode,
#else
				   const struct drm_display_mode *mode,
#endif
				   struct drm_display_mode *adjusted_mode)
{
	/*
	 * Fix up mode so that it's compatible with the hardware. The results
	 * should be stored in adjusted_mode (i.e. mode should be untouched).
	 */
	return true;
}

static void nulldisp_encoder_helper_prepare(struct drm_encoder *encoder)
{
	/*
	 * Prepare the encoder for a mode change e.g. set the active encoder
	 * accordingly/turn the encoder off
	 */
}

static void nulldisp_encoder_helper_commit(struct drm_encoder *encoder)
{
	/* Turn the encoder back on/set the active encoder */
}

static void
nulldisp_encoder_helper_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	/* Setup the encoder for the new mode */
}

static void nulldisp_encoder_destroy(struct drm_encoder *encoder)
{
	struct nulldisp_encoder *nulldisp_encoder =
		to_nulldisp_encoder(encoder);

	DRM_DEBUG_DRIVER("[ENCODER:%d:%s]\n",
			 encoder->base.id,
			 encoder_name(encoder));

	drm_encoder_cleanup(encoder);
	kfree(nulldisp_encoder);
}

static const struct drm_encoder_helper_funcs nulldisp_encoder_helper_funcs = {
	.dpms = nulldisp_encoder_helper_dpms,
	.save = NULL,
	.restore = NULL,
	.mode_fixup = nulldisp_encoder_helper_mode_fixup,
	.prepare = nulldisp_encoder_helper_prepare,
	.commit = nulldisp_encoder_helper_commit,
	.mode_set = nulldisp_encoder_helper_mode_set,
	.get_crtc = NULL,
	.detect = NULL,
	.disable = NULL,
};

static const struct drm_encoder_funcs nulldisp_encoder_funcs = {
	.reset = NULL,
	.destroy = nulldisp_encoder_destroy,
};

static struct nulldisp_encoder *
nulldisp_encoder_create(struct nulldisp_display_device *display_dev,
			int type)
{
	struct nulldisp_encoder *nulldisp_encoder;

	nulldisp_encoder = kzalloc(sizeof(*nulldisp_encoder), GFP_KERNEL);
	if (!nulldisp_encoder)
		return NULL;

	drm_encoder_init(display_dev->dev,
			 &nulldisp_encoder->base,
			 &nulldisp_encoder_funcs,
			 type);
	drm_encoder_helper_add(&nulldisp_encoder->base,
			       &nulldisp_encoder_helper_funcs);

	nulldisp_encoder->display_dev = display_dev;

	/*
	 * This is a bit field that's used to determine which
	 * CRTCs can drive this encoder.
	 */
	nulldisp_encoder->base.possible_crtcs = 0x1;

	DRM_DEBUG_DRIVER("[ENCODER:%d:%s]\n",
			 nulldisp_encoder->base.base.id,
			 encoder_name(&nulldisp_encoder->base));

	return nulldisp_encoder;
}


/******************************************************************************
 * Framebuffer functions
 ******************************************************************************/

static void nulldisp_framebuffer_destroy(struct drm_framebuffer *framebuffer)
{
	struct nulldisp_framebuffer *nulldisp_framebuffer =
		to_nulldisp_framebuffer(framebuffer);

	DRM_DEBUG_DRIVER("[FB:%d]\n", framebuffer->base.id);

	drm_framebuffer_cleanup(framebuffer);

	drm_gem_object_unreference_unlocked(nulldisp_framebuffer->obj);

	kfree(nulldisp_framebuffer);
}

static int
nulldisp_framebuffer_create_handle(struct drm_framebuffer *framebuffer,
				   struct drm_file *file_priv,
				   unsigned int *handle)
{
	struct nulldisp_framebuffer *nulldisp_framebuffer =
		to_nulldisp_framebuffer(framebuffer);

	DRM_DEBUG_DRIVER("[FB:%d]\n", framebuffer->base.id);

	return drm_gem_handle_create(file_priv,
				     nulldisp_framebuffer->obj,
				     handle);
}

static int
nulldisp_framebuffer_dirty(struct drm_framebuffer *framebuffer,
			   struct drm_file *file_priv,
			   unsigned flags,
			   unsigned color,
			   struct drm_clip_rect *clips,
			   unsigned num_clips)
{
	return -ENOSYS;
}

static const struct drm_framebuffer_funcs nulldisp_framebuffer_funcs = {
	.destroy = nulldisp_framebuffer_destroy,
	.create_handle = nulldisp_framebuffer_create_handle,
	.dirty = nulldisp_framebuffer_dirty,
};

static int
nulldisp_framebuffer_init(struct drm_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
			  struct drm_mode_fb_cmd *mode_cmd,
#else
			  struct drm_mode_fb_cmd2 *mode_cmd,
#endif
			  struct nulldisp_framebuffer *nulldisp_framebuffer,
			  struct drm_gem_object *obj)
{
	int err;

	err = drm_framebuffer_init(dev,
				   &nulldisp_framebuffer->base,
				   &nulldisp_framebuffer_funcs);
	if (err) {
		DRM_ERROR("failed to initialise framebuffer structure (%d)\n",
			  err);
		return err;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	drm_helper_mode_fill_fb_struct(&nulldisp_framebuffer->base, mode_cmd);
#else
	err = drm_helper_mode_fill_fb_struct(&nulldisp_framebuffer->base,
					     mode_cmd);
	if (err) {
		DRM_ERROR("failed to fill in framebuffer mode info (%d)\n",
			  err);
		return err;
	}
#endif

	nulldisp_framebuffer->obj = obj;

	DRM_DEBUG_DRIVER("[FB:%d]\n", nulldisp_framebuffer->base.base.id);

	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
static struct drm_framebuffer *
nulldisp_fb_create(struct drm_device *dev,
		   struct drm_file *file_priv,
		   struct drm_mode_fb_cmd *mode_cmd)
#else
static struct drm_framebuffer *
nulldisp_fb_create(struct drm_device *dev,
		   struct drm_file *file_priv,
		   struct drm_mode_fb_cmd2 *mode_cmd)
#endif
{
	struct drm_gem_object *obj;
	struct nulldisp_framebuffer *nulldisp_framebuffer;
	__u32 handle;
	int err;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
	handle = mode_cmd->handle;
#else
	handle = mode_cmd->handles[0];
#endif

	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to find buffer with handle %u\n",
			  handle);
		return ERR_PTR(-ENOENT);
	}

	nulldisp_framebuffer = kzalloc(sizeof(*nulldisp_framebuffer),
				       GFP_KERNEL);
	if (!nulldisp_framebuffer) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	err = nulldisp_framebuffer_init(dev,
					mode_cmd,
					nulldisp_framebuffer,
					obj);
	if (err) {
		kfree(nulldisp_framebuffer);
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(err);
	}

	DRM_DEBUG_DRIVER("[FB:%d]\n", nulldisp_framebuffer->base.base.id);

	return &nulldisp_framebuffer->base;
}

static const struct drm_mode_config_funcs nulldisp_mode_config_funcs = {
	.fb_create = nulldisp_fb_create,
	.output_poll_changed = NULL,
};


int drm_nulldisp_init(struct drm_device *dev, void **display_priv_out)
{
	struct nulldisp_display_device *display_dev;
#if defined(LMA)
	int err;
#endif

	if (!dev || !display_priv_out)
		return -EINVAL;

	display_dev = kzalloc(sizeof(*display_dev), GFP_KERNEL);
	if (!display_dev)
		return -ENOMEM;

	display_dev->dev = dev;

#if defined(LMA)
	err = nulldisp_mm_init(display_dev);
	if (err) {
		kfree(display_dev);
		return err;
	}
#endif

	*display_priv_out = display_dev;

	return 0;
}
EXPORT_SYMBOL(drm_nulldisp_init);

int drm_nulldisp_configure(void *display_priv)
{
	struct nulldisp_display_device *display_dev =
		(struct nulldisp_display_device *)display_priv;
	struct drm_device *dev;
	struct nulldisp_crtc *nulldisp_crtc;
	struct nulldisp_connector *nulldisp_connector;
	struct nulldisp_encoder *nulldisp_encoder;
	int err;

	if (!display_dev)
		return -EINVAL;

	dev = display_dev->dev;

	drm_mode_config_init(dev);

	dev->mode_config.funcs = (void *)&nulldisp_mode_config_funcs;
	dev->mode_config.min_width = NULLDISP_FB_WIDTH_MIN;
	dev->mode_config.max_width = NULLDISP_FB_WIDTH_MAX;
	dev->mode_config.min_height = NULLDISP_FB_HEIGHT_MIN;
	dev->mode_config.max_height = NULLDISP_FB_HEIGHT_MAX;
	dev->mode_config.fb_base = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
	dev->mode_config.async_page_flip = true;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	display_dev->mem_layout_prop =
		drm_property_create_enum(dev,
					 DRM_MODE_PROP_IMMUTABLE,
					 "mem_layout",
					 nulldisp_mem_layout_enum_list,
					 ARRAY_SIZE(nulldisp_mem_layout_enum_list));
	if (!display_dev->mem_layout_prop) {
		DRM_ERROR("failed to create memory layout property.\n");
		err = -ENOMEM;
		goto err_config_cleanup;
	}

	display_dev->fbc_format_prop =
		drm_property_create_enum(dev,
					 DRM_MODE_PROP_IMMUTABLE,
					 "fbc_format",
					 nulldisp_fbc_format_enum_list,
					 ARRAY_SIZE(nulldisp_fbc_format_enum_list));
	if (!display_dev->fbc_format_prop) {
		DRM_ERROR("failed to create FBC format property.\n");

		err = -ENOMEM;
		goto err_config_cleanup;
	}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) */

	nulldisp_crtc = nulldisp_crtc_create(display_dev);
	if (!nulldisp_crtc) {
		DRM_ERROR("failed to create a CRTC.\n");

		err = -ENOMEM;
		goto err_config_cleanup;
	}

	nulldisp_connector =
		nulldisp_connector_create(display_dev,
					  DRM_MODE_CONNECTOR_Unknown);
	if (!nulldisp_connector) {
		DRM_ERROR("failed to create a connector.\n");

		err = -ENOMEM;
		goto err_config_cleanup;
	}

	nulldisp_encoder = nulldisp_encoder_create(display_dev,
						   DRM_MODE_ENCODER_NONE);
	if (!nulldisp_encoder) {
		DRM_ERROR("failed to create an encoder.\n");

		err = -ENOMEM;
		goto err_config_cleanup;
	}

	err = drm_mode_connector_attach_encoder(&nulldisp_connector->base,
						&nulldisp_encoder->base);
	if (err) {
		DRM_ERROR("failed to attach [ENCODER:%d:%s] to "
			  "[CONNECTOR:%d:%s] (err=%d)\n",
			  nulldisp_encoder->base.base.id,
			  encoder_name(&nulldisp_encoder->base),
			  nulldisp_connector->base.base.id,
			  connector_name(&nulldisp_connector->base),
			  err);
		goto err_config_cleanup;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	err = drm_connector_register(&nulldisp_connector->base);
	if (err) {
		DRM_ERROR("[CONNECTOR:%d:%s] failed to register (err=%d)\n",
			  nulldisp_connector->base.base.id,
			  connector_name(&nulldisp_connector->base),
			  err);
		goto err_config_cleanup;
	}
#endif

	return 0;

err_config_cleanup:
	drm_mode_config_cleanup(dev);

	return err;
}
EXPORT_SYMBOL(drm_nulldisp_configure);

void drm_nulldisp_cleanup(void *display_priv)
{
	struct nulldisp_display_device *display_dev =
		(struct nulldisp_display_device *)display_priv;

	if (display_dev) {
		drm_mode_config_cleanup(display_dev->dev);

#if defined(LMA)
		nulldisp_mm_deinit(display_dev);
#endif

		kfree(display_dev);
	}
}
EXPORT_SYMBOL(drm_nulldisp_cleanup);

int drm_nulldisp_buffer_alloc(void *display_priv,
			      size_t size,
			      struct pvr_drm_display_buffer **buffer_out)
{
#if defined(LMA)
	struct nulldisp_display_device *display_dev =
		(struct nulldisp_display_device *)display_priv;
#endif
	struct pvr_drm_display_buffer *buffer;

	if (!display_priv || size == 0 || !buffer_out)
		return -EINVAL;

#if defined(LMA)
	if (!display_dev->mm) {
		DRM_ERROR("memory manager not initialised\n");
		return -EINVAL;
	}
#endif

	buffer = kmalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

#if defined(LMA)
	buffer->mem = nulldisp_mm_alloc(display_dev->mm, size);
#else
	buffer->mem = __vmalloc(size,
				GFP_KERNEL | __GFP_HIGHMEM,
				pgprot_noncached(PAGE_KERNEL));
#endif
	if (!buffer->mem) {
		kfree(buffer);
		return -ENOMEM;
	}

	kref_init(&buffer->refcount);
	buffer->size = size;

	*buffer_out = buffer;

	return 0;
}
EXPORT_SYMBOL(drm_nulldisp_buffer_alloc);

int drm_nulldisp_buffer_free(struct pvr_drm_display_buffer *buffer)
{
	if (!buffer)
		return -EINVAL;

	display_buffer_unref(buffer);

	return 0;
}
EXPORT_SYMBOL(drm_nulldisp_buffer_free);

uint64_t *drm_nulldisp_buffer_acquire(struct pvr_drm_display_buffer *buffer)
{
	uint64_t *addr_array;
	size_t page_count;
	size_t i;
#if defined(LMA)
	uint64_t addr;
	int err;
#else
	void *addr;
#endif

	if (!buffer)
		return ERR_PTR(-EINVAL);

	display_buffer_ref(buffer);

#if defined(LMA)
	err = nulldisp_mm_alloc_dev_phys_addr(buffer->mem, &addr);
	if (err) {
		display_buffer_unref(buffer);
		return ERR_PTR(err);
	}
#else
	addr = buffer->mem;
#endif

	page_count = buffer->size >> PAGE_SHIFT;

	addr_array = kmalloc_array(page_count, sizeof(*addr_array), GFP_KERNEL);
	if (!addr_array) {
		display_buffer_unref(buffer);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < page_count; i++, addr += PAGE_SIZE) {
#if defined(LMA)
		addr_array[i] = addr;
#else
		struct page *page = vmalloc_to_page(addr);

		addr_array[i] = (uint64_t)(uintptr_t)page_to_phys(page);
#endif
	}

	return addr_array;
}
EXPORT_SYMBOL(drm_nulldisp_buffer_acquire);

int drm_nulldisp_buffer_release(struct pvr_drm_display_buffer *buffer,
				uint64_t *dev_paddr_array)
{
	if (!buffer || !dev_paddr_array)
		return -EINVAL;

	display_buffer_unref(buffer);

	kfree(dev_paddr_array);

	return 0;
}
EXPORT_SYMBOL(drm_nulldisp_buffer_release);

void *drm_nulldisp_buffer_vmap(struct pvr_drm_display_buffer *buffer)
{
#if defined(LMA)
	uint64_t addr;
	int err;
#endif

	if (!buffer)
		return NULL;

	display_buffer_ref(buffer);

#if defined(LMA)
	err = nulldisp_mm_alloc_cpu_phys_addr(buffer->mem, &addr);
	if (err) {
		DRM_ERROR("failed to get CPU physical address\n");

		display_buffer_ref(buffer);
		return NULL;
	}

	return ioremap_wc(addr, buffer->size);
#else
	return buffer->mem;
#endif
}
EXPORT_SYMBOL(drm_nulldisp_buffer_vmap);

void drm_nulldisp_buffer_vunmap(struct pvr_drm_display_buffer *buffer,
				void *vaddr)
{
	if (buffer)
		display_buffer_unref(buffer);

#if defined(LMA)
	if (vaddr)
		iounmap(vaddr);
#endif
}
EXPORT_SYMBOL(drm_nulldisp_buffer_vunmap);

u32 drm_nulldisp_get_vblank_counter(void *display_priv, int crtc)
{
	struct nulldisp_display_device *display_dev =
		(struct nulldisp_display_device *)display_priv;

	if (!display_dev)
		return 0;

	return drm_vblank_count(display_dev->dev, crtc);
}
EXPORT_SYMBOL(drm_nulldisp_get_vblank_counter);

int drm_nulldisp_enable_vblank(void *display_priv, int crtc)
{
	if (!display_priv)
		return -EINVAL;

	switch (crtc) {
	case 0:
		break;
	default:
		DRM_ERROR("invalid crtc %d\n", crtc);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(drm_nulldisp_enable_vblank);

int drm_nulldisp_disable_vblank(void *display_priv, int crtc)
{
	if (!display_priv)
		return -EINVAL;

	switch (crtc) {
	case 0:
		break;
	default:
		DRM_ERROR("invalid crtc %d\n", crtc);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(drm_nulldisp_disable_vblank);

static int __init nulldisp_init(void)
{
	return 0;
}

static void __exit nulldisp_exit(void)
{
}

module_init(nulldisp_init);
module_exit(nulldisp_exit);

MODULE_AUTHOR("Imagination Technologies Ltd. <gpl-support@imgtec.com>");
MODULE_LICENSE("Dual MIT/GPL");
