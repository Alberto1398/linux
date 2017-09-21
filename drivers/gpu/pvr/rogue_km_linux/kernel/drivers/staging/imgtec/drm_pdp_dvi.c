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

static char preferred_mode_name[DRM_DISPLAY_MODE_LEN] = "\0";

module_param_string(dvi_preferred_mode,
		    preferred_mode_name,
		    DRM_DISPLAY_MODE_LEN,
		    S_IRUSR | S_IRGRP | S_IROTH);

MODULE_PARM_DESC(dvi_preferred_mode,
		 "Specify the preferred mode (if supported), e.g. 1280x1024.");

struct pdp_dvi_connector {
	struct drm_connector base;
	struct pdp_display_device *display_dev;
};

#define to_pdp_connector(connector) \
	container_of(connector, struct pdp_dvi_connector, base)

static int pdp_dvi_connector_helper_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	int num_modes;

	num_modes = drm_add_modes_noedid(connector,
					 dev->mode_config.max_width,
					 dev->mode_config.max_height);
	if (num_modes) {
		struct drm_display_mode *pref_mode = NULL;

		if (strlen(preferred_mode_name) != 0) {
			struct drm_display_mode *mode;
			struct list_head *entry;

			list_for_each(entry, &connector->probed_modes) {
				mode = list_entry(entry,
						  struct drm_display_mode,
						  head);
				if (!strcmp(mode->name, preferred_mode_name)) {
					pref_mode = mode;
					break;
				}
			}
		}

		if (!pref_mode) {
			if (!list_empty(&connector->probed_modes)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
				pref_mode =
					list_entry(connector->probed_modes.next,
						   struct drm_display_mode,
						   head);
#else
				pref_mode =
					list_entry(connector->probed_modes.prev,
						   struct drm_display_mode,
						   head);
#endif
			}
		}

		if (pref_mode)
			pref_mode->type |= DRM_MODE_TYPE_PREFERRED;
	}

	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s] found %d modes\n",
			 connector->base.id,
			 connector_name(connector),
			 num_modes);

	return num_modes;
}

static int pdp_dvi_connector_helper_mode_valid(struct drm_connector *connector,
					       struct drm_display_mode *mode)
{
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;
	else if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	return MODE_OK;
}

static struct drm_encoder *
pdp_dvi_connector_helper_best_encoder(struct drm_connector *connector)
{
	/* Pick the first encoder we find */
	if (connector->encoder_ids[0] != 0) {
		struct drm_mode_object *mode_obj;

		mode_obj = drm_mode_object_find(connector->dev,
						connector->encoder_ids[0],
						DRM_MODE_OBJECT_ENCODER);
		if (mode_obj) {
			struct drm_encoder *encoder =
				obj_to_encoder(mode_obj);

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
pdp_dvi_connector_detect(struct drm_connector *connector,
			 bool force)
{
	/*
	 * It appears that there is no way to determine if a monitor
	 * is connected. This needs to be set to connected otherwise
	 * DPMS never gets set to ON.
	 */
	return connector_status_connected;
}

static int pdp_dvi_connector_set_property(struct drm_connector *connector,
					  struct drm_property *property,
					  uint64_t value)
{
	return -ENOSYS;
}

static void pdp_dvi_connector_destroy(struct drm_connector *connector)
{
	struct pdp_dvi_connector *pdp_connector = to_pdp_connector(connector);
	struct pdp_display_device *display_dev = pdp_connector->display_dev;

	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s]\n",
			 connector->base.id,
			 connector_name(connector));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	drm_connector_unregister(connector);
#endif
	drm_connector_cleanup(connector);

	kfree(pdp_connector);
	display_dev->connector = NULL;
}

static void pdp_dvi_connector_force(struct drm_connector *connector)
{
}

static struct drm_connector_helper_funcs pdp_dvi_connector_helper_funcs = {
	.get_modes = pdp_dvi_connector_helper_get_modes,
	.mode_valid = pdp_dvi_connector_helper_mode_valid,
	.best_encoder = pdp_dvi_connector_helper_best_encoder,
};

static const struct drm_connector_funcs pdp_dvi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = NULL,
	.restore = NULL,
	.reset = NULL,
	.detect = pdp_dvi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = pdp_dvi_connector_set_property,
	.destroy = pdp_dvi_connector_destroy,
	.force = pdp_dvi_connector_force,
};


struct drm_connector *
pdp_dvi_connector_create(struct pdp_display_device *display_dev)
{
	struct pdp_dvi_connector *pdp_connector;

	pdp_connector = kzalloc(sizeof(*pdp_connector), GFP_KERNEL);
	if (!pdp_connector)
		return ERR_PTR(-ENOMEM);

	pdp_connector->display_dev = display_dev;

	drm_connector_init(display_dev->dev,
			   &pdp_connector->base,
			   &pdp_dvi_connector_funcs,
			   DRM_MODE_CONNECTOR_DVID);
	drm_connector_helper_add(&pdp_connector->base,
				 &pdp_dvi_connector_helper_funcs);

	pdp_connector->base.dpms = DRM_MODE_DPMS_OFF;
	pdp_connector->base.interlace_allowed = false;
	pdp_connector->base.doublescan_allowed = false;
	pdp_connector->base.display_info.subpixel_order = SubPixelHorizontalRGB;

	DRM_DEBUG_DRIVER("[CONNECTOR:%d:%s]\n",
			 pdp_connector->base.base.id,
			 connector_name(&pdp_connector->base));

	return &pdp_connector->base;
}
