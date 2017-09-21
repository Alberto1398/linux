/*
 * Camera module common Driver
 *
 * Copyright (C) 2011 Actions Semiconductor Co.,LTD
 * Wang Xin <wangxin@actions-semi.com>
 *
 * Based on gc2035 driver
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>
#include <media/soc_camera.h>
#include <media/v4l2-chip-ident.h>
#include <linux/delay.h>
#include <linux/device.h>
#include "module_comm.h"
#include "./../flashlight/flashlight.h"

/*#define GL5209_EVB_DDR_V01_V02*/
bool light_on;

static struct camera_module_priv
*to_camera_priv(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client),
			    struct camera_module_priv, subdev);
}

static int parse_config_info(struct soc_camera_link *link,
			     struct dts_sensor_config *dsc, const char *name)
{
	struct device_node *fdt_node;

	fdt_node = of_find_compatible_node(NULL, NULL, name);
	if (NULL == fdt_node) {
		DBG_ERR("no sensor [%s]", name);
		goto fail;
	}
	dsc->dn = fdt_node;

	dsc->i2c_adapter = get_parent_node_id(fdt_node, "i2c_adapter", "i2c");
	if (dsc->i2c_adapter < 0) {
#if USE_AS_FRONT
	    dsc->i2c_adapter = get_parent_node_id(fdt_node, "front_i2c_adapter", "i2c");
#endif

#if USE_AS_REAR
	    dsc->i2c_adapter = get_parent_node_id(fdt_node, "rear_i2c_adapter", "i2c");
#endif
	    if (dsc->i2c_adapter < 0) {
		    DBG_ERR("fail to get i2c adapter id");
		    goto fail;
        }
	}
    if (link) {
        link->i2c_adapter_id = dsc->i2c_adapter; 
    }

	return 0;

 fail:
	return -EINVAL;
}

static int camera_module_init_process(struct i2c_client *client)
{
	int ret = 0;

	sensor_power_on(g_sensor_cfg.rear, &g__spinfo, true);

	ret = module_soft_reset(client);
	if (0 > ret)
		return ret;

	ret = camera_write_array(client->adapter, module_init_regs);
	if (0 > ret)
		return ret;

	update_after_init(client->adapter);
#ifdef CAMERA_MODULE_WITH_MOTOR
	ret = motor_init(client);
	if (0 > ret)
		return ret;
#endif

#ifdef MODULE_SUPPORT_AF
	ret = camera_write_array(client->adapter, module_init_auto_focus);
	if (0 > ret)
		return ret;
	msleep(20);
#endif

#ifdef DOWNLOAD_AF_FW
	download_fw = true;
#endif

	return ret;
}

static struct camera_module_win_size *camera_module_select_win(u32 width,
							       u32 height)
{
	unsigned int diff = 0;
	struct camera_module_win_size *win = NULL;
	int win_num = 0;
	int i = 0;
	int j = 0;

	DBG_INFO("width:%d, height:%d", width, height);
	win_num = ARRAY_SIZE(module_win_list);
	if (win_num == 1) {
		win = module_win_list[0];
		j = 0;
	} else {
		diff = abs(width - module_win_list[0]->width) +
		    abs(height - module_win_list[0]->height);
		win = module_win_list[0];
		j = 0;
		for (i = 1; i < win_num; i++) {
			if (diff > abs(width - module_win_list[i]->width) +
			    abs(height - module_win_list[i]->height)) {
				win = module_win_list[i];
				j = i;
				diff = abs(width - module_win_list[i]->width) +
				    abs(height - module_win_list[i]->height);
			}
		}
	}

	return win;
}

static int camera_module_get_params(struct i2c_client *client,
				    u32 *width, u32 *height)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;

	if (priv->info->flags & SENSOR_FLAG_CHANNEL0)
		module_set_color_format(mf % 10);
	else
		module_set_color_format(mf / 10);

	/*
	 * select format
	 */
	priv->cfmt = module_cfmts;

	if (!priv->cfmt) {
		DBG_INFO("Unsupported sensor format.");
		goto module_get_fmt_error;
	}
	priv->win = camera_module_select_win(*width, *height);
	*width = priv->win->width;
	*height = priv->win->height;
	DBG_INFO("current params: %s %dX%d\n", priv->win->name, *width,
		 *height);
	return ret;

 module_get_fmt_error:
	priv->win = NULL;
	priv->cfmt = NULL;
	return -1;
}

static int camera_module_set_params(struct i2c_client *client, u32 *width,
				    u32 *height, enum v4l2_mbus_pixelcode code)
{
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;
	int i = 0;

	/*
	 * select format
	 */
	priv->cfmt = NULL;
	for (i = 0; i < ARRAY_SIZE(module_cfmts); i++) {
		if (code == module_cfmts[i].code) {
			priv->cfmt = module_cfmts + i;
			break;
		}
	}

	if (!priv->cfmt) {
		DBG_INFO("Unsupported sensor format.");
		goto module_set_fmt_error;
	}

	/*
	 * select win
	 */
	priv->win = camera_module_select_win(*width, *height);
	DBG_INFO("the window name is %s", priv->win->name);
	ret = camera_write_array(client->adapter, priv->win->win_regs);
	if (ret < 0)
		return ret;

	if (priv->info->flags & SENSOR_FLAG_CHANNEL0)
		module_set_mirror_flip(client, mf % 10);
	else
		module_set_mirror_flip(client, mf / 10);

	ret = module_set_mbusformat(client, priv->cfmt);

	if (ret < 0) {
		DBG_ERR("module set mbus format error.");
		goto module_set_fmt_error;
	}

	*width = priv->win->width;
	*height = priv->win->height;

	return ret;

 module_set_fmt_error:
	module_soft_reset(client);
	priv->win = NULL;
	priv->cfmt = NULL;

	return ret;
}

static int camera_module_set_flash_led_mode(struct v4l2_subdev *sd,
					    struct v4l2_ctrl *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int mode = ctrl->val;

	switch (mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		DBG_INFO("----V4L2_FLASH_LED_MODE_NONE %d",
		       V4L2_FLASH_LED_MODE_NONE);
		priv->flash_led_mode = mode;
		flashlight_control(FLASHLIGHT_OFF);
		break;

	case V4L2_FLASH_LED_MODE_TORCH:
		DBG_INFO("----V4L2_FLASH_LED_MODE_TORCH %d",
		       V4L2_FLASH_LED_MODE_TORCH);
		priv->flash_led_mode = mode;
		flashlight_control(FLASHLIGHT_TORCH);
		break;

	case V4L2_FLASH_LED_MODE_FLASH:
		DBG_INFO("----V4L2_FLASH_LED_MODE_FLASH %d",
		       V4L2_FLASH_LED_MODE_FLASH);
		priv->flash_led_mode = mode;
		break;

	case V4L2_FLASH_LED_MODE_AUTO:
		DBG_INFO("----V4L2_FLASH_LED_MODE_AUTO %d",
		       V4L2_FLASH_LED_MODE_AUTO);
		priv->flash_led_mode = mode;
		break;

	default:
		return -ERANGE;
	}

	ctrl->cur.val = mode;

	return 0;
}

/*
 * v4l2_subdev_core_ops function
 */
static int camera_module_g_chip_ident(struct v4l2_subdev *sd,
				      struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);

	id->ident = priv->model;
	id->revision = 0;

	return 0;
}

/*
 * v4l2_ctrl_ops function
 */
static int camera_module_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct camera_module_priv *priv = container_of(ctrl->handler,
						       struct
						       camera_module_priv, hdl);
	struct v4l2_subdev *sd = &priv->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		module_set_exposure(client, ctrl->val);
		break;

	case V4L2_CID_GAIN:
		module_set_gain(client, ctrl->val);
		break;

	case V4L2_CID_AUTO_WHITE_BALANCE:
		module_set_auto_white_balance(sd, ctrl);
		break;

	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		module_set_white_balance_temperature(sd, ctrl);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		module_s_mirror_flip(sd, ctrl);
		break;

	case V4L2_CID_COLORFX:
		module_set_colorfx(sd, ctrl);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		module_set_exposure_auto(sd, ctrl);
		break;
	case V4L2_CID_SCENE_MODE:
		module_set_scene_exposure(sd, ctrl);
		break;

	case V4L2_CID_AF_MODE:
		module_set_af_mode(sd, ctrl);
		break;

	case V4L2_CID_AF_STATUS:
		break;
	case V4L2_CID_AF_REGION:
		module_set_af_region(sd, ctrl);
		break;
#ifdef CAMERA_MODULE_WITH_MOTOR
	case V4L2_CID_MOTOR:
		motor_set_pos(sd, ctrl);
		break;
#endif
	case V4L2_CID_FLASH_LED_MODE:
		if (priv->info->flags & SENSOR_FLAG_CHANNEL0)
			camera_module_set_flash_led_mode(sd, ctrl);
		break;

	case V4L2_CID_FLASH_STROBE:
		if (priv->info->flags & SENSOR_FLAG_CHANNEL0) {
			flashlight_control(FLASHLIGHT_TORCH);
			light_on = true;
		}
		break;

	case V4L2_CID_FLASH_STROBE_STOP:
		if (priv->info->flags & SENSOR_FLAG_CHANNEL0) {
			flashlight_control(TORCHLIGHT_OFF);
			light_on = false;
		}
		break;

	case V4L2_CID_EXPOSURE_COMP:
		module_set_ev(sd, ctrl);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		module_set_power_line(sd, ctrl);
		break;
	default:
		DBG_INFO("v4l2-ctrl set %s, %d out of range",
			 v4l2_ctrl_get_name(ctrl->id), ctrl->val);
		return 0;
	}

	return ret;
}

static int camera_module_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct camera_module_priv *priv = container_of(ctrl->handler,
						       struct
						       camera_module_priv, hdl);
	struct v4l2_subdev *sd = &priv->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_AF_STATUS:
		ret = module_get_af_status(priv, ctrl);
		break;

	case V4L2_CID_GAIN:
		ret = module_get_gain(client, &ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		ret = module_get_exposure(client, &ctrl->val);
		break;

	case V4L2_CID_AF_MODE:
		module_get_af_mode(sd, ctrl);
		break;

#ifdef CAMERA_MODULE_WITH_MOTOR
	case V4L2_CID_MOTOR:
		motor_get_pos(sd, ctrl);
		break;
	case V4L2_CID_MOTOR_GET_MAX:
		motor_get_max_pos(sd, ctrl);
		break;
#endif
	case V4L2_CID_SENSOR_ID:
		get_sensor_id(sd, ctrl);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = module_get_power_line(sd, ctrl);
		break;
	default:
		DBG_ERR("v4l2-ctrl get %s out of range",
			v4l2_ctrl_get_name(ctrl->id));
		return 0;
	}
	return 0;
}

/*
 * v4l2_subdev_video_ops function
 */
static int camera_module_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;

	if (enable && (ACTS_PREVIEW_MODE == priv->pcv_mode)) {
		DBG_INFO("ACTS_PREVIEW_MODE....");
		enter_preview_mode(client->adapter);
	}
	if (enable && (ACTS_CAPTURE_MODE == priv->pcv_mode)) {
		DBG_INFO("ACTS_CAPTURE_MODE....");
		enter_capture_mode(client->adapter);
	}
	if (enable) {
		if (light_on)
			flashlight_control(FLASHLIGHT_FLASH);
	} else {
		if (light_on)
			flashlight_control(FLASHLIGHT_OFF);
	}
	ret = module_set_stream(client, enable);

	if (ACTS_PREVIEW_MODE == priv->pcv_mode) {
		ret |= module_save_exposure_param(sd);
	} else if (ACTS_CAPTURE_MODE == priv->pcv_mode) {
		ret |= module_freeze_aec(sd);
		ret |= module_set_exposure_param(sd);
		ret |= module_start_aec(sd);
	}

	return ret;
}

static int camera_module_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left = 0;
	a->bounds.top = 0;
	a->bounds.width = MODULE_MAX_WIDTH;
	a->bounds.height = MODULE_MAX_HEIGHT;
	a->defrect = a->bounds;
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator = 1;
	a->pixelaspect.denominator = 30;

	return 0;
}

static int camera_module_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *vc)
{
	vc->c.left = 0;
	vc->c.top = 0;
	vc->c.width = MODULE_MAX_WIDTH;
	vc->c.height = MODULE_MAX_HEIGHT;
	vc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int camera_module_g_parm(struct v4l2_subdev *sd,
				struct v4l2_streamparm *parms)
{
	return 0;
}

static int camera_module_s_parm(struct v4l2_subdev *sd,
				struct v4l2_streamparm *parms)
{
	return 0;
}

static int camera_module_enum_framesizes(struct v4l2_subdev *sd,
					 struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= ARRAY_SIZE(module_win_list))
		return -EINVAL;

	switch (fsize->pixel_format) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUYV:
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = module_win_list[fsize->index]->width;
		fsize->discrete.height = module_win_list[fsize->index]->height;
		fsize->reserved[0] =
		    module_win_list[fsize->index]->capture_only;
		break;

	default:
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = module_win_list[fsize->index]->width;
		fsize->discrete.height = module_win_list[fsize->index]->height;
		fsize->reserved[0] =
		    module_win_list[fsize->index]->capture_only;
		break;
	}

	return 0;
}

static int camera_module_enum_frameintervals(struct v4l2_subdev *sd,
					     struct v4l2_frmivalenum *fival)
{
	const struct camera_module_win_size *win_size =
	    camera_module_select_win(fival->width, fival->height);
	unsigned int array_size = sizeof(win_size->frame_rate_array) /
	    sizeof(unsigned int);

	if (fival->index >= array_size)
		return -EINVAL;

	if ((win_size->width != fival->width) ||
	    (win_size->height != fival->height)) {
		DBG_INFO("width(%d) height(%d) is over range.",
		       fival->width, fival->height);
		return -EINVAL;
	}

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator =
	    (win_size->frame_rate_array)[fival->index];

	return 0;
}

static int camera_module_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
				  enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(module_cfmts))
		return -EINVAL;

	*code = module_cfmts[index].code;

	return 0;
}

static int camera_module_g_fmt(struct v4l2_subdev *sd,
			       struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;

	if (!priv->win || !priv->cfmt) {
		u32 width = MODULE_DEFAULT_WIDTH;
		u32 height = MODULE_DEFAULT_HEIGHT;

		ret = camera_module_get_params(client, &width, &height);
		if (ret < 0)
			return ret;
	}

	mf->width = priv->win->width;
	mf->height = priv->win->height;
	mf->code = priv->cfmt->code;
	mf->colorspace = priv->cfmt->colorspace;
	mf->field = V4L2_FIELD_NONE;

	return ret;
}

static int camera_module_try_fmt(struct v4l2_subdev *sd,
				 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	const struct camera_module_win_size *win;
	int i = 0;

	/*select suitable win */
	win = camera_module_select_win(mf->width, mf->height);

	mf->width = win->width;
	mf->height = win->height;
	mf->field = V4L2_FIELD_NONE;

	for (i = 0; i < ARRAY_SIZE(module_cfmts); i++) {
		if (mf->code == module_cfmts[i].code)
			break;
	}

	if (i == ARRAY_SIZE(module_cfmts)) {
		/* Unsupported format requested. Propose either */
		if (priv->cfmt) {
			/* the current one or */
			mf->colorspace = priv->cfmt->colorspace;
			mf->code = priv->cfmt->code;
		} else {
			/* the default one */
			mf->colorspace = module_cfmts[0].colorspace;
			mf->code = module_cfmts[0].code;
		}
	} else {
		/* Also return the colorspace */
		mf->colorspace = module_cfmts[i].colorspace;
	}

	return 0;
}

static int camera_module_s_fmt(struct v4l2_subdev *sd,
			       struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);
	int ret = 0;

	ret = camera_module_set_params(client, &mf->width, &mf->height,
				       mf->code);
	if (!ret)
		mf->colorspace = priv->cfmt->colorspace;

	return ret;
}

static int camera_module_g_mbus_config(struct v4l2_subdev *sd,
				       struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *desc = soc_camera_i2c_to_desc(client);

	switch ((camera_module_info.flags & SENSOR_FLAG_INTF_MASK) >> 1) {
	case 0:
		cfg->type = V4L2_MBUS_CSI2;
		cfg->flags = 1 << (camera_module_info.mipi_cfg->lan_num) |
		    V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;
		break;
	case 1:
		cfg->type = V4L2_MBUS_PARALLEL;
		cfg->flags = DEFAULT_PCLK_SAMPLE_EDGE | V4L2_MBUS_MASTER |
		    V4L2_MBUS_HSYNC_ACTIVE_HIGH | DEFAULT_VSYNC_ACTIVE_LEVEL |
		    V4L2_MBUS_DATA_ACTIVE_HIGH;
		break;
	default:
		cfg->type = V4L2_MBUS_PARALLEL;
		cfg->flags = DEFAULT_PCLK_SAMPLE_EDGE | V4L2_MBUS_MASTER |
		    V4L2_MBUS_HSYNC_ACTIVE_HIGH | DEFAULT_VSYNC_ACTIVE_LEVEL |
		    V4L2_MBUS_DATA_ACTIVE_HIGH;
		break;
	}

	cfg->flags = soc_camera_apply_board_flags(desc, cfg);

	return 0;
}

static int camera_module_s_mbus_config(struct v4l2_subdev *sd,
				       const struct v4l2_mbus_config *cfg)
{
	return 0;
}

static long camera_module_ioctrl(struct v4l2_subdev *sd,
				 unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);

	switch (cmd) {
	case V4L2_CID_CAM_CV_MODE:{
			int mode = *(int *)arg;

			if (mode < ACTS_PREVIEW_MODE
				|| mode > ACTS_VIDEO_MODE) {
				return -EINVAL;
			}
			priv->pcv_mode = mode;
			break;
		}
	default:
		DBG_ERR("Don't support current cmd:0x%x", cmd);
		return -EINVAL;
	}

	switch (priv->pcv_mode) {
	case ACTS_PREVIEW_MODE:
		DBG_INFO("Preview Mode\n");
		break;
	case ACTS_CAPTURE_MODE:
		DBG_INFO("Capture Mode\n");
		break;
	case ACTS_VIDEO_MODE:
		DBG_INFO("Video Mode\n");
		break;
	default:
		DBG_ERR("out of range");
		break;
	}

	return 0;
}

static int camera_module_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *desc = soc_camera_i2c_to_desc(client);
	int ret = 0;

	DBG_INFO("%s", on ? "on" : "off");
	if (!on)
		return soc_camera_power_off(&client->dev, desc);

#ifdef GL5209_EVB_DDR_V01_V02
	ret = camera_module_init_process(client);
#else
	ret = soc_camera_power_on(&client->dev, desc);
	if (ret < 0)
		return ret;

	ret = camera_write_array(client->adapter, module_init_regs);
#endif

	return ret;
}

static int camera_module_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_module_priv *priv = to_camera_priv(client);

	if (ACTS_CAPTURE_MODE == priv->pcv_mode)
		*frames = DROP_NUM_CAPTURE;
	else
		*frames = DROP_NUM_PREVIEW;

	DBG_INFO("skip %d frames.", *frames);
	return 0;
}

static const struct v4l2_ctrl_ops camera_module_ctrl_ops = {
	.g_volatile_ctrl = camera_module_g_volatile_ctrl,
	.s_ctrl = camera_module_s_ctrl,
};

static struct v4l2_subdev_sensor_ops module_subdev_sensor_ops = {
	.g_skip_frames = camera_module_g_skip_frames,
};

static struct v4l2_subdev_core_ops camera_module_subdev_core_ops = {
	.g_chip_ident = camera_module_g_chip_ident,
	.ioctl = camera_module_ioctrl,
	.s_power = camera_module_s_power,
};

static struct v4l2_subdev_video_ops camera_module_subdev_video_ops = {
	.s_stream = camera_module_s_stream,
	.cropcap = camera_module_cropcap,
	.g_crop = camera_module_g_crop,
	.g_parm = camera_module_g_parm,
	.s_parm = camera_module_s_parm,
	.enum_framesizes = camera_module_enum_framesizes,
	.enum_frameintervals = camera_module_enum_frameintervals,
	.enum_mbus_fmt = camera_module_enum_fmt,
	.g_mbus_fmt = camera_module_g_fmt,
	.try_mbus_fmt = camera_module_try_fmt,
	.s_mbus_fmt = camera_module_s_fmt,
	.g_mbus_config = camera_module_g_mbus_config,
	.s_mbus_config = camera_module_s_mbus_config,
};

static struct v4l2_subdev_ops module_subdev_ops = {
	.core = &camera_module_subdev_core_ops,
	.video = &camera_module_subdev_video_ops,
	.sensor = &module_subdev_sensor_ops,
};

static void camera_module_priv_init(struct camera_module_priv *priv)
{
	priv->pcv_mode = ACTS_PREVIEW_MODE;
	priv->exposure_auto = 1;
	priv->auto_white_balance = 1;
	priv->power_line_frequency = DEFAULT_POWER_LINE_FREQUENCY;
	priv->power_line_frequency = V4L2_CID_POWER_LINE_FREQUENCY_50HZ;
	priv->win = NULL;
	priv->af_status = AF_STATUS_DISABLE;
	priv->af_mode = CONTINUE_AF;

	return;
}

static void camera_module_init_ops(struct v4l2_ctrl_handler *hdl,
				   const struct v4l2_ctrl_ops *ops)
{
	unsigned int cmd_array_size = ARRAY_SIZE(v4l2_ctl_array);
	unsigned int cmd_menu_array_size = ARRAY_SIZE(v4l2_ctl_array_menu);

	struct v4l2_ctrl *ret = NULL;
	unsigned int i = 0;

	v4l2_ctrl_handler_init(hdl, cmd_array_size + cmd_menu_array_size);

	for (i = 0; i < cmd_array_size; i++) {
		const struct v4l2_ctl_cmd_info *pctl = v4l2_ctl_array + i;

		ret = v4l2_ctrl_new_std(hdl, ops, pctl->id, pctl->min,
					pctl->max, pctl->step, pctl->def);

		if (NULL == ret) {
			DBG_ERR
			    ("ctr[%d] - id:%d, min:%d, max:%d, step:%d, def:%d",
			     i, pctl->id, pctl->min, pctl->max, pctl->step,
			     pctl->def);
		}
		if ((pctl->id == V4L2_CID_GAIN)
		    || (pctl->id == V4L2_CID_POWER_LINE_FREQUENCY)
		    || (pctl->id == V4L2_CID_AF_STATUS)
		    || (pctl->id == V4L2_CID_AF_MODE)
		    || (pctl->id == V4L2_CID_EXPOSURE)
		    || (pctl->id == V4L2_CID_MOTOR)
		    || (pctl->id == V4L2_CID_MOTOR_GET_MAX)
		    || (pctl->id == V4L2_CID_AF_REGION)
		    || (pctl->id == V4L2_CID_SENSOR_ID)) {
			if (ret != NULL) {
				ret->flags |= V4L2_CTRL_FLAG_VOLATILE;
				ret = NULL;
			}
		}

		hdl->error = 0;
	}
	for (i = 0; i < cmd_menu_array_size; i++) {
		const struct v4l2_ctl_cmd_info_menu *pmenu =
		    v4l2_ctl_array_menu + i;
		if ((pmenu->id == V4L2_CID_FLASH_LED_MODE) &&
		    (!gpio_flash_cfg_exist)) {
			continue;
		}

		ret = v4l2_ctrl_new_std_menu(hdl, ops, pmenu->id, pmenu->max,
					     pmenu->mask, pmenu->def);

		if (NULL == ret) {
			DBG_ERR("menu[%d] - id:%d, max:%d, mask:%d, def:%d", i,
				pmenu->id, pmenu->max, pmenu->mask, pmenu->def);
		}
		hdl->error = 0;
	}
	return;
}

static int camera_module_probe(struct i2c_client *client,
			       const struct i2c_device_id *did)
{
	int ret = 0;
	struct camera_module_priv *priv;
	struct soc_camera_subdev_desc *desc = soc_camera_i2c_to_desc(client);
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	struct v4l2_subdev *subdev;

	DBG_INFO("%s probe start...", CAMERA_MODULE_NAME);
	DBG_INFO("flags:0x%x, addr:0x%x, name:%s, irq:0x%x",
		 client->flags, client->addr, client->name, client->irq);

	if (NULL == desc) {
		DBG_ERR("error: camera module missing soc camera link");
		return -EINVAL;
	}
	if (NULL == desc->drv_priv) {
		DBG_ERR("error: no init module_info of camera module");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		DBG_ERR
		    ("I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE_DATA\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	camera_module_priv_init(priv);
	priv->info = desc->drv_priv;

	v4l2_i2c_subdev_init(&priv->subdev, client, &module_subdev_ops);
	camera_module_init_ops(&priv->hdl, &camera_module_ctrl_ops);
	priv->subdev.ctrl_handler = &priv->hdl;
	priv->hdl.error = 0;
	if (priv->hdl.error) {
		ret = priv->hdl.error;
		kfree(priv);
		DBG_ERR("module init error!");
		return ret;
	}

	subdev = i2c_get_clientdata(client);

#ifndef GL5209_EVB_DDR_V01_V02
	ret = camera_module_init_process(client);

	/*In order to save power, we power off sensor here. */
	//sensor_power_off(g_sensor_cfg.rear, &g__spinfo, false);
#endif

	priv->pcv_mode = ACTS_PREVIEW_MODE;
	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

static int camera_module_remove(struct i2c_client *client)
{
	struct camera_module_priv *priv = to_camera_priv(client);

	v4l2_device_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
	kfree(priv);

	return 0;
}

#define CAMERA_COMMON_NAME           "sensor_common"

/* soc_camer_link's hooks */
static int camera_module_power(struct device *dev, int mode)
{
	if (mode)
		sensor_power_on(g_sensor_cfg.rear, &g__spinfo, false);
	else
		sensor_power_off(g_sensor_cfg.rear, &g__spinfo, false);

	return 0;
}

static int camera_module_reset(struct device *dev)
{
	return 0;
}

static struct i2c_board_info asoc_i2c_camera = {
	I2C_BOARD_INFO(CAMERA_MODULE_NAME, MODULE_I2C_REG_ADDRESS),
};

static const unsigned short camera_module_addrs[] = {
	MODULE_I2C_REG_ADDRESS,
	I2C_CLIENT_END,
};

static struct soc_camera_link camera_module_link = {
	.bus_id = 0,
	.power = camera_module_power,
	.reset = camera_module_reset,
	.board_info = &asoc_i2c_camera,
	.i2c_adapter_id = 2,	/*id num start from 0 */
	.module_name = CAMERA_MODULE_NAME,
	.priv = &camera_module_info,
};

static struct platform_device asoc_camera_device = {
	.name = "soc-camera-pdrv",
	.id = 0,
	.dev = {
		.platform_data = &camera_module_link,
		},
};

static const struct i2c_device_id camera_module_id[] = {
	{CAMERA_MODULE_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, camera_module_id);

static int camera_module_suspend(struct i2c_client *client, pm_message_t mesg)
{
	sensor_power_off(g_sensor_cfg.rear, &g__spinfo, true);
	return 0;
}

static int camera_module_resume(struct i2c_client *client)
{
	return camera_module_init_process(client);
}

static struct i2c_driver camera_i2c_driver = {
	.driver = {
		   .name = CAMERA_MODULE_NAME,
		   },
	.probe = camera_module_probe,
	.suspend = camera_module_suspend,
	.resume = camera_module_resume,
	.remove = camera_module_remove,
	.id_table = camera_module_id,
};

static int sensor_mod_init(struct soc_camera_link *link,
			   struct platform_device *pdev,
			   struct i2c_driver *idrv)
{
	struct dts_sensor_config *dsc = &g_sensor_cfg;
	int ret = 0;
	int camera_id;

	ret = parse_config_info(link, dsc, CAMERA_COMMON_NAME);
	if (ret) {
		DBG_ERR("fail go get config");
		goto err;
	}

	pdev->dev.of_node = dsc->dn;
	dsc->dev = &pdev->dev;

	ret = detect_init();
	if (ret) {
		DBG_ERR("module detect init error.");
		goto err;
	}
	camera_id = detect_work();

	if (SENSOR_REAR == camera_id) {
		dsc->rear = 1;
		camera_module_info.video_devnum = 0;
	} else if (SENSOR_FRONT == camera_id) {
		dsc->rear = 0;
		camera_module_info.video_devnum = 1;
	} else {
		detect_deinit_power_off();
		return camera_id;
	}

	DBG_INFO("install as [%s] camera\n", (dsc->rear ? "REAR" : "FRONT"));
	DBG_INFO("i2c adapter[%d], host[%d], channel[%d]\n",
		 dsc->i2c_adapter, dsc->host, dsc->channel);

	pdev->id = !(!dsc->rear);

	DBG_INFO("sensor_mod_init():platform_device_register: %s.%d\n",
		 pdev->name, pdev->id);
	ret = platform_device_register(pdev);
	if (ret) {
		DBG_ERR("fail to register platform.");
		goto regdev_err;
	}

	DBG_INFO("sensor_mod_init():i2c_add_driver\n");
	ret = i2c_add_driver(idrv);
	if (ret) {
		DBG_ERR("fail to add i2c driver.");
		goto regdrv_err;
	}

	detect_deinit_power_hold();
	return ret;

 regdrv_err:
	platform_device_unregister(pdev);
 regdev_err:
 err:
	detect_deinit_power_off();
	return ret;
}

/* module function */
static int __init camera_module_init(void)
{
	unsigned int ret = 0;

	if (fornt_sensor_detected && rear_sensor_detected)
		return ret;

	ret =
	    sensor_mod_init(&camera_module_link, &asoc_camera_device,
			    &camera_i2c_driver);
	return ret;
}

module_init(camera_module_init);

static void __exit camera_module_exit(void)
{
#if HDMI_INPUT
    free_thread();
#endif

	i2c_del_driver(&camera_i2c_driver);
	platform_device_unregister(&asoc_camera_device);
}

module_exit(camera_module_exit);

MODULE_DESCRIPTION("Camera module driver");
MODULE_AUTHOR("Actions-semi");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");
