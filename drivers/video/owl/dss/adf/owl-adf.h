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
#ifndef _ADF_OWL_ADF_H_
#define _ADF_OWL_ADF_H_

#ifdef CONFIG_VIDEO_OWL_ADF_FBDEV_SUPPORT
#include <video/adf_fbdev.h>
#endif

#include <linux/ktime.h>

#include <video/owl_dss.h>

#include "owl-adf-ext.h"

#define ADF_DEBUG_ENABLE
#define SUBSYS_NAME		"owl_adf: "

/*
 * a special buffer ID for FB device,
 * which is the max number of 64-bit unsigned type
 */
#define OWL_ADF_FBDEV_BUF_ID	(~(1ULL))

#define OWL_ADF_INTF_CVBS	(ADF_INTF_TYPE_DEVICE_CUSTOM + 1)
//#define OWL_ADF_USE_HALF_VSYNC
struct owl_adf_interface {
	int				id;

	struct adf_interface		intf;
	enum adf_interface_type		type;

	struct owl_panel		*panel;
	struct owl_de_path		*path;

	enum owl_dss_state		state;

	/* for debug */
	ktime_t				pre_vsync_stamp;
	u32				pre_vsync_duration_us;

	/* strange guys, FIXME later */
	int				owleng_bitmap;
	int				owleng_bitmap_pre;
	int				owleng_out_bitmap;
	int				owleng_in_bitmap;
	bool				dirty;

	bool				is_vsync_on;

	/* should replaced by mutex */
	atomic_t			ref_cnt;

#ifdef OWL_ADF_USE_HALF_VSYNC
	/*
	 * these guys are used to wait HALF vsync
	 */
	struct hrtimer			half_vsync_hrtimer;
#endif
	wait_queue_head_t		half_vsync_wait;
	atomic_t                half_vsync_need_wait;
};

struct owl_adf_overlay_engine {
	int				id;

	struct adf_overlay_engine	eng;

	struct owl_adf_overlay_engine_capability_ext cap_ext;

	struct owl_de_video		*video;
};

struct owl_adf_device {
	struct adf_device		adfdev;
	struct platform_device		*pdev;

	int				n_intfs;
	struct owl_adf_interface	*intfs;
#ifdef CONFIG_VIDEO_OWL_ADF_FBDEV_SUPPORT
	struct adf_fbdev		*fbdevs;
#endif

	int				n_engs;
	struct owl_adf_overlay_engine	*engs;

	struct ion_client		*ion_client;
};

#define adf_to_owl_adf(p) container_of(p, struct owl_adf_device, adfdev)
#define intf_to_owl_intf(p) container_of(p, struct owl_adf_interface, intf)
#define eng_to_owl_eng(p) container_of(p, struct owl_adf_overlay_engine, eng)

void get_drm_mode_from_panel(struct owl_panel *owl_panel,
			     struct drm_mode_modeinfo *mode);

enum owl_color_mode drm_color_to_owl_color(u32 color);

enum adf_interface_type dss_type_to_interface_type(enum owl_display_type type);

void owleng_get_capability_ext(struct owl_adf_overlay_engine *owleng);

int adf_buffer_to_video_info(struct owl_adf_device *owladf,
			     struct adf_buffer *buf,
			     struct adf_buffer_mapping *mappings,
			     struct owl_adf_buffer_config_ext *config_ext,
			     struct owl_de_video_info *info);

int buffer_and_ext_validate(struct owl_adf_device *owladf,
			struct adf_buffer *buf,
			struct owl_adf_buffer_config_ext *config_ext);

#ifdef ADF_DEBUG_ENABLE
	extern int owl_adf_debug;
	#define ADFVISABLE(format, ...) \
		do { \
			if (owl_adf_debug > 2) \
				printk(KERN_DEBUG SUBSYS_NAME format, ## __VA_ARGS__); \
		} while (0)

	#define ADFDBG(format, ...) \
		do { \
			if (owl_adf_debug > 1) \
				printk(KERN_DEBUG SUBSYS_NAME format, ## __VA_ARGS__); \
		} while (0)

	#define ADFINFO(format, ...) \
		do { \
			if (owl_adf_debug > 0) \
				printk(KERN_INFO SUBSYS_NAME format, ## __VA_ARGS__); \
		} while (0)

#else
	#define ADFDBG(format, ...)
	#define ADFINFO(format, ...)
	#define ADFVISABLE(format, ...)
#endif

#define ADFERR(format, ...) \
	printk(KERN_ERR SUBSYS_NAME "error!" format, ## __VA_ARGS__);

#endif
