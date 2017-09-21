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
 *	2015/11/30: Created by Lipeng.
 */
#ifndef __LCD_LCDC_H_
#define __LCD_LCDC_H_

/* should consider other platforms, TODO */
#define LCDC_CTL			(0x0000)
#define LCDC_SIZE			(0x0004)
#define LCDC_STATUS			(0x0008)
#define LCDC_TIM0			(0x000C)
#define LCDC_TIM1			(0x0010)
#define LCDC_TIM2			(0x0014)
#define LCDC_COLOR			(0x0018)

#define LCDC_IMG_XPOS			(0x001c)
#define LCDC_IMG_YPOS			(0x0020)

#define LCDC_LVDS_CTL			(0x0200)
#define LCDC_LVDS_ALG_CTL0		(0x0204)
#define LCDC_LVDS_DEBUG			(0x0208)

#endif
