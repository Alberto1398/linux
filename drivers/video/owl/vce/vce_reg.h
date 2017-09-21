/*
 * Actions OWL SoCs VCE driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Jed Zeng <zengjie@actions-semi.com>
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

#ifndef __VCE_REG_H__
#define __VCE_REG_H__

#define VCE_ID          (0)	/*0X78323634 */
#define VCE_STATUS      (4)	/*swreg1 */
#define VCE_CFG         (16)	/*swreg4 */
#define VCE_PARAM0      (20)	/*swreg5 */
#define VCE_PARAM1      (24)
#define VCE_STRM        (28)
#define VCE_STRM_ADDR   (32)
#define VCE_YADDR       (36)
#define VCE_LIST0_ADDR  (40)
#define VCE_LIST1_ADDR  (44)
#define VCE_ME_PARAM    (48)	/*swreg12 */
#define VCE_SWIN        (52)
#define VCE_SCALE_OUT   (56)
#define VCE_RECT        (60)
#define VCE_RC_PARAM1   (64)
#define VCE_RC_PARAM2   (68)
#define VCE_RC_PARAM3   (72)
#define VCE_RC_HDBITS   (76)
#define VCE_TS_INFO     (80)
#define VCE_TS_HEADER   (84)
#define VCE_TS_BLUHD    (88)
#define VCE_REF_DHIT    (92)
#define VCE_REF_DMISS   (96)
#define CSC_COEFF_CFG0  (100)
#define CSC_COEFF_CFG1  (104)
#define CSC_COEFF_CFG2  (108)
#define CSC_COEFF_CFG3  (112)
#define CSC_COEFF_CFG4  (116)

#define VCE_OUTSTANDING (8)
#define UPS_YAS         (120)
#define UPS_CBCRAS      (124)
#define UPS_CRAS        (128)
#define UPS_IFORMAT     (132)
#define VCE_STRM_LEN    (136)
#define UPS_RATIO       (140)
#define UPS_IFS         (144)

#endif
