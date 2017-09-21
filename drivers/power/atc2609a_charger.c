/*
 * Actions ATC260X PMICs CHARGER driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Terry Chen <chenbo@actions-semi.com>
 *
 * Atc2609a Charger Phy file
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
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mfd/atc260x/atc260x.h>
#include <linux/power_supply.h>
#include "atc260x_charger.h"

#define ATC2609A_PMU_BASE			(0x00)

#define ATC2609A_PMU_OT_CTL			(ATC2609A_PMU_BASE + 0x52)
#define OT_CTL_OT							(1 << 15)
#define	OT_CTL_OT_INT_SET_SHIFT         	(13)
#define	OT_CTL_OT_INT_SET_MASK         		(0x3 << OT_CTL_OT_INT_SET_SHIFT)
#define	OT_CTL_OT_INT_SET_60         		(0 << OT_CTL_OT_INT_SET_SHIFT)
#define	OT_CTL_OT_INT_SET_70         		(1 << OT_CTL_OT_INT_SET_SHIFT)
#define	OT_CTL_OT_INT_SET_80         		(2 << OT_CTL_OT_INT_SET_SHIFT)
#define	OT_CTL_OT_INT_SET_90         		(3 << OT_CTL_OT_INT_SET_SHIFT)
#define OT_CTL_OT_INT_EN 			        (1 << 12)
#define OT_CTL_OT_SHUTOFF_EN		        (1 << 11)
#define	OT_CTL_OT_SHUTOFF_SET_SHIFT         (9)
#define	OT_CTL_OT_SHUTOFF_SET_MASK          (0x3 << OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	OT_CTL_OT_SHUTOFF_SET_90           (0 << OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	OT_CTL_OT_SHUTOFF_SET_100           (1 << OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	OT_CTL_OT_SHUTOFF_SET_110           (2 << OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	OT_CTL_OT_SHUTOFF_SET_120           (3 << OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define OT_CTL_OT_EN					    (1 << 8)

#define ATC2609A_PMU_SYS_PENDING		(ATC2609A_PMU_BASE + 0x10)
#define SYS_PENDING_BAT_OV			(1 << 15)

#define ATC2609A_PMU_SWCHG_CTL0			(ATC2609A_PMU_BASE + 0x16)
#define SWCHG_CTL0_SWCHG_EN			(1 << 15)
#define SWCHG_CTL0_TRICKLEEN			(1 << 14)
#define SWCHG_CTL0_ICHG_REG_CHGISTK_SHIFT	(13)
#define SWCHG_CTL0_ICHG_REG_CHGISTK_MASK	\
	(0x1 << SWCHG_CTL0_ICHG_REG_CHGISTK_SHIFT)
#define SWCHG_CTL0_ICHG_REG_CHGISTK_100MA	\
	(0 << SWCHG_CTL0_ICHG_REG_CHGISTK_SHIFT)
#define SWCHG_CTL0_ICHG_REG_CHGISTK_200MA	\
	(0x1 << SWCHG_CTL0_ICHG_REG_CHGISTK_SHIFT)
#define SWCHG_CTL0_RSENSEL_SHIFT		(12)
#define SWCHG_CTL0_RSENSEL_MASK		        (0x1 << SWCHG_CTL0_RSENSEL_SHIFT)
#define SWCHG_CTL0_RSENSEL_20mohm		(0x0 << SWCHG_CTL0_RSENSEL_SHIFT)
#define SWCHG_CTL0_RSENSEL_10mohm		(0x1 << SWCHG_CTL0_RSENSEL_SHIFT)
#define SWCHG_CTL0_ICHG_REG_CC_SHIFT		(8)
#define SWCHG_CTL0_ICHG_REG_CC_MASK		\
	(0xf << SWCHG_CTL0_ICHG_REG_CC_SHIFT)
#define SWCHG_CTL0_EN_CHG_TIME			(1 << 7)
#define SWCHG_CTL0_CHARGE_TRIKLE_TIMER_SHIFT	(5)
#define SWCHG_CTL0_CHARGE_TRIKLE_TIMER_MASK	\
	(0x3 << SWCHG_CTL0_CHARGE_TRIKLE_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_TRIKLE_TIMER_30MIN	\
	(0 << SWCHG_CTL0_CHARGE_TRIKLE_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_TRIKLE_TIMER_40MIN	\
	(1 << SWCHG_CTL0_CHARGE_TRIKLE_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_TRIKLE_TIMER_50MIN	\
	(2 << SWCHG_CTL0_CHARGE_TRIKLE_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_TRIKLE_TIMER_60MIN	\
	(3 << SWCHG_CTL0_CHARGE_TRIKLE_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_CCCV_TIMER_SHIFT	(3)
#define SWCHG_CTL0_CHARGE_CCCV_TIMER_MASK	\
	(0x3 << SWCHG_CTL0_CHARGE_CCCV_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_CCCV_4H		\
	(0 << SWCHG_CTL0_CHARGE_CCCV_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_CCCV_6H		\
	(1 << SWCHG_CTL0_CHARGE_CCCV_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_CCCV_8H		\
	(2 << SWCHG_CTL0_CHARGE_CCCV_TIMER_SHIFT)
#define SWCHG_CTL0_CHARGE_CCCV_12H		\
	(3 << SWCHG_CTL0_CHARGE_CCCV_TIMER_SHIFT)
#define SWCHG_CTL0_CHG_FORCE_OFF		(1 << 2)
#define SWCHG_CTL0_CHGAUTO_DETECT_EN		(1 << 1)
#define SWCHG_CTL0_DTSEL_SHIFT			(0)
#define SWCHG_CTL0_DTSEL_MASK			(0x1 << SWCHG_CTL0_DTSEL_SHIFT)
#define SWCHG_CTL0_DTSEL_12MIN			(0 << SWCHG_CTL0_DTSEL_SHIFT)
#define SWCHG_CTL0_DTSEL_20S			(1 << SWCHG_CTL0_DTSEL_SHIFT)

#define ATC2609A_PMU_SWCHG_CTL1			(ATC2609A_PMU_BASE + 0x17)
#define SWCHG_CTL1_EN_BAT_DET			(1 << 15)
#define SWCHG_CTL1_CHG_EN_CUR_RISE		(1 << 13)
#define SWCHG_CTL1_CV_SET_L_SHIFT		(10)
#define	SWCHG_CTL1_CV_SET_L			(1 << SWCHG_CTL1_CV_SET_L_SHIFT)
#define SWCHG_CTL1_CV_SET_MASK			\
	(SWCHG_CTL1_CV_SET_L | SWCHG_CTL1_CV_SET_H)
#define SWCHG_CTL1_CV_SET_4200MV		(0)
#define SWCHG_CTL1_CV_SET_4250MV		(SWCHG_CTL1_CV_SET_L)
#define SWCHG_CTL1_CV_SET_4350MV		(SWCHG_CTL1_CV_SET_H)
#define SWCHG_CTL1_CV_SET_4400MV		\
	(SWCHG_CTL1_CV_SET_L | SWCHG_CTL1_CV_SET_H)
#define SWCHG_CTL1_STOPV_SHIFT			(9)
#define SWCHG_CTL1_STOPV_MASK			\
	(0x1 << SWCHG_CTL1_STOPV_SHIFT)
#define SWCHG_CTL1_STOPV_4160MV			\
	(0 << SWCHG_CTL1_STOPV_SHIFT)
#define SWCHG_CTL1_STOPV_4180MV			\
	(1 << SWCHG_CTL1_STOPV_SHIFT)
#define SWCHG_CTL1_CV_SET_H_SHIFT		(8)
#define SWCHG_CTL1_CV_SET_H			\
	(1 << SWCHG_CTL1_CV_SET_H_SHIFT)
#define SWCHG_CTL1_CHGPWR_SET_SHIFT		(6)
#define SWCHG_CTL1_CHGPWR_SET_MASK		\
	(0x3 << SWCHG_CTL1_CHGPWR_SET_SHIFT)
#define SWCHG_CTL1_CHGPWR_SET_60MV		(0 << SWCHG_CTL1_CHGPWR_SET_SHIFT)
#define SWCHG_CTL1_CHGPWR_SET_160MV		(1 << SWCHG_CTL1_CHGPWR_SET_SHIFT)
#define SWCHG_CTL1_CHGPWR_SET_264MV		(2 << SWCHG_CTL1_CHGPWR_SET_SHIFT)
#define SWCHG_CTL1_CHGPWR_SET_373MV		(3 << SWCHG_CTL1_CHGPWR_SET_SHIFT)
#define SWCHG_CTL1_CHG_SYSPWR			(1 << 5)
#define SWCHG_CTL1_EN_CHG_TEMP			(1 << 4)
#define SWCHG_CTL1_CHG_SYSSTEADY_SET_SHIFT	(2)
#define SWCHG_CTL1_CHG_SYSSTEADY_SET_MASK	(0x3 << SWCHG_CTL1_CHG_SYSSTEADY_SET_SHIFT)
#define SWCHG_CTL1_CHG_SYSSTEADY_SET_LOWER	(0 << SWCHG_CTL1_CHG_SYSSTEADY_SET_SHIFT)
#define SWCHG_CTL1_CHG_SYSSTEADY_SET_LOW	(1 << SWCHG_CTL1_CHG_SYSSTEADY_SET_SHIFT)
#define SWCHG_CTL1_CHG_SYSSTEADY_SET_HIGH	(2 << SWCHG_CTL1_CHG_SYSSTEADY_SET_SHIFT)
#define SWCHG_CTL1_CHG_SYSSTEADY_SET_HIGHER	(3 << SWCHG_CTL1_CHG_SYSSTEADY_SET_SHIFT)

#define ATC2609A_PMU_SWCHG_CTL2			(ATC2609A_PMU_BASE + 0x18)
#define SWCHG_CTL2_TM_EN2			(1 << 12)
#define SWCHG_CTL2_TM_EN			(1 << 10)
#define SWCHG_CTL2_EN_OCP			(1 << 4)
#define SWCHG_CTL2_ILIMITED_SHIFT		(3)
#define SWCHG_CTL2_ILIMITED_MASK		(1 << SWCHG_CTL2_ILIMITED_SHIFT)
#define SWCHG_CTL2_ILINITED_2500MA		(0 << SWCHG_CTL2_ILIMITED_SHIFT)
#define SWCHG_CTL2_ILINITED_3470MA		(1 << SWCHG_CTL2_ILIMITED_SHIFT)

#define ATC2609A_PMU_SWCHG_CTL3			(ATC2609A_PMU_BASE + 0x19)
#define SWCHG_CTL3_CHARGER_MODE_SEL_SHIFT	(15)
#define SWCHG_CTL3_CHARGER_MODE_SEL_MASK	(1 << SWCHG_CTL3_CHARGER_MODE_SEL_SHIFT)
#define SWCHG_CTL3_CHARGER_MODE_SEL_LINER	(0 << SWCHG_CTL3_CHARGER_MODE_SEL_SHIFT)
#define SWCHG_CTL3_CHARGER_MODE_SEL_SWITCH	(1 << SWCHG_CTL3_CHARGER_MODE_SEL_SHIFT)

#define ATC2609A_PMU_SWCHG_CTL4			(ATC2609A_PMU_BASE + 0x1A)
#define SWCHG_CTL4_PHASE_SHIFT			(11)
#define SWCHG_CTL4_PHASE_MASK			(0x3 << SWCHG_CTL4_PHASE_SHIFT)
#define SWCHG_CTL4_PHASE_PRECHARGE		(1 << SWCHG_CTL4_PHASE_SHIFT)
#define SWCHG_CTL4_PHASE_CONSTANT_CURRENT	(2 << SWCHG_CTL4_PHASE_SHIFT)
#define SWCHG_CTL4_PHASE_CONSTANT_VOLTAGE	(3 << SWCHG_CTL4_PHASE_SHIFT)
#define SWCHG_CTL4_BAT_EXT			(1 << 8)
#define SWCHG_CTL4_BAT_DT_OVER			(1 << 7)

#define ATC2609A_PMU_APDS_CTL0			(ATC2609A_PMU_BASE + 0x11)
#define APDS_CTL0_VBUSCONTROL_EN		(1 << 15)
#define APDS_CTL0_VBUS_CONTROL_SEL		(1 << 14)
#define APDS_CTL0_VBUS_CUR_LIMITED_SHIFT	(12)
#define APDS_CTL0_VBUS_CUR_LIMITED_MASK		(0x3 << APDS_CTL0_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_CUR_LIMITED_100MA	(0x0 << APDS_CTL0_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_CUR_LIMITED_300MA	(0x1 << APDS_CTL0_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_CUR_LIMITED_500MA	(0x2 << APDS_CTL0_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_CUR_LIMITED_800MA	(0x3 << APDS_CTL0_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_VOL_LIMITED_SHIFT	(10)
#define APDS_CTL0_VBUS_VOL_LIMITED_MASK		(0x3 << APDS_CTL0_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_VOL_LIMITED_4200MV	(0x0 << APDS_CTL0_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_VOL_LIMITED_4300MV	(0x1 << APDS_CTL0_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_VOL_LIMITED_4400MV	(0x2 << APDS_CTL0_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL0_VBUS_VOL_LIMITED_4500MV	(0x3 << APDS_CTL0_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL0_VBUSOTG			(1 << 9)
#define APDS_CTL0_VBUS_PD			(1 << 2)
#define APDS_CTL0_WALL_PD			(1 << 1)

#define ATC2609A_PMU_CHARGER_CTL		(ATC2609A_PMU_BASE + 0x14)
#define	CHARGER_CTL_TEMPTH1_SHIFT		(13)
#define	CHARGER_CTL_TEMPTH1_MASK		(0x3 << CHARGER_CTL_TEMPTH1_SHIFT)
#define CHARGER_CTL_TEMPTH1_65			(0 << CHARGER_CTL_TEMPTH1_SHIFT)
#define CHARGER_CTL_TEMPTH1_75			(1 << CHARGER_CTL_TEMPTH1_SHIFT)
#define CHARGER_CTL_TEMPTH1_85			(2 << CHARGER_CTL_TEMPTH1_SHIFT)
#define CHARGER_CTL_TEMPTH1_95			(3 << CHARGER_CTL_TEMPTH1_SHIFT)
#define	CHARGER_CTL_TEMPTH2_SHIFT		(11)
#define	CHARGER_CTL_TEMPTH2_MASK		(0x3 << CHARGER_CTL_TEMPTH2_SHIFT)
#define CHARGER_CTL_TEMPTH2_75			(0 << CHARGER_CTL_TEMPTH2_SHIFT)
#define	CHARGER_CTL_TEMPTH2_85			(1 << CHARGER_CTL_TEMPTH2_SHIFT)
#define CHARGER_CTL_TEMPTH2_95			(2 << CHARGER_CTL_TEMPTH2_SHIFT)
#define CHARGER_CTL_TEMPTH2_105			(3 << CHARGER_CTL_TEMPTH2_SHIFT)
#define	CHARGER_CTL_TEMPTH3_SHIFT		(9)
#define	CHARGER_CTL_TEMPTH3_MASK		(0x3 << CHARGER_CTL_TEMPTH3_SHIFT)
#define CHARGER_CTL_TEMPTH3_85			(0 << CHARGER_CTL_TEMPTH3_SHIFT)
#define CHARGER_CTL_TEMPTH3_95			(1 << CHARGER_CTL_TEMPTH3_SHIFT)
#define CHARGER_CTL_TEMPTH3_105			(2 << CHARGER_CTL_TEMPTH3_SHIFT)
#define CHARGER_CTL_TEMPTH3_115			(2 << CHARGER_CTL_TEMPTH3_SHIFT)

#define ATC2609A_PMU_ADC12B_V			(ATC2609A_PMU_BASE + 0x57)
#define ADC12B_V_MASK				(0xfff)

#define ADC_LSB_FOR_BATV			(732)
#define SHARE_VOL_FACTOR			(2)
#define CONST_ROUNDING				(5 * 100)
#define CONST_FACTOR				(1000)


enum CC_TIMER {
	CC_TIMER_4H,
	CC_TIMER_6H,
	CC_TIMER_8H,
	CC_TIMER_12H,
};

enum VBUS_CURRENT_LMT {
	VBUS_CURR_LIMT_100MA,
	VBUS_CURR_LIMT_300MA,
	VBUS_CURR_LIMT_500MA,
	VBUS_CURR_LIMT_800MA
};

enum VBUS_VOLTAGE_LMT {
	VBUS_VOL_LIMT_4200MV,
	VBUS_VOL_LIMT_4300MV,
	VBUS_VOL_LIMT_4400MV,
	VBUS_VOL_LIMT_4500MV
};

enum STOP_VOLTAGE {
	STOP_VOLTAGE_4160MV,
	STOP_VOLTAGE_4180MV,
};

enum ILINITED {
	ILINITED_2500MA,
	ILINITED_3470MA
};

enum CONSTANT_VOLTAGE {
	CONSTANT_VOL_4200MV,
	CONSTANT_VOL_4250MV,
	CONSTANT_VOL_4350MV,
	CONSTANT_VOL_4400MV
};

enum CHARGER_MODE {
	CHARGER_MODE_LINER,
	CHARGER_MODE_SWITCH
};

enum TRICKLE_CURRENT {
	TRICKLE_CURRENT_100MA = 100,
	TRICKLE_CURRENT_200MA = 200
};

enum TRICKLE_TIMER {
	TRICKLE_TIMER_30MIN,
	TRICKLE_TIMER_40MIN,
	TRICKLE_TIMER_50MIN,
	TRICKLE_TIMER_60MIN,
};

static void atc2609a_charger_set_onoff(struct atc260x_dev *atc260x,
	bool enable)
{
	int val;

	if (enable)
		val = SWCHG_CTL0_SWCHG_EN;
	else
		val = 0;

	atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
		SWCHG_CTL0_SWCHG_EN, val);
}

static bool atc2609a_charger_get_onoff(struct atc260x_dev *atc260x)
{
	bool onoff;

	onoff =  atc260x_reg_read(atc260x, ATC2609A_PMU_SWCHG_CTL0) &
			SWCHG_CTL0_SWCHG_EN;
	if (onoff)
		return true;
	else
		return false;
}


static int atc2609a_charger_get_trick_current(struct atc260x_dev *atc260x)
{
	int data;

	data =  atc260x_reg_read(atc260x, ATC2609A_PMU_SWCHG_CTL0) &
			~SWCHG_CTL0_ICHG_REG_CHGISTK_MASK;

	if (data)
		return 200;
	else
		return 100;
}

static void atc2609a_charger_set_trick_current(struct atc260x_dev *atc260x,
	enum TRICKLE_CURRENT value)
{
	if (value == TRICKLE_CURRENT_100MA)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_ICHG_REG_CHGISTK_MASK,
			SWCHG_CTL0_ICHG_REG_CHGISTK_100MA);
	else if (value == TRICKLE_CURRENT_200MA)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_ICHG_REG_CHGISTK_MASK,
			SWCHG_CTL0_ICHG_REG_CHGISTK_200MA);
	else
		pr_err("%s tricle current value invalid!\n", __func__);
}

static void atc2609a_charger_set_trick_timer(struct atc260x_dev *atc260x,
	enum TRICKLE_TIMER timer)
{
	switch (timer) {
	case (TRICKLE_TIMER_30MIN):
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
		SWCHG_CTL0_CHARGE_TRIKLE_TIMER_MASK,
		SWCHG_CTL0_CHARGE_TRIKLE_TIMER_30MIN);
		break;
	case (TRICKLE_TIMER_40MIN):
	atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
		SWCHG_CTL0_CHARGE_TRIKLE_TIMER_MASK,
		SWCHG_CTL0_CHARGE_TRIKLE_TIMER_40MIN);
		break;
	case (TRICKLE_TIMER_50MIN):
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_TRIKLE_TIMER_MASK,
			SWCHG_CTL0_CHARGE_TRIKLE_TIMER_50MIN);
		break;
	case (TRICKLE_TIMER_60MIN):
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_TRIKLE_TIMER_MASK,
			SWCHG_CTL0_CHARGE_TRIKLE_TIMER_60MIN);
		break;
	default:
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_TRIKLE_TIMER_MASK,
			SWCHG_CTL0_CHARGE_TRIKLE_TIMER_30MIN);
		break;
	}
}

static void atc2609a_charger_enable_trick(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_TRICKLEEN,
			SWCHG_CTL0_TRICKLEEN);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_TRICKLEEN, 0);
}

static void atc2609a_charger_set_mode(struct atc260x_dev *atc260x,
	int mode)
{
	if (mode == CHARGER_MODE_LINER)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL3,
			SWCHG_CTL3_CHARGER_MODE_SEL_MASK,
			SWCHG_CTL3_CHARGER_MODE_SEL_LINER);
	else if (mode == CHARGER_MODE_SWITCH)
		 atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL3,
			SWCHG_CTL3_CHARGER_MODE_SEL_MASK,
			SWCHG_CTL3_CHARGER_MODE_SEL_SWITCH);
	else
		pr_info("%s charger mode value invalid\n", __func__);
}

static enum CHARGER_MODE
	atc2609a_charger_get_mode(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_SWCHG_CTL3) &
		SWCHG_CTL3_CHARGER_MODE_SEL_MASK;

	if (data)
		return CHARGER_MODE_SWITCH;
	else
		return CHARGER_MODE_LINER;
}



static int atc2609a_charger_cc_filter(struct atc260x_dev *atc260x,
	int value)
{
	enum CHARGER_MODE mode;
	int data;

	mode = atc2609a_charger_get_mode(atc260x);

	if (mode == CHARGER_MODE_LINER)
		data = value * 2;
	else
		data = value;

	if (data < 200)
		data =  100;
	else if (data <= 3000)
		data = (data / 200) * 200;
	else
		data = 3000;

	if (mode == CHARGER_MODE_LINER)
		data = data / 2;

	return data;
}

static int atc2609a_charger_cc2regval(enum CHARGER_MODE mode,
		int data)
{
	int value = 0;

	if (mode == CHARGER_MODE_LINER)
		value = data * 2;
	else
		value = data;

	return (value / 200) & 0xf;
}

static int atc2609a_charger_get_cc(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_SWCHG_CTL0)
			& SWCHG_CTL0_ICHG_REG_CC_MASK;
	data >>= SWCHG_CTL0_ICHG_REG_CC_SHIFT;

	if (data == 0)
		return 100;
	else
		return data * 200;
}

void atc2609a_charger_set_cc(struct atc260x_dev *atc260x, int cc)
{
	int reg;
	enum CHARGER_MODE mode;

	mode = atc2609a_charger_get_mode(atc260x);
	reg = atc2609a_charger_cc2regval(mode, cc);

	atc260x_reg_setbits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
		SWCHG_CTL0_ICHG_REG_CC_MASK,
		reg << SWCHG_CTL0_ICHG_REG_CC_SHIFT);
}

void atc2609a_charger_set_cc_timer(struct atc260x_dev *atc260x,
	enum CC_TIMER timer)
{
	switch (timer) {
	case CC_TIMER_4H:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_CCCV_TIMER_MASK | SWCHG_CTL0_EN_CHG_TIME,
			SWCHG_CTL0_CHARGE_CCCV_4H | SWCHG_CTL0_EN_CHG_TIME);
		break;
	case CC_TIMER_6H:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_CCCV_TIMER_MASK | SWCHG_CTL0_EN_CHG_TIME,
			SWCHG_CTL0_CHARGE_CCCV_6H | SWCHG_CTL0_EN_CHG_TIME);
		break;
	case CC_TIMER_8H:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_CCCV_TIMER_MASK | SWCHG_CTL0_EN_CHG_TIME,
			SWCHG_CTL0_CHARGE_CCCV_8H | SWCHG_CTL0_EN_CHG_TIME);
		break;
	case CC_TIMER_12H:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_CCCV_TIMER_MASK | SWCHG_CTL0_EN_CHG_TIME,
			SWCHG_CTL0_CHARGE_CCCV_12H | SWCHG_CTL0_EN_CHG_TIME);
		break;
	default:
		pr_err("%s CCCV TIMER value invalid\n", __func__);
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHARGE_CCCV_TIMER_MASK | SWCHG_CTL0_EN_CHG_TIME,
			SWCHG_CTL0_CHARGE_CCCV_12H | SWCHG_CTL0_EN_CHG_TIME);
		break;
	}

}

static void atc2609a_charger_set_vbus_vol_lmt(struct atc260x_dev *atc260x,
	enum VBUS_VOLTAGE_LMT value)
{
	switch (value) {
	case VBUS_VOL_LIMT_4200MV:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_VOL_LIMITED_MASK,
			APDS_CTL0_VBUS_VOL_LIMITED_4200MV);
		break;
	case VBUS_VOL_LIMT_4300MV:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_VOL_LIMITED_MASK,
			APDS_CTL0_VBUS_VOL_LIMITED_4300MV);
		break;
	case VBUS_VOL_LIMT_4400MV:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_VOL_LIMITED_MASK,
			APDS_CTL0_VBUS_VOL_LIMITED_4400MV);
		break;
	case VBUS_VOL_LIMT_4500MV:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_VOL_LIMITED_MASK,
			APDS_CTL0_VBUS_VOL_LIMITED_4500MV);
		break;
	default:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_VOL_LIMITED_MASK,
			APDS_CTL0_VBUS_VOL_LIMITED_4300MV);
		break;
	}
}

int atc2609a_charger_get_vbus_vol_lmt(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_APDS_CTL0);
	data = data & APDS_CTL0_VBUS_VOL_LIMITED_MASK;

	switch (data) {
	case APDS_CTL0_VBUS_VOL_LIMITED_4200MV:
		return 4200;
	case APDS_CTL0_VBUS_VOL_LIMITED_4300MV:
		return 4300;
	case APDS_CTL0_VBUS_VOL_LIMITED_4400MV:
		return 4400;
	case APDS_CTL0_VBUS_VOL_LIMITED_4500MV:
		return 4500;
	default:
		pr_err("%s get vbus voltage limited value err(%x)\n",
			__func__, data);
		return -1;
	}
}

static void atc2609a_charger_set_vbus_current_lmt(struct atc260x_dev *atc260x,
	enum VBUS_CURRENT_LMT value)
{
	switch (value) {
	case VBUS_CURR_LIMT_100MA:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_CUR_LIMITED_MASK,
			APDS_CTL0_VBUS_CUR_LIMITED_100MA);
		break;
	case VBUS_CURR_LIMT_300MA:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_CUR_LIMITED_MASK,
			APDS_CTL0_VBUS_CUR_LIMITED_300MA);
		break;
	case VBUS_CURR_LIMT_500MA:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_CUR_LIMITED_MASK,
			APDS_CTL0_VBUS_CUR_LIMITED_500MA);
		break;
	case VBUS_CURR_LIMT_800MA:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_CUR_LIMITED_MASK,
			APDS_CTL0_VBUS_CUR_LIMITED_800MA);
		break;
	default:
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_CUR_LIMITED_MASK,
			APDS_CTL0_VBUS_CUR_LIMITED_500MA);
		break;
	}
}

static int atc2609a_charger_get_vbus_current_lmt(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_APDS_CTL0);
	data = data & APDS_CTL0_VBUS_CUR_LIMITED_MASK;

	switch (data) {
	case APDS_CTL0_VBUS_CUR_LIMITED_100MA:
		return 100;
	case APDS_CTL0_VBUS_CUR_LIMITED_300MA:
		return 300;
	case APDS_CTL0_VBUS_CUR_LIMITED_500MA:
		return 500;
	case APDS_CTL0_VBUS_CUR_LIMITED_800MA:
		return 800;
	default:
		pr_err("%s get vbus current limited value err\n", __func__);
		return -1;
	}
}

static int atc2609a_charger_get_vbus_ctlmode(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_APDS_CTL0);
	if (data & APDS_CTL0_VBUS_CONTROL_SEL)
		return CURRENT_LIMITED;
	else
		return VOLTAGE_LIMITED;
}

static void atc2609a_charger_set_vbus_ctlmode(struct atc260x_dev *atc260x,
			enum VBUS_CTL_MODE vbus_control_mode)
{
	if (vbus_control_mode == VOLTAGE_LIMITED)
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_CONTROL_SEL, 0);
	else if (vbus_control_mode == CURRENT_LIMITED)
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_CONTROL_SEL,
			APDS_CTL0_VBUS_CONTROL_SEL);
	else
		pr_err("%s vbus ctl mode value invalid\n", __func__);
}

static void atc2609a_charger_set_vbus_ctl_en(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUSCONTROL_EN,
			APDS_CTL0_VBUSCONTROL_EN);
	else
		atc260x_reg_setbits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUSCONTROL_EN, 0);
}

static bool atc2609a_charger_get_vbus_ctl_en(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_APDS_CTL0);
	if (data & APDS_CTL0_VBUSCONTROL_EN)
		return true;
	else
		return false;
}

static int atc2609a_charger_get_vbus_onoff(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_APDS_CTL0);
	data &= APDS_CTL0_VBUSOTG;
	if (data)
		return 0;
	else
		return 1;
}

static void atc2609a_charger_set_vbus_onoff(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		/*shut off the path from vbus to vbat,
		 when support usb adaptor only.*/
		atc260x_set_bits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUSOTG, 0);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUSOTG, APDS_CTL0_VBUSOTG);
}

static void atc2609a_charger_set_wall_pd(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_WALL_PD, APDS_CTL0_WALL_PD);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_WALL_PD, 0);
}

static void atc2609a_charger_set_vbus_pd(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_PD, APDS_CTL0_VBUS_PD);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_APDS_CTL0,
			APDS_CTL0_VBUS_PD, 0);
}

static void atc2609a_charger_syspwr_steady(struct atc260x_dev *atc260x,
	bool enable)
{
	atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
		SWCHG_CTL1_CHGPWR_SET_MASK |
		SWCHG_CTL1_CHG_SYSSTEADY_SET_MASK,
		SWCHG_CTL1_CHGPWR_SET_160MV |
		SWCHG_CTL1_CHG_SYSSTEADY_SET_HIGH);

	if (!enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CHG_SYSPWR, 0);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CHG_SYSPWR, SWCHG_CTL1_CHG_SYSPWR);
}

static void atc2609a_charger_en_current_rise(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CHG_EN_CUR_RISE,
			SWCHG_CTL1_CHG_EN_CUR_RISE);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CHG_EN_CUR_RISE, 0);
}

static void atc2609a_charger_set_cv(struct atc260x_dev *atc260x,
	enum BATTERY_TYPE type)
{
	switch (type) {
	case BAT_TYPE_4180MV:
	case BAT_TYPE_4200MV:
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CV_SET_MASK,
			SWCHG_CTL1_CV_SET_4250MV);
		break;
	case BAT_TYPE_4300MV:
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CV_SET_MASK,
			SWCHG_CTL1_CV_SET_4350MV);
		break;
	case BAT_TYPE_4350MV:
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CV_SET_MASK,
			SWCHG_CTL1_CV_SET_4400MV);
		break;
	default:
		pr_err("%s bat type invalid\n", __func__);
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_CV_SET_MASK,
			SWCHG_CTL1_CV_SET_4250MV);
		break;
	}

}

static void atc2609a_charger_ot_shutoff(struct atc260x_dev *atc260x,
			int ot_shutoff_enable)
{
	if (!ot_shutoff_enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_OT_CTL,
			OT_CTL_OT_SHUTOFF_EN, 0);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_OT_CTL,
			OT_CTL_OT_SHUTOFF_EN,
			OT_CTL_OT_SHUTOFF_EN);
}

static void atc2609a_charger_set_otint_en(struct atc260x_dev *atc260x,
			bool otint_enable)
{
	atc260x_set_bits(atc260x, ATC2609A_PMU_OT_CTL,
			OT_CTL_OT_INT_SET_MASK, OT_CTL_OT_INT_SET_90);
	if (!otint_enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_OT_CTL,
			OT_CTL_OT_INT_EN, 0);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_OT_CTL,
			OT_CTL_OT_INT_EN,
			OT_CTL_OT_INT_EN);
}

static void atc2609a_charger_clear_otint_pending(struct atc260x_dev *atc260x)
{
	atc2609a_charger_set_otint_en(atc260x,0);
	atc260x_set_bits(atc260x, ATC2609A_PMU_OT_CTL,
			OT_CTL_OT, OT_CTL_OT);
	atc2609a_charger_set_otint_en(atc260x,1);
}

static void atc2609a_charger_current_temp(struct atc260x_dev *atc260x,
			int  change_current_temp)
{
	if (change_current_temp)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_EN_CHG_TEMP,
			SWCHG_CTL1_EN_CHG_TEMP);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_EN_CHG_TEMP, 0);
}

static void atc2609a_charger_set_stop_vol(struct atc260x_dev *atc260x,
			enum STOP_VOLTAGE stopv)
{
	if (stopv == STOP_VOLTAGE_4160MV)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_STOPV_MASK,
			SWCHG_CTL1_STOPV_4160MV);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
			SWCHG_CTL1_STOPV_MASK,
			SWCHG_CTL1_STOPV_4180MV);
}

static void atc2609a_charger_op_offset_threshold(struct atc260x_dev *atc260x)
{
	atc260x_set_bits(atc260x, ATC2609A_PMU_CHARGER_CTL,
		CHARGER_CTL_TEMPTH1_MASK |
		CHARGER_CTL_TEMPTH2_MASK |
		CHARGER_CTL_TEMPTH3_MASK,
		CHARGER_CTL_TEMPTH1_95 |
		CHARGER_CTL_TEMPTH2_105 |
		CHARGER_CTL_TEMPTH3_115);
}

static void atc2609a_charger_adjust_op_offset(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL2,
			SWCHG_CTL2_TM_EN2 | SWCHG_CTL2_TM_EN,
			SWCHG_CTL2_TM_EN2 | SWCHG_CTL2_TM_EN);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL2,
			SWCHG_CTL2_TM_EN2 | SWCHG_CTL2_TM_EN, 0);
}

static void atc2609a_charger_set_ocp(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL2,
			SWCHG_CTL2_EN_OCP,
			SWCHG_CTL2_EN_OCP);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL2,
			SWCHG_CTL2_EN_OCP, 0);
}

static void atc2609a_charger_pick_current(struct atc260x_dev *atc260x,
	enum ILINITED value)
{
	if (value == ILINITED_2500MA)
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL2,
			SWCHG_CTL2_ILIMITED_MASK,
			SWCHG_CTL2_ILINITED_2500MA);
	else
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL2,
			SWCHG_CTL2_ILIMITED_MASK,
			SWCHG_CTL2_ILINITED_3470MA);
}

static void atc2609a_charger_autodet_timer(struct atc260x_dev *atc260x,
	int timer)
{
	atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
		SWCHG_CTL0_DTSEL_MASK | SWCHG_CTL0_EN_CHG_TIME,
		SWCHG_CTL0_EN_CHG_TIME | timer);
}

static void atc2609a_charger_auto_stop(struct atc260x_dev *atc260x,
	bool enable)
{
	if (!enable) {
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHGAUTO_DETECT_EN |
			SWCHG_CTL0_CHG_FORCE_OFF, 0);
	} else {
		atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL0,
			SWCHG_CTL0_CHGAUTO_DETECT_EN |
			SWCHG_CTL0_CHG_FORCE_OFF,
			SWCHG_CTL0_CHGAUTO_DETECT_EN |
			SWCHG_CTL0_CHG_FORCE_OFF);
		atc2609a_charger_autodet_timer(atc260x, SWCHG_CTL0_DTSEL_12MIN);
	}
}

static void atc2609a_charger_init_base(struct atc260x_dev *atc260x)
{
	/*charger init*/
	atc2609a_charger_enable_trick(atc260x, true);
	atc2609a_charger_set_trick_current(atc260x, TRICKLE_CURRENT_200MA);
	atc2609a_charger_set_cc(atc260x, 200);
	atc2609a_charger_set_trick_timer(atc260x, TRICKLE_TIMER_30MIN);
	atc2609a_charger_set_cc_timer(atc260x, CC_TIMER_12H);
	atc2609a_charger_auto_stop(atc260x, false);

	atc2609a_charger_en_current_rise(atc260x, items.cur_rise_enable);
	atc2609a_charger_set_cv(atc260x, BAT_TYPE_4200MV);
	atc2609a_charger_set_stop_vol(atc260x, STOP_VOLTAGE_4160MV);
	atc2609a_charger_current_temp(atc260x,  items.change_current_temp);

	atc2609a_charger_set_ocp(atc260x, true);
	atc2609a_charger_pick_current(atc260x, ILINITED_3470MA);

	atc2609a_charger_set_mode(atc260x, CHARGER_MODE_SWITCH);

	atc2609a_charger_ot_shutoff(atc260x, items.ot_shutoff_enable);

	atc2609a_charger_op_offset_threshold(atc260x);

	atc2609a_charger_adjust_op_offset(atc260x, true);
	/* wall init*/
	atc2609a_charger_set_wall_pd(atc260x, true);
	/*vbus init*/
	atc2609a_charger_set_vbus_current_lmt(atc260x, VBUS_CURR_LIMT_500MA);
	atc2609a_charger_set_vbus_vol_lmt(atc260x, VBUS_VOL_LIMT_4300MV);
}

static int atc2609a_bat_check_online(struct atc260x_dev *atc260x)
{
	int data;
	int count = 0;
	int online;

	/* dectect bit 0 > 1 to start dectecting */
	data = atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
		SWCHG_CTL1_EN_BAT_DET, SWCHG_CTL1_EN_BAT_DET);
	if (data < 0)
		return data;

	/* wait bat detect over */
	do {
		msleep(70);
		data = atc260x_reg_read(atc260x, ATC2609A_PMU_SWCHG_CTL4) &
			SWCHG_CTL4_BAT_DT_OVER;
		pr_info("%s wait battery detect over,data:0x%x\n",
			__func__, data);
		count += 70;
		if (count >= 300)
			break;
	} while (!data);

	data = atc260x_reg_read(atc260x, ATC2609A_PMU_SWCHG_CTL4);
	if (data < 0)
		return data;

	if (data & SWCHG_CTL4_BAT_EXT)
		online = 1;
	else
		online = 0;

	/* clear battery detect bit, otherwise cannot changer */
	data = atc260x_set_bits(atc260x, ATC2609A_PMU_SWCHG_CTL1,
		SWCHG_CTL1_EN_BAT_DET, 0);
	if (data < 0)
		return data;

	pr_info("%s battery exist:%d\n", __func__, online);

	return online;
}

struct atc260x_charger_ops atc2609a_charger_ops = {
	.set_onoff = atc2609a_charger_set_onoff,
	.get_onoff = atc2609a_charger_get_onoff,
	.set_vbus_ctl_en = atc2609a_charger_set_vbus_ctl_en,
	.get_vbus_ctl_en = atc2609a_charger_get_vbus_ctl_en,
	.set_vbus_ctlmode = atc2609a_charger_set_vbus_ctlmode,
	.get_vbus_ctlmode = atc2609a_charger_get_vbus_ctlmode,
	.get_vbus_vol_lmt = atc2609a_charger_get_vbus_vol_lmt,
	.get_vbus_current_lmt = atc2609a_charger_get_vbus_current_lmt,
	.cc_filter = atc2609a_charger_cc_filter,
	.set_cc = atc2609a_charger_set_cc,
	.get_cc = atc2609a_charger_get_cc,
	.get_trick_current = atc2609a_charger_get_trick_current,
	.set_cv = atc2609a_charger_set_cv,
	.set_wall_pd = atc2609a_charger_set_wall_pd,
	.get_vbus_onoff = atc2609a_charger_get_vbus_onoff,
	.set_vbus_onoff = atc2609a_charger_set_vbus_onoff,
	.set_vbus_pd = atc2609a_charger_set_vbus_pd,
	.init_base = atc2609a_charger_init_base,
	.chk_bat_online = atc2609a_bat_check_online,
	.set_syspwr_steady = atc2609a_charger_syspwr_steady,
	.set_otint_en = atc2609a_charger_set_otint_en,
	.clear_otint_pending = atc2609a_charger_clear_otint_pending,
};
