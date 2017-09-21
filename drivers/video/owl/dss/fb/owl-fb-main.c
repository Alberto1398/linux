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
 *	2015/8/28: Created by Lipeng.
 */
#define DEBUG

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>

#include <video/owl_dss.h>

#define OWL_FB_MAX_FBS		(2)
#define OWL_FB_BUFS_PER_FB	(2)

#define OWL_FB_DEFAULT_BPP	(32)

#define OWL_FB_DEFAULT_XRES	(1280)
#define OWL_FB_DEFAULT_YRES	(720)

struct owl_fb_info {
	int			id;

	u32			pseudo_palette[17];

	struct fb_info		*fbi;

	struct owl_panel	*panel;
	struct owl_de_path	*path;
	struct owl_de_video	*video;

	/* FB buffer */
	size_t			size;
	void			*vaddr;
	dma_addr_t		paddr;

	/* same as owl_fb_device.pdev */
	struct platform_device	*pdev;
};
#define FBI_TO_OFBI(fbi)	((struct owl_fb_info *)(fbi->par))

struct owl_fb_device {
	struct platform_device	*pdev;

	int			num_fbs;
	struct owl_fb_info	*fbs[OWL_FB_MAX_FBS];
};


/*============================================================================
 *			framrbuffer ops
 *==========================================================================*/

static int owl_fb_blank(int blank, struct fb_info *fbi)
{
	struct owl_fb_info *ofbi = FBI_TO_OFBI(fbi);
	struct device *dev = fbi->dev;

	dev_dbg(dev, "%s\n", __func__);

	return 0;
}

static int owl_fb_ioctl(struct fb_info *fbi, unsigned int cmd,
			unsigned long arg)
{
	struct owl_fb_info *ofbi = FBI_TO_OFBI(fbi);
	struct device *dev = fbi->dev;

	dev_dbg(dev, "%s\n", __func__);

	return 0;
}

static int owl_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct owl_fb_info *ofbi = FBI_TO_OFBI(fbi);
	struct device *dev = fbi->dev;

	dev_dbg(dev, "%s\n", __func__);

	return 0;
}

static int __ofbi_alloc_fbmem(struct owl_fb_info *ofbi)
{
	size_t size;
	void *vaddr;
	dma_addr_t paddr;

	struct device *dev = &ofbi->pdev->dev;

	struct fb_info *fbi = ofbi->fbi;
	struct fb_var_screeninfo *var = &fbi->var;

	dev_dbg(dev, "%s\n", __func__);

	size = var->xres * var->yres * (var->bits_per_pixel / 8)
		* OWL_FB_BUFS_PER_FB;
	if (size == 0) {
		dev_err(dev, "%s: size is zero\n", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, "%s: size = %zd\n", __func__, size);

	/* really ??? TODO */
	if (ofbi->size != 0 || ofbi->vaddr != NULL || ofbi->paddr != 0) {
		dma_free_coherent(dev, ofbi->size, ofbi->vaddr, ofbi->paddr);
		ofbi->size = 0;
		ofbi->vaddr = NULL;
		ofbi->paddr = 0;
	}

	vaddr = dma_alloc_coherent(dev, size, &paddr, GFP_KERNEL);
	if (vaddr == NULL) {
		dev_err(dev, "%s: alloc failed\n", __func__);
		return -ENOMEM;
	}
	dev_dbg(dev, "%s: paddr = %zx, vaddr = %p\n", __func__, paddr, vaddr);

	ofbi->size = size;
	ofbi->vaddr = vaddr;
	ofbi->paddr = paddr;

	return 0;
}

/*
 * set fb fix according to var, maybe re-alloc buffer, TODO
 */
static void __ofbi_set_fix(struct owl_fb_info *ofbi)
{
	struct fb_info *fbi = ofbi->fbi;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;

	fbi->screen_base = (char __iomem *)ofbi->vaddr;

	fix->line_length = (var->xres_virtual * var->bits_per_pixel) >> 3;
	fix->smem_len = ofbi->size;
	fix->smem_start = ofbi->paddr;
	fix->type = FB_TYPE_PACKED_PIXELS;

	if (var->nonstd) {
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	} else {
		switch (var->bits_per_pixel) {
		case 32:
		case 24:
		case 16:
		case 12:
			/* 12bpp is stored in 16 bits */
			fix->visual = FB_VISUAL_TRUECOLOR;
			break;
		case 1:
		case 2:
		case 4:
		case 8:
			fix->visual = FB_VISUAL_PSEUDOCOLOR;
			break;
		}
	}

	fix->accel = FB_ACCEL_NONE;
	fix->xpanstep = 1;
	fix->ypanstep = 1;
}

static void __ofbi_set_video_info(struct owl_fb_info *ofbi)
{
	struct owl_de_video *video = ofbi->video;

	struct fb_info *fbi = ofbi->fbi;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;

	struct owl_de_video_info v_info;

	owl_de_video_get_info(video, &v_info);

	if (var->bits_per_pixel == 32)
		v_info.color_mode = OWL_DSS_COLOR_BGRX8888;
	else if (var->bits_per_pixel == 16)
		v_info.color_mode = OWL_DSS_COLOR_RGB565;
	else	/* TODO */
		v_info.color_mode = OWL_DSS_COLOR_BGRA8888;

	v_info.xoff = 0;
	v_info.yoff = 0;
	v_info.width = var->xres;
	v_info.height = var->yres;

	v_info.pos_x = 0;
	v_info.pos_y = 0;
	v_info.out_width = var->xres;
	v_info.out_height = var->yres;

	v_info.alpha = 0xff;
	v_info.blending = OWL_BLENDING_COVERAGE;

	v_info.rotation = 0;

	v_info.mmu_enable = false;

	v_info.n_planes = 1;
	v_info.addr[0] = (unsigned long)ofbi->paddr;
	v_info.offset[0] = var->yoffset * fix->line_length
				+ var->xoffset * (var->bits_per_pixel >> 3);
	v_info.pitch[0] = (var->bits_per_pixel >> 3) * v_info.width;

	owl_de_video_set_info(video, &v_info);
}

static int owl_fb_set_par(struct fb_info *fbi)
{
	int ret;

	struct device *dev = fbi->device;

	struct fb_var_screeninfo *var = &fbi->var;

	struct owl_fb_info *ofbi = FBI_TO_OFBI(fbi);

	dev_dbg(dev, "%s: resolution, %ux%u (%ux%u virtual)\n", __func__,
		var->xres, var->yres, var->xres_virtual, var->yres_virtual);

	dev_dbg(dev, "%s: ofbi %p\n", __func__, ofbi);
	dev_dbg(dev, "%s: &ofbi->pdev->dev %p\n", __func__, &ofbi->pdev->dev);

	/* alloc fb memory according to var info */
	ret = __ofbi_alloc_fbmem(ofbi);
	if (ret < 0) {
		dev_err(dev, "%s: alloc fbmem failed\n", __func__);
		return ret;
	}

	__ofbi_set_fix(ofbi);

	__ofbi_set_video_info(ofbi);

	/* apply path info and video info */
	owl_de_path_apply(ofbi->path);

	return 0;
}

static int owl_fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *fbi)
{
	struct owl_fb_info *ofbi = FBI_TO_OFBI(fbi);
	struct device *dev = fbi->dev;

	dev_dbg(dev, "%s: offset, %ux%u\n",
		__func__, var->xoffset, var->yoffset);

	if (var->xoffset == fbi->var.xoffset &&
	    var->yoffset == fbi->var.yoffset)
		return 0;

	fbi->var.xoffset = var->xoffset;
	fbi->var.yoffset = var->yoffset;

	__ofbi_set_video_info(ofbi);
	owl_de_path_apply(ofbi->path);

	return 0;
}

static int owl_fb_setcolreg(u32 regno, u32 red, u32 green, u32 blue, u32 transp,
			    struct fb_info *fbi)
{
	u32 *pal = fbi->pseudo_palette;
	u32 cr = red >> (16 - fbi->var.red.length);
	u32 cg = green >> (16 - fbi->var.green.length);
	u32 cb = blue >> (16 - fbi->var.blue.length);
	u32 value;

	struct device *dev = fbi->dev;

	dev_dbg(dev, "%s\n", __func__);

	if (regno >= 16)
		return -EINVAL;

	value = (cr << fbi->var.red.offset) |
		(cg << fbi->var.green.offset) |
		(cb << fbi->var.blue.offset);
	if (fbi->var.transp.length > 0) {
		u32 mask = (1 << fbi->var.transp.length) - 1;
		mask <<= fbi->var.transp.offset;
		value |= mask;
	}
	pal[regno] = value;

	return 0;
}

static struct fb_ops owl_fb_ops = {
	.owner          = THIS_MODULE,
	.fb_blank       = owl_fb_blank,
	.fb_ioctl       = owl_fb_ioctl,
	.fb_check_var   = owl_fb_check_var,
	.fb_set_par     = owl_fb_set_par,
	.fb_pan_display = owl_fb_pan_display,
	.fb_setcolreg	= owl_fb_setcolreg,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea    = cfb_copyarea,
	.fb_imageblit   = cfb_imageblit,
};

/*============================================================================
 *			framrbuffer initialize
 *==========================================================================*/
/*
 * initialize struct fb_var_screeninfo
 */
static void ofbi_init_var(struct owl_fb_info *ofbi)
{
	int width = 0, height = 0;

	struct device *dev = &ofbi->pdev->dev;

	struct fb_info *fbi = ofbi->fbi;
	struct fb_var_screeninfo *var = &fbi->var;

	fbi->device = dev;

	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &owl_fb_ops;

	fbi->pseudo_palette = ofbi->pseudo_palette;

	var->nonstd = 0;

	/* TODO, can parse from boot args */
	var->bits_per_pixel = OWL_FB_DEFAULT_BPP;

	/* TODO */
	if (var->bits_per_pixel == 16) {	/* RGB565 */
		var->red.offset = 11;	var->red.length = 5;
		var->green.offset = 5;	var->green.length = 6;
		var->blue.offset = 0;	var->blue.length = 5;
		var->transp.offset = 0;	var->transp.length = 0;
	} else if (var->bits_per_pixel == 32) {	/* XBGR8888 */
		var->transp.offset = 24; var->transp.length = 0;
		var->red.offset = 16;	var->red.length = 8;
		var->green.offset = 8;	var->green.length = 8;
		var->blue.offset = 0;	var->blue.length = 8;
	}

	/*
	 * initialize xres & yres using boot args (TODO),
	 * or using the resolution of owl panel,
	 * or using a default one
	 */

	/* get width from boot args, TODO */

	/* get width from panel */
	if (width <= 0 || height <= 0)
		owl_panel_get_resolution(ofbi->panel, &width, &height);

	if (width <= 0 || height <= 0) {
		width = OWL_FB_DEFAULT_XRES;
		height = OWL_FB_DEFAULT_YRES;
	}

	var->xres = width;
	var->yres = height;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres * OWL_FB_BUFS_PER_FB;

	var->activate |= (FB_ACTIVATE_FORCE | FB_ACTIVATE_NOW);
}

static void owl_fb_deinit_ofbi(struct owl_fb_info *ofbi)
{
	/* TODO */
}

static int owl_fb_init_ofbi(struct owl_fb_info *ofbi)
{
	int				ret = 0;

	struct owl_de_path		*path;
	struct owl_de_video		*video;

	struct device			*dev = &ofbi->pdev->dev;

	BUG_ON(ofbi->panel == NULL);

	dev_dbg(dev, "%s: id %d\n", __func__, ofbi->id);

	path = owl_de_path_get_by_type(owl_panel_get_type(ofbi->panel));
	video = owl_de_video_get_by_index(ofbi->id);
	if (path == NULL || video == NULL) {
		dev_err(dev, "%s: get de path or de video failed\n", __func__);
		return -ENODEV;
	}
	ofbi->path = path;
	ofbi->video = video;

	owl_de_path_attach(path, video);

	dev_dbg(dev, "%s: enable\n", __func__);
	owl_panel_enable(ofbi->panel);
	owl_de_path_enable(ofbi->path);

	ofbi_init_var(ofbi);

	ret = fb_set_var(ofbi->fbi, &ofbi->fbi->var);
	if (ret < 0) {
		dev_err(dev, "%s: fb_set_var failed(%d)\n", __func__, ret);
		return ret;
	}

	if (ret < 0) {
		dev_err(dev, "%s: init fbinfo failed(%d)\n", __func__, ret);
		goto err_out;
	}

	dev_dbg(dev, "%s: register_framebuffer\n", __func__);
	ret = register_framebuffer(ofbi->fbi);
	if (ret < 0) {
		dev_err(dev, "%s: register_framebuffer failed(%d)\n",
			__func__, ret);
		goto err_out;
	}

#if defined(CONFIG_LOGO) && !defined(MODULE)
	dev_dbg(dev, "%s: show logo\n", __func__);
	if (fb_prepare_logo(ofbi->fbi, 0))
		fb_show_logo(ofbi->fbi, 0);
	else
		dev_err(dev, "%s: prepare logo failed\n", __func__);
#endif
	return 0;

err_out:
	owl_fb_deinit_ofbi(ofbi);
	return ret;
}

/*============================================================================
 *				platform driver
 *==========================================================================*/
static const struct of_device_id owl_fb_match[] = {
	{
		.compatible		= "actions,framebuffer",
	},
};

static int __init owl_fb_probe(struct platform_device *pdev)
{
	bool				search_primary;

	int				ret = 0;

	struct owl_panel		*panel;

	struct device			*dev = &pdev->dev;
	const struct of_device_id	*match;

	struct owl_fb_device		*owlfb;
	struct owl_fb_info		*ofbi;
	struct fb_info			*fbi;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(owl_fb_match), dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	owlfb = devm_kzalloc(dev, sizeof(*owlfb), GFP_KERNEL);
	if (owlfb == NULL) {
		dev_err(dev, "No Mem\n");
		return -ENOMEM;
	}
	owlfb->pdev = pdev;
	dev_set_drvdata(dev, owlfb);

	search_primary = true;	/* ensure primary panel is FB0 */
	panel = NULL;
	owl_panel_for_each(panel) {
		dev_dbg(dev, "%s: panel '%s'\n", __func__,
			owl_panel_get_name(panel));

		if (search_primary && !PANEL_IS_PRIMARY(panel))
			continue;

		if (!search_primary && PANEL_IS_PRIMARY(panel))
			continue;

		if (owlfb->num_fbs > OWL_FB_MAX_FBS) {
			dev_warn(dev, "%s: too many FB devices\n", __func__);
			break;
		}

		fbi = framebuffer_alloc(sizeof(struct owl_fb_info), dev);
		if (!fbi) {
			dev_err(dev, "cannot allocate memory\n");
			return -ENOMEM;
		}
		ofbi = fbi->par;

		ofbi->fbi = fbi;
		ofbi->id = owlfb->num_fbs;
		owlfb->fbs[owlfb->num_fbs] = ofbi;
		owlfb->num_fbs++;

		ofbi->panel = panel;
		ofbi->pdev = pdev;

		ret = owl_fb_init_ofbi(ofbi);
		if (ret < 0) {
			dev_err(dev, "init fb dev failed\n");
			return -EINVAL;
		}

		if (search_primary) {
			/*
			 * arive here with search_primary is true,
			 * which means wo got the primary panel,
			 * reset panel to NULL, in order to search
			 * no-primary panel from scratch
			 */
			search_primary = false;
			panel = NULL;
		}
	}

	return 0;
}

static int __exit owl_fb_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s\n", __func__);

	/* TODO */
	return 0;
}

static struct platform_driver owl_fb_platform_driver = {
	.probe			= owl_fb_probe,
	.remove			= __exit_p(owl_fb_remove),
	.driver = {
		.name		= "owl-fb",
		.owner		= THIS_MODULE,
		.of_match_table	= owl_fb_match,
	},
};

static int __init owl_fb_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&owl_fb_platform_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	return 0;
}

static void __exit owl_fb_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&owl_fb_platform_driver);
}

module_init(owl_fb_init);
module_exit(owl_fb_exit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL Framebuffer Driver");
MODULE_LICENSE("GPL v2");
