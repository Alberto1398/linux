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
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include <linux/uaccess.h>

#include <drm/drm_fourcc.h>

#include <video/adf.h>
#include <video/adf_format.h>
#include <video/adf_client.h>

#include <linux/fb.h>

#include <video/owl_dss.h>
#include <linux/owl_common.h>

#include "ion/ion.h"
#include "uapi/owl_ion.h"

#include "owl-adf.h"
#include "owl-adf-ext.h"

#define CREATE_TRACE_POINTS
#include "owl-adf-trace.h"

#ifdef ADF_DEBUG_ENABLE
/* 0, error; 1, error+info; 2, error+info+debug */
int owl_adf_debug = 1;
module_param_named(debug, owl_adf_debug, int, 0644);
#endif

extern struct ion_device *owl_ion_device;

static bool is_fb_reserved_memory_freed;
static int memory_free_skip_frames = 900;	/* 15s in 60Hz */

/*
 * some test variables
 */
static int test_stop_post;
static int test_gpu_compose;

static long fb_logo_base;
static long fb_logo_size;
static int __init fb_logo_process(char *str)
{
	char *str_len;

	if (str == NULL || *str == '\0')
		return 0;

	str_len = strchr(str, ',');
	if (!str_len)
		return 0;
	if (*(str_len + 1) == 0)
		return 0;

	str_len = str_len + 1;

	fb_logo_base = simple_strtoul(str, NULL, 16);
	fb_logo_size = simple_strtoul(str_len, NULL, 16);
	return 1;
}
__setup("fb_logo_reserve=0x", fb_logo_process);

void free_fb_logo_reserved_memory(long base, long size)
{
	struct zone *zone;
	unsigned long flags;

	free_reserved_area((unsigned long)__va(base),
			   (unsigned long)(__va(base) + size), 0, "fb_logo");
	zone = page_zone(pfn_to_page(base >> PAGE_SHIFT));
	spin_lock_irqsave(&zone->lock, flags);
	zone->managed_pages += size >> PAGE_SHIFT;
	spin_unlock_irqrestore(&zone->lock, flags);
	setup_per_zone_wmarks();
}

/*==============================================================================
			the operations of adf device
 *============================================================================*/
static int owl_adf_device_attach(struct adf_device *adfdev,
				struct adf_overlay_engine *eng,
				struct adf_interface *intf)
{
	struct owl_adf_overlay_engine *owleng = eng_to_owl_eng(eng);

	ADFDBG("%s: eng %d, intf %d\n", __func__, eng->base.id, intf->base.id);
	return 0;
}

static int owl_adf_device_detach(struct adf_device *adfdev,
				struct adf_overlay_engine *eng,
				struct adf_interface *intf)
{
	struct owl_adf_overlay_engine *owleng = eng_to_owl_eng(eng);

	ADFDBG("%s: eng %d, intf %d\n", __func__, eng->base.id, intf->base.id);
	return 0;
}

static int owl_adf_device_validate(struct adf_device *adfdev,
				struct adf_post *cfg, void **driver_state)
{
	struct owl_adf_device *owladf = adf_to_owl_adf(adfdev);

	size_t buf_ext_size = cfg->custom_data_size;
	struct owl_adf_post_ext *buf_ext;

	int i, ret = 0;

	if (cfg->n_bufs > owladf->n_engs) {
		ADFERR("n_bufs %ld invalid\n", cfg->n_bufs);
		return -EINVAL;
	}

	if (cfg->custom_data == NULL) {
		ADFERR("custom_data is NULL\n");
		return -EINVAL;
	}
	if ((sizeof(struct owl_adf_buffer_config_ext) * cfg->n_bufs
		+ sizeof(struct owl_adf_post_ext)) != buf_ext_size) {
		ADFERR("buf_ext_size %ld is invalid\n", buf_ext_size);
		return -EINVAL;
	}

	buf_ext = (struct owl_adf_post_ext *)cfg->custom_data;
	ADFDBG("before buffer_and_ext_validate, post_id %d, flag %d\n",
		buf_ext->post_id, buf_ext->flag);

	/* abandoned post, let it go */
	if ((buf_ext->flag & OWL_ADF_POST_FLAG_ABANDON) != 0)
		return 0;

	if (test_gpu_compose == 1 && cfg->n_bufs > 1) {
		ADFDBG("Force GPU composing\n");
		return -EINVAL;
	}

	ADFDBG("before buffer_and_ext_validate, post_id %d\n",
		buf_ext->post_id);
	for (i = 0; i < cfg->n_bufs; i++) {
		ADFDBG("buffer_and_ext_validate buf %d\n", i);
		ret = buffer_and_ext_validate(owladf, &cfg->bufs[i],
					&buf_ext->bufs_ext[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int __owl_adf_device_validate_ext(struct adf_device *adfdev,
			struct owl_adf_validate_config_ext __user *arg)
{
	size_t i, j;
	int err = 0;
	u32 post_ext_size;

	struct adf_interface **intfs = NULL;
	struct adf_buffer *bufs = NULL;

	struct adf_post post_cfg;
	void *driver_state;

#if 0
	if (!access_ok(VERIFY_READ, arg,
			sizeof(struct owl_adf_validate_config_ext))) {
		err = -EFAULT;
		goto err_out;
	}
#endif

	if (arg->n_interfaces > ADF_MAX_INTERFACES
		|| arg->n_bufs > ADF_MAX_BUFFERS) {
		err = -EINVAL;
		goto err_out;
	}

	post_ext_size = sizeof(struct owl_adf_post_ext)
		+ arg->n_bufs * sizeof(struct owl_adf_buffer_config_ext);

	if (!access_ok(VERIFY_READ, arg->bufs,
		sizeof(struct adf_buffer_config) * arg->n_bufs)) {
		err = -EFAULT;
		goto err_out;
	}

	if (!access_ok(VERIFY_READ, arg->post_ext, post_ext_size)) {
		err = -EFAULT;
		goto err_out;
	}

	if (arg->n_interfaces) {
		intfs = kmalloc(sizeof(intfs[0]) * arg->n_interfaces,
				GFP_KERNEL);
		if (!intfs) {
			err = -ENOMEM;
			goto err_out;
		}
	}

	for (i = 0; i < arg->n_interfaces; i++) {
		intfs[i] = idr_find(&adfdev->interfaces, arg->interfaces[i]);
		if (!intfs[i]) {
			err = -EINVAL;
			goto err_out;
		}
	}

	if (arg->n_bufs) {
		bufs = kzalloc(sizeof(bufs[0]) * arg->n_bufs, GFP_KERNEL);
		if (!bufs) {
			err = -ENOMEM;
			goto err_out;
		}
	}

	for (i = 0; i < arg->n_bufs; i++) {
		struct adf_buffer_config *config = &arg->bufs[i];

		memset(&bufs[i], 0, sizeof(bufs[i]));

		if (config->n_planes > ADF_MAX_PLANES) {
			err = -EINVAL;
			goto err_import;
		}

		bufs[i].overlay_engine = idr_find(&adfdev->overlay_engines,
						config->overlay_engine);
		if (!bufs[i].overlay_engine) {
			err = -ENOENT;
			goto err_import;
		}

		bufs[i].w = config->w;
		bufs[i].h = config->h;
		bufs[i].format = config->format;

		for (j = 0; j < config->n_planes; j++) {
			bufs[i].dma_bufs[j] = dma_buf_get(config->fd[j]);
			if (IS_ERR_OR_NULL(bufs[i].dma_bufs[j])) {
				err = PTR_ERR(bufs[i].dma_bufs[j]);
				bufs[i].dma_bufs[j] = NULL;
				goto err_import;
			}
			bufs[i].offset[j] = config->offset[j];
			bufs[i].pitch[j] = config->pitch[j];
		}
		bufs[i].n_planes = config->n_planes;

		bufs[i].acquire_fence = NULL;
	}


	/* Fake up a post configuration to validate */
	post_cfg.custom_data_size = post_ext_size;
	post_cfg.custom_data = arg->post_ext;
	post_cfg.n_bufs = arg->n_bufs;
	post_cfg.bufs = bufs;

	/*
	 * Mapping dma bufs is too expensive for validate, and we don't
	 * need to do it at the moment.
	 */
	post_cfg.mappings = NULL;

	err = adfdev->ops->validate(adfdev, &post_cfg, &driver_state);
	if (err)
		goto err_import;

	/* For the validate ioctl, we don't need the driver state. If it
	 * was allocated, free it immediately.
	 */
	if (adfdev->ops->state_free)
		adfdev->ops->state_free(adfdev, driver_state);

err_import:
	for (i = 0; i < arg->n_bufs; i++)
		for (j = 0; j < ARRAY_SIZE(bufs[i].dma_bufs); j++)
			if (bufs[i].dma_bufs[j])
				dma_buf_put(bufs[i].dma_bufs[j]);
err_out:
	kfree(intfs);
	kfree(bufs);

	ADFDBG("%s: ret = %d\n", __func__, err);
	return err;
}

static long owl_adf_device_ioctl(struct adf_obj *obj, unsigned int cmd,
				unsigned long arg)
{
	struct adf_device *adfdev = adf_obj_to_device(obj);

	struct owl_adf_validate_config_ext32 *ext32;
	struct owl_adf_validate_config_ext ext;

	switch (cmd) {
	case OWL_ADF_VALIDATE_CONFIG_EXT:
		ADFDBG("%s: cmd(0x%x)\n", __func__, cmd);
		return __owl_adf_device_validate_ext(adfdev,
			(struct owl_adf_validate_config_ext __user *)arg);

	case OWL_ADF_VALIDATE_CONFIG_EXT32:
		ADFDBG("%s: cmd32(0x%x)\n", __func__, cmd);

		ext32 = (struct owl_adf_validate_config_ext32 __user *)arg;

		ext.n_interfaces = ext32->n_interfaces;
		ext.interfaces = (__u32 __user *)ext32->interfaces;
		ext.n_bufs = ext32->n_bufs;
		ext.bufs = (struct adf_buffer_config __user *)ext32->bufs;
		ext.post_ext
			= (struct owl_adf_post_ext __user *)ext32->post_ext;

		return __owl_adf_device_validate_ext(adfdev, &ext);

	default:
		ADFDBG("%s: cmd(0x%x) invalid\n", __func__, cmd);
		return -EINVAL;
	}
}

static void owl_adf_device_post(struct adf_device *adfdev, struct adf_post *cfg,
				void *driver_state)
{
	int i, j;

	struct owl_adf_device *owladf = adf_to_owl_adf(adfdev);
	struct owl_adf_interface *owlintf, *owlintf2;
	struct owl_adf_overlay_engine *owleng;

	struct owl_de_video *video;
	struct owl_de_video_info v_info;

	/* extension data, src/crop/transform/Alpha etc. */
	struct owl_adf_post_ext *buf_ext;
	struct owl_adf_buffer_config_ext *config_ext;

	ADFDBG("%s: n_bufs %zd\n", __func__, cfg->n_bufs);

	if (1 == test_stop_post) {
		ADFINFO("post test, n_bufs %ld\n", cfg->n_bufs);
		return;
	}

	buf_ext = (struct owl_adf_post_ext *)cfg->custom_data;
	ADFDBG("%s: post_id %d, flag %d\n", __func__,
	       buf_ext->post_id, buf_ext->flag);

	/* abandoned post, let it go */
	if (((buf_ext->flag & OWL_ADF_POST_FLAG_ABANDON) != 0) && (cfg->n_bufs > 0)) {
		ADFDBG("abandoned post, post_id %d\n", buf_ext->post_id);
		return;
	}

	/*
	 * prepare post
	 */
	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];

		/*
		 * detach all video from its path,
		 * we will re-attach the requied guys later.
		 * TODO, FIXME
		 */
		owl_de_path_detach_all(owlintf->path);
	}

	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];

		owlintf->dirty = false;

		owlintf->owleng_bitmap_pre = owlintf->owleng_bitmap;
		owlintf->owleng_bitmap = 0;

		if (((buf_ext->flag & OWL_ADF_POST_FLAG_ABANDON) != 0) && (cfg->n_bufs == 0)) {
			owlintf->dirty = true;
		}

		for (j = 0; j < cfg->n_bufs; j++) {
			config_ext = &buf_ext->bufs_ext[j];
			if (i != config_ext->aintf_id)
				continue;	/* not for me */

			owleng = eng_to_owl_eng(cfg->bufs[j].overlay_engine);
			video = owleng->video;

			owl_de_video_get_info(video, &v_info);

			ADFDBG("%s: buf %d\n", __func__, j);
			adf_buffer_to_video_info(owladf, &cfg->bufs[j],
						 &cfg->mappings[j], config_ext,
						 &v_info);

			owl_de_video_set_info(video, &v_info);

			owl_de_path_attach(owlintf->path, video);

			owlintf->owleng_bitmap |= (1 << owleng->id);
			owlintf->dirty = true;
		}

		owlintf->owleng_out_bitmap
			= (owlintf->owleng_bitmap ^ owlintf->owleng_bitmap_pre)
				& owlintf->owleng_bitmap_pre;
		owlintf->owleng_in_bitmap
			= (owlintf->owleng_bitmap ^ owlintf->owleng_bitmap_pre)
				& owlintf->owleng_bitmap;

		ADFDBG("###owlintf%d, pre 0x%x, curr 0x%x, out 0x%x, in 0x%x\n",
		       owlintf->id,
		       owlintf->owleng_bitmap_pre, owlintf->owleng_bitmap,
		       owlintf->owleng_out_bitmap, owlintf->owleng_in_bitmap);
	}

	/*
	 * apply first for path that its layer is re-assined to others,
	 * for example, if owleng_bitmap is changed from 0100 to 1000,
	 * layer2 is re-assined out,
	 * if owleng_bitmap is changed from 0000 to 0100,
	 * layer2 is re-assined in
	 */
	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];

		if (owlintf->owleng_out_bitmap == 0)
			continue;

		for (j = 0; j < owladf->n_intfs; j++) {
			owlintf2 = &owladf->intfs[j];

			if (i == j || owlintf2->owleng_in_bitmap == 0)
				continue;

			if (((~(owlintf->owleng_out_bitmap
			       ^ owlintf2->owleng_in_bitmap))
			    & owlintf->owleng_out_bitmap) != 0) {
				/*
				 * owlintf's layers are re-assigned to owlintf2
				 */
				owl_de_path_apply(owlintf->path);
				owl_de_path_wait_for_go(owlintf->path);
				owlintf->dirty = false;
				ADFDBG("###owlintf%d apply first\n",
				       owlintf->id);
				break;
			}
		}

	}

	if (owl_de_is_s900()) {
		/* apply and wait for primary interface */
		for (i = 0; i < owladf->n_intfs; i++) {
			owlintf = &owladf->intfs[i];

			if (owlintf->dirty && PANEL_IS_PRIMARY(owlintf->panel)) {
				owl_de_path_apply(owlintf->path);
				owl_de_path_wait_for_go(owlintf->path);
			}
		}

		/* apply and wait for others */
		for (i = 0; i < owladf->n_intfs; i++) {
			owlintf = &owladf->intfs[i];

			if (owlintf->dirty && !PANEL_IS_PRIMARY(owlintf->panel)) {
				owl_de_path_apply(owlintf->path);
				owl_de_path_wait_for_go(owlintf->path);
			}
		}
	} else {
		/* apply */
		for (i = 0; i < owladf->n_intfs; i++) {
			owlintf = &owladf->intfs[i];

			if (owlintf->dirty)
				owl_de_path_apply(owlintf->path);
		}

		/* wait for primary interface */
		for (i = 0; i < owladf->n_intfs; i++) {
			owlintf = &owladf->intfs[i];

			if (owlintf->dirty && PANEL_IS_PRIMARY(owlintf->panel))
				owl_de_path_wait_for_go(owlintf->path);
		}

		/* wait for others */
		for (i = 0; i < owladf->n_intfs; i++) {
			owlintf = &owladf->intfs[i];

			if (owlintf->dirty && !PANEL_IS_PRIMARY(owlintf->panel))
				owl_de_path_wait_for_go(owlintf->path);
		}
	}

	if (unlikely(!is_fb_reserved_memory_freed)) {
		/*
		 * In order to avoid screen blur, I need a simple arithmetic
		 * to check if it is the time to free reserved logo memory,
		 * which is:
		 *	Aha, just skip some frames, such as 3600 frames,
		 *	if frame rate is 60Hz, which is 60s, enough!
		 */
		if (memory_free_skip_frames--)
			return;

		free_fb_logo_reserved_memory(fb_logo_base, fb_logo_size);
		is_fb_reserved_memory_freed = true;
		ADFINFO("%s, fb reserved memory is freed.\n", __func__);
	}
}

static struct adf_device_ops owl_adf_device_ops = {
	.owner		= THIS_MODULE,
	.base		= {
		.ioctl	= owl_adf_device_ioctl,
	},
	.attach		= owl_adf_device_attach,
	.detach		= owl_adf_device_detach,
	.validate	= owl_adf_device_validate,
	.post		= owl_adf_device_post,
};

/*==============================================================================
			the operations of adf overlay engine
 *============================================================================*/

int owl_adf_eng_custom_data(struct adf_obj *obj, void *data, size_t *size)
{
	struct adf_overlay_engine *eng = adf_obj_to_overlay_engine(obj);
	struct owl_adf_overlay_engine *owleng = eng_to_owl_eng(eng);

	*size = sizeof(struct owl_adf_overlay_engine_capability_ext);

	memcpy(data, &owleng->cap_ext, *size);

	ADFDBG("%s: data size is %ld\n", __func__, *size);

	return 0;
}

static const u32 owl_adf_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,

	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,

	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV420,
};

static struct adf_overlay_engine_ops owl_adf_eng_ops = {
	.base = {
		.custom_data = owl_adf_eng_custom_data,
	},
	.supported_formats = owl_adf_formats,
	.n_supported_formats = ARRAY_SIZE(owl_adf_formats),
};


/*==============================================================================
			the operations of adf interface
 *============================================================================*/

#ifdef OWL_ADF_USE_HALF_VSYNC
static enum hrtimer_restart
owl_adf_half_vsync_hrtimer_func(struct hrtimer *timer)
{
	struct owl_adf_interface *owlintf;

	owlintf = container_of(timer, struct owl_adf_interface,
			       half_vsync_hrtimer);

	wake_up(&owlintf->half_vsync_wait);

	return HRTIMER_NORESTART;
}
#endif
static void owl_adf_intf_vsync_cb(struct owl_panel *panel, void *data,
				  u32 value)
{
	struct owl_adf_interface *owlintf = data;
	ktime_t stamp = ktime_get();
	int duration, deviation;

	/* do not support vsync if it is not primary panel */
	if (!PANEL_IS_PRIMARY(panel))
		return;
#ifdef OWL_ADF_USE_HALF_VSYNC
	unsigned long half_vsync_duration_ns;

	/* wakeup half vsync wait thread at VSYNC + HLAF VSYNC */
	half_vsync_duration_ns = 1000 * 1000 * 1000 / 2 ;
	if (owlintf->intf.current_mode.vrefresh != 0)
		half_vsync_duration_ns /= owlintf->intf.current_mode.vrefresh;

	hrtimer_start(&owlintf->half_vsync_hrtimer,
		      ktime_set(0, half_vsync_duration_ns),
		      HRTIMER_MODE_REL);

#endif

	if (owlintf->is_vsync_on)
		adf_vsync_notify(&owlintf->intf, stamp);

 /* wakeup half vsync wait thread at VSYNC */
	wake_up(&owlintf->half_vsync_wait);
	duration = ktime_to_us(ktime_sub(stamp, owlintf->pre_vsync_stamp));
	deviation = duration - owlintf->pre_vsync_duration_us;

	if (deviation != 0)
	    ADFVISABLE("%s: intf%d, duration %dus, deviation %dus\n",
			__func__, owlintf->intf.base.id, duration, deviation);

	owlintf->pre_vsync_stamp = stamp;
	owlintf->pre_vsync_duration_us = duration;

	trace_owl_adf_vsync(owlintf, duration, deviation);
}

static int owl_adf_intf_blank(struct adf_interface *intf, u8 state);

static void owl_adf_intf_hotplug_cb(struct owl_panel *panel,
				    void *data, u32 value)
{
	struct owl_adf_interface *owlintf = data;

	struct owl_panel *hdmi_panel, *cvbs_panel;	/* for S700 OTT*/

	ADFINFO("%s: intf%d state %d hotplug %d\n", __func__,
		owlintf->intf.base.id, owlintf->state, value);

	if (panel == NULL) {
		ADFERR("dss device is NULL\n");
		return;
	}

	/* re-set adf interface's current mode */
	get_drm_mode_from_panel(panel, &owlintf->intf.current_mode);

	if (value == 1 && !owlintf->intf.hotplug_detect) {
		/* only report plug event to ADF for HDMI & CVBS */
		if ((PANEL_TYPE(panel) == OWL_DISPLAY_TYPE_HDMI) ||
				(PANEL_TYPE(panel) == OWL_DISPLAY_TYPE_CVBS))
			adf_hotplug_notify_connected(&owlintf->intf, NULL, 0);
	} else if (value == 0 && owlintf->intf.hotplug_detect) {
		if (OWL_DSS_STATE_BOOT_ON == owlintf->state) {
			/*
			 * we are in BOOT ON STATE, and received a hotplug out
			 * event. In this specail case, we should bypass
			 * adf core, and do some specail handling using our API.
			 */
			owl_adf_intf_blank(&owlintf->intf, DRM_MODE_DPMS_OFF);
		}

		/* only report plug event to ADF for HDMI & CVBS */
		if ((PANEL_TYPE(panel) == OWL_DISPLAY_TYPE_HDMI) ||
				(PANEL_TYPE(panel) == OWL_DISPLAY_TYPE_CVBS))
			adf_hotplug_notify_disconnected(&owlintf->intf);
	}

	if (owl_de_is_s700() && owl_de_is_ott()) {
		hdmi_panel = owl_panel_get_by_type(OWL_DISPLAY_TYPE_HDMI);
		cvbs_panel = owl_panel_get_by_type(OWL_DISPLAY_TYPE_CVBS);

		/* make sure anther panel is off (only for CVBS now), TODO*/
		if ((hdmi_panel != NULL) && (cvbs_panel != NULL) && owl_panel_hpd_is_connected(hdmi_panel))
			owl_ctrl_disable(cvbs_panel->ctrl);
			/* owl_panel_disable(cvbs_panel); */
	}
}

static int __owl_adf_intf_open(struct owl_adf_interface *owlintf)
{
	ADFINFO("%s: intf%d, ref_cnt %d\n", __func__, owlintf->intf.base.id,
		atomic_read(&owlintf->ref_cnt));

	if (atomic_inc_return(&owlintf->ref_cnt) > 1)
		return 0;

	return 0;
}

static int owl_adf_intf_open(struct adf_obj *obj, struct inode *inode,
			     struct file *file)
{
	struct owl_adf_interface *owlintf;

	ADFDBG("%s\n", __func__);

	owlintf = intf_to_owl_intf(adf_obj_to_interface(obj));

	return __owl_adf_intf_open(owlintf);
}

static void __owl_adf_intf_close(struct owl_adf_interface *owlintf)
{
	ADFINFO("%s: intf%d, ref_cnt %d\n", __func__, owlintf->intf.base.id,
		atomic_read(&owlintf->ref_cnt));

	if (atomic_dec_return(&owlintf->ref_cnt) > 0)
		return;
}

static void owl_adf_intf_close(struct adf_obj *obj, struct inode *inode,
			       struct file *file)
{
	struct owl_adf_interface *owlintf;

	ADFDBG("%s\n", __func__);

	owlintf = intf_to_owl_intf(adf_obj_to_interface(obj));

	return __owl_adf_intf_close(owlintf);
}

static bool owl_adf_intf_supports_event(struct adf_obj *obj,
					enum adf_event_type type)
{
	struct owl_adf_interface *owlintf;

	owlintf = intf_to_owl_intf(adf_obj_to_interface(obj));

	switch (type) {
	case ADF_EVENT_VSYNC:
		if (PANEL_IS_PRIMARY(owlintf->panel))
			return true;
		else
			return false;

	case ADF_EVENT_HOTPLUG:
		/* only hdmi & cvbs devices support hotplug event */
		if ((PANEL_TYPE(owlintf->panel) == OWL_DISPLAY_TYPE_HDMI) ||
		        (PANEL_TYPE(owlintf->panel) == OWL_DISPLAY_TYPE_CVBS))
			return true;
		else
			return false;

	default:
		return false;
	}
}

static void owl_adf_intf_set_event(struct adf_obj *obj,
				   enum adf_event_type type, bool enabled)
{
	struct owl_adf_interface *owlintf;

	ADFDBG("%s: event %d is enabled?(%d)\n",
		__func__, type, enabled);

	owlintf = intf_to_owl_intf(adf_obj_to_interface(obj));

	switch (type) {
	case ADF_EVENT_VSYNC:
		if (enabled) {
			owl_de_path_enable_vsync(owlintf->path);
			trace_owl_adf_vsync_enable(owlintf);
		} else {
			owl_de_path_disable_vsync(owlintf->path);
			trace_owl_adf_vsync_disable(owlintf);
		}
		owlintf->is_vsync_on = enabled;

		break;

	case ADF_EVENT_HOTPLUG:
		break;

	default:
		BUG();
	}
}

static int owl_adf_intf_custom_data(struct adf_obj *obj, void *data,
				    size_t *size)
{
	struct owl_adf_interface *owlintf;

	struct owl_adf_interface_data_ext *ext_data;

	ADFDBG("%s\n", __func__);

	*size = 0;

	owlintf = intf_to_owl_intf(adf_obj_to_interface(obj));

	ext_data = (struct owl_adf_interface_data_ext *)data;
	*size = sizeof(struct owl_adf_interface_data_ext);

	owl_panel_get_resolution(owlintf->panel, &ext_data->real_width,
				 &ext_data->real_height);

	return 0;
}

static long owl_adf_intf_ioctl(struct adf_obj *obj, unsigned int cmd,
			       unsigned long arg)
{
	struct adf_interface *intf = adf_obj_to_interface(obj);
	struct owl_adf_interface *owlintf = intf_to_owl_intf(intf);
	struct owl_panel *panel = owlintf->panel;
	struct owl_dss_panel_desc *desc = &panel->desc;
	switch (cmd) {
	case OWL_ADF_WAIT_HALF_VSYNC:
	{

		static ktime_t pre_stamp;
		long long timstamp = 0;
		ktime_t cur_stamp;
		DEFINE_WAIT(__wait);

		ADFDBG("%s: OWL_ADF_WAIT_HALF_VSYNC\n", __func__);
        if(owlintf->state == OWL_DSS_STATE_ON && atomic_read(&owlintf->half_vsync_need_wait) == 1){
		    prepare_to_wait(&owlintf->half_vsync_wait, &__wait,
				TASK_UNINTERRUPTIBLE);
		    schedule();
		    finish_wait(&owlintf->half_vsync_wait, &__wait);
        }else{
			if(atomic_read(&owlintf->half_vsync_need_wait) == 0){
				ADFERR("%s: OWL_ADF_WAIT_HALF_VSYNC not init \n", __func__);
			}
        }

        /*vsync off ,used for atw ,config in dts pannel vsync_off_us attr*/
        usleep_range(desc->vsync_off_us,desc->vsync_off_us + 333);

		cur_stamp = ktime_get();

		ADFDBG("%s: half vsync period is %lldms\n", __func__,
		       ktime_to_ms(ktime_sub(cur_stamp, pre_stamp)));

		if(ktime_to_ms(ktime_sub(cur_stamp, pre_stamp)) > 1){
			pre_stamp = cur_stamp;
			timstamp = ktime_to_ns(cur_stamp);

			if(copy_to_user((void *)arg,&timstamp,sizeof(long long))){
				return -EFAULT;
			}
		}
		pre_stamp = cur_stamp;

		return 0;
	}
	case OWL_ADF_INIT_HALF_VSYNC:
	{
		wake_up(&owlintf->half_vsync_wait);
		atomic_set(&owlintf->half_vsync_need_wait,1);
		return 0;
	}
	case OWL_ADF_UNINIT_HALF_VSYNC:
	{
		wake_up(&owlintf->half_vsync_wait);
		atomic_set(&owlintf->half_vsync_need_wait,0);
		return 0;
	}
	default:
		ADFDBG("%s: cmd(0x%x) invalid\n", __func__, cmd);
		return -EINVAL;
	}
}
static int owl_adf_intf_modeset(struct adf_interface *intf,
				struct drm_mode_modeinfo *mode)
{
	return 0;
}

static int owl_adf_intf_blank(struct adf_interface *intf, u8 state)
{
	struct owl_adf_interface *owlintf = intf_to_owl_intf(intf);
	struct owl_panel *panel = owlintf->panel;

	struct owl_de_path *path = owlintf->path;

	struct fb_event event;
	int fb_state;

	ADFINFO("%s: intf%d owlintf->state %d state %d\n", __func__,
		owlintf->intf.base.id, owlintf->state, state);

	if (DRM_MODE_DPMS_OFF == state && OWL_DSS_STATE_OFF != owlintf->state) {
		owlintf->state = OWL_DSS_STATE_OFF;

		/* for primary device, off backlight first */
		if (intf->flags & ADF_INTF_FLAG_PRIMARY) {
			fb_state = FB_BLANK_POWERDOWN;

			event.data = &fb_state;
			fb_notifier_call_chain(FB_EVENT_BLANK, &event);
		}

		owl_de_path_disable(path);
		owl_panel_disable(panel);
		wake_up(&owlintf->half_vsync_wait);
	} else if (DRM_MODE_DPMS_ON == state &&
		   OWL_DSS_STATE_ON != owlintf->state) {
		owl_panel_enable(panel);
		owl_de_path_enable(path);

		/* for primary device, on backlight later */
		if (intf->flags & ADF_INTF_FLAG_PRIMARY) {
			fb_state = FB_BLANK_UNBLANK;

			event.data = &fb_state;
			fb_notifier_call_chain(FB_EVENT_BLANK, &event);
		}
		wake_up(&owlintf->half_vsync_wait);
		owlintf->state = OWL_DSS_STATE_ON;
	}

	return 0;
}

#ifdef CONFIG_VIDEO_OWL_ADF_FBDEV_SUPPORT
static int owl_adf_intf_alloc_simple_buffer(struct adf_interface *intf,
					    u16 w, u16 h, u32 format,
					    struct dma_buf **dma_buf,
					    u32 *offset, u32 *pitch)
{
	bool is_hdmi_connected = false;
	bool is_cvbs_connected = false;

	bool format_valid = false;

	int err = 0, i;
	u32 size;

	struct ion_handle *hdl;

	struct adf_device *adfdev = intf->base.parent;
	struct owl_adf_device *owladf = adf_to_owl_adf(adfdev);

	struct owl_adf_interface *owlintf;

	ADFDBG("%s, %dx%d, format %x\n", __func__, w, h, format);

	owlintf = intf_to_owl_intf(intf);

	/*
	 * some specail handling for S700 OTT's HDMI & CVBS
	 */
	if (!owl_de_is_s700() || (PANEL_TYPE(owlintf->panel) != OWL_DISPLAY_TYPE_CVBS && PANEL_TYPE(owlintf->panel) != OWL_DISPLAY_TYPE_HDMI))
		goto skip_s700_handle;


	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];
		if (owl_panel_hpd_is_connected(owlintf->panel)) {
			if (PANEL_TYPE(owlintf->panel) == OWL_DISPLAY_TYPE_HDMI)
				is_hdmi_connected = true;

			if (PANEL_TYPE(owlintf->panel) == OWL_DISPLAY_TYPE_CVBS)
				is_cvbs_connected = true;
		}
	}

	owlintf = intf_to_owl_intf(intf);

	if (!is_hdmi_connected && is_cvbs_connected) {
		/* CVBS only, only let CVBS go */
		if (PANEL_TYPE(owlintf->panel) != OWL_DISPLAY_TYPE_CVBS)
			return -EINVAL;
	} else {
		/* none, all, or HDMI only, only let HDMI go */
		if (PANEL_TYPE(owlintf->panel) != OWL_DISPLAY_TYPE_HDMI)
			return -EINVAL;
	}
	/*
	 * some specail handling for S700 OTT's HDMI & CVBS, END
	 */
skip_s700_handle:

	for (i = 0; i < ARRAY_SIZE(owl_adf_formats); i++) {
		if (owl_adf_formats[i] == format) {
			format_valid = true;
			break;
		}
	}

	if (!format_valid) {
		ADFERR("format %d invalid\n", format);
		return -EINVAL;
	}

	size = w * h * adf_format_bpp(format) / 8;
	ADFDBG("%s, size %d\n", __func__, size);

	hdl = ion_alloc(owladf->ion_client, size, 0,
						(1 << ION_HEAP_ID_PMEM), 0);
	if (IS_ERR(hdl)) {
		err = PTR_ERR(hdl);
		ADFERR("ion_alloc failed (%d)\n", err);
		goto err_out;
	}

	*dma_buf = ion_share_dma_buf(owladf->ion_client, hdl);
	if (IS_ERR(*dma_buf)) {
		err = PTR_ERR(hdl);
		ADFERR("ion_share_dma_buf failed (%d)\n", err);
		goto err_free_buffer;
	}
	*pitch = w * adf_format_bpp(format) / 8;
	*offset = 0;

	/* GPU need FB's physical address, export it */
	err =
	ion_phys(owladf->ion_client, hdl,
		 &owladf->fbdevs[owlintf->id].info->fix.smem_start,
		 (size_t *)&owladf->fbdevs[owlintf->id].info->fix.smem_len);
	if (err < 0) {
		ADFERR("ion_phys error\n");
	} else {
		ADFDBG("smem_start = 0x%lx, smem_len=%d\n",
			owladf->fbdevs[owlintf->id].info->fix.smem_start,
			owladf->fbdevs[owlintf->id].info->fix.smem_len);
	}

err_free_buffer:
	ion_free(owladf->ion_client, hdl);
err_out:
	return err;
}


/*
 * attach owl_adf_post_ext information
 */
static int owl_adf_intf_describe_simple_post(struct adf_interface *intf,
		struct adf_buffer *fb, void *data, size_t *size)
{
	struct owl_adf_interface *owlintf = intf_to_owl_intf(intf);

	struct owl_adf_post_ext *buf_ext;
	struct owl_adf_buffer_config_ext *config_ext;
	size_t buf_ext_size = sizeof(struct owl_adf_buffer_config_ext)
			+ sizeof(struct owl_adf_post_ext);

	ADFDBG("%s\n", __func__);

	if (buf_ext_size > ADF_MAX_CUSTOM_DATA_SIZE) {
		ADFERR("buf_ext_size(%ld) is too large\n", buf_ext_size);
		return -ENOMEM;
	}
	*size = buf_ext_size;

	buf_ext = (struct owl_adf_post_ext *)data;
	config_ext = &buf_ext->bufs_ext[0];

	config_ext->aintf_id = owlintf->id;

	/* fill the config_ext */
	config_ext->stamp = OWL_ADF_FBDEV_BUF_ID;

	config_ext->crop.x1 = 0;
	config_ext->crop.y1 = 0;
	config_ext->crop.x2 = fb->w;
	config_ext->crop.y2 = fb->h;

	ADFDBG("%s: crop (%d,%d)~(%d,%d)\n", __func__,
		config_ext->crop.x1, config_ext->crop.y1,
		config_ext->crop.x2, config_ext->crop.y2);

	config_ext->display.x1 = 0;
	config_ext->display.y1 = 0;
	config_ext->display.x2 = fb->w;
	config_ext->display.y2 = fb->h;
	ADFDBG("%s: display (%d,%d)~(%d,%d)\n", __func__,
		config_ext->display.x1, config_ext->display.y1,
		config_ext->display.x2, config_ext->display.y2);

	config_ext->transform = OWL_ADF_TRANSFORM_NONE_EXT;
	config_ext->blend_type = OWL_ADF_BLENDING_NONE_EXT;
	config_ext->plane_alpha = 0xff;

	return 0;
}
#endif

static int owl_adf_intf_screen_size(struct adf_interface *intf,
				u16 *width_mm, u16 *height_mm)
{
	struct owl_adf_interface *owlintf = intf_to_owl_intf(intf);

	*width_mm = PANEL_WIDTH_MM(owlintf->panel);
	*height_mm = PANEL_HEIGTH_MM(owlintf->panel);

	return 0;
}

static const char *owl_adf_intf_type_str(struct adf_interface *intf)
{
	switch (intf->type) {
		case OWL_ADF_INTF_CVBS:
			return "cvbs";
		default:
			return "unkown";
	}
}

static struct adf_interface_ops owl_adf_intf_ops = {
	.base = {
		.open = owl_adf_intf_open,
		.release = owl_adf_intf_close,
		.supports_event = owl_adf_intf_supports_event,
		.set_event = owl_adf_intf_set_event,
		.custom_data = owl_adf_intf_custom_data,
		.ioctl	= owl_adf_intf_ioctl,
	},
	.modeset = owl_adf_intf_modeset,
	.blank = owl_adf_intf_blank,
#ifdef CONFIG_VIDEO_OWL_ADF_FBDEV_SUPPORT
	.alloc_simple_buffer = owl_adf_intf_alloc_simple_buffer,
	.describe_simple_post = owl_adf_intf_describe_simple_post,
#endif
	.screen_size = owl_adf_intf_screen_size,
	.type_str = owl_adf_intf_type_str,
};


/*==============================================================================
			adf framebuffer device
 *============================================================================*/


#ifdef CONFIG_VIDEO_OWL_ADF_FBDEV_SUPPORT

#define OWL_FBDEV_FORMAT		DRM_FORMAT_RGB565

static int owl_adf_fbdev_open(struct fb_info *info, int user)
{
#ifdef CONFIG_VIDEO_OWL_ADF_FBDEV_PCBABOARD_SUPPORT
	struct adf_fbdev *fbdev = info->par;
	struct adf_interface *intf = fbdev->intf;
	struct owl_adf_interface *owlintf = intf_to_owl_intf(intf);

	int err = 0;

	ADFDBG("%s\n", __func__);

	err = __owl_adf_intf_open(owlintf);
	if (err < 0) {
		ADFERR("__owl_adf_intf_open failed (%d)\n", err);
		return err;
	}

	adf_fbdev_blank(FB_BLANK_UNBLANK, info);

	return adf_fbdev_open(info, user);
#else
	return 0;
#endif
}

static int owl_adf_fbdev_release(struct fb_info *info, int user)
{
#ifdef CONFIG_VIDEO_OWL_ADF_FBDEV_PCBABOARD_SUPPORT
	struct adf_fbdev *fbdev = info->par;
	struct adf_interface *intf = fbdev->intf;
	struct owl_adf_interface *owlintf = intf_to_owl_intf(intf);

	ADFDBG("%s\n", __func__);

	__owl_adf_intf_close(owlintf);

	return adf_fbdev_release(info, user);
#else
	return 0;
#endif
}

static void owl_adf_fbdev_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	cfb_imageblit(info, image);
}

static struct fb_ops owl_adf_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= owl_adf_fbdev_open,
	.fb_release	= owl_adf_fbdev_release,
	.fb_check_var	= adf_fbdev_check_var,
	.fb_set_par	= adf_fbdev_set_par,
	.fb_blank	= adf_fbdev_blank,
	.fb_pan_display	= adf_fbdev_pan_display,
	.fb_mmap	= adf_fbdev_mmap,
	.fb_imageblit	= owl_adf_fbdev_imageblit,
};

static int owl_adf_fbdev_init(struct owl_adf_device *owladf)
{
	u16 width, height;
	int i, err = 0;

	struct device *dev = &owladf->pdev->dev;

	struct owl_adf_interface *owlintf;
	struct owl_adf_overlay_engine *owleng;

	struct owl_panel *panel;

	ADFINFO("%s\n", __func__);

	/* create fbdev for every intf */
	owladf->fbdevs = devm_kzalloc(dev, sizeof(struct adf_fbdev)
					* owladf->n_intfs, GFP_KERNEL);
	if (!owladf->fbdevs) {
		ADFERR("devm_kzalloc failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];
		owleng = &owladf->engs[i];	/* lazy trick */

		panel = owlintf->panel;

		/* do not create framebuffer for dummy device */
		if (PANEL_TYPE(panel) == OWL_DISPLAY_TYPE_DUMMY)
			continue;

		owl_panel_get_draw_size(panel, (int *)&width, (int *)&height);

		/* regitser FB device */
		err = adf_fbdev_init(&owladf->fbdevs[i], &owlintf->intf,
				     &owleng->eng, width, height,
				     OWL_FBDEV_FORMAT, &owl_adf_fb_ops,
				     "owladf_fb%d", i);
		if (err < 0) {
			dev_warn(dev, "Failed to init ADF fbdev%d (%d)\n",
				 i, err);
			continue;
		}
	}


	return 0;
}

static int owl_adf_fbdev_deinit(struct owl_adf_device *owladf)
{
	int i;
	ADFINFO("%s\n", __func__);

	for (i = 0; i < owladf->n_intfs; i++)
		adf_fbdev_destroy(&owladf->fbdevs[i]);

	devm_kfree(&owladf->pdev->dev, owladf->fbdevs);
	return 0;
}

#else
static int owl_adf_fbdev_init(struct owl_adf_device *owladf) {}
static int owl_adf_fbdev_deinit(struct owl_adf_device *owladf) {}
#endif


/*============================================================================
			local help functions
 *==========================================================================*/

static int owl_adf_intfs_init(struct owl_adf_device *owladf)
{
	int n_intfs, cnt;
	int i, err = 0, primary_path_id, index;
	u32 flags;

	struct device *dev = &owladf->pdev->dev;
	struct owl_adf_interface *owlintf;

	struct owl_de_path *path;
	struct owl_panel *panel;

	/* counter the valid interface numbers */
	n_intfs = 0;
	for (i = 0; i < owl_de_get_path_num(); i++) {
		path = owl_de_path_get_by_index(i);
		if (path == NULL || path->panel == NULL)
			continue;

		if (PANEL_IS_PRIMARY(path->panel))
			primary_path_id = path->id;

		n_intfs++;
	}

	if (n_intfs == 0) {
		ADFINFO("no valid interface\n");
		return -ENODEV;
	}
	ADFINFO("n_intfs = %d\n", n_intfs);

	owladf->n_intfs = n_intfs;
	owladf->intfs = devm_kzalloc(dev, sizeof(struct owl_adf_interface)
				     * n_intfs, GFP_KERNEL);
	if (!owladf->intfs) {
		err = -ENOMEM;
		goto err_mem;
	}

	cnt = 0;
	/*
	 * Define the primary panel path to the adf interface 0.
	 *
	 * 'index' is path id, range is [0, owl_de_get_path_num()).
	 *
	 * 'primary_path_id' is primary panel path id,
	 *  range is [0, owl_de_get_path_num()).
	 */
	index = primary_path_id;
	for (i = 0; i < owl_de_get_path_num(); i++, index++) {
		/* ensure index rangs is correct */
		if (index >= owl_de_get_path_num())
			index = 0;

		path = owl_de_path_get_by_index(index);
		if (path == NULL || path->panel == NULL)
			continue;

		panel = path->panel;

		owlintf = &owladf->intfs[cnt];
		owlintf->id = cnt;

		atomic_set(&owlintf->ref_cnt, 0);
		init_waitqueue_head(&owlintf->half_vsync_wait);
	#ifdef OWL_ADF_USE_HALF_VSYNC
		hrtimer_init(&owlintf->half_vsync_hrtimer, CLOCK_MONOTONIC,
			      HRTIMER_MODE_REL);
		owlintf->half_vsync_hrtimer.function
			= owl_adf_half_vsync_hrtimer_func;
	#endif

		owlintf->path = path;
		owlintf->panel = path->panel;

		owlintf->type = dss_type_to_interface_type(PANEL_TYPE(panel));

		if (PANEL_IS_PRIMARY(panel))
			flags = ADF_INTF_FLAG_PRIMARY;
		else
			flags = ADF_INTF_FLAG_EXTERNAL;

		if (owl_de_path_is_enabled(path))
			owlintf->state = OWL_DSS_STATE_BOOT_ON;
		else
			owlintf->state = OWL_DSS_STATE_OFF;

		/* update panel's state to owlintf's state */
		panel->state = owlintf->state;

		owlintf->owleng_bitmap = 0;
		owlintf->owleng_bitmap_pre = 0;

		/* adf interfaces init */
		err = adf_interface_init(&owlintf->intf, &owladf->adfdev,
					owlintf->type, 0, flags,
					&owl_adf_intf_ops,
					"%s", owlintf->path->name);
		if (err < 0) {
			ADFERR("Failed to init ADF interface %d(%d)\n", cnt, err);
			goto err_init;
		}

		/* set callbacks */
		owl_panel_vsync_cb_set(panel, owl_adf_intf_vsync_cb, owlintf);
		owl_panel_hotplug_cb_set(panel, owl_adf_intf_hotplug_cb,
					 owlintf);

		cnt++;
	}

	ADFINFO("%d interfaces is inited\n", n_intfs);
	return 0;

err_init:
	while (cnt--)
		adf_interface_destroy(&owladf->intfs[i].intf);

	devm_kfree(dev, owladf->intfs);

err_mem:
	return err;
}


static int owl_adf_intfs_deinit(struct owl_adf_device *owladf)
{
	int i;

	struct owl_adf_interface *owlintf;

	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];

		owl_panel_vsync_cb_set(owlintf->panel, NULL, owlintf);
		owl_panel_hotplug_cb_set(owlintf->panel, NULL, owlintf);

		adf_interface_destroy(&owlintf->intf);
	}

	devm_kfree(&owladf->pdev->dev, owladf->intfs);

	return 0;
}

static int owl_adf_engs_init(struct owl_adf_device *owladf)
{
	struct device *dev = &owladf->pdev->dev;
	int n_engs = owl_de_get_video_num();
	int i, err = 0;

	owladf->n_engs = n_engs;
	owladf->engs = devm_kzalloc(dev, sizeof(struct owl_adf_overlay_engine)
				    * n_engs, GFP_KERNEL);
	if (!owladf->engs) {
		err = -ENOMEM;
		goto err_mem;
	}

	for (i = 0; i < n_engs; i++) {
		owladf->engs[i].id = i;

		owleng_get_capability_ext(&owladf->engs[i]);

		owladf->engs[i].video = owl_de_video_get_by_index(i);

		/* adf overlay engine init */
		err = adf_overlay_engine_init(&owladf->engs[i].eng,
					      &owladf->adfdev,
					      &owl_adf_eng_ops, "%s",
					      owladf->engs[i].video->name);
		if (err < 0) {
			ADFERR("Failed to init ADF engine %d(%d)\n", i, err);
			goto err_init;
		}
	}

	ADFINFO("%d overlay engines is inited\n", n_engs);
	return 0;

err_init:
	while (i--)
		adf_overlay_engine_destroy(&owladf->engs[i].eng);

	devm_kfree(dev, owladf->engs);

err_mem:
	return err;

}


static int owl_adf_engs_deinit(struct owl_adf_device *owladf)
{
	int i;

	for (i = 0; i < owladf->n_engs; i++)
		adf_overlay_engine_destroy(&owladf->engs[i].eng);

	devm_kfree(&owladf->pdev->dev, owladf->engs);

	return 0;
}

/* allow the attachment of all possile intf and eng pairs */
static int owl_adf_attachment_allow(struct owl_adf_device *owladf)
{
	int i, j, err = 0;
	struct owl_adf_interface *owlintf;

	/* DE allow any interface and overlay engine pair */
	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];

		/* do not assign overlay engine for dummy device */
		if (PANEL_TYPE(owlintf->panel) == OWL_DISPLAY_TYPE_DUMMY)
			continue;

		for (j = 0; j < owladf->n_engs; j++) {
			err = adf_attachment_allow(&owladf->adfdev,
						   &owladf->engs[j].eng,
						   &owlintf->intf);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

/*============================================================================
			platform driver/device functions
 *==========================================================================*/

static int owl_adf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct owl_adf_device *owladf;
	struct owl_adf_interface *owlintf;
	int err = 0, i;

	owladf = devm_kzalloc(dev, sizeof(struct owl_adf_device), GFP_KERNEL);
	if (!owladf) {
		err = -ENOMEM;
		goto err_out;
	}
	platform_set_drvdata(pdev, owladf);
	owladf->pdev = pdev;

	err = adf_device_init(&owladf->adfdev, dev,
			      &owl_adf_device_ops, dev_name(dev));
	if (err < 0) {
		ADFERR("Failed to init ADF device (%d)\n", err);
		goto err_dev_init;
	}

	err = owl_adf_intfs_init(owladf);
	if (err < 0) {
		ADFERR("Failed to init ADF interface (%d)\n", err);
		goto err_inf_init;
	}

	err = owl_adf_engs_init(owladf);
	if (err < 0) {
		ADFERR("Failed to init ADF engine (%d)\n", err);
		goto err_eng_init;
	}

	err = owl_adf_attachment_allow(owladf);
	if (err < 0) {
		dev_warn(dev, "Failed to attachment allow (%d)\n", err);
		/* just a warning, we will continue */
	}

	owladf->ion_client = ion_client_create(owl_ion_device, "owl_adf");
	if (IS_ERR(owladf->ion_client)) {
		err = PTR_ERR(owladf->ion_client);
		ADFERR("Failed to create ADF ION client (%d)\n", err);
		goto err_ion_create;
	}

	err = owl_adf_fbdev_init(owladf);
	if (err < 0) {
		ADFERR("Failed to init ADF fbdev (%d)\n", err);
		goto err_fbdev_init;
	}

	for (i = 0; i < owladf->n_intfs; i++) {
		owlintf = &owladf->intfs[i];

		if (PANEL_IS_PRIMARY(owlintf->panel) ||
		    owl_panel_hpd_is_connected(owlintf->panel)) {
			ADFINFO("%s: intf%d connected\n",
				__func__, owlintf->intf.base.id);

			/* re-set adf interface's current mode */
			get_drm_mode_from_panel(owlintf->panel,
						&owlintf->intf.current_mode);

			adf_hotplug_notify_connected(&owlintf->intf, NULL, 0);
		}
	}

	ADFINFO("%s SUCCESS\n", __func__);
	return 0;

err_fbdev_init:
	ion_client_destroy(owladf->ion_client);

err_ion_create:
	owl_adf_engs_deinit(owladf);

err_eng_init:
	owl_adf_intfs_deinit(owladf);

err_inf_init:
	adf_device_destroy(&owladf->adfdev);

err_dev_init:
	devm_kfree(dev, owladf);

err_out:
	ADFINFO("%s FAIL\n", __func__);
	return err;
}


static int owl_adf_remove(struct platform_device *pdev)
{
	struct owl_adf_device *owladf = platform_get_drvdata(pdev);

	ion_client_destroy(owladf->ion_client);

	owl_adf_fbdev_deinit(owladf);

	owl_adf_engs_deinit(owladf);
	owl_adf_intfs_deinit(owladf);
	adf_device_destroy(&owladf->adfdev);

	devm_kfree(&pdev->dev, owladf);

	ADFINFO("%s SUCCESS\n", __func__);
	return 0;
}

/* now only ensure backlight is off, others TODO */
static void owl_adf_shutdown(struct platform_device *pdev)
{
	struct fb_event event;
	int fb_state;

	fb_state = FB_BLANK_POWERDOWN;
	event.data = &fb_state;
	fb_notifier_call_chain(FB_EVENT_BLANK, &event);

	ADFINFO("%s SUCCESS\n", __func__);
}

static struct platform_device owl_adf_pdev = {
	.name		= "owl-adf",
	.id		= -1,
	.num_resources	= 0,
};

static struct platform_driver owl_adf_pdrv = {
	.driver		= {
		.name   = "owl-adf",
		.owner  = THIS_MODULE,
	},
	.probe		= owl_adf_probe,
	.remove		= owl_adf_remove,
	.shutdown	= owl_adf_shutdown,
};

static int __init owl_adf_init(void)
{
	ADFINFO("%s\n", __func__);

	if (platform_device_register(&owl_adf_pdev)) {
		pr_err("failed to register owl_adf_pdev\n");
		return -ENODEV;
	}


	if (platform_driver_register(&owl_adf_pdrv)) {
		pr_err("failed to register owl_adf_pdrv\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit owl_adf_exit(void)
{
	ADFINFO("%s\n", __func__);

	platform_device_unregister(&owl_adf_pdev);
	platform_driver_unregister(&owl_adf_pdrv);
}

module_init(owl_adf_init);
module_exit(owl_adf_exit);

module_param(test_stop_post, int, 0644);
module_param(test_gpu_compose, int, 0644);

MODULE_AUTHOR("lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL ADF Driver");
MODULE_LICENSE("GPL");
