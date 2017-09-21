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

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/compat.h>
#include <linux/cpu_cooling.h>

#include "vce_drv.h"
#include "vce_reg.h"

/* Macros */
#define DEVDRV_NAME_VCE         "vce"	/*device_driver.h */
#define DEVICE_VCE              "/dev/vce"	/*major.h */
#define MINOR_VCE               32
#define _MAX_HANDLES_           1024
#define IRQ_VCE OWL_IRQ_H264_JPEG_ENCODER	/*81 */
#define WAIT_TIMEOUT            HZ	/* 1s <--> HZ */
#define WAIT_TIMEOUT_MS         3000	/* ms */
#define VCE_DEFAULT_WIDTH       320
#define VCE_DEFAULT_HEIGHT      240
#define VCE_DEFAULT_FREQ        240	/* MHZ */
#define VCE_COOL_FREQ           240
#define IC_TYPE_S500            0x500
#define IC_TYPE_S900            0x900
#define IC_TYPE_S700            0x700
#define VOL_INIT                950000
#define VOL_RUG                 1050000
#define VOL_MAX                 1200000
#define VOL_COOL                950000

#define PUT_USER          put_user
#define GET_USER          get_user
#define GET_USER_STRUCT   copy_from_user
#define SET_USER_STRUCT   copy_to_user

#define enable_more_print 0	/* show more print */
#if enable_more_print
#define vce_info(fmt, ...) printk(KERN_WARNING "vce_drv: " fmt, ## __VA_ARGS__);
#else
#define vce_info(fmt, ...) {}
#endif
#define vce_warning(fmt, ...) \
	printk(KERN_WARNING "vce_drv: warning: " fmt, ## __VA_ARGS__)
#define vce_err(fmt, ...) \
	printk(KERN_ERR "vce_drv: %s(L%d) error: " fmt, __func__, __LINE__, ## __VA_ARGS__)

enum {
	STOPED,
	RUNNING,
	ERR,
};

typedef struct {
	vce_input_t vce_in;
	vce_output_t vce_out;
	unsigned int vce_count;
	unsigned int vce_status;
} vce_info_t;

static DEFINE_MUTEX(vce_ioctl_mutex);
static DEFINE_MUTEX(vce_reset_mutex);
static struct completion vce_complete;
static wait_queue_head_t vce_wait;
static int vce_status;
static int vce_irq_registered;
static int vce_open_count;
static int vce_last_handle;
static void *pbuf[_MAX_HANDLES_];	/* multi-instance */
static int vce_power_is_enable;
static int vce_clk_is_enable;
static vce_multi_freq_t vce_freq_multi = {
	VCE_DEFAULT_WIDTH, VCE_DEFAULT_HEIGHT, VCE_DEFAULT_FREQ
};
static int vce_cooling_flag = 0;	/* temperature regulation */

static int vce_freq_is_init;
static int vce_resumed_flag;
static int g_power_on_off_count;
static unsigned int g_vce_status;
static int ic_type = IC_TYPE_S900;
static unsigned int irq_vce = -1;
static unsigned long iobase_vce;	/* VCE_BASE_ADDR */
static unsigned long iobase_cmu_devclken0;	/* CMU_DEVCLKEN0 */
static unsigned long vce_clk_en;
static unsigned long iobase_sps_pg_ctl;	/* SPS_PG_CTL */
static unsigned long vce_pg_en;
static struct regulator *vol_set;	/* voltage regulation */
static struct clk *vce_clk;	/* clk */
static struct reset_control *reset;	/* reset */
static struct device *vce_device;	/* for powergate and clk */


/* FPGA verification flag, manually start vce instead of standard
interface. close it! */
/* #define FPGA_VERIFY 1 */

#ifdef FPGA_VERIFY
static unsigned long iobase_cmu_devrst0;	/* CMU_DEVRST0 */
static unsigned long vce_reset_en;
static unsigned long iobase_sps_pg_ack;	/* SPS_PG_ACK */
static unsigned long iobase_cmu_vceclk;	/* CMU_VCECLK */
#endif


static void *vce_malloc(int32_t size)
{
	return kzalloc(size, GFP_KERNEL | GFP_DMA);
}

static void vce_free(void *ptr)
{
	kfree(ptr);
	ptr = NULL;
}

static unsigned int read_reg(unsigned int reg)
{
	unsigned int value = readl((void *)(iobase_vce + reg));
	return value;
}

static void write_reg(unsigned int reg, unsigned int value)
{
	writel(value, (void *)(iobase_vce + reg));
}

static unsigned int read_reg_self(unsigned long reg)
{
	unsigned int value = readl((void *)(reg));
	return value;
}

static int query_status(void)
{
	int ret_value = 0;
	int vce_status = read_reg(VCE_STATUS);
	g_vce_status = vce_status;

	if ((vce_status & 0x1) && ((vce_status & 0x100) == 0)) {
		ret_value = VCE_BUSY;	/* codec is runing */
	} else if (((vce_status & 0x1) == 0) && (vce_status & 0x100)
		   && (vce_status & 0x1000)) {
		ret_value = VCE_READY;
	} else if (((vce_status & 0x1) == 0) && ((vce_status & 0x100) == 0)) {
		ret_value = VCE_IDLE;
	} else if ((vce_status & 0x100) && (vce_status & 0x8000)) {
		ret_value = VCE_ERR_STM_FULL;
	} else if ((vce_status & 0x100) && (vce_status & 0x10000)) {
		ret_value = VCE_ERR_STM;
	} else if ((vce_status & 0x100) && (vce_status & 0x40000)) {
		ret_value = VCE_ERR_TIMEOUT;
	}
	return ret_value;
}

static void vce_reset(void)
{
	vce_info("vce_reset\n");
	reset_control_reset(reset);
}

static void vce_stop(void)
{
	if (vce_power_is_enable) {
		int i = 0x10000000;
		while ((VCE_BUSY == query_status())) {
			mdelay(1);
			if (i-- < 0)
				break;
			vce_err("OWL_POWERGATE_VCE_BISP is PownOn when VCE_BUSY\n");
		}
	}
}

static void vce_power_on(void)
{
	vce_info("vce_power_on in\n");
	vce_stop();

	if (vce_power_is_enable)
		vce_info("OWL_POWERGATE_VCE_BISP is PownOn\n");

	pm_runtime_get_sync(vce_device);
	g_power_on_off_count++;
	vce_info("vce_power_on out %d\n", g_power_on_off_count);
}

static void vce_power_off(void)
{
	vce_info("vce_power_off in\n");
	if (vce_power_is_enable)
		vce_info("OWL_POWERGATE_VCE_BISP is PownOn\n");

	pm_runtime_put_sync(vce_device);
	g_power_on_off_count--;
	vce_info("vce_power_off out %d\n", g_power_on_off_count);
}

#ifdef FPGA_VERIFY
/* Manually start vce, configure the powergate and clk registers related VCE. each IC
is different, please refer to the appropriate SPEC and modify the code. */

static void write_reg_self(unsigned long reg, unsigned int value)
{
	writel(value, (void *)(reg));
}

static void vce_drv_start(void)
{
	unsigned int value;
	vce_info("%s in\n", __func__);

	switch (ic_type) {
	case IC_TYPE_S900:
		iobase_cmu_devrst0 = (unsigned long)ioremap(0xE01600A8, 4);
		vce_reset_en = 0x00100000;
		iobase_sps_pg_ack = (unsigned long)ioremap(0xE012E004, 4);
		iobase_cmu_vceclk = (unsigned long)ioremap(0xE0160044, 4);
		break;
	case IC_TYPE_S700:
		iobase_cmu_devrst0 = (unsigned long)ioremap(0xE01680A8, 4);
		vce_reset_en = 0x00000800;
		iobase_sps_pg_ack = (unsigned long)ioremap(0xE01B0118, 4);
		iobase_cmu_vceclk = (unsigned long)ioremap(0xE0168044, 4);
		break;
	default:
		vce_err("unsupported ic type!\n");
		goto err;
	}

	vce_info("vce_probe!iobase_cmu_devrst0 = %p!\n",
		 (void *)iobase_cmu_devrst0);
	if (iobase_cmu_devrst0 == 0) {
		vce_err("iobase_cmu_devrst0 is NULL!\n");
		goto err;
	}
	vce_info("vce_probe!iobase_sps_pg_ack = %p!\n",
		 (void *)iobase_sps_pg_ack);
	if (iobase_sps_pg_ack == 0) {
		vce_err("iobase_sps_pg_ack is NULL!\n");
		goto err;
	}
	vce_info("vce_probe!iobase_cmu_vceclk = %p!\n",
		 (void *)iobase_cmu_vceclk);
	if (iobase_cmu_vceclk == 0) {
		vce_err("iobase_cmu_vceclk is NULL!\n");
		goto err;
	}

	value = read_reg_self(iobase_cmu_devrst0);
	value = value & (~vce_reset_en);
	write_reg_self(iobase_cmu_devrst0, value);

	value = read_reg_self(iobase_cmu_devclken0);
	value = value | vce_clk_en;
	write_reg_self(iobase_cmu_devclken0, value);

	value = read_reg_self(iobase_sps_pg_ctl);
	value = value | vce_pg_en;
	write_reg_self(iobase_sps_pg_ctl, value);

loop:
	value = read_reg_self(iobase_sps_pg_ack);
	value = value & vce_pg_en;
	if (value == 0)
		goto loop;

	value = read_reg_self(iobase_cmu_devrst0);
	value = value | vce_reset_en;
	write_reg_self(iobase_cmu_devrst0, value);

	value = 0x2;	/* set VCE_CLK divisor */
	write_reg_self(iobase_cmu_vceclk, value);

	vce_info("%s out\n", __func__);
	return;

err:
	if (iobase_cmu_devrst0 != 0) {
		iounmap((void *)iobase_cmu_devrst0);
		iobase_cmu_devrst0 = 0;
	}
	if (iobase_sps_pg_ack != 0) {
		iounmap((void *)iobase_sps_pg_ack);
		iobase_sps_pg_ack = 0;
	}
	if (iobase_cmu_vceclk != 0) {
		iounmap((void *)iobase_cmu_vceclk);
		iobase_cmu_vceclk = 0;
	}

	vce_err("manually start vce failure!\n");
}

static void vce_drv_close(void)
{
	unsigned int value;
	vce_info("%s in\n", __func__);

	value = read_reg_self(iobase_sps_pg_ctl);
	value = value & (~vce_pg_en);
	write_reg_self(iobase_sps_pg_ctl, value);

	vce_info("%s out\n", __func__);
}
#endif

static void vce_clk_enable(void)
{
#ifndef FPGA_VERIFY
	/* vce reset should be prior to vce clk enable,
	   or vce may work abnormally */
	vce_power_on();

	if ((vce_open_count == 1) || (vce_resumed_flag == 1))
		vce_reset();
	if (vce_open_count == 1) {
		/* the first instance enable powergate */
		vce_power_is_enable = 1;
		vce_info("vce powergate enable!\n");
	}
	clk_prepare_enable(vce_clk);
#else
	if (vce_open_count == 1) {
		vce_drv_start();	/* manually start vce */
		vce_power_is_enable = 1;
		vce_info("vce powergate enable!\n");
	}
#endif

	vce_clk_is_enable = 1;
}

static void vce_clk_disable(void)
{
#ifndef FPGA_VERIFY
	clk_disable_unprepare(vce_clk);

	if (vce_open_count == 0) {
		/* free the last instance, disable powergate */
		vce_power_is_enable = 0;
		vce_info("vce powergate not enable!\n");
	}

	vce_power_off();
#else
	if (vce_open_count == 0) {
		vce_drv_close();	/* manually close vce */
		vce_power_is_enable = 0;
		vce_info("vce powergate not enable!\n");
	}
#endif

	vce_clk_is_enable = 0;
}

/* Frequency regulation, return the frequency value which be successfully set.
   If need to regulate voltage, set a proper voltage value */
static unsigned long vce_set_freq(vce_multi_freq_t *freq_MHZ)
{
	unsigned long rate, new_rate = freq_MHZ->freq;
	unsigned int voltage;
	int ret;

	vce_info("width:%d  height:%d freq:%d\n",
		 freq_MHZ->width, freq_MHZ->height, (int)freq_MHZ->freq);

	if (new_rate > 500) {
		vce_err("cannot set vce freq to : %ld MHz\n", new_rate);
		return -1;
	}

	if (vce_clk_is_enable == 0) {
		vce_warning("vce clk is not enable yet, re-enable!\n");
		vce_clk = devm_clk_get(vce_device, "vce");
		if (IS_ERR(vce_clk)) {
			vce_err("devm_clk_get(&pdev->dev, vce) failed\n");
			return PTR_ERR(vce_clk);
		}
	}

	rate = clk_get_rate(vce_clk);	/* get current frequency, such as:24000000Hz */
	if (rate == new_rate * 1000 * 1000) {
		vce_info
		    ("requested rate (%ld Mhz)is the same as the current rate,do nothing\n",
		     new_rate);
		return new_rate;
	}

	if(!vce_cooling_flag) {
		/* multi-instance frequency */
		if ((vce_open_count > 1) && (vce_freq_is_init == 1)) {
			if (freq_MHZ->width * freq_MHZ->height <
			    vce_freq_multi.width * vce_freq_multi.height) {
				vce_info
				    ("multi instance, vce freq %ld MHz force to %ld MH!\n",
				     new_rate, vce_freq_multi.freq);
				new_rate = vce_freq_multi.freq;
			} else {
				vce_info("multi instance, vce freq %ld MHz\n",
					 new_rate);
				vce_freq_multi.width = freq_MHZ->width;
				vce_freq_multi.height = freq_MHZ->height;
				vce_freq_multi.freq = new_rate;
			}
		} else {
			vce_freq_multi.width = freq_MHZ->width;
			vce_freq_multi.height = freq_MHZ->height;
			vce_freq_multi.freq = new_rate;
			vce_freq_is_init = 1;
		}

		/* Voltage regulation */
		if (!IS_ERR(vol_set)) {
			if (vce_freq_multi.width * vce_freq_multi.height >= 1280 * 720)
				voltage = VOL_RUG;
			else
				voltage = VOL_INIT;

			if (regulator_set_voltage(vol_set, voltage, VOL_MAX)) {
				vce_err("cannot set corevdd to %duV !\n", voltage);
				return -1;
			}
			printk(KERN_NOTICE "vce_drv: voltage regulator is ok!,to %duV.\n", voltage);
		}
	}
	else
	{
		printk(KERN_NOTICE "vce_drv: keeping cool state!\n");
		/* multi-instance frequency */
		if ((vce_open_count > 1) && (vce_freq_is_init == 1)) {
			if (freq_MHZ->width * freq_MHZ->height >
			    vce_freq_multi.width * vce_freq_multi.height) {
				vce_info("keeping cool! multi instance, vce freq %ld MHz\n",
					 new_rate);
				vce_freq_multi.width = freq_MHZ->width;
				vce_freq_multi.height = freq_MHZ->height;
				vce_freq_multi.freq = new_rate;
			} else {
				/* the frequency of new instance is not more than old one, keep
				the decreased value and return. If it is that thermal notifier
				(width = height = 0), go on. */
				if(freq_MHZ->width || freq_MHZ->height)
					return rate;
			}
		}

		/* reduce 1/3 */
		new_rate = new_rate * 2/3;

		/* limit max frequency in cool state */
		if(new_rate > VCE_COOL_FREQ) {
			printk(KERN_NOTICE "vce_drv: keeping cool! limit max frequency %MHz!\n",
				VCE_COOL_FREQ);
			new_rate = VCE_COOL_FREQ;
		}

		/* for cooling CPU temperature, reduce voltage directly */
		if (!IS_ERR(vol_set)) {
			voltage = VOL_COOL;

			if (regulator_set_voltage(vol_set, voltage, VOL_MAX)) {
				vce_err("cannot set corevdd to %duV !\n", voltage);
				return -1;
			}
			printk(KERN_NOTICE "vce_drv: voltage regulator is ok!,to %duV.\n", voltage);
		}
	}

	/* set the "round rate" */
	rate = clk_round_rate(vce_clk, new_rate * 1000 * 1000);
	if (rate > 0) {
		ret = clk_set_rate(vce_clk, rate);	/* set clk hz */
		if (ret != 0) {
			vce_err("clk_set_rate failed\n");
			return -1;
		}
	}

	rate = clk_get_rate(vce_clk);	/*get clk current frequency */
	printk(KERN_NOTICE "vce_drv: new rate (%ld MHz) is set\n", rate / (1000 * 1000));

	return rate / (1000 * 1000);
}

/* get frequency value, MHz */
static unsigned long vce_get_freq(void)
{
	unsigned long rate;

	if (vce_clk_is_enable == 0) {
		vce_warning("vce clk is not enable yet, re-enable!\n");
		vce_clk = devm_clk_get(vce_device, "vce");
		if (IS_ERR(vce_clk)) {
			vce_err("devm_clk_get(&pdev->dev, vce) failed\n");
			return PTR_ERR(vce_clk);
		}
	}

	rate = clk_get_rate(vce_clk);
	vce_info("cur vce freq : %ld MHz\n", rate / (1000 * 1000));

	return rate / (1000 * 1000);
}

/* thermostat function, used to limit frequency and voltage of VCE */
static int vce_thermal_notifier(struct notifier_block *nb, unsigned long event, void *data)
{
	int ret_value = 0;
	vce_multi_freq_t vce_freq = {0, 0, 0};

	printk(KERN_NOTICE "vce_drv: cool CPU! in %s\n", __func__);

	/* decrease frequency for cooling CPU */
	if(event == CPUFREQ_COOLING_START)
	{
		vce_info("CPUFREQ_COOLING_START, %d to %d\n", vce_freq_multi.freq,
			vce_freq_multi.freq * 2/3);
		vce_cooling_flag = 1;
		vce_freq.freq = vce_freq_multi.freq;
	}

	/* restore the normal frequency */
	if(event == CPUFREQ_COOLING_STOP)
	{
		vce_info("CPUFREQ_COOLING_STOP, back to %d\n", vce_freq_multi.freq);
		vce_cooling_flag = 0;
		vce_freq = vce_freq_multi;
	}

	/* adjust the frequency */
	mutex_lock(&vce_ioctl_mutex);
	vce_info("vce_thermal_notifier!(%d)\n", vce_open_count);
	if(vce_open_count > 0)
	{
		ret_value = vce_set_freq(&vce_freq);
	}
	mutex_unlock(&vce_ioctl_mutex);

	return ret_value;
}

/* enable interrupt */
static inline void enable_vce_irq(void)
{
	/* VCE hardware enable interrupt, software only reset it. */
}

static inline void disable_vce_irq(void)
{
	int vce_status;

	vce_status = read_reg(VCE_STATUS);
	vce_status = vce_status & (~0x100);
	write_reg(VCE_STATUS, vce_status);	/* reset interrupt */
	g_vce_status = vce_status;
}

/* This function is vce ISR */
irqreturn_t vce_ISR(int irq, void *dev_id)
{
	if (vce_open_count > 0) {
		disable_vce_irq();
		complete(&vce_complete);
		wake_up_interruptible(&vce_wait);
	}

	return IRQ_HANDLED;
}

/* For compatible with old IC type, we design unified external interface same as
   the old one. so vce_drv need a conversion step to fit new IC type. */
typedef struct {
	unsigned int vce_status;
	unsigned int vce_outstanding;
	unsigned int vce_cfg;
	unsigned int vce_param0;
	unsigned int vce_param1;
	unsigned int vce_strm;
	unsigned int vce_strm_addr;
	unsigned int vce_yaddr;
	unsigned int vce_list0;
	unsigned int vce_list1;
	unsigned int vce_me_param;
	unsigned int vce_swindow;
	unsigned int vce_scale_out;
	unsigned int vce_rect;
	unsigned int vce_rc_param1;
	unsigned int vce_rc_param2;
	unsigned int vce_rc_hdbits;
	unsigned int vce_ts_info;
	unsigned int vce_ts_header;
	unsigned int vce_ts_blu;
	unsigned int vce_ref_dhit;
	unsigned int vce_ref_dmiss;

	unsigned int ups_yas;
	unsigned int ups_cacras;
	unsigned int ups_cras;
	unsigned int ups_ifomat;
	unsigned int ups_ratio;
	unsigned int ups_ifs;
} vce_input_t_compat;

static int set_registers_compat(vce_input_t_compat *vce_in)
{
	write_reg(UPS_YAS, vce_in->ups_yas);
	write_reg(UPS_CBCRAS, vce_in->ups_cacras);
	write_reg(UPS_CRAS, vce_in->ups_cras);
	write_reg(UPS_IFORMAT, vce_in->ups_ifomat);
	write_reg(UPS_RATIO, vce_in->ups_ratio);
	write_reg(UPS_IFS, vce_in->ups_ifs);

	write_reg(VCE_CFG, vce_in->vce_cfg);
	write_reg(VCE_PARAM0, vce_in->vce_param0);
	write_reg(VCE_OUTSTANDING, vce_in->vce_outstanding);
	write_reg(VCE_PARAM1, vce_in->vce_param1);
	write_reg(VCE_STRM, vce_in->vce_strm);
	write_reg(VCE_STRM_ADDR, vce_in->vce_strm_addr);
	write_reg(VCE_YADDR, vce_in->vce_yaddr);
	write_reg(VCE_LIST0_ADDR, vce_in->vce_list0);
	write_reg(VCE_LIST1_ADDR, vce_in->vce_list1);
	write_reg(VCE_ME_PARAM, vce_in->vce_me_param);
	write_reg(VCE_SWIN, vce_in->vce_swindow);
	write_reg(VCE_SCALE_OUT, vce_in->vce_scale_out);
	write_reg(VCE_RECT, vce_in->vce_rect);
	write_reg(VCE_RC_PARAM1, vce_in->vce_rc_param1);
	write_reg(VCE_RC_PARAM2, vce_in->vce_rc_param2);
	write_reg(VCE_TS_INFO, vce_in->vce_ts_info);
	write_reg(VCE_TS_HEADER, vce_in->vce_ts_header);
	write_reg(VCE_TS_BLUHD, vce_in->vce_ts_blu);
	write_reg(VCE_RC_HDBITS, vce_in->vce_rc_hdbits);
	write_reg(VCE_REF_DHIT, vce_in->vce_ref_dhit);
	write_reg(VCE_REF_DMISS, vce_in->vce_ref_dmiss);

	return 0;
}

static void set_color_format_s700(void)
{
	/*601: Q7
	[66  129  25  16
	-38  -74  112  128
	112  -94  -18  128]*/
	write_reg(CSC_COEFF_CFG0, 0x00810042);
	write_reg(CSC_COEFF_CFG1, 0x01DA0019);
	write_reg(CSC_COEFF_CFG2, 0x007001B6);
	write_reg(CSC_COEFF_CFG3, 0x01A20070);
	write_reg(CSC_COEFF_CFG4, 0x000301EE);

	/*709: Q7
	[47  157  16  16
	-26  -87  112  128
	112  -102  -10  128]*/
	/*write_reg(CSC_COEFF_CFG0, 0x009D002F);
	write_reg(CSC_COEFF_CFG1, 0x01E60010);
	write_reg(CSC_COEFF_CFG2, 0x007001A9);
	write_reg(CSC_COEFF_CFG3, 0x019A0070);
	write_reg(CSC_COEFF_CFG4, 0x000301F6);*/

	/*default:
	[77  150  29  0
	-43  -85  128  0
	128  -107  -21  0]*/
	/*write_reg(CSC_COEFF_CFG0, 0x0096004D);
	write_reg(CSC_COEFF_CFG1, 0x01D5001D);
	write_reg(CSC_COEFF_CFG2, 0x008001AB);
	write_reg(CSC_COEFF_CFG3, 0x01950080);
	write_reg(CSC_COEFF_CFG4, 0x000001EB);*/
}

static unsigned int get_ups_ifomat(vce_input_t *vce_in)
{
	unsigned int ups_ifomat = 0;
	unsigned int stride = (vce_in->ups_str & 0x3ff) * 8;
	unsigned int input_fomat = vce_in->input_fomat;
	ups_ifomat = input_fomat | (stride << 16);

	return ups_ifomat;
}

static unsigned int get_ups_ratio(vce_input_t *vce_in)
{
	unsigned int ups_ratio = 0;
	unsigned int ups_ifs = vce_in->ups_ifs;
	unsigned int ups_ofs = vce_in->ups_ofs;
	int srcw = (ups_ifs & 0xffff) * 8;
	int srch = ((ups_ifs >> 16) & 0xffff) * 8;
	int dstw = (ups_ofs & 0xffff) * 16;
	int dsth = ((ups_ofs >> 16) & 0xffff) * 16;

	unsigned int upscale_factor_h_int, upscale_factor_v_int;
	if (dstw <= srcw)
		upscale_factor_h_int = (srcw * 8192) / dstw;
	else
		upscale_factor_h_int = (srcw / 2 - 1) * 8192 / (dstw / 2 - 1);

	if (dsth <= srch)
		upscale_factor_v_int = (srch * 8192) / dsth;
	else
		upscale_factor_v_int = (srch / 2 - 1) * 8192 / (dsth / 2 - 1);

	ups_ratio = (upscale_factor_v_int & 0xffff) << 16 |
		(upscale_factor_h_int & 0xffff);

	return ups_ratio;
}

static void registers_compat(vce_input_t *vce_in,
				   vce_input_t_compat *vce_in_compat)
{
	vce_in_compat->ups_ifomat = get_ups_ifomat(vce_in);
	vce_in_compat->ups_ratio = get_ups_ratio(vce_in);
	vce_in_compat->ups_ifs = vce_in->ups_ifs << 3;
	vce_in_compat->ups_yas = vce_in->ups_yas;
	vce_in_compat->ups_cacras = vce_in->ups_cacras;
	vce_in_compat->ups_cras = vce_in->ups_cras;
	vce_in_compat->vce_outstanding = (1 << 31) | (12 << 16) | (1 << 15) | 128;

	vce_in_compat->vce_status = vce_in->vce_status;
	vce_in_compat->vce_cfg = vce_in->vce_cfg;
	vce_in_compat->vce_param0 = vce_in->vce_param0;
	vce_in_compat->vce_param1 = vce_in->vce_param1;
	vce_in_compat->vce_strm = vce_in->vce_strm;
	vce_in_compat->vce_strm_addr = vce_in->vce_strm_addr;
	vce_in_compat->vce_yaddr = vce_in->vce_yaddr;
	vce_in_compat->vce_list0 = vce_in->vce_list0;
	vce_in_compat->vce_list1 = vce_in->vce_list1;
	vce_in_compat->vce_me_param = vce_in->vce_me_param;
	vce_in_compat->vce_swindow = vce_in->vce_swindow;
	vce_in_compat->vce_scale_out = vce_in->vce_scale_out;
	vce_in_compat->vce_rect = vce_in->vce_rect;
	vce_in_compat->vce_rc_param1 = vce_in->vce_rc_param1;
	vce_in_compat->vce_rc_param2 = vce_in->vce_rc_param2;
	vce_in_compat->vce_ts_info = vce_in->vce_ts_info;
	vce_in_compat->vce_ts_header = vce_in->vce_ts_header;
	vce_in_compat->vce_ts_blu = vce_in->vce_ts_blu;
	vce_in_compat->vce_rc_hdbits = vce_in->vce_rc_hdbits;
	vce_in_compat->vce_ref_dhit = vce_in->vce_ref_dhit;
	vce_in_compat->vce_ref_dmiss = vce_in->vce_ref_dmiss;
}

static int set_registers(vce_input_t *vce_in)
{
	vce_input_t_compat vce_in_compat;
	registers_compat(vce_in, &vce_in_compat);	/* need conversion */

	if (ic_type == IC_TYPE_S700) {
		vce_in_compat.vce_ts_info &= 0xffff00ff;
		vce_in_compat.vce_ts_info |= 0x2200;	/* AWCACHE must be set 0x2200 */
		set_color_format_s700();	/* set color conversion format  */
	}
	set_registers_compat(&vce_in_compat);	/* for new IC type */
	return 0;
}

static int get_registers(vce_output_t *vce_out)
{
	if (ic_type == IC_TYPE_S700)
		vce_out->vce_strm = read_reg(VCE_STRM_LEN);
	else
		vce_out->vce_strm = read_reg(VCE_STRM);
	vce_out->vce_rc_param3 = read_reg(VCE_RC_PARAM3);
	vce_out->vce_rc_hdbits = read_reg(VCE_RC_HDBITS);

	vce_out->strm_addr = read_reg(VCE_STRM_ADDR);
	vce_out->i_ts_offset = read_reg(VCE_TS_INFO);
	vce_out->i_ts_header = read_reg(VCE_TS_HEADER);

	vce_out->vce_ref_dhit = read_reg(VCE_REF_DHIT);
	vce_out->vce_ref_dmiss = read_reg(VCE_REF_DMISS);
	return 0;
}

static void pbuf_release(int vce_count)
{
	int i;
	vce_info_t *info = NULL;
	/* vce support multi-instance, pbuf[i] restore each handle */
	if (vce_count >= 1 && vce_count <= vce_open_count) {
		for (i = vce_count; i < vce_open_count; i++) {
			pbuf[i - 1] = pbuf[i];
			info = (vce_info_t *) pbuf[i - 1];
			info->vce_count--;
		}
		pbuf[vce_open_count - 1] = NULL;

		if (vce_last_handle == vce_count)
			vce_last_handle = 0;
		else if (vce_last_handle > vce_count)
			vce_last_handle--;
	} else {
		vce_warning("vce_count(%d) is out of range(%d)!\n", vce_count,
			    vce_open_count);
	}
};

static void print_all_regs(char *s)
{
	printk(KERN_ERR "%s\n", s);

	printk(KERN_ERR "VCE_ID:%x,VCE_STATUS:%x,VCE_OUTSTANDING:%x,VCE_CFG:%x\n",
	       read_reg(VCE_ID), read_reg(VCE_STATUS), read_reg(VCE_OUTSTANDING),
	       read_reg(VCE_CFG));

	printk(KERN_ERR "VCE_PARAM0:%x,VCE_PARAM1:%x,VCE_STRM:%x\n",
	       read_reg(VCE_PARAM0), read_reg(VCE_PARAM1), read_reg(VCE_STRM));

	printk(KERN_ERR "VCE_STRM_ADDR:%x,VCE_YADDR:%x,VCE_LIST0_ADDR:%x\n",
	       read_reg(VCE_STRM_ADDR), read_reg(VCE_YADDR),
	       read_reg(VCE_LIST0_ADDR));

	printk(KERN_ERR "VCE_LIST1_ADDR:%x,VCE_ME_PARAM:%x,VCE_SWIN:%x\n",
	       read_reg(VCE_LIST1_ADDR), read_reg(VCE_ME_PARAM), read_reg(VCE_SWIN));

	printk(KERN_ERR "VCE_SCALE_OUT:%x,VCE_RECT:%x,VCE_RC_PARAM1:%x\n",
	       read_reg(VCE_SCALE_OUT), read_reg(VCE_RECT), read_reg(VCE_RC_PARAM1));

	printk(KERN_ERR "VCE_RC_PARAM2:%x,VCE_RC_PARAM3:%x,VCE_RC_HDBITS:%x\n",
	       read_reg(VCE_RC_PARAM2), read_reg(VCE_RC_PARAM3),
	       read_reg(VCE_RC_HDBITS));

	printk(KERN_ERR "VCE_TS_INFO:%x,VCE_TS_HEADER:%x,VCE_TS_BLUHD:%x\n",
	       read_reg(VCE_TS_INFO), read_reg(VCE_TS_HEADER),
	       read_reg(VCE_TS_BLUHD));

	printk(KERN_ERR "VCE_REF_DHIT:%x,VCE_REF_DMISS:%x,UPS_YAS:%x\n",
	       read_reg(VCE_REF_DHIT), read_reg(VCE_REF_DMISS),
	       read_reg(UPS_YAS));

	printk(KERN_ERR "UPS_CBCRAS:%x,UPS_CRAS:%x,UPS_IFORMAT:%x\n",
	       read_reg(UPS_CBCRAS), read_reg(UPS_CRAS),
	       read_reg(UPS_IFORMAT));

	printk(KERN_ERR "UPS_RATIO:%x,UPS_IFS:%x\n",
	       read_reg(UPS_RATIO), read_reg(UPS_IFS));
}

long vce_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	vce_info_t *info = (vce_info_t *) filp->private_data;

	int ret_value = 0;
	void __user *from, *to;
	long time_rest = 0;
	int cur_status = 0;
	unsigned long timeout;
	unsigned long expire;
	int left_time;
	vce_multi_freq_t vce_freq;

	if ((read_reg_self(iobase_cmu_devclken0) & vce_clk_en) == 0) {
		vce_err("vce clk is not enabled,CMU_DEVCLKEN0 = %x\n",
			read_reg_self(iobase_cmu_devclken0));
		return VCE_ERR_UNKOWN;
	}

	if ((read_reg_self(iobase_sps_pg_ctl) & vce_pg_en) == 0) {
		vce_err("vce power gate is not enabled,SPS_PG_CTL = %x\n",
			read_reg_self(iobase_sps_pg_ctl));
		return VCE_ERR_UNKOWN;
	}

	switch (cmd) {
	case VCE_CHECK_VERSION:
		{
			mutex_lock(&vce_ioctl_mutex);
			if (read_reg(VCE_ID) != 0x78323634)
				vce_err("VCE_ID ERR....\n");
			mutex_unlock(&vce_ioctl_mutex);
		}
		break;

	case VCE_CMD_ENC_RUN:
		{
			mutex_lock(&vce_ioctl_mutex);
			from = (void __user *)arg;

			/* is timeout? */
			cur_status = query_status();
			time_rest = wait_event_interruptible_timeout(vce_wait,
				(!((g_vce_status & 0x1) == 1) && ((g_vce_status & 0x100) == 0)),
				WAIT_TIMEOUT);

			cur_status = query_status();

			if (time_rest <= 0 && (cur_status == VCE_BUSY)) {
				print_all_regs("vce_dev timeout when runing!reginfo:\n");
				vce_reset();
				mutex_unlock(&vce_ioctl_mutex);
				return VCE_ERR_BUSY;
			} else {
				/* already done, clear all and save irqs */
				if (vce_last_handle >= 1 && vce_last_handle < _MAX_HANDLES_) {
					vce_info_t *pinfo = (vce_info_t *) pbuf[vce_last_handle - 1];
					if (pinfo != NULL) {
						get_registers(&pinfo->vce_out);
						pinfo->vce_status = STOPED;
					}
				}
			}
			if (GET_USER_STRUCT(&info->vce_in, from, sizeof(vce_input_t))) {
				vce_err("copy_from_user fail!\n");
				mutex_unlock(&vce_ioctl_mutex);
				return VCE_ERR_UNKOWN;
			}

			/* init completion var */
			init_completion(&vce_complete);

			/* set registers */
			set_registers(&info->vce_in);

			/* reset code enable bit */
			write_reg(VCE_STATUS, 0);
			vce_status = info->vce_in.vce_status;
			vce_status = (vce_status | 0x1) & (~(0x1 << 8));
			write_reg(VCE_STATUS, vce_status);
			vce_last_handle = info->vce_count;
			info->vce_status = RUNNING;
			mutex_unlock(&vce_ioctl_mutex);
			break;
		}

	case VCE_GET_ENC_STATUS:
		{
			mutex_lock(&vce_ioctl_mutex);
			ret_value = query_status();
			mutex_unlock(&vce_ioctl_mutex);
			break;
		}

	case VCE_CMD_QUERY_FINISH:
		{
			mutex_lock(&vce_ioctl_mutex);
			to = (void __user *)arg;

			if (info->vce_status == STOPED) {
				if (SET_USER_STRUCT(to, &info->vce_out, sizeof(vce_output_t))) {
					vce_err("copy_to_user fail!\n");
					mutex_unlock(&vce_ioctl_mutex);
					return VCE_ERR_UNKOWN;
				}
				mutex_unlock(&vce_ioctl_mutex);
				return 0;
			}

			/* not VCE_BUSY ? */
			ret_value = query_status();

			if (ret_value != VCE_BUSY) {
				get_registers(&info->vce_out);
				if (SET_USER_STRUCT(to, &info->vce_out, sizeof(vce_output_t))) {
					vce_err("copy_to_user fail!\n");
					mutex_unlock(&vce_ioctl_mutex);
					return VCE_ERR_UNKOWN;
				}
				info->vce_status = STOPED;
				mutex_unlock(&vce_ioctl_mutex);
				goto out;
			}

			/* wait until end VCE_BUSY */
			/* method 1, better one */
			timeout = msecs_to_jiffies(WAIT_TIMEOUT_MS) + 1;
			if (vce_irq_registered) {
				enable_vce_irq();
				left_time = wait_for_completion_timeout(&vce_complete, timeout);
				if (unlikely(left_time == 0)) {
					vce_status = read_reg(VCE_STATUS);
					vce_err("time out!\n");

					if (vce_status & 0x100) {
						ret_value = 0;
						get_registers(&info->vce_out);
						if (SET_USER_STRUCT(to, &info->vce_out,
						    sizeof(vce_output_t))) {
							vce_err("copy_to_user fail!\n");
							mutex_unlock(&vce_ioctl_mutex);
							return VCE_ERR_UNKOWN;
						}

						info->vce_status = STOPED;
						disable_vce_irq();
						mutex_unlock(&vce_ioctl_mutex);
						goto out;
					}

					info->vce_status = STOPED;
					ret_value = VCE_ERR_TIMEOUT;
					disable_vce_irq();
					vce_err("timeout when QUERY_FINISH\n");
					vce_reset();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				} else {
					/* normal case */
					ret_value = 0;
					get_registers(&info->vce_out);
					if (SET_USER_STRUCT(to, &info->vce_out,
					    sizeof(vce_output_t))) {
						vce_err("copy_to_user fail!\n");
						mutex_unlock(&vce_ioctl_mutex);
						return VCE_ERR_UNKOWN;
					}

					info->vce_status = STOPED;
					disable_vce_irq();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				}
			}

			/* method 2, no used */
			expire = timeout + jiffies;
			do {
				ret_value = query_status();

				if (ret_value != VCE_BUSY) {
					get_registers(&info->vce_out);
					if (SET_USER_STRUCT(to, &info->vce_out,
					    sizeof(vce_output_t))) {
						vce_err("copy_to_user fail!\n");
						mutex_unlock(&vce_ioctl_mutex);
						return VCE_ERR_UNKOWN;
					}

					info->vce_status = STOPED;
					disable_vce_irq();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				}

				if (time_after(jiffies, expire)) {
					ret_value = VCE_ERR_TIMEOUT;
					info->vce_status = STOPED;
					disable_vce_irq();
					vce_err("timeout when QUERY_FINISH jiffies\n");
					vce_reset();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				}
			} while (1);

			mutex_unlock(&vce_ioctl_mutex);
		}
		break;

	case VCE_SET_DISABLE_CLK:
		vce_info("vce_ioctl get clk disable cmd!\n");
		/*vce_clk_disable(); */
		break;

	case VCE_SET_ENABLE_CLK:
		vce_info("vce_ioctl get clk enable cmd!\n");
		/*vce_clk_enable(); */
		break;

	case VCE_SET_FREQ:
		{
			mutex_lock(&vce_ioctl_mutex);
			vce_info("VCE_SET_FREQ...\n");
			from = (void __user *)arg;
			if (GET_USER_STRUCT(&vce_freq, from, sizeof(vce_multi_freq_t))) {
				vce_err("copy_from_user fail!\n");
				mutex_unlock(&vce_ioctl_mutex);
				return VCE_ERR_UNKOWN;
			}
			ret_value = vce_set_freq(&vce_freq);
			mutex_unlock(&vce_ioctl_mutex);
		}
		break;

	case VCE_GET_FREQ:
		{
			mutex_lock(&vce_ioctl_mutex);
			vce_info("VCE_GET_FREQ...\n");
			ret_value = vce_get_freq();
			mutex_unlock(&vce_ioctl_mutex);
		}
		break;

	default:
		vce_err("no such cmd ...\n");
		return -EIO;
	}

out:
	return ret_value;
}

/* 32-bit compatible processing */
typedef struct {
	compat_int_t width;
	compat_int_t height;
	compat_ulong_t freq;
} compat_vce_multi_freq_t;

#define COMPAT_VCE_SET_FREQ _IOW(VCE_DRV_IOC_MAGIC_NUMBER, 0x5, compat_vce_multi_freq_t)

int compat_get_vce_multi_freq_t(compat_vce_multi_freq_t __user *data32,
				vce_multi_freq_t __user *data)
{
	compat_int_t i;
	compat_ulong_t l;
	int err;

	err = get_user(i, &data32->width);
	err |= put_user(i, &data->width);
	err |= get_user(i, &data32->height);
	err |= put_user(i, &data->height);
	err |= get_user(l, &data32->freq);
	err |= put_user(l, &data->freq);

	return err;
}

long compat_vce_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret_value = 0;

	switch (cmd) {
	case COMPAT_VCE_SET_FREQ:
		{
			compat_vce_multi_freq_t __user *data32;
			vce_multi_freq_t __user *data;
			int err;
			/* 32bits to 64bits */
			data32 = (compat_vce_multi_freq_t __user *) arg;
			data = compat_alloc_user_space(sizeof(*data));
			if (data == NULL) {
				vce_err("compat_alloc_user_space fail!\n");
				return -1;
			}
			err = compat_get_vce_multi_freq_t(data32, data);
			if (err) {
				vce_err("user space 32bit to 64bit fail!\n");
				return err;
			}
			ret_value = vce_ioctl(filp, VCE_SET_FREQ, (unsigned long)data);
			break;
		}

	default:
		ret_value = vce_ioctl(filp, cmd, arg);
		break;
	}
	return ret_value;
}

int vce_open(struct inode *inode, struct file *filp)
{
	vce_info_t *info = NULL;

	int ret_frep;

	mutex_lock(&vce_ioctl_mutex);
	vce_open_count++;
	filp->private_data = NULL;

	if (vce_open_count > _MAX_HANDLES_) {
		vce_open_count--;
		vce_err("max vce_drv_open ...%d.\n", vce_open_count);
		mutex_unlock(&vce_ioctl_mutex);
		return -1;
	}

	info = (vce_info_t *) vce_malloc(sizeof(vce_info_t));
	printk(KERN_NOTICE "vce_drv: vce_open!info:%p,count:%d\n", info, vce_open_count);
	if (info == NULL) {
		vce_open_count--;
		vce_err("vce info malloc failed!...\n");
		mutex_unlock(&vce_ioctl_mutex);
		return -1;
	}

	/* init completion var */
	if (vce_open_count == 1)
		init_completion(&vce_complete);

	vce_clk_enable();	/* clk enable */
	disable_vce_irq();
	vce_info("vce disable_vce_irq ok\n");

	if (vce_open_count == 1) {
		vce_freq_multi.width = VCE_DEFAULT_WIDTH;
		vce_freq_multi.height = VCE_DEFAULT_HEIGHT;
		vce_freq_multi.freq = VCE_DEFAULT_FREQ;
		ret_frep = vce_set_freq(&vce_freq_multi);
		if (ret_frep < 0) {
			vce_freq_is_init = 0;
			vce_open_count--;
			vce_free(info);
			vce_clk_disable();
			mutex_unlock(&vce_ioctl_mutex);
			vce_err("freq_init to %d MHZ fail %d\n",
				VCE_DEFAULT_FREQ, vce_open_count);
			return -1;
		}
		vce_info("freq_init to %d MHZ!!\n", ret_frep);
	}

	pbuf[vce_open_count - 1] = (void *)info;	/* save the instance handle */
	info->vce_count = vce_open_count;	/* the number of instance, starting from 1 */
	info->vce_status = STOPED;
	filp->private_data = (void *)info;
	mutex_unlock(&vce_ioctl_mutex);
	printk(KERN_NOTICE "vce_drv: out of vce_drv_open ...%d.\n",
	       vce_open_count);

	return 0;
}

int vce_release(struct inode *inode, struct file *filp)
{
	vce_info_t *info = (vce_info_t *) filp->private_data;
	printk(KERN_NOTICE "vce_drv: vce_drv_release..count:%d,info:%p\n",
	       vce_open_count, info);

	if (info == NULL) {
		vce_err("Vce Info is Null, return\n");
		return 0;
	}
	mutex_lock(&vce_ioctl_mutex);

	if (vce_open_count >= 1)
		pbuf_release(info->vce_count);

	vce_open_count--;

	if (vce_open_count >= 0) {
		vce_free(info);
		info = filp->private_data = NULL;
	} else if (vce_open_count < 0) {
		vce_err("count %d,%p\n", vce_open_count, info);
		vce_open_count = 0;
	}

	if (vce_open_count == 0) {
		vce_freq_multi.width = VCE_DEFAULT_WIDTH;
		vce_freq_multi.height = VCE_DEFAULT_HEIGHT;
		vce_freq_multi.freq = VCE_DEFAULT_FREQ;
		vce_freq_is_init = 0;

		vce_last_handle = 0;
		vce_stop();
		disable_vce_irq();
	}

	vce_clk_disable();
	mutex_unlock(&vce_ioctl_mutex);

	return 0;
}

/* note:Before goto low-power state, must ensure that VCE had finished
   encoding the current frame. */
int vce_suspend(struct platform_device *dev, pm_message_t state)
{
	vce_info("vce_suspend in %d,%d\n", vce_clk_is_enable, vce_open_count);
	mutex_lock(&vce_ioctl_mutex);
	if ((vce_open_count > 0) && (vce_clk_is_enable == 1)) {
		vce_stop();
		disable_vce_irq();
		vce_clk_disable();
		vce_info("vce_suspend!clk disable!\n");
	}
	disable_irq(irq_vce);
	if (!IS_ERR(vol_set)) {
		regulator_disable(vol_set);
		vce_info("vce_suspend!vdd regulator disable!\n");
	}
	mutex_unlock(&vce_ioctl_mutex);
	vce_info("vce_suspend out %d,%d\n", vce_clk_is_enable, vce_open_count);
	return 0;
}

int vce_resume(struct platform_device *dev)
{
	vce_info("vce_resume in %d,%d\n", vce_clk_is_enable, vce_open_count);
	mutex_lock(&vce_ioctl_mutex);
	vce_resumed_flag = 1;
	if ((vce_open_count > 0) && (vce_clk_is_enable == 0)) {
		vce_clk_enable();
		disable_vce_irq();
	} else {
		/* there may be invalid vce interrupt when cpu resume,
		   though vce module is not enabled; vce isr is crashed due to the
		   non-initialized variable vce_complete; to clear the invalid vce interrupt,
		   enable the vce module first, clear vce pending bit,
		   disable the module at last. */
		vce_reset();
		disable_vce_irq();
	}

	enable_irq(irq_vce);
	if (!IS_ERR(vol_set)) {
		if (regulator_enable(vol_set) != 0) {
			vce_err("vce_resume!vdd regulator err!\n");
			return -1;
		}
		vce_info("vce_resume!vdd regulator enable!\n");
	}
	vce_resumed_flag = 0;
	mutex_unlock(&vce_ioctl_mutex);
	vce_info("vce_resume out %d,%d\n", vce_clk_is_enable, vce_open_count);
	return 0;
}

/* IC type */
struct ic_info {
	int ic_type;
};

static struct ic_info s500_data = {
	.ic_type = IC_TYPE_S500,
};

static struct ic_info s900_data = {
	.ic_type = IC_TYPE_S900,
};

static struct ic_info s700_data = {
	.ic_type = IC_TYPE_S700,
};

static const struct of_device_id owl_vce_of_match[] = {
	{.compatible = "actions,s500-vce", .data = &s500_data},
	{.compatible = "actions,s900-vce", .data = &s900_data},
	{.compatible = "actions,s700-vce", .data = &s700_data},
	{}
};

MODULE_DEVICE_TABLE(of, owl_vce_of_match);

static int vce_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	/* get ic_type */
	const struct of_device_id *id = of_match_device(owl_vce_of_match, &pdev->dev);
	if (id != NULL) {
		struct ic_info *info = (struct ic_info *)id->data;
		if (info != NULL) {
			ic_type = info->ic_type;
			vce_info("ic_type(0x%x)!\n", ic_type);
		} else {
			vce_warning("info is null!\n");
		}
	} else {
		vce_warning("id is null!\n");
	}

	/* get powergate */
	vce_device = &pdev->dev;
	pm_runtime_enable(vce_device);	/* pm enable */
	vce_info("aft powergate ...\n");

	/* get reset */
	reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(reset)) {
		vce_err("devm_reset_control_get(&pdev->dev, NULL) failed\n");
		return PTR_ERR(reset);
	}
	vce_info("aft reset_get ...\n");

	/* get clk */
	vce_clk = devm_clk_get(&pdev->dev, "vce");
	if (IS_ERR(vce_clk)) {
		vce_err("devm_clk_get(&pdev->dev, 'vce') failed\n");
		return PTR_ERR(vce_clk);
	}
	vce_info("aft clk_get ...\n");

	/* get irq */
	irq_vce = platform_get_irq(pdev, 0);
	vce_info("vce->irq =%d\n", irq_vce);
	if (irq_vce < 0)
		return irq_vce;
	ret = devm_request_irq(&pdev->dev, irq_vce, (void *)vce_ISR, 0, "vce_isr", 0);
	if (ret) {
		vce_err("register vce irq failed!...\n");
		return ret;
	} else {
		vce_irq_registered = 1;
	}
	vce_info("aft irq_request ...\n");

	/* get vol regulator */
	vol_set = devm_regulator_get(&pdev->dev, "corevdd");
	vce_info("vol_set:%p\n", vol_set);
	if (IS_ERR(vol_set)) {
		vce_warning
		    ("cann't get vol regulator, may be this board not need, or lost in dts!\n");
	} else {
		if (regulator_set_voltage(vol_set, VOL_INIT, VOL_MAX)) {
			vce_err("cannot set corevdd to %duV !\n", VOL_INIT);
			return -1;
		}
		vce_info("init vdd:%d\n", VOL_INIT);
		if (regulator_enable(vol_set) != 0) {
			vce_err("vdd regulator err!\n");
			return -1;
		}
		vce_info("vdd regulator enable!\n");
	}
	vce_info("aft vol_set ...\n");

	/* get resouce, ioremap */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res != NULL) {
		if (request_mem_region(res->start, resource_size(res), "vce") !=
		    NULL) {
			vce_info("vce_probe!start = %p,size = %d!\n",
				 (void *)res->start, (int)resource_size(res));
			iobase_vce =
			    (unsigned long)ioremap(res->start,
						   resource_size(res));
			vce_info("vce_probe!iobase_vce = %p!\n",
				 (void *)iobase_vce);
			if (iobase_vce == 0) {
				vce_err("iobase_vce is NULL!\n");
				goto err;
			}

			switch (ic_type) {
			case IC_TYPE_S900:
				iobase_cmu_devclken0 =
					(unsigned long)ioremap(0xE01600A0, 4);
				vce_clk_en = 0x04000000;
				iobase_sps_pg_ctl =
					(unsigned long)ioremap(0xE012E000, 4);
				vce_pg_en = 0x00000010;
				break;
			case IC_TYPE_S700:
				iobase_cmu_devclken0 =
					(unsigned long)ioremap(0xE01680A0, 4);
				vce_clk_en = 0x00000800;
				iobase_sps_pg_ctl =
					(unsigned long)ioremap(0xE01B0100, 4);
				vce_pg_en = 0x00000002;
				break;
			default:
				vce_err("unsupported ic type!\n");
				goto err;
			}

			vce_info("vce_probe!iobase_cmu_devclken0 = %p!\n",
				 (void *)iobase_cmu_devclken0);
			if (iobase_cmu_devclken0 == 0) {
				vce_err("iobase_cmu_devclken0 is NULL!\n");
				goto err;
			}

			vce_info("vce_probe!iobase_sps_pg_ctl = %p!\n",
				 (void *)iobase_sps_pg_ctl);
			if (iobase_sps_pg_ctl == 0) {
				vce_err("iobase_sps_pg_ctl is NULL!\n");
				goto err;
			}

		} else {
			vce_err("request_mem_region is fail!\n");
			return -1;
		}
	} else {
		vce_err("res is null!\n");
		return -1;
	}

	return 0;

err:
	if (iobase_vce != 0) {
		iounmap((void *)iobase_vce);
		iobase_vce = 0;
	}
	if (iobase_cmu_devclken0 != 0) {
		iounmap((void *)iobase_cmu_devclken0);
		iobase_cmu_devclken0 = 0;
	}
	if (iobase_sps_pg_ctl != 0) {
		iounmap((void *)iobase_sps_pg_ctl);
		iobase_sps_pg_ctl = 0;
	}
	return -1;
}

static int vce_remove(struct platform_device *pdev)
{
	struct resource *res;

	/* disable vol regulator */
	if (!IS_ERR(vol_set)) {
		regulator_disable(vol_set);
		vce_info("vdd regulator disable!\n");
	}

	/* other resource, such as clk, reset, iqr, vol regulator, use those functions like
	   devm_XXX_get(). Its will be released automatically. */

	/* free resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		vce_info("vce_remove!start = %p,size = %d!\n",
			 (void *)res->start, (int)resource_size(res));
		release_mem_region(res->start, resource_size(res));
	} else {
		vce_warning("res is null!\n");
	}

	vce_info("vce_remove!iobase_vce = %p!\n", (void *)iobase_vce);
	if (iobase_vce != 0) {
		iounmap((void *)iobase_vce);
		iobase_vce = 0;
	}
	if (iobase_cmu_devclken0 != 0) {
		iounmap((void *)iobase_cmu_devclken0);
		iobase_cmu_devclken0 = 0;
	}
	if (iobase_sps_pg_ctl != 0) {
		iounmap((void *)iobase_sps_pg_ctl);
		iobase_sps_pg_ctl = 0;
	}
	return 0;
}

static const struct file_operations vce_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vce_ioctl,
	.compat_ioctl = compat_vce_ioctl,	/*compatable 32_bit */
	.open = vce_open,
	.release = vce_release,
};

#ifndef CONFIG_OF
static void vce_platform_device_release(struct device *dev)
{
	return;
}

static struct platform_device vce_platform_device = {
	.name = DEVDRV_NAME_VCE,
	.id = -1,
	.dev = {
		.release = vce_platform_device_release,
		},

};
#endif

static struct platform_driver vce_platform_driver = {
	.driver = {
		   .name = DEVDRV_NAME_VCE,
		   .owner = THIS_MODULE,
		   .of_match_table = owl_vce_of_match,
		   },
	.probe = vce_probe,
	.remove = vce_remove,
	.suspend = vce_suspend,
	.resume = vce_resume,
};

static struct miscdevice vce_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_VCE,
	.fops = &vce_fops,
};

static struct notifier_block thermal_notifier_block = {
	.notifier_call = vce_thermal_notifier,
};

static int vce_init(void)
{
	int ret;

#ifndef CONFIG_OF
	/* device register (resoure) */
	ret = platform_device_register(&vce_platform_device);
	if (ret) {
		vce_err("register vce platform_device error!...\n");
		goto err0;
	}
#endif

	/* device register (file_operations) */
	ret = misc_register(&vce_miscdevice);
	if (ret) {
		vce_err("register vce misc device failed!...\n");
		goto err1;
	}
	vce_info("vce_init!(ic_type:0x%x)......\n", ic_type);

	/* driver register */
	ret = platform_driver_register(&vce_platform_driver);
	if (ret) {
		vce_err("register vce platform driver error!...\n");
		goto err2;
	}

	init_waitqueue_head(&vce_wait);

	/* register a notifier for cpu cooling */
	cputherm_register_notifier(&thermal_notifier_block, CPUFREQ_COOLING_START);

	return 0;

err2:
#ifndef CONFIG_OF
	platform_device_unregister(&vce_platform_device);
#endif

err1:
	misc_deregister(&vce_miscdevice);

#ifndef CONFIG_OF
err0:
#endif
	return ret;
}

static void vce_exit(void)
{
	vce_info("vce_exit!(ic_type:0x%x)......\n", ic_type);
	cputherm_unregister_notifier(&thermal_notifier_block, CPUFREQ_COOLING_START);
#ifndef CONFIG_OF
	platform_device_unregister(&vce_platform_device);
#endif
	misc_deregister(&vce_miscdevice);
	platform_driver_unregister(&vce_platform_driver);
}

module_init(vce_init);
module_exit(vce_exit);

MODULE_LICENSE("GPL");
