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
 *	2015/8/21: Created by Lipeng.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/clk.h>

#include <video/owl_dss.h>

#include "de-s900.h"

static struct owl_de_device		de_s900_device;

/*
 * NOTE:
 *	the following macros must be used after
 *	executing 'owl_de_register'
 */
#define de_readl(idx)		readl(de_s900_device.base + (idx))
#define de_writel(val, idx)	writel(val, de_s900_device.base + (idx))

#define de_debug(format, ...) \
	dev_dbg(&de_s900_device.pdev->dev, format, ## __VA_ARGS__)
#define de_info(format, ...) \
	dev_info(&de_s900_device.pdev->dev, format, ## __VA_ARGS__)
#define de_err(format, ...) \
	dev_err(&de_s900_device.pdev->dev, format, ## __VA_ARGS__)

struct de_s900_pdata {
	void __iomem			*cmu_declk_reg;
	struct clk			*clk;
	struct clk			*parent_clk;
};

/*===================================================================
 *			S900 DE path
 *==================================================================*/

static int de_s900_path_enable(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	de_debug("%s, path %d enable %d\n", __func__, path->id, enable);

	val = de_readl(DE_PATH_EN(path->id));
	val = REG_SET_VAL(val, enable, DE_PATH_ENABLE_BIT, DE_PATH_ENABLE_BIT);
	de_writel(val, DE_PATH_EN(path->id));

	return 0;
}

static bool de_s900_path_is_enabled(struct owl_de_path *path)
{
	uint32_t val;

	val = de_readl(DE_PATH_EN(path->id));

	return REG_GET_VAL(val, DE_PATH_ENABLE_BIT, DE_PATH_ENABLE_BIT) == 1;
}

static int de_s900_path_attach(struct owl_de_path *path,
			       struct owl_de_video *video)
{
	uint32_t val;

	de_debug("%s, attach video%d to path%d\n",
		 __func__, video->id, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	val = de_readl(DE_PATH_CTL(path->id));
	val = REG_SET_VAL(val, 1,
			  DE_PATH_VIDEO_ENABLE_BEGIN_BIT + video->id,
			  DE_PATH_VIDEO_ENABLE_BEGIN_BIT + video->id);
	de_writel(val, DE_PATH_CTL(path->id));

	return 0;
}

static int de_s900_path_detach(struct owl_de_path *path,
			       struct owl_de_video *video)
{
	uint32_t val;

	de_debug("%s, attach video%d to path%d\n",
		 __func__, video->id, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	val = de_readl(DE_PATH_CTL(path->id));
	val = REG_SET_VAL(val, 0, DE_PATH_VIDEO_ENABLE_BEGIN_BIT + video->id,
			  DE_PATH_VIDEO_ENABLE_BEGIN_BIT + video->id);
	de_writel(val, DE_PATH_CTL(path->id));

	return 0;
}

static int de_s900_path_detach_all(struct owl_de_path *path)
{
	uint32_t val;

	de_debug("%s, path %d\n", __func__, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	val = de_readl(DE_PATH_CTL(path->id));
	val = REG_SET_VAL(val, 0, DE_PATH_VIDEO_ENABLE_END_BIT,
			  DE_PATH_VIDEO_ENABLE_BEGIN_BIT);
	de_writel(val, DE_PATH_CTL(path->id));

	return 0;
}

static void __path_display_type_set(struct owl_de_path *path,
				    enum owl_display_type type)
{
	uint32_t val;
	uint32_t path2_clk_src;

	struct de_s900_pdata *pdata = de_s900_device.pdata;

	de_debug("%s, path %d type %d\n", __func__, path->id, type);

	if (type != OWL_DISPLAY_TYPE_HDMI) {
		val = de_readl(DE_OUTPUT_CON);

		path2_clk_src = readl(pdata->cmu_declk_reg);
		path2_clk_src &= ~(0x3 << 16);

		switch (type) {
		case OWL_DISPLAY_TYPE_LCD:
			val = REG_SET_VAL(val, 0,
					  DE_OUTPUT_PATH2_DEVICE_END_BIT,
					  DE_OUTPUT_PATH2_DEVICE_BEGIN_BIT);
			path2_clk_src |= (0 << 16);
			break;

		case OWL_DISPLAY_TYPE_DSI:
			val = REG_SET_VAL(val, 1,
					  DE_OUTPUT_PATH2_DEVICE_END_BIT,
					  DE_OUTPUT_PATH2_DEVICE_BEGIN_BIT);
			path2_clk_src |= (1 << 16);
			break;

		case OWL_DISPLAY_TYPE_EDP:
			val = REG_SET_VAL(val, 2,
					  DE_OUTPUT_PATH2_DEVICE_END_BIT,
					  DE_OUTPUT_PATH2_DEVICE_BEGIN_BIT);
			path2_clk_src |= (2 << 16);
			break;

		default:
			BUG();
			break;
		}

		de_writel(val, DE_OUTPUT_CON);
		writel(path2_clk_src, pdata->cmu_declk_reg);
	}
}

static void __path_size_set(struct owl_de_path *path,
			    uint32_t width, uint32_t height)
{
	uint32_t val;

	de_debug("%s, path %d %dx%d\n", __func__, path->id, width, height);

	BUG_ON((width > DE_PATH_SIZE_WIDTH) || (height > DE_PATH_SIZE_HEIGHT));

	val = REG_VAL(height - 1, DE_PATH_SIZE_HEIGHT_END_BIT,
			  DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(width - 1, DE_PATH_SIZE_WIDTH_END_BIT,
			  DE_PATH_SIZE_WIDTH_BEGIN_BIT);

	de_writel(val, DE_PATH_SIZE(path->id));
}

static void __path_dither_enable(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	de_debug("%s, path %d, enable %d\n", __func__, path->id, enable);

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	val = de_readl(DE_PATH_DITHER);
	val = REG_SET_VAL(val, enable, DE_PATH_DITHER_ENABLE_BIT,
			  DE_PATH_DITHER_ENABLE_BIT);
	de_writel(val, DE_PATH_DITHER);
}

static void __path_dither_set(struct owl_de_path *path,
			      enum owl_dither_mode mode)
{
	uint32_t val;

	de_debug("%s, path %d, mode %d\n", __func__, path->id, mode);

	if (path->id != 1)	/* TODO */
		return;

	if (mode == DITHER_DISABLE) {
		__path_dither_enable(path, false);
		return;
	}

	val = de_readl(DE_PATH_DITHER);

	switch (mode) {
	case DITHER_24_TO_18:
		val = REG_SET_VAL(val, 0, DE_PATH_DITHER_MODE_BIT,
				  DE_PATH_DITHER_MODE_BIT);
		break;

	case DITHER_24_TO_16:
		val = REG_SET_VAL(val, 1, DE_PATH_DITHER_MODE_BIT,
				  DE_PATH_DITHER_MODE_BIT);
		break;

	default:
		return;
	}

	de_writel(val, DE_PATH_DITHER);

	__path_dither_enable(path, true);
}

static void __path_vmode_set(struct owl_de_path *path, int vmode)
{
	uint32_t val;

	de_debug("%s, path %d vmode %d\n", __func__, path->id, vmode);

	val = de_readl(DE_PATH_CTL(path->id));

	if (vmode == DSS_VMODE_INTERLACED)
		val = REG_SET_VAL(val, 1, DE_PATH_CTL_ILACE_BIT,
				  DE_PATH_CTL_ILACE_BIT);
	else
		val = REG_SET_VAL(val, 0, DE_PATH_CTL_ILACE_BIT,
				  DE_PATH_CTL_ILACE_BIT);
	de_writel(val, DE_PATH_CTL(path->id));
}

static void __path_default_color_set(struct owl_de_path *path, uint32_t color)
{
	de_debug("%s, path %d color %x\n", __func__, path->id, color);

	de_writel(color, DE_PATH_BK(path->id));
}

static void de_s900_path_apply_info(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;

	de_debug("%s, path%d\n", __func__, path->id);

	__path_display_type_set(path, info->type);

	__path_size_set(path, info->width, info->height);

	__path_dither_set(path, info->dither_mode);

	__path_vmode_set(path, info->vmode);

	/* for test */
	__path_default_color_set(path, 0x0);
}

static void de_s900_path_set_fcr(struct owl_de_path *path)
{
	uint32_t val;

	de_debug("%s, path%d\n", __func__, path->id);

	val = de_readl(DE_PATH_FCR(path->id));

	val = REG_SET_VAL(val, 1, DE_PATH_FCR_BIT, DE_PATH_FCR_BIT);

	de_writel(val, DE_PATH_FCR(path->id));

	val = de_readl(DE_OUTPUT_STAT);
	if (val != 0) {
		printk(KERN_DEBUG "###DE fifo underflow 0x%x\n", val);
		de_writel(val, DE_OUTPUT_STAT);
	}
}

static bool de_s900_path_is_fcr_set(struct owl_de_path *path)
{
	return !!(de_readl(DE_PATH_FCR(path->id)) & (1 << DE_PATH_FCR_BIT));
}

/*
 * preline enable & preline pending masks
 * and, VB active mask is equal to "preline_mask << 8"
 */
static uint32_t __path_preline_mask(struct owl_de_path *path)
{
	if (path->id == 0) {
		return 1 << 0;
	} else {
		switch (path->info.type) {
		case OWL_DISPLAY_TYPE_LCD:
			return 1 << 1;

		case OWL_DISPLAY_TYPE_DSI:
			return 1 << 2;

		case OWL_DISPLAY_TYPE_EDP:
			return 1 << 3;

		default:
			BUG();
			break;
		}
	}
}

static void de_s900_path_preline_enable(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	de_debug("%s, path%d, enable %d\n", __func__, path->id, enable);

	val = de_readl(DE_IRQENABLE);

	if (enable)
		val |= __path_preline_mask(path);
	else
		val &= ~__path_preline_mask(path);

	de_writel(val, DE_IRQENABLE);
}

static bool de_s900_path_is_preline_enabled(struct owl_de_path *path)
{
	return !!(de_readl(DE_IRQENABLE) & __path_preline_mask(path));
}

static bool de_s900_path_is_preline_pending(struct owl_de_path *path)
{
	return !!(de_readl(DE_IRQSTATUS) & __path_preline_mask(path));
}

static void de_s900_path_clear_preline_pending(struct owl_de_path *path)
{
	de_writel(__path_preline_mask(path), DE_IRQSTATUS);
}

static bool de_s900_path_is_vb_valid(struct owl_de_path *path)
{
	return !!(de_readl(DE_IRQSTATUS) & (__path_preline_mask(path) << 8));
}

static void __path_gamma_set(struct owl_de_path *path, uint32_t *gamma_val)
{
	bool is_busy;
	uint32_t idx, val;

	/* write operation mode(1 for write) */
	val = de_readl(DE_PATH_GAMMA_IDX(path->id));
	val = REG_SET_VAL(val, 1, DE_PATH_GAMMA_IDX_OP_SEL_END_BIT,
			DE_PATH_GAMMA_IDX_OP_SEL_BEGIN_BIT);

	for (idx = 0; idx < (256 * 3 / 4); idx++) {
		/* write index */
		val = REG_SET_VAL(val, idx, DE_PATH_GAMMA_IDX_INDEX_END_BIT,
				DE_PATH_GAMMA_IDX_INDEX_BEGIN_BIT);

		de_writel(val, DE_PATH_GAMMA_IDX(path->id));

		/* write ram */
		de_writel(gamma_val[idx], DE_PATH_GAMMA_RAM(path->id));

		/* wait for busy bit */
		do {
			val = de_readl(DE_PATH_GAMMA_IDX(path->id));
			is_busy = REG_GET_VAL(val, DE_PATH_GAMMA_IDX_BUSY_BIT,
					DE_PATH_GAMMA_IDX_BUSY_BIT);
		} while (is_busy);
	}

	/* write finish, clear write bit and index */
	val = de_readl(DE_PATH_GAMMA_IDX(path->id));
	val = REG_SET_VAL(val, 0, DE_PATH_GAMMA_IDX_INDEX_END_BIT,
					DE_PATH_GAMMA_IDX_INDEX_BEGIN_BIT);
	val = REG_SET_VAL(val, 0, DE_PATH_GAMMA_IDX_OP_SEL_END_BIT,
				DE_PATH_GAMMA_IDX_OP_SEL_BEGIN_BIT);

	de_writel(val, DE_PATH_GAMMA_IDX(path->id));
}


static void de_s900_path_set_gamma_table(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;
	uint8_t gamma_data[256 * 3];
	int is_enabled;
	int i = 0;

	de_debug("gamma_r %d, gamma_g %d, gamma_b %d\n",
		info->gamma_r_val, info->gamma_g_val, info->gamma_b_val);

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	/* R */
	for (i = 0; i < 256; i++) {
		gamma_data[i] = i * info->gamma_r_val / 100;
	}

	/* G */
	for (i = 0; i < 256; i++) {
		gamma_data[i + 256] = i * info->gamma_g_val / 100;
	}

	/* B */
	for (i = 0; i < 256; i++) {
		gamma_data[i + 256 * 2] = i * info->gamma_b_val / 100;
	}

	__path_gamma_set(path, &gamma_data[0]);

	de_debug("%s, ok!\n", __func__);
}

static void de_s900_path_get_gamma_table(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;

	de_debug("%s, path%d\n", __func__, path->id);
}

static int de_s900_path_enable_gamma(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	de_debug("%s, enable %d\n", __func__, enable);

	val = de_readl(DE_PATH_GAMMA_ENABLE(path->id));

	val = REG_SET_VAL(val, enable, DE_PATH_GAMMA_ENABLE_BIT,
			DE_PATH_GAMMA_ENABLE_BIT);

	de_writel(val, DE_PATH_GAMMA_ENABLE(path->id));

}

static bool de_s900_path_is_gamma_enabled(struct owl_de_path *path)
{
	uint32_t val;

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	val = de_readl(DE_PATH_GAMMA_ENABLE(path->id));

	return REG_GET_VAL(val, DE_PATH_GAMMA_ENABLE_BIT,
			DE_PATH_GAMMA_ENABLE_BIT) == 1;
}

static struct owl_de_path_ops de_s900_path_ops = {
	.enable = de_s900_path_enable,
	.is_enabled = de_s900_path_is_enabled,

	.attach = de_s900_path_attach,
	.detach = de_s900_path_detach,
	.detach_all = de_s900_path_detach_all,

	.apply_info = de_s900_path_apply_info,

	.set_fcr = de_s900_path_set_fcr,
	.is_fcr_set = de_s900_path_is_fcr_set,

	.preline_enable = de_s900_path_preline_enable,
	.is_preline_enabled = de_s900_path_is_preline_enabled,
	.is_preline_pending = de_s900_path_is_preline_pending,
	.clear_preline_pending = de_s900_path_clear_preline_pending,

	.set_gamma_table = de_s900_path_set_gamma_table,
	.get_gamma_table = de_s900_path_get_gamma_table,
	.gamma_enable = de_s900_path_enable_gamma,
	.is_gamma_enabled = de_s900_path_is_gamma_enabled,

	.is_vb_valid = de_s900_path_is_vb_valid,
};

static struct owl_de_path de_s900_paths[] = {
	{
		.id			= 0,
		.name			= "digit",
		.supported_displays	= OWL_DISPLAY_TYPE_HDMI,
		.ops			= &de_s900_path_ops,
	},
	{
		.id			= 1,
		.name			= "lcd",
		.supported_displays	= OWL_DISPLAY_TYPE_LCD
					| OWL_DISPLAY_TYPE_DSI
					| OWL_DISPLAY_TYPE_EDP
					| OWL_DISPLAY_TYPE_DUMMY,
		.ops			= &de_s900_path_ops,
	},
};


/*===================================================================
 *			S900 DE video layer
 *==================================================================*/
static int __de_mmu_enable(struct owl_de_video *video, bool enable)
{
	uint32_t val;

	de_debug("%s, enable %d\n", __func__, enable);

	val = de_readl(DE_MMU_EN);
	val = REG_SET_VAL(val, enable, video->id, video->id);

	de_writel(val, DE_MMU_EN);
	return 0;
}

static int __de_color_mode_to_hw_mode(enum owl_color_mode color_mode)
{
	int hw_format = 0;

	switch (color_mode) {
	/* 565 is specail, same as DE */
	case OWL_DSS_COLOR_BGR565:
		hw_format = 1;
		break;
	case OWL_DSS_COLOR_RGB565:
		hw_format = 0;
		break;

	/* 8888 * 1555, reverse with DE */
	case OWL_DSS_COLOR_BGRA8888:
	case OWL_DSS_COLOR_BGRX8888:
		hw_format = 4;
		break;
	case OWL_DSS_COLOR_RGBA8888:
	case OWL_DSS_COLOR_RGBX8888:
		hw_format = 5;
		break;
	case OWL_DSS_COLOR_ABGR8888:
	case OWL_DSS_COLOR_XBGR8888:
		hw_format = 6;
		break;
	case OWL_DSS_COLOR_ARGB8888:
	case OWL_DSS_COLOR_XRGB8888:
		hw_format = 7;
		break;

	case OWL_DSS_COLOR_BGRA1555:
	case OWL_DSS_COLOR_BGRX1555:
		hw_format = 12;
		break;
	case OWL_DSS_COLOR_RGBA1555:
	case OWL_DSS_COLOR_RGBX1555:
		hw_format = 13;
		break;
	case OWL_DSS_COLOR_ABGR1555:
	case OWL_DSS_COLOR_XBGR1555:
		hw_format = 14;
		break;
	case OWL_DSS_COLOR_ARGB1555:
	case OWL_DSS_COLOR_XRGB1555:
		hw_format = 15;
		break;

	case OWL_DSS_COLOR_NV12:
		hw_format = 8;
		break;
	case OWL_DSS_COLOR_NV21:
		hw_format = 9;
		break;
	case OWL_DSS_COLOR_YVU420:	/* switch U/V address */
	case OWL_DSS_COLOR_YUV420:
		hw_format = 10;
		break;

	default:
		BUG();
		break;
	}

	return hw_format;

}

static void __video_fb_addr_set(struct owl_de_video *video,
				struct owl_de_video_info *info)
{
	/*
	 * set fb addr according to color mode.
	 */

	/* OWL_DSS_COLOR_YVU420(DRM_FORMAT_YVU420), U/V switch. */
	if (info->color_mode == OWL_DSS_COLOR_YVU420 && info->n_planes == 3) {
		de_writel(info->addr[0], DE_OVL_BA0(video->id));
		de_writel(info->addr[2], DE_OVL_BA1UV(video->id));
		de_writel(info->addr[1], DE_OVL_BA2V(video->id));
	} else {
		if (info->n_planes > 0)
			de_writel(info->addr[0], DE_OVL_BA0(video->id));

		if (info->n_planes > 1)
			de_writel(info->addr[1], DE_OVL_BA1UV(video->id));

		if (info->n_planes > 2)
			de_writel(info->addr[2], DE_OVL_BA2V(video->id));
	}
}

static void __video_pitch_set(struct owl_de_video *video,
			      enum owl_color_mode color_mode, uint32_t *pitch)
{
	uint32_t val;

	if (owl_dss_color_is_rgb(color_mode))
		val = pitch[0] / 8;
	else if (color_mode == OWL_DSS_COLOR_YUV420)
		val = (pitch[0] / 8) | ((pitch[1] / 8) << 11)
			| ((pitch[2] / 8) << 21);
	else if (color_mode == OWL_DSS_COLOR_YVU420)/* switch pitch1 & pitch2 */
		val = (pitch[0] / 8) | ((pitch[2] / 8) << 11)
			| ((pitch[1] / 8) << 21);
	else
		val = (pitch[0] / 8) | ((pitch[1] / 8) << 11);

	de_writel(val, DE_OVL_STR(video->id));
}

static void __video_format_set(struct owl_de_video *video,
			       enum owl_color_mode color_mode)
{
	int hw_format = 0;
	uint32_t val;

	hw_format = __de_color_mode_to_hw_mode(color_mode);
	de_debug("%s, color_mode = 0x%x, hw_format = 0x%x\n",
		 __func__, color_mode, hw_format);

	val = de_readl(DE_OVL_CFG(video->id));

	val = REG_SET_VAL(val, hw_format, DE_OVL_CFG_FMT_END_BIT,
			  DE_OVL_CFG_FMT_BEGIN_BIT);

	if (owl_dss_color_is_rgb(color_mode)) {
		val = REG_SET_VAL(val, 0x0, 29, 28);
	} else {
		/* Using BT709_FULL_RANGE for YUV format */

		val = REG_SET_VAL(val, 0x1, 28, 28);	/* BT709 */
		val = REG_SET_VAL(val, 0x0, 29, 29);	/* NO quantization */
	}

	de_writel(val, DE_OVL_CFG(video->id));

	/*
	 * bypass CSC for RGB format
	 */
	val = de_readl(DE_OVL_CSC(video->id));
	val = REG_SET_VAL(val, owl_dss_color_is_rgb(color_mode),
			  DE_OVL_CSC_BYPASS_BIT, DE_OVL_CSC_BYPASS_BIT);
	de_writel(val, DE_OVL_CSC(video->id));
}

static void __video_rotate_set(struct owl_de_video *video, int rotation)
{
	uint32_t val;

	de_debug("%s, video %d, rotation %d\n", __func__, video->id, rotation);

	BUG_ON(rotation != 0 && rotation != 1 &&
	       rotation != 2 && rotation != 3);

	val = de_readl(DE_OVL_CFG(video->id));

	val = REG_SET_VAL(val, rotation, 21, 20);
	de_writel(val, DE_OVL_CFG(video->id));
}

static void __video_isize_set(struct owl_de_video *video,
			      uint32_t width, uint32_t height)
{
	uint32_t val;

	de_debug("%s, video %d, %dx%d\n", __func__, video->id, width, height);

	BUG_ON((width > DE_PATH_SIZE_WIDTH) || (height > DE_PATH_SIZE_HEIGHT));

	val = REG_VAL(height - 1, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(width - 1, DE_PATH_SIZE_WIDTH_END_BIT,
		      DE_PATH_SIZE_WIDTH_BEGIN_BIT);

	de_writel(val, DE_OVL_ISIZE(video->id));
}

static void __video_osize_set(struct owl_de_video *video,
			      uint32_t width, uint32_t height)
{
	uint32_t val;

	de_debug("%s, video %d, %dx%d\n", __func__, video->id, width, height);

	BUG_ON((width > DE_PATH_SIZE_WIDTH) || (height > DE_PATH_SIZE_HEIGHT));

	val = REG_VAL(height - 1, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(width - 1, DE_PATH_SIZE_WIDTH_END_BIT,
		      DE_PATH_SIZE_WIDTH_BEGIN_BIT);

	de_writel(val, DE_OVL_OSIZE(video->id));
}

static void __video_position_set(struct owl_de_path *path,
				 struct owl_de_video *video,
				 uint32_t x_pos, uint32_t y_pos)
{
	uint32_t val;

	de_debug("%s, video %d, (%d, %d)\n", __func__, video->id, x_pos, y_pos);

	BUG_ON((x_pos > DE_PATH_SIZE_WIDTH) || (y_pos > DE_PATH_SIZE_HEIGHT));

	val = REG_VAL(y_pos, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(x_pos, DE_PATH_SIZE_WIDTH_END_BIT,
		      DE_PATH_SIZE_WIDTH_BEGIN_BIT);

	de_writel(val, DE_OVL_COOR(path->id, video->id));
}

static void __video_set_scal_coef(struct owl_de_video *video,
				  uint8_t scale_mode)
{
	de_debug("%s, video %d, scale_mode %d\n", __func__,
	      video->id, scale_mode);

	switch (scale_mode) {
	case DE_SCLCOEF_ZOOMIN:
		de_writel(0x00004000, DE_OVL_SCOEF0(video->id));
		de_writel(0xFF073EFC, DE_OVL_SCOEF1(video->id));
		de_writel(0xFE1038FA, DE_OVL_SCOEF2(video->id));
		de_writel(0xFC1B30F9, DE_OVL_SCOEF3(video->id));
		de_writel(0xFA2626FA, DE_OVL_SCOEF4(video->id));
		de_writel(0xF9301BFC, DE_OVL_SCOEF5(video->id));
		de_writel(0xFA3810FE, DE_OVL_SCOEF6(video->id));
		de_writel(0xFC3E07FF, DE_OVL_SCOEF7(video->id));
		break;

	case DE_SCLCOEF_HALF_ZOOMOUT:
		de_writel(0x00004000, DE_OVL_SCOEF0(video->id));
		de_writel(0x00083800, DE_OVL_SCOEF1(video->id));
		de_writel(0x00103000, DE_OVL_SCOEF2(video->id));
		de_writel(0x00182800, DE_OVL_SCOEF3(video->id));
		de_writel(0x00202000, DE_OVL_SCOEF4(video->id));
		de_writel(0x00281800, DE_OVL_SCOEF5(video->id));
		de_writel(0x00301000, DE_OVL_SCOEF6(video->id));
		de_writel(0x00380800, DE_OVL_SCOEF7(video->id));
		break;

	case DE_SCLCOEF_SMALLER_ZOOMOUT:
		de_writel(0x00102010, DE_OVL_SCOEF0(video->id));
		de_writel(0x02121E0E, DE_OVL_SCOEF1(video->id));
		de_writel(0x04141C0C, DE_OVL_SCOEF2(video->id));
		de_writel(0x06161A0A, DE_OVL_SCOEF3(video->id));
		de_writel(0x08181808, DE_OVL_SCOEF4(video->id));
		de_writel(0x0A1A1606, DE_OVL_SCOEF5(video->id));
		de_writel(0x0C1C1404, DE_OVL_SCOEF6(video->id));
		de_writel(0x0E1E1202, DE_OVL_SCOEF7(video->id));
		break;

	default:
		BUG();
	}
}

static void __video_scaling_set(struct owl_de_video *video,
				uint32_t width, uint32_t height,
				uint32_t out_width, uint32_t out_height)
{
	uint8_t scale_mode;
	uint16_t w_factor, h_factor;
	uint16_t factor;
	uint32_t val = 0;

	de_debug("%s, video %d, %dx%d->%dx%d\n", __func__, video->id,
	      width, height, out_width, out_height);

	w_factor = (width * 8192 +  out_width - 1) / out_width;
	h_factor = (height * 8192 + out_height - 1) / out_height;

	val = REG_SET_VAL(val, h_factor, 31, 16);
	val |= REG_SET_VAL(val, w_factor, 15, 0);

	de_writel(val, DE_OVL_SR(video->id));

	factor = (width * height * 10) / (out_width * out_height);
	if (factor <= 10)
		scale_mode = DE_SCLCOEF_ZOOMIN;
	else if (factor <= 20)
		scale_mode = DE_SCLCOEF_HALF_ZOOMOUT;
	else if (factor > 20)
		scale_mode = DE_SCLCOEF_SMALLER_ZOOMOUT;

	__video_set_scal_coef(video, scale_mode);
}

static void __video_alpha_set(struct owl_de_path *path,
			      struct owl_de_video *video,
			      enum owl_blending_type blending, uint8_t alpha)
{
	uint32_t val;

	de_debug("%s, blending %d, alpha %d\n", __func__, blending, alpha);

	val = de_readl(DE_OVL_ALPHA_CFG(path->id, video->id));

	/* set global alpha */
	val = REG_SET_VAL(val, alpha, DE_OVL_ALPHA_CFG_VALUE_END_BIT,
			  DE_OVL_ALPHA_CFG_VALUE_BEGIN_BIT);

	if (blending == OWL_BLENDING_NONE)
		val = REG_SET_VAL(val, 0, DE_OVL_ALPHA_CFG_ENABLE_END_BIT,
				  DE_OVL_ALPHA_CFG_ENABLE_BEGIN_BIT);
	else if (blending == OWL_BLENDING_PREMULT)
		val = REG_SET_VAL(val, 2, DE_OVL_ALPHA_CFG_ENABLE_END_BIT,
				  DE_OVL_ALPHA_CFG_ENABLE_BEGIN_BIT);
	else
		val = REG_SET_VAL(val, 1, DE_OVL_ALPHA_CFG_ENABLE_END_BIT,
				  DE_OVL_ALPHA_CFG_ENABLE_BEGIN_BIT);

	de_writel(val, DE_OVL_ALPHA_CFG(path->id, video->id));
}

static void __video_csc_set(struct owl_de_video *video)
{
	uint32_t val;

	uint32_t bri, con, sat;

	struct owl_de_video_info *info = &video->info;

	/*if brightness not adjust or is default value ,return and open bypass csc*/
	if(info->brightness == 0 || info->brightness == 128)
	{
		val = de_readl(DE_OVL_CSC(video->id));
		val = REG_SET_VAL(val, 1, DE_OVL_CSC_BYPASS_BIT, DE_OVL_CSC_BYPASS_BIT);
		de_writel(val, DE_OVL_CSC(video->id));
	}else{
		/* do not bypass CSC */
		val = de_readl(DE_OVL_CSC(video->id));
		val = REG_SET_VAL(val, 0, DE_OVL_CSC_BYPASS_BIT, DE_OVL_CSC_BYPASS_BIT);
		de_writel(val, DE_OVL_CSC(video->id));
	}

	/* 0 ~ 255 */
	bri = info->brightness;

	/* 0~200-->0~14 */
	con = (info->contrast * 14 + 100) / 200;
	if (con > 14)
		con = 14;

	sat = (info->saturation * 14 + 100) / 200;
	if (sat > 14)
		sat = 14;
	val = de_readl(DE_OVL_CSC(video->id));
	val = REG_SET_VAL(val, bri, 15, 8);
	val = REG_SET_VAL(val, sat, 7, 4);
	val = REG_SET_VAL(val, con, 3, 0);
	de_writel(val, DE_OVL_CSC(video->id));
};

static void de_s900_video_apply_info(struct owl_de_video *video)
{
	uint16_t outw, outh;
	struct owl_de_video_info *info = &video->info;

	de_debug("%s, video %d, dirty %d\n", __func__, video->id, video->dirty);

	__de_mmu_enable(video, info->mmu_enable);

	outw = (info->out_width == 0 ? info->width : info->out_width);
	outh = (info->out_height == 0 ? info->height : info->out_height);

	__video_fb_addr_set(video, info);
	__video_pitch_set(video, info->color_mode, info->pitch);
	__video_format_set(video, info->color_mode);
	__video_rotate_set(video, info->rotation);
	__video_isize_set(video, info->width, info->height);
	__video_osize_set(video, outw, outh);
	__video_scaling_set(video, info->width, info->height, outw, outh);
	__video_position_set(video->path, video, info->pos_x, info->pos_y);


	/* enable global aplha to emulate RGBX */
	if (info->color_mode == OWL_DSS_COLOR_RGBX8888 ||
	    info->color_mode == OWL_DSS_COLOR_BGRX8888 ||
	    info->color_mode == OWL_DSS_COLOR_XRGB8888 ||
	    info->color_mode == OWL_DSS_COLOR_XBGR8888) {
		info->blending = OWL_BLENDING_NONE;
		info->alpha = 0xff;
	}
	__video_alpha_set(video->path, video, info->blending, info->alpha);
	__video_csc_set(video);
}

static struct owl_de_video_ops de_s900_video_ops = {
	.apply_info = de_s900_video_apply_info,
};

#define S900_SUPPORTED_COLORS (OWL_DSS_COLOR_RGB565 | OWL_DSS_COLOR_BGR565 \
			| OWL_DSS_COLOR_BGRA8888 | OWL_DSS_COLOR_RGBA8888 \
			| OWL_DSS_COLOR_ABGR8888 | OWL_DSS_COLOR_ARGB8888 \
			| OWL_DSS_COLOR_BGRX8888 | OWL_DSS_COLOR_RGBX8888 \
			| OWL_DSS_COLOR_XBGR8888 | OWL_DSS_COLOR_XRGB8888 \
			| OWL_DSS_COLOR_BGRA1555 | OWL_DSS_COLOR_RGBA1555 \
			| OWL_DSS_COLOR_ABGR1555 | OWL_DSS_COLOR_ARGB1555 \
			| OWL_DSS_COLOR_BGRX1555 | OWL_DSS_COLOR_RGBX1555 \
			| OWL_DSS_COLOR_XBGR1555 | OWL_DSS_COLOR_XRGB1555 \
			| OWL_DSS_COLOR_NV12 | OWL_DSS_COLOR_NV21 \
			| OWL_DSS_COLOR_YVU420 | OWL_DSS_COLOR_YUV420)


#define DE_S900_CAPACITIES {					\
	.supported_colors	= S900_SUPPORTED_COLORS,	\
	.pitch_align		= 8,				\
	.address_align		= 64,				\
	.rgb_limits = {						\
		.input_width	= {33, 2560},			\
		.input_height	= {4, 2560},			\
		.output_width	= {5, 4096},			\
		.output_height	= {1, 4096},			\
		.scaling_width	= {40, 80},			\
		.scaling_height	= {40, 80},			\
	},							\
	.yuv_limits = {						\
		.input_width	= {129, 4096},			\
		.input_height	= {4, 4096},			\
		.output_width	= {9, 4096},			\
		.output_height	= {1, 4096},			\
		.scaling_width	= {40, 80},			\
		.scaling_height	= {40, 80},			\
	},							\
}

/* need 2 video layers at most in boot */
static struct owl_de_video de_s900_videos[] = {
	{
		.id			= 3,
		.name			= "video3",
		.capacities		= DE_S900_CAPACITIES,
		.ops			= &de_s900_video_ops,
	},
	{
		.id			= 2,
		.name			= "video2",
		.capacities		= DE_S900_CAPACITIES,
		.ops			= &de_s900_video_ops,
	},
	{
		.id			= 1,
		.name			= "video1",
		.capacities		= DE_S900_CAPACITIES,
		.ops			= &de_s900_video_ops,
	},
	{
		.id			= 0,
		.name			= "video0",
		.capacities		= DE_S900_CAPACITIES,
		.ops			= &de_s900_video_ops,
	},
};


/*===================================================================
 *			S900 DE video layer
 *==================================================================*/

static int de_s900_device_power_on(struct owl_de_device *de)
{
	uint32_t val = 0;

	struct de_s900_pdata *pdata = de->pdata;

	void __iomem *shareram = ioremap(0xE0240004, 4);
	void __iomem *dmm_axi_de_priority = ioremap(0xe0290074, 4);
	void __iomem *dmm_axi_normal_priority = ioremap(0xe029002c, 4);

	de_info("%s\n", __func__);

	/*
	 * DECLK:
	 * source is ASSIST_PLL(500MHz),
	 * DE_CLK1/DE_CLK2/DE_CLK3, divider is 1, 500MHz
	 * TODO
	 */
	val = readl(pdata->cmu_declk_reg);
	val = REG_SET_VAL(val, 0, 2, 0);
	val = REG_SET_VAL(val, 0, 6, 4);
	val = REG_SET_VAL(val, 0, 10, 8);
	writel(val, pdata->cmu_declk_reg);

	clk_set_parent(pdata->clk, pdata->parent_clk);
	clk_prepare_enable(pdata->clk);

	/*
	 * some specail settings
	 */

	/* enable share mem */
	val = readl(shareram);
	val = REG_SET_VAL(val, 0xf, 3, 0);
	writel(val, shareram);

	de_writel(0x00001f1f, DE_MAX_OUTSTANDING);
	de_writel(0xcccc, DE_QOS);

	writel(0x0, dmm_axi_de_priority);
	val = readl(dmm_axi_normal_priority);
	val &= ~(1 << 4);
	writel(val, dmm_axi_normal_priority);
	val |= (1 << 4);
	writel(val, dmm_axi_normal_priority);

	iounmap(shareram);
	iounmap(dmm_axi_de_priority);
	iounmap(dmm_axi_normal_priority);

	return 0;
}

static int de_s900_device_power_off(struct owl_de_device *de)
{
	struct de_s900_pdata *pdata = de->pdata;

	de_info("%s\n", __func__);

	clk_disable_unprepare(pdata->clk);
	return 0;
}

static int de_s900_device_mmu_config(struct owl_de_device *de,
				     uint32_t base_addr)
{
	de_debug("%s\n", __func__);

	de_writel(base_addr, DE_MMU_BASE);
	return 0;
}

static void de_s900_device_dump_regs(struct owl_de_device *de)
{
	int i = 0;

	#define DUMPREG(r) de_info("%08x ~ %08x ~ %s\n", r, de_readl(r), #r)

	DUMPREG(DE_IRQSTATUS);
	DUMPREG(DE_IRQENABLE);
	DUMPREG(DE_MAX_OUTSTANDING);
	DUMPREG(DE_MMU_EN);
	DUMPREG(DE_MMU_BASE);
	DUMPREG(DE_OUTPUT_CON);
	DUMPREG(DE_OUTPUT_STAT);
	DUMPREG(DE_PATH_DITHER);

	for (i = 0; i < 2; i++) {
		de_info("\npath %d ------------------>\n", i);

		DUMPREG(DE_PATH_CTL(i));
		DUMPREG(DE_PATH_FCR(i));
		DUMPREG(DE_PATH_EN(i));
		DUMPREG(DE_PATH_BK(i));
		DUMPREG(DE_PATH_SIZE(i));
		DUMPREG(DE_PATH_GAMMA_IDX(i));
		DUMPREG(DE_PATH_GAMMA_RAM(i));
	}

	for (i = 0; i < 4; i++) {
		de_info("\nlayer %d ------------------>\n", i);

		DUMPREG(DE_OVL_CFG(i));
		DUMPREG(DE_OVL_ISIZE(i));
		DUMPREG(DE_OVL_OSIZE(i));
		DUMPREG(DE_OVL_SR(i));
		DUMPREG(DE_OVL_SCOEF0(i));
		DUMPREG(DE_OVL_SCOEF1(i));
		DUMPREG(DE_OVL_SCOEF2(i));
		DUMPREG(DE_OVL_SCOEF3(i));
		DUMPREG(DE_OVL_SCOEF4(i));
		DUMPREG(DE_OVL_SCOEF5(i));
		DUMPREG(DE_OVL_SCOEF6(i));
		DUMPREG(DE_OVL_SCOEF7(i));
		DUMPREG(DE_OVL_BA0(i));
		DUMPREG(DE_OVL_BA1UV(i));
		DUMPREG(DE_OVL_BA2V(i));
		DUMPREG(DE_OVL_3D_RIGHT_BA0(i));
		DUMPREG(DE_OVL_3D_RIGHT_BA1UV(i));
		DUMPREG(DE_OVL_3D_RIGHT_BA2V(i));

		DUMPREG(DE_OVL_STR(i));
		DUMPREG(DE_OVL_CRITICAL_CFG(i));
		DUMPREG(DE_OVL_REMAPPING(i));
		DUMPREG(DE_OVL_CSC(i));
		DUMPREG(DE_OVL_COOR(0, i));
		DUMPREG(DE_OVL_COOR(1, i));
		DUMPREG(DE_OVL_ALPHA_CFG(0, i));
		DUMPREG(DE_OVL_ALPHA_CFG(1, i));
	}
	#undef DUMPREG
}

static inline void __de_s900_backup_regs(struct owl_de_device_regs *p, int reg)
{
	p->reg      = reg;
	p->value    = de_readl(reg);
}

static void de_s900_device_backup_regs(struct owl_de_device *de)
{
	int i = 0, cnt = 0;
	struct owl_de_device_regs *regs = de->regs;

	de_info("%s\n", __func__);

	__de_s900_backup_regs(&regs[cnt++], DE_IRQENABLE);
	__de_s900_backup_regs(&regs[cnt++], DE_MAX_OUTSTANDING);
	__de_s900_backup_regs(&regs[cnt++], DE_OUTPUT_CON);
	__de_s900_backup_regs(&regs[cnt++], DE_WB_CON);
	__de_s900_backup_regs(&regs[cnt++], DE_WB_ADDR);
	__de_s900_backup_regs(&regs[cnt++], DE_MMU_BASE);
	__de_s900_backup_regs(&regs[cnt++], DE_MMU_EN);

	for (i = 0; i < 2; i++) {
		__de_s900_backup_regs(&regs[cnt++], DE_PATH_CTL(i));
		__de_s900_backup_regs(&regs[cnt++], DE_PATH_EN(i));
		__de_s900_backup_regs(&regs[cnt++], DE_PATH_BK(i));
		__de_s900_backup_regs(&regs[cnt++], DE_PATH_SIZE(i));
		__de_s900_backup_regs(&regs[cnt++], DE_PATH_GAMMA_IDX(i));
		__de_s900_backup_regs(&regs[cnt++], DE_PATH_GAMMA_RAM(i));
	}

	for (i = 0; i < 4; i++) {
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_CFG(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_ISIZE(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_OSIZE(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SR(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF0(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF1(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF2(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF3(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF4(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF5(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF6(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_SCOEF7(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_BA0(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_BA1UV(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_BA2V(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_3D_RIGHT_BA0(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_3D_RIGHT_BA1UV(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_3D_RIGHT_BA2V(i));

		__de_s900_backup_regs(&regs[cnt++], DE_OVL_STR(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_CRITICAL_CFG(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_REMAPPING(i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_COOR(0, i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_COOR(1, i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_ALPHA_CFG(0, i));
		__de_s900_backup_regs(&regs[cnt++], DE_OVL_ALPHA_CFG(1, i));
	}

	de->regs_cnt = cnt;
}


static void de_s900_device_restore_regs(struct owl_de_device *de)
{
	int i;

	de_info("%s\n", __func__);

	for (i = 0; i < de->regs_cnt; i++)
		de_writel(de->regs[i].value, de->regs[i].reg);
}

static struct owl_de_device_ops de_s900_device_ops = {
	.power_on = de_s900_device_power_on,
	.power_off = de_s900_device_power_off,

	.mmu_config = de_s900_device_mmu_config,

	.dump_regs = de_s900_device_dump_regs,
	.backup_regs = de_s900_device_backup_regs,
	.restore_regs = de_s900_device_restore_regs,
};

static struct owl_de_device de_s900_device = {
	.hw_id			= DE_HW_ID_S900,

	.num_paths		= ARRAY_SIZE(de_s900_paths),
	.paths			= de_s900_paths,

	.num_videos		= ARRAY_SIZE(de_s900_videos),
	.videos			= de_s900_videos,

	.ops			= &de_s900_device_ops,
};


/*============================================================================
 *			platform driver
 *==========================================================================*/

static const struct of_device_id de_s900_match[] = {
	{
		.compatible	= "actions,s900-de",
	},
};

static int __init de_s900_probe(struct platform_device *pdev)
{
	int				ret = 0;

	struct device			*dev = &pdev->dev;
	const struct of_device_id	*match;

	struct resource			*res;
	struct de_s900_pdata		*pdata;

	dev_info(dev, "%s, pdev = 0x%p\n", __func__, pdev);

	match = of_match_device(of_match_ptr(de_s900_match), dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	de_s900_device.pdev = pdev;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		dev_err(dev, "No Mem for pdata\n");
		return -ENOMEM;
	}

	de_s900_device.regs
		= devm_kzalloc(dev, sizeof(*de_s900_device.regs) * 256,
			       GFP_KERNEL);
	if (de_s900_device.regs == NULL) {
		dev_err(dev, "No Mem for de_s900_device.regs\n");
		return -ENOMEM;
	}

	/*
	 * parse our special resources
	 */

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmu_declk");
	if (!res) {
		dev_err(dev, "get 'cmu_declk' error\n");
		return -EINVAL;
	}
	pdata->cmu_declk_reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->cmu_declk_reg)) {
		dev_err(dev, "can't remap IORESOURCE_MEM\n");
		return -EINVAL;
	}
	dev_dbg(dev, "cmu_declk: 0x%p\n", pdata->cmu_declk_reg);

	pdata->clk = devm_clk_get(dev, "clk");
	pdata->parent_clk = devm_clk_get(dev, "clk_parent");
	if (IS_ERR(pdata->clk) || IS_ERR(pdata->parent_clk)) {
		dev_err(dev, "can't get de clk or de parent_clk\n");
		return -EINVAL;
	}

	de_s900_device.pdata = pdata;

	ret = owl_de_register(&de_s900_device);
	if (ret < 0) {
		dev_err(dev, "register s900 de device failed(%d)\n", ret);
		return ret;
	}

	return 0;
}

static int __exit de_s900_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s, pdev = 0x%p\n", __func__, pdev);

	/* TODO */
	return 0;
}

static int de_s900_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	owl_de_generic_suspend(dev);

	return 0;
}

static int de_s900_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	owl_de_generic_resume(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(de_s900_pm_ops, de_s900_suspend, de_s900_resume);

static struct platform_driver de_s900_platform_driver = {
	.probe			= de_s900_probe,
	.remove			= __exit_p(de_s900_remove),
	.driver = {
		.name		= "de-s900",
		.owner		= THIS_MODULE,
		.of_match_table	= de_s900_match,
		.pm		= &de_s900_pm_ops,
	},
};

static int __init owl_de_s900_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&de_s900_platform_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	return 0;
}

static void __exit owl_de_s900_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&de_s900_platform_driver);
}

module_init(owl_de_s900_init);
module_exit(owl_de_s900_exit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL S900 DE Driver");
MODULE_LICENSE("GPL v2");
