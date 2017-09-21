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

#if !defined(__DRM_PDP_H__)
#define __DRM_PDP_H__

#include <linux/version.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>

#include <pvr_drm_display.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
#define kmalloc_array(n, size, flags) kmalloc((n) * (size), flags)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
#define connector_name(connector) ((connector)->name)
#define encoder_name(encoder) ((encoder)->name)
#else
#define connector_name(connector) "unknown"
#define encoder_name(encoder) "unknown"
#endif

#define DRVNAME "drm_pdp"

#define PDP_PCI_BAR_REGISTERS 0

#define PDP_PCI_BAR_REGISTERS_PLL_PDP1_OFFSET 0x1000
#define PDP_PCI_BAR_REGISTERS_PLL_PDP1_SIZE 0x13FF

#define PDP_PCI_BAR_REGISTERS_PDP1_OFFSET 0xC000
#define PDP_PCI_BAR_REGISTERS_PDP1_SIZE 0x2000

struct pdp_mm_allocation;

struct pdp_display_device {
	struct drm_device *dev;

	struct pdp_mm *mm;
	void *irq_handle;

	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	struct dentry *debugfs_dir_entry;
	struct dentry *display_enabled_entry;
	bool enabled;
};

struct pdp_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
	uint64_t dev_addr;
};

#define to_pdp_framebuffer(fb) container_of(fb, struct pdp_framebuffer, base)

int pdp_mm_init(struct pdp_display_device *display_dev);
void pdp_mm_cleanup(struct pdp_display_device *display_dev);
int pdp_mm_alloc(struct pdp_display_device *display_dev,
		 size_t size,
		 struct pdp_mm_allocation **alloc_out);
void pdp_mm_free(struct pdp_mm_allocation *alloc);
int pdp_mm_cpu_paddr(struct pdp_mm_allocation *alloc, uint64_t *addr_out);
int pdp_mm_dev_paddr(struct pdp_mm_allocation *alloc, uint64_t *addr_out);
int pdp_mm_size(struct pdp_mm_allocation *alloc, size_t *size_out);

struct drm_crtc *pdp_crtc_create(struct pdp_display_device *display_dev,
				 uint32_t number);
void pdp_crtc_set_plane_enabled(struct drm_crtc *crtc, bool enable);
void pdp_crtc_set_vblank_enabled(struct drm_crtc *crtc, bool enable);
irqreturn_t pdp_crtc_irq_handler(struct pdp_display_device *display_dev);

struct drm_connector *
pdp_dvi_connector_create(struct pdp_display_device *display_dev);

struct drm_encoder *
pdp_tmds_encoder_create(struct pdp_display_device *display_dev);

int pdp_modeset_init(struct pdp_display_device *display_dev);
void pdp_modeset_cleanup(struct pdp_display_device *display_dev);

#endif /* !defined(__DRM_PDP_H__) */
