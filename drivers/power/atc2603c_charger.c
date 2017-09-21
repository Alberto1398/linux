/*
 * Actions ATC260X PMICs CHARGER driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Terry Chen <chenbo@actions-semi.com>
 *
 * atc2603c Charger Phy file
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

/* PMU_CHARGER_CTL0 */
#define     PMU_CHARGER_CTL0_CHGAUTO_DETECT_EN      (1 << 0)
#define     PMU_CHARGER_CTL0_CHGPWR_SET_SHIFT       (1)
#define     PMU_CHARGER_CTL0_CHGPWR_SET_MASK        (0x3 << PMU_CHARGER_CTL0_CHGPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHGPWR_SET_100MV       (0 << PMU_CHARGER_CTL0_CHGPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHGPWR_SET_200MV       (1 << PMU_CHARGER_CTL0_CHGPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHGPWR_SET_300MV       (2 << PMU_CHARGER_CTL0_CHGPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHGPWR_SET_400MV       (3 << PMU_CHARGER_CTL0_CHGPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHG_CURRENT_TEMP       (1 << 3)
#define     PMU_CHARGER_CTL0_CHG_SYSPWR_SET_SHIFT   (4)
#define     PMU_CHARGER_CTL0_CHG_SYSPWR_SET         (0x3 << PMU_CHARGER_CTL0_CHG_SYSPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHG_SYSPWR_SET_3810MV      (0 << PMU_CHARGER_CTL0_CHG_SYSPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHG_SYSPWR_SET_3960MV      (1 << PMU_CHARGER_CTL0_CHG_SYSPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHG_SYSPWR_SET_4250MV      (2 << PMU_CHARGER_CTL0_CHG_SYSPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHG_SYSPWR_SET_4400MV      (3 << PMU_CHARGER_CTL0_CHG_SYSPWR_SET_SHIFT)
#define     PMU_CHARGER_CTL0_CHG_SYSPWR             (1 << 6)
#define     PMU_CHARGER_CTL0_DTSEL_SHIFT            (7)
#define     PMU_CHARGER_CTL0_DTSEL_MASK             (0x1 << PMU_CHARGER_CTL0_DTSEL_SHIFT)
#define     PMU_CHARGER_CTL0_DTSEL_12MIN            (0 << PMU_CHARGER_CTL0_DTSEL_SHIFT)
#define     PMU_CHARGER_CTL0_DTSEL_20S              (1 << PMU_CHARGER_CTL0_DTSEL_SHIFT)
#define     PMU_CHARGER_CTL0_CHG_FORCE_OFF          (1 << 8)
#define     PMU_CHARGER_CTL0_TRICKLEEN              (1 << 9)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER2_SHIFT    (10)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER2_MASK     (0x3 << PMU_CHARGER_CTL0_CHARGE_TIMER2_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER2_30MIN    (0 << PMU_CHARGER_CTL0_CHARGE_TIMER2_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER2_40MIN    (1 << PMU_CHARGER_CTL0_CHARGE_TIMER2_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER2_50MIN    (2 << PMU_CHARGER_CTL0_CHARGE_TIMER2_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER2_60MIN    (3 << PMU_CHARGER_CTL0_CHARGE_TIMER2_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER1_SHIFT    (12)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER1_MASK     (0x3 << PMU_CHARGER_CTL0_CHARGE_TIMER1_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER1_4H       (0 << PMU_CHARGER_CTL0_CHARGE_TIMER1_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER1_6H       (1 << PMU_CHARGER_CTL0_CHARGE_TIMER1_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER1_8H       (2 << PMU_CHARGER_CTL0_CHARGE_TIMER1_SHIFT)
#define     PMU_CHARGER_CTL0_CHARGE_TIMER1_12H      (3 << PMU_CHARGER_CTL0_CHARGE_TIMER1_SHIFT)
#define     PMU_CHARGER_CTL0_CHGTIME                (1 << 14)
#define     PMU_CHARGER_CTL0_ENCH                   (1 << 15)

/* ATC2603C_PMU_CHARGER_CTL1 */
#define     PMU_CHARGER_CTL1_ICHG_REG_CC_SHIFT      (0)
#define     PMU_CHARGER_CTL1_ICHG_REG_CC_MASK       (0xf << PMU_CHARGER_CTL1_ICHG_REG_CC_SHIFT)
#define     PMU_CHARGER_CTL1_ICHG_REG_CC(i)         \
				((((i) / 100) << PMU_CHARGER_CTL1_ICHG_REG_CC_SHIFT) & PMU_CHARGER_CTL1_ICHG_REG_CC_MASK)

#define     PMU_CHARGER_CTL1_BAT_EXIST_EN           (1 << 5)
#define     PMU_CHARGER_CTL1_CURRENT_SOFT_START     (1 << 6)
#define     PMU_CHARGER_CTL1_STOPV_SHIFT            (7)
#define     PMU_CHARGER_CTL1_STOPV_MASK             (0x1 << PMU_CHARGER_CTL1_STOPV_SHIFT)
#define     PMU_CHARGER_CTL1_STOPV_4180MV           (0 << PMU_CHARGER_CTL1_STOPV_SHIFT)
#define     PMU_CHARGER_CTL1_STOPV_4160MV           (1 << PMU_CHARGER_CTL1_STOPV_SHIFT)
#define     PMU_CHARGER_CTL1_CHARGER_TIMER_END      (1 << 8)
#define     PMU_CHARGER_CTL1_BAT_DT_OVER            (1 << 9)
#define     PMU_CHARGER_CTL1_BAT_EXIST              (1 << 10)
#define     PMU_CHARGER_CTL1_CUR_ZERO               (1 << 11)
#define     PMU_CHARGER_CTL1_CHGPWROK               (1 << 12)
#define     PMU_CHARGER_CTL1_PHASE_SHIFT            (13)
#define     PMU_CHARGER_CTL1_PHASE_MASK             (0x3 << PMU_CHARGER_CTL1_PHASE_SHIFT)
#define     PMU_CHARGER_CTL1_PHASE_PRECHARGE        (1 << PMU_CHARGER_CTL1_PHASE_SHIFT)
#define     PMU_CHARGER_CTL1_PHASE_CONSTANT_CURRENT (2 << PMU_CHARGER_CTL1_PHASE_SHIFT)
#define     PMU_CHARGER_CTL1_PHASE_CONSTANT_VOLTAGE (3 << PMU_CHARGER_CTL1_PHASE_SHIFT)
#define     PMU_CHARGER_CTL1_CHGEND                 (1 << 15)


/* PMU_CHARGER_CTL2 */
#define     PMU_CHARGER_CTL2_CV_SET_SHIFT           (1)
#define		PMU_CHARGER_CTL2_CV_SET_MASK			(0x7 << PMU_CHARGER_CTL2_CV_SET_SHIFT)
#define     PMU_CHARGER_CTL2_CV_SET_4200MV          (0 << PMU_CHARGER_CTL2_CV_SET_SHIFT)
#define     PMU_CHARGER_CTL2_CV_SET_4250MV          (1 << PMU_CHARGER_CTL2_CV_SET_SHIFT)
#define     PMU_CHARGER_CTL2_CV_SET_4300MV          (2 << PMU_CHARGER_CTL2_CV_SET_SHIFT)
#define     PMU_CHARGER_CTL2_CV_SET_4350MV          (3 << PMU_CHARGER_CTL2_CV_SET_SHIFT)
#define     PMU_CHARGER_CTL2_CV_SET_4400MV          (4 << PMU_CHARGER_CTL2_CV_SET_SHIFT)

#define     PMU_CHARGER_CTL2_ICHG_REG_T_SHIFT       (4)
#define     PMU_CHARGER_CTL2_ICHG_REG_T_MASK        (0x3 << PMU_CHARGER_CTL2_ICHG_REG_T_SHIFT)
#define     PMU_CHARGER_CTL2_ICHG_REG_T(i)          \
				((((i) / 100) << PMU_CHARGER_CTL2_ICHG_REG_T_SHIFT) & PMU_CHARGER_CTL2_ICHG_REG_T_MASK)

#define		PMU_CHARGER_CTL2_TEMPTH3_SHIFT          (9)
#define		PMU_CHARGER_CTL2_TEMPTH3_MASK		    (0x3 << PMU_CHARGER_CTL2_TEMPTH3_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH3_100			(0 << PMU_CHARGER_CTL2_TEMPTH3_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH3_120			(1 << PMU_CHARGER_CTL2_TEMPTH3_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH3_130			(2 << PMU_CHARGER_CTL2_TEMPTH3_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH3_140			(3 << PMU_CHARGER_CTL2_TEMPTH3_SHIFT)
#define		PMU_CHARGER_CTL2_TEMPTH2_SHIFT			(11)
#define		PMU_CHARGER_CTL2_TEMPTH2_MASK           (0x3 << PMU_CHARGER_CTL2_TEMPTH2_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH2_90			    (0 << PMU_CHARGER_CTL2_TEMPTH2_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH2_105			(1 << PMU_CHARGER_CTL2_TEMPTH2_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH2_120			(2 << PMU_CHARGER_CTL2_TEMPTH2_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH2_135			(3 << PMU_CHARGER_CTL2_TEMPTH2_SHIFT)
#define		PMU_CHARGER_CTL2_TEMPTH1_SHIFT          (13)
#define		PMU_CHARGER_CTL2_TEMPTH1_MASK 		    (0x3 << PMU_CHARGER_CTL2_TEMPTH1_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH1_75			    (0 << PMU_CHARGER_CTL2_TEMPTH1_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH1_90			    (1 << PMU_CHARGER_CTL2_TEMPTH1_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH1_105			(2 << PMU_CHARGER_CTL2_TEMPTH1_SHIFT)
#define 	PMU_CHARGER_CTL2_TEMPTH1_115			(3 << PMU_CHARGER_CTL2_TEMPTH1_SHIFT)

/* PMU_APDS_CTL */
#define APDS_CTL_VBUSCONTROL_EN		(1 << 15)
#define APDS_CTL_VBUS_CONTROL_SEL		(1 << 14)
#define APDS_CTL_VBUS_CUR_LIMITED_SHIFT	(12)
#define APDS_CTL_VBUS_CUR_LIMITED_MASK		(0x3 << APDS_CTL_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL_VBUS_CUR_LIMITED_100MA	(0x0 << APDS_CTL_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL_VBUS_CUR_LIMITED_300MA	(0x1 << APDS_CTL_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL_VBUS_CUR_LIMITED_500MA	(0x2 << APDS_CTL_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL_VBUS_CUR_LIMITED_800MA	(0x3 << APDS_CTL_VBUS_CUR_LIMITED_SHIFT)
#define APDS_CTL_VBUS_VOL_LIMITED_SHIFT	(10)
#define APDS_CTL_VBUS_VOL_LIMITED_MASK		(0x3 << APDS_CTL_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL_VBUS_VOL_LIMITED_4200MV	(0x0 << APDS_CTL_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL_VBUS_VOL_LIMITED_4300MV	(0x1 << APDS_CTL_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL_VBUS_VOL_LIMITED_4400MV	(0x2 << APDS_CTL_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL_VBUS_VOL_LIMITED_4500MV	(0x3 << APDS_CTL_VBUS_VOL_LIMITED_SHIFT)
#define APDS_CTL_VBUSOTG			(1 << 9)
#define APDS_CTL_VBUS_PD			(1 << 2)
#define APDS_CTL_WALL_PD			(1 << 1)

/* PMU_OT_CTL */
#define     PMU_OT_CTL_OT                           (1 << 15)
#define	    PMU_OT_CTL_OT_INT_SET_SHIFT         	(13)
#define		PMU_OT_CTL_OT_INT_SET_MASK         		(0x3 << PMU_OT_CTL_OT_INT_SET_SHIFT)
#define	    PMU_OT_CTL_OT_INT_SET_70         		(0 << PMU_OT_CTL_OT_INT_SET_SHIFT)
#define	    PMU_OT_CTL_OT_INT_SET_90         		(1 << PMU_OT_CTL_OT_INT_SET_SHIFT)
#define	    PMU_OT_CTL_OT_INT_SET_100         		(2 << PMU_OT_CTL_OT_INT_SET_SHIFT)
#define	    PMU_OT_CTL_OT_INT_SET_110         		(3 << PMU_OT_CTL_OT_INT_SET_SHIFT)
#define 	PMU_OT_CTL_OT_INT_EN 			        (1 << 12)
#define 	PMU_OT_CTL_OT_SHUTOFF_EN		        (1 << 11)
#define	    PMU_OT_CTL_OT_SHUTOFF_SET_SHIFT         (9)
#define	    PMU_OT_CTL_OT_SHUTOFF_SET_MASK          (0x3 << PMU_OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	    PMU_OT_CTL_OT_SHUTOFF_SET_100           (0 << PMU_OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	    PMU_OT_CTL_OT_SHUTOFF_SET_120           (1 << PMU_OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	    PMU_OT_CTL_OT_SHUTOFF_SET_130           (2 << PMU_OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define	    PMU_OT_CTL_OT_SHUTOFF_SET_140           (3 << PMU_OT_CTL_OT_SHUTOFF_SET_SHIFT)
#define 	PMU_OT_CTL_OT_EN					    (1 << 8)

/* PMU_AUXADC_CTL1 */
#define		PMU_AUXADC_CTL1_CM_R_SHIFT				(4)
#define		PMU_AUXADC_CTL1_CM_R_MASK				(0x1 << PMU_AUXADC_CTL1_CM_R_SHIFT)
#define		PMU_AUXADC_CTL1_CM_R_10MOHM				(1 << PMU_AUXADC_CTL1_CM_R_SHIFT)
#define		PMU_AUXADC_CTL1_CM_R_20MOHM				(0 << PMU_AUXADC_CTL1_CM_R_SHIFT)

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
enum CONSTANT_CURRENT {
	CONSTANT_CURRENT_50MA,
	CONSTANT_CURRENT_100MA,
	CONSTANT_CURRENT_200MA,
	CONSTANT_CURRENT_400MA,
	CONSTANT_CURRENT_500MA,
	CONSTANT_CURRENT_600MA,
	CONSTANT_CURRENT_800MA,
	CONSTANT_CURRENT_900MA,
	CONSTANT_CURRENT_1000MA,
	CONSTANT_CURRENT_1200MA,
	CONSTANT_CURRENT_1300MA,
	CONSTANT_CURRENT_1400MA,
	CONSTANT_CURRENT_1600MA,
	CONSTANT_CURRENT_1700MA,
	CONSTANT_CURRENT_1800MA,
	CONSTANT_CURRENT_2000MA,
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
static void atc2603c_charger_set_onoff(struct atc260x_dev *atc260x,
	bool enable)
{
	int val;

	if (enable) {
		val = PMU_CHARGER_CTL0_ENCH;
	} else {
		val = 0;
	}

	atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
		PMU_CHARGER_CTL0_ENCH, val);
}

static bool atc2603c_charger_get_onoff(struct atc260x_dev *atc260x)
{
	bool onoff;

	onoff =  atc260x_reg_read(atc260x, ATC2603C_PMU_CHARGER_CTL0) &
			PMU_CHARGER_CTL0_ENCH;
	if (onoff)
		return true;
	else
		return false;
}


static int atc2603c_charger_get_trick_current(struct atc260x_dev *atc260x)
{
	int data;

	data =  (atc260x_reg_read(atc260x, ATC2603C_PMU_CHARGER_CTL2) &
			~PMU_CHARGER_CTL2_ICHG_REG_T_MASK) >> PMU_CHARGER_CTL2_ICHG_REG_T_SHIFT;

	if (data == 0)
		data = 50;
	else
		data *= 100;
	return data;
}

static void atc2603c_charger_set_trick_current(struct atc260x_dev *atc260x,
	enum TRICKLE_CURRENT value)
{
	atc260x_reg_write(atc260x, ATC2603C_PMU_CHARGER_CTL2,
					(atc260x_reg_read(atc260x, ATC2603C_PMU_CHARGER_CTL2) &
					(~PMU_CHARGER_CTL2_ICHG_REG_T_MASK)) |
					PMU_CHARGER_CTL2_ICHG_REG_T(value));
}

static void atc2603c_charger_set_trick_timer(struct atc260x_dev *atc260x,
	enum TRICKLE_TIMER timer)
{
	switch (timer) {
	case (TRICKLE_TIMER_30MIN):
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
		PMU_CHARGER_CTL0_CHARGE_TIMER2_MASK,
		PMU_CHARGER_CTL0_CHARGE_TIMER2_30MIN);
		break;
	case (TRICKLE_TIMER_40MIN):
	atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
		PMU_CHARGER_CTL0_CHARGE_TIMER2_MASK,
		PMU_CHARGER_CTL0_CHARGE_TIMER2_40MIN);
		break;
	case (TRICKLE_TIMER_50MIN):
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER2_MASK,
			PMU_CHARGER_CTL0_CHARGE_TIMER2_50MIN);
		break;
	case (TRICKLE_TIMER_60MIN):
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER2_MASK,
			PMU_CHARGER_CTL0_CHARGE_TIMER2_60MIN);
		break;
	default:
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER2_MASK,
			PMU_CHARGER_CTL0_CHARGE_TIMER2_30MIN);
		break;
	}
}

static void atc2603c_charger_enable_trick(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_TRICKLEEN,
			PMU_CHARGER_CTL0_TRICKLEEN);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_TRICKLEEN, 0);
}

/* static void atc2603c_charger_set_mode(struct atc260x_dev *atc260x,
	int mode)
{
	if (mode == CHARGER_MODE_LINER)
		atc260x_set_bits(atc260x, atc2603c_PMU_SWCHG_CTL3,
			SWCHG_CTL3_CHARGER_MODE_SEL_MASK,
			SWCHG_CTL3_CHARGER_MODE_SEL_LINER);
	else if (mode == CHARGER_MODE_SWITCH)
		 atc260x_set_bits(atc260x, atc2603c_PMU_SWCHG_CTL3,
			SWCHG_CTL3_CHARGER_MODE_SEL_MASK,
			SWCHG_CTL3_CHARGER_MODE_SEL_SWITCH);
	else
		pr_info("%s charger mode value invalid\n", __func__);
} */

/* static enum CHARGER_MODE
	atc2603c_charger_get_mode(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, atc2603c_PMU_SWCHG_CTL3) &
		SWCHG_CTL3_CHARGER_MODE_SEL_MASK;

	if (data)
		return CHARGER_MODE_SWITCH;
	else
		return CHARGER_MODE_LINER;
} */



static int atc2603c_charger_cc_filter(struct atc260x_dev *atc260x,
	int value)
{
	if (value < 100)
		value =  50;
	else if ((value >= 100) && (value < 200))
		value = 100;
	else if ((value >= 200) && (value < 400))
		value = 200;
	else if ((value >= 400) && (value < 500))
		value = 400;
	else if ((value >= 500) && (value < 600))
		value = 500;
	else if ((value >= 600) && (value < 800))
		value = 600;
	else if ((value >= 800) && (value < 900))
		value = 800;
	else if ((value >= 900) && (value < 1000))
		value = 900;
	else if ((value >= 1000) && (value < 1200))
		value = 1000;
	else if ((value >= 1200) && (value < 1300))
		value = 1200;
	else if ((value >= 1300) && (value < 1400))
		value = 1300;
	else if ((value >= 1400) && (value < 1600))
		value = 1400;
	else if ((value >= 1600) && (value < 1700))
		value = 1600;
	else if ((value >= 1700) && (value < 1800))
		value = 1700;
	else if ((value >= 1800) && (value < 2000))
		value = 1800;
	else
		value = 2000;

	return value;
}

static enum CONSTANT_CURRENT
	atc2603c_charger_cc2regval(int value)
{
	if (value < 100)
		return CONSTANT_CURRENT_50MA;
	else if ((value >= 100) && (value < 200))
		return CONSTANT_CURRENT_100MA;
	else if ((value >= 200) && (value < 400))
		return CONSTANT_CURRENT_200MA;
	else if ((value >= 400) && (value < 500))
		return CONSTANT_CURRENT_400MA;
	else if ((value >= 500) && (value < 600))
		return CONSTANT_CURRENT_500MA;
	else if ((value >= 600) && (value < 800))
		return CONSTANT_CURRENT_600MA;
	else if ((value >= 800) && (value < 900))
		return CONSTANT_CURRENT_800MA;
	else if ((value >= 900) && (value < 1000))
		return CONSTANT_CURRENT_900MA;
	else if ((value >= 1000) && (value < 1200))
		return CONSTANT_CURRENT_1000MA;
	else if ((value >= 1200) && (value < 1300))
		return CONSTANT_CURRENT_1200MA;
	else if ((value >= 1300) && (value < 1400))
		return CONSTANT_CURRENT_1300MA;
	else if ((value >= 1400) && (value < 1600))
		return CONSTANT_CURRENT_1400MA;
	else if ((value >= 1600) && (value < 1700))
		return CONSTANT_CURRENT_1600MA;
	else if ((value >= 1700) && (value < 1800))
		return CONSTANT_CURRENT_1700MA;
	else if ((value >= 1800) && (value < 2000))
		return CONSTANT_CURRENT_1800MA;
	else
		return CONSTANT_CURRENT_2000MA;
}

static int atc2603c_charger_get_cc(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2603C_PMU_CHARGER_CTL1)
			& PMU_CHARGER_CTL1_ICHG_REG_CC_MASK;
	data >>= PMU_CHARGER_CTL1_ICHG_REG_CC_SHIFT;

	switch (data) {
	case CONSTANT_CURRENT_50MA:
		return 50;
	case CONSTANT_CURRENT_100MA:
		return 100;
	case CONSTANT_CURRENT_200MA:
		return 200;
	case CONSTANT_CURRENT_400MA:
		return 400;
	case CONSTANT_CURRENT_500MA:
		return 500;
	case CONSTANT_CURRENT_600MA:
		return 600;
	case CONSTANT_CURRENT_800MA:
		return 800;
	case CONSTANT_CURRENT_900MA:
		return 900;
	case CONSTANT_CURRENT_1000MA:
		return 1000;
	case CONSTANT_CURRENT_1200MA:
		return 1200;
	case CONSTANT_CURRENT_1300MA:
		return 1300;
	case CONSTANT_CURRENT_1400MA:
		return 1400;
	case CONSTANT_CURRENT_1600MA:
		return 1600;
	case CONSTANT_CURRENT_1700MA:
		return 1700;
	case CONSTANT_CURRENT_1800MA:
		return 1800;
	case CONSTANT_CURRENT_2000MA:
		return 2000;
	default:
		break;
	}

	return  -EINVAL;
}

void atc2603c_charger_set_cc(struct atc260x_dev *atc260x, int cc)
{
	enum CONSTANT_CURRENT reg;

	reg = atc2603c_charger_cc2regval(cc);

	atc260x_reg_setbits(atc260x, ATC2603C_PMU_CHARGER_CTL1,
		PMU_CHARGER_CTL1_ICHG_REG_CC_MASK,
		reg << PMU_CHARGER_CTL1_ICHG_REG_CC_SHIFT);
}

void atc2603c_charger_set_cc_timer(struct atc260x_dev *atc260x,
	enum CC_TIMER timer)
{
	switch (timer) {
	case CC_TIMER_4H:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_MASK | PMU_CHARGER_CTL0_CHGTIME,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_4H | PMU_CHARGER_CTL0_CHGTIME);
		break;
	case CC_TIMER_6H:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_MASK | PMU_CHARGER_CTL0_CHGTIME,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_6H | PMU_CHARGER_CTL0_CHGTIME);
		break;
	case CC_TIMER_8H:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_MASK | PMU_CHARGER_CTL0_CHGTIME,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_8H | PMU_CHARGER_CTL0_CHGTIME);
		break;
	case CC_TIMER_12H:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_MASK | PMU_CHARGER_CTL0_CHGTIME,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_12H | PMU_CHARGER_CTL0_CHGTIME);
		break;
	default:
		pr_err("%s CCCV TIMER value invalid\n", __func__);
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_MASK | PMU_CHARGER_CTL0_CHGTIME,
			PMU_CHARGER_CTL0_CHARGE_TIMER1_12H | PMU_CHARGER_CTL0_CHGTIME);
		break;
	}

}

static void atc2603c_charger_set_vbus_vol_lmt(struct atc260x_dev *atc260x,
	enum VBUS_VOLTAGE_LMT value)
{
	switch (value) {
	case VBUS_VOL_LIMT_4200MV:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_VOL_LIMITED_MASK,
			APDS_CTL_VBUS_VOL_LIMITED_4200MV);
		break;
	case VBUS_VOL_LIMT_4300MV:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_VOL_LIMITED_MASK,
			APDS_CTL_VBUS_VOL_LIMITED_4300MV);
		break;
	case VBUS_VOL_LIMT_4400MV:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_VOL_LIMITED_MASK,
			APDS_CTL_VBUS_VOL_LIMITED_4400MV);
		break;
	case VBUS_VOL_LIMT_4500MV:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_VOL_LIMITED_MASK,
			APDS_CTL_VBUS_VOL_LIMITED_4500MV);
		break;
	default:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_VOL_LIMITED_MASK,
			APDS_CTL_VBUS_VOL_LIMITED_4300MV);
		break;
	}
}

int atc2603c_charger_get_vbus_vol_lmt(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2603C_PMU_APDS_CTL);
	data = data & APDS_CTL_VBUS_VOL_LIMITED_MASK;

	switch (data) {
	case APDS_CTL_VBUS_VOL_LIMITED_4200MV:
		return 4200;
	case APDS_CTL_VBUS_VOL_LIMITED_4300MV:
		return 4300;
	case APDS_CTL_VBUS_VOL_LIMITED_4400MV:
		return 4400;
	case APDS_CTL_VBUS_VOL_LIMITED_4500MV:
		return 4500;
	default:
		pr_err("%s get vbus voltage limited value err(%x)\n",
			__func__, data);
		return -1;
	}
}

static void atc2603c_charger_set_vbus_current_lmt(struct atc260x_dev *atc260x,
	enum VBUS_CURRENT_LMT value)
{
	switch (value) {
	case VBUS_CURR_LIMT_100MA:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_CUR_LIMITED_MASK,
			APDS_CTL_VBUS_CUR_LIMITED_100MA);
		break;
	case VBUS_CURR_LIMT_300MA:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_CUR_LIMITED_MASK,
			APDS_CTL_VBUS_CUR_LIMITED_300MA);
		break;
	case VBUS_CURR_LIMT_500MA:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_CUR_LIMITED_MASK,
			APDS_CTL_VBUS_CUR_LIMITED_500MA);
		break;
	case VBUS_CURR_LIMT_800MA:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_CUR_LIMITED_MASK,
			APDS_CTL_VBUS_CUR_LIMITED_800MA);
		break;
	default:
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_CUR_LIMITED_MASK,
			APDS_CTL_VBUS_CUR_LIMITED_500MA);
		break;
	}
}

static int atc2603c_charger_get_vbus_current_lmt(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2603C_PMU_APDS_CTL);
	data = data & APDS_CTL_VBUS_CUR_LIMITED_MASK;

	switch (data) {
	case APDS_CTL_VBUS_CUR_LIMITED_100MA:
		return 100;
	case APDS_CTL_VBUS_CUR_LIMITED_300MA:
		return 300;
	case APDS_CTL_VBUS_CUR_LIMITED_500MA:
		return 500;
	case APDS_CTL_VBUS_CUR_LIMITED_800MA:
		return 800;
	default:
		pr_err("%s get vbus current limited value err\n", __func__);
		return -1;
	}
}

static int atc2603c_charger_get_vbus_ctlmode(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2603C_PMU_APDS_CTL);
	if (data & APDS_CTL_VBUS_CONTROL_SEL)
		return CURRENT_LIMITED;
	else
		return VOLTAGE_LIMITED;
}

static void atc2603c_charger_set_vbus_ctlmode(struct atc260x_dev *atc260x,
			enum VBUS_CTL_MODE vbus_control_mode)
{
	if (vbus_control_mode == VOLTAGE_LIMITED)
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_CONTROL_SEL, 0);
	else if (vbus_control_mode == CURRENT_LIMITED)
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_CONTROL_SEL,
			APDS_CTL_VBUS_CONTROL_SEL);
	else
		pr_err("%s vbus ctl mode value invalid\n", __func__);
}

static void atc2603c_charger_set_vbus_ctl_en(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUSCONTROL_EN,
			APDS_CTL_VBUSCONTROL_EN);
	else
		atc260x_reg_setbits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUSCONTROL_EN, 0);
}

static bool atc2603c_charger_get_vbus_ctl_en(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2603C_PMU_APDS_CTL);
	if (data & APDS_CTL_VBUSCONTROL_EN)
		return true;
	else
		return false;
}

static int atc2603c_charger_get_vbus_onoff(struct atc260x_dev *atc260x)
{
	int data;

	data = atc260x_reg_read(atc260x, ATC2603C_PMU_APDS_CTL);
	data &= APDS_CTL_VBUSOTG;
	if (data)
		return 0;
	else
		return 1;
}

static void atc2603c_charger_set_vbus_onoff(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		/*shut off the path from vbus to vbat,
		 when support usb adaptor only.*/
		atc260x_set_bits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUSOTG, 0);
	else {
		atc260x_set_bits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUSOTG, APDS_CTL_VBUSOTG);
	}
}

static void atc2603c_charger_set_wall_pd(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_WALL_PD, APDS_CTL_WALL_PD);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_WALL_PD, 0);
}

static void atc2603c_charger_set_vbus_pd(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_PD, APDS_CTL_VBUS_PD);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_APDS_CTL,
			APDS_CTL_VBUS_PD, 0);
}

static void atc2603c_charger_syspwr_steady(struct atc260x_dev *atc260x,
	bool enable)
{
	atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
		PMU_CHARGER_CTL0_CHGPWR_SET_MASK |
		PMU_CHARGER_CTL0_CHG_SYSPWR_SET,
		PMU_CHARGER_CTL0_CHGPWR_SET_100MV |
		PMU_CHARGER_CTL0_CHG_SYSPWR_SET_4250MV);

	if (!enable)
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHG_SYSPWR, 0);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHG_SYSPWR, PMU_CHARGER_CTL0_CHG_SYSPWR);
}
/* ------------review here!!
static void atc2603c_charger_adjust_current(struct atc260x_dev *atc260x,
	bool enable)
{
	if (enable)
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHG_CURRENT_TEMP,
			PMU_CHARGER_CTL0_CHG_CURRENT_TEMP);
	else
		atc260x_set_bits(atc260x, atc2603c_PMU_SWCHG_CTL1,
			PMU_CHARGER_CTL0_CHG_CURRENT_TEMP, 0);
}
*/
static void atc2603c_charger_set_cv(struct atc260x_dev *atc260x,
	enum BATTERY_TYPE type)
{
	switch (type) {
	case BAT_TYPE_4180MV:
	case BAT_TYPE_4200MV:
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL2,
			PMU_CHARGER_CTL2_CV_SET_MASK,
			PMU_CHARGER_CTL2_CV_SET_4300MV);
		break;
	case BAT_TYPE_4300MV:
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL2,
			PMU_CHARGER_CTL2_CV_SET_MASK,
			PMU_CHARGER_CTL2_CV_SET_4350MV);
		break;
	case BAT_TYPE_4350MV:
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL2,
			PMU_CHARGER_CTL2_CV_SET_MASK,
			PMU_CHARGER_CTL2_CV_SET_4400MV);
		break;
	default:
		pr_err("%s bat type invalid\n", __func__);
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL2,
			PMU_CHARGER_CTL2_CV_SET_MASK,
			PMU_CHARGER_CTL2_CV_SET_4200MV);
		break;
	}

}

static void atc2603c_charger_ot_shutoff(struct atc260x_dev *atc260x,
			int ot_shutoff_enable)
{
	if (!ot_shutoff_enable)
		atc260x_set_bits(atc260x, ATC2603C_PMU_OT_CTL,
			PMU_OT_CTL_OT_SHUTOFF_EN, 0);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_OT_CTL,
			PMU_OT_CTL_OT_SHUTOFF_EN,
			PMU_OT_CTL_OT_SHUTOFF_EN);
}

static void atc2603c_charger_set_otint_en(struct atc260x_dev *atc260x,
			bool otint_enable)
{
	atc260x_set_bits(atc260x, ATC2603C_PMU_OT_CTL,
			PMU_OT_CTL_OT_INT_SET_MASK, PMU_OT_CTL_OT_INT_SET_90);
	if (!otint_enable)
		atc260x_set_bits(atc260x, ATC2603C_PMU_OT_CTL,
			PMU_OT_CTL_OT_INT_EN, 0);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_OT_CTL,
			PMU_OT_CTL_OT_INT_EN, PMU_OT_CTL_OT_INT_EN);
}

static void atc2603c_charger_clear_otint_pending(struct atc260x_dev *atc260x)
{
	atc2603c_charger_set_otint_en(atc260x,0);
	atc260x_set_bits(atc260x, ATC2603C_PMU_OT_CTL,
			PMU_OT_CTL_OT, PMU_OT_CTL_OT);
	pr_debug("[%s]:ot_ctl:0x%x\n", __func__, atc260x_reg_read(atc260x, ATC2603C_PMU_OT_CTL));
	atc2603c_charger_set_otint_en(atc260x,1);
}

/* enable charge current automatically adjust depending on temp */
static void atc2603c_charger_current_temp(struct atc260x_dev *atc260x,
			int  change_current_temp)
{
	if (change_current_temp)
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHG_CURRENT_TEMP,
			PMU_CHARGER_CTL0_CHG_CURRENT_TEMP);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL0,
			PMU_CHARGER_CTL0_CHG_CURRENT_TEMP, 0);
}

static void atc2603c_charger_set_stop_vol(struct atc260x_dev *atc260x,
			enum STOP_VOLTAGE stopv)
{
	if (stopv == STOP_VOLTAGE_4160MV)
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL1,
			PMU_CHARGER_CTL1_STOPV_MASK,
			PMU_CHARGER_CTL1_STOPV_4160MV);
	else
		atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL1,
			PMU_CHARGER_CTL1_STOPV_MASK,
			PMU_CHARGER_CTL1_STOPV_4180MV);
}

static void atc2603c_charger_op_offset_threshold(struct atc260x_dev *atc260x)
{
	atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL2,
		PMU_CHARGER_CTL2_TEMPTH1_MASK |
		PMU_CHARGER_CTL2_TEMPTH2_MASK |
		PMU_CHARGER_CTL2_TEMPTH3_MASK,
		PMU_CHARGER_CTL2_TEMPTH1_90 |
		PMU_CHARGER_CTL2_TEMPTH2_105 |
		PMU_CHARGER_CTL2_TEMPTH3_120);
}

static void atc2603c_charger_init_base(struct atc260x_dev *atc260x)
{
	/*charger init*/
	atc2603c_charger_enable_trick(atc260x, true);
	atc2603c_charger_set_trick_current(atc260x, TRICKLE_CURRENT_200MA);
	atc2603c_charger_set_cc(atc260x, 200);
	atc2603c_charger_set_trick_timer(atc260x, TRICKLE_TIMER_30MIN);
	atc2603c_charger_set_cc_timer(atc260x, CC_TIMER_12H);

	atc2603c_charger_set_cv(atc260x, BAT_TYPE_4200MV);
	atc2603c_charger_set_stop_vol(atc260x, STOP_VOLTAGE_4160MV);
	atc2603c_charger_current_temp(atc260x,  items.change_current_temp);

	atc2603c_charger_ot_shutoff(atc260x, items.ot_shutoff_enable);

	atc2603c_charger_op_offset_threshold(atc260x);
	/* wall init*/
	atc2603c_charger_set_wall_pd(atc260x, true);
	/*vbus init*/
	atc2603c_charger_set_vbus_current_lmt(atc260x, VBUS_CURR_LIMT_500MA);
	atc2603c_charger_set_vbus_vol_lmt(atc260x, VBUS_VOL_LIMT_4300MV);
}

static int atc2603c_check_bat_online(struct atc260x_dev *atc260x)
{
	int data;
	int count = 0;
	int online;

	/* dectect bit 0 > 1 to start dectecting */
	data = atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL1,
	PMU_CHARGER_CTL1_BAT_EXIST_EN, PMU_CHARGER_CTL1_BAT_EXIST_EN);
	if (data < 0)
		return data;

	/* wait bat detect over */
	do {
		msleep(70);
		data = atc260x_reg_read(atc260x, ATC2603C_PMU_CHARGER_CTL1) &
			PMU_CHARGER_CTL1_BAT_DT_OVER;
		pr_info("%s wait battery detect over,data:0x%x\n",
			__func__, data);
		count += 70;
		if (count >= 300)
			break;
	} while (!data);

	data = atc260x_reg_read(atc260x, ATC2603C_PMU_CHARGER_CTL1);
	if (data < 0)
		return data;

	if (data & PMU_CHARGER_CTL1_BAT_EXIST)
		online = 1;
	else
		online = 0;

	/* cleare battery detect bit, otherwise cannot changer */
	data = atc260x_set_bits(atc260x, ATC2603C_PMU_CHARGER_CTL1,
	PMU_CHARGER_CTL1_BAT_EXIST_EN, 0);
	if (data < 0)
		return data;

	pr_info("%s battery exist:%d\n", __func__, online);

	return online;
}

struct atc260x_charger_ops atc2603c_charger_ops = {
	.set_onoff = atc2603c_charger_set_onoff,
	.get_onoff = atc2603c_charger_get_onoff,
	.set_vbus_ctl_en = atc2603c_charger_set_vbus_ctl_en,
	.get_vbus_ctl_en = atc2603c_charger_get_vbus_ctl_en,
	.set_vbus_ctlmode = atc2603c_charger_set_vbus_ctlmode,
	.get_vbus_ctlmode = atc2603c_charger_get_vbus_ctlmode,
	.get_vbus_vol_lmt = atc2603c_charger_get_vbus_vol_lmt,
	.get_vbus_current_lmt = atc2603c_charger_get_vbus_current_lmt,
	.cc_filter = atc2603c_charger_cc_filter,
	.set_cc = atc2603c_charger_set_cc,
	.get_cc = atc2603c_charger_get_cc,
	.get_trick_current = atc2603c_charger_get_trick_current,
	.set_cv = atc2603c_charger_set_cv,
	.set_wall_pd = atc2603c_charger_set_wall_pd,
	.get_vbus_onoff = atc2603c_charger_get_vbus_onoff,
	.set_vbus_onoff = atc2603c_charger_set_vbus_onoff,
	.set_vbus_pd = atc2603c_charger_set_vbus_pd,
	.init_base = atc2603c_charger_init_base,
	.chk_bat_online = atc2603c_check_bat_online,
	.set_syspwr_steady = atc2603c_charger_syspwr_steady,
	.set_otint_en = atc2603c_charger_set_otint_en,
	.clear_otint_pending = atc2603c_charger_clear_otint_pending,
};
