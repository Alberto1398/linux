/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/9/22: Created by Lipeng.
 */
#include <drm/drm_fourcc.h>

#include <video/adf.h>
#include <video/adf_format.h>
#include <video/adf_client.h>

#include <video/owl_dss.h>

#include "owl-adf.h"
#include "owl-adf-ext.h"

void get_drm_mode_from_panel(struct owl_panel *panel,
			     struct drm_mode_modeinfo *mode)
{
	memset(mode, 0, sizeof(*mode));

	/* only there three is neccessary for ADF */
	owl_panel_get_draw_size(panel, (int *)&mode->hdisplay,
				(int *)&mode->vdisplay);
	mode->vrefresh = owl_panel_get_refresh_rate(panel);

	adf_modeinfo_set_name(mode);

	ADFDBG("%s: %dx%d@%d\n", __func__,
	       mode->hdisplay, mode->vdisplay, mode->vrefresh);
}

enum owl_color_mode drm_color_to_owl_color(u32 color)
{
	enum owl_color_mode owl_color;

	switch (color) {
	case DRM_FORMAT_RGB565:
		owl_color = OWL_DSS_COLOR_RGB565;
		break;

	case DRM_FORMAT_BGR565:
		owl_color = OWL_DSS_COLOR_BGR565;
		break;

	case DRM_FORMAT_BGRA8888:
		owl_color = OWL_DSS_COLOR_BGRA8888;
		break;
	case DRM_FORMAT_BGRX8888:
		owl_color = OWL_DSS_COLOR_BGRX8888;
		break;

	case DRM_FORMAT_RGBA8888:
		owl_color = OWL_DSS_COLOR_RGBA8888;
		break;
	case DRM_FORMAT_RGBX8888:
		owl_color = OWL_DSS_COLOR_RGBX8888;
		break;

	case DRM_FORMAT_ABGR8888:
		owl_color = OWL_DSS_COLOR_ABGR8888;
		break;
	case DRM_FORMAT_XBGR8888:
		owl_color = OWL_DSS_COLOR_XBGR8888;
		break;

	case DRM_FORMAT_ARGB8888:
		owl_color = OWL_DSS_COLOR_ARGB8888;
		break;
	case DRM_FORMAT_XRGB8888:
		owl_color = OWL_DSS_COLOR_XRGB8888;
		break;

	/* Y VU */
	case DRM_FORMAT_NV12:
		owl_color = OWL_DSS_COLOR_NV12;
		break;
	/* Y UV */
	case DRM_FORMAT_NV21:
		owl_color = OWL_DSS_COLOR_NV21;
		break;
	/* Y V U */
	case DRM_FORMAT_YVU420:
		owl_color = OWL_DSS_COLOR_YVU420;
		break;
	/* Y U V */
	case DRM_FORMAT_YUV420:
		owl_color = OWL_DSS_COLOR_YUV420;
		break;

	default:
		BUG();
		break;
	}

	return owl_color;
}

enum adf_interface_type dss_type_to_interface_type(enum owl_display_type type)
{
	switch (type) {
	case OWL_DISPLAY_TYPE_LCD:
	case OWL_DISPLAY_TYPE_DUMMY:
		return ADF_INTF_DPI;

	case OWL_DISPLAY_TYPE_DSI:
		return ADF_INTF_DSI;

	case OWL_DISPLAY_TYPE_EDP:
		return ADF_INTF_eDP;

	case OWL_DISPLAY_TYPE_HDMI:
		return ADF_INTF_HDMI;

	case OWL_DISPLAY_TYPE_CVBS:
		return OWL_ADF_INTF_CVBS;

	default:
		return ADF_INTF_TYPE_DEVICE_CUSTOM;
	}
}

void owleng_get_capability_ext(struct owl_adf_overlay_engine *owleng)
{
	struct owl_adf_overlay_engine_capability_ext *cap_ext;

	cap_ext = &owleng->cap_ext;

	/* TODO, cap_ext should directly get from DE */
	cap_ext->support_transform = (OWL_ADF_TRANSFORM_FLIP_H_EXT
					| OWL_ADF_TRANSFORM_FLIP_V_EXT);
	cap_ext->support_blend = (OWL_ADF_BLENDING_PREMULT_EXT
					| OWL_ADF_BLENDING_COVERAGE_EXT);
}

/*
 * convert adf buffer and buffer config ext to overlay info,
 * it will do a simple check for the parameters,
 * if there is some invalid, return error.
 */
int adf_buffer_to_video_info(struct owl_adf_device *owladf,
			     struct adf_buffer *buf,
			     struct adf_buffer_mapping *mappings,
			     struct owl_adf_buffer_config_ext *config_ext,
			     struct owl_de_video_info *info)
{
	u8 x_subsampling, y_subsampling;

	u32 paddr;

	int plane, ret = 0;
	int draw_width, draw_height;
	int disp_x, disp_y, disp_width, disp_height; /* real display area */

	struct device *dev = owladf->adfdev.dev;
	struct owl_adf_interface *owlintf;

	if (config_ext->aintf_id >= owladf->n_intfs) {
		dev_err(dev, "aintf_id %d invalid\n", config_ext->aintf_id);
		return -EINVAL;
	}
	owlintf = &owladf->intfs[config_ext->aintf_id];

	owl_panel_get_draw_size(owlintf->panel, &draw_width, &draw_height);
	owl_panel_get_disp_area(owlintf->panel, &disp_x, &disp_y,
				&disp_width, &disp_height);

	ADFDBG("%s: disp size(%dx%d), draw size(%dx%d)\n",
	       __func__, disp_width, disp_height, draw_width, draw_height);
	ADFDBG("%s: disp area(%d,%d %dx%d)\n", __func__,
	       disp_x, disp_y, disp_width, disp_height);

	ADFDBG("%s: %d,%d~%d,%d --> %d,%d~%d,%d\n", __func__,
		config_ext->crop.x1, config_ext->crop.y1,
		config_ext->crop.x2, config_ext->crop.y2,
		config_ext->display.x1, config_ext->display.y1,
		config_ext->display.x2, config_ext->display.y2);

	info->xoff = config_ext->crop.x1;
	info->yoff = config_ext->crop.y1;
	info->width = config_ext->crop.x2 - info->xoff;
	info->height = config_ext->crop.y2 - info->yoff;

	info->pos_x = config_ext->display.x1;
	info->pos_y = config_ext->display.y1;
	info->out_width = config_ext->display.x2 - info->pos_x;
	info->out_height = config_ext->display.y2 - info->pos_y;

	/* TODO, now only valid for OTT */
	if (info->width != info->out_width || info->height != info->out_height)
		info->is_original_scaled = true;
	else
		info->is_original_scaled = false;
	ADFDBG("%s: is_original_scaled %d\n", __func__,
	       info->is_original_scaled);

	/*
	 * caculate real position without scaling
	 */
	info->real_pos_x = (info->pos_x == 0 ? 0 : (info->pos_x - (info->width - info->out_width) / 2));
	info->real_pos_x = (info->real_pos_x < 0 ? 0 : info->real_pos_x);

	info->real_pos_y = (info->pos_y == 0 ? 0 : (info->pos_y - (info->height - info->out_height) / 2));
	info->real_pos_y = (info->real_pos_y < 0 ? 0 : info->real_pos_y);

	ADFDBG("%s: original area-->\n", __func__);
	ADFDBG("%s: (%d,%d %dx%d) --> (%d,%d/%d,%d %dx%d)\n", __func__,
		info->xoff, info->yoff, info->width, info->height,
		info->pos_x, info->pos_y, info->real_pos_x, info->real_pos_y,
		info->out_width, info->out_height);

	/*
	 * Adjust with draw size and display area size.
	 *
	 * There are 2 rectangles in the coordinate system, one is
	 * DRAW rectangle, denoted by (0,0 draw_width,draw_height),
	 * another is DISPLAY rectangle, denoted by
	 * (disp_x,disp_y disp_width,disp_height).
	 *
	 * Now, we need convert the target rectangle
	 * (pos_x,pos_y out_width,out_height) from DRAW rectangle to
	 * DISPLAY rectangle, the algorithm is:
	 * 	out_width / draw_width = out_width_new / disp_width,
	 * 	out_height / draw_height = out_height_new / disp_height,
	 *	(pos_x - 0) / draw_width = (pso_x_new - disp_x) / disp_width
	 *	(pos_y - 0) / draw_height = (pso_y_new - disp_y) / disp_height
	 */
	#define DIV_ROUND(x, y) (((x) + ((y) / 2)) / y)
	info->pos_x = DIV_ROUND(info->pos_x * disp_width, draw_width) + disp_x;
	info->pos_y = DIV_ROUND(info->pos_y * disp_height, draw_height) + disp_y;

	info->out_width = DIV_ROUND(info->out_width * disp_width, draw_width);
	info->out_height = DIV_ROUND(info->out_height * disp_height, draw_height);
	#undef DIV_ROUND

	ADFDBG("%s: area after adjusting-->\n", __func__);
	ADFDBG("%s: (%d,%d %dx%d) --> (%d,%d/%d,%d %dx%d)\n", __func__,
		info->xoff, info->yoff, info->width, info->height,
		info->pos_x, info->pos_y, info->real_pos_x, info->real_pos_y,
		info->out_width, info->out_height);

	info->color_mode = drm_color_to_owl_color(buf->format);

	ADFDBG("%s: blend %d, alpha %d\n", __func__,
		config_ext->blend_type, config_ext->plane_alpha);
	if (config_ext->blend_type == OWL_ADF_BLENDING_PREMULT_EXT)
		info->blending = OWL_BLENDING_PREMULT;
	else if (config_ext->blend_type == OWL_ADF_BLENDING_COVERAGE_EXT)
		info->blending = OWL_BLENDING_COVERAGE;
	else
		info->blending = OWL_BLENDING_NONE;
	info->alpha = config_ext->plane_alpha;

	if (config_ext->transform == OWL_ADF_TRANSFORM_FLIP_H_EXT)
		info->rotation = 2;
	else if (config_ext->transform == OWL_ADF_TRANSFORM_FLIP_V_EXT)
		info->rotation = 1;
	else if (config_ext->transform == OWL_ADF_TRANSFORM_ROT_180_EXT)
		info->rotation = 3;
	else
		info->rotation = 0;

	info->mmu_enable = false;
	if (mappings == NULL) {
		/* maybe a validate buffer, paddr canbe zero */
		ADFDBG("#mappings is NULL)\n");
		paddr = 0;
	} else if (config_ext->stamp == OWL_ADF_FBDEV_BUF_ID) {
		ADFDBG("###NO MMU(maybe ADF FBDEV)\n");
		paddr = sg_phys(mappings->sg_tables[0]->sgl);
	} else {
		ret = owl_de_mmu_sg_table_to_da(mappings->sg_tables[0],
						config_ext->stamp, &paddr);
		if (ret < 0) {
			dev_err(dev, "MMU ERROR %d\n", ret);
			goto err_out;
		}
		ADFDBG("paddr %d\n", paddr);
		info->mmu_enable = true;
	}

	info->offset[0] = buf->offset[0];
	info->pitch[0] = buf->pitch[0];
	info->addr[0] = paddr + info->offset[0] + info->yoff * info->pitch[0]
			+ info->xoff * adf_format_plane_cpp(buf->format, 0);

	ADFDBG("%s: plane 0, P %d, O %d, A %ld\n", __func__,
	       info->pitch[0], info->offset[0], info->addr[0]);

	if (adf_format_is_rgb(buf->format)) {
		info->n_planes = 1;
	} else {
		info->n_planes = buf->n_planes;

		x_subsampling = adf_format_horz_chroma_subsampling(buf->format);
		y_subsampling = adf_format_vert_chroma_subsampling(buf->format);

		for (plane = 1; plane < info->n_planes; plane++) {
			info->offset[plane] = buf->offset[plane];
			info->pitch[plane] = buf->pitch[plane];

			info->addr[plane] = paddr + info->offset[plane]
			  + (info->yoff / y_subsampling) * info->pitch[plane]
			  + info->xoff / x_subsampling;

			ADFDBG("%s: plane %d, P %d, O %d, A %ld\n", __func__,
			       plane, info->pitch[plane], info->offset[plane],
			       info->addr[plane]);
		}
	}

	/* you can use other values as you like */
	info->brightness = owl_panel_brightness_get(owlintf->panel);
	info->contrast = owl_panel_contrast_get(owlintf->panel);
	info->saturation = owl_panel_saturation_get(owlintf->panel);
	if ((config_ext->flag & OWL_ADF_BUFFER_FLAG_ADJUST_SIZE) == OWL_ADF_BUFFER_FLAG_ADJUST_SIZE) {
	    info->flag |= OWL_VIDEO_FLAG_ADJUST_DRAW_SIZE;
	} else {
	    info->flag &= ~OWL_VIDEO_FLAG_ADJUST_DRAW_SIZE;
	}

	return 0;

err_out:
	return ret;
}

int buffer_and_ext_validate(struct owl_adf_device *owladf,
			    struct adf_buffer *buf,
			    struct owl_adf_buffer_config_ext *config_ext)
{
	int ret = 0;

	struct owl_adf_overlay_engine *owleng;
	struct owl_de_video_info info;

	owleng = eng_to_owl_eng(buf->overlay_engine);
	if (!owleng) {
		ret = -EINVAL;
		goto out;
	}

	ADFDBG("%s: owleng %d\n", __func__, owleng->id);

	if (adf_buffer_to_video_info(owladf, buf, NULL,
				     config_ext, &info) < 0) {
		ret = -EFAULT;
		goto out;
	}

	ret = owl_de_video_info_validate(owleng->video, &info);
out:
	ADFDBG("%s: ret = %d\n", __func__, ret);
	return ret;
}
