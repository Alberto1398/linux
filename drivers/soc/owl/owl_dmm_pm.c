/*
 * drivers/soc/owl/owl_dmm_pm.c
 *
 * OWL DMM Performance Monitor.
 * Based on "gs705a/kernel/drivers/video/owl/fb/owl-ddr-debug.c".
 *
 * Copyright (C) 2014 Actions Corporation
 * Author: lipeng<lipeng@actions-semi.com>
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

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>

/*
 * Usage:
 *
 * /sys/kernel/debug/dmm_pm
 *	enable--start or stop performace monitor, write 1 start, write 0 stop.
 *	master_info--get all the master we support, include id and name.
 *	master0
 *		id--master id will be monitored, the value can be get from 'master_info'.
 *		mode--0 for read, 1 for write and 2 for read&write.
 *	master1
 *		id
 *		mode
 *	sampling_rate--monitor rate, unit is ms, read/wtite, default is 1000
 *	max_statistic_cnt--max statistic count, read/wtite, default is 1000
 *	statistic_cnt--currrent statistic count, readonly
 *	result--monitor result, readonly.
 *
 * operation flow
 *	0. make sure monitor is disabled (or will cause strange results).
 *		echo 0 > /sys/kernel/debug/dmm_pm/enable
 *	1. set parameters, 'master/id', 'master/mode', sampling_rate etc.
 *		echo 255 > /sys/kernel/debug/dmm_pm/master0/id	# ALL
 *		echo 11 > /sys/kernel/debug/dmm_pm/master1/id	# DE_1
 *	2. enable monitor
 *		echo 1 > /sys/kernel/debug/dmm_pm/enable
 *	3. view result
 *		cat /sys/kernel/debug/dmm_pm/result
 *	4. disable monitor
 *		echo 0 > /sys/kernel/debug/dmm_pm/enable
 *
 * result format
 *
 *	total_bandwidth: 9600MB
 *	sampling_rate: 1000
 *	max_statistic_cnt: 1000
 *	statistic_cnt: 20
 *       master:  mode  BW(MB)  PCT(%)      master:  mode  BW(MB)  PCT(%) 
 *          ALL:    RW    1214    12.6        DE_1:    RW     284     2.9 
 *          ALL:    RW    1768    18.4        DE_1:    RW     298     3.1 
 *          ALL:    RW    1710    17.8        DE_1:    RW     285     2.9 
 *          ALL:    RW    2206    22.9        DE_1:    RW     273     2.8 
 *
 * 	First 4 lines are our parameters, the followings are
 *	every monitor's result(one line for one staticstic):
 *	    master: master name;
 *	    mode: monitor mode, R(read ops), W(write ops), or RW(read/write ops);
 *	    BW(MB): bandwidth in M byte.
 *	    PCT(%): bandwidth percent.
 * 
 *  NOTE: if sampling_rate too large, DMM_PC will overflow, we will print
 *	a warning message, and you must avoid it.
 */

/*===================================================================
			macro and definition
 *=================================================================*/

#define DMM_PM_HW_ID_S700		(0)
#define DMM_PM_HW_ID_S900		(1)

#define MAX_CHANNELS			(2)
#define MAX_MASTERS_PER_CHANNEL		(2)

#define DEFAULT_MAX_STATISTIC_CNT	(1000) /* times */
#define DEFAULT_SIMPLE_RATE		(1000) /* ms */

#define MS_TO_NS(x)			((x) * 1000000)

/*===================================================================
			   strcutures
 *=================================================================*/

struct dmm_pm_master_info {
	/* should stay the same with the ID in DMM_PM_CTRLx */
	const uint32_t		id;	
	const char		*name;
};

struct dmm_pm_pdata {
	const uint32_t		hw_id;

	const uint32_t		nr_channel;
	const uint32_t		nr_master_per_channel;

	/* registers and some bit offsets */
	const uint32_t		reg_dmm_pm_ctrl[MAX_CHANNELS];
	const uint32_t		event;
	const uint32_t		direction;
	const uint32_t		pc_of;
	const uint32_t		count_type;
	const uint32_t		pc_en;
	const uint32_t		bit_per_pc;

	const uint32_t		reg_dmm_pc[MAX_CHANNELS * MAX_MASTERS_PER_CHANNEL];
	const uint32_t		pc_mask;

	/*
	 * All master informations can support by DMM PM driver.
	 *
	 * assume all master info for each channel and counter is same,
	 * or you should define one info for each channel
	 */
	const struct dmm_pm_master_info	*master_info;
	const uint32_t			nr_master;
};


enum owl_dmm_pm_master_mode {
	MASTER_MODE_READ = 0,
	MASTER_MODE_WRITE,
	MASTER_MODE_ALL,
};
const char *name_of_mode[MASTER_MODE_ALL + 1] = {
	"R", "W", "RW",
};


struct owl_dmm_pm_master {
	uint32_t		id;
	uint32_t		mode;

	uint32_t		id_in_channel;
	
	/*
	 * an array to hold staticstic result, one for each channel,
	 * should be alloced at DMM PM enable according to max_statistic_cnt,
	 * and released at DMM PM disable.
	 */
	uint64_t		*pc[MAX_CHANNELS];

	/* a pointer to pdata */
	const struct dmm_pm_pdata *pdata;

	struct dentry		*fs_rootdir;
	struct dentry		*fs_id;
	struct dentry		*fs_mode;
};

#define MASTER_MODE(master)	name_of_mode[master->mode]

static const char *MASTER_ID(struct owl_dmm_pm_master *master)	
{
	int i;

	for (i = 0; i < master->pdata->nr_master; i++) {
		if (master->id == master->pdata->master_info[i].id)
			return master->pdata->master_info[i].name;
	}

	return NULL;
}

/* some useful macro for register bit operation */
#define BIT_OFFSET(master) \
	(master->pdata->bit_per_pc * master->id_in_channel)

#define COUNT_TYPE_BIT(master) \
	(master->pdata->count_type + BIT_OFFSET(master))

#define EVENT_BIT(master)		\
	(master->pdata->event + BIT_OFFSET(master))

#define DIRECTION_BIT(master) \
	(master->pdata->direction + BIT_OFFSET(master))

#define PC_EN_BIT(master) \
	(master->pdata->pc_en + BIT_OFFSET(master))

#define PC_OF_BIT(master) \
	(master->pdata->pc_of + BIT_OFFSET(master))


struct owl_dmm_pm {
	struct platform_device	*pdev;
	void __iomem		*base;

	struct clk		*ddr_clk;

	const struct dmm_pm_pdata *pdata;

	/* masters can support at the same time */
	struct owl_dmm_pm_master *masters;
	uint32_t		nr_master;

	uint32_t		enable;

	uint32_t		sampling_rate;
	struct hrtimer		timer;	

	uint32_t		max_statistic_cnt;
	uint32_t		statistic_cnt;

	/* total DDR bandwith(all channel) in M byte */
	uint32_t		total_bandwidth;

	bool			actived;

	/*
	 * followings are for debugfs
	 */
	struct dentry		*fs_rootdir;
	struct dentry		*fs_enable;
	struct dentry		*fs_sampling_rate;
	struct dentry		*fs_max_statistic_cnt;
	struct dentry		*fs_statistic_cnt;

	struct dentry		*fs_master_info;

	struct dentry		*fs_result;
};


/*===================================================================
		arch-dependent master information
 *=================================================================*/

#define MASTER_ID_DCU_LOAD	(0xfe)
#define MASTER_ID_ALL		(0xff)

static const struct dmm_pm_master_info s900_master_info[] = {
	{0x0,	"CPU"},
	{0x1,	"DE_W"},
	{0x2,	"ETH"},
	{0x3,	"DE_0"},
	{0x4,	"VCE"},
	{0x5,	"USB2_0"},
	{0x6,	"USB2_1"},
	{0x7,	"IMX"},
	{0x8,	"SE"},
	{0xa,	"HDE"},
	{0xb,	"DE_1"},
	{0xc,	"VDE"},
	{0xd,	"BISP"},
	{0xe,	"DMA"},
	{0xf,	"USB3"},
	{0x10,	"G3D_0_1"},
	{0x11,	"G3D_MMU"},
	{0x12,	"G3D_N_MMU"},
	{0x20,	"G3D_0"},
	{0x30,	"G3D_1"},

	{MASTER_ID_ALL, "ALL"},	
};

static const struct dmm_pm_pdata s900_pdata = {
	.hw_id			= DMM_PM_HW_ID_S900,

	.nr_channel		= 2,
	.nr_master_per_channel	= 2,

	.reg_dmm_pm_ctrl	= {
		0x48, 0x4c
	},
	.event			= 0,
	.direction		= 8,
	.pc_of			= 10,
	.count_type		= 12,
	.pc_en			= 15,
	.bit_per_pc		= 16,

	.reg_dmm_pc		= {
		0x50, 0x54, 0x58, 0x5c
	},
	.pc_mask		= 0x0FFFFFFF,

	.master_info = s900_master_info,
	.nr_master = sizeof(s900_master_info)
			/ sizeof(struct dmm_pm_master_info),
};

static const struct dmm_pm_master_info s700_master_info[] = {
	{0x0,	"DE"},
	{0x1,	"GPU_0"},
	{0x2,	"GPU_1"},
	{0x3,	"USB20_1"},
	{0x4,	"USB20_2"},
	{0x5,	"USB20_3"},
	{0x6,	"USB20_4"},
	{0x7,	"HDE"},
	{0x8,	"DMA_MEM"},
	{0x9,	"ETH"},
	{0xa,	"USB30"},
	{0xb,	"SEC"},
	{0xc,	"SI_TVIN"},
	{0xd,	"VCE"},
	{0xe,	"VDE"},
	{0xf,	"DAP_AHB"},

	/* these guys are special ID, do not change it */
	{MASTER_ID_DCU_LOAD, "DCU_LOAD"},
	{MASTER_ID_ALL, "ALL"},
};

static const struct dmm_pm_pdata s700_pdata = {
	.hw_id			= DMM_PM_HW_ID_S700,

	.nr_channel		= 1,
	.nr_master_per_channel	= 2,

	.reg_dmm_pm_ctrl	= {
		0x48
	},
	.event			= 0,
	.direction		= 4,
	.pc_of			= 6,
	.count_type		= 8,
	.pc_en			= 15,
	.bit_per_pc		= 16,

	.reg_dmm_pc		= {
		0x50, 0x54
	},
	.pc_mask		= 0x0FFFFFFF,

	.master_info = s700_master_info,
	.nr_master = sizeof(s700_master_info)
			/ sizeof(struct dmm_pm_master_info),
};

/*===================================================================
		 DMM PM internal realization
 *=================================================================*/

static inline void dmm_pm_write_reg(struct owl_dmm_pm *dmm_pm,
				const uint16_t index, uint32_t val)
{
	writel(val, dmm_pm->base + index);
}

static inline uint32_t dmm_pm_read_reg(struct owl_dmm_pm *dmm_pm,
				const uint16_t index)
{
	return readl(dmm_pm->base + index);
}

static void dmm_pm_start_statistic(struct owl_dmm_pm *dmm_pm) 
{
	int i, j;
	int temp = 0;
	uint32_t reg_dmm_pm_ctrl;

	struct owl_dmm_pm_master *master;
	
	const struct dmm_pm_pdata *pdata = dmm_pm->pdata;

	for (i = 0; i < pdata->nr_channel; i++) {
		reg_dmm_pm_ctrl = pdata->reg_dmm_pm_ctrl[i];

		/* WRITE 0 TO CLEAR PC */
		dmm_pm_write_reg(dmm_pm, reg_dmm_pm_ctrl, 0);
	}

	/* master0: PC0/2, master1: PC1/3, etc. */
	for (j = 0; j < dmm_pm->nr_master; j++) {
		master = &dmm_pm->masters[j];

		for (i = 0; i < pdata->nr_channel; i++) {
			reg_dmm_pm_ctrl = pdata->reg_dmm_pm_ctrl[i];

			temp = dmm_pm_read_reg(dmm_pm, reg_dmm_pm_ctrl);

			if (master->id == MASTER_ID_ALL)
				temp |= (1 << COUNT_TYPE_BIT(master));
			else if (master->id == MASTER_ID_DCU_LOAD)
				temp |= (2 << COUNT_TYPE_BIT(master));
			else
				temp |= (master->id << EVENT_BIT(master));

			temp |= (master->mode << DIRECTION_BIT(master));
			temp |= (1 << PC_EN_BIT(master));

			dmm_pm_write_reg(dmm_pm, reg_dmm_pm_ctrl, temp);
		}
	}
}

static void dmm_pm_get_statistic_result(struct owl_dmm_pm *dmm_pm) 
{
	int i, j;

	uint32_t reg_dmm_pc, reg_dmm_pm_ctrl;

	uint64_t *pc;

	struct owl_dmm_pm_master *master;
	
	const struct dmm_pm_pdata *pdata = dmm_pm->pdata;

	if (!dmm_pm->actived) {
		dev_err(&dmm_pm->pdev->dev, "DMM PM is inactive\n");
		return;
	}

	/* we will run max_statistic_cnt + 1 times */
	if (dmm_pm->statistic_cnt > dmm_pm->max_statistic_cnt) {
		dev_err(&dmm_pm->pdev->dev,
			"out of memory, you set max_statistic cnt is %d \n",
			dmm_pm->max_statistic_cnt);
		return;
	}

	/* master0: PC0/2, master1: PC1/3 etc. */
	for (i = 0; i < dmm_pm->nr_master; i++) {
		master = &dmm_pm->masters[i];

		for (j = 0; j < pdata->nr_channel; j++) {
			reg_dmm_pm_ctrl = pdata->reg_dmm_pm_ctrl[pdata->nr_channel];
			reg_dmm_pc = pdata->reg_dmm_pc[i + j * pdata->nr_channel];

			/* this time get last time's data */
			pc = master->pc[j] + dmm_pm->statistic_cnt - 1;

			*pc = (dmm_pm_read_reg(dmm_pm, reg_dmm_pc) & pdata->pc_mask);
			if (dmm_pm_read_reg(dmm_pm, reg_dmm_pm_ctrl)
				& (1 << PC_OF_BIT(master))) {
				dev_warn(&dmm_pm->pdev->dev, "pc overflow!!\n");
				*pc += pdata->pc_mask;
			}
		}
	}
}

static enum hrtimer_restart dmm_pm_timer_func(struct hrtimer *timer)
{
	unsigned long missed;

	struct owl_dmm_pm *dmm_pm = container_of(timer, struct owl_dmm_pm, timer);

	if (dmm_pm->statistic_cnt != 0)
		dmm_pm_get_statistic_result(dmm_pm);
		
	dmm_pm_start_statistic(dmm_pm);

	dmm_pm->statistic_cnt++;
	
	/* we will run max_statistic_cnt + 1 times */
	if (dmm_pm->statistic_cnt > dmm_pm->max_statistic_cnt)
 		return HRTIMER_NORESTART;

	missed = hrtimer_forward_now(timer,
			ktime_set(0, MS_TO_NS(dmm_pm->sampling_rate)));

	if (missed > 1)
		pr_info("%s: Missed ticks %ld\n", __func__, missed - 1);

 	return HRTIMER_RESTART;
}

/*
 * initialize struct owl_dmm_pm according to dmm_pm_pdata
 */
static int dmm_pm_initialize(struct owl_dmm_pm *dmm_pm)
{
	int i;

	dmm_pm->sampling_rate = DEFAULT_SIMPLE_RATE;
	hrtimer_init(&dmm_pm->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);   
	dmm_pm->timer.function = dmm_pm_timer_func;

	dmm_pm->max_statistic_cnt = DEFAULT_MAX_STATISTIC_CNT;
	dmm_pm->statistic_cnt = 0;

	dmm_pm->enable = 0;
	dmm_pm->actived = false;

	/* count all channel together */
	dmm_pm->nr_master = dmm_pm->pdata->nr_master_per_channel;

	dmm_pm->masters = devm_kzalloc(&dmm_pm->pdev->dev, dmm_pm->nr_master
				* sizeof(struct owl_dmm_pm_master), GFP_KERNEL);

	if (dmm_pm->masters == NULL)
		return -ENOMEM;

	for (i = 0; i < dmm_pm->nr_master; i++) {
		dmm_pm->masters[i].id = 0;
		dmm_pm->masters[i].mode = MASTER_MODE_ALL;

		dmm_pm->masters[i].pdata = dmm_pm->pdata;

		dmm_pm->masters[i].id_in_channel = i;
	}

	return 0;
}

static void dmm_pm_deinitialize(struct owl_dmm_pm *dmm_pm)
{

}

static int dmm_pm_enable(struct owl_dmm_pm *dmm_pm, bool enable)
{
	int i, j;

	struct device *dev = &dmm_pm->pdev->dev;

	struct owl_dmm_pm_master *master;

	if (enable && !dmm_pm->actived) {
		/* alloc results memmory for all master */
		for (i = 0; i < dmm_pm->nr_master; i++) {
			master = &dmm_pm->masters[i];

			for (j = 0; j < dmm_pm->pdata->nr_channel; j++) {
				if (master->pc[j] != NULL)
					kfree(master->pc[j]);

				master->pc[j] = kmalloc(sizeof(*(master->pc[j]))
					* dmm_pm->max_statistic_cnt, GFP_KERNEL);

				if (master->pc[j] == NULL) {
					dev_err(dev, "%s: kmalloc failed\n",
						__func__);
					return -ENOMEM;
				}
			}
		}

		hrtimer_start(&dmm_pm->timer,
				ktime_set(0, MS_TO_NS(dmm_pm->sampling_rate)),
				HRTIMER_MODE_REL);
		dmm_pm->actived = true;
	} else if (!enable && dmm_pm->actived) {
		hrtimer_cancel(&dmm_pm->timer);
		dmm_pm->actived = false;

		/* free all results memmory */
		for (i = 0; i < dmm_pm->nr_master; i++) {
			master = &dmm_pm->masters[i];

			for (j = 0; j < dmm_pm->pdata->nr_channel; j++) {
				if (master->pc[j] != NULL) {
					kfree(master->pc[j]);
					master->pc[j] = NULL;
				}
			}
		}

		dmm_pm->statistic_cnt = 0;
	} else {
		dev_info(dev, "%s: already %s\n", __func__,
			enable ? "enabled" : "disabled");
	}

	return 0;
}

/*===================================================================
		 DMM PM debugfs realization
 *=================================================================*/

static int dmm_pm_simple_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

/*
 * file operations for "dmm_pm/enable"
 */
static ssize_t dmm_pm_enable_read(struct file *filp, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	char buf[3];

	struct owl_dmm_pm *dmm_pm = filp->private_data;

	if (dmm_pm->enable == 1)
		buf[0] = 'Y';
	else
		buf[0] = 'N';

	buf[1] = '\n';
	buf[2] = 0x00;

	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t dmm_pm_enable_write(struct file *filp, const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;

	struct owl_dmm_pm *dmm_pm = filp->private_data;

	struct device *dev = &dmm_pm->pdev->dev;
	
	buf_size = min(count, (sizeof(buf) - 1));
	
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	switch (buf[0]) {
	case 'y':
	case 'Y':
	case '1':
		dmm_pm->enable = 1;
		break;

	case 'n':
	case 'N':
	case '0':
		dmm_pm->enable = 0;
		break;
	}
	

	dmm_pm_enable(dmm_pm, dmm_pm->enable == 1);

	dev_info(dev, "dmm pm enable %d \n", dmm_pm->enable);

	return count;
}

static struct file_operations dmm_pm_enable_fops = {
	.open = dmm_pm_simple_open,
	.read = dmm_pm_enable_read,
	.write = dmm_pm_enable_write,
};


/*
 * file operations for "dmm_pm/result"
 */
static ssize_t dmm_pm_result_read(struct file *filp, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	char *buf;
	int i, j, k, ret = 0;
	ssize_t out_count = PAGE_SIZE * 30, offset = 0;

	uint32_t bandwidth, bandwidth_percent;

	struct owl_dmm_pm *dmm_pm = filp->private_data;
	struct owl_dmm_pm_master *master;

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;	

	if (!dmm_pm->actived) {
		offset += snprintf(buf + offset, out_count - offset,
					"DMM PM is not enabled!!\n");	
		goto out;
	}

	if (dmm_pm->statistic_cnt == 0) {
		offset += snprintf(buf + offset, out_count - offset,
					"NO DATA\n");	
		goto out;
	}

	dmm_pm->total_bandwidth = clk_get_rate(dmm_pm->ddr_clk) / 1000 / 1000;
	dmm_pm->total_bandwidth *= 8;	/* dual edge and 32bit */
	dmm_pm->total_bandwidth = dmm_pm->total_bandwidth
					* dmm_pm->sampling_rate / 1000;
	dmm_pm->total_bandwidth *= dmm_pm->pdata->nr_channel;

	offset += snprintf(buf + offset, out_count - offset,
			"total_bandwidth: %dMB\n", dmm_pm->total_bandwidth);	
	offset += snprintf(buf + offset, out_count - offset,
			"sampling_rate: %d\n", dmm_pm->sampling_rate);	
	offset += snprintf(buf + offset, out_count - offset,
			"max_statistic_cnt: %d\n", dmm_pm->max_statistic_cnt);	
	offset += snprintf(buf + offset, out_count - offset,
			"statistic_cnt: %d\n", dmm_pm->statistic_cnt);	
		
	for (i = 0; i < dmm_pm->nr_master; i++) {
		master = &dmm_pm->masters[i];

		offset += snprintf(buf + offset, out_count - offset,
					"%12s:", "master");	
		offset += snprintf(buf + offset, out_count - offset,
					"%6s", "mode");
		offset += snprintf(buf + offset, out_count - offset,
					"%8s", "BW(MB)");
		offset += snprintf(buf + offset, out_count - offset,
					"%8s", "PCT(%)");
	}
	offset += snprintf(buf + offset, out_count - offset, " \n");	

	/* lastest statistic is not ready, so use dmm_pm->statistic_cnt - 1 */
	for (i = 0; i < dmm_pm->statistic_cnt - 1; i++) {
		for (j = 0; j < dmm_pm->nr_master; j++) {
			master = &dmm_pm->masters[j];

			bandwidth = 0;
			for (k = 0; k < dmm_pm->pdata->nr_channel; k++)
				bandwidth += (*(master->pc[k] + i)
						* 16 / (1024 * 1024));

			/* percent * 10 */
			bandwidth_percent = bandwidth * 1000
					/ dmm_pm->total_bandwidth;

			/*
			 * DCU IDLE = idle clock cycle / dmm clk
			 * DCU LOAD = 1 - DCU IDLE
			 */
			if (master->id == MASTER_ID_DCU_LOAD)
				bandwidth_percent = 1000 - bandwidth_percent;

			offset += snprintf(buf + offset, out_count - offset,
					"%12s:", MASTER_ID(master));
			offset += snprintf(buf + offset, out_count - offset,
					"%6s", MASTER_MODE(master));
			offset += snprintf(buf + offset, out_count - offset,
					"%8d", bandwidth);
			offset += snprintf(buf + offset, out_count - offset,
					"%6d.%1d", bandwidth_percent / 10,
					bandwidth_percent % 10);
		}
		offset += snprintf(buf + offset, out_count - offset, " \n");	
	}

out:
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, offset);

	kfree(buf);

	return ret;
}


static struct file_operations dmm_pm_result_fops = {
	.open = dmm_pm_simple_open,
	.read = dmm_pm_result_read,
	.llseek = default_llseek,
};

/*
 * file operations for "dmm_pm/master_info"
 */
static ssize_t dmm_pm_master_info_read(struct file *filp, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	char *buf;
	int i, ret = 0;
	ssize_t out_count = PAGE_SIZE * 30, offset = 0;

	struct owl_dmm_pm *dmm_pm = filp->private_data;
	const struct dmm_pm_pdata *pdata = dmm_pm->pdata;

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;	
		
	offset += snprintf(buf + offset, out_count - offset,
				"%12s", "name");	
	offset += snprintf(buf + offset, out_count - offset,
				"%6s", "id\n");

	for (i = 0; i < pdata->nr_master; i++) {
		offset += snprintf(buf + offset, out_count - offset,
				"%12s", pdata->master_info[i].name);
		offset += snprintf(buf + offset, out_count - offset,
				"%6d\n", pdata->master_info[i].id);
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, offset);

	kfree(buf);	

	return ret;
}


static struct file_operations dmm_pm_master_info_fops = {
	.open = dmm_pm_simple_open,
	.read = dmm_pm_master_info_read,
	.llseek = default_llseek,
};


static int dmm_pm_create_debugfs(struct owl_dmm_pm *dmm_pm)
{
	int ret = 0;
	int i;

	struct device *dev = &dmm_pm->pdev->dev;

	struct owl_dmm_pm_master *master;
	char buf[32];

	dmm_pm->fs_rootdir = debugfs_create_dir("dmm_pm", NULL);
	if (IS_ERR(dmm_pm->fs_rootdir)) {
		ret = PTR_ERR(dmm_pm->fs_rootdir);
		dmm_pm->fs_rootdir = NULL;
		goto out;
	}

	dmm_pm->fs_enable = debugfs_create_file("enable", S_IRUGO | S_IWUSR,
					dmm_pm->fs_rootdir, dmm_pm,
					&dmm_pm_enable_fops); 
	if (IS_ERR(dmm_pm->fs_enable)){
		ret = PTR_ERR(dmm_pm->fs_enable);
		dmm_pm->fs_enable = NULL;
		dev_err(dev, "%s: create enable failed\n", __func__);
		goto out;
	}

	dmm_pm->fs_sampling_rate
		= debugfs_create_u32("sampling_rate", S_IRUGO | S_IWUSR,
				dmm_pm->fs_rootdir, &dmm_pm->sampling_rate);
	dmm_pm->fs_max_statistic_cnt
		= debugfs_create_u32("max_statistic_cnt", S_IRUGO | S_IWUSR,
				dmm_pm->fs_rootdir, &dmm_pm->max_statistic_cnt);
	dmm_pm->fs_statistic_cnt
		= debugfs_create_u32("statistic_cnt", S_IRUGO | S_IWUSR,
				dmm_pm->fs_rootdir, &dmm_pm->statistic_cnt);
	if (IS_ERR(dmm_pm->fs_sampling_rate)
	|| IS_ERR(dmm_pm->fs_max_statistic_cnt)
	|| IS_ERR(dmm_pm->fs_statistic_cnt)) {
		ret = -EIO;
		dmm_pm->fs_sampling_rate = NULL;
		dmm_pm->fs_max_statistic_cnt = NULL;
		dmm_pm->fs_statistic_cnt = NULL;
		goto out;
	}

	dmm_pm->fs_result = debugfs_create_file("result", S_IRUGO,
					dmm_pm->fs_rootdir, dmm_pm,
					&dmm_pm_result_fops); 
	if (IS_ERR(dmm_pm->fs_result)){
		ret = PTR_ERR(dmm_pm->fs_result);
		dmm_pm->fs_result = NULL;
		dev_err(dev, "%s: create result failed\n", __func__);
		goto out;
	}

	dmm_pm->fs_master_info = debugfs_create_file("master_info", S_IRUGO,
					dmm_pm->fs_rootdir, dmm_pm,
					&dmm_pm_master_info_fops); 
	if (IS_ERR(dmm_pm->fs_master_info)) {
		ret = PTR_ERR(dmm_pm->fs_master_info);
		dmm_pm->fs_master_info = NULL;
		dev_err(dev, "%s: create master_info failed\n", __func__);
		goto out;
	}

	for (i = 0; i < dmm_pm->nr_master; i++) {
		master = &dmm_pm->masters[i];
		sprintf(buf, "master%d", master->id_in_channel);

		master->fs_rootdir = debugfs_create_dir(buf, dmm_pm->fs_rootdir);
		if (IS_ERR(master->fs_rootdir)) {
			ret = PTR_ERR(master->fs_rootdir);
			master->fs_rootdir = NULL;
			goto out;
		}

		master->fs_id = debugfs_create_u32("id", S_IRUGO | S_IWUSR,
						master->fs_rootdir, &master->id);
		master->fs_mode = debugfs_create_u32("mode", S_IRUGO | S_IWUSR,
						master->fs_rootdir, &master->mode);
		if (IS_ERR(master->fs_id) || IS_ERR(master->fs_mode)) {
			ret = -EIO;
			master->fs_id = NULL;
			master->fs_mode = NULL;
			goto out;
		}
	}

	dev_info(dev, "%s success\n", __func__);   

out:
	if (ret)
		dev_warn(dev, "%s failed\n", __func__);   

	return ret;
}

static void dmm_pm_remove_debugfs(struct owl_dmm_pm *dmm_pm)
{
	int i;

	struct owl_dmm_pm_master *master;

	for (i = 0; i < dmm_pm->nr_master; i++) {
		master = &dmm_pm->masters[i];

		debugfs_remove(master->fs_id);
		debugfs_remove(master->fs_mode);

		debugfs_remove(master->fs_rootdir);
	}

	debugfs_remove(dmm_pm->fs_sampling_rate);
	debugfs_remove(dmm_pm->fs_max_statistic_cnt);
	debugfs_remove(dmm_pm->fs_statistic_cnt);

	debugfs_remove(dmm_pm->fs_master_info);
	debugfs_remove(dmm_pm->fs_result);
	debugfs_remove(dmm_pm->fs_enable);
	debugfs_remove(dmm_pm->fs_rootdir);
}
	
/*===================================================================
			platform driver
 *=================================================================*/

static struct of_device_id owl_dmm_pm_of_match[] = {
	{
		.compatible	= "actions,s900-dmm-pm",
		.data		= &s900_pdata,
	},
	{
		.compatible	= "actions,s700-dmm-pm",
		.data		= &s700_pdata,
	},
	{},
};

static int owl_dmm_pm_probe(struct platform_device *pdev)
{
	int ret = 0;

	struct owl_dmm_pm *dmm_pm;

	struct device *dev = &pdev->dev;
	const struct of_device_id *match;

	struct resource *res;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(owl_dmm_pm_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	dmm_pm = devm_kzalloc(dev, sizeof(*dmm_pm), GFP_KERNEL);
	if (!dmm_pm)
		return -ENOMEM;
	dev_set_drvdata(dev, dmm_pm);

	dmm_pm->pdev = pdev;
	dmm_pm->pdata = (struct dmm_pm_pdata *)match->data;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (IS_ERR(res)) {
		dev_err(dev, "can't get regs\n");
		return PTR_ERR(res);
	}
	dmm_pm->base = ioremap(res->start, resource_size(res));
	if (IS_ERR(dmm_pm->base)) {
		dev_err(dev, "map registers error\n");
		return -ENODEV;
	}
	dev_dbg(dev, "base: 0x%p\n", dmm_pm->base);

	dmm_pm->ddr_clk = devm_clk_get(dev, "ddr");
	if (IS_ERR(dmm_pm->ddr_clk)) {
		dev_err(dev, "can't get ddr clk\n");
		return -EINVAL;
	}

	if ((ret = dmm_pm_initialize(dmm_pm)) < 0) {
		dev_err(dev, "%s: dmm_pm_initialize failed\n", __func__);
		return ret;
	}

	if ((ret = dmm_pm_create_debugfs(dmm_pm)) < 0) {
		dev_err(dev, "%s: dmm_pm_create_debugfs failed\n", __func__);
		dmm_pm_remove_debugfs(dmm_pm);
		return ret;
	}

	dev_dbg(dev, "%s: ok\n", __func__);
	return 0;
}

static int owl_dmm_pm_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct owl_dmm_pm *dmm_pm = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);

	dmm_pm_remove_debugfs(dmm_pm);
	dmm_pm_deinitialize(dmm_pm);
	
	return 0;
}

static struct platform_driver owl_dmm_pm_driver = {
	.probe          = owl_dmm_pm_probe,
	.remove         = owl_dmm_pm_remove,
	.driver         = {
		.name   = "owl_dmm_pm",
		.owner  = THIS_MODULE,
		.of_match_table	= owl_dmm_pm_of_match,
	},
};

int __init owl_dmm_pm_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);
	
	ret = platform_driver_register(&owl_dmm_pm_driver);
	if (ret)
		pr_err("%s: Failed to initialize platform driver\n", __func__);

	return ret;
}

void __exit owl_dmm_pm_exit(void)
{   
	platform_driver_unregister(&owl_dmm_pm_driver);
}

module_init(owl_dmm_pm_init);
module_exit(owl_dmm_pm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL DMM Performance Monitor");
