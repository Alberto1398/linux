/*
 * Actions OWL SoC Camera Host driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * LiChi <lichi@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _OWL_CAMERA_H_
#define _OWL_CAMERA_H_

#include <linux/clk-private.h>
#include <linux/device.h>
#include <linux/videodev2.h>
#include "owl_device.h"

#define CAMERA_DATA_HIGH	    1
#define CAMERA_PCLK_RISING	    2
#define CAMERA_HSYNC_HIGH	    4
#define CAMERA_VSYNC_HIGH	    8
#define CAMERA_DATAWIDTH_4	    0x10
#define CAMERA_DATAWIDTH_8	    0x20
#define CAMERA_DATAWIDTH_10	    0x40
#define CAMERA_DATAWIDTH_16	    0x80

#define CAMERA_DATAWIDTH_MASK (CAMERA_DATAWIDTH_4 | \
							CAMERA_DATAWIDTH_8 | \
							CAMERA_DATAWIDTH_10 | \
							CAMERA_DATAWIDTH_16)

enum dev_state {
	DEV_STOP = 0,
	DEV_START,
	DEV_SUSPEND,
	DEV_RESUME,
	DEV_OPEN,
	DEV_CLOSE,
};

/*
 * GL5023 have tow channel, if support all, should register tow soc camera host
 */
struct camera_dev {
	struct soc_camera_device *icds[2];
	unsigned int dvp_mbus_flags;
	struct soc_camera_host soc_host;
	int skip_frames;

	spinlock_t lock;	/* Protects video buffer lists */
	int irq;
	struct pinctrl *mfp;
	struct sensor_pwd_info spinfo;
	struct module_regulators ir;
	struct clk *module_clk_parent;
	struct clk *module_clk;
	struct clk *sensor_clk_parent[2];
	struct clk *sensor_clk;

	/*csi clock src parent correspond to
	   HOST_MODULE_CHANNEL_0/1 (should be 0 or 1) */
	struct clk *csi_clk_parent;
	struct clk *csi_clk;
	/*csi clock correspond to
	   HOST_MODULE_CHANNEL_0/1 (should be 0 or 1) */
	struct clk *ch_clk[2];
};

struct camera_param {
	struct soc_camera_device *icd;
	struct list_head capture;
	struct videobuf_buffer *cur_frm;
	struct videobuf_buffer *prev_frm;
	struct completion wait_stop;

	int started;
	/* SI data offsets within croped by the S700 camera output */
	unsigned int left;
	unsigned int top;
	/* Client output, as seen by the S700 */
	unsigned int width;
	unsigned int height;
	/*
	 * User window from S_CROP / G_CROP, produced by client cropping,
	 * S700 cropping, mapped back onto the client
	 * input window
	 */
	struct v4l2_rect subrect;
	/* Camera cropping rectangle */
	struct v4l2_rect rect;
	const struct soc_mbus_pixelfmt *extra_fmt;
	enum v4l2_mbus_pixelcode code;
	unsigned long flags;
	unsigned int skip_frames;
	int channel;
	enum v4l2_mbus_type bus_type;
	int lane_num;
	int raw_width;
	int data_type;		/*0 for yuv sensor,1 for raw-bayer senso*/
	int (*ext_cmd) (struct v4l2_subdev *sd, int cmd, void *args);
	int b_splited_capture;

	int rb_w;
	int rb_lsub_col_size;
	int rb_lright_cut;
	int rb_lcol_stride;
	int rb_lleft_cut;
	int rb_rsub_col_size;
	int rb_rright_cut;
	int rb_rcol_stride;
	int rb_rleft_cut;
	int rb_rows;
	int rb_cnt;

	phys_addr_t p_raw_addr;

	int n_crop_x;
	int n_crop_y;
	int n_r_skip_num;
	int n_r_skip_size;

	int real_w;
	int real_h;

	/*
	 * 0 disable raw store
	 * 1 enable raw store in preview mode
	 * 2 enable raw store in capture mode
	 * 3 raw store finished oneFrame
	 * 4 raw store finished twoFrame
	 * 5 ....
	 */
	unsigned long b_raw_store_status;
};

/*
#define DEBUG_MODULE_INFO
#define DEBUG_MODULE_ERR
*/

#ifdef DEBUG_MODULE_INFO
#define module_info(fmt, ...) \
		pr_info(fmt, ## __VA_ARGS__)
#else
#define	module_info(fmt, ...) do {} while (0)
#endif

#ifdef DEBUG_MODULE_ERR
#define module_err(fmt, ...) \
		pr_err("%s(L%d) error: " fmt, \
		__func__, __LINE__, ## __VA_ARGS__)
#else
#define	module_err(fmt, ...) do {} while (0)
#endif

#define ALIAS_ID                                "isp"

#define CMU_SENSORCLK_INVT0                     (0x1 << 12)
#define CMU_SENSORCLK_INVT1                     (0x1 << 13)

#define CH1_PRELINE_NUM_MASK                    (0xFFF << 0)
#define CH1_PRELINE_NUM(x)                      ((0xFFF & (x)) << 0)

#define CH1_COL_START(x)                        (0x1FFF & (x))
#define CH1_COL_END(x)                          ((0x1FFF & (x)) << 16)

#define CH1_ROW_START(x)                        (0xFFF & (x))
#define CH1_ROW_END(x)                          ((0xFFF & (x)) << 16)

#define CH2_PRELINE_NUM_MASK                    (0xFFF << 16)
#define CH2_PRELINE_NUM(x)                      ((0xFFF & (x)) << 16)

#define CH2_COL_START(x)                        (0x1FFF & (x))
#define CH2_COL_END(x)                          ((0x1FFF & (x)) << 16)

#define CH2_ROW_START(x)                        (0xFFF & (x))
#define CH2_ROW_END(x)                          ((0xFFF & (x)) << 16)

#define MODULE_ENABLE_EN                        (0x1)

#define MODULE_INT_STAT_RS_FRAME_END_PD         (0x1 << 16)
#define MODULE_INT_STAT_YUV_FRAME_END_PD        (0x1 << 17)
#define MODULE_INT_STAT_FRAME_END_PD            (0x1 << 18)
#define MODULE_INT_STAT_AF_PD                   (0x1 << 19)
#define MODULE_INT_STAT_RB_PL_PD                (0x1 << 20)
#define MODULE_INT_STAT_OVERFLOWL_PD            (0x3 << 29)

/* module_out_fmt reg */
#define MODULE_OUT_FMT_STRIDE1_MASK             (0x1fff << 16)
#define MODULE_OUT_FMT_STRIDE1(x)               ((0x1fff & (x)) << 16)
#define MODULE_OUT_FMT_SEMI_UV_INV              (0x1 << 15)
#define MODULE_OUT_FMT_MASK                     0x3
#define MODULE_OUT_FMT_YUV420                   0x0
#define MODULE_OUT_FMT_YUV422P                  0x1
#define MODULE_OUT_FMT_NV12                     0x2
#define MODULE_OUT_FMT_YUYV                     0x3

#define MODULE_CTL_CHANNEL1_INTF_MIPI           (0x1 << 8)
#define MODULE_CTL_CHANNEL1_INTF_PARAL          (0x0 << 8)
#define MODULE_CTL_CHANNEL1_MODE_MASK           (0x3 << 12)
#define MODULE_CTL_CHANNEL1_MODE_RGB8           (0x2 << 12)
#define MODULE_CTL_CHANNEL1_MODE_RGB10          (0x0 << 12)
#define MODULE_CTL_CHANNEL1_MODE_RGB12          (0x0 << 12)
#define MODULE_CTL_CHANNEL1_MODE_YUYV           (0x1 << 12)
#define MODULE_CTL_CHANNEL1_HSYNC_ACTIVE_HIGH   (0x1 << 11)
#define MODULE_CTL_CHANNEL1_VSYNC_ACTIVE_HIGH   (0x1 << 10)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_MASK      (0x3<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_UYVY      (0x0<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_VYUY      (0x1<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_YUYV      (0x2<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_YVYU      (0x3<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_SBGGR     (0x0<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_SGRBG     (0x1<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_SGBRG     (0x2<<24)
#define MODULE_CTL_CHANNEL1_COLOR_SEQ_SRGGB     (0x3<<24)

#define MODULE_CTL_CHANNEL1_RAW_CLOLR_SEQ_MASK  (0x3<<28)
#define MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SBGGR (0x0<<28)
#define MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SGRBG (0x1<<28)
#define MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SGBRG (0x2<<28)
#define MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SRGGB (0x3<<28)

#define MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SBGGR (0x0<<28)
#define MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SGRBG (0x1<<28)
#define MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SGBRG (0x2<<28)
#define MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SRGGB (0x3<<28)

#define MODULE_CTL_CHANNEL2_MODE_MASK           (0x3 << 19)
#define MODULE_CTL_CHANNEL2_MODE_RGB8           (0x2 << 19)
#define MODULE_CTL_CHANNEL2_MODE_YUYV           (0x1 << 19)
#define MODULE_CTL_CHANNEL2_MODE_RGB10          (0x0 << 19)
#define MODULE_CTL_CHANNEL2_MODE_RGB12          (0x0 << 19)

#define MODULE_CTL_CHANNEL2_HSYNC_ACTIVE_HIGH   (0x1 << 18)
#define MODULE_CTL_CHANNEL2_VSYNC_ACTIVE_HIGH   (0x1 << 17)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_MASK      (0x3<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_UYVY      (0x0<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_VYUY      (0x1<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_YUYV      (0x2<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_YVYU      (0x3<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_SBGGR     (0x0<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_SGRBG     (0x1<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_SGBRG     (0x2<<26)
#define MODULE_CTL_CHANNEL2_COLOR_SEQ_SRGGB     (0x3<<26)

#define CSI_CTRL_EN                             (0x1 << 0)
#define CSI_CTRL_D_PHY_EN                       (0x1 << 2)
#define CSI_CTRL_PHY_INIT                       (0x1 << 3)
#define CSI_CTRL_LANE_NUM(x)                    (((x) & 0x3) << 4)
#define CSI_CTRL_ECE                            (0x1 << 6)
#define CSI_CTRL_CCE                            (0x1 << 7)
#define CSI_CTRL_CLK_LANE_HS                    (0x1 << 8)
#define CSI_CTRL_PHY_INIT_SEL                   (0x1 << 9)

#define MIPI_PHY_1LANE                          0x3
#define MIPI_PHY_2LANE                          0x7
#define MIPI_PHY_3LANE                          0xf
#define MIPI_PHY_4LANE                          0x1f

#define CSI_CONTEXT_EN                          (0x1 << 0)
#define CSI_CONTEXT_DT(x)                       (((x) & 0x3f) << 1)

#define CONTEXT_DT_RAW8                         (0x2A)
#define CONTEXT_DT_RAW10                        (0x2B)
#define CONTEXT_DT_RAW12                        (0x2C)

static inline int module_camera_clock_init(struct device *dev,
					   struct camera_dev *cdev);
static inline int module_camera_clock_enable(struct camera_dev *cdev,
					     struct camera_param *cam_param,
					     struct mipi_setting *mipi_cfg);
static inline void module_camera_clock_disable(struct camera_dev *cdev);
static inline void module_mipi_csi_init(struct camera_param *cam_param,
					struct mipi_setting *mipi_cfg);
static inline void module_mipi_csi_disable(struct camera_dev *cdev,
					   struct soc_camera_device *icd);

static inline int set_col_range(struct soc_camera_device *icd);
static inline void module_set_output_fmt(struct soc_camera_device *icd,
					 u32 fourcc);
static int ext_cmd(struct v4l2_subdev *sd, int cmd, void *args);
static int raw_store_set(struct soc_camera_device *icd);

static inline void module_set_frame_yuv420(phys_addr_t module_addr,
					   struct soc_camera_device *icd);
static inline void module_set_frame_yvu420(phys_addr_t module_addr,
					   struct soc_camera_device *icd);
static inline void module_set_frame_yvu422(phys_addr_t module_addr,
					   struct soc_camera_device *icd);
static inline void module_set_frame_nv12_nv21(phys_addr_t module_addr,
					      struct soc_camera_device *icd);
static inline void module_set_frame_yuyv(phys_addr_t module_addr,
					 struct soc_camera_device *icd);

static inline int module_isr(struct soc_camera_device *icd,
		struct camera_param *cam_param, struct v4l2_subdev *sd,
		unsigned int module_int_stat, int i);

static inline void module_camera_suspend(struct device *dev);
static inline void module_camera_resume(struct device *dev,
					struct camera_dev *cam_dev);

static int updata_module_info(struct v4l2_subdev *sd,
			      struct camera_param *cam_param);

static int host_module_init(void);
static void host_module_exit(void);

static void module_regs_init(void);

#endif
