/*
 * linux/drivers/video/owl/dss/lcdchw.h
 *
 * Copyright (C) 2011 Actions
 * Author: Hui Wang <wanghui@actions-semi.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OWL_DSIHW_H
#define __OWL_DSIHW_H

#include <linux/platform_device.h>
#include <video/owl_dss.h>


#define GL5209

#ifdef GL5209

#define    DSIHW_REG_MEM_BASE                           0xb0220000
#define    DSIHW_REG_MEM_END                            0xb022008a
#define     DSI_CTRL                                    (0x0000)
#define     DSI_SIZE                                    (0x0004)
#define     DSI_COLOR                                   (0x0008)
#define     DSI_VIDEO_CFG                               (0x000C)
#define     DSI_RGBHT0                                  (0x0010)
#define     DSI_RGBHT1                                  (0x0014)
#define     DSI_RGBVT0                                  (0x0018)
#define     DSI_RGBVT1                                  (0x001c)
#define     DSI_TIMEOUT                                 (0x0020)
#define     DSI_TR_STA                                  (0x0024)
#define     DSI_INT_EN                                  (0x0028)
#define     DSI_ERROR_REPORT                            (0x002c)
#define     DSI_FIFO_ODAT                               (0x0030)
#define     DSI_FIFO_IDAT                               (0x0034)
#define     DSI_IPACK                                   (0x0038)
#define     DSI_PACK_CFG                                (0x0040)
#define     DSI_PACK_HEADER                             (0x0044)
#define     DSI_TX_TRIGGER                              (0x0048)
#define     DSI_RX_TRIGGER                              (0x004c)
#define     DSI_LANE_CTRL                               (0x0050)
#define     DSI_LANE_STA                                (0x0054)
#define     DSI_PHY_T0                                  (0x0060)
#define     DSI_PHY_T1                                  (0x0064)
#define     DSI_PHY_T2                                  (0x0068)
#define     DSI_APHY_DEBUG0                             (0x0070)
#define     DSI_APHY_DEBUG1                             (0x0074)
#define     DSI_SELF_TEST                               (0x0078)
#define     DSI_PIN_MAP                                 (0x007c)
#define     DSI_PHY_CTRL                                (0x0080)
#define     DSI_FT_TEST                                 (0x0088)
#endif


#ifdef GL5206

#define    DSIHW_REG_MEM_BASE                           0xb0220000
#define    DSIHW_REG_MEM_END                            0xb022008a

#define     DSI_CTRL                                    (0x0000)
#define     DSI_SIZE                                    (0x0004)
#define     DSI_COLOR                                   (0x0008)
#define     DSI_VIDEO_CFG                               (0x000C)
#define     DSI_RGBHT0                                  (0x0010)
#define     DSI_RGBHT1                                  (0x0014)
#define     DSI_RGBVT0                                  (0x0018)
#define     DSI_RGBVT1                                  (0x001c)
#define     DSI_TIMEOUT                                 (0x0020)
#define     DSI_TR_STA                                  (0x0024)
#define     DSI_INT_EN                                  (0x0028)
#define     DSI_ERROR_REPORT                            (0x002c)
#define     DSI_FIFO_ODAT                               (0x0030)
#define     DSI_FIFO_IDAT                               (0x0034)
#define     DSI_IPACK                                   (0x0038)
#define     DSI_PACK_CFG                                (0x0040)
#define     DSI_PACK_HEADER                             (0x0044)
#define     DSI_TX_TRIGGER                              (0x0048)
#define     DSI_RX_TRIGGER                              (0x004c)
#define     DSI_LANE_CTRL                               (0x0050)
#define     DSI_LANE_STA                                (0x0054)
#define     DSI_PHY_T0                                  (0x0060)
#define     DSI_PHY_T1                                  (0x0064)
#define     DSI_PHY_T2                                  (0x0068)
#define     DSI_APHY_DEBUG0                             (0x0070)
#define     DSI_APHY_DEBUG1                             (0x0074)
#define     DSI_SELF_TEST                               (0x0078)
#define     DSI_PIN_MAP					(0x007c)
#define     DSI_PHY_CTRL                                (0x0080)
#define     DSI_FT_TEST                                 (0x0088)

#define    DMA_REG_MEM_BASE                             0xB0260000
#define    DMA_REG_MEM_END                              0xB0260c00
#define    n						4
#define    DMA_IRQ_PD0					(0x0000)

#define     DMA0_BASE					0x0100
#define     DMA0_MODE                                   (DMA0_BASE*n+0x0000)
#define     DMA0_SOURCE                                 (DMA0_BASE*n+0x0004)
#define     DMA0_DESTINATION                            (DMA0_BASE*n+0x0008)
#define     DMA0_FRAME_LEN                              (DMA0_BASE*n+0x000C)
#define     DMA0_FRAME_CNT                              (DMA0_BASE*n+0x0010)
#define     DMA0_REMAIN_FRAME_CNT                       (DMA0_BASE*n+0x0014)
#define     DMA0_REMAIN_CNT                             (DMA0_BASE*n+0x0018)
#define     DMA0_SOURCE_STRIDE                          (DMA0_BASE*n+0x001C)
#define     DMA0_DESTINATION_STRIDE                     (DMA0_BASE*n+0x0020)
#define     DMA0_START                                  (DMA0_BASE*n+0x0024)
#define     DMA0_PAUSE                                  (DMA0_BASE*n+0x0028)
#define     DMA0_CHAINED_CTL                            (DMA0_BASE*n+0x002C)
#define     DMA0_CONSTANT                               (DMA0_BASE*n+0x0030)
#define     DMA0_LINKLIST_CTL                           (DMA0_BASE*n+0x0034)
#define     DMA0_NEXT_DESCRIPTOR                        (DMA0_BASE*n+0x0038)
#define     DMA0_CURRENT_DESCRIPTOR_NUM                 (DMA0_BASE*n+0x003C)
#define     DMA0_INT_CTL                                (DMA0_BASE*n+0x0040)
#define     DMA0_INT_STATUS                             (DMA0_BASE*n+0x0044)
#define     DMA0_CURRENT_SOURCE_POINTER                 (DMA0_BASE*n+0x0048)
#define     DMA0_CURRENT_DESTINATION_POINTER            (DMA0_BASE*n+0x004C)

#endif

#define DATE_TYPE_NO_PAR	0x05
#define	DATE_TYPE_ONE_PAR	0x15

#define p_round(x) ((int)((x) + 500) / 1000)
/************************************************/
#define HSS 4	/*H sync start			*/
#define HSE 4	/*H sync end			*/
#define HAF 6	/*long packet header and footer	*/
/************************************************/


struct owl_dsi_config {
	uint32_t	lcd_mode;	/* 0: command mode, 1: video mode*/
	uint32_t	lane_count;	/* 0: 1, 1:1~2, 2:1~3, 3:1~4 */
	uint32_t	lane_polarity;	/* 0: normal, 1: reversed*/
	uint32_t	lane_swap;	/* the sequence of data lane */
	uint32_t	burst_bllp;	/* 0 ~ 300 */
	uint32_t	video_mode;	/* 0:sync mode,1:de mode,2:burst mode*/
	uint32_t	pclk_rate;	/* MHz*/

	uint32_t	dsi_phy_t0;
	uint32_t	dsi_phy_t1;
	uint32_t	dsi_phy_t2;
};
#define DSI_ID_S500 0
#define DSI_ID_S700 1
#define DSI_ID_S900 2

struct dsi_diffs {
	int id;
};

struct dsi_data {
	struct owl_display_ctrl		ctrl;
	struct owl_dsi_config		configs;
	const struct dsi_diffs		*diffs;

	bool				enabled;
	uint32_t			hsclk_pll;/* dsi pll clk */

	struct mutex			lock;
	/*
	 *resources used by DSI controller
	 */
	void __iomem			*base;
	void __iomem			*cmu_base;
	void __iomem			*sps_ctl_reg;
	struct platform_device		*pdev;
	struct reset_control		*rst;
	struct clk			*clk;
	struct clk			*clk_24m;
};

/*
 * dsi phy tx calculate
 * ***************************************************************************/
#define TLPX 100 /*in nano second, 0.000 000 001s*/
#define T_WAKEUP (1200000ul) /**in nano second, 0.000 000 001s*/

/*
 * config about dsi_phy_tx
 * */
#define CLK_PREPARE	5
#define CLK_ZERO	7
#define CLK_PRE		3
#define CLK_POST	7
#define CLK_TRAIL	5

#define HS_EXIT		7
#define HS_TRAIL	6
#define HS_ZERO		7
#define HS_PREPARE	5

uint16_t n_hs_prepare_cal_arra[5] = {0, 1, 8, 10, 14};
uint16_t n_hs_prepare_arra[6] = {0, 1, 2, 3, 4, 5};

uint16_t n_hs_zero_cal_arra[6] = {0, 16, 19, 28, 40, 56};
uint16_t n_hs_zero_arra[8] = {0, 1, 2, 3, 4, 5, 6, 7};

uint16_t n_hs_exit_cal_arra[6] = {0, 16, 19, 28, 40, 56};
uint16_t n_hs_exit_arra[8] = {0, 1, 2, 3, 4, 5, 6, 7};

uint16_t n_clk_prepare_cal_arra[5] = {0, 1, 8, 10, 14};
uint16_t n_clk_prepare_arra[6] = {0, 1, 2, 3, 4, 5};

uint16_t n_clk_zero_cal_arra[4] = {0, 64, 80, 112};
uint16_t n_clk_zero_arra[8] = {0, 1, 2, 3, 4, 5, 6, 7};

uint16_t n_clk_pre_cal_arra[2] = {0, 4};
uint16_t n_clk_pre_arra[4] = {0, 1, 2, 3};

uint16_t n_clk_post_cal_arra[1] = {7};
uint16_t n_clk_post_arra[8] = {0, 1, 2, 3, 4, 5, 6, 7};

uint16_t n_clk_trail_cal_arra[6] = {0, 3, 5, 7, 10, 14};
uint16_t n_clk_trail_arra[6] = {0, 1, 2, 3, 4, 5};

struct phy_tx_mode {
	uint16_t mode;
	uint16_t start;
	uint16_t end;
	uint16_t arra_prop;
};
/*hs_prepare*/
struct phy_tx_mode hs_prepare = {
	.mode	= HS_PREPARE,
	.start	= 1,
	.end	= 8,
	.arra_prop = 1,
};
/*hs_zero*/
struct phy_tx_mode hs_zero = {
	.mode	= HS_ZERO,
	.start	= 0,
	.end	= 16,
	.arra_prop = 2,
};
/*hs_exit*/
struct phy_tx_mode hs_exit = {
	.mode	= HS_EXIT,
	.start	= 0,
	.end	= 16,
	.arra_prop = 2,
};
/*clk_prepare*/
struct phy_tx_mode clk_prepar = {
	.mode	= CLK_PREPARE,
	.start	= 1,
	.end	= 8,
	.arra_prop = 1,
};
/*clk_zero*/
struct phy_tx_mode clk_zero = {
	.mode	= CLK_ZERO,
	.start	= 0,
	.end	= 64,
	.arra_prop = 4,
};
/*clk_pre*/
struct phy_tx_mode clk_pre = {
	.mode	= CLK_PRE,
	.start	= 0,
	.end	= 4,
	.arra_prop = 1,
};
/*clk_post*/
struct phy_tx_mode clk_post = {
	.mode = CLK_POST,
	.start = 0,
	.end  = 0,
	.arra_prop = 0,
};

/*clk_trail*/
struct phy_tx_mode clk_trail = {
	.mode = CLK_TRAIL,
	.start = 0,
	.end  = 0,
	.arra_prop = 0,
};
/*
 * Decimals up remainder function
 * */
static uint64_t dsi_ceil(uint64_t dividend, uint64_t divisor)
{
	return (dividend / divisor + (dividend % divisor != 0 ? 1 : 0));
}

uint32_t get_val_from_cal(uint32_t cal, uint32_t mode)
{
	uint32_t val = 0;
	int i;
	if (mode == CLK_PRE) {
		val = cal - 1;
		return val;
	}
	for (i = 0; i < mode; i++) {
		val = (1 << i) * 10;
		if (cal <= val)
			return i;
	}
}
static uint16_t get_dsi_phy_tx(uint16_t cal, uint16_t *cal_arra,
		uint16_t cal_arra_length, uint16_t *arra,
		struct phy_tx_mode  *tx_mode)
{
	int i = 0, j = 0;
	uint16_t val;
	if (cal_arra_length >= 2) {
		for (i = 1, j = 0; i < cal_arra_length; i++) {
			if ((cal > cal_arra[i - 1] * 10) &&
					(cal <= cal_arra[i] * 10)) {
				if ((tx_mode->start == cal_arra[i - 1]) &&
						(tx_mode->end == cal_arra[i])) {
					val = get_val_from_cal(cal,
							tx_mode->mode);
					if ((tx_mode->mode == HS_ZERO) ||
						(tx_mode->mode == HS_EXIT) ||
						(tx_mode->mode == CLK_ZERO))
						return val - 1;
					else
						return val;
				} else {
					val = arra[j];
					return val;
				}
			} else if (cal > cal_arra[cal_arra_length - 1] * 10) {
				val = arra[j];
				return val;
			}
			if (tx_mode->start == j)
				j += tx_mode->arra_prop;
			j++;
		}
	} else if (1 == cal_arra_length) {
		if (cal >= cal_arra[0] * 10) {
			val = arra[tx_mode->mode];
			return val;
		} else {
			val = cal;
			return val;
		}
	} else
		return -1;
	return 0;
}
#endif
