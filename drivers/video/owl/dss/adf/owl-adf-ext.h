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
#ifndef __OWL_ADF_EXT_H__
#define __OWL_ADF_EXT_H__

#include <drm/drm.h>
#include <video/adf.h>

#define OWL_ADF_TRANSFORM_NONE_EXT	(0 << 0)
#define OWL_ADF_TRANSFORM_FLIP_H_EXT	(1 << 0)
#define OWL_ADF_TRANSFORM_FLIP_V_EXT	(1 << 1)
#define OWL_ADF_TRANSFORM_ROT_90_EXT	(1 << 2)
#define OWL_ADF_TRANSFORM_ROT_180_EXT	((1 << 0) + (1 << 1))
#define OWL_ADF_TRANSFORM_ROT_270_EXT	((1 << 0) + (1 << 1) + (1 << 2))

#define OWL_ADF_BLENDING_NONE_EXT	(0 << 0)
#define OWL_ADF_BLENDING_PREMULT_EXT	(1 << 0)
#define OWL_ADF_BLENDING_COVERAGE_EXT	(1 << 1)

#define OWL_ADF_POST_FLAG_ABANDON	(1 << 0)

#define OWL_ADF_BUFFER_FLAG_MODE3D_LR	(1 << 0)
#define OWL_ADF_BUFFER_FLAG_MODE3D_TB	(1 << 1)
#define OWL_ADF_BUFFER_FLAG_ADJUST_SIZE   (1 << 2)

struct owl_adf_overlay_engine_capability_ext {
	__u32 support_transform;
	__u32 support_blend;
} __attribute__((packed, aligned(8)));

struct owl_adf_interface_data_ext {
	__u32 real_width;
	__u32 real_height;
} __attribute__((packed, aligned(8)));

struct owl_adf_buffer_config_ext {
	/* KERNEL unique identifier for any gralloc allocated buffer. */
	__u64 stamp;

	/*
	 * Which interface id that the buffer belongs to,
	 * added for convenience.
	 */
	__u32 aintf_id;

	/* Window buffer property flag */
	__u32 flag;

	/* Crop applied to surface (BEFORE transformation) */
	struct drm_clip_rect	crop;

	/* Region of screen to display surface in (AFTER scaling) */
	struct drm_clip_rect	display;

	/* Surface rotation / flip / mirror */
	__u32			transform;

	/* Alpha blending mode e.g. none / premult / coverage */
	__u32			blend_type;

	/* Plane alpha */
	__u8			plane_alpha;
	__u8			reserved[3];
} __attribute__((packed, aligned(8)));

struct owl_adf_post_ext {
	__u32	post_id;
	__u32	flag;
	struct owl_adf_buffer_config_ext bufs_ext[];
} __attribute__((packed, aligned(8)));

struct owl_adf_validate_config_ext {
	__u32 n_interfaces;
	__u32 __user *interfaces;

	__u32 n_bufs;
	struct adf_buffer_config __user *bufs;

	struct owl_adf_post_ext __user *post_ext;
} __attribute__((packed, aligned(8)));

struct owl_adf_validate_config_ext32 {
	__u32 n_interfaces;
	compat_uptr_t interfaces;

	__u32 n_bufs;
	compat_uptr_t bufs;

	compat_uptr_t post_ext;
} __attribute__((packed, aligned(8)));

/* These shouldn't be stripped by the uapi process in the bionic headers,
 * but currently are being. Redefine them so the custom ioctl interface is
 * actually useful.
 */
#ifndef ADF_IOCTL_TYPE
#define ADF_IOCTL_TYPE 'D'
#endif

#ifndef ADF_IOCTL_NR_CUSTOM
#define ADF_IOCTL_NR_CUSTOM 128
#endif

#define OWL_ADF_VALIDATE_CONFIG_EXT			\
	_IOW(ADF_IOCTL_TYPE, ADF_IOCTL_NR_CUSTOM + 0,	\
	     struct owl_adf_validate_config_ext)

#define OWL_ADF_VALIDATE_CONFIG_EXT32			\
	_IOW(ADF_IOCTL_TYPE, ADF_IOCTL_NR_CUSTOM + 0,	\
	     struct owl_adf_validate_config_ext32)

#define OWL_ADF_WAIT_HALF_VSYNC				\
	_IO(ADF_IOCTL_TYPE, ADF_IOCTL_NR_CUSTOM + 1)

#define OWL_ADF_INIT_HALF_VSYNC				\
	_IO(ADF_IOCTL_TYPE, ADF_IOCTL_NR_CUSTOM + 2)

#define OWL_ADF_UNINIT_HALF_VSYNC				\
	_IO(ADF_IOCTL_TYPE, ADF_IOCTL_NR_CUSTOM + 3)

#endif /* __OWL_ADF_EXT_H__ */
