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
#define DEBUGX
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
#include <linux/delay.h>

#include <video/owl_dss.h>

#include "de-s700.h"

/*
 * some useful definition for awlful scaler
 */

#define ML_ID_HAS_NO_SCALER		(2)

static int				ml_id_for_scaler0;
static int				ml_id_for_scaler1;

static struct owl_de_device		de_s700_device;

/* DE_IRQENABLE cannot be read, use a static variable to hold it */
static uint32_t				irq_enable_val;
static spinlock_t			irq_enable_val_lock;


/*
 * NOTE:
 *	the following macros must be used after
 *	executing 'owl_de_register'
 */
#define de_readl(idx)		readl(de_s700_device.base + (idx))
#define de_writel(val, idx)	writel(val, de_s700_device.base + (idx))

#define de_debug(format, ...) \
	dev_dbg(&de_s700_device.pdev->dev, format, ## __VA_ARGS__)
#define de_info(format, ...) \
	dev_info(&de_s700_device.pdev->dev, format, ## __VA_ARGS__)
#define de_err(format, ...) \
	dev_err(&de_s700_device.pdev->dev, format, ## __VA_ARGS__)

#define ML_ID(video)			((video)->id / 4)
#define SL_ID(video)			((video)->id % 4)

struct de_s700_pdata {
	struct clk			*clk;
	struct clk			*parent_clk;
};

/*
 * Assign scaler1 to 'ml_id'
 */

#define CAPACITIES_COPY(to, from)				\
	memcpy(&de_s700_device.videos[(to)].capacities,		\
	       &de_s700_device.videos[(from)].capacities,	\
	       sizeof(struct owl_de_video_capacities))

static void de_s700_assign_scaler1(int ml_id, bool enable)
{
	int i;
	uint32_t val;

	if (!enable) {
		/* disable scaler 1 */
		val = de_readl(DE_SCALER_CFG(1));
		val = REG_SET_VAL(val, 0, DE_SCALER_CFG_ENABLE_BIT,
				  DE_SCALER_CFG_ENABLE_BIT);
		de_writel(val, DE_SCALER_CFG(1));

		/* set capacities to ML_ID_HAS_NO_SCALER */
		for (i = 0; i < 4; i++)
			CAPACITIES_COPY(4 * ml_id + i, 4 * ML_ID_HAS_NO_SCALER + i);

		ml_id_for_scaler1 = -1;
	} else {
		/* assign scaler 1 to ml_id and enable it */
		val = de_readl(DE_SCALER_CFG(1));
		val = REG_SET_VAL(val, 1, DE_SCALER_CFG_ENABLE_BIT,
				  DE_SCALER_CFG_ENABLE_BIT);
		val = REG_SET_VAL(val, ml_id,
				  DE_SCALER_CFG_SEL_END_BIT,
				  DE_SCALER_CFG_SEL_BEGIN_BIT);
		de_writel(val, DE_SCALER_CFG(1));

		/* set capacities to ml_id_for_scaler0 */
		for (i = 0; i < 4; i++)
			CAPACITIES_COPY(4 * ml_id + i, 4 * ml_id_for_scaler0 + i);

		ml_id_for_scaler1 = ml_id;
	}
}

/*===================================================================
 *			S700 DE path
 *==================================================================*/

static int de_s700_path_enable(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	de_debug("%s, path %d enable %d\n", __func__, path->id, enable);

	if (owl_de_is_ott() && path->id == 0 && enable &&
	    (path->info.width >= 3840 || path->info.height >= 2160))
		de_s700_assign_scaler1(1, false);

	val = de_readl(DE_PATH_EN(path->id));
	val = REG_SET_VAL(val, enable, DE_PATH_ENABLE_BIT, DE_PATH_ENABLE_BIT);
	de_writel(val, DE_PATH_EN(path->id));

	if (owl_de_is_ott() && path->id == 0 && !enable)
		de_s700_assign_scaler1(1, true);

	return 0;
}

static bool de_s700_path_is_enabled(struct owl_de_path *path)
{
	uint32_t val;

	val = de_readl(DE_PATH_EN(path->id));

	return REG_GET_VAL(val, DE_PATH_ENABLE_BIT, DE_PATH_ENABLE_BIT) == 1;
}

static int de_s700_path_attach(struct owl_de_path *path,
			       struct owl_de_video *video)
{
	uint32_t val;

	de_debug("%s, attach video%d to path%d\n",
		 __func__, video->id, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	/* enable sub layer in macro layer */
	val = de_readl(DE_ML_CFG(ML_ID(video)));
	val = REG_SET_VAL(val, 1, SL_ID(video), SL_ID(video));
	de_writel(val, DE_ML_CFG(ML_ID(video)));

	/* enable macro layer in path */
	val = de_readl(DE_PATH_CTL(path->id));
	val = REG_SET_VAL(val, 1,
			  DE_PATH_ML_EN_BEGIN_BIT + ML_ID(video),
			  DE_PATH_ML_EN_BEGIN_BIT + ML_ID(video));
	de_writel(val, DE_PATH_CTL(path->id));

	return 0;
}

static int de_s700_path_detach(struct owl_de_path *path,
			       struct owl_de_video *video)
{
	uint32_t val;

	de_debug("%s, attach video%d to path%d\n",
		 __func__, video->id, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	/*
	 * disable sub layer in macro layer
	 */
	val = de_readl(DE_ML_CFG(ML_ID(video)));
	val = REG_SET_VAL(val, 0, SL_ID(video), SL_ID(video));
	de_writel(val, DE_ML_CFG(ML_ID(video)));


	if ((val & 0xf) == 0) {
		/* all sub layer is disabled, disable macro layer in path */
		val = de_readl(DE_PATH_CTL(path->id));
		val = REG_SET_VAL(val, 0,
				  DE_PATH_ML_EN_BEGIN_BIT + ML_ID(video),
				  DE_PATH_ML_EN_BEGIN_BIT + ML_ID(video));
		de_writel(val, DE_PATH_CTL(path->id));
	}

	return 0;
}

/* TODO */
static int de_s700_path_detach_all(struct owl_de_path *path)
{
	int i;
	uint32_t val, val2;

	de_debug("%s, path %d\n", __func__, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	val = de_readl(DE_PATH_CTL(path->id));

	for (i = 0; i < 3; i++) {
		if ((val & (0x1 << i)) != 0) {
			/* macro layer i is disabled in path,
			 * disable its sublayers */
			val2 = de_readl(DE_ML_CFG(i));
			val2 = REG_SET_VAL(val2, 0, DE_ML_EN_END_BIT,
					   DE_ML_EN_BEGIN_BIT);
			de_writel(val2, DE_ML_CFG(i));
		}
	}

	/* disable this path's macro layers */
	val = REG_SET_VAL(val, 0, DE_PATH_ML_EN_END_BIT,
			  DE_PATH_ML_EN_BEGIN_BIT);
	de_writel(val, DE_PATH_CTL(path->id));

	return 0;
}

static void __path_display_type_set(struct owl_de_path *path,
				    enum owl_display_type type)
{
	uint32_t val;

	de_debug("%s, path %d type %d\n", __func__, path->id, type);

	val = de_readl(DE_OUTPUT_CON);

	if (path->id == 1) {
		/* LCD */
		switch (type) {
		case OWL_DISPLAY_TYPE_LCD:
			val = REG_SET_VAL(val, 1,
					  DE_OUTPUT_PATH2_DEVICE_END_BIT,
					  DE_OUTPUT_PATH2_DEVICE_BEGIN_BIT);
			break;
		case OWL_DISPLAY_TYPE_DSI:
			val = REG_SET_VAL(val, 0,
					  DE_OUTPUT_PATH2_DEVICE_END_BIT,
					  DE_OUTPUT_PATH2_DEVICE_BEGIN_BIT);
			break;
		default:
			BUG();
			break;
		}
	} else {
		/* digit */
		switch (type) {
		case OWL_DISPLAY_TYPE_HDMI:
			val = REG_SET_VAL(val, 0,
					  DE_OUTPUT_PATH1_DEVICE_END_BIT,
					  DE_OUTPUT_PATH1_DEVICE_BEGIN_BIT);
			break;
		case OWL_DISPLAY_TYPE_CVBS:
			val = REG_SET_VAL(val, 1,
					  DE_OUTPUT_PATH1_DEVICE_END_BIT,
					  DE_OUTPUT_PATH1_DEVICE_BEGIN_BIT);
			break;
		default:
			BUG();
			break;
		}
	}

	de_writel(val, DE_OUTPUT_CON);
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
		val = REG_SET_VAL(val, 0, DE_PATH_DITHER_MODE_END_BIT,
				  DE_PATH_DITHER_MODE_BEGIN_BIT);
		break;

	case DITHER_24_TO_16:
		val = REG_SET_VAL(val, 1, DE_PATH_DITHER_MODE_END_BIT,
				  DE_PATH_DITHER_MODE_BEGIN_BIT);
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

static void __path_output_format_set(struct owl_de_path *path, bool is_yuv)
{
	uint32_t val;
	de_debug("%s, path %d format :%s\n",
		__func__, path->id, is_yuv == 1 ? "yuv" : "rgb");

	val = de_readl(DE_PATH_CTL(path->id));

	if (is_yuv == DE_OUTPUT_FORMAT_YUV)
		val = REG_SET_VAL(val, 1, DE_PATH_CTL_RGB_YUV_EN_BIT,
				  DE_PATH_CTL_RGB_YUV_EN_BIT);
	else
		val = REG_SET_VAL(val, 0, DE_PATH_CTL_RGB_YUV_EN_BIT,
				  DE_PATH_CTL_RGB_YUV_EN_BIT);
	de_writel(val, DE_PATH_CTL(path->id));
}
static void __path_default_color_set(struct owl_de_path *path, uint32_t color)
{
	de_debug("%s, path %d color %x\n", __func__, path->id, color);

	de_writel(color, DE_PATH_BK(path->id));
}

static void de_s700_path_apply_info(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;

	de_debug("%s, path%d\n", __func__, path->id);

	__path_display_type_set(path, info->type);

	__path_size_set(path, info->width, info->height);

	__path_dither_set(path, info->dither_mode);

	__path_vmode_set(path, info->vmode);

	if (info->type == OWL_DISPLAY_TYPE_CVBS)
		__path_output_format_set(path, DE_OUTPUT_FORMAT_YUV);
	else
		__path_output_format_set(path, DE_OUTPUT_FORMAT_RGB);

	/* for test */
	__path_default_color_set(path, 0x0);
}

static void de_s700_path_set_fcr(struct owl_de_path *path)
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

static bool de_s700_path_is_fcr_set(struct owl_de_path *path)
{
	return !!(de_readl(DE_PATH_FCR(path->id)) & (1 << DE_PATH_FCR_BIT));
}

static void de_s700_path_preline_enable(struct owl_de_path *path, bool enable)
{
	unsigned long flags;

	de_debug("%s, path%d, enable %d\n", __func__, path->id, enable);

	spin_lock_irqsave(&irq_enable_val_lock, flags);

	if (enable)
		irq_enable_val |= (1 << path->id);
	else
		irq_enable_val &= ~(1 << path->id);

	de_writel(irq_enable_val, DE_IRQENABLE);

	spin_unlock_irqrestore(&irq_enable_val_lock, flags);
}

static bool de_s700_path_is_preline_enabled(struct owl_de_path *path)
{
	uint32_t val;
	unsigned long flags;

	/*
	 * IC BUG:
	 * can not read 'DE_IRQENABLE'
	 */
	spin_lock_irqsave(&irq_enable_val_lock, flags);
	val = irq_enable_val;
	spin_unlock_irqrestore(&irq_enable_val_lock, flags);

	return !!(val & (1 << path->id));
}

static bool de_s700_path_is_preline_pending(struct owl_de_path *path)
{
	return !!(de_readl(DE_IRQSTATUS) & (1 << path->id));
}

static void de_s700_path_clear_preline_pending(struct owl_de_path *path)
{
	de_writel((1 << path->id), DE_IRQSTATUS);
}

static bool de_s700_path_is_vb_valid(struct owl_de_path *path)
{
	return !!(de_readl(DE_IRQSTATUS) & (1 << (path->id + 8)));
}

static void __path_gamma_set(struct owl_de_path *path, uint32_t *gamma_val)
{
	bool is_busy;
	uint32_t idx, val;

	for (idx = 0; idx < (256 * 3 / 4); idx++) {
		/* write index */
		val = REG_SET_VAL(val, idx, DE_PATH_GAMMA_IDX_INDEX_END_BIT,
				DE_PATH_GAMMA_IDX_INDEX_BEGIN_BIT);
		de_writel(val, DE_PATH_GAMMA_IDX(path->id));

		/* write ram */
		de_writel(gamma_val[idx], DE_PATH_GAMMA_RAM(path->id));

	}

	/* write finish, clear write bit and index */
	val = de_readl(DE_PATH_GAMMA_IDX(path->id));
	val = REG_SET_VAL(val, 0, DE_PATH_GAMMA_IDX_INDEX_END_BIT,
					DE_PATH_GAMMA_IDX_INDEX_BEGIN_BIT);
	de_writel(val, DE_PATH_GAMMA_IDX(path->id));
}

static void de_s700_path_set_gamma_table(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;
	uint8_t gamma_data[256 * 3];
	int is_enabled;
	int i = 0;

	de_debug("gamma_r %d, gamma_g %d, gamma_b %d\n",
		info->gamma_r_val, info->gamma_g_val, info->gamma_b_val);

	if ((info->gamma_r_val < 0) || (info->gamma_g_val < 0)
			|| (info->gamma_b_val < 0))
		de_err("unavailable gamma val!");

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

static bool de_s700_path_is_gamma_enabled(struct owl_de_path *path)
{
	uint32_t val;

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	val = de_readl(DE_PATH_GAMMA_ENABLE(path->id));

	return REG_GET_VAL(val, DE_PATH_GAMMA_ENABLE_BIT,
			DE_PATH_GAMMA_ENABLE_BIT) == 1;
}

static int de_s700_path_enable_gamma(struct owl_de_path *path, bool enable)
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

static void de_s700_path_get_gamma_table(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;

}

static struct owl_de_path_ops de_s700_path_ops = {
	.enable = de_s700_path_enable,
	.is_enabled = de_s700_path_is_enabled,

	.attach = de_s700_path_attach,
	.detach = de_s700_path_detach,
	.detach_all = de_s700_path_detach_all,

	.apply_info = de_s700_path_apply_info,

	.set_fcr = de_s700_path_set_fcr,
	.is_fcr_set = de_s700_path_is_fcr_set,

	.preline_enable = de_s700_path_preline_enable,
	.is_preline_enabled = de_s700_path_is_preline_enabled,
	.is_preline_pending = de_s700_path_is_preline_pending,
	.clear_preline_pending = de_s700_path_clear_preline_pending,

	.set_gamma_table = de_s700_path_set_gamma_table,
	.get_gamma_table = de_s700_path_get_gamma_table,
	.gamma_enable = de_s700_path_enable_gamma,
	.is_gamma_enabled = de_s700_path_is_gamma_enabled,

	.is_vb_valid = de_s700_path_is_vb_valid,
};

static struct owl_de_path de_s700_paths[] = {
	{
		.id			= 0,
		.name			= "hdmi",
		.supported_displays	= OWL_DISPLAY_TYPE_HDMI,
		.ops			= &de_s700_path_ops,
	},
	{
		.id			= 1,
		.name			= "lcd",
		.supported_displays	= OWL_DISPLAY_TYPE_LCD
					| OWL_DISPLAY_TYPE_DSI
					| OWL_DISPLAY_TYPE_DUMMY,
		.ops			= &de_s700_path_ops,
	},
	{	/* a fake path, use same registers as HDMI */
		.id			= 0,
		.name			= "cvbs",
		.supported_displays	= OWL_DISPLAY_TYPE_CVBS,
		.ops			= &de_s700_path_ops,
	},
};


/*===================================================================
 *			S700 DE video layer
 *==================================================================*/
static int __de_mmu_enable(struct owl_de_video *video)
{
	uint32_t val;

	struct owl_de_video_info *info = &video->info;

	de_debug("%s, enable %d\n", __func__, info->mmu_enable);

	val = de_readl(DE_MMU_EN);
	val = REG_SET_VAL(val, info->mmu_enable, 0, 0);
	de_writel(val, DE_MMU_EN);

	return 0;
}

static int __de_color_mode_to_hw_mode(enum owl_color_mode color_mode)
{
	int hw_format = 0;

	switch (color_mode) {
	/* 565 is specail, same as DE */
	case OWL_DSS_COLOR_BGR565:
		hw_format = 9;
		break;
	case OWL_DSS_COLOR_RGB565:
		hw_format = 8;
		break;

	/* 8888 * 1555, reverse with DE */
	case OWL_DSS_COLOR_BGRA8888:
	case OWL_DSS_COLOR_BGRX8888:
		hw_format = 0;
		break;
	case OWL_DSS_COLOR_RGBA8888:
	case OWL_DSS_COLOR_RGBX8888:
		hw_format = 1;
		break;
	case OWL_DSS_COLOR_ABGR8888:
	case OWL_DSS_COLOR_XBGR8888:
		hw_format = 2;
		break;
	case OWL_DSS_COLOR_ARGB8888:
	case OWL_DSS_COLOR_XRGB8888:
		hw_format = 3;
		break;

	case OWL_DSS_COLOR_BGRA1555:
	case OWL_DSS_COLOR_BGRX1555:
		hw_format = 4;
		break;
	case OWL_DSS_COLOR_RGBA1555:
	case OWL_DSS_COLOR_RGBX1555:
		hw_format = 5;
		break;
	case OWL_DSS_COLOR_ABGR1555:
	case OWL_DSS_COLOR_XBGR1555:
		hw_format = 6;
		break;
	case OWL_DSS_COLOR_ARGB1555:
	case OWL_DSS_COLOR_XRGB1555:
		hw_format = 7;
		break;

	case OWL_DSS_COLOR_NV12:
		hw_format = 12;
		break;
	case OWL_DSS_COLOR_NV21:
		hw_format = 13;
		break;
	case OWL_DSS_COLOR_YVU420:	/* switch U/V address */
	case OWL_DSS_COLOR_YUV420:
		hw_format = 14;
		break;

	default:
		BUG();
		break;
	}

	return hw_format;

}

static void __video_fb_addr_set(struct owl_de_video *video)
{
	struct owl_de_video_info *info = &video->info;

	/*
	 * set fb addr according to color mode.
	 */

	if (owl_dss_color_is_rgb(info->color_mode)) {
		de_writel(info->addr[0], DE_SL_FB(ML_ID(video), SL_ID(video)));
		return;
	} else {
		if (SL_ID(video) != 0)
			return;
	}

	/*
	 * YUV
	 */

	if (info->color_mode == OWL_DSS_COLOR_YVU420 && info->n_planes == 3) {
		/* OWL_DSS_COLOR_YVU420(DRM_FORMAT_YVU420), U/V switch. */
		de_writel(info->addr[0], DE_SL_FB(ML_ID(video), 0));
		de_writel(info->addr[2], DE_SL_FB(ML_ID(video), 1));
		de_writel(info->addr[1], DE_SL_FB(ML_ID(video), 2));
	} else {
		if (info->n_planes > 0)
			de_writel(info->addr[0], DE_SL_FB(ML_ID(video), 0));

		if (info->n_planes > 1)
			de_writel(info->addr[1], DE_SL_FB(ML_ID(video), 1));

		if (info->n_planes > 2)
			de_writel(info->addr[2], DE_SL_FB(ML_ID(video), 2));
	}
}

static void __video_pitch_set(struct owl_de_video *video)
{
	uint32_t val;
	struct owl_de_video_info *info = &video->info;

	if (owl_dss_color_is_rgb(info->color_mode)) {
		val = info->pitch[0];
		de_writel(val, DE_SL_STR(ML_ID(video), SL_ID(video)));
	} else {
		val = info->pitch[0] | (info->pitch[1] << 14);
		de_writel(val, DE_SL_STR(ML_ID(video), 0));
	}
}

static void __video_format_set(struct owl_de_video *video)
{
	int hw_format = 0;
	uint32_t val;
	struct owl_de_video_info *info = &video->info;

	hw_format = __de_color_mode_to_hw_mode(info->color_mode);
	de_debug("%s, color_mode = 0x%x, hw_format = 0x%x\n",
		 __func__, info->color_mode, hw_format);

	val = de_readl(DE_SL_CFG(ML_ID(video), SL_ID(video)));

	val = REG_SET_VAL(val, hw_format, DE_SL_CFG_FMT_END_BIT,
			  DE_SL_CFG_FMT_BEGIN_BIT);

	de_writel(val, DE_SL_CFG(ML_ID(video), SL_ID(video)));


	/*
	 * config YUV convert format
	 */
	val = de_readl(DE_ML_CFG(ML_ID(video)));
	if (owl_dss_color_is_rgb(info->color_mode)) {
		val = REG_SET_VAL(val, 0x0, 29, 28);
	} else {
		/* Using BT709_FULL_RANGE for YUV format */

		val = REG_SET_VAL(val, 0x1, 28, 28);	/* BT709 */
		val = REG_SET_VAL(val, 0x0, 29, 29);	/* NO quantization */
	}
	de_writel(val, DE_ML_CFG(ML_ID(video)));
}

static void __video_rotate_set(struct owl_de_video *video)
{
	int rotation;
	uint32_t val;
	struct owl_de_video_info *info = &video->info;

	rotation = info->rotation;

	de_debug("%s, video %d, rotation %d\n", __func__, video->id, rotation);

	BUG_ON(rotation != 0 && rotation != 1 &&
	       rotation != 2 && rotation != 3);

	val = de_readl(DE_ML_CFG(ML_ID(video)));

	val = REG_SET_VAL(val, (rotation == 0 ? 0 : 1),
			  DE_ML_ROT180_BIT, DE_ML_ROT180_BIT);
	de_writel(val, DE_ML_CFG(ML_ID(video)));
}

static void __video_crop_set(struct owl_de_video *video)
{
	uint32_t val;

	int sl_width, sl_height;
	int ml_width, ml_height;
	int pri_width = 0, pri_height = 0;

	struct owl_de_video_info *info = &video->info;

	sl_width = info->width;
	sl_height = info->height;

	if (!owl_dss_color_is_rgb(info->color_mode) ||
	    info->is_original_scaled) {
		/*
		 * YUV layer or original scaled, monopolize a single macrolayer:
		 * Set macro layer size to crop size.
		 */
		ml_width = info->width;
		ml_height = info->height;
	} else {
		/*
		 * RGB layers share one macro layer:
		 * Set macro layer size to draw size(TODO, maybe improper).
		 */
		owl_panel_get_draw_size(video->path->panel,
					&ml_width, &ml_height);
		/*
		 * If all layers have no scale ,not YUV layers and have no overlap,
		 * so external display device just copy primary display size.
		 * and now we should set maclayer input size equal to primary display
		 * input size.
		 */
		if ((info->flag & OWL_VIDEO_FLAG_ADJUST_DRAW_SIZE) == OWL_VIDEO_FLAG_ADJUST_DRAW_SIZE) {
		    if (owl_panel_get_pri_panel_resolution(&pri_width, &pri_height)) {
		        ml_width = pri_width;
		        ml_height = pri_height;
		    }
		}
	}

	de_debug("%s, video %d, sl size %dx%d, ml size %dx%d\n", __func__,
		 video->id, sl_width, sl_height, ml_width, ml_height);

	BUG_ON((sl_width > DE_PATH_SIZE_WIDTH) ||
	       (sl_height > DE_PATH_SIZE_HEIGHT));
	BUG_ON((ml_width > DE_PATH_SIZE_WIDTH) ||
	       (ml_height > DE_PATH_SIZE_HEIGHT));

	val = REG_VAL(sl_height - 1, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(sl_width - 1, DE_PATH_SIZE_WIDTH_END_BIT,
		      DE_PATH_SIZE_WIDTH_BEGIN_BIT);
	de_writel(val, DE_SL_CROPSIZE(ML_ID(video), SL_ID(video)));

	val = REG_VAL(ml_height - 1, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(ml_width - 1, DE_PATH_SIZE_WIDTH_END_BIT,
		      DE_PATH_SIZE_WIDTH_BEGIN_BIT);
	de_writel(val, DE_ML_ISIZE(ML_ID(video)));
}

static void __video_display_set(struct owl_de_video *video)
{
	uint32_t val;

	int sl_x, sl_y;
	int ml_x, ml_y;
	int out_width, out_height;

	struct owl_de_video_info *info = &video->info;

	if (!owl_dss_color_is_rgb(info->color_mode) ||
	    info->is_original_scaled) {
		/*
		 * YUV layer or original scaled, monopolize a single macrolayer:
		 *
		 * Set sublayer's coordinate in macrolayer to (0,0);
		 *
		 * Set macro layer coordinate in path to (pos_x,pos_y);
		 *
		 * Set scaler output size to out_width,out_height.
		 */
		sl_x = 0;
		sl_y = 0;

		ml_x = info->pos_x;
		ml_y = info->pos_y;

		out_width = (info->out_width == 0 ?
			     info->width : info->out_width);
		out_height = (info->out_height == 0 ?
			      info->height : info->out_height);
	} else {
		/*
		 * RGB layers share one macro layer:
		 *
		 * Set sublayer's coordinate in macrolayer to
		 * real coordinate before scaling, if there is no scaling,
		 * real coordinate is equal to original coordinate;
		 *
		 * Set macro layer coordinate in path to (disp_x,disp_y),
		 * if there is no scaling, (disp_x,disp_y) = (0,0);
		 *
		 * Set scaler output size to disp_width,disp_height,
		 * if there is no scaling, it will be skipped;
		 */
		sl_x = info->real_pos_x;
		sl_y = info->real_pos_y;

		owl_panel_get_disp_area(video->path->panel, &ml_x, &ml_y,
					&out_width, &out_height);
		if (ML_ID(video) != ml_id_for_scaler0 &&
		    ML_ID(video) != ml_id_for_scaler1) {
			/*
			 * This macro layer has no scaler, ml_x and ml_y
			 * must be 0. And, out_width and out_height
			 * will be skipped, so do not care about them
			 */
			ml_x = 0;
			ml_y = 0;
		}
	}

	BUG_ON((out_width > DE_PATH_SIZE_WIDTH) ||
	       (out_height > DE_PATH_SIZE_HEIGHT));

	val = REG_VAL(sl_y, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(sl_x, DE_PATH_SIZE_WIDTH_END_BIT,
		      DE_PATH_SIZE_WIDTH_BEGIN_BIT);
	de_writel(val, DE_SL_COOR(ML_ID(video), SL_ID(video)));

	val = REG_VAL(ml_y, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(ml_x, DE_PATH_SIZE_WIDTH_END_BIT,
		      DE_PATH_SIZE_WIDTH_BEGIN_BIT);
	de_writel(val, DE_PATH_COOR(video->path->id, ML_ID(video)));

	val = REG_VAL(out_height - 1, DE_PATH_SIZE_HEIGHT_END_BIT,
		      DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(out_width - 1, DE_PATH_SIZE_WIDTH_END_BIT,
			  DE_PATH_SIZE_WIDTH_BEGIN_BIT);
	if (ML_ID(video) == ml_id_for_scaler0)
		de_writel(val, DE_SCALER_OSZIE(0));
	else if (ML_ID(video) == ml_id_for_scaler1)
		de_writel(val, DE_SCALER_OSZIE(1));
}

static void __video_set_scal_coef(uint8_t scaler_id, uint8_t scale_mode)
{
	de_debug("%s, scaler_id %d, scale_mode %d\n",
		 __func__, scaler_id, scale_mode);

	switch (scale_mode) {
	case DE_SCLCOEF_ZOOMIN:
		de_writel(0x00004000, DE_SCALER_SCOEF0(scaler_id));
		de_writel(0xFF073EFC, DE_SCALER_SCOEF1(scaler_id));
		de_writel(0xFE1038FA, DE_SCALER_SCOEF2(scaler_id));
		de_writel(0xFC1B30F9, DE_SCALER_SCOEF3(scaler_id));
		de_writel(0xFA2626FA, DE_SCALER_SCOEF4(scaler_id));
		de_writel(0xF9301BFC, DE_SCALER_SCOEF5(scaler_id));
		de_writel(0xFA3810FE, DE_SCALER_SCOEF6(scaler_id));
		de_writel(0xFC3E07FF, DE_SCALER_SCOEF7(scaler_id));
		break;

	case DE_SCLCOEF_HALF_ZOOMOUT:
		de_writel(0x00004000, DE_SCALER_SCOEF0(scaler_id));
		de_writel(0x00083800, DE_SCALER_SCOEF1(scaler_id));
		de_writel(0x00103000, DE_SCALER_SCOEF2(scaler_id));
		de_writel(0x00182800, DE_SCALER_SCOEF3(scaler_id));
		de_writel(0x00202000, DE_SCALER_SCOEF4(scaler_id));
		de_writel(0x00281800, DE_SCALER_SCOEF5(scaler_id));
		de_writel(0x00301000, DE_SCALER_SCOEF6(scaler_id));
		de_writel(0x00380800, DE_SCALER_SCOEF7(scaler_id));
		break;

	case DE_SCLCOEF_SMALLER_ZOOMOUT:
		de_writel(0x00102010, DE_SCALER_SCOEF0(scaler_id));
		de_writel(0x02121E0E, DE_SCALER_SCOEF1(scaler_id));
		de_writel(0x04141C0C, DE_SCALER_SCOEF2(scaler_id));
		de_writel(0x06161A0A, DE_SCALER_SCOEF3(scaler_id));
		de_writel(0x08181808, DE_SCALER_SCOEF4(scaler_id));
		de_writel(0x0A1A1606, DE_SCALER_SCOEF5(scaler_id));
		de_writel(0x0C1C1404, DE_SCALER_SCOEF6(scaler_id));
		de_writel(0x0E1E1202, DE_SCALER_SCOEF7(scaler_id));
		break;

	default:
		BUG();
	}
}

static void __video_scaling_set(struct owl_de_video *video)
{
	uint8_t scaler_id;
	uint8_t scale_mode;
	uint16_t w_factor, h_factor;
	uint16_t factor;

	int width, height, out_width, out_height;
	int disp_x, disp_y;
	int pri_width = 0, pri_height = 0;

	struct owl_de_video_info *info = &video->info;

	/* scaler0 for macro layer 0, scaler1 for macro layer 1 */
	if (ML_ID(video) == ml_id_for_scaler0)
		scaler_id = 0;
	else if (ML_ID(video) == ml_id_for_scaler1)
		scaler_id = 1;
	else
		return;

	if (!owl_dss_color_is_rgb(info->color_mode) ||
	    info->is_original_scaled) {
		/*
		 * YUV layer or original scaled, monopolize a single macrolayer.
		 */
		width = info->width;
		height = info->height;

		out_width = (info->out_width == 0 ?
			     info->width : info->out_width);
		out_height = (info->out_height == 0 ?
			      info->height : info->out_height);
	} else {
		/* RGB layers share one macro layer(TODO, maybe improper) */
		owl_panel_get_draw_size(video->path->panel, &width, &height);

		/*
		 * If all layers have no scale ,not YUV layers and have no overlap,
		 * so external display device just copy primary display size.
		 * and now we should use primary display input size to calculate
		 * scale factor.
		 */
		if ((info->flag & OWL_VIDEO_FLAG_ADJUST_DRAW_SIZE) == OWL_VIDEO_FLAG_ADJUST_DRAW_SIZE) {
		    if (owl_panel_get_pri_panel_resolution(&pri_width, &pri_height)) {
		        width = pri_width;
		        height = pri_height;
		    }
		}

		owl_panel_get_disp_area(video->path->panel, &disp_x, &disp_y,
					&out_width, &out_height);
	}

	de_debug("%s, video %d, %dx%d->%dx%d\n", __func__,
		 video->id, width, height, out_width, out_height);

	w_factor = (width * 8192 + out_width - 1) / out_width;
	h_factor = (height * 8192 + out_height - 1) / out_height;

	de_writel(w_factor, DE_SCALER_HSR(scaler_id));
	de_writel(h_factor, DE_SCALER_VSR(scaler_id));

	factor = (width * height * 10) / (out_width * out_height);
	if (factor <= 10)
		scale_mode = DE_SCLCOEF_ZOOMIN;
	else if (factor <= 20)
		scale_mode = DE_SCLCOEF_HALF_ZOOMOUT;
	else if (factor > 20)
		scale_mode = DE_SCLCOEF_SMALLER_ZOOMOUT;

	__video_set_scal_coef(scaler_id, scale_mode);
}

static void __video_alpha_set(struct owl_de_video *video)
{
	enum owl_blending_type blending;
	uint8_t alpha;

	uint32_t val;

	struct owl_de_video_info *info = &video->info;

	blending = info->blending;
	alpha = info->alpha;

	/* enable global aplha to emulate RGBX */
	if (info->color_mode == OWL_DSS_COLOR_RGBX8888 ||
	    info->color_mode == OWL_DSS_COLOR_BGRX8888 ||
	    info->color_mode == OWL_DSS_COLOR_XRGB8888 ||
	    info->color_mode == OWL_DSS_COLOR_XBGR8888) {
		blending = OWL_BLENDING_NONE;
		alpha = 0xff;
	}

	de_debug("%s, blending %d, alpha %d\n", __func__, blending, alpha);

	if (blending == OWL_BLENDING_NONE)
		alpha = 0xff;

	val = de_readl(DE_SL_CFG(ML_ID(video), SL_ID(video)));

	/* set global alpha */
	val = REG_SET_VAL(val, alpha, DE_SL_CFG_GLOBAL_ALPHA_END_BIT,
			  DE_SL_CFG_GLOBAL_ALPHA_BEGIN_BIT);

	if (blending == OWL_BLENDING_COVERAGE)
		val = REG_SET_VAL(val, 1, DE_SL_CFG_DATA_MODE_BIT,
				  DE_SL_CFG_DATA_MODE_BIT);
	else
		val = REG_SET_VAL(val, 0, DE_SL_CFG_DATA_MODE_BIT,
				  DE_SL_CFG_DATA_MODE_BIT);

	de_writel(val, DE_SL_CFG(ML_ID(video), SL_ID(video)));
}

static void __video_csc_set(struct owl_de_video *video)
{
	uint32_t val;

	uint32_t bri, con, sat;

	struct owl_de_video_info *info = &video->info;

	/* 0~200-->0~255 */
	bri = (info->brightness * 255 + 100) / 200;
	if (bri > 255)
		bri = 255;

	/* 0~200-->0~14 */
	con = (info->contrast * 14 + 100) / 200;
	if (con > 14)
		con = 14;

	sat = (info->saturation * 14 + 100) / 200;
	if (sat > 14)
		sat = 14;

	val = de_readl(DE_ML_CSC(ML_ID(video)));
	val = REG_SET_VAL(val, bri, 15, 8);
	val = REG_SET_VAL(val, sat, 7, 4);
	val = REG_SET_VAL(val, con, 3, 0);
	de_writel(val, DE_ML_CSC(ML_ID(video)));
};

static void de_s700_video_apply_info(struct owl_de_video *video)
{
	de_debug("%s, video %d, dirty %d\n", __func__, video->id, video->dirty);

	__de_mmu_enable(video);

	__video_fb_addr_set(video);
	__video_pitch_set(video);
	__video_format_set(video);
	__video_rotate_set(video);

	__video_crop_set(video);
	__video_display_set(video);

	__video_scaling_set(video);

	__video_alpha_set(video);

	__video_csc_set(video);
}

static struct owl_de_video_ops de_s700_video_ops = {
	.apply_info = de_s700_video_apply_info,
};

#define S700_SUPPORTED_COLORS (OWL_DSS_COLOR_RGB565 | OWL_DSS_COLOR_BGR565 \
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

#define S700_SUPPORTED_COLORS_NO_YUV (OWL_DSS_COLOR_RGB565 \
			| OWL_DSS_COLOR_BGR565 \
			| OWL_DSS_COLOR_BGRA8888 | OWL_DSS_COLOR_RGBA8888 \
			| OWL_DSS_COLOR_ABGR8888 | OWL_DSS_COLOR_ARGB8888 \
			| OWL_DSS_COLOR_BGRX8888 | OWL_DSS_COLOR_RGBX8888 \
			| OWL_DSS_COLOR_XBGR8888 | OWL_DSS_COLOR_XRGB8888 \
			| OWL_DSS_COLOR_BGRA1555 | OWL_DSS_COLOR_RGBA1555 \
			| OWL_DSS_COLOR_ABGR1555 | OWL_DSS_COLOR_ARGB1555 \
			| OWL_DSS_COLOR_BGRX1555 | OWL_DSS_COLOR_RGBX1555 \
			| OWL_DSS_COLOR_XBGR1555 | OWL_DSS_COLOR_XRGB1555)

#define DE_S700_CAPACITIES {					\
	.supported_colors	= S700_SUPPORTED_COLORS,	\
	.pitch_align		= 1,				\
	.address_align		= 1,				\
	.rgb_limits = {						\
		.input_width	= {1, 4096},			\
		.input_height	= {1, 4096},			\
		.output_width	= {1, 4096},			\
		.output_height	= {1, 4096},			\
		.scaling_width	= {80, 80},			\
		.scaling_height	= {80, 80},			\
	},							\
	.yuv_limits = {						\
		.input_width	= {2, 4096},			\
		.input_height	= {2, 4096},			\
		.output_width	= {2, 4096},			\
		.output_height	= {2, 4096},			\
		.scaling_width	= {80, 80},			\
		.scaling_height	= {80, 80},			\
	},							\
}

#define DE_S700_CAPACITIES_NO_YUV {				\
	.supported_colors	= S700_SUPPORTED_COLORS_NO_YUV,	\
	.pitch_align		= 1,				\
	.address_align		= 1,				\
	.rgb_limits = {						\
		.input_width	= {1, 4096},			\
		.input_height	= {1, 4096},			\
		.output_width	= {1, 4096},			\
		.output_height	= {1, 4096},			\
		.scaling_width	= {80, 80},			\
		.scaling_height	= {80, 80},			\
	},							\
	.yuv_limits = {						\
		.input_width	= {2, 4096},			\
		.input_height	= {2, 4096},			\
		.output_width	= {2, 4096},			\
		.output_height	= {2, 4096},			\
		.scaling_width	= {80, 80},			\
		.scaling_height	= {80, 80},			\
	},							\
}

#define DE_S700_CAPACITIES_NO_SCALER {				\
	.supported_colors	= S700_SUPPORTED_COLORS,	\
	.pitch_align		= 1,				\
	.address_align		= 1,				\
	.rgb_limits = {						\
		.input_width	= {1, 4096},			\
		.input_height	= {1, 4096},			\
		.output_width	= {1, 4096},			\
		.output_height	= {1, 4096},			\
		.scaling_width	= {10, 10},			\
		.scaling_height	= {10, 10},			\
	},							\
	.yuv_limits = {						\
		.input_width	= {2, 4096},			\
		.input_height	= {2, 4096},			\
		.output_width	= {2, 4096},			\
		.output_height	= {2, 4096},			\
		.scaling_width	= {10, 10},			\
		.scaling_height	= {10, 10},			\
	},							\
}

#define DE_S700_CAPACITIES_NO_YUV_SCALER {			\
	.supported_colors	= S700_SUPPORTED_COLORS_NO_YUV,	\
	.pitch_align		= 1,				\
	.address_align		= 1,				\
	.rgb_limits = {						\
		.input_width	= {1, 4096},			\
		.input_height	= {1, 4096},			\
		.output_width	= {1, 4096},			\
		.output_height	= {1, 4096},			\
		.scaling_width	= {10, 10},			\
		.scaling_height	= {10, 10},			\
	},							\
	.yuv_limits = {						\
		.input_width	= {2, 4096},			\
		.input_height	= {2, 4096},			\
		.output_width	= {2, 4096},			\
		.output_height	= {2, 4096},			\
		.scaling_width	= {10, 10},			\
		.scaling_height	= {10, 10},			\
	},							\
}

/* need 2 video layers at most in boot */
static struct owl_de_video de_s700_videos[] = {
	/* macro layer 0 */
	{
		.id			= 0,
		.sibling		= 0xf,
		.name			= "video0",
		.capacities		= DE_S700_CAPACITIES,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 1,
		.sibling		= 0xf,
		.name			= "video1",
		.capacities		= DE_S700_CAPACITIES_NO_YUV,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 2,
		.sibling		= 0xf,
		.name			= "video2",
		.capacities		= DE_S700_CAPACITIES_NO_YUV,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 3,
		.sibling		= 0xf,
		.name			= "video3",
		.capacities		= DE_S700_CAPACITIES_NO_YUV,
		.ops			= &de_s700_video_ops,
	},

	/* macro layer 1 */
	{
		.id			= 4,
		.sibling		= 0xf0,
		.name			= "video4",
		.capacities		= DE_S700_CAPACITIES_NO_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 5,
		.sibling		= 0xf0,
		.name			= "video5",
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 6,
		.sibling		= 0xf0,
		.name			= "video6",
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 7,
		.sibling		= 0xf0,
		.name			= "video6",
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},

	/* macro layer 2 */
	{
		.id			= 8,
		.sibling		= 0xf00,
		.name			= "video8",
		.capacities		= DE_S700_CAPACITIES_NO_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 9,
		.sibling		= 0xf00,
		.name			= "video9",
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 10,
		.name			= "video10",
		.sibling		= 0xf00,
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 11,
		.name			= "video11",
		.sibling		= 0xf00,
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},

	/* macro layer 3 */
	{
		.id			= 12,
		.sibling		= 0xf000,
		.name			= "video12",
		.capacities		= DE_S700_CAPACITIES_NO_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 13,
		.sibling		= 0xf000,
		.name			= "video13",
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 14,
		.name			= "video14",
		.sibling		= 0xf000,
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},
	{
		.id			= 15,
		.name			= "video15",
		.sibling		= 0xf000,
		.capacities		= DE_S700_CAPACITIES_NO_YUV_SCALER,
		.ops			= &de_s700_video_ops,
	},
};


/*===================================================================
 *			S700 DE video layer
 *==================================================================*/

static int de_s700_device_power_on(struct owl_de_device *de)
{
	uint32_t val;

	struct de_s700_pdata *pdata = de->pdata;

	void *dmm_reg1 = ioremap(0xe029000c, 4);
	void *dmm_reg2 = ioremap(0xe0290068, 4);
	void *dmm_reg3 = ioremap(0xe0290000, 4);
	void *dmm_reg4 = ioremap(0xe0290008, 4);

	de_info("%s\n", __func__);

	clk_set_parent(pdata->clk, pdata->parent_clk);
	clk_set_rate(pdata->clk, 300000000);
	clk_prepare_enable(pdata->clk);

	/*
	 * some specail settings
	 */
	de_writel(0x3f, DE_MAX_OUTSTANDING);
	de_writel(0x0f, DE_QOS);

	writel(0xf832, dmm_reg1);
	writel(0xf801, dmm_reg4);
	writel(0x500, dmm_reg2);

	writel(0x80000000, dmm_reg3);
	mdelay(1);
	writel(0x80000004, dmm_reg3);

	iounmap(dmm_reg1);
	iounmap(dmm_reg2);
	iounmap(dmm_reg3);
	iounmap(dmm_reg4);

	/* scaler 0 is assigned to video0 at boot */
	ml_id_for_scaler0 = 0;
	val = de_readl(DE_SCALER_CFG(0));
	val = REG_SET_VAL(val, 1, DE_SCALER_CFG_ENABLE_BIT,
			  DE_SCALER_CFG_ENABLE_BIT);
	val = REG_SET_VAL(val, ml_id_for_scaler0,
			  DE_SCALER_CFG_SEL_END_BIT,
			  DE_SCALER_CFG_SEL_BEGIN_BIT);
	de_writel(val, DE_SCALER_CFG(0));

	if (owl_de_is_ott())
		de_s700_assign_scaler1(1, true);
	else
		de_s700_assign_scaler1(3, true);

	return 0;
}

static int de_s700_device_power_off(struct owl_de_device *de)
{
	struct de_s700_pdata *pdata = de->pdata;

	de_info("%s\n", __func__);

	clk_disable_unprepare(pdata->clk);
	return 0;
}

static int de_s700_device_mmu_config(struct owl_de_device *de,
				     uint32_t base_addr)
{
	de_debug("%s\n", __func__);

	de_writel(base_addr, DE_MMU_BASE);
	return 0;
}

static void de_s700_device_dump_regs(struct owl_de_device *de)
{
	int i, j;

#define DUMPREG(r) de_info("%08x ~ %08x ~ %s\n", r, de_readl(r), #r)

	/*
	 * do not dump DE_IRQENABLE for S700, please see
	 * 'de_s700_path_is_preline_enabled' for detail
	 */
	/* DUMPREG(DE_IRQENABLE); */
	DUMPREG(DE_IRQSTATUS);
	DUMPREG(DE_MAX_OUTSTANDING);
	DUMPREG(DE_MMU_EN);
	DUMPREG(DE_MMU_BASE);
	DUMPREG(DE_OUTPUT_CON);
	DUMPREG(DE_OUTPUT_STAT);
	DUMPREG(DE_PATH_DITHER);

	for (i = 0; i < 2; i++) {
		pr_info("\npath %d ------------------>\n", i);
		DUMPREG(DE_PATH_CTL(i));
		DUMPREG(DE_PATH_FCR(i));
		DUMPREG(DE_PATH_EN(i));
		DUMPREG(DE_PATH_BK(i));
		DUMPREG(DE_PATH_SIZE(i));
		DUMPREG(DE_PATH_GAMMA_IDX(i));
	}

	for (i = 0; i < 4; i++) {
		pr_info("\nlayer %d ------------------>\n", i);
		DUMPREG(DE_ML_CFG(i));
		DUMPREG(DE_ML_ISIZE(i));
		DUMPREG(DE_ML_CSC(i));
		DUMPREG(DE_ML_BK(i));

		DUMPREG(DE_PATH_COOR(0, i));
		DUMPREG(DE_PATH_COOR(1, i));

		for (j = 0; j < 4; j++) {
			pr_info("\n");
			DUMPREG(DE_SL_CFG(i, j));
			DUMPREG(DE_SL_COOR(i, j));
			DUMPREG(DE_SL_FB(i, j));
			DUMPREG(DE_SL_FB_RIGHT(i, j));
			DUMPREG(DE_SL_STR(i, j));
			DUMPREG(DE_SL_CROPSIZE(i, j));
		}
	}

	for (i = 0; i < 2; i++) {
		pr_info("\nscaler %d ------------------>\n", i);
		DUMPREG(DE_SCALER_CFG(i));
		DUMPREG(DE_SCALER_OSZIE(i));
		DUMPREG(DE_SCALER_HSR(i));
		DUMPREG(DE_SCALER_VSR(i));
		DUMPREG(DE_SCALER_SCOEF0(i));
		DUMPREG(DE_SCALER_SCOEF1(i));
		DUMPREG(DE_SCALER_SCOEF2(i));
		DUMPREG(DE_SCALER_SCOEF3(i));
		DUMPREG(DE_SCALER_SCOEF4(i));
		DUMPREG(DE_SCALER_SCOEF5(i));
		DUMPREG(DE_SCALER_SCOEF6(i));
		DUMPREG(DE_SCALER_SCOEF7(i));
	}
#undef DUMPREG

	pr_info("\n");

	pr_info("%08x %08x\n", de_readl(DE_PATH_CTL(0)),
		de_readl(DE_PATH_CTL(1)));
	pr_info("%08x %08x %08x %08x\n", de_readl(DE_ML_CFG(0)),
		de_readl(DE_ML_CFG(1)), de_readl(DE_ML_CFG(2)),
		de_readl(DE_ML_CFG(3)));
}

static inline void __de_s700_backup_regs(struct owl_de_device_regs *p, int reg)
{
	p->reg      = reg;
	p->value    = de_readl(reg);
}

static void de_s700_device_backup_regs(struct owl_de_device *de)
{
	int i, j, cnt = 0;
	struct owl_de_device_regs *regs = de->regs;

	de_info("%s\n", __func__);

	/*
	 * do not backup DE_IRQENABLE for S700, please see
	 * 'de_s700_path_is_preline_enabled' for detail
	 */
	/* __de_s700_backup_regs(&regs[cnt++], DE_IRQENABLE); */
	__de_s700_backup_regs(&regs[cnt++], DE_MAX_OUTSTANDING);
	__de_s700_backup_regs(&regs[cnt++], DE_MMU_EN);
	__de_s700_backup_regs(&regs[cnt++], DE_MMU_BASE);
	__de_s700_backup_regs(&regs[cnt++], DE_OUTPUT_CON);
	__de_s700_backup_regs(&regs[cnt++], DE_PATH_DITHER);

	for (i = 0; i < 2; i++) {
		__de_s700_backup_regs(&regs[cnt++], DE_PATH_CTL(i));
		__de_s700_backup_regs(&regs[cnt++], DE_PATH_EN(i));
		__de_s700_backup_regs(&regs[cnt++], DE_PATH_BK(i));
		__de_s700_backup_regs(&regs[cnt++], DE_PATH_SIZE(i));
		__de_s700_backup_regs(&regs[cnt++], DE_PATH_GAMMA_IDX(i));
	}

	for (i = 0; i < 4; i++) {
		__de_s700_backup_regs(&regs[cnt++], DE_ML_CFG(i));
		__de_s700_backup_regs(&regs[cnt++], DE_ML_ISIZE(i));
		__de_s700_backup_regs(&regs[cnt++], DE_ML_CSC(i));
		__de_s700_backup_regs(&regs[cnt++], DE_ML_BK(i));
		for (j = 0; j < 4; j++) {
			__de_s700_backup_regs(&regs[cnt++], DE_SL_CFG(i, j));
			__de_s700_backup_regs(&regs[cnt++], DE_SL_COOR(i, j));
			__de_s700_backup_regs(&regs[cnt++], DE_SL_FB(i, j));
			__de_s700_backup_regs(&regs[cnt++],
					      DE_SL_FB_RIGHT(i, j));
			__de_s700_backup_regs(&regs[cnt++], DE_SL_STR(i, j));
			__de_s700_backup_regs(&regs[cnt++],
					      DE_SL_CROPSIZE(i, j));
		}
	}

	for (i = 0; i < 2; i++) {
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_CFG(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_OSZIE(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_HSR(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_VSR(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF0(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF1(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF2(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF3(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF4(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF5(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF6(i));
		__de_s700_backup_regs(&regs[cnt++], DE_SCALER_SCOEF7(i));
	}

	de->regs_cnt = cnt;
}


static void de_s700_device_restore_regs(struct owl_de_device *de)
{
	int i;

	de_info("%s\n", __func__);

	for (i = 0; i < de->regs_cnt; i++)
		de_writel(de->regs[i].value, de->regs[i].reg);
}

static struct owl_de_device_ops de_s700_device_ops = {
	.power_on = de_s700_device_power_on,
	.power_off = de_s700_device_power_off,

	.mmu_config = de_s700_device_mmu_config,

	.dump_regs = de_s700_device_dump_regs,
	.backup_regs = de_s700_device_backup_regs,
	.restore_regs = de_s700_device_restore_regs,
};

static struct owl_de_device de_s700_device = {
	.hw_id			= DE_HW_ID_S700,

	.num_paths		= ARRAY_SIZE(de_s700_paths),
	.paths			= de_s700_paths,

	.num_videos		= ARRAY_SIZE(de_s700_videos),
	.videos			= de_s700_videos,

	.ops			= &de_s700_device_ops,
};


/*============================================================================
 *			platform driver
 *==========================================================================*/

static const struct of_device_id de_s700_match[] = {
	{
		.compatible	= "actions,s700-de",
	},
};

static int __init de_s700_probe(struct platform_device *pdev)
{
	int				ret = 0;

	struct device			*dev = &pdev->dev;
	const struct of_device_id	*match;

	struct de_s700_pdata		*pdata;

	dev_info(dev, "%s, pdev = 0x%p\n", __func__, pdev);

	match = of_match_device(of_match_ptr(de_s700_match), dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	de_s700_device.pdev = pdev;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		dev_err(dev, "No Mem for pdata\n");
		return -ENOMEM;
	}

	de_s700_device.regs
		= devm_kzalloc(dev, sizeof(*de_s700_device.regs) * 256,
			       GFP_KERNEL);
	if (de_s700_device.regs == NULL) {
		dev_err(dev, "No Mem for de_s700_device.regs\n");
		return -ENOMEM;
	}

	/*
	 * parse our special resources
	 */

	pdata->clk = devm_clk_get(dev, "clk");
	pdata->parent_clk = devm_clk_get(dev, "clk_parent");
	if (IS_ERR(pdata->clk) || IS_ERR(pdata->parent_clk)) {
		dev_err(dev, "can't get de clk or de parent_clk\n");
		return -EINVAL;
	}

	de_s700_device.pdata = pdata;

	ret = owl_de_register(&de_s700_device);
	if (ret < 0) {
		dev_err(dev, "register s700 de device failed(%d)\n", ret);
		return ret;
	}

	spin_lock_init(&irq_enable_val_lock);

	return 0;
}

static int __exit de_s700_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s, pdev = 0x%p\n", __func__, pdev);

	/* TODO */
	return 0;
}

static int de_s700_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	owl_de_generic_suspend(dev);

	return 0;
}

static int de_s700_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	owl_de_generic_resume(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(de_s700_pm_ops, de_s700_suspend, de_s700_resume);

static struct platform_driver de_s700_platform_driver = {
	.probe			= de_s700_probe,
	.remove			= __exit_p(de_s700_remove),
	.driver = {
		.name		= "de-s700",
		.owner		= THIS_MODULE,
		.of_match_table	= de_s700_match,
		.pm		= &de_s700_pm_ops,
	},
};

static int __init owl_de_s700_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&de_s700_platform_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	return 0;
}

static void __exit owl_de_s700_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&de_s700_platform_driver);
}

module_init(owl_de_s700_init);
module_exit(owl_de_s700_exit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL S700 DE Driver");
MODULE_LICENSE("GPL v2");
