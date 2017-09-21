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

#include "drm_pdp.h"

struct pdp_tmds_encoder {
	struct drm_encoder base;
	struct pdp_display_device *display_dev;
};

#define to_pdp_encoder(encoder) \
	container_of(encoder, struct pdp_tmds_encoder, base)


static void pdp_tmds_encoder_helper_dpms(struct drm_encoder *encoder, int mode)
{
}

static bool
pdp_tmds_encoder_helper_mode_fixup(struct drm_encoder *encoder,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
				   struct drm_display_mode *mode,
#else
				   const struct drm_display_mode *mode,
#endif
				   struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void pdp_tmds_encoder_helper_prepare(struct drm_encoder *encoder)
{
}

static void pdp_tmds_encoder_helper_commit(struct drm_encoder *encoder)
{
}

static void
pdp_tmds_encoder_helper_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
}

static void pdp_tmds_encoder_destroy(struct drm_encoder *encoder)
{
	struct pdp_tmds_encoder *pdp_encoder = to_pdp_encoder(encoder);
	struct pdp_display_device *display_dev = pdp_encoder->display_dev;

	DRM_DEBUG_DRIVER("[ENCODER:%d:%s]\n",
			 encoder->base.id,
			 encoder_name(encoder));

	drm_encoder_cleanup(encoder);

	kfree(pdp_encoder);
	display_dev->encoder = NULL;
}

static const struct drm_encoder_helper_funcs pdp_tmds_encoder_helper_funcs = {
	.dpms = pdp_tmds_encoder_helper_dpms,
	.save = NULL,
	.restore = NULL,
	.mode_fixup = pdp_tmds_encoder_helper_mode_fixup,
	.prepare = pdp_tmds_encoder_helper_prepare,
	.commit = pdp_tmds_encoder_helper_commit,
	.mode_set = pdp_tmds_encoder_helper_mode_set,
	.get_crtc = NULL,
	.detect = NULL,
	.disable = NULL,
};

static const struct drm_encoder_funcs pdp_tmds_encoder_funcs = {
	.reset = NULL,
	.destroy = pdp_tmds_encoder_destroy,
};

struct drm_encoder *
pdp_tmds_encoder_create(struct pdp_display_device *display_dev)
{
	struct pdp_tmds_encoder *pdp_encoder;

	pdp_encoder = kzalloc(sizeof(*pdp_encoder), GFP_KERNEL);
	if (!pdp_encoder)
		return ERR_PTR(-ENOMEM);

	pdp_encoder->display_dev = display_dev;

	drm_encoder_init(display_dev->dev,
			 &pdp_encoder->base,
			 &pdp_tmds_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(&pdp_encoder->base,
			       &pdp_tmds_encoder_helper_funcs);

	/*
	 * This is a bit field that's used to determine which
	 * CRTCs can drive this encoder.
	 */
	pdp_encoder->base.possible_crtcs = 0x1;

	DRM_DEBUG_DRIVER("[ENCODER:%d:%s]\n",
			 pdp_encoder->base.base.id,
			 encoder_name(&pdp_encoder->base));

	return &pdp_encoder->base;
}
