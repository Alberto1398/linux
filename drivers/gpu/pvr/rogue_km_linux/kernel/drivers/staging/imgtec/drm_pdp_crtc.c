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

#include <linux/version.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "drm_pdp.h"
#include "tcf_rgbpdp_regs.h"
#include "tcf_pll.h"

#define PDP_STRIDE_SHIFT 4
#define PDP_BASE_ADDR_SHIFT 4

#define REG_VALUE_GET(v, s, m) \
	(uint32_t)(((v) & (m)) >> (s))
#define REG_VALUE_SET(v, b, s, m) \
	(uint32_t)(((v) & (uint32_t)~(m)) | (uint32_t)(((b) << (s)) & (m)))

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
#define PDP_CRTC_FB(crtc) ((crtc)->fb)
#else
#define PDP_CRTC_FB(crtc) ((crtc)->primary->fb)
#endif

enum pdp_crtc_flip_status {
	PDP_CRTC_FLIP_STATUS_NONE = 0,
	PDP_CRTC_FLIP_STATUS_PENDING,
	PDP_CRTC_FLIP_STATUS_DONE,
};

struct pdp_crtc {
	struct drm_crtc base;
	struct pdp_display_device *display_dev;

	uint32_t number;

	bool enabled;
	bool reenable_after_modeset;

	resource_size_t pdp_reg_size;
	resource_size_t pdp_reg_phys_base;
	void __iomem *pdp_reg;

	resource_size_t pll_reg_size;
	resource_size_t pll_reg_phys_base;
	void __iomem *pll_reg;

	/* Reuse the drm_device event_lock to protect these */
	enum pdp_crtc_flip_status flip_status;
	struct drm_pending_vblank_event *flip_event;
	struct drm_framebuffer *old_fb;
	struct pvr_drm_flip_data *flip_data_active;
	struct pvr_drm_flip_data *flip_data_done;
	bool flip_async;
};

#define to_pdp_crtc(crtc) container_of(crtc, struct pdp_crtc, base)


static inline uint32_t pdp_rreg32(void __iomem *base, uint32_t reg)
{
	return ioread32(base + reg);
}

static inline void pdp_wreg32(void __iomem *base, uint32_t reg, uint32_t value)
{
	iowrite32(value, base + reg);
}

static void pdp_clocks_set(struct drm_crtc *crtc,
			   struct drm_display_mode *adjusted_mode)
{
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	uint32_t clockInMHz;
	uint32_t clock;

	/* Calculate the clock in MHz */
	clockInMHz = adjusted_mode->clock / 1000;

	/*
	 * Setup TCF_CR_PLL_PDP_CLK1TO5 based on the main clock speed
	 * (clock 0 or 3)
	 */
	clock = (clockInMHz >= 50) ? 0 : 0x3;

	/* Set phase 0, ratio 50:50 and frequency in MHz */
	pdp_wreg32(pdp_crtc->pll_reg, TCF_PLL_PLL_PDP_CLK0, clockInMHz);

	pdp_wreg32(pdp_crtc->pll_reg, TCF_PLL_PLL_PDP_CLK1TO5, clock);

	/* Now initiate reprogramming of the PLLs */
	pdp_wreg32(pdp_crtc->pll_reg, TCF_PLL_PLL_PDP_DRP_GO, 0x1);
	udelay(1000);
	pdp_wreg32(pdp_crtc->pll_reg, TCF_PLL_PLL_PDP_DRP_GO, 0x0);

	DRM_DEBUG_DRIVER("set clock to %uMhz\n", clockInMHz);
}


static void pdp_crtc_helper_dpms(struct drm_crtc *crtc, int mode)
{
}

static void pdp_crtc_helper_prepare(struct drm_crtc *crtc)
{
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	uint32_t value;

	drm_vblank_pre_modeset(crtc->dev, pdp_crtc->number);

	/* Store the current enable state */
	pdp_crtc->reenable_after_modeset = pdp_crtc->enabled;

	/* Turn off memory requests */
	pdp_crtc_set_plane_enabled(crtc, false);

	/* Disable sync gen */
	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL);
	value = REG_VALUE_SET(value, 0x0, SYNCACTIVE_SHIFT, SYNCACTIVE_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg,
		   TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL,
		   value);
}

static void pdp_crtc_helper_commit(struct drm_crtc *crtc)
{
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	uint32_t value;

	/* Enable sync gen */
	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL);
	value = REG_VALUE_SET(value, 0x1, SYNCACTIVE_SHIFT, SYNCACTIVE_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg,
		   TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL,
		   value);

	/* Turn on memory requests if necessary */
	if (pdp_crtc->reenable_after_modeset)
		pdp_crtc_set_plane_enabled(crtc, true);

	drm_vblank_post_modeset(crtc->dev, pdp_crtc->number);
}

static bool pdp_crtc_helper_mode_fixup(struct drm_crtc *crtc,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
				       struct drm_display_mode *mode,
#else
				       const struct drm_display_mode *mode,
#endif
				       struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int pdp_crtc_helper_mode_set_base_atomic(struct drm_crtc *crtc,
						struct drm_framebuffer *fb,
						int x, int y,
						enum mode_set_atomic atomic)
{
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	struct pdp_framebuffer *pdp_fb = to_pdp_framebuffer(fb);
	uint32_t address;
	uint32_t value;
	unsigned int pitch;

	if (pdp_fb->dev_addr == 0)
		return -EINVAL;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
	pitch = fb->pitch;
#else
	pitch = fb->pitches[0];
#endif

	/*
	 * NOTE: If the buffer dimensions are less than the current mode then
	 * the output will appear in the top left of the screen. This can be
	 * centred by adjusting horizontal active start, right border start,
	 * vertical active start and bottom border start. At this point it's
	 * not entirely clear where this should be done. On the one hand it's
	 * related to pdp_crtc_helper_mode_set but on the other hand there
	 * might not always be a call to pdp_crtc_helper_mode_set. This needs
	 * to be investigated.
	 */

	/*
	 * Set the framebuffer size (this might be smaller than the
	 * resolution)
	 */
	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_STR1SURF);
	value = REG_VALUE_SET(value,
			      fb->width - 1,
			      STR1WIDTH_SHIFT,
			      STR1WIDTH_MASK);
	value = REG_VALUE_SET(value,
			      fb->height - 1,
			      STR1HEIGHT_SHIFT,
			      STR1HEIGHT_MASK);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
	if (fb->bits_per_pixel == 32 && fb->depth == 24)
		value = REG_VALUE_SET(value,
				      0xE,
				      STR1PIXFMT_SHIFT,
				      STR1PIXFMT_MASK);
	else
		DRM_ERROR("unsupported pixel format (bpp = %d depth = %u)\n",
			  fb->bits_per_pixel, fb->depth);
#else
	/* Set the framebuffer pixel format */
	switch (fb->pixel_format) {
	case DRM_FORMAT_XRGB8888:
		/*
		 * The documentation claims that 0xE is ARGB8888 but alpha
		 * is actually ignored so it's really XRGB8888
		 */
		value = REG_VALUE_SET(value,
				      0xE,
				      STR1PIXFMT_SHIFT,
				      STR1PIXFMT_MASK);
		break;
	default:
		DRM_ERROR("unsupported pixel format (format = %d)\n",
			  fb->pixel_format);
	}
#endif
	pdp_wreg32(pdp_crtc->pdp_reg,
		   TCF_RGBPDP_PVR_TCF_RGBPDP_STR1SURF,
		   value);

	/* Set the framebuffer stride, which is number of 16byte words */
	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_PDP_STR1POSN);
	value = REG_VALUE_SET(value,
			      (pitch >> PDP_STRIDE_SHIFT) - 1,
			      STR1STRIDE_SHIFT,
			      STR1STRIDE_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_PDP_STR1POSN, value);

	/*
	 * Set the base address to the buffer address and disable
	 * interlaced output
	 */
	address  = pdp_fb->dev_addr;
	address += ((y * pitch) + (x * (fb->bits_per_pixel / 8)));

	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL);
	value = REG_VALUE_SET(value,
			      0x0,
			      STR1INTFIELD_SHIFT,
			      STR1INTFIELD_MASK);
	value = REG_VALUE_SET(value,
			      address >> PDP_BASE_ADDR_SHIFT,
			      STR1BASE_SHIFT,
			      STR1BASE_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg,
		   TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL,
		   value);

	return 0;
}

static int pdp_crtc_helper_mode_set_base(struct drm_crtc *crtc,
					 int x, int y,
					 struct drm_framebuffer *old_fb)
{
	if (!PDP_CRTC_FB(crtc))	{
		DRM_ERROR("no framebuffer\n");
		return 0;
	}

	return pdp_crtc_helper_mode_set_base_atomic(crtc,
						    PDP_CRTC_FB(crtc),
						    x, y,
						    0);
}

static int pdp_crtc_helper_mode_set(struct drm_crtc *crtc,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode,
				    int x, int y,
				    struct drm_framebuffer *old_fb)
{
	/*
	 * ht   = horizontal total
	 * hbps = horizontal back porch start
	 * has  = horizontal active start
	 * hlbs = horizontal left border start
	 * hfps = horizontal front porch start
	 * hrbs = horizontal right border start
	 *
	 * vt   = vertical total
	 * vbps = vertical back porch start
	 * vas  = vertical active start
	 * vtbs = vertical top border start
	 * vfps = vertical front porch start
	 * vbbs = vertical bottom border start
	 */
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	uint32_t ht = adjusted_mode->htotal;
	uint32_t hbps = adjusted_mode->hsync_end - adjusted_mode->hsync_start;
	uint32_t has = (adjusted_mode->htotal - adjusted_mode->hsync_start);
	uint32_t hlbs = has;
	uint32_t hfps = (hlbs + adjusted_mode->hdisplay);
	uint32_t hrbs = hfps;
	uint32_t vt = adjusted_mode->vtotal;
	uint32_t vbps = adjusted_mode->vsync_end - adjusted_mode->vsync_start;
	uint32_t vas = (adjusted_mode->vtotal - adjusted_mode->vsync_start);
	uint32_t vtbs = vas;
	uint32_t vfps = (vtbs + adjusted_mode->vdisplay);
	uint32_t vbbs = vfps;
	uint32_t value;

	DRM_DEBUG_DRIVER("setting mode to %dx%d\n",
			 adjusted_mode->hdisplay, adjusted_mode->vdisplay);

	pdp_clocks_set(crtc, adjusted_mode);

	/* Border colour */
	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_BORDCOL);
	value = REG_VALUE_SET(value, 0x0, BORDCOL_SHIFT, BORDCOL_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_BORDCOL, value);

	/* Update control */
	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_UPDCTRL);
	value = REG_VALUE_SET(value, 0x0, UPDCTRL_SHIFT, UPDCTRL_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_UPDCTRL, value);

	/* Set hsync timings */
	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC1);
	value = REG_VALUE_SET(value, hbps, HBPS_SHIFT, HBPS_MASK);
	value = REG_VALUE_SET(value, ht, HT_SHIFT, HT_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC1, value);

	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC2);
	value = REG_VALUE_SET(value, has, HAS_SHIFT, HAS_MASK);
	value = REG_VALUE_SET(value, hlbs, HLBS_SHIFT, HLBS_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC2, value);

	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC3);
	value = REG_VALUE_SET(value, hfps, HFPS_SHIFT, HFPS_MASK);
	value = REG_VALUE_SET(value, hrbs, HRBS_SHIFT, HRBS_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC3, value);


	/* Set vsync timings */
	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC1);
	value = REG_VALUE_SET(value, vbps, VBPS_SHIFT, VBPS_MASK);
	value = REG_VALUE_SET(value, vt, VT_SHIFT, VT_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC1, value);

	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC2);
	value = REG_VALUE_SET(value, vas, VAS_SHIFT, VAS_MASK);
	value = REG_VALUE_SET(value, vtbs, VTBS_SHIFT, VTBS_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC2, value);

	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC3);
	value = REG_VALUE_SET(value, vfps, VFPS_SHIFT, VFPS_MASK);
	value = REG_VALUE_SET(value, vbbs, VBBS_SHIFT, VBBS_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC3, value);


	/* Horizontal data enable */
	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_HDECTRL);
	value = REG_VALUE_SET(value, hlbs, HDES_SHIFT, HDES_MASK);
	value = REG_VALUE_SET(value, hfps, HDEF_SHIFT, HDEF_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_HDECTRL, value);


	/* Vertical data enable */
	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_VDECTRL);
	value = REG_VALUE_SET(value, vtbs, VDES_SHIFT, VDES_MASK);
	value = REG_VALUE_SET(value, vfps, VDEF_SHIFT, VDEF_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VDECTRL, value);


	/* Vertical event start and vertical fetch start */
	value = pdp_rreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VEVENT);
	value = REG_VALUE_SET(value, vbps, VFETCH_SHIFT, VFETCH_MASK);
	value = REG_VALUE_SET(value, vfps, VEVENT_SHIFT, VEVENT_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_VEVENT, value);


	/* Set up polarities of sync/blank */
	value = REG_VALUE_SET(0, 0x1, BLNKPOL_SHIFT, BLNKPOL_MASK);

	if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
		value = REG_VALUE_SET(value, 0x1, HSPOL_SHIFT, HSPOL_MASK);

	if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
		value = REG_VALUE_SET(value, 0x1, VSPOL_SHIFT, VSPOL_MASK);

	pdp_wreg32(pdp_crtc->pdp_reg,
		   TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL,
		   value);

	return pdp_crtc_helper_mode_set_base(crtc, x, y, old_fb);
}

static void pdp_crtc_helper_load_lut(struct drm_crtc *crtc)
{
}

static void pdp_crtc_destroy(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	struct pdp_display_device *display_dev = pdp_crtc->display_dev;
	unsigned long flags;

	DRM_DEBUG_DRIVER("[CRTC:%d]\n", crtc->base.id);

	/* Turn off memory requests */
	pdp_crtc_set_plane_enabled(crtc, false);

	drm_crtc_cleanup(crtc);

	spin_lock_irqsave(&dev->event_lock, flags);
	if (pdp_crtc->flip_data_active) {
		pvr_drm_flip_done(dev, pdp_crtc->flip_data_active);
		pdp_crtc->flip_data_active = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	iounmap(pdp_crtc->pll_reg);

	iounmap(pdp_crtc->pdp_reg);
	release_mem_region(pdp_crtc->pdp_reg_phys_base, pdp_crtc->pdp_reg_size);

	kfree(pdp_crtc);
	display_dev->crtc = NULL;
}

static void pdp_flip_complete_unlocked(struct drm_device *dev,
				       struct drm_crtc *crtc)
{
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	struct drm_pending_vblank_event *event = pdp_crtc->flip_event;

	/* The flipping process has been completed so reset the flip status */
	pdp_crtc->flip_status = PDP_CRTC_FLIP_STATUS_NONE;

	if (pdp_crtc->flip_data_done)
		pvr_drm_flip_done(dev, pdp_crtc->flip_data_done);

	if (event) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
		struct timeval vblanktime;

		event->event.sequence =
			drm_vblank_count_and_time(dev,
						  pdp_crtc->number,
						  &vblanktime);
		event->event.tv_sec = vblanktime.tv_sec;
		event->event.tv_usec = vblanktime.tv_usec;

		list_add_tail(&event->base.link,
			      &event->base.file_priv->event_list);
		wake_up_interruptible(&event->base.file_priv->event_wait);
#else
		drm_send_vblank_event(dev, pdp_crtc->number, event);
#endif
		pdp_crtc->flip_event = NULL;
	}
}

static void pdp_flip(struct drm_gem_object *bo,
		     void *data,
		     struct pvr_drm_flip_data *flip_data)
{
	struct drm_crtc *crtc = (struct drm_crtc *)data;
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	struct drm_framebuffer *old_fb;
	unsigned long flags;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	WARN_ON(pdp_crtc->flip_status != PDP_CRTC_FLIP_STATUS_PENDING);

	old_fb = pdp_crtc->old_fb;
	pdp_crtc->old_fb = NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	/*
	 * The graphics stream registers latch on vsync. As the vblank period
	 * can be tight, do the flip now
	 */
	(void)pdp_crtc_helper_mode_set_base(crtc, crtc->x, crtc->y, old_fb);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	pdp_crtc->flip_data_done = pdp_crtc->flip_data_active;
	pdp_crtc->flip_data_active = flip_data;

	if (pdp_crtc->flip_async) {
		pdp_flip_complete_unlocked(crtc->dev, crtc);
		pdp_crtc->flip_async = false;
	} else {
		pdp_crtc->flip_status = PDP_CRTC_FLIP_STATUS_DONE;
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)) && \
	!defined(CHROMIUMOS_WORKAROUNDS_KERNEL310)
static int pdp_crtc_page_flip(struct drm_crtc *crtc,
			      struct drm_framebuffer *fb,
			      struct drm_pending_vblank_event *event)
#else
static int pdp_crtc_page_flip(struct drm_crtc *crtc,
			      struct drm_framebuffer *fb,
			      struct drm_pending_vblank_event *event,
			      uint32_t page_flip_flags)
#endif
{
	struct drm_device *dev = crtc->dev;
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	struct pdp_framebuffer *pdp_fb = to_pdp_framebuffer(fb);
	bool flip_async = false;
	unsigned long flags;
	int err;

#if defined(DRM_MODE_PAGE_FLIP_ASYNC)
	if (page_flip_flags & DRM_MODE_PAGE_FLIP_ASYNC)
		flip_async = true;
#endif

	if (!flip_async) {
		err = drm_vblank_get(crtc->dev, pdp_crtc->number);
		if (err)
			return err;
	}

	spin_lock_irqsave(&dev->event_lock, flags);
	if (pdp_crtc->flip_status != PDP_CRTC_FLIP_STATUS_NONE) {
		err = -EBUSY;
		goto err_unlock;
	}

	pdp_crtc->flip_status = PDP_CRTC_FLIP_STATUS_PENDING;
	pdp_crtc->flip_event = event;
	pdp_crtc->old_fb = PDP_CRTC_FB(crtc);
	pdp_crtc->flip_async = flip_async;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* Set the crtc to point to the new framebuffer */
	PDP_CRTC_FB(crtc) = fb;

	err = pvr_drm_flip_schedule(pdp_fb->obj, pdp_flip, crtc);
	if (err) {
		spin_lock_irqsave(&dev->event_lock, flags);
		PDP_CRTC_FB(crtc) = pdp_crtc->old_fb;

		pdp_crtc->old_fb = NULL;
		pdp_crtc->flip_event = NULL;
		pdp_crtc->flip_status = PDP_CRTC_FLIP_STATUS_NONE;
		spin_unlock_irqrestore(&dev->event_lock, flags);

		DRM_ERROR("failed to schedule flip (err=%d)\n", err);
		goto err_vblank_put;
	}

	return 0;

err_unlock:
	spin_unlock_irqrestore(&dev->event_lock, flags);

err_vblank_put:
	if (!flip_async)
		drm_vblank_put(crtc->dev, pdp_crtc->number);
	return err;
}

static const struct drm_crtc_helper_funcs pdp_crtc_helper_funcs = {
	.dpms = pdp_crtc_helper_dpms,
	.prepare = pdp_crtc_helper_prepare,
	.commit = pdp_crtc_helper_commit,
	.mode_fixup = pdp_crtc_helper_mode_fixup,
	.mode_set = pdp_crtc_helper_mode_set,
	.mode_set_base = pdp_crtc_helper_mode_set_base,
	.load_lut = pdp_crtc_helper_load_lut,
	.mode_set_base_atomic = pdp_crtc_helper_mode_set_base_atomic,
	.disable = NULL,
};

static const struct drm_crtc_funcs pdp_crtc_funcs = {
	.save = NULL,
	.restore = NULL,
	.reset = NULL,
	.cursor_set = NULL,
	.cursor_move = NULL,
	.gamma_set = NULL,
	.destroy = pdp_crtc_destroy,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = pdp_crtc_page_flip,
};


struct drm_crtc *pdp_crtc_create(struct pdp_display_device *display_dev,
				 uint32_t number)
{
	struct pdp_crtc *pdp_crtc;
	int err;

	pdp_crtc = kzalloc(sizeof(*pdp_crtc), GFP_KERNEL);
	if (!pdp_crtc) {
		err = -ENOMEM;
		goto err_exit;
	}

	pdp_crtc->display_dev = display_dev;
	pdp_crtc->number = number;
	pdp_crtc->enabled = display_dev->enabled;

	switch (number) {
	case 0:
	{
		resource_size_t reg_base =
			pci_resource_start(display_dev->dev->pdev,
					   PDP_PCI_BAR_REGISTERS);

		pdp_crtc->pdp_reg_phys_base = reg_base +
			PDP_PCI_BAR_REGISTERS_PDP1_OFFSET;
		pdp_crtc->pdp_reg_size = PDP_PCI_BAR_REGISTERS_PDP1_SIZE;

		pdp_crtc->pll_reg_phys_base = reg_base +
			PDP_PCI_BAR_REGISTERS_PLL_PDP1_OFFSET;
		pdp_crtc->pll_reg_size = PDP_PCI_BAR_REGISTERS_PLL_PDP1_SIZE;
		break;
	}
	default:
		DRM_ERROR("invalid crtc number %u\n", number);
		err = -EINVAL;
		goto err_crtc_free;
	}

	if (!request_mem_region(pdp_crtc->pdp_reg_phys_base,
				pdp_crtc->pdp_reg_size,
				DRVNAME)) {
		DRM_ERROR("failed to reserve registers\n");
		err = -EBUSY;
		goto err_crtc_free;
	}

	pdp_crtc->pdp_reg = ioremap_nocache(pdp_crtc->pdp_reg_phys_base,
					    pdp_crtc->pdp_reg_size);
	if (!pdp_crtc->pdp_reg) {
		DRM_ERROR("failed to map registers\n");
		err = -ENOMEM;
		goto err_release_mem_region;
	}

	pdp_crtc->pll_reg = ioremap_nocache(pdp_crtc->pll_reg_phys_base,
					    pdp_crtc->pll_reg_size);
	if (!pdp_crtc->pll_reg) {
		DRM_ERROR("failed to map pll registers\n");
		err = -ENOMEM;
		goto err_iounmap_regs;
	}

	drm_crtc_init(display_dev->dev, &pdp_crtc->base, &pdp_crtc_funcs);
	drm_crtc_helper_add(&pdp_crtc->base, &pdp_crtc_helper_funcs);

	DRM_DEBUG_DRIVER("[CRTC:%d]\n", pdp_crtc->base.base.id);

	return &pdp_crtc->base;

err_iounmap_regs:
	iounmap(pdp_crtc->pdp_reg);
err_release_mem_region:
	release_mem_region(pdp_crtc->pdp_reg_phys_base, pdp_crtc->pdp_reg_size);
err_crtc_free:
	kfree(pdp_crtc);
err_exit:
	return ERR_PTR(err);
}

void pdp_crtc_set_plane_enabled(struct drm_crtc *crtc, bool enable)
{
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	uint32_t value;

	pdp_crtc->enabled = enable;

	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL);
	value = REG_VALUE_SET(value,
			      enable ? 0x1 : 0x0,
			      STR1STREN_SHIFT,
			      STR1STREN_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg,
		   TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL,
		   value);
}

void pdp_crtc_set_vblank_enabled(struct drm_crtc *crtc, bool enable)
{
	struct pdp_crtc *pdp_crtc = to_pdp_crtc(crtc);
	uint32_t value;

	value = pdp_rreg32(pdp_crtc->pdp_reg,
			   TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB);
	value = REG_VALUE_SET(value,
			      enable ? 0x1 : 0x0,
			      INTEN_VBLNK0_SHIFT,
			      INTEN_VBLNK0_MASK);
	pdp_wreg32(pdp_crtc->pdp_reg, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB, value);
}

irqreturn_t pdp_crtc_irq_handler(struct pdp_display_device *display_dev)
{
	struct drm_device *dev = display_dev->dev;
	struct drm_crtc *crtc;
	struct pdp_crtc *pdp_crtc;
	uint32_t value;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		pdp_crtc = to_pdp_crtc(crtc);

		value = pdp_rreg32(pdp_crtc->pdp_reg,
				   TCF_RGBPDP_PVR_TCF_RGBPDP_INTSTAT);
		if (REG_VALUE_GET(value, INTS_VBLNK0_SHIFT, INTS_VBLNK0_MASK)) {
			bool flipped;
			unsigned long flags;

			value = REG_VALUE_SET(0,
					      0x1,
					      INTCLR_VBLNK0_SHIFT,
					      INTCLR_VBLNK0_MASK);
			pdp_wreg32(pdp_crtc->pdp_reg,
				   TCF_RGBPDP_PVR_TCF_RGBPDP_INTCLEAR,
				   value);

			drm_handle_vblank(dev, pdp_crtc->number);

			spin_lock_irqsave(&dev->event_lock, flags);
			if (pdp_crtc->flip_status == PDP_CRTC_FLIP_STATUS_DONE)
				flipped = true;
			else
				flipped = false;
			if (flipped)
				pdp_flip_complete_unlocked(dev, crtc);
			spin_unlock_irqrestore(&dev->event_lock, flags);

			if (flipped)
				drm_vblank_put(dev, pdp_crtc->number);
		}
	}

	return IRQ_HANDLED;
}
