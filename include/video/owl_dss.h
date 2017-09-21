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
 *	2015/8/20: Created by Lipeng.
 */
#ifndef _VIDEO_OWL_DSS_H_
#define _VIDEO_OWL_DSS_H_

#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/ktime.h>

struct owl_display_ctrl;
struct owl_panel;

struct owl_de_path;
struct owl_de_video;
struct owl_de_device;

/*=============================================================================
 *				OWL DSS Common
 *===========================================================================*/

/* horizontal&vertical sync high active */
#define DSS_SYNC_HOR_HIGH_ACT		(1 << 0)
#define DSS_SYNC_VERT_HIGH_ACT		(1 << 1)

#define DSS_VMODE_NONINTERLACED		0	/* non interlaced */
#define DSS_VMODE_INTERLACED		1	/* interlaced */

struct owl_videomode {
	int xres;		/* visible resolution */
	int yres;
	int refresh;		/* vertical refresh rate in hz */

	/*
	 * Timing: All values in pixclocks, except pixclock
	 */

	int pixclock;		/* pixel clock in ps (pico seconds) */
	int hfp;		/* horizontal front porch */
	int hbp;		/* horizontal back porch */
	int vfp;		/* vertical front porch */
	int vbp;		/* vertical back porch */
	int hsw;		/* horizontal synchronization pulse width */
	int vsw;		/* vertical synchronization pulse width */

	int sync;		/* see DSS_SYNC_* */
	int vmode;		/* see DSS_VMODE_* */
};

/*
 * VIDEOMODE_TO_STR,
 * STR_TO_VIDEOMODE:
 *	used for reading and writing videomode through sysfs.
 */

#define VIDEOMODE_TO_STR(mode, str)					\
	snprintf((str), PAGE_SIZE, "%ux%u%c-%uHz\n",			\
		 (mode)->xres, (mode)->yres,				\
		 (mode)->vmode == DSS_VMODE_INTERLACED ? 'i' : 'p',	\
		 (mode)->refresh)

#define STR_TO_VIDEOMODE(mode, str)					\
	do {								\
		char p_or_i;						\
									\
		sscanf((str), "%ux%u%c-%uHz,%u,%u/%u/%u,%u/%u/%u\n",	\
		       &(mode)->xres, &(mode)->yres, &p_or_i,		\
		       &(mode)->refresh, &(mode)->pixclock,		\
		       &(mode)->hsw, &(mode)->hbp, &(mode)->hfp,	\
		       &(mode)->vsw, &(mode)->vbp, &(mode)->vfp);	\
		if (p_or_i == 'i')					\
			(mode)->vmode = DSS_VMODE_INTERLACED;		\
		else							\
			(mode)->vmode = DSS_VMODE_NONINTERLACED;	\
	} while (0)

enum owl_display_type {
	OWL_DISPLAY_TYPE_NONE		= 0,
	OWL_DISPLAY_TYPE_LCD		= 1 << 0,
	OWL_DISPLAY_TYPE_DSI		= 1 << 1,
	OWL_DISPLAY_TYPE_EDP		= 1 << 2,
	OWL_DISPLAY_TYPE_CVBS		= 1 << 3,
	OWL_DISPLAY_TYPE_YPBPR		= 1 << 4,
	OWL_DISPLAY_TYPE_HDMI		= 1 << 5,
	OWL_DISPLAY_TYPE_DUMMY		= 1 << 6,
};

enum owl_color_mode {
	OWL_DSS_COLOR_BGR565		= (1 << 0),
	OWL_DSS_COLOR_RGB565		= (1 << 1),

	OWL_DSS_COLOR_BGRA8888		= (1 << 2),
	OWL_DSS_COLOR_RGBA8888		= (1 << 3),
	OWL_DSS_COLOR_ABGR8888		= (1 << 4),
	OWL_DSS_COLOR_ARGB8888		= (1 << 5),
	OWL_DSS_COLOR_BGRX8888		= (1 << 6),
	OWL_DSS_COLOR_RGBX8888		= (1 << 7),
	OWL_DSS_COLOR_XBGR8888		= (1 << 8),
	OWL_DSS_COLOR_XRGB8888		= (1 << 9),

	OWL_DSS_COLOR_BGRA1555		= (1 << 10),
	OWL_DSS_COLOR_RGBA1555		= (1 << 11),
	OWL_DSS_COLOR_ABGR1555		= (1 << 12),
	OWL_DSS_COLOR_ARGB1555		= (1 << 13),
	OWL_DSS_COLOR_BGRX1555		= (1 << 14),
	OWL_DSS_COLOR_RGBX1555		= (1 << 15),
	OWL_DSS_COLOR_XBGR1555		= (1 << 16),
	OWL_DSS_COLOR_XRGB1555		= (1 << 17),

	/* Y VU420(SP), YUV420 SP DE */
	OWL_DSS_COLOR_NV12		= (1 << 18),
	/* Y UV420(sp) YVU420 SP DE */
	OWL_DSS_COLOR_NV21		= (1 << 19),
	OWL_DSS_COLOR_YVU420		= (1 << 20),/* Y V U420, YUV420 DE */
	OWL_DSS_COLOR_YUV420		= (1 << 21),/* Y U V420, YVU420 DE */
};


/* clockwise rotation angle */
enum owl_rotation {
	OWL_DSS_ROT_0 = 0,
	OWL_DSS_ROT_90,
	OWL_DSS_ROT_180,
	OWL_DSS_ROT_270,
};

enum owl_dither_mode {
	DITHER_DISABLE = 0,
	DITHER_24_TO_16,
	DITHER_24_TO_18,
};

enum owl_blending_type {
	/* no blending */
	OWL_BLENDING_NONE,

	/*
	 * premultiplied, H/W should use the following blending method:
	 * src + (1 - src.a) * dst
	 */
	OWL_BLENDING_PREMULT,

	/*
	 * not premultiplied, H/W should use the following blending method:
	 * src * src.a + (1 - src.a) * dst
	 */
	OWL_BLENDING_COVERAGE
};

enum owl_3d_mode {
	OWL_3D_MODE_2D		= 1 << 0,
	OWL_3D_MODE_LR_HALF	= 1 << 1,
	OWL_3D_MODE_TB_HALF	= 1 << 2,
	OWL_3D_MODE_FRAME	= 1 << 3,
};

/*
 * different device may use different state,
 * @OWL_DSS_STATE_BOOT_ON: some device may need do some special handling
 *	while it switches from BOOT_ON to ON state.
 */
enum owl_dss_state {
	OWL_DSS_STATE_BOOT_ON = 0,
	OWL_DSS_STATE_ON,
	OWL_DSS_STATE_OFF,
	OWL_DSS_STATE_SUSPENDED,
};

int owl_dss_get_color_bpp(enum owl_color_mode color);
bool owl_dss_color_is_rgb(enum owl_color_mode color);

char *owl_dss_3d_mode_to_string(enum owl_3d_mode mode);
enum owl_3d_mode owl_dss_string_to_3d_mode(char *mode);

/*=============================================================================
 *		Display Controller, only used by panel driver
 *===========================================================================*/
struct owl_display_ctrl_ops {
	int (*add_panel)(struct owl_display_ctrl *ctrl,
			 struct owl_panel *panel);
	int (*remove_panel)(struct owl_display_ctrl *ctrl,
			 struct owl_panel *panel);

	int (*power_on)(struct owl_display_ctrl *ctrl);
	int (*power_off)(struct owl_display_ctrl *ctrl);

	int (*enable)(struct owl_display_ctrl *ctrl);
	int (*disable)(struct owl_display_ctrl *ctrl);

	bool (*ctrl_is_enabled)(struct owl_display_ctrl *ctrl);
	/*
	 * sometime(specail for HDMI), panel's hotplug status(whether panel
	 * is connected or not) is detected by controller, so controller
	 * should provice some callbacks, which can reduce
	 * the coupling between controllers and panels.
	 */
	bool (*hpd_is_panel_connected)(struct owl_display_ctrl *ctrl);

	void (*hpd_enable)(struct owl_display_ctrl *ctrl, bool enable);
	bool (*hpd_is_enabled)(struct owl_display_ctrl *ctrl);

	/* optional */
	int (*aux_read)(struct owl_display_ctrl *ctrl, char *buf, int count);
	int (*aux_write)(struct owl_display_ctrl *ctrl, const char *buf,
			int count);

	int (*set_3d_mode)(struct owl_display_ctrl *ctrl,
			   enum owl_3d_mode mode);
	enum owl_3d_mode (*get_3d_mode)(struct owl_display_ctrl *ctrl);
	int (*get_3d_modes)(struct owl_display_ctrl *ctrl);

	/* for debug */
	void (*regs_dump)(struct owl_display_ctrl *ctrl);
};

struct owl_display_ctrl {
	const char			*name;
	enum owl_display_type		type;

	void				*data;

	struct owl_panel		*panel;

	struct owl_display_ctrl_ops	*ops;

	struct list_head		list;
};

int owl_ctrl_init(void);

/* for display device module */
int owl_ctrl_add_panel(struct owl_panel *panel);
void owl_ctrl_remove_panel(struct owl_panel *panel);

int owl_ctrl_power_on(struct owl_display_ctrl *ctrl);
int owl_ctrl_power_off(struct owl_display_ctrl *ctrl);

int owl_ctrl_enable(struct owl_display_ctrl *ctrl);
void owl_ctrl_disable(struct owl_display_ctrl *ctrl);

/* for display controller driver */
int owl_ctrl_register(struct owl_display_ctrl *ctrl);
void owl_ctrl_unregister(struct owl_display_ctrl *ctrl);

struct owl_display_ctrl *owl_ctrl_find_by_type(enum owl_display_type type);

void owl_ctrl_set_drvdata(struct owl_display_ctrl *ctrl, void *data);
void *owl_ctrl_get_drvdata(struct owl_display_ctrl *ctrl);

int owl_ctrl_aux_read(struct owl_display_ctrl *ctrl, char *buf, int count);
void owl_ctrl_aux_write(struct owl_display_ctrl *ctrl, const char *buf,
			int count);
/*=============================================================================
 *				OWL Display Panel
 *===========================================================================*/

#define OWL_PANEL_MAX_VIDEOMODES	(10)

struct owl_panel_ops {
	int (*power_on)(struct owl_panel *panel);
	int (*power_off)(struct owl_panel *panel);
	int (*enable)(struct owl_panel *panel);
	int (*disable)(struct owl_panel *panel);

	/* optional */
	bool (*hpd_is_connected)(struct owl_panel *panel);
	void (*hpd_enable)(struct owl_panel *panel, bool enable);
	bool (*hpd_is_enabled)(struct owl_panel *panel);

	/* optional */
	int (*set_3d_mode)(struct owl_panel *panel, enum owl_3d_mode mode);
	enum owl_3d_mode (*get_3d_mode)(struct owl_panel *panel);
	int (*get_3d_modes)(struct owl_panel *panel);
};

struct owl_dss_panel_desc {
	char				*name;
	enum owl_display_type		type;

	uint32_t			width_mm;
	uint32_t			height_mm;
	uint32_t			bpp;

	uint32_t			power_on_delay;
	uint32_t			power_off_delay;
	uint32_t			enable_delay;
	uint32_t			disable_delay;

	bool				is_primary;
	bool				need_edid;
	bool				is_dummy;

	/* overscan in 4 directions, used by API, unit is pixel */
	int				overscan_left;
	int				overscan_top;
	int				overscan_right;
	int				overscan_bottom;

	/* range is 0 ~ 200 in percent, while 100 is the normal value */
	int				brightness;
	int				min_brightness;
	int				max_brightness;

	int				contrast;
	int				saturation;

	int 				gamma_r_val;
	int 				gamma_g_val;
	int 				gamma_b_val;

	int				preline_num;	/* recommend value */
	int				preline_time;	/* unit is us */
	int				vb_time;	/* unit is us */
	int				vsync_off_us;

	struct owl_panel_ops		*ops;
};

typedef void (*owl_panel_cb_t)(struct owl_panel *panel, void *data, u32 value);

struct owl_panel {
	struct owl_dss_panel_desc	desc;
	enum owl_dss_state		state;

	struct owl_display_ctrl		*ctrl;
	struct owl_de_path		*path;

	/*
	 * video modes this panel support, just suggestion values,
	 * you can specify other configuration as you need.
	 */
	struct owl_videomode		mode_list[OWL_PANEL_MAX_VIDEOMODES];
	int				n_modes;

	/* default videomode used while EDID invalid */
	struct owl_videomode		default_mode;

	/* current video mode is being used */
	struct owl_videomode		mode;

	/*
	 * draw size
	 * in some case, the size of draw buffer is not same
	 * as the actual resolution.
	 */
	int				draw_width;
	int				draw_height;

	struct list_head		list;

	struct device			*dev;
	void				*pdata;

	/*
	 * some specail callbacks,
	 * must provide by up layer and called by DSS
	 */
	owl_panel_cb_t			hotplug_cb;
	void				*hotplug_data;

	owl_panel_cb_t			vsync_cb;
	void				*vsync_data;
};

/*
 * useful macros
 */
#define PANEL_TYPE(panel)		(panel->desc.type)
#define PANEL_WIDTH_MM(panel)		(panel->desc.width_mm)
#define PANEL_HEIGTH_MM(panel)		(panel->desc.height_mm)
#define PANEL_BPP(panel)		(panel->desc.bpp)
#define PANEL_HOTPLUGABLE(panel)	(panel->desc.hotplugable)
#define PANEL_IS_PRIMARY(panel)		(panel->desc.is_primary)
#define PANEL_NEED_EDID(panel)		(panel->desc.need_edid)

/*
 * APIs for panel core & providers
 */
int owl_panel_init(void);

struct owl_panel *owl_panel_alloc(const char *name,
				  enum owl_display_type type);
void owl_panel_free(struct owl_panel *panel);

int owl_panel_register(struct device *parent, struct owl_panel *panel);
void owl_panel_unregister(struct owl_panel *panel);

int owl_panel_parse_panel_info(struct device_node *of_node,
			       struct owl_panel *panel);

void owl_panel_hotplug_cb_set(struct owl_panel *panel, owl_panel_cb_t cb,
			      void *data);
void owl_panel_vsync_cb_set(struct owl_panel *panel, owl_panel_cb_t cb,
			    void *data);

void owl_panel_hotplug_notify(struct owl_panel *dssdev, bool is_connected);
void owl_panel_vsync_notify(struct owl_panel *panel);

/*
 * check panel's connect status, the rule is:
 *	if panel's .hpd_is_connected is not NULL, using it;
 *	or, if its controller's .hpd_is_panel_connected is not NULL, using it;
 *	or, return TRUE.
 */
bool owl_panel_hpd_is_connected(struct owl_panel *panel);

void owl_panel_hpd_enable(struct owl_panel *panel, bool enable);
bool owl_panel_hpd_is_enabled(struct owl_panel *panel);

int owl_panel_get_preline_num(struct owl_panel *panel);
int owl_panel_get_preline_time(struct owl_panel *panel);
int owl_panel_get_vb_time(struct owl_panel *panel);

/*
 * APIs for panel comsumers
 */

int owl_panel_get_panel_num(void);	/* TODO, maybe No need */
bool owl_panel_get_pri_panel_resolution(int *width, int *height); /* Get primary panel resolution */

struct owl_panel *owl_panel_get_by_name(const char *name);
struct owl_panel *owl_panel_get_by_type(enum owl_display_type type);
struct owl_panel *owl_panel_get_next_panel(struct owl_panel *from);
#define  owl_panel_for_each(p)			\
	for (p = owl_panel_get_next_panel(p);	\
	     p;					\
	     p = owl_panel_get_next_panel(p))

void owl_panel_get_default_mode(struct owl_panel *panel,
				struct owl_videomode *mode);
void owl_panel_get_mode(struct owl_panel *panel, struct owl_videomode *mode);
int owl_panel_set_mode(struct owl_panel *panel, struct owl_videomode *mode);
void owl_panel_set_mode_list(struct owl_panel *panel,
			     struct owl_videomode *modes, int n_modes);

void owl_panel_get_resolution(struct owl_panel *panel, int *xres, int *yres);
void owl_panel_get_gamma(struct owl_panel *panel, int *gamma_r_val,
			 int *gamma_g_val, int *gamma_b_val);

/*
 * get valid display area
 * fromat is coor(x, y), widthxheight
 * for overscan reason, display area may be small than resolution
 */
void owl_panel_get_disp_area(struct owl_panel *panel, int *x, int *y,
			     int *width, int *height);

int owl_panel_get_vmode(struct owl_panel *panel);
int owl_panel_get_refresh_rate(struct owl_panel *panel);
void owl_panel_get_draw_size(struct owl_panel *panel, int *width, int *height);
int owl_panel_get_bpp(struct owl_panel *panel);
char *owl_panel_get_name(struct owl_panel *panel);
enum owl_display_type owl_panel_get_type(struct owl_panel *panel);

int owl_panel_enable(struct owl_panel *panel);
void owl_panel_disable(struct owl_panel *panel);
bool owl_panel_is_enabled(struct owl_panel *panel);

/*
 * owl_panel_status_get
 * Get all panels' on/off status, the return value is constructed
 * as following:
 *	bit16			bit15:0
 *	is backlight on		enabled panels, which is a bit map
 *				of 'enum owl_display_type'
 * for example:
 * 0x10002 means:
 *	dsi is on, and backlight is on.
 * 0x10024 means:
 *	epd and hdmi is on, and backlight is on.
 * 0x0 means:
 *	all panels are off, backlight is off too.
 */
int owl_panel_status_get(void);

int owl_panel_3d_mode_set(struct owl_panel *panel, enum owl_3d_mode mode);
enum owl_3d_mode owl_panel_3d_mode_get(struct owl_panel *panel);
int owl_panel_3d_modes_get(struct owl_panel *panel);

int owl_panel_brightness_get(struct owl_panel *panel);
int owl_panel_contrast_get(struct owl_panel *panel);
int owl_panel_saturation_get(struct owl_panel *panel);

void owl_panel_regs_dump(struct owl_panel *panel);

/*=============================================================================
 *				OWL Display Engine
 *===========================================================================*/
#define OWL_DE_VIDEO_MAX_PLANE		(3)

/* range is [min, max] */
struct owl_de_range_type {
	const uint32_t			min;
	const uint32_t			max;
};

enum owl_de_hw_id {
	DE_HW_ID_ATM7059TC,
	DE_HW_ID_ATM7059,
	DE_HW_ID_S900,
	DE_HW_ID_S700,
};

enum owl_de_output_format {
	DE_OUTPUT_FORMAT_RGB = 0,
	DE_OUTPUT_FORMAT_YUV,
};
struct owl_de_path_info {
	enum owl_display_type		type;

	int				width;
	int				height;
	int				vmode;

	enum owl_dither_mode		dither_mode;

	/* gammas, dither, hist, etc. TODO */
	int 				gamma_adjust_needed;
	int 				gamma_r_val;
	int 				gamma_g_val;
	int 				gamma_b_val;
};

struct owl_de_path_ops {
	int (*enable)(struct owl_de_path *path, bool enable);
	bool (*is_enabled)(struct owl_de_path *path);

	/*
	 * @attach
	 * @detach
	 * Attach/Detach DE video @video to/from DE path @path.
	 * Return 0 on success or error code(<0) on failure.
	 *
	 * @detach_all
	 * A convenient to detach all videos from path.
	 */
	int (*attach)(struct owl_de_path *path, struct owl_de_video *video);
	int (*detach)(struct owl_de_path *path, struct owl_de_video *video);
	int (*detach_all)(struct owl_de_path *path);

	void (*apply_info)(struct owl_de_path *path);

	void (*set_fcr)(struct owl_de_path *path);
	bool (*is_fcr_set)(struct owl_de_path *path);

	void (*preline_enable)(struct owl_de_path *path, bool enable);
	bool (*is_preline_enabled)(struct owl_de_path *path);
	bool (*is_preline_pending)(struct owl_de_path *path);
	void (*clear_preline_pending)(struct owl_de_path *path);

	void (*set_gamma_table)(struct owl_de_path *path);
	void (*get_gamma_table)(struct owl_de_path *path);
	void (*gamma_enable)(struct owl_de_path *path, bool enable);
	bool (*is_gamma_enabled)(struct owl_de_path *path);

	bool (*is_vb_valid)(struct owl_de_path *path);
};

struct owl_de_path {
	const int			id;
	const char			*name;
	const uint32_t			supported_displays;

	struct owl_panel		*panel;

	struct owl_de_path_info		info;
	const struct owl_de_path_ops	*ops;

	/* all videos attached to this path */
	struct list_head		videos;

	/* dirty flag, indicate that path FCR need be set */
	bool				dirty;

	struct work_struct		vsync_work;
	ktime_t				vsync_stamp;
	atomic_t			vsync_enable_cnt;
	wait_queue_head_t		vsync_wq;

	/* debugfs */
	struct dentry			*dir;
};

#define OWL_VIDEO_FLAG_ADJUST_DRAW_SIZE  (1 << 2)
struct owl_de_video_info {
	int				n_planes;

	/* 3D use 2 plane, L/R, L/R, L/R */
	unsigned long			addr[OWL_DE_VIDEO_MAX_PLANE * 2];

	unsigned int			offset[OWL_DE_VIDEO_MAX_PLANE];
	unsigned int			pitch[OWL_DE_VIDEO_MAX_PLANE];

	enum owl_color_mode		color_mode;

	/* crop window */
	unsigned short			xoff;
	unsigned short			yoff;
	unsigned short			width;
	unsigned short			height;

	/* display window */
	unsigned short			pos_x;
	unsigned short			pos_y;
	unsigned short			out_width;
	unsigned short			out_height;

	bool				is_original_scaled;

	/* position before scaling, used by DE_S700 */
	unsigned short			real_pos_x;
	unsigned short			real_pos_y;

	uint32_t			rotation;

	/*
	 * bending type for the layer,
	 * pls see the commit of enum owl_blending_type
	 */
	enum owl_blending_type		blending;

	/*
	 * Alpha value applied to the whole layer, coordinate with
	 * blending type, the H/W's blending method should be:
	 * if blending == OWL_BLENDING_NONE
	 *	a = 255
	 *	src.rgb = src.rgb
	 * if blending == OWL_BLENDING_PREMULT
	 *	a = alpha
	 *	src.rgb = src.rgb * a / 255 + (255 - a) * dst / 255
	 * if blending == OWL_BLENDING_COVERAGE
	 *	a = src.a * alpha / 255
	 *	src.rgb = src.rgb * a / 255 + (255 - a) * dst / 255
	 */
	unsigned char			alpha;


	bool				mmu_enable;

	/* range is 0 ~ 200 in percent, while 100 is the normal value */
	int				brightness;
	int				contrast;
	int				saturation;
	uint32_t               flag;
};

struct owl_de_video_ops {
	void (*apply_info)(struct owl_de_video *video);
};

struct owl_de_video_crop_limits {
	/* units is pixel */
	const struct owl_de_range_type	input_width;
	const struct owl_de_range_type	input_height;
	const struct owl_de_range_type	output_width;
	const struct owl_de_range_type	output_height;

	/* 1 / min ~ max, multiplied by 10 */
	const struct owl_de_range_type	scaling_width;
	const struct owl_de_range_type	scaling_height;
};

struct owl_de_video_capacities {
	const uint32_t			supported_colors;

	/* pitch and address align in byte */
	const uint32_t			pitch_align;
	const uint32_t			address_align;

	const struct owl_de_video_crop_limits rgb_limits;
	const struct owl_de_video_crop_limits yuv_limits;
};

struct owl_de_video {
	const int			id;
	const char			*name;

	/*
	 * a bitmask to hold the sibling video layers, which is
	 * specailly for sublayers, which have some limitations
	 * between each other.
	 * if a layer has no sibling, .sibling equal to its 'id'.
	 */
	const uint32_t			sibling;

	/* not const, can be modified at running */
	struct owl_de_video_capacities	capacities;

	const struct owl_de_video_ops	*ops;

	struct owl_de_video_info	info;

	struct owl_de_path		*path;

	/* dirty flag, indicate that its path's FCR need be set */
	bool				dirty;

	/* debugfs */
	struct dentry			*dir;

	/* used for attaching video to path */
	struct list_head		list;
};

struct owl_de_device_ops {
	int (*power_on)(struct owl_de_device *de);
	int (*power_off)(struct owl_de_device *de);

	int (*mmu_config)(struct owl_de_device *de, uint32_t base_addr);

	void (*dump_regs)(struct owl_de_device *de);
	void (*backup_regs)(struct owl_de_device *de);
	void (*restore_regs)(struct owl_de_device *de);
};

/* used for the backup and restore of DE registers */
struct owl_de_device_regs {
	int				reg;
	uint32_t			value;
};

struct owl_de_device {
	enum owl_de_hw_id		hw_id;

	const uint8_t			num_paths;
	struct owl_de_path		*paths;

	const uint8_t			num_videos;
	struct owl_de_video		*videos;

	const struct owl_de_device_ops	*ops;

	enum owl_dss_state		state;
	struct mutex			state_mutex;

	bool				is_ott;

	/*
	 * debugfs
	 */
	struct dentry			*dir;

	/*
	 * common platform resources for every Display Engine
	 */
	struct platform_device		*pdev;
	void __iomem			*base;
	struct reset_control		*rst;
	int				irq;

	/* for registers backup and restore */
	struct owl_de_device_regs	*regs;
	int				regs_cnt;

	/*
	 * special platform resources, DE core will not touch it
	 */
	void				*pdata;
};

/*
 * APIs for providers
 */
int owl_de_register(struct owl_de_device *de);

int owl_de_generic_suspend(struct device *dev);
int owl_de_generic_resume(struct device *dev);

int owl_de_mmu_config(uint32_t base_addr);

/*
 * APIs for consumers
 */

int owl_de_get_path_num(void);
int owl_de_get_video_num(void);

bool owl_de_is_s700(void);
bool owl_de_is_s900(void);
bool owl_de_is_ott(void);

/*
 * path functions
 */
struct owl_de_path *owl_de_path_get_by_type(enum owl_display_type type);
struct owl_de_path *owl_de_path_get_by_id(int id);
struct owl_de_path *owl_de_path_get_by_index(int index);

int owl_de_path_enable(struct owl_de_path *path);
int owl_de_path_disable(struct owl_de_path *path);
bool owl_de_path_is_enabled(struct owl_de_path *path);

int owl_de_path_enable_vsync(struct owl_de_path *path);
int owl_de_path_disable_vsync(struct owl_de_path *path);

void owl_de_path_get_info(struct owl_de_path *path,
			  struct owl_de_path_info *info);
void owl_de_path_set_info(struct owl_de_path *path,
			  struct owl_de_path_info *info);

/*
 * Attach/Detach DE video @video to DE path @path.
 * Return 0 on success or error code(<0) on failure.
 */
int owl_de_path_attach(struct owl_de_path *path, struct owl_de_video *video);
int owl_de_path_detach(struct owl_de_path *path, struct owl_de_video *video);

/* detach all videos from 'path', temp in here, TODO */
int owl_de_path_detach_all(struct owl_de_path *path);

/*
 * Apply all the settings about this path, include path info,
 * and video info of all videos attached to this path.
 * Also, path's FCR will be set.
 */
void owl_de_path_apply(struct owl_de_path *path);
void owl_de_path_wait_for_go(struct owl_de_path *path);

int owl_de_path_add_panel(struct owl_panel *panel);
int owl_de_path_remove_panel(struct owl_panel *panel);

/*
 * video functions
 */
struct owl_de_video *owl_de_video_get_by_id(int id);
struct owl_de_video *owl_de_video_get_by_index(int index);

void owl_de_video_get_info(struct owl_de_video *video,
			   struct owl_de_video_info *info);
void owl_de_video_set_info(struct owl_de_video *video,
			   struct owl_de_video_info *info);

int owl_de_video_info_validate(struct owl_de_video *video,
			       struct owl_de_video_info *info);
/*
 * DE MMU function
 */
int owl_de_mmu_init(struct device *dev);
int owl_de_mmu_sg_table_to_da(struct sg_table *sg, __u64 stamp, u32 *da);


/*===========================================================================
				others
 *===========================================================================*/

/*
 * Recommended preline time in us, the time is
 * related to system's irq(or schedule) latency,
 * and, we should set it according to the actual condition.
 */
#define DSS_RECOMMENDED_PRELINE_TIME    (60)

/*
 * some help functions to operate display panel's gpio(power, reset, etc.).
 */
struct owl_dss_gpio {
	int				gpio;
	int				active_low;
};

int owl_dss_gpio_parse(struct device_node *of_node, const char *propname,
		       struct owl_dss_gpio *gpio);

static inline int owl_dss_gpio_active(struct owl_dss_gpio *gpio)
{
	if (gpio_is_valid(gpio->gpio)) {
		gpio_direction_output(gpio->gpio, !gpio->active_low);
		return 0;
	} else
		return -1;
}

static inline int owl_dss_gpio_deactive(struct owl_dss_gpio *gpio)
{
	if (gpio_is_valid(gpio->gpio)) {
		gpio_direction_output(gpio->gpio, gpio->active_low);
		return 0;
	} else
		return -1;
}

/*
 * bit operations. TODO
 * gives bitfields as start : end,
 * where start is the higher bit number.
 * For example 7:0
 */
#define REG_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define REG_VAL(val, start, end) (((val) << (end)) & REG_MASK(start, end))
#define REG_GET_VAL(val, start, end) (((val) & REG_MASK(start, end)) >> (end))
#define REG_SET_VAL(orig, val, start, end) (((orig) & ~REG_MASK(start, end))\
						 | REG_VAL(val, start, end))

/*
 * WAIT_WITH_TIMEOUT
 *
 * wait a condition until it's true, or timeout.
 *
 * @condition, the condition to wait.
 * @timeout_us, timeout in us, if it is 0, wait forever.
 *
 * if wait success, return the remain time.
 * or, return 0, which means timeout.
 */
#define WAIT_WITH_TIMEOUT(condition, timeout_us)			\
({									\
	int __ret;							\
	ktime_t __time;							\
	int __wait_us = 0;						\
									\
	if ((timeout_us) == 0)						\
		while (!(condition))					\
			;						\
									\
	__time = ktime_get();						\
	while (!(condition)) {						\
		__wait_us = ktime_to_us(ktime_sub(ktime_get(), __time));\
		if (__wait_us > (timeout_us))				\
			break;						\
	}								\
									\
	if (__wait_us > (timeout_us))					\
		__ret = 0;						\
	else								\
		__ret = (timeout_us) - __wait_us;			\
									\
	__ret;								\
})


#endif
