/*
 * Actions OWL SoCs BISP driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Shiheng Tan <tanshiheng@actions-semi.com>
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

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/errno.h>	/* error codes */
#include <linux/vmalloc.h>
#include <linux/init.h>		/* module_init/module_exit */
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/prom.h>
#include "bisp_drv.h"
#include "bisp_reg.h"

#include "module_diff.h"
#include "../host_comm/owl_camera.c"

static inline int module_camera_clock_init(struct device *dev,
					   struct camera_dev *cdev)
{
	cdev->sensor_clk = devm_clk_get(dev, SENSOR_CLK);
	if (IS_ERR(cdev->sensor_clk)) {
		module_err("error: sensor_clk can't get clocks!!!\n");
		return -EINVAL;
	}

	clk_set_parent(cdev->sensor_clk,
		       cdev->sensor_clk_parent[HOST_MODULE_CHANNEL_0]);

	clk_set_parent(cdev->ch_clk[HOST_MODULE_CHANNEL_0],
		       cdev->csi_clk_parent);
	clk_set_parent(cdev->ch_clk[HOST_MODULE_CHANNEL_1],
		       cdev->csi_clk_parent);

	return 0;
}

static inline int module_camera_clock_enable(struct camera_dev *cdev,
					     struct camera_param *cam_param,
					     struct mipi_setting *mipi_cfg)
{
	clk_set_rate(cdev->sensor_clk, SENSOR_WORK_CLOCK);
	clk_prepare(cdev->sensor_clk);
	clk_enable(cdev->sensor_clk);

	/* Enable CSI CLOCK */
	if (mipi_cfg) {
		clk_set_rate(cdev->ch_clk[cam_param->channel],
			     mipi_cfg->csi_clk);
		clk_prepare(cdev->ch_clk[cam_param->channel]);
		clk_enable(cdev->ch_clk[cam_param->channel]);
	}

	return 0;
}

static inline void module_camera_clock_disable(struct camera_dev *cdev)
{
	clk_put(cdev->sensor_clk);
}

static inline void module_mipi_csi_init(struct camera_param *cam_param,
					struct mipi_setting *mipi_cfg)
{
	unsigned int reg_data = 0;
	int time = 50;

	reg_data = 0x0c | ((mipi_cfg->lan_num & 0x3) << 4)
	    | (mipi_cfg->ecc_en << 6)
	    | (mipi_cfg->crc_en << 7)
	    | (mipi_cfg->hclk_om_ent_en << 8)
	    | (mipi_cfg->lp11_not_chek << 9)
	    | (mipi_cfg->hsclk_edge << 10);

	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		reg_write(MIPI_PHY_4LANE | (mipi_cfg->clk_lane_direction << 5),
			  GCSI1MAPADDR, CSI0_ANALOG_PHY, CSI0_BASE);
		reg_write(reg_data, GCSI1MAPADDR, CSI0_CTRL, CSI0_BASE);
		reg_data = reg_read(GCSI1MAPADDR, CSI0_CTRL, CSI0_BASE);

		while ((reg_data & CSI_CTRL_PHY_INIT) && (--time > 0)) {
			reg_data = reg_read(GCSI1MAPADDR, CSI0_CTRL, CSI0_BASE);
			udelay(1);
		}
		if (time <= 0)
			module_err("csi0 init time out\n");

		reg_write((mipi_cfg->contex0_virtual_num << 7)
			  | (mipi_cfg->contex0_data_type << 1)
			  | (mipi_cfg->contex0_en),
			  GCSI1MAPADDR, CSI0_CONTEXT_CFG, CSI0_BASE);
		reg_write(0xF4, GCSI1MAPADDR, CSI0_PHY_T0, CSI0_BASE);
		reg_write(mipi_cfg->clk_term_time +
			  (mipi_cfg->clk_settle_time << 4), GCSI1MAPADDR,
			  CSI0_PHY_T1, CSI0_BASE);
		reg_write(mipi_cfg->data_term_time +
			  (mipi_cfg->data_settle_time << 4), GCSI1MAPADDR,
			  CSI0_PHY_T2, CSI0_BASE);
		reg_write(0xffffffff, GCSI1MAPADDR, CSI0_ERROR_PENDING,
			  CSI0_BASE);
		reg_write(0xffff0000, GCSI1MAPADDR, CSI0_STATUS_PENDING,
			  CSI0_BASE);
		reg_write(reg_read(GCSI1MAPADDR, CSI0_CTRL, CSI0_BASE)
			  | CSI_CTRL_EN, GCSI1MAPADDR, CSI0_CTRL, CSI0_BASE);
		reg_write((mipi_cfg->lane3_map << 9)
                | (mipi_cfg->lane2_map << 6)
                | (mipi_cfg->lane1_map << 3)
                | (mipi_cfg->lane0_map), GCSI1MAPADDR, CSI0_PIN_MAP, CSI0_BASE);
	} else {
		reg_write(MIPI_PHY_4LANE | (mipi_cfg->clk_lane_direction << 5),
			  GCSI2MAPADDR, CSI1_ANALOG_PHY, CSI1_BASE);
		reg_write(reg_data, GCSI2MAPADDR, CSI1_CTRL, CSI1_BASE);
		reg_data = reg_read(GCSI2MAPADDR, CSI1_CTRL, CSI1_BASE);

		while ((reg_data & CSI_CTRL_PHY_INIT) && (--time > 0)) {
			reg_data = reg_read(GCSI2MAPADDR, CSI1_CTRL, CSI1_BASE);
			udelay(1);
		}
		if (time <= 0)
			module_err("csi1 init time out\n");

		reg_write((mipi_cfg->contex0_virtual_num << 7)
			  | (mipi_cfg->contex0_data_type << 1)
			  | (mipi_cfg->contex0_en),
			  GCSI2MAPADDR, CSI1_CONTEXT_CFG, CSI1_BASE);
		reg_write(0xF4, GCSI2MAPADDR, CSI1_PHY_T0, CSI1_BASE);
		reg_write(mipi_cfg->clk_term_time +
			  (mipi_cfg->clk_settle_time << 4), GCSI2MAPADDR,
			  CSI1_PHY_T1, CSI1_BASE);
		reg_write(mipi_cfg->data_term_time +
			  (mipi_cfg->data_settle_time << 4), GCSI2MAPADDR,
			  CSI1_PHY_T2, CSI1_BASE);
		reg_write(0xffffffff, GCSI2MAPADDR, CSI1_ERROR_PENDING,
			  CSI1_BASE);
		reg_write(0xffff0000, GCSI2MAPADDR, CSI1_STATUS_PENDING,
			  CSI1_BASE);
		reg_write(reg_read(GCSI2MAPADDR, CSI1_CTRL, CSI1_BASE)
			  | CSI_CTRL_EN, GCSI2MAPADDR, CSI1_CTRL, CSI1_BASE);
		reg_write((mipi_cfg->lane3_map << 9)
                | (mipi_cfg->lane2_map << 6)
                | (mipi_cfg->lane1_map << 3)
                | (mipi_cfg->lane0_map), GCSI2MAPADDR, CSI1_PIN_MAP, CSI1_BASE);
	}
}

static inline void module_mipi_csi_disable(struct camera_dev *cdev,
					   struct soc_camera_device *icd)
{
	struct clk *csi_clk = cdev->csi_clk;
	struct camera_param *cam_param = icd->host_priv;

	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		csi_clk = cdev->ch_clk[HOST_MODULE_CHANNEL_0];
		reg_write(0x0, GCSI1MAPADDR, CSI0_CONTEXT_CFG, CSI0_BASE);
		reg_write(0x0, GCSI1MAPADDR, CSI0_CTRL, CSI0_BASE);
		reg_write(0x0, GCSI1MAPADDR, CSI0_ANALOG_PHY, CSI0_BASE);
		reg_write(0x0, GCSI1MAPADDR, CSI0_PHY_T0, CSI0_BASE);
		reg_write(0x0, GCSI1MAPADDR, CSI0_PHY_T1, CSI0_BASE);
		reg_write(0x0, GCSI1MAPADDR, CSI0_PHY_T2, CSI0_BASE);
	} else {
		csi_clk = cdev->ch_clk[HOST_MODULE_CHANNEL_1];
		reg_write(0x0, GCSI2MAPADDR, CSI1_CONTEXT_CFG, CSI1_BASE);
		reg_write(0x0, GCSI2MAPADDR, CSI1_CTRL, CSI1_BASE);
		reg_write(0x0, GCSI2MAPADDR, CSI1_ANALOG_PHY, CSI1_BASE);
		reg_write(0x0, GCSI2MAPADDR, CSI1_PHY_T0, CSI1_BASE);
		reg_write(0x0, GCSI2MAPADDR, CSI1_PHY_T1, CSI1_BASE);
		reg_write(0x0, GCSI2MAPADDR, CSI1_PHY_T2, CSI1_BASE);
	}
}

static inline int set_col_range(struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;

	unsigned int start = cam_param->left;
	unsigned int end = cam_param->left + cam_param->real_w - 1;

	int out_fmt;
	int width = end - start + 1;
	if ((width > MODULE_MAX_WIDTH) || (width < 32)
	    || (start >= 8192) || (end >= 8192)) {
		/*isp_err("width is over range, width:%d, start:%d, end:%d\n",
		      width, start, end);*/
		return -EINVAL;
	}

	if ((cam_param->width > 2592) &&
	    (cam_param->data_type == SENSOR_DATA_TYPE_RAW)) {
		int w2 = (cam_param->width / 2) & 0xffc0;

		cam_param->rb_cnt = 0;
		cam_param->rb_w = w2;
		cam_param->b_splited_capture = 1;
		cam_param->rb_lsub_col_size = w2 + 64;
		cam_param->rb_lright_cut = 64;
		cam_param->rb_lcol_stride = cam_param->width
		    - cam_param->rb_lsub_col_size;
		cam_param->rb_lleft_cut = 0;

		cam_param->rb_rsub_col_size = cam_param->width - w2 + 64;
		cam_param->rb_rright_cut = 0;
		cam_param->rb_rcol_stride = cam_param->width
		    - cam_param->rb_rsub_col_size;
		cam_param->rb_rleft_cut = 64;
		cam_param->rb_rows = cam_param->height;
		out_fmt = reg_read(GMODULEMAPADDR, ISP_OUT_FMT, MODULE_BASE);
		out_fmt = out_fmt & (~(0x1FFF << 16));
		out_fmt = out_fmt | ((cam_param->rb_lcol_stride
				      + cam_param->rb_lright_cut) << 16);
		reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT, MODULE_BASE);

		reg_write(400, GMODULEMAPADDR, RAW_RB_PRELINE_SET, MODULE_BASE);
		reg_write(cam_param->rb_lcol_stride
			  | (cam_param->height << 16), GMODULEMAPADDR,
			  RAW_RB_SIZE, MODULE_BASE);
		reg_write(cam_param->rb_lsub_col_size
			  | (cam_param->rb_lleft_cut << 16)
			  | (cam_param->rb_lright_cut << 24), GMODULEMAPADDR,
			  RAW_RB_COL_RANGE, MODULE_BASE);

	} else {
		cam_param->b_splited_capture = 0;
		cam_param->rb_w = 0;
	}
	/* support stride */
	/*if ((cam_param->width % 32) && (cam_param->b_splited_capture == 0)) {
	   int alinged = 32;
	   if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
	   out_fmt = isp_reg_read(ISP_OUT_FMT);
	   else
	   out_fmt = isp_reg_read(YUV_OUT_FMT);

	   out_fmt = out_fmt & (~(0x1FFF << 16));
	   out_fmt = out_fmt | (alinged << 16);
	   if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
	   reg_write(out_fmt, ISP_OUT_FMT);
	   else
	   reg_write(out_fmt, YUV_OUT_FMT);

	   end = (width & (~0x1f)) - 1;
	   } else if (((cam_param->width % 32) == 0)
	   && (cam_param->b_splited_capture == 0)) { */
	{

		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			out_fmt = reg_read(GMODULEMAPADDR, ISP_OUT_FMT,
					   MODULE_BASE);
		} else {
			out_fmt = reg_read(GMODULEMAPADDR, YUV_OUT_FMT,
					   MODULE_BASE);
		}

		out_fmt = out_fmt & (~(0x1FFF << 16));

		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
				  MODULE_BASE);
		} else {
			reg_write(out_fmt, GMODULEMAPADDR, YUV_OUT_FMT,
				  MODULE_BASE);
		}
	}

	/*if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
	   reg_write(CH1_COL_START(start)
	   | CH1_COL_END(end), CH1_COL_RANGE);
	   } else {
	   reg_write(CH2_COL_START(start)
	   | CH2_COL_END(end), CH2_COL_RANGE);
	   } */

	if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
		reg_write(CH1_COL_START(0)
			  | CH1_COL_END(end - start), GMODULEMAPADDR,
			  CH1_COL_RANGE, MODULE_BASE);
	} else {
		reg_write(CH2_COL_START(0)
			  | CH2_COL_END(end - start), GMODULEMAPADDR,
			  CH2_COL_RANGE, MODULE_BASE);
	}

	return 0;
}

static inline int set_rb_size(struct camera_param *cam_param, int bleft)
{
	if (bleft == 0) {
		reg_write(cam_param->rb_lcol_stride
			  | (cam_param->height << 16), GMODULEMAPADDR,
			  RAW_RB_SIZE, MODULE_BASE);
		reg_write(cam_param->rb_lsub_col_size
			  | (cam_param->rb_lleft_cut << 16)
			  | (cam_param->rb_lright_cut << 24), GMODULEMAPADDR,
			  RAW_RB_COL_RANGE, MODULE_BASE);
	} else {
		reg_write(cam_param->rb_rcol_stride
			  | (cam_param->height << 16), GMODULEMAPADDR,
			  RAW_RB_SIZE, MODULE_BASE);
		reg_write(cam_param->rb_rsub_col_size
			  | (cam_param->rb_rleft_cut << 16)
			  | (cam_param->rb_rright_cut << 24), GMODULEMAPADDR,
			  RAW_RB_COL_RANGE, MODULE_BASE);
	}
	return 0;
}

static inline void module_set_frame_yuv420(phys_addr_t module_addr,
					   struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	phys_addr_t module_addr_u;
	phys_addr_t module_addr_v;

	module_addr_y = ALIGN(module_addr, 2);
	module_addr_u = ALIGN(module_addr_y + icd->user_width *
			      icd->user_height, 2);
	module_addr_v = ALIGN(module_addr_u + icd->user_width *
			      icd->user_height / 4, 2);

	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		if (cam_param->b_splited_capture == 1) {
			unsigned int out_fmt = reg_read(GMODULEMAPADDR,
							ISP_OUT_FMT,
							MODULE_BASE);
			if ((cam_param->rb_cnt & 0x1) == 1) {
				module_addr_y += ((cam_param->rb_rcol_stride +
						   cam_param->rb_rleft_cut));
				module_addr_u += ((cam_param->rb_rcol_stride +
						   cam_param->rb_rleft_cut)) /
				    2;
				module_addr_v +=
				    ((cam_param->rb_rcol_stride +
				      cam_param->rb_rleft_cut)) / 2;
				out_fmt = out_fmt & (~(0x1FFF << 16));
				out_fmt = out_fmt |
				    ((cam_param->rb_rcol_stride +
				      cam_param->rb_rleft_cut) << 16);
				reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
					  MODULE_BASE);
			} else {
				out_fmt = reg_read(GMODULEMAPADDR, ISP_OUT_FMT,
						   MODULE_BASE);
				out_fmt = out_fmt & (~(0x1FFF << 16));
				out_fmt = out_fmt |
				    ((cam_param->rb_lcol_stride +
				      cam_param->rb_lright_cut) << 16);
				reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
					  MODULE_BASE);
			}
			set_rb_size(cam_param, (cam_param->rb_cnt & 0x1));

			cam_param->rb_cnt++;
			if (cam_param->rb_cnt >= 2)
				cam_param->rb_cnt = 0;
		}

		reg_write(module_addr_y, GMODULEMAPADDR, ISP_OUT_ADDRY,
			  MODULE_BASE);
		reg_write(module_addr_u, GMODULEMAPADDR, ISP_OUT_ADDRU,
			  MODULE_BASE);
		reg_write(module_addr_v, GMODULEMAPADDR, ISP_OUT_ADDRV,
			  MODULE_BASE);
	} else {
		reg_write(module_addr_y, GMODULEMAPADDR, YUV_OUT_ADDRY,
			  MODULE_BASE);
		reg_write(module_addr_u, GMODULEMAPADDR, YUV_OUT_ADDRU,
			  MODULE_BASE);
		reg_write(module_addr_v, GMODULEMAPADDR, YUV_OUT_ADDRV,
			  MODULE_BASE);
	}
}

static inline void module_set_frame_yvu420(phys_addr_t module_addr,
					   struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	phys_addr_t module_addr_u;
	phys_addr_t module_addr_v;

	module_addr_y = ALIGN(module_addr, 2);
	module_addr_u = ALIGN(module_addr_y + icd->user_width *
			      icd->user_height, 2);
	module_addr_v = ALIGN(module_addr_u + icd->user_width *
			      icd->user_height / 4, 2);

	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		if (cam_param->b_splited_capture == 1) {
			unsigned int out_fmt = reg_read(GMODULEMAPADDR,
							ISP_OUT_FMT,
							MODULE_BASE);
			if ((cam_param->rb_cnt & 0x1) == 1) {
				module_addr_y += ((cam_param->rb_rcol_stride +
						   cam_param->rb_rleft_cut));
				module_addr_u += ((cam_param->rb_rcol_stride +
						   cam_param->rb_rleft_cut)) /
				    2;
				module_addr_v +=
				    ((cam_param->rb_rcol_stride +
				      cam_param->rb_rleft_cut)) / 2;
				out_fmt = out_fmt & (~(0x1FFF << 16));
				out_fmt = out_fmt |
				    ((cam_param->rb_rcol_stride +
				      cam_param->rb_rleft_cut) << 16);
				reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
					  MODULE_BASE);
			} else {
				out_fmt = reg_read(GMODULEMAPADDR, ISP_OUT_FMT,
						   MODULE_BASE);
				out_fmt = out_fmt & (~(0x1FFF << 16));
				out_fmt = out_fmt |
				    ((cam_param->rb_lcol_stride +
				      cam_param->rb_lright_cut) << 16);
				reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
					  MODULE_BASE);
			}
			set_rb_size(cam_param, (cam_param->rb_cnt & 0x1));

			cam_param->rb_cnt++;
			if (cam_param->rb_cnt >= 2)
				cam_param->rb_cnt = 0;
		}

		reg_write(module_addr_y, GMODULEMAPADDR, ISP_OUT_ADDRY,
			  MODULE_BASE);
		reg_write(module_addr_v, GMODULEMAPADDR, ISP_OUT_ADDRU,
			  MODULE_BASE);
		reg_write(module_addr_u, GMODULEMAPADDR, ISP_OUT_ADDRV,
			  MODULE_BASE);
	} else {
		reg_write(module_addr_y, GMODULEMAPADDR, YUV_OUT_ADDRY,
			  MODULE_BASE);
		reg_write(module_addr_v, GMODULEMAPADDR, YUV_OUT_ADDRU,
			  MODULE_BASE);
		reg_write(module_addr_u, GMODULEMAPADDR, YUV_OUT_ADDRV,
			  MODULE_BASE);
	}
}

static inline void module_set_frame_yvu422(phys_addr_t module_addr,
					   struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	phys_addr_t module_addr_u;
	phys_addr_t module_addr_v;
	phys_addr_t module_addr_uv;

	module_addr_y = ALIGN(module_addr, 2);
	module_addr_uv = ALIGN(module_addr_y + icd->user_width *
			       icd->user_height, 2);

	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		if (cam_param->b_splited_capture == 1) {
			if (cam_param->b_splited_capture == 1) {
				unsigned int out_fmt = reg_read(GMODULEMAPADDR,
								ISP_OUT_FMT,
								MODULE_BASE);
				if ((cam_param->rb_cnt & 0x1) == 1) {
					module_addr_y +=
					    ((cam_param->rb_rcol_stride +
					      cam_param->rb_rleft_cut));
					module_addr_u +=
					    ((cam_param->rb_rcol_stride +
					      cam_param->rb_rleft_cut)) / 2;
					module_addr_v +=
					    ((cam_param->rb_rcol_stride +
					      cam_param->rb_rleft_cut)) / 2;
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt =
					    out_fmt |
					    ((cam_param->rb_rcol_stride +
					      cam_param->rb_rleft_cut) << 16);
					reg_write(out_fmt, GMODULEMAPADDR,
						  ISP_OUT_FMT, MODULE_BASE);
				} else {
					out_fmt = reg_read(GMODULEMAPADDR,
							   ISP_OUT_FMT,
							   MODULE_BASE);
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt =
					    out_fmt |
					    ((cam_param->rb_lcol_stride +
					      cam_param->rb_lright_cut) << 16);
					reg_write(out_fmt, GMODULEMAPADDR,
						  ISP_OUT_FMT, MODULE_BASE);
				}
				set_rb_size(cam_param, (cam_param->rb_cnt &
							0x1));

				cam_param->rb_cnt++;
				if (cam_param->rb_cnt >= 2)
					cam_param->rb_cnt = 0;
			}
			cam_param->rb_cnt++;
			if (cam_param->rb_cnt >= 2)
				cam_param->rb_cnt = 0;
		}

		reg_write(module_addr_y, GMODULEMAPADDR, ISP_OUT_ADDRY,
			  MODULE_BASE);
		reg_write(module_addr_uv, GMODULEMAPADDR, ISP_OUT_ADDRU,
			  MODULE_BASE);
	} else {
		reg_write(module_addr_y, GMODULEMAPADDR,
			  YUV_OUT_ADDRY, MODULE_BASE);
		reg_write(module_addr_uv, GMODULEMAPADDR,
			  YUV_OUT_ADDRU, MODULE_BASE);
	}
}

static inline void module_set_frame_nv12_nv21(phys_addr_t module_addr,
					      struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	phys_addr_t module_addr_uv;

	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		phys_addr_t nmodule_addr_y = ALIGN(module_addr, 2);
		unsigned int out_fmt = reg_read(GMODULEMAPADDR, ISP_OUT_FMT,
						MODULE_BASE);

		module_addr_y = nmodule_addr_y + OFFSET_OF_ALIGN *
		    (icd->user_width + OFFSET_OF_ALIGN) + OFFSET_OF_ALIGN;
		module_addr_uv = nmodule_addr_y + (OFFSET_OF_ALIGN +
						   icd->user_height) *
		    (icd->user_width + OFFSET_OF_ALIGN) +
		    OFFSET_OF_ALIGN / 2 * (icd->user_width + OFFSET_OF_ALIGN) +
		    OFFSET_OF_ALIGN;
		if (cam_param->b_splited_capture == 1) {
			if ((cam_param->rb_cnt & 0x1) == 1) {
				module_addr_y += ((cam_param->rb_rcol_stride +
						   cam_param->rb_rleft_cut));
				module_addr_uv += ((cam_param->rb_rcol_stride +
						    cam_param->rb_rleft_cut));
				out_fmt = out_fmt & (~(0x1FFF << 16));
				out_fmt =
				    out_fmt |
				    ((cam_param->rb_rcol_stride +
				      cam_param->rb_rleft_cut +
				      OFFSET_OF_ALIGN) << 16);
				reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
					  MODULE_BASE);
			} else {
				out_fmt = reg_read(GMODULEMAPADDR, ISP_OUT_FMT,
						   MODULE_BASE);
				out_fmt = out_fmt & (~(0x1FFF << 16));
				out_fmt =
				    out_fmt |
				    ((cam_param->rb_lcol_stride +
				      cam_param->rb_lright_cut +
				      OFFSET_OF_ALIGN) << 16);
				reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
					  MODULE_BASE);
			}
			set_rb_size(cam_param, (cam_param->rb_cnt & 0x1));

			cam_param->rb_cnt++;
			if (cam_param->rb_cnt >= 2)
				cam_param->rb_cnt = 0;
		} else {
			out_fmt = out_fmt & (~(0x1FFF << 16));
			out_fmt = out_fmt | (OFFSET_OF_ALIGN << 16);
			reg_write(out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
				  MODULE_BASE);
		}
		reg_write(module_addr_y, GMODULEMAPADDR, ISP_OUT_ADDRY,
			  MODULE_BASE);
		reg_write(module_addr_uv, GMODULEMAPADDR, ISP_OUT_ADDRU,
			  MODULE_BASE);
	} else {
		module_addr_y = ALIGN(module_addr, 2);
		module_addr_uv = ALIGN(module_addr_y + icd->user_width *
				       icd->user_height, 2);
		reg_write(module_addr_y, GMODULEMAPADDR,
			  YUV_OUT_ADDRY, MODULE_BASE);
		reg_write(module_addr_uv, GMODULEMAPADDR,
			  YUV_OUT_ADDRU, MODULE_BASE);
	}
}

static inline void module_set_frame_yuyv(phys_addr_t module_addr,
					 struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	module_addr_y = ALIGN(module_addr, 2);

	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		reg_write(module_addr_y, GMODULEMAPADDR, ISP_OUT_ADDRY,
			  MODULE_BASE);
	} else {
		reg_write(module_addr_y, GMODULEMAPADDR, YUV_OUT_ADDRY,
			  MODULE_BASE);
	}
}

static int updata_module_rawinfo(struct v4l2_subdev *sd,
				 struct camera_param *cam_param)
{
	int ret = 0;
	/*if(cam_param->bFlagOutBayer == 0) */
	if (SENSOR_DATA_TYPE_RAW == cam_param->data_type) {
		if (cam_param->ext_cmd) {
			cam_param->ext_cmd(sd, V4L2_CID_MODULE_UPDATERAW,
					   cam_param);
		}
	}
	return ret;
}

static inline int module_isr(struct soc_camera_device *icd,
		struct camera_param *cam_param, struct v4l2_subdev *sd,
		unsigned int module_int_stat, int i)
{
	unsigned int preline_int_pd;
	unsigned int prelien_rs_pd;
	unsigned int temp_data = 0;
	unsigned int nw = 0;
	unsigned int nh = 0;
	struct videobuf_buffer *vb;

	temp_data = reg_read(GMODULEMAPADDR, VTD_CTL, MODULE_BASE);
	nw = (temp_data >> 3) & 0x1fff;
	nh = (temp_data >> 16) & 0xffff;
	if ((module_int_stat & 0xff000000)) {
		reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
				MODULE_BASE);
		return -1;
	}

	if (nw != icd->user_width || nh != icd->user_height) {
		reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
				MODULE_BASE);
		return -1;
	}

	if (cam_param->b_raw_store_status >= 2 &&
			cam_param->b_raw_store_status <
			(2 + RAWSTORE_FRAMES_NUM)) {
		if (HOST_MODULE_CHANNEL_0 == cam_param->channel)
			preline_int_pd = MODULE_INT_STAT_CH1_PL_PD;
		else
			preline_int_pd = MODULE_INT_STAT_CH2_PL_PD;

		reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
					MODULE_BASE);

		if (module_int_stat & preline_int_pd) {
			cam_param->b_raw_store_status++;
			updata_module_rawinfo(sd, cam_param);
		}
		if (cam_param->b_raw_store_status >=
			(2 + RAWSTORE_FRAMES_NUM)) {
			int nisp_enable = reg_read(GMODULEMAPADDR,
						MODULE_ENABLE, MODULE_BASE);
			nisp_enable &= (~(1 << 4));
			if (cam_param->b_splited_capture == 1) {
				nisp_enable |= 1;
				nisp_enable |= (1 << 1);
			}
			cam_param->rb_cnt = 0;
			updata_module_info(sd, cam_param);
			cam_param->rb_cnt = 1;
			reg_write(nisp_enable, GMODULEMAPADDR,
						MODULE_ENABLE, MODULE_BASE);
		}
	} else {
		if (cam_param->b_splited_capture == 1) {
			if (HOST_MODULE_CHANNEL_0 == cam_param->channel)
				prelien_rs_pd = MODULE_INT_STAT_CH1_PL_PD;
			else
				prelien_rs_pd = MODULE_INT_STAT_CH2_PL_PD;

			reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
						MODULE_BASE);

			if (HOST_MODULE_CHANNEL_0 == cam_param->channel)
				preline_int_pd = MODULE_INT_STAT_RB_PL_PD;
			else
				preline_int_pd = MODULE_INT_STAT_RB_PL_PD;

			if (cam_param->started == DEV_STOP) {
				complete(&cam_param->wait_stop);
				if (i == 0)
					return 0;
				else
					return -1;
			}

			if (module_int_stat & preline_int_pd) {
				if ((cam_param->rb_cnt & 0x1) == 0) {
					if (cam_param->prev_frm != NULL) {
						vb = cam_param->prev_frm;
						vb->state = VIDEOBUF_DONE;
						do_gettimeofday(&vb->ts);
						vb->field_count++;
						wake_up(&vb->done);
					}

					if (!list_empty(&cam_param->capture)) {
						cam_param->prev_frm =
							cam_param->cur_frm;
						cam_param->cur_frm =
							list_entry(
							cam_param->capture.next,
							struct videobuf_buffer,
							queue);
						list_del_init(
						&cam_param->cur_frm->queue);
						updata_module_info(sd,
							cam_param);
						set_frame(icd,
							cam_param->cur_frm);
						cam_param->cur_frm->state =
							VIDEOBUF_ACTIVE;
					} else {
						cam_param->prev_frm = NULL;
						updata_module_info(sd,
							cam_param);
						set_frame(icd,
							cam_param->cur_frm);
					}
				} else {
					updata_module_info(sd, cam_param);
					set_frame(icd, cam_param->cur_frm);
				}
			}
		} else {
			reg_write(module_int_stat, GMODULEMAPADDR, MODULE_STAT,
					MODULE_BASE);

			if (HOST_MODULE_CHANNEL_0 == cam_param->channel)
				preline_int_pd = MODULE_INT_STAT_CH1_PL_PD;
			else
				preline_int_pd = MODULE_INT_STAT_CH2_PL_PD;

			if (cam_param->started == DEV_STOP) {
				complete(&cam_param->wait_stop);
				if (i == 0)
					return 0;
				else
					return -1;
			}

			if (module_int_stat & preline_int_pd) {
				if (cam_param->prev_frm != NULL) {
					vb = cam_param->prev_frm;
					vb->state = VIDEOBUF_DONE;
					do_gettimeofday(&vb->ts);
					vb->field_count++;
					wake_up(&vb->done);
				}

				if (!list_empty(&cam_param->capture)) {
					cam_param->prev_frm =
						cam_param->cur_frm;
					cam_param->cur_frm =
						list_entry(
						cam_param->capture.next,
						struct videobuf_buffer,
						queue);
					list_del_init(
						&cam_param->cur_frm->queue);

					updata_module_info(sd, cam_param);
					set_frame(icd, cam_param->cur_frm);
					cam_param->cur_frm->state =
						VIDEOBUF_ACTIVE;
					BUG_ON(cam_param->prev_frm ==
						cam_param->cur_frm);
				} else {
					cam_param->prev_frm = NULL;
					updata_module_info(sd, cam_param);
					set_frame(icd, cam_param->cur_frm);
				}
			}
		}
	}

	return 0;
}

static inline void module_camera_suspend(struct device *dev)
{
	reg_write(reg_read(GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE) | (~1),
		  GMODULEMAPADDR, MODULE_ENABLE, MODULE_BASE);
}

static inline void module_camera_resume(struct device *dev,
					struct camera_dev *cam_dev)
{
	cam_dev->sensor_clk->parent = NULL;
	cam_dev->sensor_clk->rate = 0;

	clk_set_parent(cam_dev->sensor_clk,
		       cam_dev->sensor_clk_parent[HOST_MODULE_CHANNEL_0]);
	clk_set_parent(cam_dev->ch_clk[HOST_MODULE_CHANNEL_0],
		       cam_dev->csi_clk_parent);
	clk_set_parent(cam_dev->ch_clk[HOST_MODULE_CHANNEL_1],
		       cam_dev->csi_clk_parent);
}

static inline void module_set_output_fmt(struct soc_camera_device *icd,
					 u32 fourcc)
{
	struct camera_param *cam_param = icd->host_priv;
	unsigned int module_out_fmt;

	module_out_fmt = reg_read(GMODULEMAPADDR, YUV_OUT_FMT, MODULE_BASE);
	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		module_out_fmt = reg_read(GMODULEMAPADDR, ISP_OUT_FMT,
					  MODULE_BASE);
	}

	module_out_fmt &= ~(MODULE_OUT_FMT_MASK | MODULE_OUT_FMT_SEMI_UV_INV);
	get_fmt(fourcc, &module_out_fmt);
	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		reg_write(module_out_fmt, GMODULEMAPADDR, ISP_OUT_FMT,
			  MODULE_BASE);
	} else {
		reg_write(module_out_fmt, GMODULEMAPADDR, YUV_OUT_FMT,
			  MODULE_BASE);
	}
}

static int updata_module_info(struct v4l2_subdev *sd,
			      struct camera_param *cam_param)
{
	int ret = 0;
	/*if(cam_param->bFlagOutBayer == 0) */
	if (SENSOR_DATA_TYPE_RAW == cam_param->data_type) {
		if (cam_param->ext_cmd) {
			cam_param->ext_cmd(sd, V4L2_CID_MODULE_UPDATE,
					   cam_param);
		}
	}
	return ret;
}

static int updata_get_moduleinfo(struct v4l2_subdev *sd,
				 struct camera_param *cam_param)
{
	int ret = 0;
	/*if(cam_param->bFlagOutBayer == 0) */
	if (SENSOR_DATA_TYPE_RAW == cam_param->data_type) {
		if (cam_param->ext_cmd) {
			cam_param->ext_cmd(sd, V4L2_CID_MODULE_GETINFO,
					   (void *)cam_param);
		}
	}
	return ret;
}

static int raw_store_set(struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	int ret = 0;
	int start = 0;
	int end = 0;

	int nisp_enable = 0;
	int nch_preline;
	int module_bk_int_stat;

	updata_get_moduleinfo(sd, cam_param);
	if ((cam_param->b_raw_store_status == 0x1) ||
	    cam_param->b_raw_store_status == 0x2) {
		if (HOST_MODULE_CHANNEL_0 == cam_param->channel) {
			start = 0;
			end = cam_param->real_h - 1;
			reg_write(CH1_ROW_START(0)
				  | CH1_ROW_END(end - start), GMODULEMAPADDR,
				  CH1_ROW_RANGE, MODULE_BASE);
			start = 0;
			end = cam_param->real_w - 1;
			reg_write(CH1_COL_START(0)
				  | CH1_COL_END(end - start), GMODULEMAPADDR,
				  CH1_COL_RANGE, MODULE_BASE);

			/* waiting for raw-store finished */
			nisp_enable = reg_read(GMODULEMAPADDR, MODULE_ENABLE,
					       MODULE_BASE);
			nisp_enable &= 0xfffffffe;	/*disable now */

			nch_preline = reg_read(GMODULEMAPADDR, CH_PRELINE_SET,
					       MODULE_BASE);
			nch_preline &= 0xffff0000;
			nch_preline |= (cam_param->real_h - 200);
			reg_write(nch_preline, GMODULEMAPADDR, CH_PRELINE_SET,
				  MODULE_BASE);
			module_bk_int_stat = reg_read(GMODULEMAPADDR,
						      MODULE_STAT, MODULE_BASE);
			module_bk_int_stat |= 1;
			reg_write(module_bk_int_stat, GMODULEMAPADDR,
				  MODULE_STAT, MODULE_BASE);
			updata_module_rawinfo(sd, cam_param);
			nisp_enable &= (~(1 << 4));
			reg_write(nisp_enable, GMODULEMAPADDR, MODULE_ENABLE,
				  MODULE_BASE);
		} else {
			start = 0;
			end = cam_param->real_h - 1;
			reg_write(CH2_ROW_START(0)
				  | CH2_ROW_END(end - start), GMODULEMAPADDR,
				  CH2_ROW_RANGE, MODULE_BASE);
			start = 0;
			end = cam_param->real_w - 1;
			reg_write(CH2_COL_START(0)
				  | CH2_COL_END(end - start), GMODULEMAPADDR,
				  CH2_COL_RANGE, MODULE_BASE);

			/* waiting for raw-store finished */
			nisp_enable = reg_read(GMODULEMAPADDR, MODULE_ENABLE,
					       MODULE_BASE);
			/*disable now */
			nisp_enable &= 0xfffffffe;
			nch_preline = reg_read(GMODULEMAPADDR, CH_PRELINE_SET,
					       MODULE_BASE);
			nch_preline &= 0xffff;
			nch_preline |= ((cam_param->real_h - 200) << 16);
			reg_write(nch_preline, GMODULEMAPADDR, CH_PRELINE_SET,
				  MODULE_BASE);
			module_bk_int_stat = reg_read(GMODULEMAPADDR,
						      MODULE_STAT, MODULE_BASE);
			module_bk_int_stat |= 1;
			reg_write(module_bk_int_stat, GMODULEMAPADDR,
				  MODULE_STAT, MODULE_BASE);
			updata_module_rawinfo(sd, cam_param);
			nisp_enable &= (~(1 << 4));
			reg_write(nisp_enable, GMODULEMAPADDR, MODULE_ENABLE,
				  MODULE_BASE);
		}
	}

	return ret;
}

static void module_regs_init(void)
{
	/*
	   channel 0 dpc and ee
	   isp_reg_write(0xff00e4, BUS_PRIORITY_CTL); */
	reg_write(0, GMODULEMAPADDR, ISP_CTL, MODULE_BASE);

	reg_write(0xFF01FF01, GMODULEMAPADDR, BA_OFFSET0, MODULE_BASE);
	reg_write(0xFF01FF01, GMODULEMAPADDR, BA_OFFSET1, MODULE_BASE);

	reg_write(0x00000100, GMODULEMAPADDR, ISP_CG_B_GAIN, MODULE_BASE);
	reg_write(0x00000100, GMODULEMAPADDR, ISP_CG_G_GAIN, MODULE_BASE);
	reg_write(0x00000100, GMODULEMAPADDR, ISP_CG_R_GAIN, MODULE_BASE);
	reg_write(0x00000100, GMODULEMAPADDR, ISP_SECOND_CG_B_GAIN,
		  MODULE_BASE);
	reg_write(0x00000100, GMODULEMAPADDR, ISP_SECOND_CG_G_GAIN,
		  MODULE_BASE);
	reg_write(0x00000100, GMODULEMAPADDR, ISP_SECOND_CG_R_GAIN,
		  MODULE_BASE);

	reg_write(0x00000400, GMODULEMAPADDR, ISP_CC_CMA1, MODULE_BASE);
	reg_write(0x00000000, GMODULEMAPADDR, ISP_CC_CMA2, MODULE_BASE);
	reg_write(0x00000400, GMODULEMAPADDR, ISP_CC_CMA3, MODULE_BASE);
	reg_write(0x00000000, GMODULEMAPADDR, ISP_CC_CMA4, MODULE_BASE);
	reg_write(0x00000400, GMODULEMAPADDR, ISP_CC_CMA5, MODULE_BASE);

	/*channel 0 csc setting */
	reg_write(0x00000010, GMODULEMAPADDR, ISP_CSC_Y_OFFSET, MODULE_BASE);
	reg_write(0x00000080, GMODULEMAPADDR, ISP_CSC_CB_OFFSET, MODULE_BASE);
	reg_write(0x00000080, GMODULEMAPADDR, ISP_CSC_CR_OFFSET, MODULE_BASE);
	reg_write(0x00000042, GMODULEMAPADDR, ISP_CSC_Y_R, MODULE_BASE);
	reg_write(0x00000081, GMODULEMAPADDR, ISP_CSC_Y_G, MODULE_BASE);
	reg_write(0x00000019, GMODULEMAPADDR, ISP_CSC_Y_B, MODULE_BASE);
	reg_write(0x000007DA, GMODULEMAPADDR, ISP_CSC_CB_R, MODULE_BASE);
	reg_write(0x000007B6, GMODULEMAPADDR, ISP_CSC_CB_G, MODULE_BASE);
	reg_write(0x00000070, GMODULEMAPADDR, ISP_CSC_CB_B, MODULE_BASE);
	reg_write(0x00000070, GMODULEMAPADDR, ISP_CSC_CR_R, MODULE_BASE);
	reg_write(0x000007A2, GMODULEMAPADDR, ISP_CSC_CR_G, MODULE_BASE);
	reg_write(0x000007EE, GMODULEMAPADDR, ISP_CSC_CR_B, MODULE_BASE);
	reg_write(0x00000000, GMODULEMAPADDR, ISP_CSC_CONTROL, MODULE_BASE);
}

enum bisp_stat_flag_t {
	UNUSED,
	FREED,
	PREPARED,
	ACTIVED,
	READY,
	USED,
};

struct bisp_buffer_node_t {
	phys_addr_t phy_addr;
	void *pvir_addr;
	int32 mstate;
	int16 pred;
	int16 next;
};

enum bisp_rb_flag_t {
	BISP_BUF_FREED,
	BISP_BUF_USED,
};

struct bisp_rb_node_t {
	phys_addr_t phy_addr;
	int16 nflag;		/*bisp_rb_flag_t */
};

struct bisp_internal_t {
	int32 ch_id;
	int32 open_count;

	/*stat buffers manage */
	int32 total_node_num;
	int32 ready_num;
	int32 free_idx;
	int32 read_idx;
	struct bisp_buffer_node_t buffer_queue[16];

	/*raw store & read back buffers manage */
	int nrs_idx;
	int nrb_idx;
	uint32 nskip_frm;
	uint32 nraw_num;
	struct bisp_rb_node_t nrb_addr[8];
	int brs_en;

	/*lsc */
	struct bisp_clsc_t blsc;

	/*autofocus manage */
	int32 af_enabled;
	struct bisp_af_region_t af_region;
	struct bisp_af_param_t af_param;
};

struct stat_updata_t {
	phys_addr_t out_addr;
	phys_addr_t stat_addr;
	uint32 rgain;
	uint32 ggain;
	uint32 bgain;
	uint32 r2gain;
	uint32 g2gain;
	uint32 b2gain;
};

#define DEVDRV_NAME_BISP      "bisp"
#define DEVICE_BISP           "/dev/bisp"
#define BISP_FRONT_SENSOR 0
#define BISP_REAR_SENSOR  1
#define _CH_MX_NUM_ 64

static unsigned int bisp_drv_opened;
static uint32 af_enable_count;
static struct stat_updata_t gstat_data[_CH_MX_NUM_][3];
static struct bisp_internal_t *gbisp_fifo[_CH_MX_NUM_];

static DEFINE_MUTEX(gisp_cmd_mutex);
static struct module_name_t module_name;
static void *g_iomap_base;

static void *bisp_malloc(uint32 size)
{
	return kzalloc(size, GFP_KERNEL | GFP_DMA);
}

static void bisp_free(void *ptr)
{
	kfree(ptr);
	ptr = NULL;
	return;
}

static int finder_ch(int ch_id)
{
	return 0;
}

static uint32 bisp_reg_read(uint32 reg)
{
	unsigned int value =
	    readl_relaxed((uint8 *) g_iomap_base + reg - MODULE_BASE);
	return value;
}

static void bisp_reg_write(uint32 reg, uint32 value)
{
	/*printk("write bisp regs(%x)=>(%x)\n", reg, value); */
	writel_relaxed(value, (uint8 *) g_iomap_base + reg - MODULE_BASE);
}

static int bisp_enable(struct bisp_internal_t *bisp_fifo, unsigned int ben)
{
	uint32 isp_en = bisp_reg_read(MODULE_ENABLE);
	isp_en = (isp_en | (ben & 0x1));
	bisp_reg_write(MODULE_ENABLE, isp_en);
	return 0;
}

static int bisp_set_blc(struct bisp_internal_t *bisp_fifo,
			struct bisp_blc_t *blc_info)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	uint32 ngrgb, nrb;
	if ((blc_info->ben & 0x1) == 1) {
		nisp_ctl |= (0x1 << 5);
		ngrgb = (blc_info->ngr_offset << 16) + blc_info->ngb_offset;
		nrb = (blc_info->nr_offset << 16) + blc_info->nb_offset;
		bisp_reg_write(BA_OFFSET0, ngrgb);
		bisp_reg_write(BA_OFFSET1, nrb);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 5));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}
	return 0;
}

static int bisp_set_dpc(struct bisp_internal_t *bisp_fifo,
			struct bisp_dpc_t *isp_nr)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);

	if (isp_nr->bd_en) {
		/*printk("set_dpc %x,%x\n", isp_nr->nd_th, isp_nr->nd_num); */
		bisp_reg_write(ISP_DPC_THRESHOLD,
			       (isp_nr->nd_th & 0xfff) | (isp_nr->
							  nd_num << 16));
		/*printk("read_dpc %x\n", bisp_reg_read(ISP_DPC_THRESHOLD)); */
		nisp_ctl = nisp_ctl | (0x1 << 7);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 7));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}

	return 0;
}

static int bisp_set_dpc_nr(struct bisp_internal_t *info,
			   struct bisp_dpc_t *nr_info)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);

	if (nr_info->bnr_en) {
		bisp_reg_write(ISP_DPC_NR_THRESHOLD,
			       (nr_info->bnr_th1 & 0xfff) | (nr_info->
							     bnr_th2 << 16));
		nisp_ctl = nisp_ctl | (0x1 << 17);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 17));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}

	return 0;
}

static int bisp_set_stat_crop(struct bisp_internal_t *bisp_fifo,
			      struct bisp_region_t *isp_bar, int mode)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	uint32 nsta_ctl = bisp_reg_read(STAT_CTL);
	bisp_reg_write(STAT_CTL, nsta_ctl | 0x1);
	if (mode == 0) {
		/*the first statistics' crop */
		bisp_reg_write(ISP_STAT_REGION_Y,
			       isp_bar->nsy + (isp_bar->nwh << 16));
		bisp_reg_write(ISP_STAT_REGION_X,
			       isp_bar->nsx + (isp_bar->nww << 16));
	} else {
		/*the second statistics' crop */
		bisp_reg_write(ISP_Y_REGION_Y,
			       isp_bar->nsy + (isp_bar->nwh << 16));
		bisp_reg_write(ISP_Y_REGION_X,
			       isp_bar->nsx + (isp_bar->nww << 16));
	}
	bisp_reg_write(STAT_CTL, (isp_bar->nidx << 16) & 0xff0000);
	bisp_reg_write(ISP_CTL, nisp_ctl | (1 << 8));

	return 0;
}

static int bisp_set_lsc(struct bisp_internal_t *bisp_fifo,
			struct bisp_clsc_t *lut_data)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);

	if (lut_data->blsc_en) {
		bisp_reg_write(ISP_LSC_SETTING, lut_data->nbw);
		bisp_reg_write(ISP_LSC_REG_ADR, lut_data->ncoeff_addr);
		nisp_ctl = nisp_ctl | (0x1 << 3);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 3));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}

	return 0;
}

static int bisp_set_lsc_split(struct bisp_internal_t *bisp_fifo,
			      struct bisp_clsc_t *lut_data)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);

	if (lut_data->blsc_en) {
		bisp_reg_write(ISP_LSC_SETTING, lut_data->nbw);
		bisp_reg_write(ISP_LSC_REG_ADR, lut_data->ncoeff_addr_next);
		nisp_ctl = nisp_ctl | (0x1 << 3);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 3));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}

	return 0;
}

static int bisp_set_bpi_thd(struct bisp_internal_t *bisp_fifo, unsigned int thd)
{
	bisp_reg_write(ISP_BPI_NR_THRESHOLD, thd & 0x1ffff);
	return 0;
}

static int bisp_set_temp(struct bisp_internal_t *info,
			 struct bisp_temp_t *pinfo)
{
	uint32 nsta_ctl = bisp_reg_read(STAT_CTL);
	bisp_reg_write(STAT_CTL, nsta_ctl | 0x1);

	bisp_reg_write(ISP_COLOR_TEMPERATURE0_YR, pinfo->ncoeff[0]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE0_XSE, pinfo->ncoeff[1]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE0_YSE, pinfo->ncoeff[2]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE1_YR, pinfo->ncoeff[3]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE1_XSE, pinfo->ncoeff[4]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE1_YSE, pinfo->ncoeff[5]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE2_YR, pinfo->ncoeff[6]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE2_XSE, pinfo->ncoeff[7]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE2_YSE, pinfo->ncoeff[8]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE3_YR, pinfo->ncoeff[9]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE3_XSE, pinfo->ncoeff[10]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE3_YSE, pinfo->ncoeff[11]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE4_YR, pinfo->ncoeff[12]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE4_XSE, pinfo->ncoeff[13]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE4_YSE, pinfo->ncoeff[14]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE5_YR, pinfo->ncoeff[15]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE5_XSE, pinfo->ncoeff[16]);
	bisp_reg_write(ISP_COLOR_TEMPERATURE5_YSE, pinfo->ncoeff[17]);

	bisp_reg_write(STAT_CTL, (pinfo->nidx << 16) & 0xff0000);
	return 0;
}

static int bisp_set_wps(struct bisp_internal_t *info,
			struct bisp_wps_t *pinfo, int mode)
{
	uint32 nsta_ctl = bisp_reg_read(STAT_CTL);
	bisp_reg_write(STAT_CTL, nsta_ctl | 0x1);
	if (mode == 0) {
		bisp_reg_write(ISP_WB_PRE_THROD_0_0, pinfo->ncoeff[0]);
		bisp_reg_write(ISP_WB_PRE_THROD_0_1, pinfo->ncoeff[1]);
		bisp_reg_write(ISP_WB_PRE_THROD_0_2, pinfo->ncoeff[2]);
		bisp_reg_write(ISP_WB_PRE_THROD_0_3, pinfo->ncoeff[3]);
		bisp_reg_write(ISP_WB_PRE_THROD_1_0, pinfo->ncoeff[4]);
		bisp_reg_write(ISP_WB_PRE_THROD_1_1, pinfo->ncoeff[5]);
		bisp_reg_write(ISP_WB_PRE_THROD_1_2, pinfo->ncoeff[6]);
		bisp_reg_write(ISP_WB_PRE_THROD_1_3, pinfo->ncoeff[7]);
		bisp_reg_write(ISP_WB_PRE_THROD_2_0, pinfo->ncoeff[8]);
		bisp_reg_write(ISP_WB_PRE_THROD_2_1, pinfo->ncoeff[9]);
		bisp_reg_write(ISP_WB_PRE_THROD_2_2, pinfo->ncoeff[10]);
		bisp_reg_write(ISP_WB_PRE_THROD_2_3, pinfo->ncoeff[11]);
		bisp_reg_write(ISP_WB_PRE_THROD_3_0, pinfo->ncoeff[12]);
		bisp_reg_write(ISP_WB_PRE_THROD_3_1, pinfo->ncoeff[13]);
		bisp_reg_write(ISP_WB_PRE_THROD_3_2, pinfo->ncoeff[14]);
		bisp_reg_write(ISP_WB_PRE_THROD_3_3, pinfo->ncoeff[15]);
	} else {
		bisp_reg_write(ISP_WB_B_THROD_0_0, pinfo->ncoeff[0]);
		bisp_reg_write(ISP_WB_B_THROD_0_1, pinfo->ncoeff[1]);
		bisp_reg_write(ISP_WB_B_THROD_1_0, pinfo->ncoeff[2]);
		bisp_reg_write(ISP_WB_B_THROD_1_1, pinfo->ncoeff[3]);
		bisp_reg_write(ISP_WB_B_THROD_2_0, pinfo->ncoeff[4]);
		bisp_reg_write(ISP_WB_B_THROD_2_1, pinfo->ncoeff[5]);
		bisp_reg_write(ISP_WB_B_THROD_3_0, pinfo->ncoeff[6]);
		bisp_reg_write(ISP_WB_B_THROD_3_1, pinfo->ncoeff[7]);
	}
	bisp_reg_write(STAT_CTL, (pinfo->nidx << 16) & 0xff0000);
	return 0;
}

static int bisp_set_wbgain(struct bisp_internal_t *info,
			   struct bisp_gain_t *pinfo, int mode)
{
	uint32 nsta_ctl = bisp_reg_read(STAT_CTL);
	bisp_reg_write(STAT_CTL, nsta_ctl | 0x1);
	if (mode == 0) {
		bisp_reg_write(ISP_CG_R_GAIN, pinfo->nrgain);
		bisp_reg_write(ISP_CG_G_GAIN, pinfo->nggain);
		bisp_reg_write(ISP_CG_B_GAIN, pinfo->nbgain);
	} else {
		bisp_reg_write(ISP_SECOND_CG_R_GAIN, pinfo->nrgain);
		bisp_reg_write(ISP_SECOND_CG_G_GAIN, pinfo->nggain);
		bisp_reg_write(ISP_SECOND_CG_B_GAIN, pinfo->nbgain);
	}
	bisp_reg_write(STAT_CTL, (pinfo->nidx << 16) & 0xff0000);

	return 0;
}

static int bisp_set_rgbgamma(struct bisp_internal_t *bisp_fifo,
			     struct bisp_rgb_gamma_t *rgbgamma_info)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	uint8 i, j;
	unsigned int rgb_gamma_val = 0;
	if (rgbgamma_info->ben) {
		bisp_reg_write(RGB_GC_LUT_RST, 0x1);
		for (j = 0; j < 3; j++) {
			for (i = 0; i < 32; i++) {
				rgb_gamma_val =
				    rgbgamma_info->rgb_gamma[j][2 * i] |
				    (rgbgamma_info->
				     rgb_gamma[j][2 * i + 1] << 16);

				/*printk("rgbgamma %x\n",rgb_gamma_val); */
				bisp_reg_write(RGB_GC_COEFF, rgb_gamma_val);
			}
		}
		nisp_ctl = nisp_ctl | (0x1 << 10);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 10));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}

	return 0;
}

static int bisp_set_mcc(struct bisp_internal_t *info, struct bisp_mcc_t *pinfo)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	if (pinfo->ben) {
		bisp_reg_write(ISP_CC_CMA1, pinfo->mcc[0]);
		bisp_reg_write(ISP_CC_CMA2, pinfo->mcc[1]);
		bisp_reg_write(ISP_CC_CMA3, pinfo->mcc[2]);
		bisp_reg_write(ISP_CC_CMA4, pinfo->mcc[3]);
		bisp_reg_write(ISP_CC_CMA5, pinfo->mcc[4]);
		nisp_ctl = nisp_ctl | (0x1 << 13);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 13));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}
	return 0;
}

static int bisp_set_csc(struct bisp_internal_t *info,
			struct bisp_csc_t *pinfo, int mode)
{
	uint32 nsta_ctl = bisp_reg_read(STAT_CTL);
	if (mode == 0) {
		bisp_reg_write(STAT_CTL, nsta_ctl | 0x1);
		bisp_reg_write(ISP_STAT_CSC_Y_OFFSET, pinfo->ny_off);
		bisp_reg_write(ISP_STAT_CSC_CB_OFFSET, pinfo->ncb_off);
		bisp_reg_write(ISP_STAT_CSC_CR_OFFSET, pinfo->ncr_off);
		bisp_reg_write(ISP_STAT_CSC_Y_R, pinfo->nyr);
		bisp_reg_write(ISP_STAT_CSC_Y_G, pinfo->nyg);
		bisp_reg_write(ISP_STAT_CSC_Y_B, pinfo->nyb);
		bisp_reg_write(ISP_STAT_CSC_CB_R, pinfo->nur);
		bisp_reg_write(ISP_STAT_CSC_CB_G, pinfo->nug);
		bisp_reg_write(ISP_STAT_CSC_CB_B, pinfo->nub);
		bisp_reg_write(ISP_STAT_CSC_CR_R, pinfo->nvr);
		bisp_reg_write(ISP_STAT_CSC_CR_G, pinfo->nvg);
		bisp_reg_write(ISP_STAT_CSC_CR_B, pinfo->nvb);
		bisp_reg_write(STAT_CTL, (pinfo->nidx << 16) & 0xff0000);
	} else {
		bisp_reg_write(ISP_CSC_Y_OFFSET, pinfo->ny_off);
		bisp_reg_write(ISP_CSC_CB_OFFSET, pinfo->ncb_off);
		bisp_reg_write(ISP_CSC_CR_OFFSET, pinfo->ncr_off);
		bisp_reg_write(ISP_CSC_Y_R, pinfo->nyr);
		bisp_reg_write(ISP_CSC_Y_G, pinfo->nyg);
		bisp_reg_write(ISP_CSC_Y_B, pinfo->nyb);
		bisp_reg_write(ISP_CSC_CB_R, pinfo->nur);
		bisp_reg_write(ISP_CSC_CB_G, pinfo->nug);
		bisp_reg_write(ISP_CSC_CB_B, pinfo->nub);
		bisp_reg_write(ISP_CSC_CR_R, pinfo->nvr);
		bisp_reg_write(ISP_CSC_CR_G, pinfo->nvg);
		bisp_reg_write(ISP_CSC_CR_B, pinfo->nvb);
	}
	return 0;
}

static int bisp_set_bc(struct bisp_internal_t *info, struct bisp_bc_t *pinfo)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	uint16 i;

	if (pinfo->ben) {
		bisp_reg_write(BC_YGC_LUT_RST, 0x1);
		for (i = 0; i < 64; i++)
			bisp_reg_write(BC_YGC_COEFF, pinfo->ngtbl[i]);

		bisp_reg_write(LY_LUT_RST, pinfo->noffset << 16);

		bisp_reg_write(LY_LUT_RST, bisp_reg_read(LY_LUT_RST) | 0x1);
		for (i = 0; i < 128; i++)
			bisp_reg_write(LY_COEFF, pinfo->nutbl[i]);

		nisp_ctl = nisp_ctl | (0x1 << 9);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 9));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}
	return 0;
}

static int bisp_set_fcs(struct bisp_internal_t *info, struct bisp_fcs_t *pinfo)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);

	if (pinfo->ben) {
		uint16 i;
		bisp_reg_write(FCS_LW_GAIN_RST, 0x1);
		for (i = 0; i < 64; i++)
			bisp_reg_write(FCS_LW_GAIN_LUT, pinfo->nutbl[i]);

		nisp_ctl = nisp_ctl | (0x1 << 15);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 15));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}
	return 0;
}

static int bisp_set_ccs(struct bisp_internal_t *info, struct bisp_ccs_t *pinfo)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);

	if (pinfo->ben) {
		bisp_reg_write(CCS_CTL, pinfo->nyt1 | (pinfo->nct1 << 8) |
			       (pinfo->net << 16) | (pinfo->nct2 << 28));

		nisp_ctl = nisp_ctl | (0x1 << 16);
		bisp_reg_write(ISP_CTL, nisp_ctl);
	} else {
		nisp_ctl = nisp_ctl & (~(0x1 << 16));
		bisp_reg_write(ISP_CTL, nisp_ctl);
	}
	return 0;
}

/*set:bri&sta&con&hue*/
static int bisp_set_bsch(struct bisp_internal_t *info,
			 struct bisp_bsch_t *pinfo, int mode)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	if (mode == 0) {
		bisp_reg_write(ISP_BRIGHTNESS_SETTING, pinfo->bri_level);
	} else if (mode == 1) {
		if (pinfo->sta_level != 256) {
			bisp_reg_write(ISP_SATURATION_SETTING,
				       pinfo->sta_level);
			nisp_ctl = nisp_ctl | (0x1 << 11);
			bisp_reg_write(ISP_CTL, nisp_ctl);	/*enable */
		} else {
			nisp_ctl = nisp_ctl & (~(0x1 << 11));
			bisp_reg_write(ISP_CTL, nisp_ctl);	/*disable */
		}
	} else if (mode == 2) {
		bisp_reg_write(ISP_CONTRAST_SETTING,
			       pinfo->con_level | (pinfo->con_offset << 8));
	} else {
		if (pinfo->hue_level0 != 0 && pinfo->hue_level1 != 1) {
			bisp_reg_write(ISP_HUE_SETTING, pinfo->hue_level0 |
				       (pinfo->hue_level1 << 16));

			nisp_ctl = nisp_ctl | (0x1 << 12);
			bisp_reg_write(ISP_CTL, nisp_ctl);	/*enable */
		} else {
			nisp_ctl = nisp_ctl & (~(0x1 << 12));
			bisp_reg_write(ISP_CTL, nisp_ctl);	/*disable */
		}
	}
	return 0;
}

static int bisp_set_cfx(struct bisp_internal_t *info,
			struct bisp_cfixed_t *cr_data)
{
	bisp_reg_write(ISP_CSC_CONTROL, (cr_data->ncr_val << 24) |
		       (cr_data->
			ncb_val << 16) | ((cr_data->
					   ncr_fixed & 0x1) << 2) | ((cr_data->
								      ncb_fixed
								      & 0x1) <<
								     1) |
		       (cr_data->ny_invert & 0x1));
	return 0;
}

static int bisp_get_raw_crop(struct bisp_internal_t *bisp_fifo,
			     struct bisp_region_t *isp_bar)
{
	uint32 nch_id = bisp_fifo->ch_id;
	uint32 nw, nh;
	if (nch_id == 0) {
		nh = bisp_reg_read(CH1_ROW_RANGE);
		nw = bisp_reg_read(CH1_ROW_RANGE);
	} else {
		nh = bisp_reg_read(CH2_ROW_RANGE);
		nw = bisp_reg_read(CH2_ROW_RANGE);
	}

	isp_bar->nsx = nw & 0xffff;
	isp_bar->nww = (nw >> 16) & 0xffff;
	isp_bar->nsy = nh & 0xffff;
	isp_bar->nwh = (nh >> 16) & 0xffff;
	return 0;
}

static void bisp_get_phy_info(struct bisp_internal_t *bisp_fifo,
			      struct bisp_ads_t *lut_data)
{
	lut_data->sphy_addr = gstat_data[bisp_fifo->ch_id][0].stat_addr;
	lut_data->yphy_addr = gstat_data[bisp_fifo->ch_id][0].out_addr;
}

static void bisp_get_all_info(struct bisp_internal_t *bisp_fifo,
			      struct bisp_frame_info_t *lut_data)
{
	lut_data->ngain.nrgain = gstat_data[bisp_fifo->ch_id][0].rgain;
	lut_data->ngain.nggain = gstat_data[bisp_fifo->ch_id][0].ggain;
	lut_data->ngain.nbgain = gstat_data[bisp_fifo->ch_id][0].bgain;
	lut_data->ngain2.nrgain = gstat_data[bisp_fifo->ch_id][0].r2gain;
	lut_data->ngain2.nggain = gstat_data[bisp_fifo->ch_id][0].g2gain;
	lut_data->ngain2.nbgain = gstat_data[bisp_fifo->ch_id][0].b2gain;
	lut_data->nfrm.sphy_addr = gstat_data[bisp_fifo->ch_id][0].stat_addr;
	lut_data->nfrm.yphy_addr = gstat_data[bisp_fifo->ch_id][0].out_addr;
}

static void bisp_get_gain(struct bisp_internal_t *bisp_fifo,
			  struct bisp_gain_t *lut_data, int mode)
{
	if (mode == 0) {
		lut_data->nrgain = gstat_data[bisp_fifo->ch_id][0].rgain;
		lut_data->nggain = gstat_data[bisp_fifo->ch_id][0].ggain;
		lut_data->nbgain = gstat_data[bisp_fifo->ch_id][0].bgain;
	} else {
		lut_data->nrgain = gstat_data[bisp_fifo->ch_id][0].r2gain;
		lut_data->nggain = gstat_data[bisp_fifo->ch_id][0].g2gain;
		lut_data->nbgain = gstat_data[bisp_fifo->ch_id][0].b2gain;
	}
}

static int bisp_get_bv(struct bisp_internal_t *bisp_fifo,
		       struct bisp_blc_t *blc_info)
{
	uint32 ngrgb, nrb;
	ngrgb = bisp_reg_read(BV_DATA0);
	nrb = bisp_reg_read(BV_DATA1);
	blc_info->ngr_offset = ngrgb >> 16;
	blc_info->ngb_offset = ngrgb & 0xffff;
	blc_info->nr_offset = nrb >> 16;
	blc_info->nb_offset = nrb & 0xffff;
	return 0;
}

static void bisp_af_enable(void)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	bisp_reg_write(ISP_CTL, nisp_ctl | (0x1 << 4));
}

static void bisp_af_disable(void)
{
	uint32 nisp_ctl = bisp_reg_read(ISP_CTL);
	bisp_reg_write(ISP_CTL, nisp_ctl & (~(0x1 << 4)));
}

static inline void bisp_af_irq_enable(void)
{
	int af_status = bisp_reg_read(MODULE_STAT);
	af_status = af_status & 0xff;
	af_status = af_status | (0x1 << 3);
	bisp_reg_write(MODULE_STAT, af_status);
}

static inline void bisp_af_irq_disable(void)
{
	int af_status = bisp_reg_read(MODULE_STAT);
	af_status = af_status & 0xff;
	af_status = af_status & (~(0x1 << 3));
	bisp_reg_write(MODULE_STAT, af_status);
}

static void bisp_af_param_set(struct bisp_internal_t *info,
			      struct bisp_af_param_t *af_param)
{
	int af_ctl = bisp_reg_read(AF_CTRL);
	af_ctl = af_ctl & 0x303;
	af_ctl = af_ctl | (af_param->af_al << 2) |
	    (af_param->tshift << 3) | (af_param->nr_thold << 16);
	bisp_reg_write(AF_CTRL, af_ctl);
}

static void bisp_af_setwin(struct bisp_internal_t *info,
			   struct bisp_af_region_t *af_param)
{
	int af_ctl = bisp_reg_read(AF_CTRL);
	af_ctl = af_ctl & 0xfffffcfc;
	af_ctl = af_ctl | (af_param->af_inc << 8)
	    | (af_param->win_size << 0);
	bisp_reg_write(AF_CTRL, af_ctl);
	bisp_reg_write(AF_SPACE, af_param->skip_wx | (af_param->skip_wy << 16));
	bisp_reg_write(AF_WP, af_param->start_x | (af_param->start_y << 16));
}

/*init stat buffers*/
static void bisp_init_node(struct bisp_internal_t *bisp_fifo,
			   struct bisp_stat_buffers_info_t *buffers_node)
{
	int i;
	/*memset(bisp_fifo,0,sizeof(struct bisp_internal_t)); */
	bisp_fifo->total_node_num = 0;
	bisp_fifo->ready_num = 0;
	bisp_fifo->free_idx = 0;
	bisp_fifo->read_idx = 0;
	memset(bisp_fifo->buffer_queue, 0,
	       sizeof(struct bisp_buffer_node_t) * 16);

	/*printk("init_node %d\n", buffers_node->buffer_num); */
	for (i = 0; i < buffers_node->buffer_num; i++) {
		bisp_fifo->buffer_queue[i].phy_addr = buffers_node->phy_addr[i];
		/*printk("init_node adr %x\n",
		   bisp_fifo->buffer_queue[i].phy_addr); */
		bisp_fifo->buffer_queue[i].mstate = FREED;
		if (i == 0) {
			bisp_fifo->buffer_queue[i].pred =
			    buffers_node->buffer_num - 1;
			bisp_fifo->buffer_queue[i].next = i + 1;
		} else if (i == buffers_node->buffer_num - 1) {
			bisp_fifo->buffer_queue[i].pred = i - 1;
			bisp_fifo->buffer_queue[i].next = 0;
		} else {
			bisp_fifo->buffer_queue[i].pred = i - 1;
			bisp_fifo->buffer_queue[i].next = i + 1;
		}
	}
	bisp_fifo->free_idx = 0;
	bisp_fifo->read_idx = -1;
	bisp_fifo->total_node_num = buffers_node->buffer_num;
}

/*get stat buffer(freed)*/
static void bisp_change_buffer_state(struct bisp_internal_t *bisp_fifo,
				     unsigned int *pphy_addr)
{
	int i, k;
	int fidx;
	int has_idx = -1;
	struct bisp_buffer_node_t *buff_node_tmp = NULL;
	if (bisp_fifo == NULL) {
		pr_err("err!bisp_change_buffer_state,bisp_fifo is NULL\n");
		return;
	}
	if (bisp_fifo->total_node_num == 0)
		return;

	for (i = 0; i < bisp_fifo->total_node_num; i++) {
		buff_node_tmp = &bisp_fifo->buffer_queue[i];
		if (buff_node_tmp->mstate == PREPARED) {
			buff_node_tmp->mstate = ACTIVED;
		} else if (buff_node_tmp->mstate == ACTIVED) {
			buff_node_tmp->mstate = READY;
			if (bisp_fifo->read_idx < 0) {
				bisp_fifo->read_idx = i;
				bisp_fifo->ready_num = 1;
			} else {
				if (bisp_fifo->buffer_queue
				    [bisp_fifo->read_idx].mstate == READY) {
					bisp_fifo->
					    buffer_queue
					    [bisp_fifo->read_idx].mstate =
					    FREED;
					bisp_fifo->read_idx = i;
					bisp_fifo->ready_num = 1;
				} else
				    if (bisp_fifo->buffer_queue
					[bisp_fifo->read_idx].mstate == USED) {
					bisp_fifo->read_idx = i;
					bisp_fifo->ready_num = 1;
				} else
				    if (bisp_fifo->buffer_queue
					[bisp_fifo->read_idx].mstate == FREED) {
					bisp_fifo->read_idx = i;
					bisp_fifo->ready_num = 1;
				}
			}
		}
	}

	*pphy_addr = 0;
	buff_node_tmp = (struct bisp_buffer_node_t *)
	    (&bisp_fifo->buffer_queue[bisp_fifo->free_idx]);
	if (buff_node_tmp->mstate == FREED) {
		buff_node_tmp->mstate = PREPARED;
		*pphy_addr = buff_node_tmp->phy_addr;
	} else {
		pr_err("free_idx err %d,%d\n", bisp_fifo->free_idx,
		       buff_node_tmp->mstate);
		k = bisp_fifo->free_idx + 1;
		fidx = bisp_fifo->free_idx;
		do {
			if (k >= bisp_fifo->total_node_num)
				k = 0;
			buff_node_tmp = &bisp_fifo->buffer_queue[k];
			if (buff_node_tmp->mstate == FREED) {
				bisp_fifo->free_idx = k;
				has_idx = 1;
				break;
			}
			k++;
			/*printk("fidx %d\n",k); */
		} while (k != fidx);

		if (has_idx == 1) {
			buff_node_tmp->mstate = PREPARED;
			*pphy_addr = buff_node_tmp->phy_addr;
		} else {
			pr_err("free_idx err2 %d,%d\n", bisp_fifo->free_idx,
			       buff_node_tmp->mstate);
		}
	}

	k = bisp_fifo->free_idx + 1;
	fidx = bisp_fifo->free_idx;
	do {
		if (k >= bisp_fifo->total_node_num)
			k = 0;
		buff_node_tmp = &bisp_fifo->buffer_queue[k];
		if (buff_node_tmp->mstate == FREED) {
			bisp_fifo->free_idx = k;
			break;
		}
		k++;
		/*printk("fidx %d\n",k); */
	} while (k != fidx);
}

/*update stat buffer states & raw store & read back*/
static void module_updata_stat(int isp_ch, int bstat)
{
	unsigned int pyaddr = 0;
	int ch_id = finder_ch(1);
	if (gbisp_fifo[ch_id] == NULL) {
		pr_err("err!bisp_updata_stat,gbisp_fifo[ch_id] is NULL\n");
		return;
	}
	/*mutex_lock(&gisp_cmd_mutex); */
	if (bstat) {
		bisp_change_buffer_state(gbisp_fifo[ch_id], &pyaddr);
		if (pyaddr)
			bisp_reg_write(ISP_STAT_ADDR, pyaddr);
		else
			pr_err("warning!bisp_updata_stat,pyaddr is NULL\n");

#if 0
		gstat_data[ch_id][0].out_addr = gstat_data[ch_id][1].out_addr;
		gstat_data[ch_id][0].stat_addr = gstat_data[ch_id][1].stat_addr;
		gstat_data[ch_id][0].rgain = gstat_data[ch_id][1].rgain;
		gstat_data[ch_id][0].ggain = gstat_data[ch_id][1].ggain;
		gstat_data[ch_id][0].bgain = gstat_data[ch_id][1].bgain;
		gstat_data[ch_id][0].r2gain = gstat_data[ch_id][1].r2gain;
		gstat_data[ch_id][0].g2gain = gstat_data[ch_id][1].g2gain;
		gstat_data[ch_id][0].b2gain = gstat_data[ch_id][1].b2gain;

		gstat_data[ch_id][1].out_addr = bisp_reg_read(ISP_OUT_ADDRY);
		gstat_data[ch_id][1].stat_addr = bisp_reg_read(ISP_STAT_ADDR);
		gstat_data[ch_id][1].rgain = bisp_reg_read(ISP_CG_R_GAIN);
		gstat_data[ch_id][1].ggain = bisp_reg_read(ISP_CG_G_GAIN);
		gstat_data[ch_id][1].bgain = bisp_reg_read(ISP_CG_B_GAIN);
		gstat_data[ch_id][1].r2gain =
		    bisp_reg_read(ISP_SECOND_CG_R_GAIN);
		gstat_data[ch_id][1].g2gain =
		    bisp_reg_read(ISP_SECOND_CG_G_GAIN);
		gstat_data[ch_id][1].b2gain =
		    bisp_reg_read(ISP_SECOND_CG_B_GAIN);
#else
		gstat_data[ch_id][0].out_addr = gstat_data[ch_id][1].out_addr;
		gstat_data[ch_id][0].stat_addr = gstat_data[ch_id][1].stat_addr;
		gstat_data[ch_id][0].rgain = gstat_data[ch_id][1].rgain;
		gstat_data[ch_id][0].ggain = gstat_data[ch_id][1].ggain;
		gstat_data[ch_id][0].bgain = gstat_data[ch_id][1].bgain;
		gstat_data[ch_id][0].r2gain = gstat_data[ch_id][1].r2gain;
		gstat_data[ch_id][0].g2gain = gstat_data[ch_id][1].g2gain;
		gstat_data[ch_id][0].b2gain = gstat_data[ch_id][1].b2gain;

		gstat_data[ch_id][1].out_addr = gstat_data[ch_id][2].out_addr;
		gstat_data[ch_id][1].stat_addr = gstat_data[ch_id][2].stat_addr;
		gstat_data[ch_id][1].rgain = gstat_data[ch_id][2].rgain;
		gstat_data[ch_id][1].ggain = gstat_data[ch_id][2].ggain;
		gstat_data[ch_id][1].bgain = gstat_data[ch_id][2].bgain;
		gstat_data[ch_id][1].r2gain = gstat_data[ch_id][2].r2gain;
		gstat_data[ch_id][1].g2gain = gstat_data[ch_id][2].g2gain;
		gstat_data[ch_id][1].b2gain = gstat_data[ch_id][2].b2gain;

		gstat_data[ch_id][2].out_addr = bisp_reg_read(ISP_OUT_ADDRY);
		gstat_data[ch_id][2].stat_addr = bisp_reg_read(ISP_STAT_ADDR);
		gstat_data[ch_id][2].rgain = bisp_reg_read(ISP_CG_R_GAIN);
		gstat_data[ch_id][2].ggain = bisp_reg_read(ISP_CG_G_GAIN);
		gstat_data[ch_id][2].bgain = bisp_reg_read(ISP_CG_B_GAIN);
		gstat_data[ch_id][2].r2gain =
		    bisp_reg_read(ISP_SECOND_CG_R_GAIN);
		gstat_data[ch_id][2].g2gain =
		    bisp_reg_read(ISP_SECOND_CG_G_GAIN);
		gstat_data[ch_id][2].b2gain =
		    bisp_reg_read(ISP_SECOND_CG_B_GAIN);
#endif
	} else {
		pr_err("warning!bisp_updata_stat,bstat is (%d)\n", bstat);
	}
	/*mutex_unlock(&gisp_cmd_mutex); */
}

/*EXPORT_SYMBOL(bisp_updata_stat);*/

static void module_updata_rawstore(int isp_ch, int brs_en)
{
	int ch_id = finder_ch(1);
	if (gbisp_fifo[ch_id] == NULL) {
		pr_err("err!bisp_updata_rawstore,gbisp_fifo[ch_id] is NULL\n");
		return;
	}

	/*if ((gbisp_fifo[ch_id]->brs_en >= 1) && brs_en) { */
	if (brs_en) {
		int nrs_idx = gbisp_fifo[ch_id]->nrs_idx;
		if (nrs_idx >= 0 && nrs_idx < gbisp_fifo[ch_id]->nraw_num) {
			gbisp_fifo[ch_id]->nrb_addr[nrs_idx].nflag =
			    BISP_BUF_USED;
			bisp_reg_write(RAW_OUT_ADDR,
				       gbisp_fifo[ch_id]->nrb_addr[nrs_idx].
				       phy_addr);
			gbisp_fifo[ch_id]->nrs_idx++;

			if (gbisp_fifo[ch_id]->nrs_idx >=
			    gbisp_fifo[ch_id]->nraw_num) {
				gbisp_fifo[ch_id]->nrs_idx = 0;
			}
		} else {
			pr_err("warning!bisp_updata_rawstore,nrs_idx is (%d)\n",
			       nrs_idx);
		}
	} else {
		pr_err("warning!bisp_updata_rawstore,brs_en is (%d)\n", brs_en);
	}
}

/*EXPORT_SYMBOL(bisp_updata_rawstore);*/

static void module_updata_readback(int isp_ch, int brb_mode, int bsplit_next,
				   int rb_w)
{
	int ch_id = finder_ch(1);
	if (gbisp_fifo[ch_id] == NULL) {
		pr_err("err!bisp_updata_readback,gbisp_fifo[ch_id] is NULL\n");
		return;
	}

	if (brb_mode == 1) {
		int nrb_idx = gbisp_fifo[ch_id]->nrb_idx;
		if (nrb_idx >= 0 && nrb_idx < gbisp_fifo[ch_id]->nraw_num) {
			if (bsplit_next == 0) {
				bisp_set_lsc(gbisp_fifo[ch_id],
					     &gbisp_fifo[ch_id]->blsc);
				bisp_reg_write(ISP_RAW_INPUT_ADDR,
					       gbisp_fifo[ch_id]->nrb_addr
					       [nrb_idx].phy_addr + 0);
			} else {
				bisp_set_lsc_split(gbisp_fifo[ch_id],
						   &gbisp_fifo[ch_id]->blsc);

				bisp_reg_write(ISP_RAW_INPUT_ADDR,
					       gbisp_fifo[ch_id]->
					       nrb_addr[nrb_idx].phy_addr +
					       rb_w * 2);

				gbisp_fifo[ch_id]->nrb_idx++;
				if (gbisp_fifo[ch_id]->nrb_idx >=
				    gbisp_fifo[ch_id]->nraw_num) {
					gbisp_fifo[ch_id]->nrb_idx = 0;
				}
			}
		} else {
			pr_err("warning!bisp_updata_readback,nrb_idx is (%d)\n",
			       nrb_idx);
		}
	} else {
		pr_err("warning!bisp_updata_readback,brb_mode is (%d)\n",
		       brb_mode);
	}
}

/*EXPORT_SYMBOL(bisp_updata_readback);*/

static void module_get_info(int chid, int cmd, void *args)
{
	int ch_id = finder_ch(1);
	if (gbisp_fifo[ch_id] == NULL) {
		pr_err("err!bisp_get_info,gbisp_fifo[ch_id] is NULL\n");
		return;
	}

	if (cmd == 0x100)
		*(unsigned long *)args = gbisp_fifo[ch_id]->brs_en;

}

/*EXPORT_SYMBOL(bisp_get_info);*/

static int ext_cmd(struct v4l2_subdev *sd, int cmd, void *args)
{
	int ret = 0;
	struct camera_param *cam_param = (struct camera_param *)args;

	switch (cmd) {
	case V4L2_CID_MODULE_UPDATE:
		module_updata_stat(cam_param->channel, 1);
		if (cam_param->b_splited_capture) {
			module_updata_readback(cam_param->channel,
					       cam_param->b_splited_capture,
					       (cam_param->rb_cnt & 0x1),
					       cam_param->rb_rcol_stride);
		}
		break;

	case V4L2_CID_MODULE_UPDATERAW:
		module_updata_rawstore(cam_param->channel, 1);
		break;

	case V4L2_CID_MODULE_GETINFO:
		module_get_info(cam_param->channel, 0x100,
				&cam_param->b_raw_store_status);
		break;

	default:
		return -EINVAL;
	}
	return ret;
}

long bisp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct bisp_internal_t *info =
	    (struct bisp_internal_t *)filp->private_data;
	void __user *from = (void __user *)arg;
	int ret_count = 0;

	if (info == NULL) {
		pr_err("err!bisp_ioctl,bisp_fifo is NULL\n");
		return -1;
	}
	mutex_lock(&gisp_cmd_mutex);

	switch (cmd) {
	case BRI_S_CHID:
		{
			info->ch_id = 0;	/*channel_num + 1; */
		}
		break;

	case BRI_S_EN:
		{
			int ben = (int)arg;
			pr_err("bisp cmd %d\n", __LINE__);
			bisp_enable(info, ben);
		}
		break;

	case BRI_S_SBS:
		{
			struct bisp_stat_buffers_info_t bisp_buffers;
			ret_count = copy_from_user(&bisp_buffers, from,
						   sizeof(struct
						bisp_stat_buffers_info_t));
			bisp_init_node(info, &bisp_buffers);
		}
		break;

	case BRI_S_SCRP:
		{
			/*the first statistics' crop */
			struct bisp_region_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct
							  bisp_region_t));
			bisp_set_stat_crop(info, &binf, 0);
		}
		break;

	case BRI_S_SCRP2:
		{
			/*the second statistics' crop */
			struct bisp_region_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct
							  bisp_region_t));
			bisp_set_stat_crop(info, &binf, 1);
		}
		break;

	case BRI_S_PCSC:
		{
			struct bisp_csc_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_csc_t));
			bisp_set_csc(info, &binf, 0);
		}
		break;

	case BRI_S_TEMP:
		{
			struct bisp_temp_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_temp_t));
			bisp_set_temp(info, &binf);
		}
		break;

	case BRI_S_WPS:
		{
			struct bisp_wps_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_wps_t));
			bisp_set_wps(info, &binf, 0);
		}
		break;

	case BRI_S_WPS2:
		{
			struct bisp_wps_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_wps_t));
			bisp_set_wps(info, &binf, 1);
		}
		break;

	case BRI_S_RBS:
		{
			int i;
			struct bisp_rawstore_buffers_info_t bisp_buffers;
			ret_count = copy_from_user(&bisp_buffers, from,
						   sizeof(struct
						bisp_rawstore_buffers_info_t));

			for (i = 0; i < bisp_buffers.nraw_num; i++) {
				info->nrb_addr[i].phy_addr =
				    bisp_buffers.praw_phy[i];
				info->nrb_addr[i].nflag = BISP_BUF_FREED;
			}
			info->nraw_num = bisp_buffers.nraw_num;
			info->nskip_frm = bisp_buffers.nskip_frm;
			info->nrb_idx = 0;
			info->nrs_idx = 0;
			bisp_reg_write(RAW_OUT_ADDR, bisp_buffers.praw_phy[0]);
		}
		break;

	case BRI_S_RSEN:
		{
			int i;
			info->brs_en = (int)arg;
			/*clean for next capture */
			for (i = 0; i < info->nraw_num; i++)
				info->nrb_addr[i].nflag = BISP_BUF_FREED;

			info->nrb_idx = 0;
			info->nrs_idx = 0;
		}
		break;

	case BRI_S_BLC:
		{
			struct bisp_blc_t blc_inf;
			ret_count = copy_from_user(&blc_inf, from,
						   sizeof(struct bisp_blc_t));
			bisp_set_blc(info, &blc_inf);
		}
		break;

	case BRI_S_DPC:
		{
			struct bisp_dpc_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_dpc_t));
			bisp_set_dpc(info, &binf);
		}
		break;

	case BRI_S_DPCNR:
		{
			struct bisp_dpc_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_dpc_t));
			bisp_set_dpc_nr(info, &binf);
		}
		break;

	case BRI_S_LSC:
		{
			struct bisp_clsc_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_clsc_t));
			bisp_set_lsc(info, &binf);
			info->blsc = binf;
		}
		break;

	case BRI_S_BPITHD:
		{
			unsigned int binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(unsigned int));
			bisp_set_bpi_thd(info, binf);
		}
		break;

	case BRI_S_GN:
		{
			struct bisp_gain_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_gain_t));
			bisp_set_wbgain(info, &binf, 0);
		}
		break;

	case BRI_S_RGBGAMMA:
		{
			struct bisp_rgb_gamma_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct
							  bisp_rgb_gamma_t));
			bisp_set_rgbgamma(info, &binf);
		}
		break;

	case BRI_S_GN2:
		{
			struct bisp_gain_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_gain_t));
			bisp_set_wbgain(info, &binf, 1);
		}
		break;

	case BRI_S_MCC:
		{
			struct bisp_mcc_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_mcc_t));
			bisp_set_mcc(info, &binf);
		}
		break;

	case BRI_S_CSC:
		{
			struct bisp_csc_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_csc_t));
			bisp_set_csc(info, &binf, 1);
		}
		break;

	case BRI_S_BC:
		{
			struct bisp_bc_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_bc_t));
			bisp_set_bc(info, &binf);
		}
		break;

	case BRI_S_FCS:
		{
			struct bisp_fcs_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_fcs_t));
			bisp_set_fcs(info, &binf);
		}
		break;

	case BRI_S_CCS:
		{
			struct bisp_ccs_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_ccs_t));
			bisp_set_ccs(info, &binf);
		}
		break;

	case BRI_S_BRI:
		{
			struct bisp_bsch_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_bsch_t));
			bisp_set_bsch(info, &binf, 0);
		}
		break;

	case BRI_S_STA:
		{
			struct bisp_bsch_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_bsch_t));
			bisp_set_bsch(info, &binf, 1);
		}
		break;

	case BRI_S_CON:
		{
			struct bisp_bsch_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_bsch_t));
			bisp_set_bsch(info, &binf, 2);
		}
		break;

	case BRI_S_HUE:
		{
			struct bisp_bsch_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_bsch_t));
			bisp_set_bsch(info, &binf, 3);
		}
		break;

	case BRI_S_CFX:
		{
			struct bisp_cfixed_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct
							  bisp_cfixed_t));
			bisp_set_cfx(info, &binf);
		}
		break;

	case BRI_G_SIF:
		{
			struct bisp_ads_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct bisp_ads_t));
			bisp_get_phy_info(info, &binf);
			ret_count = copy_to_user(from, &binf,
						 sizeof(struct bisp_ads_t));
		}
		break;

	case BRI_G_FIF:
		{
			struct bisp_frame_info_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct
							  bisp_frame_info_t));
			bisp_get_all_info(info, &binf);
			ret_count = copy_to_user(from, &binf,
						 sizeof(struct
							bisp_frame_info_t));
		}
		break;

	case BRI_G_CRP:
		{
			struct bisp_region_t binf;
			ret_count = copy_from_user(&binf, from,
						   sizeof(struct
							  bisp_region_t));
			bisp_get_raw_crop(info, &binf);
			ret_count = copy_to_user(from, &binf,
						 sizeof(struct bisp_region_t));
		}
		break;

	case BRI_G_MDL:
		{
			pr_info("info->module_name %s\n", module_name.buf);
			ret_count = copy_to_user(from, &module_name,
						 sizeof(struct module_name_t));
		}
		break;

	case BRI_G_GN:
		{
			struct bisp_gain_t binf;
			bisp_get_gain(info, &binf, 0);
			ret_count = copy_to_user(from, &binf,
						 sizeof(struct bisp_gain_t));
		}
		break;

	case BRI_G_GN2:
		{
			struct bisp_gain_t binf;
			bisp_get_gain(info, &binf, 1);
			ret_count = copy_to_user(from, &binf,
						 sizeof(struct bisp_gain_t));
		}
		break;

	case BRI_G_BV:
		{
			struct bisp_blc_t binf;
			bisp_get_bv(info, &binf);
			ret_count = copy_to_user(from, &binf,
						 sizeof(struct bisp_blc_t));
		}
		break;

	case BRI_S_AF_ENABLE:
		{
			bisp_af_enable();
			/*bisp_af_irq_enable(); */
			info->af_enabled = 1;
			af_enable_count++;
		}
		break;

	case BRI_S_AF_DISABLE:
		{
			bisp_af_irq_disable();
			bisp_af_disable();
			info->af_enabled = 0;
			af_enable_count--;
		}
		break;

	case BRI_S_AF_WINS:
		{
			ret_count = copy_from_user(&info->af_region, from,
						   sizeof(struct
							  bisp_af_region_t));
			bisp_af_setwin(info, &info->af_region);
		}
		break;

	case BRI_S_AF_PARAM:
		{
			ret_count = copy_from_user(&info->af_param, from,
						   sizeof(struct
							  bisp_af_param_t));
			bisp_af_param_set(info, &info->af_param);
		}
		break;

	default:
		{
			pr_err("bisp drv can not support this cmd(%x)!%d\n",
			       cmd, __LINE__);
			mutex_unlock(&gisp_cmd_mutex);
			return -EIO;
		}
	}

	mutex_unlock(&gisp_cmd_mutex);
	return 0;
}

int bisp_open(struct inode *inode, struct file *filp)
{
	struct bisp_internal_t *info = NULL;
	int i = 0;
	int opened_cnt = 0;
	mutex_lock(&gisp_cmd_mutex);

	bisp_drv_opened++;
	if (bisp_drv_opened > _CH_MX_NUM_) {
		pr_err("bisp_drv open count %d\n", bisp_drv_opened);
		bisp_drv_opened -= 1;
		mutex_unlock(&gisp_cmd_mutex);
		return -1;
	}

	if (bisp_drv_opened == 1) {
		for (i = 0; i < _CH_MX_NUM_; i++) {
			gbisp_fifo[i] = NULL;
			memset(&gstat_data[i][0], 0,
			       sizeof(struct stat_updata_t));
			memset(&gstat_data[i][1], 0,
			       sizeof(struct stat_updata_t));
			memset(&gstat_data[i][2], 0,
			       sizeof(struct stat_updata_t));
		}
	}

	for (i = 0; i < _CH_MX_NUM_; i++) {
		if (gbisp_fifo[i] == NULL) {
			opened_cnt = i;
			i = _CH_MX_NUM_ + 1;
		} else {
			pr_err("err!bisp_open out of range!%d,%d,%d\n",
			       bisp_drv_opened, opened_cnt,
			       gbisp_fifo[i]->ch_id);
		}
	}

	memset(&gstat_data[opened_cnt][0], 0, sizeof(struct stat_updata_t));
	memset(&gstat_data[opened_cnt][1], 0, sizeof(struct stat_updata_t));
	memset(&gstat_data[opened_cnt][2], 0, sizeof(struct stat_updata_t));

	info = (struct bisp_internal_t *)
	    bisp_malloc(sizeof(struct bisp_internal_t));
	if (info == NULL) {
		pr_err("err!bisp_drv malloc failed!\n");
		mutex_unlock(&gisp_cmd_mutex);
		return -1;
	}

	if (bisp_drv_opened == 1)
		g_iomap_base = ioremap(MODULE_BASE, 1108);

	info->open_count = opened_cnt;
	memset(info, 0, sizeof(struct bisp_internal_t));
	filp->private_data = (void *)info;
	gbisp_fifo[opened_cnt] = info;

	pr_info("bisp open success(%d)!\n", bisp_drv_opened);
	mutex_unlock(&gisp_cmd_mutex);
	return 0;
}

int bisp_release(struct inode *inode, struct file *filp)
{
	struct bisp_internal_t *info =
	    (struct bisp_internal_t *)filp->private_data;
	mutex_lock(&gisp_cmd_mutex);
	bisp_drv_opened--;
	if (bisp_drv_opened <= 0) {
		bisp_drv_opened = 0;
		if (g_iomap_base) {
			iounmap(g_iomap_base);
			g_iomap_base = NULL;
		}
	}

	if (info) {
		gbisp_fifo[info->open_count] = NULL;
		bisp_free(info);
		info = NULL;
	}
	mutex_unlock(&gisp_cmd_mutex);
	pr_info("bisp release success(%d)!\n", bisp_drv_opened);
	return 0;
}

int bisp_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

int bisp_resume(struct platform_device *dev)
{
	return 0;
}

static void bisp_dummy_release(struct device *dev)
{
}

static const struct file_operations bisp_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = bisp_ioctl,
	.compat_ioctl = bisp_ioctl,
	.open = bisp_open,
	.release = bisp_release,
};

static struct platform_device bisp_platform_device = {
	.name = DEVDRV_NAME_BISP,
	.id = -1,
	.dev = {
		.release = bisp_dummy_release,
		},
};

static struct platform_driver bisp_platform_driver = {
	.driver = {
		   .name = DEVDRV_NAME_BISP,
		   .owner = THIS_MODULE,
		   },
	.suspend = bisp_suspend,
	.resume = bisp_resume,
};

static struct miscdevice bisp_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_BISP,
	.fops = &bisp_fops,
};

static int host_module_init(void)
{
	int ret;
	ret = misc_register(&bisp_miscdevice);
	if (ret) {
		pr_err("register bisp misc device failed!...\n");
		goto err0;
	}
	pr_info("bisp_drv_init ...\n");

	ret = platform_device_register(&bisp_platform_device);
	if (ret) {
		pr_err("register bisp_platform_device error!...\n");
		goto err1;
	}

	ret = platform_driver_register(&bisp_platform_driver);
	if (ret) {
		pr_err("register bisp platform driver4pm error!...\n");
		goto err2;
	}

	return 0;

 err2:
	platform_device_unregister(&bisp_platform_device);
 err1:
	misc_deregister(&bisp_miscdevice);
 err0:
	return ret;
}

static void host_module_exit(void)
{
	pr_info("bisp_drv_exit ..!.\n");
	misc_deregister(&bisp_miscdevice);
	platform_device_unregister(&bisp_platform_device);
	platform_driver_unregister(&bisp_platform_driver);
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Actions Semi, Inc");
MODULE_DESCRIPTION("Actions SOC BISP device driver");
MODULE_ALIAS("platform: asoc-bisp");
