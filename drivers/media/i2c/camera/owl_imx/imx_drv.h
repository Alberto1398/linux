/*
 * Actions OWL SoCs IMX driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Kevin Deng <dengzhiquan@actions-semi.com>
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

#ifndef __IMX_DRV_H__
#define __IMX_DRV_H__

enum {
	IMX_IDLE,
	IMX_READY,
	IMX_BUSY,
	IMX_ERR
};

struct imx_reg_t {
	unsigned int imx_ctl;
	unsigned int imx_cfg1;
	unsigned int imx_cfg2;
	unsigned int imx_cfg3;
	unsigned int imx_size;
	unsigned int imx_yin;
	unsigned int imx_uin;
	unsigned int imx_vin;
	unsigned int imx_yout;
	unsigned int imx_uout;
	unsigned int imx_vout;
};

struct imx_freq_t {
	int width;
	int height;
	int freq;
};

#define IMX_DRV_IOC_MAGIC_NUMBER   'I'
#define IMX_IOC_VERSION    _IO(IMX_DRV_IOC_MAGIC_NUMBER, 0x0)
#define IMX_IOC_START      _IOW(IMX_DRV_IOC_MAGIC_NUMBER, 0x1, struct imx_reg_t)
#define IMX_IOC_QUERY      _IO(IMX_DRV_IOC_MAGIC_NUMBER, 0x2)
#define IMX_IOC_GET_STATUS _IO(IMX_DRV_IOC_MAGIC_NUMBER, 0x3)
#define IMX_IOC_SET_FREQ  _IOW(IMX_DRV_IOC_MAGIC_NUMBER, 0x4, struct imx_freq_t)
#define IMX_IOC_GET_FREQ   _IO(IMX_DRV_IOC_MAGIC_NUMBER, 0x5)

#endif
