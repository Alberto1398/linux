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

#include <linux/moduleparam.h>
#include <linux/version.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "drm_pdp.h"

#define PDP_WIDTH_MIN 640
#define PDP_WIDTH_MAX 1280
#define PDP_HEIGHT_MIN 480
#define PDP_HEIGHT_MAX 1024

static bool async_flip_enable = true;

module_param(async_flip_enable,
	     bool,
	     S_IRUSR | S_IRGRP | S_IROTH);

#if defined(DRM_MODE_PAGE_FLIP_ASYNC)
MODULE_PARM_DESC(async_flip_enable,
		 "Enable support for 'faked' async flipping (default: Y)");
#else
MODULE_PARM_DESC(async_flip_enable,
		 "This option is unsupported on this kernel version");
#endif


static void pdp_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct pdp_framebuffer *pdp_fb = to_pdp_framebuffer(fb);

	DRM_DEBUG_DRIVER("[FB:%d]\n", fb->base.id);

	drm_framebuffer_cleanup(fb);

	pvr_drm_gem_unmap(pdp_fb->obj);
	drm_gem_object_unreference_unlocked(pdp_fb->obj);

	kfree(pdp_fb);
}

static int pdp_framebuffer_create_handle(struct drm_framebuffer *fb,
					 struct drm_file *file,
					 unsigned int *handle)
{
	struct pdp_framebuffer *pdp_fb = to_pdp_framebuffer(fb);

	DRM_DEBUG_DRIVER("[FB:%d]\n", fb->base.id);

	return drm_gem_handle_create(file, pdp_fb->obj, handle);
}

static const struct drm_framebuffer_funcs pdp_framebuffer_funcs = {
	.destroy = pdp_framebuffer_destroy,
	.create_handle = pdp_framebuffer_create_handle,
	.dirty = NULL,
};

static int pdp_framebuffer_init(struct pdp_display_device *display_dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
				struct drm_mode_fb_cmd *mode_cmd,
#else
				struct drm_mode_fb_cmd2 *mode_cmd,
#endif
				struct pdp_framebuffer *pdp_fb,
				struct drm_gem_object *bo)
{
	int err;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
	if (mode_cmd->bpp != 32 || mode_cmd->depth != 24)
		DRM_ERROR("pixel format not supported (bpp = %u depth = %u)\n",
			  mode_cmd->bpp, mode_cmd->depth);
#else
	switch (mode_cmd->pixel_format) {
	case DRM_FORMAT_XRGB8888:
		break;
	default:
		DRM_ERROR("pixel format not supported (format = %u)\n",
			  mode_cmd->pixel_format);
		return -EINVAL;
	}

	if (mode_cmd->flags & DRM_MODE_FB_INTERLACED) {
		DRM_ERROR("interlaced framebuffers not supported\n");
		return -EINVAL;
	}
#endif

	err = drm_framebuffer_init(display_dev->dev,
				   &pdp_fb->base,
				   &pdp_framebuffer_funcs);
	if (err) {
		DRM_ERROR("failed to initialise framebuffer (err=%d)\n",
			  err);
		return err;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	drm_helper_mode_fill_fb_struct(&pdp_fb->base, mode_cmd);
#else
	err = drm_helper_mode_fill_fb_struct(&pdp_fb->base, mode_cmd);
	if (err) {
		DRM_ERROR("failed to fill in framebuffer mode info (err=%d)\n",
			  err);
		return err;
	}
#endif
	pdp_fb->obj = bo;

	err = pvr_drm_gem_map(pdp_fb->obj);
	if (err)
		return err;

	err = pvr_drm_gem_dev_addr(pdp_fb->obj, 0, &pdp_fb->dev_addr);
	if (err) {
		pvr_drm_gem_unmap(pdp_fb->obj);
		return err;
	}

	DRM_DEBUG_DRIVER("[FB:%d]\n", pdp_fb->base.base.id);

	return 0;
}


/*************************************************************************/ /*!
 DRM mode config callbacks
*/ /**************************************************************************/

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
static struct drm_framebuffer *pdp_fb_create(struct drm_device *dev,
					     struct drm_file *file,
					     struct drm_mode_fb_cmd *mode_cmd)
#else
static struct drm_framebuffer *pdp_fb_create(struct drm_device *dev,
					     struct drm_file *file,
					     struct drm_mode_fb_cmd2 *mode_cmd)
#endif
{
	struct pdp_display_device *display_dev =
		(struct pdp_display_device *)pvr_drm_get_display_device(dev);
	struct pdp_framebuffer *pdp_fb;
	struct drm_gem_object *bo;
	__u32 handle;
	int err;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
	handle = mode_cmd->handle;
#else
	handle = mode_cmd->handles[0];
#endif

	bo = drm_gem_object_lookup(dev, file, handle);
	if (!bo) {
		DRM_ERROR("failed to find buffer with handle %u\n", handle);
		return ERR_PTR(-ENOENT);
	}

	pdp_fb = kzalloc(sizeof(*pdp_fb), GFP_KERNEL);
	if (!pdp_fb) {
		drm_gem_object_unreference_unlocked(bo);
		return ERR_PTR(-ENOMEM);
	}

	err = pdp_framebuffer_init(display_dev, mode_cmd, pdp_fb, bo);
	if (err) {
		kfree(pdp_fb);
		drm_gem_object_unreference_unlocked(bo);
		return ERR_PTR(err);
	}

	DRM_DEBUG_DRIVER("[FB:%d]\n", pdp_fb->base.base.id);

	return &pdp_fb->base;
}

static
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
const
#endif
struct drm_mode_config_funcs pdp_mode_config_funcs = {
	.fb_create = pdp_fb_create,
	.output_poll_changed = NULL,
};


int pdp_modeset_init(struct pdp_display_device *display_dev)
{
	struct drm_device *dev = display_dev->dev;
	int err;

	drm_mode_config_init(dev);

	dev->mode_config.funcs = &pdp_mode_config_funcs;
	dev->mode_config.min_width = PDP_WIDTH_MIN;
	dev->mode_config.max_width = PDP_WIDTH_MAX;
	dev->mode_config.min_height = PDP_HEIGHT_MIN;
	dev->mode_config.max_height = PDP_HEIGHT_MAX;
	dev->mode_config.fb_base = 0;

#if defined(DRM_MODE_PAGE_FLIP_ASYNC)
	dev->mode_config.async_page_flip = async_flip_enable;

	DRM_INFO("async flip support is %s\n",
		 async_flip_enable ? "enabled" : "disabled");
#else
	if (async_flip_enable)
		DRM_INFO("async flipping unsupported on this kernel version\n");
#endif

	display_dev->crtc = pdp_crtc_create(display_dev, 0);
	if (IS_ERR(display_dev->crtc)) {
		DRM_ERROR("failed to create a CRTC\n");
		err = PTR_ERR(display_dev->crtc);
		goto err_config_cleanup;
	}

	display_dev->connector = pdp_dvi_connector_create(display_dev);
	if (IS_ERR(display_dev->connector)) {
		DRM_ERROR("failed to create a connector\n");
		err = PTR_ERR(display_dev->connector);
		goto err_config_cleanup;
	}

	display_dev->encoder = pdp_tmds_encoder_create(display_dev);
	if (IS_ERR(display_dev->encoder)) {
		DRM_ERROR("failed to create an encoder\n");
		err = PTR_ERR(display_dev->encoder);
		goto err_config_cleanup;
	}

	err = drm_mode_connector_attach_encoder(display_dev->connector,
						display_dev->encoder);
	if (err) {
		DRM_ERROR("failed to attach [ENCODER:%d:%s] to "
			  "[CONNECTOR:%d:%s] (err=%d)\n",
			  display_dev->encoder->base.id,
			  encoder_name(display_dev->encoder),
			  display_dev->connector->base.id,
			  connector_name(display_dev->connector),
			  err);
		goto err_config_cleanup;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	err = drm_connector_register(display_dev->connector);
	if (err) {
		DRM_ERROR("[CONNECTOR:%d:%s] failed to register (err=%d)\n",
			  display_dev->connector->base.id,
			  connector_name(display_dev->connector),
			  err);
		goto err_config_cleanup;
	}
#endif

	DRM_DEBUG_DRIVER("initialised\n");

	return 0;

err_config_cleanup:
	drm_mode_config_cleanup(dev);

	return err;
}

void pdp_modeset_cleanup(struct pdp_display_device *display_dev)
{
	drm_mode_config_cleanup(display_dev->dev);

	DRM_DEBUG_DRIVER("cleaned up\n");
}
