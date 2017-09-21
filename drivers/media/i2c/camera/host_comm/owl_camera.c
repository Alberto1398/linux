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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf-dma-contig.h>
#include <linux/slab.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <linux/v4l2-mediabus.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/reset.h>

#include <linux/pinctrl/consumer.h>

#include "owl_camera.h"

/* for bisp's tow soc camera host*/
static struct sensor_pwd_info *g_spinfo[2] = { NULL, NULL };

static inline struct sensor_pwd_info *to_spinfo(int host_id)
{
	return g_spinfo[!(!host_id)];
}

/* should be called before register host using
 * soc_camera_host_register() */
static void attach_sensor_pwd_info(struct device *dev,
				   struct sensor_pwd_info *pi, int host_id)
{
	int id = !(!host_id);

	if (g_spinfo[id])
		dev_err(dev, "already register it [host id : %d]\n", host_id);
	g_spinfo[id] = pi;
}

static void detach_sensor_pwd_info(struct device *dev,
				   struct sensor_pwd_info *pi, int host_id)
{
	int id = !(!host_id);

	if (pi != g_spinfo[id]) {
		dev_err(dev, "sensor pwd info don't match with host id[%d]\n",
			host_id);
	}
	g_spinfo[id] = NULL;
}

static int module_gpio_init(struct device_node *fdt_node,
			    struct sensor_pwd_info *spinfo);

static void *GMODULEMAPADDR;
static void *GCSI1MAPADDR;
static void *GCMUMAPADDR;
#ifdef MODULE_ISP
static void *GCSI2MAPADDR;
#endif
#ifdef MODULE_SI
static void *GMFPADDR;
static void *GPADADDR;
static void *GGPIOCADDR;
#endif

static unsigned int reg_read(void *map_addr, unsigned int reg,
			     unsigned long base_addr)
{
	unsigned char *pregs = (unsigned char *)map_addr + reg - base_addr;
	unsigned int value = 0;

	value = readl_relaxed(pregs);

	return value;
}

static void reg_write(unsigned int value, void *map_addr, unsigned int reg,
		      unsigned long base_addr)
{
	unsigned char *pregs = (unsigned char *)map_addr + reg - base_addr;

	writel_relaxed(value, pregs);
}

static int camera_clock_init(struct device *dev, struct camera_dev *cdev)
{
	struct sensor_pwd_info *spinfo = &cdev->spinfo;

	cdev->module_clk_parent = devm_clk_get(dev, MODULE_CLK_PARENT);
	if (IS_ERR(cdev->module_clk_parent)) {
		module_err("error: module_clk_parent can't get clocks!!!\n");
		return -EINVAL;
	}

	cdev->module_clk = devm_clk_get(dev, MODULE_CLK);
	if (IS_ERR(cdev->module_clk)) {
		module_err("error: module_clk can't get clocks!!!\n");
		return -EINVAL;
	}

	cdev->sensor_clk_parent[HOST_MODULE_CHANNEL_0] = devm_clk_get(dev,
					SENSOR_CLK_PARENT_0);
	if (IS_ERR(cdev->sensor_clk_parent[HOST_MODULE_CHANNEL_0])) {
		module_err("error: sensor_clk_parent0 can't get clocks!!!\n");
		return -EINVAL;
	}

	cdev->csi_clk_parent = devm_clk_get(dev, CSI_CLK_PARENT);
	if (IS_ERR(cdev->csi_clk_parent)) {
		module_err("error: csi_clk_parent can't get clocks!!!\n");
		return -EINVAL;
	}

	cdev->ch_clk[HOST_MODULE_CHANNEL_0] = devm_clk_get(dev, CH_CLK_0);
	if (IS_ERR(cdev->ch_clk[HOST_MODULE_CHANNEL_0])) {
		module_err("error: ch0 can't get clocks!!!\n");
		return -EINVAL;
	}

	cdev->ch_clk[HOST_MODULE_CHANNEL_1] = devm_clk_get(dev, CH_CLK_1);
	if (IS_ERR(cdev->ch_clk[HOST_MODULE_CHANNEL_1])) {
		module_err("error: ch1 can't get clocks!!!\n");
		return -EINVAL;
	}

	/*setting module clock parent */
	clk_set_parent(cdev->module_clk, cdev->module_clk_parent);

	module_camera_clock_init(dev, cdev);

	spinfo->ch_clk[HOST_MODULE_CHANNEL_0] =
	    cdev->ch_clk[HOST_MODULE_CHANNEL_0];
	spinfo->ch_clk[HOST_MODULE_CHANNEL_1] =
	    cdev->ch_clk[HOST_MODULE_CHANNEL_1];

	return 0;
}

static void camera_clock_enable(struct camera_dev *cdev,
				struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	struct mipi_setting *mipi_cfg = module_info->mipi_cfg;

	/*setting module clock rate */
	clk_set_rate(cdev->module_clk, MODULE_WORK_CLOCK);

	module_camera_clock_enable(cdev, cam_param, mipi_cfg);

	/* Enable MODULE CLOCK */
	clk_prepare(cdev->module_clk);
	clk_enable(cdev->module_clk);
}

static void camera_clock_disable(struct camera_dev *cdev,
				 struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;

	/* Disable CSI CLOCK */
	module_camera_clock_disable(cdev);

	/* Disable SENSOR CLOCK */
	clk_put(cdev->ch_clk[cam_param->channel]);

	/* Disable SI CLOCK */
	clk_put(cdev->module_clk);
}


static inline void module_mipi_parse_params(int channel,
                    struct device *pdev,
					struct mipi_setting *mipi_cfg)
{
    char propname[20];

	struct device_node *dn = pdev->of_node;
	struct device_node *entry;

	/* panel_configs */
	if (HOST_MODULE_CHANNEL_0 == channel) {
	    sprintf(propname, "rear_mipi_configs");
    } else {
	    sprintf(propname, "front_mipi_configs");
    }
   
    entry = of_parse_phandle(dn, propname, 0);
    if (entry) {
        if (of_property_read_u32(entry, "clk_settle_time",
        	&mipi_cfg->clk_settle_time))
	            module_info("can't find clk_settle_time parameter!\n");
        if (of_property_read_u32(entry, "clk_term_time",
        	&mipi_cfg->clk_term_time))
	            module_info("can't find clk_term_time parameter!\n");
        if (of_property_read_u32(entry, "data_settle_time",
        	&mipi_cfg->data_settle_time))
	            module_info("can't find data_settle_time parameter!\n");
        if (of_property_read_u32(entry, "data_term_time",
        	&mipi_cfg->data_term_time))
	            module_info("can't find data_term_time parameter!\n");
        if (of_property_read_u32(entry, "lane0_map",
        	&mipi_cfg->lane0_map))
	            module_info("can't find lane0_map parameter!\n");
        if (of_property_read_u32(entry, "lane1_map",
        	&mipi_cfg->lane1_map))
	            module_info("can't find lane1_map parameter!\n");
        if (of_property_read_u32(entry, "lane2_map",
        	&mipi_cfg->lane2_map))
	            module_info("can't find lane2_map parameter!\n");
        if (of_property_read_u32(entry, "lane3_map",
        	&mipi_cfg->lane3_map))
	            module_info("can't find lane3_map parameter!\n");
    }
}

static int mipi_csi_init(struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	struct mipi_setting *mipi_cfg = module_info->mipi_cfg;

	module_mipi_parse_params(cam_param->channel, pdev, mipi_cfg);

	module_mipi_csi_init(cam_param, mipi_cfg);

	return 0;
}

static inline void mipi_csi_disable(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct camera_dev *cdev = ici->priv;

	module_mipi_csi_disable(cdev, icd);

	return;
}

static inline void set_preline(struct soc_camera_device *icd)
{
	struct camera_param *param = icd->host_priv;
	unsigned int state;

	if (HOST_MODULE_CHANNEL_0 == param->channel) {
		state = reg_read(GMODULEMAPADDR, CH_PRELINE_SET, MODULE_BASE);
		state &= ~CH1_PRELINE_NUM_MASK;
		state |= CH1_PRELINE_NUM(PRELINE_NUM);
	} else {
		state = reg_read(GMODULEMAPADDR, CH_PRELINE_SET, MODULE_BASE);
		state &= ~CH2_PRELINE_NUM_MASK;
		state |= CH2_PRELINE_NUM(PRELINE_NUM);
	}
	reg_write(state, GMODULEMAPADDR, CH_PRELINE_SET, MODULE_BASE);

#ifdef MODULE_ISP
	reg_write(state, GMODULEMAPADDR, RAW_RB_PRELINE_SET, MODULE_BASE);
#endif
}

static inline int set_row_range(struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;

	unsigned int start = cam_param->top;

#ifdef MODULE_SI
	unsigned int end = cam_param->top + icd->user_height - 1;
#elif defined MODULE_ISP
	unsigned int end = cam_param->top + cam_param->real_h - 1;
#endif

	int height = end - start + 1;

	if ((height > MODULE_MAX_HEIGHT) || (height < 1) || (start >= 4096)
	    || (end >= 4096)) {
		return -EINVAL;
	}

	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		reg_write(CH1_ROW_START(0) | CH1_ROW_END(end - start),
			  GMODULEMAPADDR, CH1_ROW_RANGE, MODULE_BASE);
	} else {
		reg_write(CH2_ROW_START(0) | CH2_ROW_END(end - start),
			  GMODULEMAPADDR, CH2_ROW_RANGE, MODULE_BASE);
	}

	return 0;
}

/* rect is guaranteed to not exceed the camera rectangle */
static inline void set_rect(struct soc_camera_device *icd)
{
	set_col_range(icd);
	set_row_range(icd);
}

static int set_input_fmt(struct soc_camera_device *icd,
			 enum v4l2_mbus_pixelcode code)
{
	struct camera_param *cam_param = icd->host_priv;
	unsigned int module_ctl = reg_read(GMODULEMAPADDR, MODULE_ENABLE,
					   MODULE_BASE);

	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		module_ctl &= ~(MODULE_CTL_CHANNEL1_MODE_MASK |
				MODULE_CTL_CHANNEL1_COLOR_SEQ_MASK |
				MODULE_CTL_CHANNEL1_RAW_CLOLR_SEQ_MASK);
		switch (code) {
		case V4L2_MBUS_FMT_UYVY8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL1_MODE_YUYV |
				       MODULE_CTL_CHANNEL1_COLOR_SEQ_UYVY);
			break;
		case V4L2_MBUS_FMT_VYUY8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL1_MODE_YUYV |
				       MODULE_CTL_CHANNEL1_COLOR_SEQ_VYUY);
			break;
		case V4L2_MBUS_FMT_YUYV8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL1_MODE_YUYV |
				       MODULE_CTL_CHANNEL1_COLOR_SEQ_YUYV);
			break;
		case V4L2_MBUS_FMT_YVYU8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL1_MODE_YUYV |
				       MODULE_CTL_CHANNEL1_COLOR_SEQ_YVYU);
			break;
		case V4L2_MBUS_FMT_SGRBG8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB8
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SGRBG
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SGRBG;
			break;
		case V4L2_MBUS_FMT_SRGGB8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB8
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SRGGB
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SRGGB;
			break;
		case V4L2_MBUS_FMT_SGBRG8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB8
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SGBRG
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SGBRG;
			break;
		case V4L2_MBUS_FMT_SBGGR8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB8
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SBGGR
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SBGGR;
			break;
		case V4L2_MBUS_FMT_SGRBG10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB10
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SGRBG
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SGRBG;
			break;
		case V4L2_MBUS_FMT_SRGGB10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB10
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SRGGB
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SRGGB;
			break;
		case V4L2_MBUS_FMT_SGBRG10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB10
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SGBRG
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SGBRG;
			break;
		case V4L2_MBUS_FMT_SBGGR10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL1_MODE_RGB10
			    | MODULE_CTL_CHANNEL1_COLOR_SEQ_SBGGR
			    | MODULE_CTL_CHANNEL1_RAW_COLOR_SEQ_SBGGR;
			break;
		default:
			module_err("input data error (pixel code:0x%x)\n",
				   code);
			break;
		}
		reg_write(module_ctl, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
	} else if (HOST_MODULE_CHANNEL_1 == cam_param->channel) {
		module_ctl &= ~(MODULE_CTL_CHANNEL2_MODE_MASK |
				MODULE_CTL_CHANNEL2_COLOR_SEQ_MASK |
				MODULE_CTL_CHANNEL1_RAW_CLOLR_SEQ_MASK);
		switch (code) {
		case V4L2_MBUS_FMT_UYVY8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL2_MODE_YUYV |
				       MODULE_CTL_CHANNEL2_COLOR_SEQ_UYVY);
			break;
		case V4L2_MBUS_FMT_VYUY8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL2_MODE_YUYV |
				       MODULE_CTL_CHANNEL2_COLOR_SEQ_VYUY);
			break;
		case V4L2_MBUS_FMT_YUYV8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL2_MODE_YUYV |
				       MODULE_CTL_CHANNEL2_COLOR_SEQ_YUYV);
			break;
		case V4L2_MBUS_FMT_YVYU8_2X8:	/* UYVY */
			module_ctl |= (MODULE_CTL_CHANNEL2_MODE_YUYV |
				       MODULE_CTL_CHANNEL2_COLOR_SEQ_YVYU);
			break;
		case V4L2_MBUS_FMT_SGRBG8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB8
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SGRBG
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SGRBG;
			break;
		case V4L2_MBUS_FMT_SRGGB8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB8
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SRGGB
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SRGGB;
			break;
		case V4L2_MBUS_FMT_SGBRG8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB8
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SGBRG
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SGBRG;
			break;
		case V4L2_MBUS_FMT_SBGGR8_1X8:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB8
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SBGGR
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SBGGR;
			break;
		case V4L2_MBUS_FMT_SGRBG10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB10
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SGRBG
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SGRBG;
			break;
		case V4L2_MBUS_FMT_SRGGB10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB10
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SRGGB
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SRGGB;
			break;
		case V4L2_MBUS_FMT_SGBRG10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB10
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SGBRG
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SGBRG;
			break;
		case V4L2_MBUS_FMT_SBGGR10_1X10:
			module_ctl |= MODULE_CTL_CHANNEL2_MODE_RGB10
			    | MODULE_CTL_CHANNEL2_COLOR_SEQ_SBGGR
			    | MODULE_CTL_CHANNEL2_RAW_COLOR_SEQ_SBGGR;
			break;
		default:
			module_err("input data error (pixel code:0x%x)\n",
				   code);
			break;
		}
		reg_write(module_ctl, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
	}

	return 0;
}

static inline void get_fmt(u32 fourcc, unsigned int *fmt)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_YUV420:	/*420 planar */
		*fmt |= MODULE_OUT_FMT_YUV420;
		break;
	case V4L2_PIX_FMT_YVU420:	/*420 planar YV12 */
		*fmt |= MODULE_OUT_FMT_YUV420;
		break;
	case V4L2_PIX_FMT_YUV422P:	/*422 semi planar */
		*fmt |= MODULE_OUT_FMT_YUV422P;
		break;
	case V4L2_PIX_FMT_NV12:	/*420 semi-planar */
		*fmt |= MODULE_OUT_FMT_NV12;
		break;
	case V4L2_PIX_FMT_NV21:	/*420 semi-planar */
		*fmt |= MODULE_OUT_FMT_NV12 | MODULE_OUT_FMT_SEMI_UV_INV;
		break;
	case V4L2_PIX_FMT_YUYV:	/*interleaved */
		*fmt |= MODULE_OUT_FMT_YUYV;
		break;
	default:		/* Raw RGB */
		break;
	}
}

static int set_output_fmt(struct soc_camera_device *icd, u32 fourcc)
{
	module_set_output_fmt(icd, fourcc);

	return 0;
}

/*seperate the pys_addr_t*/
static void set_frame(struct soc_camera_device *icd, struct videobuf_buffer *vb)
{
	u32 fourcc = icd->current_fmt->host_fmt->fourcc;
	phys_addr_t module_addr;

	if (NULL == vb) {
		module_err("cannot get video buffer.\n");
		return;
	}

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
		module_addr = videobuf_to_dma_contig(vb);
		break;
	case V4L2_MEMORY_USERPTR:
		module_addr = vb->baddr;
		break;
	default:
		module_err("set_frame wrong memory type. %d,%p\n", vb->memory,
			   (void *)vb->baddr);
		return;
	}

	switch (fourcc) {
	case V4L2_PIX_FMT_YUV420:
		module_set_frame_yuv420(module_addr, icd);
		break;

	case V4L2_PIX_FMT_YVU420:	/*420 planar YV12 */
		module_set_frame_yvu420(module_addr, icd);
		break;

	case V4L2_PIX_FMT_YUV422P:	/*422 semi planar */
		module_set_frame_yvu422(module_addr, icd);
		break;

	case V4L2_PIX_FMT_NV12:	/*420 semi-planar */
	case V4L2_PIX_FMT_NV21:
		module_set_frame_nv12_nv21(module_addr, icd);
		break;

	case V4L2_PIX_FMT_YUYV:
		module_set_frame_yuyv(module_addr, icd);
		break;

	default:		/* Raw RGB */
		module_err("Set isp output format failed, fourcc = 0x%x\n",
			   fourcc);
		return;
	}
}

static int set_signal_polarity(struct soc_camera_device *icd,
			       unsigned int common_flags)
{
	struct camera_param *cam_param = icd->host_priv;
	u32 module_ch1_ctl, module_ch2_ctl;

	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		module_ch1_ctl = reg_read(GMODULEMAPADDR, MODULE_ENABLE,
					  MODULE_BASE);
		/* edge clk trigger */
		module_ch1_ctl &= (~(1 << 9));

		if (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW) {
			module_ch1_ctl &=
			    ~MODULE_CTL_CHANNEL1_HSYNC_ACTIVE_HIGH;
		} else {
			module_ch1_ctl |= MODULE_CTL_CHANNEL1_HSYNC_ACTIVE_HIGH;
		}

		if (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW) {
			module_ch1_ctl &=
			    ~MODULE_CTL_CHANNEL1_VSYNC_ACTIVE_HIGH;
		} else {
			module_ch1_ctl |= MODULE_CTL_CHANNEL1_VSYNC_ACTIVE_HIGH;
		}
		reg_write(module_ch1_ctl, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
	} else {
		module_ch2_ctl = reg_read(GMODULEMAPADDR, MODULE_ENABLE,
					  MODULE_BASE);
		module_ch1_ctl &= (~(1 << 16));

		if (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW) {
			module_ch2_ctl &=
			    ~MODULE_CTL_CHANNEL2_HSYNC_ACTIVE_HIGH;
		} else {
			module_ch2_ctl |= MODULE_CTL_CHANNEL2_HSYNC_ACTIVE_HIGH;
		}
		if (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW) {
			module_ch2_ctl &=
			    ~MODULE_CTL_CHANNEL2_VSYNC_ACTIVE_HIGH;
		} else {
			module_ch2_ctl |= MODULE_CTL_CHANNEL2_VSYNC_ACTIVE_HIGH;
		}
		reg_write(module_ch2_ctl, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
	}

	return 0;
}

static void capture_start(struct camera_dev *cam_dev,
			  struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	struct host_module_setting_t *module_setting = module_info->module_cfg;

	unsigned int module_enable, module_int_stat, module_ctl;
	unsigned int ch1_intf = 0;
#ifdef MODULE_ISP
	unsigned int module_mode = 0;
#endif

	if (cam_param->started == DEV_START) {
		module_info("already start\n");
		return;
	}
	if (V4L2_MBUS_CSI2 == cam_param->bus_type)
		mipi_csi_init(icd);
#ifdef MODULE_SI
	reg_write(reg_read(GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE) &
		  ((~0x1 << 19)), GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);
#endif
	set_preline(icd);

	module_ctl = reg_read(GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);
#ifdef MODULE_ISP
	if (cam_param->b_splited_capture == 1)
		module_ctl = module_ctl | (module_mode);

	module_ctl &= (~(3 << 1));
#endif

	if (V4L2_MBUS_PARALLEL == cam_param->bus_type) {
		if (cam_param->channel == HOST_MODULE_CHANNEL_0)
			ch1_intf = MODULE_CTL_CHANNEL1_INTF_PARAL;
		else
			ch1_intf = MODULE_CTL_CHANNEL2_INTF_PARAL;
		module_ctl &= ~ch1_intf;
	} else {
		if (cam_param->channel == HOST_MODULE_CHANNEL_0) {
			ch1_intf = MODULE_CTL_CHANNEL1_INTF_MIPI;
			module_ctl &= ~MODULE_CTL_CHANNEL1_INTF_MIPI;
		} else {
			ch1_intf = MODULE_CTL_CHANNEL2_INTF_MIPI;
			module_ctl &= ~MODULE_CTL_CHANNEL2_INTF_MIPI;
		}
	}
	module_ctl = module_ctl | (ch1_intf);
	reg_write(module_ctl, GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);

	module_enable = reg_read(GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);
	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		module_int_stat = reg_read(GMODULEMAPADDR, MODULE_STAT,
					   MODULE_BASE);
		module_int_stat |= MODULE_INT_STAT_CH1_PL_INT_EN;
#ifdef MODULE_SI
		reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
			  MODULE_BASE);
		/* clk_edge: */
		module_enable = (module_enable & (~(1 << 9))) |
		    (module_setting->clk_edge << 9);
		/* vsync: */
		module_enable = (module_enable & (~(1 << 10))) |
		    (module_setting->vs_pol << 10);
		/* hsync: */
		module_enable = (module_enable & (~(1 << 11))) |
		    (module_setting->hs_pol << 11);
		reg_write(module_enable, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
#elif defined MODULE_ISP
		if (cam_param->b_splited_capture == 1) {
			module_int_stat |= MODULE_INT_STAT_RAW_RB_PL_INT_EN;
			module_int_stat |=
			    MODULE_INT_STAT_RAWSTORE_FRAME_END_EN;
		}

		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW) {
			module_int_stat |= MODULE_INT_STAT_REFORM_FRAME_END_EN;
			module_enable |= (1 << 5);
		} else {
			if (cam_param->b_splited_capture == 0)
				module_enable |= 1;
		}

		if ((cam_param->b_raw_store_status == 0x1)
		    || (cam_param->b_raw_store_status == 0x2)) {
			module_enable |= (1 << 4);
		}

		module_int_stat |= MODULE_INT_STAT_CH1_FRAME_END_INT_EN;
		reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
			  MODULE_BASE);

		/* clk_edge: */
		module_enable = (module_enable & (~(1 << 9)))
		    | (module_setting->clk_edge << 9);
		/* vsync: */
		module_enable = (module_enable & (~(1 << 10)))
		    | (module_setting->vs_pol << 10);
		/* hsync: */
		module_enable = (module_enable & (~(1 << 11)))
		    | (module_setting->hs_pol << 11);
		/*color seq */

		/*
		   channel mux setting:
		   0: ch1 isp, ch2 yuv reform
		   1:ch1 yuv reform, ch2 isp
		 */
		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW)
			module_enable = module_enable | (1 << 6);
		else
			module_enable = module_enable & ~(1 << 6);

		reg_write(module_enable, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
#endif
	} else {
		module_int_stat = reg_read(GMODULEMAPADDR, MODULE_STAT,
					   MODULE_BASE);
		module_int_stat |= MODULE_INT_STAT_CH2_PL_INT_EN;
#ifdef MODULE_SI
		reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
			  MODULE_BASE);
		/* clk_edge: */
		module_enable = (module_enable & (~(1 << 16))) |
		    (module_setting->clk_edge << 16);
		/* vsync: */
		module_enable = (module_enable & (~(1 << 17))) |
		    (module_setting->vs_pol << 17);
		/* hsync: */
		module_enable = (module_enable & (~(1 << 18))) |
		    (module_setting->hs_pol << 18);

		reg_write(module_enable, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
#elif defined MODULE_ISP
		if (cam_param->b_splited_capture == 1) {
			module_int_stat &= (~1);
			module_int_stat |= MODULE_INT_STAT_RAW_RB_PL_INT_EN;
			module_int_stat |=
			    MODULE_INT_STAT_RAWSTORE_FRAME_END_EN;
		}

		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW) {
			module_int_stat |= MODULE_INT_STAT_REFORM_FRAME_END_EN;
			module_enable |= (1 << 5);
		} else {
			module_enable |= 1;
		}

		if ((cam_param->b_raw_store_status == 0x1)
		    || (cam_param->b_raw_store_status == 0x2)) {
			module_enable |= (1 << 4);
		}

		module_int_stat |= MODULE_INT_STAT_CH1_FRAME_END_INT_EN;
		reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
			  MODULE_BASE);

		/* clk_edge: */
		module_enable = (module_enable & (~(1 << 16)))
		    | (module_setting->clk_edge << 16);
		/* vsync: */
		module_enable = (module_enable & (~(1 << 17)))
		    | (module_setting->vs_pol << 17);
		/* hsync: */
		module_enable = (module_enable & (~(1 << 18)))
		    | (module_setting->hs_pol << 18);
		/*color seq */

		/*
		   channel mux setting:
		   0: ch1 isp, ch2 yuv reform
		   1:ch1 yuv reform, ch2 isp
		 */
		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW)
			module_enable = module_enable & ~(1 << 6);
		else
			module_enable = module_enable | (1 << 6);

		reg_write(module_enable, GMODULEMAPADDR, MODULE_ENABLE,
			  MODULE_BASE);
#endif
	}

	reg_write(reg_read(GMODULEMAPADDR, MODULE_STAT, MODULE_BASE),
		  GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	cam_param->started = DEV_START;

#ifdef MODULE_SI
	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		reg_write(reg_read(GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE) |
			  0x53, GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);
	} else {
		reg_write(reg_read(GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE) |
			  0x95, GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);
	}
#elif defined MODULE_ISP
	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		reg_write(0, GMODULEMAPADDR, VTD_CTL, MODULE_BASE);
		reg_write(1, GMODULEMAPADDR, VTD_CTL, MODULE_BASE);
	} else {
		reg_write(3, GMODULEMAPADDR, VTD_CTL, MODULE_BASE);
	}
#endif

	return;
}

static void clear_isr(struct camera_param *cam_param)
{
	unsigned int int_stat;
#ifdef MODULE_ISP
	unsigned int m_isr_init = 0;
#endif
	int_stat = reg_read(GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);

#ifdef MODULE_SI
	if (HOST_MODULE_CHANNEL_0 == cam_param->channel)
		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	else
		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
#elif defined MODULE_ISP
	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		m_isr_init |= (1 << 30) | (3 << 26) | (1 << 24) | (1 << 21);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= (1 << 29)
			    | (1 << 23)
			    | (7 << 18)
			    | (1 << 16)
			    | (1 << 12);
		} else {
			m_isr_init |= (1 << 17) | (1 << 13);
		}
		int_stat |= (m_isr_init);
		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	} else {
		m_isr_init |= (1 << 31) | (3 << 27) | (1 << 25) | (1 << 22);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= (1 << 29)
			    | (1 << 23)
			    | (7 << 18)
			    | (1 << 16)
			    | (1 << 12);
		} else {
			m_isr_init |= (1 << 17) | (1 << 13);
		}
		int_stat |= (m_isr_init);
		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	}
#endif
}

static void capture_stop(struct camera_dev *cam_dev,
			 struct soc_camera_device *icd)
{
	unsigned int module_enable;
	struct camera_param *cam_param = icd->host_priv;
	unsigned long flags = 0;
	unsigned int int_stat = 0;
	unsigned int m_isr_init = 0;
#ifdef MODULE_ISP
	unsigned int count = 0;
#endif

	if (cam_param->started == DEV_STOP) {
		clear_isr(cam_param);
		return;
	}
#ifdef MODULE_ISP
	while (cam_param->rb_cnt & 0x1) {
		mdelay(5);
		count++;
		if (count > 10)
			break;
	}
#endif

	spin_lock_irqsave(&cam_dev->lock, flags);
	module_enable = reg_read(GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);

#ifdef MODULE_SI
	reg_write(module_enable & (~MODULE_ENABLE_EN), GMODULEMAPADDR,
		  MODULE_ENABLE, MODULE_BASE);
#elif defined MODULE_ISP
	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		reg_write(module_enable & (~(MODULE_ENABLE_EN | (1 << 4))),
			  GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);
	} else {
		reg_write(module_enable & (~(1 << 5)), GMODULEMAPADDR,
			  MODULE_ENABLE, MODULE_BASE);
	}
#endif

	/* enable frame end interrupt to check that frame tx already finish */
	int_stat = reg_read(GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);

#ifdef MODULE_SI
	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		m_isr_init |= MODULE_INT_STAT_CH1_PL_INT_EN;
		int_stat &= (~m_isr_init);
		int_stat |= MODULE_INT_STAT_CH1_FRAME_END_INT_EN;
		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	} else {
		m_isr_init |= MODULE_INT_STAT_CH2_PL_INT_EN;
		int_stat &= (~MODULE_INT_STAT_CH2_PL_INT_EN);
		int_stat |= MODULE_INT_STAT_CH2_FRAME_END_INT_EN;
		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	}
#elif defined MODULE_ISP
	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		m_isr_init |= MODULE_INT_STAT_CH1_PL_INT_EN;
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= MODULE_INT_STAT_CH1_FRAME_END_INT_EN
			    | MODULE_INT_STAT_RAWSTORE_FRAME_END_EN;
		} else {
			m_isr_init |= MODULE_INT_STAT_REFORM_FRAME_END_EN;
		}

		int_stat &= (~m_isr_init);

		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	} else {
		m_isr_init |= (MODULE_INT_STAT_CH2_PL_INT_EN);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= MODULE_INT_STAT_CH1_FRAME_END_INT_EN
			    | MODULE_INT_STAT_RAWSTORE_FRAME_END_EN;
		} else {
			m_isr_init |= MODULE_INT_STAT_REFORM_FRAME_END_EN;
		}
		int_stat &= (~m_isr_init);
		reg_write(int_stat, GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	}
#endif

	cam_param->started = DEV_STOP;
	spin_unlock_irqrestore(&cam_dev->lock, flags);

#ifdef MODULE_ISP
	mdelay(50);
#endif

	if (V4L2_MBUS_CSI2 == cam_param->bus_type)
		mipi_csi_disable(icd);

	cam_param->cur_frm = NULL;
	cam_param->prev_frm = NULL;

	return;
}

static inline int get_bytes_per_line(struct soc_camera_device *icd)
{
	return soc_mbus_bytes_per_line(icd->user_width,
					icd->current_fmt->host_fmt);
}

static inline int get_frame_size(struct soc_camera_device *icd)
{
	int bytes_per_line = get_bytes_per_line(icd);

	return icd->user_height * bytes_per_line;
}

static irqreturn_t camera_host_isr(int irq, void *data)
{
	struct camera_dev *cam_dev = data;
	struct soc_camera_device *icd = NULL;
	struct camera_param *cam_param = NULL;
	struct v4l2_subdev *sd = NULL;
	unsigned int module_int_stat = 0;
	unsigned long flags;
	unsigned int preline_int_pd;

	int i;
	int ret;

	spin_lock_irqsave(&cam_dev->lock, flags);

	for (i = 0; i < 2; i++) {
		if (cam_dev->icds[i]) {
			icd = cam_dev->icds[i];
			cam_param = icd->host_priv;
			sd = soc_camera_to_subdev(icd);
			module_int_stat = reg_read(GMODULEMAPADDR, MODULE_STAT,
						   MODULE_BASE);

			if (HOST_MODULE_CHANNEL_0 == cam_param->channel)
				preline_int_pd = MODULE_INT_STAT_CH1_PL_PD;
			else
				preline_int_pd = MODULE_INT_STAT_CH2_PL_PD;
			if ((cam_param->skip_frames) && (module_int_stat &
							 preline_int_pd)) {
				cam_param->skip_frames--;
				reg_write(module_int_stat, GMODULEMAPADDR,
					  MODULE_STAT, MODULE_BASE);
				goto out;
			}
			ret = module_isr(icd, cam_param, sd,
							module_int_stat, i);
			if (ret == -1)
				goto out;
		}
	}

out:
	spin_unlock_irqrestore(&cam_dev->lock, flags);
	return IRQ_HANDLED;
}

static int get_supported_mbus_param(struct camera_param *cam_param)
{
	int flags = 0;

	/*
	 * S700  camera interface only works in master mode
	 * and only support 8bits currently
	 */
	if (V4L2_MBUS_PARALLEL == cam_param->bus_type) {
		flags = V4L2_MBUS_MASTER |
		    V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		    V4L2_MBUS_HSYNC_ACTIVE_LOW |
		    V4L2_MBUS_VSYNC_ACTIVE_HIGH |
		    V4L2_MBUS_VSYNC_ACTIVE_LOW |
		    V4L2_MBUS_PCLK_SAMPLE_RISING |
		    V4L2_MBUS_PCLK_SAMPLE_FALLING |
		    V4L2_MBUS_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8;
	} else if (V4L2_MBUS_CSI2 == cam_param->bus_type) {
		flags = V4L2_MBUS_CSI2_CHANNEL_0 |
		    V4L2_MBUS_CSI2_CHANNEL_1 |
		    V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		    V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK | V4L2_MBUS_CSI2_LANES;
	} else {
		module_err("bus_type is not supported\n");
	}

	return flags;
}

static int select_dvp_mbus_param(struct camera_dev *cdev,
				 unsigned int common_flags)
{
	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
	    && (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (cdev->dvp_mbus_flags & CAMERA_HSYNC_HIGH)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
	    && (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (cdev->dvp_mbus_flags & CAMERA_VSYNC_HIGH)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_DATA_ACTIVE_HIGH)
	    && (common_flags & V4L2_MBUS_DATA_ACTIVE_LOW)) {
		if (cdev->dvp_mbus_flags & CAMERA_DATA_HIGH)
			common_flags &= ~V4L2_MBUS_DATA_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_DATA_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
	    && (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)) {
		if (cdev->dvp_mbus_flags & CAMERA_PCLK_RISING)
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_FALLING;
		else
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_RISING;
	}

	return common_flags;
}

static int select_mipi_mbus_param(struct camera_param *param,
				  unsigned int common_flags)
{
	if (common_flags & V4L2_MBUS_CSI2_1_LANE) {
		/* correspond to cis  CSI_CTRL_LANE_NUM's defined */
		param->lane_num = 0;
	} else if (common_flags & V4L2_MBUS_CSI2_2_LANE) {
		param->lane_num = 1;
	} else if (common_flags & V4L2_MBUS_CSI2_3_LANE) {
		param->lane_num = 2;
	} else {
		param->lane_num = 3;
	}

	return V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_HIGH;
}

static int camera_set_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct camera_dev *cam_dev = ici->priv;
	struct camera_param *cam_param = icd->host_priv;
	struct v4l2_mbus_config cfg;

	unsigned int bus_flags;
	unsigned int common_flags;
	int ret;

	bus_flags = get_supported_mbus_param(cam_param);

	v4l2_subdev_call(sd, video, g_mbus_config, &cfg);

	common_flags = soc_mbus_config_compatible(&cfg, bus_flags);
	if (!common_flags)
		return -EINVAL;

	if (V4L2_MBUS_PARALLEL == cam_param->bus_type)
		common_flags = select_dvp_mbus_param(cam_dev, common_flags);
	else
		common_flags = select_mipi_mbus_param(cam_param, common_flags);

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		module_err("camera s_mbus_config(0x%x) returned %d\n",
			   common_flags, ret);
		return ret;
	}

	set_signal_polarity(icd, common_flags);
	set_input_fmt(icd, cam_param->code);

	return 0;
}

static int check_mbus_param(struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;

	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_config cfg;
	unsigned long bus_flags, common_flags;
	int ret;

	bus_flags = get_supported_mbus_param(cam_param);

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg, bus_flags);
		if (!common_flags)
			return -EINVAL;
	} else if (ret == -ENOIOCTLCMD) {
		ret = 0;
	}

	return ret;
}

static const struct soc_mbus_pixelfmt camera_formats[] = {
	{
	 .fourcc = V4L2_PIX_FMT_YUV420,
	 .name = "YUV 4:2:0 planar 12 bit",
	 .bits_per_sample = 12,
	 .packing = SOC_MBUS_PACKING_NONE,
	 .order = SOC_MBUS_ORDER_LE,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_YVU420,
	 .name = "YVU 4:2:0 planar 12 bit",
	 .bits_per_sample = 12,
	 .packing = SOC_MBUS_PACKING_NONE,
	 .order = SOC_MBUS_ORDER_LE,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_YUV422P,
	 .name = "YUV 4:2:2 planar 16 bit",
	 .bits_per_sample = 16,
	 .packing = SOC_MBUS_PACKING_NONE,
	 .order = SOC_MBUS_ORDER_LE,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_YUYV,
	 .name = "YUYV 4:2:2 interleaved 16bit",
	 .bits_per_sample = 16,
	 .packing = SOC_MBUS_PACKING_NONE,
	 .order = SOC_MBUS_ORDER_LE,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_NV12,
	 .name = "YUV 4:2:0 semi-planar 12 bit",
	 .bits_per_sample = 12,
	 .packing = SOC_MBUS_PACKING_NONE,
	 .order = SOC_MBUS_ORDER_LE,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_NV21,
	 .name = "YUV 4:2:0 semi-planar 12 bit",
	 .bits_per_sample = 12,
	 .packing = SOC_MBUS_PACKING_NONE,
	 .order = SOC_MBUS_ORDER_LE,
	 },
};

static int client_g_rect(struct v4l2_subdev *sd, struct v4l2_rect *rect)
{
	struct v4l2_crop crop;
	struct v4l2_cropcap cap;
	int ret;

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, g_crop, &crop);
	if (!ret) {
		*rect = crop.c;
		return ret;
	}

	/* Camera driver doesn't support .g_crop(), assume default rectangle */
	cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (!ret)
		*rect = cap.defrect;

	return ret;
}

/*
 * The common for both scaling and cropping iterative approach is:
 * 1. try if the client can produce exactly what requested by the user
 * 2. if (1) failed, try to double the client image until we get one big enough
 * 3. if (2) failed, try to request the maximum image
 */
static int client_s_crop(struct soc_camera_device *icd,
			 const struct v4l2_crop *crop,
			 struct v4l2_crop *cam_crop)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_rect *rect = (struct v4l2_rect *)&crop->c;
	struct v4l2_rect *cam_rect = &cam_crop->c;
	struct v4l2_cropcap cap;
	int ret;
	unsigned int width, height;

	ret = client_g_rect(sd, cam_rect);
	if (ret < 0) {
		module_err("get sensor rect failed %d\n", ret);
		return ret;
	}

	/* Try to fix cropping, that camera hasn't managed to set */
	/* We need sensor maximum rectangle */
	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (ret < 0) {
		module_err("get sensor cropcap failed %d\n", ret);
		return ret;
	}

	/* Put user requested rectangle within sensor and isp bounds */
	soc_camera_limit_side(&rect->left, &rect->width, cap.bounds.left, 32,
			      min(MODULE_MAX_WIDTH, cap.bounds.width));

	/* TO FIXUP: must be 32B-aligned if not support stride */
	soc_camera_limit_side(&rect->top, &rect->height, cap.bounds.top, 1,
			      min(MODULE_MAX_HEIGHT, cap.bounds.height));

	/*
	 * Popular special case - some cameras can only handle fixed sizes like
	 * QVGA, VGA,... Take care to avoid infinite loop.
	 */
	width = max(cam_rect->width, 2);
	height = max(cam_rect->height, 2);

	if (!ret && (cap.bounds.width >= width || cap.bounds.height >= height)
	    && ((rect->left >= cam_rect->left) && (rect->top >= cam_rect->top)
		&& (rect->left + rect->width <=
		    cam_rect->left + cam_rect->width)
		&& (rect->top + rect->height <=
		    cam_rect->top + cam_rect->height))) {
		return 0;
	} else {
		module_err("crop rect must be within sensor rect.\n");
		return -ERANGE;
	}
}

static int parse_sensor_flags(struct camera_param *param, unsigned long flags)
{
	if (flags & SENSOR_FLAG_CH_MASK)
		param->channel = HOST_MODULE_CHANNEL_0;
	else
		param->channel = HOST_MODULE_CHANNEL_1;

	if (flags & SENSOR_FLAG_INTF_MASK) {
		param->ext_cmd = NULL;
		param->bus_type = V4L2_MBUS_PARALLEL;
	} else {
		param->ext_cmd = ext_cmd;
		param->bus_type = V4L2_MBUS_CSI2;
	}

	if (flags & SENSOR_FLAG_DATA_MASK)
		param->data_type = SENSOR_DATA_TYPE_YUV;
	else
		param->data_type = SENSOR_DATA_TYPE_RAW;

	return 0;
}

/* Called with .video_lock held */
static int camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct camera_dev *cam_dev = ici->priv;
	struct camera_param *cam_param;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	int channel = -1;
	int b_raw = -1;

#ifdef MODULE_ISP
	void __iomem *dmm_axi_bisp_priority = NULL;
	void __iomem *dmm_axi_normal_priority = NULL;
	int val;
#endif

	if (module_info->flags & SENSOR_FLAG_CH_MASK)
		channel = HOST_MODULE_CHANNEL_0;
	else
		channel = HOST_MODULE_CHANNEL_1;

#ifdef MODULE_ISP
	if (module_info->flags & SENSOR_FLAG_DATA_MASK)
		b_raw = SENSOR_DATA_TYPE_YUV;
	else
		b_raw = SENSOR_DATA_TYPE_RAW;
#endif

	if (cam_dev->icds[channel])
		return -EBUSY;
#ifdef MODULE_ISP
	/* powergate on */
	pm_runtime_get_sync(ici->v4l2_dev.dev);
#endif

	/* Cache current client geometry */
	cam_param = kzalloc(sizeof(*cam_param), GFP_KERNEL);
	if (NULL == cam_param)
		return -ENOMEM;
	cam_param->width = 0;
	cam_param->height = 0;
	cam_param->left = 0;
	cam_param->top = 0;
	cam_param->flags = module_info->flags;
	cam_param->skip_frames = 0;
	cam_param->data_type = b_raw;
	cam_param->channel = channel;

	icd->host_priv = cam_param;
	parse_sensor_flags(cam_param, module_info->flags);

	INIT_LIST_HEAD(&cam_param->capture);
	init_completion(&cam_param->wait_stop);
	cam_param->cur_frm = NULL;
	cam_param->prev_frm = NULL;
	cam_param->icd = icd;
	cam_dev->icds[channel] = icd;
	cam_param->started = DEV_OPEN;

	camera_clock_enable(cam_dev, icd);

	if (cam_param && (cam_param->data_type == SENSOR_DATA_TYPE_RAW))
		module_regs_init();
#ifdef MODULE_ISP
	dmm_axi_bisp_priority = ioremap(DMM_AXI_BISP_PRIORITY, 4);
	dmm_axi_normal_priority = ioremap(DMM_AXI_NORMAL_PRIORITY, 4);
	writel(0x0, dmm_axi_bisp_priority);
	val = readl(dmm_axi_normal_priority);
	val &= ~(1 << 4);
	writel(val, dmm_axi_normal_priority);
	val |= (1 << 4);
	writel(val, dmm_axi_normal_priority);
#endif

	return 0;
}

/* Called with .video_lock held */
static void camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct camera_dev *cam_dev = ici->priv;
	struct camera_param *cam_param = icd->host_priv;
	unsigned long flags;
	unsigned int value = 0;

	/* make sure active buffer is canceled */
	/*disable clock */
	camera_clock_disable(cam_dev, icd);
	capture_stop(cam_dev, icd);

	spin_lock_irqsave(&cam_dev->lock, flags);
	cam_param->started = DEV_CLOSE;
	spin_unlock_irqrestore(&cam_dev->lock, flags);
	icd->host_priv = NULL;

	/* powergate off */
#ifdef MODULE_ISP
	pm_runtime_put_sync(ici->v4l2_dev.dev);
#endif

	reg_write(reg_read(GMODULEMAPADDR, MODULE_STAT, MODULE_BASE),
		  GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);

	cam_dev->icds[cam_param->channel] = NULL;
	kfree(cam_param);
#ifdef MODULE_SI
	value = reg_read(GCMUMAPADDR, CMU_DEVRST0, CMU_BASE);
	value &= ~(0x3 << 13);
	reg_write(value, GCMUMAPADDR, CMU_DEVRST0, CMU_BASE);
	msleep(20);
	value |= 0x3 << 13;
	reg_write(value, GCMUMAPADDR, CMU_DEVRST0, CMU_BASE);
	msleep(20);
#elif defined MODULE_ISP
	value = reg_read(GCMUMAPADDR, CMU_DEVRST0, CMU_BASE);
	value &= ~(0x2 << 11);
	reg_write(value, GCMUMAPADDR, CMU_DEVRST0, CMU_BASE);
	msleep(20);
	value |= 0x2 << 11;
	reg_write(value, GCMUMAPADDR, CMU_DEVRST0, CMU_BASE);
	msleep(20);
#endif
}

static int camera_get_formats(struct soc_camera_device *icd, unsigned int idx,
			      struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct camera_param *cam_param = icd->host_priv;
	int formats = 0, ret, k, n;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0) {
		/* No more formats */
		return ret;
	}

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt)
		return -EINVAL;

	if (!icd->host_priv) {
		struct v4l2_mbus_framefmt mf;
		struct v4l2_rect rect;

		/* Cache current client geometry */
		ret = client_g_rect(sd, &rect);
		if (ret < 0)
			return ret;

		/* First time */
		ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
		if (ret < 0) {
			module_err("failed to g_mbus_fmt.\n");
			return ret;
		}

		cam_param = kzalloc(sizeof(*cam_param), GFP_KERNEL);
		if (NULL == cam_param) {
			module_err("alloc camera_param struct failed\n");
			return -ENOMEM;
		}

		/* We are called with current camera crop, initialise subrect
		 * with it */
		cam_param->rect = rect;
		cam_param->subrect = rect;
		cam_param->width = mf.width;
		cam_param->height = mf.height;
		cam_param->left = 0;
		cam_param->top = 0;
		cam_param->flags = module_info->flags;
		cam_param->skip_frames = 0;
		icd->host_priv = cam_param;

		parse_sensor_flags(cam_param, module_info->flags);

		/* This also checks support for the requested bits-per-sample */
		ret = check_mbus_param(icd);
		if (ret < 0) {
			kfree(cam_param);
			icd->host_priv = NULL;
			module_err("no right formats, %s, %d\n", __func__,
				   __LINE__);
			return ret;
		}
	} else {
		cam_param = icd->host_priv;
		if ((cam_param->width == 0) && (cam_param->height == 0)) {
			struct v4l2_mbus_framefmt mf;
			struct v4l2_rect rect;

			/* Cache current client geometry */
			ret = client_g_rect(sd, &rect);
			if (ret < 0) {
				module_err("failed to client_g_rect.\n");
				return ret;
			}

			/* First time */
			ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
			if (ret < 0) {
				module_err("failed to g_mbus_fmt.\n");
				return ret;
			}

			module_info("isp get sensor's default fmt %ux%u,%x\n",
				    mf.width, mf.height,
				    (unsigned int)module_info->flags);
		/* We are called with current camera crop, initialise subrect
			 * with it */
			cam_param->rect = rect;
			cam_param->subrect = rect;
			cam_param->width = mf.width;
			cam_param->height = mf.height;
			cam_param->left = 0;
			cam_param->top = 0;
			cam_param->flags = module_info->flags;
			cam_param->skip_frames = 0;
			parse_sensor_flags(cam_param, module_info->flags);

		/* This also checks support for the requested bits-per-sample */
			ret = check_mbus_param(icd);
			if (ret < 0) {
				module_err("no right formats, %s, %d\n",
					   __func__, __LINE__);
				return ret;
			}
		}
	}

	/* Beginning of a pass */
	if (!idx)
		cam_param->extra_fmt = NULL;

	switch (code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
	case V4L2_MBUS_FMT_YVYU8_2X8:
	case V4L2_MBUS_FMT_YUYV8_2X8:
	case V4L2_MBUS_FMT_SGBRG8_1X8:
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SRGGB8_1X8:
	case V4L2_MBUS_FMT_SGRBG8_1X8:
	case V4L2_MBUS_FMT_SGBRG10_1X10:
	case V4L2_MBUS_FMT_SBGGR10_1X10:
	case V4L2_MBUS_FMT_SRGGB10_1X10:
	case V4L2_MBUS_FMT_SGRBG10_1X10:
		if (cam_param->extra_fmt)
			break;

		/*
		 * Our case is simple so far: for any of the above four camera
		 * formats we add all our four synthesized NV* formats, so,
		 * just marking the device with a single flag suffices. If
		 * the format generation rules are more complex, you would have
		 * to actually hang your already added / counted formats onto
		 * the host_priv pointer and check whether the format you're
		 * going to add now is already there.
		 */
		cam_param->extra_fmt = camera_formats;
		n = ARRAY_SIZE(camera_formats);
		formats += n;
		module_info("isp provide output format:\n");
		for (k = 0; xlate && k < n; k++) {
			xlate->host_fmt = &camera_formats[k];
			xlate->code = code;
			xlate++;
			module_info("[%d].code-%#x, %s\n", k, code,
				    camera_formats[k].name);
		}
		break;

	default:
		return 0;
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt = fmt;
		xlate->code = code;
		module_info("xlate->code = %#x\n", xlate->code);
		xlate++;
	}

	return formats;
}

static void camera_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

static int check_frame_range(u32 width, u32 height)
{
	/* limit to s700 hardware capabilities */
	return height < 1 || height > MODULE_MAX_HEIGHT || width < 32
	    || width > MODULE_MAX_WIDTH;
}

static int camera_cropcap(struct soc_camera_device *icd, struct v4l2_cropcap *a)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_rect *rect = &a->defrect;
	int ret;

	ret = v4l2_subdev_call(sd, video, cropcap, a);
	if (ret < 0) {
		module_err("failed to get camera cropcap\n");
		return ret;
	}

	/* Put user requested rectangle within sensor and isp bounds */
	if (rect->width > MODULE_MAX_WIDTH)
		rect->width = MODULE_MAX_WIDTH;
	rect->width = rect->width - (rect->width % 32);

	if (rect->height > MODULE_MAX_HEIGHT)
		rect->height = MODULE_MAX_HEIGHT;

	return 0;
}

/*
 * S700 can not crop or scale for YUV,but can crop for RawData.
 * And we don't want to waste bandwidth and kill the
 * framerate by always requesting the maximum image from the client.
 */
static int camera_set_crop(struct soc_camera_device *icd,
			   const struct v4l2_crop *a)
{
	const struct v4l2_rect *rect = &a->c;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct camera_dev *cam_dev = ici->priv;
	struct camera_param *cam_param = icd->host_priv;
	struct v4l2_crop cam_crop;
	struct v4l2_rect *cam_rect = &cam_crop.c;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	unsigned long flags;
	int ret;

	/* For UYVY/Bayer Raw input data */
	/* 1. - 2. Apply iterative camera S_CROP for new input window. */
	ret = client_s_crop(icd, a, &cam_crop);
	if (ret < 0)
		return ret;

	/* On success cam_crop contains current camera crop */

	/* 3. Retrieve camera output window */
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
	if (ret < 0) {
		module_err("failed to g_mbus_fmt.\n");
		return ret;
	}

	if (check_frame_range(mf.width, mf.height)) {
		module_err("inconsistent state. Use S_FMT to repair\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&cam_dev->lock, flags);

	/* Cache camera output window */
	cam_param->width = mf.width;
	cam_param->height = mf.height;

	icd->user_width = rect->width;
	icd->user_height = rect->height;
	cam_param->left = rect->left - cam_rect->left + cam_param->n_crop_x;
	cam_param->top = rect->top - cam_rect->top + cam_param->n_crop_y;

#ifdef MODULE_ISP
	if (cam_param->n_r_skip_num && cam_param->data_type ==
	    SENSOR_DATA_TYPE_RAW) {
		cam_param->width = rect->width;
		cam_param->height = rect->height;

		if (cam_param->n_r_skip_num == 2) {
			cam_param->n_crop_x = (cam_param->n_crop_x * 2) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y * 2) & (~7);
		} else if (cam_param->n_r_skip_num == 3) {
			cam_param->n_crop_x = (cam_param->n_crop_x * 3) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y * 3) & (~7);
		}
	}
#endif

	/* 4. Use S700 cropping to crop to the new window. */
	cam_param->rect = *cam_rect;
	cam_param->subrect = *rect;
	spin_unlock_irqrestore(&cam_dev->lock, flags);
	raw_store_set(icd);
	module_info("4: isp cropped to %ux%u@%u:%u,%d,%d\n", icd->user_width,
		    icd->user_height, cam_param->left, cam_param->top,
		    cam_param->real_w, cam_param->real_h);

	return ret;
}

static int camera_get_crop(struct soc_camera_device *icd, struct v4l2_crop *a)
{
	struct camera_param *cam_param = icd->host_priv;

	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->c = cam_param->subrect;

	return 0;
}

static int camera_set_fmt(struct soc_camera_device *icd, struct v4l2_format *f)
{
	struct camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	unsigned int skip_frames_num;
	int ret;
	int b_skip_en = 0;

	module_info("%s, S_FMT(pix=0x%x, %ux%u)\n", __func__, pixfmt,
		    pix->width, pix->height);

	/* sensor may skip different some frames */
	ret = v4l2_subdev_call(sd, sensor, g_skip_frames, &skip_frames_num);
	if (ret < 0)
		skip_frames_num = 0;
	cam_param->skip_frames = skip_frames_num;
	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		module_info("format %x not found\n", pixfmt);
		return -EINVAL;
	}

	mf.width = pix->width;
	mf.height = pix->height;
	mf.field = pix->field;
	mf.colorspace = pix->colorspace;
	mf.code = xlate->code;
	v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		cam_param->real_w = mf.width;
		cam_param->real_h = mf.height;
	} else {
		cam_param->real_w = pix->width;
		cam_param->real_h = pix->height;
	}

#ifdef MODULE_SI
	cam_param->n_crop_x = 0;
	cam_param->n_crop_y = 0;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (ret < 0) {
		module_err("failed to configure for format %x\n",
			   pix->pixelformat);
		return ret;
	}

	if (mf.code != xlate->code) {
		module_err("wrong code: mf.code = 0x%x, xlate->code = 0x%x\n",
			   mf.code, xlate->code);
		return -EINVAL;
	}

	if (check_frame_range(mf.width, mf.height)) {
		module_err("sensor produced an unsupported frame %dx%d\n",
			   mf.width, mf.height);
		return -EINVAL;
	}

	cam_param->left = 0;
	cam_param->top = 0;

	if (b_skip_en == 1) {
		mf.width = pix->width;
		mf.height = pix->height;
		cam_param->left = cam_param->n_crop_x;
		cam_param->top = cam_param->n_crop_y;
	}

	cam_param->width = mf.width;
	cam_param->height = mf.height;
	cam_param->code = xlate->code;

	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
						    xlate->host_fmt);
	if (pix->bytesperline < 0) {
		module_err("bytesperline %d not correct.\n", pix->bytesperline);
		return pix->bytesperline;
	}
	pix->sizeimage = pix->height * pix->bytesperline;
	pix->width = mf.width;
	pix->height = mf.height;
	pix->field = mf.field;
	pix->colorspace = mf.colorspace;

	icd->current_fmt = xlate;
	icd->user_width = cam_param->width;
	icd->user_height = cam_param->height;

	module_info("sensor set %dx%d, code = %#x\n", pix->width, pix->height,
		    cam_param->code);

	set_rect(icd);
	set_output_fmt(icd, icd->current_fmt->host_fmt->fourcc);

	module_info("set output data format %s\n",
		    icd->current_fmt->host_fmt->name);

#elif defined MODULE_ISP
	if ((mf.width != pix->width || mf.height != pix->height) &&
	    (cam_param->data_type == SENSOR_DATA_TYPE_RAW)) {
		unsigned int isp_ctl = reg_read(GMODULEMAPADDR, ISP_CTL,
						MODULE_BASE);
		unsigned int rh = 0;
		cam_param->n_r_skip_num = mf.width / pix->width;
		cam_param->n_r_skip_size = pix->width;
		rh = pix->height;

		cam_param->n_r_skip_size = cam_param->n_r_skip_size & (~0xf);

		if (cam_param->n_r_skip_num == 1) {
			reg_write(isp_ctl & (~7), GMODULEMAPADDR, ISP_CTL,
				  MODULE_BASE);
			cam_param->n_r_skip_num = 0;
			cam_param->n_r_skip_size = 0;
			cam_param->n_crop_x = 0;
			cam_param->n_crop_y = 0;
		} else if (cam_param->n_r_skip_num == 2) {
			reg_write(isp_ctl | 4, GMODULEMAPADDR, ISP_CTL,
				  MODULE_BASE);
			cam_param->n_crop_x = (cam_param->real_w -
					       cam_param->n_r_skip_size * 2) /
			    2;
			cam_param->n_crop_y = (cam_param->real_h - rh * 2) / 2;
			cam_param->n_crop_x = (cam_param->n_crop_x) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y) & (~3);
			cam_param->real_w = 2 * cam_param->n_r_skip_size;
			cam_param->real_h = 2 * rh;

			reg_write(cam_param->n_r_skip_size, GMODULEMAPADDR,
				  RAW_SKIP_SIZE, MODULE_BASE);
			b_skip_en = 1;

		} else if (cam_param->n_r_skip_num == 3) {
			cam_param->n_crop_x = (cam_param->real_w -
					       cam_param->n_r_skip_size * 3) /
			    2;
			cam_param->n_crop_y = (cam_param->real_h - rh * 3) / 2;
			cam_param->n_crop_x = (cam_param->n_crop_x) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y) & (~3);
			cam_param->real_w = 3 * cam_param->n_r_skip_size;
			cam_param->real_h = 3 * rh;

			reg_write(isp_ctl | 6, GMODULEMAPADDR, ISP_CTL,
				  MODULE_BASE);
			reg_write(cam_param->n_r_skip_size, GMODULEMAPADDR,
				  RAW_SKIP_SIZE, MODULE_BASE);
			b_skip_en = 1;
		}
	} else {
		unsigned int isp_ctl = reg_read(GMODULEMAPADDR, ISP_CTL,
						MODULE_BASE);
		cam_param->n_r_skip_num = 0;
		cam_param->n_r_skip_size = 0;
		cam_param->n_crop_x = 0;
		cam_param->n_crop_y = 0;
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			reg_write((isp_ctl & (~4)), GMODULEMAPADDR, ISP_CTL,
				  MODULE_BASE);
		}
	}

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if (mf.code != xlate->code)
		return -EINVAL;

	if (check_frame_range(mf.width, mf.height))
		return -EINVAL;

	cam_param->left = 0;
	cam_param->top = 0;

	if (b_skip_en == 1) {
		mf.width = pix->width;
		mf.height = pix->height;
		cam_param->left = cam_param->n_crop_x;
		cam_param->top = cam_param->n_crop_y;
	}

	cam_param->width = mf.width;
	cam_param->height = mf.height;
	cam_param->code = xlate->code;

	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
						    xlate->host_fmt);
	if (pix->bytesperline < 0)
		return pix->bytesperline;
	pix->sizeimage = pix->height * pix->bytesperline;
	pix->width = mf.width;
	pix->height = mf.height;
	pix->field = mf.field;
	pix->colorspace = mf.colorspace;

	icd->current_fmt = xlate;
	icd->user_width = cam_param->width;
	icd->user_height = cam_param->height;

	set_rect(icd);
	set_output_fmt(icd, icd->current_fmt->host_fmt->fourcc);
#endif
	return 0;
}

static int camera_try_fmt(struct soc_camera_device *icd, struct v4l2_format *f)
{
	struct camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (pixfmt && !xlate) {
		module_err("format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/*
	 * Limit to S700 hardware capabilities.  YUV422P planar format requires
	 * images size to be a multiple of 16 bytes.  If not, zeros will be
	 * inserted between Y and U planes, and U and V planes, which violates
	 * the YUV422P standard.
	 */
	v4l_bound_align_image(&pix->width, 32, MODULE_MAX_WIDTH, 5,
			      &pix->height, 1, MODULE_MAX_HEIGHT, 0,
			      pixfmt == V4L2_PIX_FMT_YUV422P ? 4 : 0);

	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
						    xlate->host_fmt);
	if (pix->bytesperline < 0) {
		module_err("bytesperline %d not correct.\n", pix->bytesperline);
		return pix->bytesperline;
	}
	pix->sizeimage = pix->height * pix->bytesperline;

	/* limit to sensor capabilities */
	mf.width = pix->width;
	mf.height = pix->height;
	mf.field = pix->field;
	mf.colorspace = pix->colorspace;
	mf.code = xlate->code;

	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if ((mf.width != pix->width || mf.height != pix->height) &&
	    (cam_param->data_type == SENSOR_DATA_TYPE_RAW) &&
	    pix->width && pix->height) {
		mf.width = pix->width;
		mf.height = pix->height;
	}

	pix->width = mf.width;
	pix->height = mf.height;
	pix->colorspace = mf.colorspace;

	switch (mf.field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		pix->field = V4L2_FIELD_NONE;
		break;
	default:
		module_err("field type %d unsupported.\n", mf.field);
		ret = -EINVAL;
		break;
	}

	return ret;
}

struct camera_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;
	enum v4l2_mbus_pixelcode code;
};

static int camera_reqbufs(struct soc_camera_device *icd,
			  struct v4l2_requestbuffers *p)
{
	int i;

	/*
	 * This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered
	 */
	for (i = 0; i < p->count; i++) {
		struct camera_buffer *buf;

		buf = container_of(icd->vb_vidq.bufs[i], struct camera_buffer,
				   vb);
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return videobuf_poll_stream(file, &icd->vb_vidq, pt);
}

static int camera_querycap(struct soc_camera_host *ici,
			   struct v4l2_capability *cap)
{
	/* cap->name is set by the firendly caller:-> */
	strlcpy(cap->card, "Camera", sizeof(cap->card));
	cap->version = KERNEL_VERSION(0, 0, 1);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int camera_get_parm(struct soc_camera_device *icd,
			   struct v4l2_streamparm *parm)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, g_parm, parm);
}

static int camera_set_parm(struct soc_camera_device *icd,
			   struct v4l2_streamparm *parm)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, s_parm, parm);
}

static int camera_suspend(struct device *dev)
{
	module_camera_suspend(dev);

	return 0;
}

static int camera_resume(struct device *dev)
{
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct camera_dev *cam_dev = container_of(ici, struct camera_dev,
						  soc_host);

	/*clk_set_parent and clk_set_rate will check the setting value
	   with last value,if no different,it will ignore and return back
	   so set the value of rate and parent with a init value in order
	   to avoid ignoring the setting value */
	cam_dev->module_clk->parent = NULL;
	cam_dev->module_clk->rate = 0;

	cam_dev->ch_clk[HOST_MODULE_CHANNEL_0]->parent = NULL;
	cam_dev->ch_clk[HOST_MODULE_CHANNEL_0]->rate = 0;

	cam_dev->ch_clk[HOST_MODULE_CHANNEL_1]->parent = NULL;
	cam_dev->ch_clk[HOST_MODULE_CHANNEL_1]->rate = 0;

	/*setting si clock parent -> display_clk */
	clk_set_parent(cam_dev->module_clk, cam_dev->module_clk_parent);

	reg_write(reg_read(GMODULEMAPADDR, MODULE_STAT, MODULE_BASE),
		  GMODULEMAPADDR, MODULE_STAT, MODULE_BASE);
	module_camera_resume(dev, cam_dev);

	return 0;
}

int camera_enum_fsizes(struct soc_camera_device *icd,
		       struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, enum_framesizes, fsize);
}

static void free_buffer(struct videobuf_queue *vq, struct camera_buffer *buf)
{
	if (in_interrupt())
		BUG();

	videobuf_waiton(vq, &buf->vb, 0, 0);
	if (buf->vb.memory == V4L2_MEMORY_MMAP)
		videobuf_dma_contig_free(vq, &buf->vb);

	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

/*
 * Videobuf operations
 */

/*
 * Calculate the __buffer__ (not data) size and number of buffers.
 * Called with .vb_lock held
 */
static int camera_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
				 unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						     icd->current_fmt->
						     host_fmt);

	if (bytes_per_line < 0)
		return bytes_per_line;

	*size = bytes_per_line * icd->user_height;

	if (!*count || *count < 2)
		*count = 2;

	if (*size * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = MAX_VIDEO_MEM * 1024 * 1024 / *size;

	module_info("count=%d, size=%d\n", *count, *size);

	return 0;
}

/* Called with .vb_lock held */
static int camera_videobuf_prepare(struct videobuf_queue *vq,
				   struct videobuf_buffer *vb,
				   enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct device *dev = icd->parent;
	struct camera_buffer *buf;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						     icd->current_fmt->
						     host_fmt);
	int ret;

	if (bytes_per_line < 0) {
		module_err("bytes_per_line err\n");
		return bytes_per_line;
	}

	buf = container_of(vb, struct camera_buffer, vb);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));
	BUG_ON(NULL == icd->current_fmt);

	if (buf->code != icd->current_fmt->code
	    || vb->width != icd->user_width
	    || vb->height != icd->user_height || vb->field != field) {
		buf->code = icd->current_fmt->code;
		vb->width = icd->user_width;
		vb->height = icd->user_height;
		vb->field = field;
		vb->state = VIDEOBUF_NEEDS_INIT;
		bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
							 icd->current_fmt->
							 host_fmt);
	}

	vb->size = vb->height * bytes_per_line;
	if (0 != vb->baddr && vb->bsize < vb->size) {
		dev_err(dev, "buffer error!\n");
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		if ((vb->memory == V4L2_MEMORY_USERPTR) && (0 != vb->baddr))
			ret = 0;
		else
			ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto fail;
		vb->state = VIDEOBUF_PREPARED;
	}

	return 0;
 fail:
	module_err("buffer err\n");
	free_buffer(vq, buf);
 out:
	return ret;
}

/*
 * Called with .vb_lock mutex held and
 * under spinlock_irqsave(&cam_dev->lock, ...)
 */
static void camera_videobuf_queue(struct videobuf_queue *vq,
				  struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct camera_dev *cam_dev = ici->priv;
	struct camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	BUG_ON(!irqs_disabled());
	vb->state = VIDEOBUF_QUEUED;

	list_add_tail(&vb->queue, &cam_param->capture);

	if (!cam_param->cur_frm) {
		cam_param->cur_frm = list_entry(cam_param->capture.next,
						struct videobuf_buffer, queue);
		cam_param->cur_frm->state = VIDEOBUF_ACTIVE;

		list_del_init(&cam_param->cur_frm->queue);
		cam_param->prev_frm = NULL;

		updata_module_info(sd, cam_param);

		set_rect(icd);
		set_frame(icd, cam_param->cur_frm);
		capture_start(cam_dev, icd);
	}
}

/* Called with .vb_lock held */
static void camera_videobuf_release(struct videobuf_queue *vq,
				    struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct camera_dev *cam_dev = ici->priv;
	struct camera_param *cam_param = icd->host_priv;
	unsigned long flags;
	int i;
	capture_stop(cam_dev, icd);

	spin_lock_irqsave(&cam_dev->lock, flags);

	if (cam_param->cur_frm == vb)
		cam_param->cur_frm = NULL;

	if (cam_param->prev_frm == vb)
		cam_param->prev_frm = NULL;

	if ((vb->state == VIDEOBUF_ACTIVE || vb->state == VIDEOBUF_QUEUED)) {
		vb->state = VIDEOBUF_ERROR;
		if (!list_empty(&cam_param->capture))
			list_del_init(&vb->queue);
		wake_up_all(&vb->done);
	}

	spin_unlock_irqrestore(&cam_dev->lock, flags);

	free_buffer(vq, container_of(vb, struct camera_buffer, vb));

	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == vq->bufs[i])
			continue;
		if (vb == vq->bufs[i]) {
			kfree(vq->bufs[i]);
			vq->bufs[i] = NULL;
		}
	}
}

static struct videobuf_queue_ops camera_videobuf_ops = {
	.buf_setup = camera_videobuf_setup,
	.buf_prepare = camera_videobuf_prepare,
	.buf_queue = camera_videobuf_queue,
	.buf_release = camera_videobuf_release,
};

static void camera_init_videobuf(struct videobuf_queue *q,
				 struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct camera_dev *cam_dev = ici->priv;
	struct camera_param *cam_param = icd->host_priv;
	struct device *dev = icd->parent;

	videobuf_queue_dma_contig_init(q, &camera_videobuf_ops, dev,
				       &cam_dev->lock,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE,
				       V4L2_FIELD_NONE,
				       sizeof(struct camera_buffer), icd, NULL);
	/*If add below, need to submit camera_init_videobuf function is
	 * called after soc_camera_set_fmt */
	/*If kernel version change, please check this line */
	cam_param->started = DEV_OPEN;
}

static struct soc_camera_host_ops soc_camera_host_ops = {
	.owner = THIS_MODULE,
	.add = camera_add_device,
	.remove = camera_remove_device,
	.get_formats = camera_get_formats,
	.put_formats = camera_put_formats,
	.cropcap = camera_cropcap,
	.get_crop = camera_get_crop,
	.set_crop = camera_set_crop,
	.set_livecrop = camera_set_crop,
	.set_fmt = camera_set_fmt,
	.try_fmt = camera_try_fmt,
	.set_parm = camera_set_parm,
	.get_parm = camera_get_parm,
	.reqbufs = camera_reqbufs,
	.poll = camera_poll,
	.querycap = camera_querycap,
	.set_bus_param = camera_set_bus_param,
	.init_videobuf = camera_init_videobuf,
	.enum_framesizes = camera_enum_fsizes,
};

static inline void sensor_pwd_info_init(struct sensor_pwd_info *spinfo)
{
	spinfo->flag = 0;
	spinfo->gpio_rear.num = -1;
	spinfo->gpio_front.num = -1;
	spinfo->gpio_front_reset.num = -1;
	spinfo->gpio_rear_reset.num = -1;
	spinfo->ch_clk[HOST_MODULE_CHANNEL_0] = NULL;
	spinfo->ch_clk[HOST_MODULE_CHANNEL_1] = NULL;
}

static inline void regulators_init(struct module_regulators *ir)
{
	struct dts_regulator *dr = &ir->avdd.regul;

	dr->regul = NULL;
	ir->avdd_use_gpio = 0;
	ir->dvdd_use_gpio = 0;
	ir->dvdd.regul = NULL;
}

static int gpio_init(struct device_node *fdt_node,
		     const char *gpio_name, struct dts_gpio *gpio, bool active)
{
	enum of_gpio_flags flags;

	if (!of_find_property(fdt_node, gpio_name, NULL)) {
		module_err("<isp> %s no config gpios\n", gpio_name);
		goto fail;
	}

	gpio->num = of_get_named_gpio_flags(fdt_node, gpio_name, 0, &flags);
	gpio->active_level = !(!(flags & OF_GPIO_ACTIVE_LOW));

	module_info("%s: num-%d, active-%s\n", gpio_name, gpio->num,
		    gpio->active_level ? "high" : "low");

	if (gpio_request(gpio->num, gpio_name)) {
		module_err("<isp>fail to request gpio [%d]\n", gpio->num);
		gpio->num = -1;
		goto fail;
	}
	if (active)
		gpio_direction_output(gpio->num, gpio->active_level);
	else
		gpio_direction_output(gpio->num, !gpio->active_level);

	module_info("gpio value: 0x%x\n", gpio_get_value(gpio->num));

	return 0;
 fail:
	return -1;
}

static void gpio_exit(struct dts_gpio *gpio, bool active)
{
	if (gpio->num >= 0) {
		if (active)
			gpio_set_value(gpio->num, gpio->active_level);
		else
			gpio_set_value(gpio->num, !gpio->active_level);

		gpio_free(gpio->num);
	}
}

static int module_gpio_init(struct device_node *fdt_node,
			    struct sensor_pwd_info *spinfo)
{
	const char *sensors = NULL;
	const char *board_type = NULL;
	const char *avdd_src = NULL;
	const char *rear_avdd_src = NULL;
	const char *front_avdd_src = NULL;

	if (of_property_read_string(fdt_node, "board_type", &board_type)) {
		module_err("get board_type faild");
		goto free_reset;
	}
	if (!strcmp(board_type, "ces")) {
		if (gpio_init(fdt_node, "front-reset-gpios",
			      &spinfo->gpio_front_reset, GPIO_HIGH)) {
			goto fail;
		}
		if (gpio_init(fdt_node, "rear-reset-gpios",
			      &spinfo->gpio_rear_reset, GPIO_HIGH)) {
			goto fail;
		}
	} else if (!strcmp(board_type, "evb")) {
		if (gpio_init
		    (fdt_node, "reset-gpios", &spinfo->gpio_front_reset,
		     GPIO_HIGH)) {
			goto fail;
		}
		spinfo->gpio_rear_reset.num = spinfo->gpio_front_reset.num;
		spinfo->gpio_rear_reset.active_level =
		    spinfo->gpio_front_reset.active_level;
	} else {
		module_err("get board type faild");
		return -1;
	}

	if (of_property_read_string(fdt_node, "sensors", &sensors)) {
		module_err("<isp> get sensors faild\n");
		goto free_reset;
	}

	if (!strcmp(sensors, "front")) {
		/* default is power-down */
		if (gpio_init(fdt_node, "pwdn-front-gpios", &spinfo->gpio_front,
			      GPIO_HIGH)) {
			goto free_reset;
		}
		spinfo->flag = SENSOR_FRONT;
	} else if (!strcmp(sensors, "rear")) {
		if (gpio_init(fdt_node, "pwdn-rear-gpios", &spinfo->gpio_rear,
			      GPIO_HIGH)) {
			goto free_reset;
		}
		spinfo->flag = SENSOR_REAR;
	} else if (!strcmp(sensors, "dual")) {
		if (gpio_init(fdt_node, "pwdn-front-gpios", &spinfo->gpio_front,
			      GPIO_HIGH)) {
			goto free_reset;
		}
		if (gpio_init(fdt_node, "pwdn-rear-gpios", &spinfo->gpio_rear,
			      GPIO_HIGH)) {
			gpio_exit(&spinfo->gpio_front, GPIO_LOW);
			goto free_reset;
		}
		spinfo->flag = SENSOR_DUAL;
	} else {
		module_err("sensors of dts is wrong\n");
		goto free_reset;
	}

	if (of_property_read_string(fdt_node, "avdd-src", &avdd_src)) {
		module_info("get avdd-src faild");
	    if (of_property_read_string(fdt_node, "rear-avdd-src", &rear_avdd_src)) {
		    module_info("get rear-avdd-src faild");
        }
	    if (of_property_read_string(fdt_node, "front-avdd-src", &front_avdd_src)) {
		    module_info("get front-avdd-src faild");
        }
        if (rear_avdd_src == NULL && front_avdd_src == NULL) {
		    module_info("get all avdd-src faild");
		    goto free_reset;
        }
	}

    if (avdd_src != NULL) {
	    if (!strcmp(avdd_src, "regulator")) {
	    	module_info("avdd using regulator");
	    } else if (!strcmp(avdd_src, "gpio")) {
	    	if (gpio_init(fdt_node, "avdd-gpios",
	    		      &spinfo->gpio_power, GPIO_HIGH)) {
	    		gpio_exit(&spinfo->gpio_power, GPIO_LOW);
	    		goto free_reset;
	    	}
	    }
    }

    if (rear_avdd_src != NULL) {
	    if (!strcmp(rear_avdd_src, "regulator")) {
	    	module_info("rear_avdd using regulator");
	    } else if (!strcmp(rear_avdd_src, "gpio")) {
	    	if (gpio_init(fdt_node, "rear-avdd-gpios",
	    		      &spinfo->gpio_rear_power, GPIO_HIGH)) {
	    		gpio_exit(&spinfo->gpio_rear_power, GPIO_LOW);
	    		goto free_reset;
	    	}
	    }
    }

    if (front_avdd_src != NULL) {
	    if (!strcmp(front_avdd_src, "regulator")) {
	    	module_info("front_avdd using regulator");
	    } else if (!strcmp(front_avdd_src, "gpio")) {
	    	if (gpio_init(fdt_node, "front-avdd-gpios",
	    		      &spinfo->gpio_front_power, GPIO_HIGH)) {
	    		gpio_exit(&spinfo->gpio_front_power, GPIO_LOW);
	    		goto free_reset;
	    	}
	    }
    }
	return 0;

 free_reset:
	gpio_exit(&spinfo->gpio_front, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear, GPIO_LOW);
	gpio_exit(&spinfo->gpio_front_reset, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
 fail:
	return -1;
}

static void module_gpio_exit(struct sensor_pwd_info *spinfo)
{
	/* only free valid gpio, so no need to check its existence. */
	gpio_exit(&spinfo->gpio_front, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
	gpio_exit(&spinfo->gpio_front_reset, GPIO_LOW);
}

static struct camera_dev *cam_dev_alloc(struct device *dev,
					struct device_node *dn)
{
	struct camera_dev *cdev;

	cdev = kzalloc(sizeof(struct camera_dev), GFP_ATOMIC);
	if (NULL == cdev) {
		module_err("alloc s700 camera device failed\n");
		goto ealloc;
	}

	spin_lock_init(&cdev->lock);

	cdev->mfp = NULL;
	cdev->module_clk = NULL;
	cdev->csi_clk = NULL;
	cdev->ch_clk[HOST_MODULE_CHANNEL_0] = NULL;
	cdev->ch_clk[HOST_MODULE_CHANNEL_1] = NULL;

	sensor_pwd_info_init(&cdev->spinfo);
	if (camera_clock_init(dev, cdev)) {
		module_err("camera clocks init error\n");
		goto clk_err;
	}

	if (module_gpio_init(dn, &(cdev->spinfo))) {
		module_err("gpio init error\n");
		goto egpio;
	}
#ifdef MODULE_ISP
	cdev->mfp = pinctrl_get_select_default(dev);
	if (IS_ERR(cdev->mfp))
		return NULL;
#endif

	return cdev;

 clk_err:
	kfree(cdev);
 ealloc:
 egpio:
	return NULL;
}

static void cam_dev_free(struct camera_dev *cdev)
{
	module_info("cam_dev_free\n");
	module_gpio_exit(&cdev->spinfo);
	pinctrl_put(cdev->mfp);
	kfree(cdev);
}

static int camera_host_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct camera_dev *cam_dev;
	struct soc_camera_host *soc_host;
	unsigned int irq;
	int err = 0;
	struct resource *module_mem;
	struct resource *csi0_mem;
#ifdef MODULE_ISP
	struct resource *csi1_mem;
#endif

	pdev->id = of_alias_get_id(dn, ALIAS_ID);
	if (pdev->id < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n",
			pdev->id);
		goto eid;
	}

	cam_dev = cam_dev_alloc(&pdev->dev, dn);
	if (NULL == cam_dev) {
		dev_err(&pdev->dev, "camera_dev alloc failed\n");
		goto eid;
	}

	attach_sensor_pwd_info(&pdev->dev, &cam_dev->spinfo, pdev->id);

	module_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!module_mem) {
		dev_err(dev, "can't get module_mem IORESOURCE_MEM\n");
		return -EINVAL;
	}

	GMODULEMAPADDR = devm_ioremap(dev, module_mem->start,
				      resource_size(module_mem));
	if (!GMODULEMAPADDR) {
		dev_err(dev, "can't ioremap module_mem\n");
		return -ENOMEM;
	}

	csi0_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!csi0_mem) {
		dev_err(dev, "can't get csi0_mem IORESOURCE_MEM\n");
		return -EINVAL;
	}

	GCSI1MAPADDR = devm_ioremap(dev, csi0_mem->start,
				    resource_size(csi0_mem));
	if (!GCSI1MAPADDR) {
		dev_err(dev, "can't ioremap csi0_mem\n");
		return -ENOMEM;
	}
#ifdef MODULE_ISP
	csi1_mem = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!csi1_mem) {
		dev_err(dev, "can't get csi1_mem IORESOURCE_MEM\n");
		return -EINVAL;
	}

	GCSI2MAPADDR = devm_ioremap(dev, csi1_mem->start,
				    resource_size(csi1_mem));
	if (!GCSI2MAPADDR) {
		dev_err(dev, "can't ioremap csi1_mem\n");
		return -ENOMEM;
	}
#endif

	if (GCMUMAPADDR == 0)
		GCMUMAPADDR = ioremap(CMU_BASE, 0xfc);
#ifdef MODULE_SI
	if (GMFPADDR == 0)
		GMFPADDR = ioremap(MFP_CTL3, 0x4);

	if (GPADADDR == 0)
		GPADADDR = ioremap(PAD_CTL, 0x4);

	if (GGPIOCADDR == 0)
		GGPIOCADDR = ioremap(GPIO_COUTEN, 0xc);
#endif

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "no isp irq resource?\n");
		err = -ENODEV;
		goto egetirq;
	}
	cam_dev->irq = irq;
	err = devm_request_irq(&pdev->dev, cam_dev->irq, camera_host_isr,
			       IRQF_DISABLED, CAM_HOST_NAME, cam_dev);
	if (err) {
		dev_err(&pdev->dev, "Unable to register isp %d interrupt.\n",
			cam_dev->irq);
		err = -EBUSY;
		goto egetirq;
	}
#ifdef MODULE_SI
	pm_runtime_enable(&pdev->dev);
	mdelay(100);
	pm_runtime_get_sync(&pdev->dev);
	mdelay(150);
#elif defined MODULE_ISP
	pm_runtime_enable(&pdev->dev);
#endif

	soc_host = &cam_dev->soc_host;
	soc_host->ops = &soc_camera_host_ops;
	soc_host->priv = cam_dev;
	soc_host->v4l2_dev.dev = &pdev->dev;
	soc_host->nr = pdev->id;
	module_info("host id %d\n", soc_host->nr);
	switch (soc_host->nr) {
	case 0:
		soc_host->drv_name = CAM_HOST_NAME;
		break;
	case 1:
		soc_host->drv_name = CAM_HOST_NAME;
		break;
	default:
		module_err("host num error\n");
		break;
	}

	err = soc_camera_host_register(soc_host);
	if (err) {
		dev_err(&pdev->dev, "Unable to register soc camera host.\n");
		goto echreg;
	}

	module_info("isp driver probe success...\n");
	return 0;

 echreg:
#ifdef MODULE_SI
	pm_runtime_put_sync(&pdev->dev);
#elif defined MODULE_ISP
	pm_runtime_disable(&pdev->dev);
#endif

 egetirq:
	detach_sensor_pwd_info(&pdev->dev, &cam_dev->spinfo, pdev->id);
	cam_dev_free(cam_dev);

 eid:
	module_err("isp driver probe fail...\n");

	return err;
}

static int camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct camera_dev *cam_dev = soc_host->priv;

	soc_camera_host_unregister(soc_host);
	pm_runtime_put_sync(&pdev->dev);
	detach_sensor_pwd_info(&pdev->dev, &cam_dev->spinfo, pdev->id);
	cam_dev_free(cam_dev);

	if (GCMUMAPADDR)
		iounmap(GCMUMAPADDR);

	return 0;
}

/*s700 and s900 is strictly different in this ops!*/
static const struct dev_pm_ops camera_dev_pm_ops = {
#ifdef MODULE_SI
	.suspend = camera_suspend,
	.resume = camera_resume,
#elif defined MODULE_ISP
	.runtime_suspend = camera_suspend,
	.runtime_resume = camera_resume,
#endif
};

static const struct of_device_id camera_of_match[] = {
	{.compatible = FDT_COMPATIBLE,},
	{},
};

MODULE_DEVICE_TABLE(of, camera_of_match);

static struct platform_driver camera_host_driver = {
	.driver = {
		   .name = CAM_HOST_NAME,
		   .owner = THIS_MODULE,
		   .pm = &camera_dev_pm_ops,
		   .of_match_table = camera_of_match,
		   },
	.probe = camera_host_probe,
	.remove = camera_remove,
};

/* platform device register by dts */
static int __init camera_init(void)
{
	int ret;

	ret = host_module_init();
	ret = platform_driver_register(&camera_host_driver);
	if (ret)
		module_err(":Could not register isp driver\n");
	module_info("camera_host_driver ok\n");
	return ret;
}

static void __exit camera_exit(void)
{
	host_module_exit();
	platform_driver_unregister(&camera_host_driver);
}

late_initcall(camera_init);
module_exit(camera_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Actions Semi, Inc");
MODULE_DESCRIPTION("Actions SOC driver");
