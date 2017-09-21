#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>    /* error codes */
#include <linux/errno.h>    /* error codes */
#include <linux/vmalloc.h>
#include <linux/slab.h>     /* kmalloc/kfree */
#include <linux/init.h>     /* module_init/module_exit */
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/sem.h>
#include <linux/time.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <../drivers/staging/android/ion/ion.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include "hde_core.h"
#include "hde_drv.h"

#define HDE_PAGE_SIZE (4U << 10)
#define HDE_TILE_SIZE (64)
#define NUM_PAGES_PER_TILE (2)
#define NUM_PAGES_PER_MMU_ENTRY (512)
#define AMDC_SIZE_PER_MMU_ENTRY (2048)

#if (HDE_PAGE_SIZE != PAGE_SIZE)
#warning "HDE_PAGE_SIZE is not equal to kernel PAGE_SIZE"
#endif

#define HDE_ERROR(fmt, args...) pr_err("hde: %s(%d): " fmt, __func__, __LINE__, ##args);

#define IC_TYPE_S900		0x900
#define IC_TYPE_S700		0x700
#define IS_HDE			1

#define HDE_REGISTER_BASE	0xe0230000
#define IRQ_ASOC_HDE		(32 + 66)

#define DEVDRV_NAME_HDE		"hde"
#define CLK_NAME_DEVCLK     "dev_clk"
#define CLK_NAME_DISPLAYPLL "display_pll"
#define HDE_REG_BASE		HDE_REGISTER_BASE

#define SPS_BASE_PHY		0xe012e000
#define CMU_BASE_PHY_S900	0xe0160000
#define CMU_BASE_PHY_S700	0xe0168000
#define HDE_REG_BACKUP_NUM	76
#define MAX_INSTANCE_NUM	8
#define VDD_0_9V		900000
#define VDD_1_0V		1000000
#define VDD_1_05V		1050000
#define VDD_1_1V		1100000
#define VDD_1_15V		1150000
#define VDD_1_2V		1200000
#define HDE_DEBUG_PRINTK	printk("%s %d\n", __FILE__, __LINE__)
#define HDE_DBG(...)	{ if (debug_enable == 1) { printk(__VA_ARGS__); } }
#define HDE_DBG2(...)	{ if (debug_enable == 2) { printk(__VA_ARGS__); } }

#define HDE_FREQ_DEFAULT	360

/*minimize 4 div*/
#define HDE_FREQ_D1		180

#define HDE_FREQ_720P		270
#define HDE_FREQ_1080P		360
#define HDE_FREQ_MULTI		480
#define HDE_FREQ_4Kx2K		720
#define HDE_FREQ_4Kx2K_S700 600

static int hde_cur_slot_id = -1;

/*jpeg slice pattern, can't free if frame is not completed*/
static int hde_slice_slot_id = -1;

static unsigned int hde_cur_slot_num;
static int hde_set_voltage_limit;

/*0:normal, -1:error;*/
static int hde_last_status;

static int multi_instance_mode;
static int debug_enable;
static int autofreq_enable = 1;
static int adjust_freq_flag = 1;
static int hde_pre_freq;
static int ic_type = IC_TYPE_S900;

static void __iomem *HDE_SPS_PG_CTL;
static void __iomem *HDE_CMU_DEVRST0;
static void __iomem *HDE_CMU_DEVCLKEN0;
static void __iomem *HDE_CMU_HDE_CLK;
static void __iomem *HDE_CMU_COREPLL;

static int hde_irq_registered;
static int hde_open_count;
static unsigned long hde_cur_need_freq;

/*hde shoule be occupied when jpeg slice pattern that
	may needs some times when decoded*/
static int hde_occupied;

/* for multi process */
static struct mutex m_mutex;
static struct mutex m_freq_adjust_mutex;

static DECLARE_WAIT_QUEUE_HEAD(waitqueue);

/*hde can accept a new RUN order if finish*/
static int hde_idle_flag = 1;

static int hde_clk_isenable;
typedef struct {
	unsigned int regs[HDE_REG_BACKUP_NUM];
} hde_user_data_t;

struct asoc_hde_dev {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	int irq;
	unsigned int setting;
	struct regulator *power;
};

struct mmu_fill_package {
	u32 n_bufs;

	struct {
		u32 w;
		u32 h;
	} common;

	struct {
		s32 fd_mmu;
		s32 fd_buf;

		u32 phys_addr;
	} private[0];
};

struct mmu_entry {
	u32 phys[NUM_PAGES_PER_MMU_ENTRY];
	u8 amdc[AMDC_SIZE_PER_MMU_ENTRY];
};

static struct ion_client *psIonClient;
struct asoc_hde_dev *GHDE;
static struct clk *hde_clk;
static struct clk *display_pll;
static struct reset_control *reset;
static struct platform_device *GHDEDEV;

static void hde_write(struct asoc_hde_dev *hde, u32 val, u32 reg)
{
	/* printk("hde_write %x, val %x\n",(unsigned int)hde->base + reg, val);*/
	void volatile *write_add = (void *)(hde->base + reg);
	writel_relaxed(val, write_add);
}

static u32 hde_read(struct asoc_hde_dev *hde, u32 reg)
{
	unsigned int data = 0;
	data = readl_relaxed((void volatile *)(hde->base + reg));
	/*HDE_DBG("hde_read %x, val %x\n",(unsigned int)hde->base + reg, data);*/
	return data;
}

#define act_readl(a)	hde_read(GHDE, a)
#define act_writel(v, a) hde_write(GHDE, v, a)
#define re_reg(a)	(act_readl(a))
#define wr_reg(a, v)	(act_writel(v, a))

struct ic_info {
	int ic_type;
};

static  struct ic_info s900_data = {
	.ic_type = IC_TYPE_S900,
};

static  struct ic_info s700_data = {
	.ic_type = IC_TYPE_S700,
};

static const struct of_device_id hde_of_match[] = {
	{.compatible = "actions,s900-hde", .data = &s900_data},
	{.compatible = "actions,s700-hde", .data = &s700_data},
	{}
};
MODULE_DEVICE_TABLE(of, hde_of_match);

typedef enum {
	SLOT_IDLE,
	SLOT_OCCUPIED,
	SLOT_COMPLETED
} slot_state_e;

typedef struct slot_s {
	struct completion isr_complete;
	slot_state_e state;
	unsigned long clientregptr;
	unsigned int hde_status;
	unsigned int slice_mode;
	int pid;
	hde_user_data_t user_data;
} slot_t;

static slot_t slot[MAX_INSTANCE_NUM];

static int slot_reset(int i)
{
	init_completion(&slot[i].isr_complete);
	slot[i].state = SLOT_IDLE;
	slot[i].clientregptr = 0;
	slot[i].slice_mode = 0;
	slot[i].pid = -1;
	if (hde_cur_slot_num > 0)
		hde_cur_slot_num--;

	return 0;
}

static int slot_get(void)
{
	int i;
	for (i = 0; i < MAX_INSTANCE_NUM; i++) {
		if (slot[i].state == SLOT_IDLE) {
			init_completion(&slot[i].isr_complete);
			slot[i].state = SLOT_OCCUPIED;
			hde_cur_slot_num++;
			slot[i].pid = task_tgid_vnr(current);
			return i;
		}
	}

	if (i == MAX_INSTANCE_NUM) {
		printk("hde : no idle slot, max %d slots\n", MAX_INSTANCE_NUM);
		return -1;
	}
	return -1;
}

static int slot_complete(int i, unsigned int hde_status)
{
	if (slot[i].state != SLOT_OCCUPIED) {
		printk("hde : slot is idle, staus error\n");
		return -1;
	}

	slot[i].hde_status = hde_status;
	slot[i].state = SLOT_COMPLETED;

	return 0;
}
/*
static int hde_query_interval(unsigned int reg4)
{
	unsigned int mb_w = (reg4 >> 23)&0x1ff;
	unsigned int mb_h = (reg4 >> 15)&0xff;
	unsigned int r = (mb_w * mb_h * 300)/(1000 * hde_pre_freq);
	if (r < 5)
		r = 5;

	return r;
}
*/
static void hde_waitforidle(void)
{
	int ctr = 0;
	int v = hde_read(GHDE, 0x4);
	if ((v&0x1) == 0x1) {
		HDE_DBG("hde : hde is on air\n");
		do {
			msleep(5);
			ctr++;
			if (ctr == 100) {
				printk("hde : hde always busy\n");
				break;
			}
		} while (((hde_read(GHDE, 0x4)) & 0x1) == 0x1);
	}
}

/*============ funcs:clock, power, reset, irq ==================*/
static void hde_do_reset(void)
{
	int ctor = 0;
	unsigned int value_assigned;
	unsigned int hde_state = hde_read(GHDE, 0x4);

	/*avoid reset hde when hde is working*/
	while ((hde_state & 0x1) == 0x1) {
		msleep(5);

		hde_state = hde_read(GHDE, 0x4);
#ifdef IS_HDE
		if (hde_state & 0x62000) {
#else
		if (hde_state & 0x1da000) {
#endif
			printk("warning, reset hde when working wrong. state: 0x%x\n", hde_state);
			break;
		}
		if (ctor > 10) {
			printk("warning:reset hde. state: 0x%x, ctor(%d) > 10\n",
				hde_state, ctor);
			break;
		}

		ctor++;
	}

	reset_control_reset(reset);
	HDE_DBG("hde : %d checking reset .............\n", __LINE__);
	value_assigned = readl_relaxed(HDE_CMU_DEVRST0);

	if (ic_type == IC_TYPE_S900) {
		while (((value_assigned>>21) & 0x1) != 0x1) {
			printk("hde : Fail to reset hde, DEVEN0 = 0x%x, \
					CMU_DEVRST0 = 0x%x PG_CTL = 0x%x\n",
						readl_relaxed(HDE_CMU_DEVCLKEN0),
							readl_relaxed(HDE_CMU_DEVRST0),
								readl_relaxed(HDE_SPS_PG_CTL));

			value_assigned |= (0x1<<21);
			writel_relaxed(value_assigned, HDE_CMU_DEVRST0);
			usleep_range(50*1000, 50*1000);
			value_assigned = readl_relaxed(HDE_CMU_DEVRST0);
		}
	} else {
			while (((value_assigned>>9) & 0x1) != 0x1) {
			printk("hde : Fail to reset hde, DEVEN0 = 0x%x, \
					CMU_DEVRST0 = 0x%x PG_CTL = 0x%x\n",
						readl_relaxed(HDE_CMU_DEVCLKEN0),
							readl_relaxed(HDE_CMU_DEVRST0),
								readl_relaxed(HDE_SPS_PG_CTL));

			value_assigned |= (0x1<<9);
			writel_relaxed(value_assigned, HDE_CMU_DEVRST0);
			usleep_range(50*1000, 50*1000);
			value_assigned = readl_relaxed(HDE_CMU_DEVRST0);
		}
	}

	hde_idle_flag = 1;
	hde_occupied = 0;
	hde_last_status = 0;

	printk("hde : hde reset end\n");
	return;
}

static void hde_reset(void)
{
	usleep_range(5*1000, 5*1000);
	hde_do_reset();
}

static void hde_clk_enable(void)
{
	int res = 0;
	/*unsigned int reg_HDE_CMU_HDE_CLK = 0;*/
	if (hde_clk_isenable != 0)
		return;

	/*power on*/
	HDE_DBG2("dev add %p, hde_clk %p\n", &GHDEDEV->dev, hde_clk);
	res = pm_runtime_get_sync(&GHDEDEV->dev);
	if (res < 0) {
		printk("hde_clk_enable: pm_runtime_get_sync failed\n");
		return;
	}

	/*enable clk*/
	clk_prepare_enable(hde_clk);

	/*printk("set hde clock to displaypll\n");*/
	if (clk_set_parent(hde_clk, display_pll))
		printk("failed to set hde parent to display_pll\n");

	hde_clk_isenable = 1;
}

static void hde_clk_disable(void)
{
	int res = 0;

	HDE_DBG("hde : hde_clk_disable, In\n");
	if (hde_clk_isenable == 0)
		return;

	hde_waitforidle();
	/*disable clk*/
	clk_disable_unprepare(hde_clk);

	hde_clk_isenable = 0;
	/*power off*/
	res = pm_runtime_put_sync(&GHDEDEV->dev);
	if (res < 0) {
		printk("hde_clk_disable: pm_runtime_put_sync failed\n");
		return;
	}

	HDE_DBG("hde : hde_clk_disable, Out\n");
}

#define MAX_HDE_REG_RETRY_TIME  5
/* enable int */
static inline void enable_hde_irq(void)
{
}

static inline void disable_hde_irq(void)
{
	unsigned int v;
	int c;

	c = MAX_HDE_REG_RETRY_TIME;
	v = re_reg(0x4);

	v &= ~(1<<8);
	wr_reg(0x4, v);

	while ((re_reg(0x4)) & (0x1<<8) && c-- > 0) {
		printk("hde : can not disable irq, write %x %x\n",
				(HDE_REG_BASE) + 0x4, re_reg(0x4));
		wr_reg(0x4, v);
	}
}
/*==============================================================*/

static void hde_drv_updatereg(int id)
{
	int i;
	/*we never handle reg0, reg1*/
	for (i = 1; i < HDE_REG_BACKUP_NUM; i++)
		slot[id].user_data.regs[i] = (unsigned int)(re_reg(i*4));
}

static void hde_drv_showreg(int id)
{
	if (id >= 0) {
		HDE_DBG("hde : (showReg-1) 0x%08x 0x%08x 0x%08x 0x%08x\n",
			slot[id].user_data.regs[1], slot[id].user_data.regs[2],
				slot[id].user_data.regs[3], slot[id].user_data.regs[4]);
	} else {
		HDE_DBG("hde : (showReg-2) 0x%08x 0x%08x 0x%08x 0x%08x\n",
			(unsigned int)(re_reg(1*4)), (unsigned int)(re_reg(2*4)),
				(unsigned int)(re_reg(3*4)),
					(unsigned int)(re_reg(4*4)));
	}
}

/**
 * This function is hde ISR.
 */
irqreturn_t hde_isr(int irq, void *dev_id)
{
	unsigned int s;
	disable_hde_irq();
	slot_complete(hde_cur_slot_id, re_reg(0x4));
	hde_drv_updatereg(hde_cur_slot_id);

	/*when bistream empty or jpeg slice ready, and not frame ready.*/
	s = act_readl(0x4);

#if 0
#ifdef IS_HDE
	if (((s & (0x1<<15))) && !(s & (0x1<<12))) {
#else
	if (((s & (0x1<<17)) || (s & (0x1<<14))) && !(s & (0x1<<12))) {
#endif
		slot[hde_cur_slot_id].slice_mode = 1;
		hde_slice_slot_id = hde_cur_slot_id;
		slot[hde_slice_slot_id].state = SLOT_OCCUPIED;
	} else {
		slot[hde_cur_slot_id].slice_mode = 0;
		hde_occupied = 0;
	}
#endif

	slot[hde_cur_slot_id].hde_status = act_readl(0x4);
	slot[hde_cur_slot_id].slice_mode = 0;
	hde_occupied = 0;
	hde_idle_flag = 1;

	if (slot[hde_cur_slot_id].hde_status & 0x6a000) {
		/*when meet some error.*/
		int v = 0;
		mdelay(10);
		act_writel(v, 0x4);

		if (act_readl(0x4) == 0x1100)
			disable_hde_irq();

		hde_drv_showreg(-1);
		hde_last_status = -1;
	} else {
		/*only if decoding normally, enable clk gating
			when decoder one frame.*/
		unsigned int r2 = act_readl(0x8);
		r2 |= (0x1 << 0);
		act_writel(r2, 0x8);
	}

	complete(&(slot[hde_cur_slot_id].isr_complete));
	wake_up_interruptible(&waitqueue);/*wake up*/

	return IRQ_HANDLED;
}

static void hde_drv_writereg(unsigned int regno, unsigned int value)
{
	/*unsigned int value_recover;*/
	wr_reg(regno*4, value);
	/*
	value_recover = (unsigned int)(re_reg(regno*4));

	while(value_recover != value) {
		printk("hde : Fail to write reg 0x%x, \
			(input,recover)=(0x%x, 0x%x)\n", regno, value, value_recover);
		wr_reg(regno*4, value);
		value_recover = (unsigned int)(re_reg(regno*4));
	}
	*/
}

static void hde_drv_flushreg(int id, void __user *v)
{
	int i, rt;
	unsigned int value, tmpval;
	unsigned int width;
	unsigned int mode;

	rt = copy_from_user(&(slot[id].user_data.regs[0]), v, sizeof(hde_user_data_t));
	if (rt != 0)
		printk("hde : Warning: copy_from_user failed, rt=%d\n", rt);

	value = slot[id].user_data.regs[2];
	tmpval = slot[id].user_data.regs[4];
	width = ((tmpval >> 23) & 0x1ff) << 4;
	mode = (tmpval >> 12) & 0x7;

	if (mode == 0) {
		/*
		// H264 all not only 4K &&((width > 2048))
		//tmpval |= (1<<10); // end frame flag --- condition 2: bit[10] = 1
		//slot[id].user_data.regs[4] = tmpval;
		*/
		value |= (1<<23);
	} else {
		value &= (~(1<<23));
	}
	slot[id].user_data.regs[2] = value;

	/*we never handle reg0, reg1*/
	for (i = 2; i < HDE_REG_BACKUP_NUM; i++) {
		/*printk("hde : %d",i);*/
		hde_drv_writereg(i, slot[id].user_data.regs[i]);
	}
}

HDE_Status_t hde_query_status(unsigned int hde_status)
{
	if (hde_status & (0x1<<12)) {
		HDE_DBG("hde : hde status gotframe, status: 0x%x\n", hde_status);
		return HDE_STATUS_GOTFRAME;
	}
#if 1 /*IC_TYPE_S900*/
	else if (hde_status & (0x1<<19)) {
		HDE_DBG("hde : hde status directmv full, status: 0x%x\n", hde_status);
		return HDE_STATUS_DIRECTMV_FULL;
	} else if (hde_status & (0x1<<20)) {
		printk("hde : hde status RLC error, status: 0x%x\n", hde_status);
		return HDE_STATUS_BUS_ERROR;
	}
#endif
	else if (hde_status & (0x1<<18)) {
		HDE_DBG("hde : hde status timeout, status: 0x%x\n", hde_status);
		return HDE_STATUS_TIMEOUT;
	} else if (hde_status & (0x1<<17)) {
		HDE_DBG("hde : hde status stream error, status: 0x%x\n", hde_status);
		return HDE_STATUS_STREAM_ERROR;
	} else if (hde_status & (0x1<<15)) {
		HDE_DBG("hde : hde status stream empty, status: 0x%x\n", hde_status);
		return HDE_STATUS_STREAM_EMPTY;
	} else if (hde_status & (0x1<<13)) {
		printk("hde : hde status Bus error, status: 0x%x\n", hde_status);
		return HDE_STATUS_BUS_ERROR;
	} else {
		printk("hde : hde status unknow error, status: 0x%x\n", hde_status);
		return HDE_STATUS_UNKNOWN_ERROR;
	}
}

/*===========start=========== fre interrelated ==============*/
typedef unsigned long HDE_FREQ_T;

HDE_FREQ_T hde_do_set_freq(HDE_FREQ_T new_rate)
{
	unsigned long rate;
	unsigned long temprate;
	long targetfreq, freq;
	int ret;

	targetfreq = new_rate*1000*1000;
	/*set the closest clk fre, hz*/
	freq = clk_round_rate(hde_clk, targetfreq);
	rate = clk_get_rate(hde_clk);
	if (rate == targetfreq) {
		printk("hde_do_set_freq: cur hde \
			freq: %lu = %lu(target), no changed!\n", rate, targetfreq);
		temprate = rate/(1000*1000);
		return temprate;
	}

	if (freq < targetfreq) {
		if (rate == freq) {
			printk("hde_do_set_freq: cur hde \
				freq: %lu = %ld(round), no changed!\n", rate, freq);
			temprate = rate/(1000*1000);
			return temprate;
		}
		ret = clk_set_rate(hde_clk, freq);
	} else {
	/*set the clk fre, hz*/
		ret = clk_set_rate(hde_clk, targetfreq);
	}

	if (ret != 0) {
		printk("hde : clk_set_rate new_rate %dM,  failed!\n",
			(unsigned int)new_rate);
		/*fixme : do we need handle vdd/parent?*/
		return 0;
	}

	/*get the clk fre, hz*/
	rate = clk_get_rate(hde_clk);
	temprate = rate/(1000*1000);
	return temprate;
}

#if 0
static int hde_set_corevdd(struct regulator *power, int voltage)
{
	if (!IS_ERR(power)) {
			if (regulator_set_voltage(power, voltage, INT_MAX)) {
			printk("cannot set corevdd to %duV !\n", voltage);
			return -EINVAL;
		}
	}
	return 0;
}
#endif

static int hde_set_parent_pll(const char *parent_name)
{
	struct clk *pre_parent;
	struct clk *cur_parent;
	struct clk *hde_clk;
	int ret;

	cur_parent = clk_get(NULL, (const char *)parent_name);

	if (IS_ERR(cur_parent)) {
		printk("hde : clk_get_sys(..) failed\n");
		return -1;
	}
	hde_clk = clk_get(NULL, "hde");
	if (IS_ERR(hde_clk)) {
		printk("hde : clk_get_sys(CLK_NAME_HDE_CLK, NULL) failed\n");
		return -1;
	}
	pre_parent = clk_get_parent(hde_clk);

	/*Compare two clk sources like this*/
	if (pre_parent == cur_parent) {
		/*printk("hde : hde_set_parent_pll: parent pll no changed! \n");*/
		return 0;
	}

	ret = clk_set_parent(hde_clk, cur_parent);
	if (ret != 0) {
		printk("hde : clk_set_parent failed(%d)\n", ret);
		/*fixme : now what*/
		return -1;
	}
	return 0;
}

/* set hde fre and return it.If fail, reruen 0.*/
static HDE_FREQ_T hde_setfreq(HDE_FREQ_T freq_mhz)
{
	unsigned long new_rate;

	new_rate = freq_mhz;

	/*			S900
	*D1  176M    DISPLAY_PLL     6    0.9v VDD_core
	*720P 264M   DISPLAY_PLL     4    0.9v
	*1080P 352M  DISPLAY_PLL     3    0.9v
	*multi/4k 704M  DISPLAY_PLL  1.5  0.9v
				S700
	*D1  160M    DISPLAY_PLL     3    1.05v VDD_core
	*720P 240M   DISPLAY_PLL     2    1.05v
	*1080P 320M  DISPLAY_PLL    1.5   1.05v
	*multi/4k 480M  DISPLAY_PLL  1    1.05v
	*/

#if 1 /*if need to set vdd or pll for s700*/
	if (ic_type == IC_TYPE_S700) {
		if (new_rate >= HDE_FREQ_4Kx2K_S700) {
#if 0
			if (hde_set_corevdd(GHDE->power, VDD_1_05V) != 0) {
				printk("hde : set vdd1 err \n");
				return 0;
			}
#endif
			if (hde_set_parent_pll(CLK_NAME_DEVCLK) != 0) {
				printk("hde : set parent_pll1 err \n");
				return 0;
			}
		} else if (new_rate >= HDE_FREQ_MULTI) {
#if 0
			if (hde_set_corevdd(GHDE->power, VDD_1_05V) != 0) {
				printk("hde : set vdd2 err \n");
				return 0;
			}
#endif
			if (hde_set_parent_pll(CLK_NAME_DEVCLK) != 0) {
				printk("hde : set parent_pll2 err \n");
				return 0;
			}
		} else if (new_rate >= HDE_FREQ_1080P) {
#if 0
			if (hde_set_corevdd(GHDE->power, VDD_1_05V) != 0) {
				printk("hde : set vdd3 err \n");
				return 0;
			}
#endif
			if (hde_set_parent_pll(CLK_NAME_DISPLAYPLL) != 0) {
				printk("hde : set parent_pll3 err \n");
				return 0;
			}
		} else if (new_rate >= HDE_FREQ_720P) {
#if 0
			if (hde_set_corevdd(GHDE->power, VDD_1_05V) != 0) {
				printk("hde : set vdd err4 \n");
				return 0;
			}
#endif
			if (hde_set_parent_pll(CLK_NAME_DISPLAYPLL) != 0) {
				printk("hde : set parent_pll4 err \n");
				return 0;
			}
		} else {
#if 0
			if (hde_set_corevdd(GHDE->power, VDD_1_05V) != 0) {
				printk("hde : set vdd err5 \n");
				return 0;
			}
#endif
			if (hde_set_parent_pll(CLK_NAME_DISPLAYPLL) != 0) {
				printk("hde : set parent_pll5 err \n");
				return 0;
			}
		}
	}
#endif

	return hde_do_set_freq(new_rate);
}

/*extern int hde_status_changed(int status);*/
static int hde_adjust_freq(void)
{
	int f, ret;
	unsigned int v;
	unsigned int width;
	unsigned int height;
	HDE_DBG("hde : hde_adjust_freq()\n");
	v = hde_read(GHDE, 0x10);
	width = (v & 0x3fff);
	height = ((v >> 16) & 0x3fff);

	if (width > (1280+1280)) {
		if (ic_type == IC_TYPE_S900)
			f = HDE_FREQ_4Kx2K;
		else
			f = HDE_FREQ_4Kx2K_S700;
	} else if (width > 1280)
		f = HDE_FREQ_1080P;
	else if (width > 720)
		f = HDE_FREQ_720P;
	else
		f = HDE_FREQ_D1;

	if (hde_cur_slot_num > 1 || multi_instance_mode == 1) {
		HDE_DBG("hde_adjust_freq: hde_cur_slot_num: %d multi_instance: %d",
			hde_cur_slot_num, multi_instance_mode);
		if (ic_type == IC_TYPE_S900)
			f = HDE_FREQ_4Kx2K;
		else
			f = HDE_FREQ_4Kx2K_S700;
	}
	if (autofreq_enable == 0)
		f = HDE_FREQ_DEFAULT;

	if (autofreq_enable == 2) {
		if (ic_type == IC_TYPE_S900)
			f = HDE_FREQ_4Kx2K;
		else
			f = HDE_FREQ_4Kx2K_S700;
	}

	if (hde_pre_freq != f) {
		hde_cur_need_freq = (unsigned long)f * 1000000;

		/*set frequency */
		ret = hde_setfreq(f);
		if (ret != 0) {
			hde_pre_freq = f;
			printk("hde_adjust: [w, h, f] =[%d, %d], autoFreq = %d, \
				mulIns = %d,freq.[need, real]= [%d, %d] \n",
					width, height, autofreq_enable, multi_instance_mode,
						(int)(hde_cur_need_freq/1000000), ret);
		} else {
			printk("hde : set %dM Failed, set default freq&pll %dM\n",
				f, HDE_FREQ_DEFAULT);
			hde_setfreq(HDE_FREQ_DEFAULT);
			hde_pre_freq = HDE_FREQ_DEFAULT;
		}
	}
	return 0;
}
/*==============end========== fre interrelated =========*/
int hde_mmu_init(void);
void hde_mmu_deinit(void);
int hde_ioctl_fill_mmu(unsigned long arg);

static long hde_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int left_time, id, s, slot_id, jpeg_on_going;

	switch (cmd) {
	case HDE_RUN:
		/* return 0; */

	if (arg != 0) {
		jpeg_on_going = 0;
		mutex_lock(&m_mutex);

		/*continue if completed, interrupted or timeout*/
		left_time = wait_event_interruptible_timeout(
			waitqueue, hde_idle_flag == 1,
				msecs_to_jiffies(1000) + 1);
		if (unlikely(left_time == 0)) {
			printk("hde : HDE_RUN wait_interruptible_timeout 5s\n");
			hde_reset();
			mutex_unlock(&m_mutex);
			return HDE_STATUS_UNKNOWN_ERROR;
		}

		if (hde_last_status == -1)
			hde_reset();

		if (hde_occupied == 1) {
			/*FIXME: Arg should not change that
				next slice comes in slice pattern*/
			if (arg != slot[hde_slice_slot_id].clientregptr) {
				printk("hde : HDE_RUN --- UNKNOWN_ERROR\n");
				mutex_unlock(&m_mutex);
				return HDE_STATUS_UNKNOWN_ERROR;
			} else {
				jpeg_on_going = 1;
			}
		}

		if (jpeg_on_going) {
			id = hde_slice_slot_id;
		} else {
		/*Get a slot. It needs several slots
			if multitasks run HDE, that can't HDE_QUERY in time*/
			id = slot_get();
			if (id < 0) {
				mutex_unlock(&m_mutex);
				return HDE_STATUS_UNKNOWN_ERROR;
			}
		}

		/*backup data*/
		hde_cur_slot_id = id;
		slot[id].clientregptr = arg;
		hde_drv_flushreg(id, (void __user *)arg);

		hde_occupied = 1;
		hde_idle_flag = 0;
		mutex_lock(&m_freq_adjust_mutex);

		if (adjust_freq_flag == 1) {
			hde_adjust_freq();
		}

		/*act_readl(0x4);*/
		s = 0;
		s |= 1;
		enable_hde_irq();

		act_writel(s, 0x4);

		#if 0 /* for debug */
		if (tag == 1) {
			if (act_readl(0x4) != 0x1)
				printk("hde: %d HDE_RUN 0x%x\n", __LINE__, act_readl(0x4));

		}
		#endif

		mutex_unlock(&m_freq_adjust_mutex);
		mutex_unlock(&m_mutex);
		return id;

	} else {
		printk("hde : Fail to execute HDE RUN, maybe arg (0x%lx) wrong\n", (unsigned long)arg);
		return HDE_STATUS_UNKNOWN_ERROR;
	}

	break;

	case HDE_QUERY:

	slot_id = (int)arg;
	HDE_DBG("hde : --- HDE_QUERY --- 0. query NO.%d slot\n", slot_id);
	/* return HDE_STATUS_GOTFRAME;*/
	if (hde_irq_registered) {
		HDE_Status_t s;
		mutex_lock(&m_mutex);

		/*check interrupted*/
		left_time = wait_for_completion_timeout(
				&(slot[slot_id].isr_complete),
					msecs_to_jiffies(1000) + 1);
		if (unlikely(left_time == 0)) {
			printk("wait timeout -> hde_reset(%d, %d)\n",
				slot_id, hde_cur_slot_id);
			hde_reset();
			s = HDE_STATUS_DEAD;
		} else {
			/* normal case */
			int rt;
			rt = copy_to_user(
				(void __user *)slot[slot_id].clientregptr,
					&(slot[slot_id].user_data.regs[0]),
						sizeof(hde_user_data_t));
			if (rt != 0)
				printk("Warning: copy_to_user failed, rt=%d\n", rt);

			s = hde_query_status(slot[slot_id].hde_status);
		}

		/* free slot */
		if ((slot[hde_cur_slot_id].slice_mode == 0)
			|| (s == HDE_STATUS_DEAD)) {
			slot_reset(slot_id);
		}

		mutex_unlock(&m_mutex);
		return s;
	} else {

		printk("hde : should not be here\n");
		return -1;
	}

	break;

	case HDE_DISABLE_CLK:
		break;
	case HDE_ENABLE_CLK:
		break;
	case HDE_SET_FREQ:
		break;
	case HDE_GET_FREQ:
		break;
	case HDE_SET_MULTI:
		mutex_lock(&m_mutex);
		if (arg > 1)
			multi_instance_mode = 1;
		else
			multi_instance_mode = 0;

		mutex_unlock(&m_mutex);
		break;
	case HDE_DUMP:
		printk("hde : hde HDE_DUMP..., but do nothing\n");
		hde_drv_showreg(-1);
		break;
	case HDE_FILL_MMU:
		if (hde_ioctl_fill_mmu(arg))
			printk("hde: HDE_FILL_MMU failed\n");
			break;
	default:
		printk("hde : no such cmd 0x%x\n", cmd);
		return -EIO;
	}

	/* hde_reset_in_playing(); */
	return 0;
}

static int hde_open(struct inode *inode, struct file *file)
{
	HDE_DBG2("\nhde : hde_open: In, hde_open_count: %d\n", hde_open_count);

	mutex_lock(&m_mutex);

	hde_open_count++;
	if (hde_open_count > 1) {
		printk("hde drv already open\n");
		mutex_unlock(&m_mutex);
		return 0;
	}
	hde_set_voltage_limit = 0;

	hde_clk_enable();

	disable_hde_irq();

	enable_irq(GHDE->irq);

	hde_idle_flag = 1;

	HDE_DBG2("hde : hde_open: Out\n");
	mutex_unlock(&m_mutex);
	return 0;
}

static int hde_release(struct inode *inode, struct file *file)
{
	int i;
	mutex_lock(&m_mutex);

	HDE_DBG2("hde : hde_release: In, hde_open_count: %d\n", hde_open_count);
	hde_open_count--;

	if (hde_open_count > 0) {
		printk("hde : hde_release: count:%d pid(%d)\n",
			hde_open_count, task_tgid_vnr(current));
		hde_waitforidle();

		goto HDE_REL;
	} else if (hde_open_count < 0) {
		printk("hde : hde_release: module is closed before opened\n");
		hde_open_count = 0;
	}

	if (hde_open_count == 0) {
		HDE_DBG2("hde : hde_release: disable hde irq\n");
		disable_hde_irq();
		HDE_DBG2("hde : hde_release: disable IRQ_HDE\n");
		disable_irq(GHDE->irq);
		HDE_DBG2("hde : hde_release: disable hde irq ok!\n");
	}

	hde_clk_disable();

	hde_pre_freq = 0;

HDE_REL:
	for (i = 0; i < MAX_INSTANCE_NUM; i++) {
		if (slot[i].pid == task_tgid_vnr(current)) {
			printk("hde : hde slot is leak by pid(%d), reset it\n",
				task_tgid_vnr(current));
			if (slot[i].slice_mode == 1 && hde_occupied == 1)
				hde_occupied = 0;
			//slot_reset(i);
		}
	}
	HDE_DBG2("hde : hde_release: Out. hde_cur_slot_num: %d hde_open_count: %d\n",
		hde_cur_slot_num, hde_open_count);
	mutex_unlock(&m_mutex);

	return 0;
}

static int hde_is_enable_before_suspend;
/*
 guarantee that frame has been decoded before low power
*/
static int hde_suspend(struct platform_device *dev, pm_message_t state)
{
	printk("hde : hde_suspend: In , hde_clk_isenable=%d, RST0=0x%x\n",
		hde_clk_isenable, readl_relaxed(HDE_CMU_DEVRST0));
	if (hde_clk_isenable != 0) {
		mutex_lock(&m_mutex);
		hde_waitforidle();
		disable_hde_irq();
		hde_clk_disable();
		hde_is_enable_before_suspend = 1;

		mutex_unlock(&m_mutex);
	}

	disable_irq(GHDE->irq);

	/*Reset the voltage if the status is not clear after resumed.*/
	hde_pre_freq = 0;
	printk("hde : hde_suspend: Out\n");

	return 0;
}

static int hde_resume(struct platform_device *dev)
{
	printk(KERN_DEBUG"hde : hde_resume: In, hde_is_enable_before_suspend=%d, RST0=0x%x\n",
		hde_is_enable_before_suspend, readl_relaxed(HDE_CMU_DEVRST0));
	if (hde_is_enable_before_suspend == 1) {
		hde_clk_enable();
		hde_is_enable_before_suspend = 0;
	} else {
		/*hde_power_off();*/
	}

	enable_irq(GHDE->irq);

	printk(KERN_DEBUG"hde : hde_resume: Out\n");
	return 0;
}

static struct file_operations hde_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = hde_ioctl,
	.compat_ioctl = hde_ioctl,
	.open = hde_open,
	.release = hde_release,
};

//#define ENABLE_COOLING

#ifdef ENABLE_COOLING
#include <linux/cpu_cooling.h>
static bool is_cooling;
static int thermal_notifier(struct notifier_block *nb,
								unsigned long event, void *data)
{
	printk("hde thermal_notifier event:%ld \n", event);
	if (event == CPUFREQ_COOLING_START) {
		int ret = 0;
		if (is_cooling == 0) {
			mutex_lock(&m_freq_adjust_mutex);
			hde_waitforidle();
			adjust_freq_flag = 0;
			printk("hde  CPUFREQ_COOLING_START,event:%ld,\n", event);
#if 0
			if (hde_set_parent_pll(CLK_NAME_DISPLAYPLL) != 0) {
				printk("set parent pll fail\n");
				mutex_unlock(&m_freq_adjust_mutex);
				return 0;
			}
		  if (hde_set_corevdd(GHDE->power, VDD_1_05V) != 0) {
					printk("set vdd VDD_1_05V fail\n");
					mutex_unlock(&m_freq_adjust_mutex);
					return 0;
			}
#endif
			ret = hde_do_set_freq(HDE_FREQ_720P);
			mutex_unlock(&m_freq_adjust_mutex);
			is_cooling = 1;
		}
		printk("-- CPUFREQ_COOLING_START --freq : %d\n", ret/30);
	}

	if (event == CPUFREQ_COOLING_STOP) {
		printk("hde  CPUFREQ_COOLING_STOP event:%ld\n", event);
		if (is_cooling == 1) {
			mutex_lock(&m_freq_adjust_mutex);
			is_cooling = 0;
			adjust_freq_flag = 1;
			mutex_unlock(&m_freq_adjust_mutex);
		}
	}
	return 0;
}
static struct notifier_block thermal_notifier_block = {
	.notifier_call = thermal_notifier,
};

#endif

static int asoc_hde_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct asoc_hde_dev *hde;
	struct resource *iores;
	int ret;

	dev_info(&pdev->dev, "Probe hde device\n");

	id = of_match_device(hde_of_match, &pdev->dev);
	if (id != NULL) {
		struct ic_info *info = (struct ic_info *)id->data;
		if (info != NULL) {
			ic_type = info->ic_type;
			printk("info ic_type 0x%x\n", ic_type);
		} else {
			printk("info is null\n");
		}
	}

	hde = devm_kzalloc(&pdev->dev, sizeof(*hde), GFP_KERNEL);
	if (!hde)
		return -ENOMEM;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	hde->base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(hde->base))
		return PTR_ERR(hde->base);

	hde->irq = platform_get_irq(pdev, 0);
	if (hde->irq < 0)
		return hde->irq;

	dev_info(&pdev->dev, "resource: iomem: %pR mapping to %p, irq %d\n",
			iores, hde->base, hde->irq);

	ret = request_irq(hde->irq, (void *)hde_isr, 0, "hde_isr", 0);
	if (ret) {
		printk("hde : register hde irq failed!\n");
		hde_irq_registered = 0;
	} else {
		hde_irq_registered = 1;
	}
	pm_runtime_enable(&pdev->dev);

	hde_clk  = clk_get(NULL, "hde");
	if (IS_ERR(hde_clk)) {
		pr_err("hde :Failed to get hde clock\n");
		return -1;
	}
	/*get display_pll struct*/
	display_pll = clk_get(NULL, "display_pll");
	if (IS_ERR(display_pll)) {
		printk("hde : clk_get display_pll failed\n");
		return 0;
	}
	reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(reset)) {
		printk("devm_reset_control_get(&pdev->dev, NULL) failed\n");
		return PTR_ERR(reset);
	}

	#ifdef ENABLE_COOLING
	cputherm_register_notifier(&thermal_notifier_block, CPUFREQ_COOLING_START);
	#endif

#if 0 /*if vdd needed will open S900:0.9v;S700:1.05v */
	if (ic_type == IC_TYPE_S700 || ic_type == IC_TYPE_S900) {
		int regulator_return = 0;
		hde->power = devm_regulator_get(&pdev->dev, "corevdd");
		if (IS_ERR(hde->power))
				printk("can not get corevdd regulator,may be this board not need, or lost in dts!\n");
		else {
				if (regulator_set_voltage(hde->power, VDD_1_05V, INT_MAX)) {
				printk("cannot set corevdd to 1050000uV !\n");
				return -EINVAL;
		}
		regulator_return = regulator_enable(hde->power);
	}
}
#endif

	HDE_SPS_PG_CTL = ioremap_nocache(SPS_BASE_PHY, 0x8);
	if (ic_type == IC_TYPE_S900) {
		printk("--SDK S900\n");
		HDE_CMU_COREPLL = ioremap_nocache(CMU_BASE_PHY_S900, 0x16);
		HDE_CMU_HDE_CLK = ioremap_nocache(CMU_BASE_PHY_S900 + 0x3C, 0x8);
		HDE_CMU_DEVRST0 = ioremap_nocache(CMU_BASE_PHY_S900 + 0xa8, 0x8);
		HDE_CMU_DEVCLKEN0 = ioremap_nocache(CMU_BASE_PHY_S900 + 0xa0, 0x8);
	} else {
		printk("--SDK S700\n");
		HDE_CMU_COREPLL = ioremap_nocache(CMU_BASE_PHY_S700, 0x16);
		HDE_CMU_HDE_CLK = ioremap_nocache(CMU_BASE_PHY_S700 + 0x3C, 0x8);
		HDE_CMU_DEVRST0 = ioremap_nocache(CMU_BASE_PHY_S700 + 0xa8, 0x8);
		HDE_CMU_DEVCLKEN0 = ioremap_nocache(CMU_BASE_PHY_S700 + 0xa0, 0x8);
	}
	printk("SPS_PG_CTL = 0x%p\n", HDE_SPS_PG_CTL);
	printk("CMU_HDE_CLK = 0x%p\n", HDE_CMU_COREPLL);
	printk("CMU_HDE_CLK = 0x%p\n", HDE_CMU_HDE_CLK);
	printk("CMU_DEVRST0 = 0x%p\n", HDE_CMU_DEVRST0);
	printk("CMU_DEVCLKEN0 = 0x%p\n", HDE_CMU_DEVCLKEN0);

	disable_irq(hde->irq);
	GHDE = hde;
	GHDEDEV = pdev;
	printk("dev_add %p, hde_clk %p\n", &pdev->dev, hde_clk);
	hde_clk_isenable = 0;

	return 0;
}

static int hde_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	hde_clk_disable();

	clk_put(hde_clk);
	clk_put(display_pll);

	return 0;
}

int hde_mmu_init(void)
{
	extern struct ion_device *owl_ion_device;
	psIonClient = ion_client_create(owl_ion_device, "hde-mmu");
	if (!psIonClient)
		return -ENODEV;
	return 0;
}

void hde_mmu_deinit(void)
{
	if (!psIonClient)
		ion_client_destroy(psIonClient);
}

struct ion_buffer_mapping {
	struct ion_handle *handle;
	struct sg_table *sg_table;
};

struct ion_mmu_buffer_mapping {
	struct ion_buffer_mapping mapping;
	ion_phys_addr_t addr_phys;
	void *addr_virt;
	size_t buf_len;
};

static void ion_buffer_cleanup(struct ion_buffer_mapping *mapping)
{
	if (mapping->handle) {
		ion_free(psIonClient, mapping->handle);
		mapping->handle = NULL;
		mapping->sg_table = NULL;
	}
}

static int ion_buffer_map(int fd, struct ion_buffer_mapping *mapping)
{
	struct ion_handle *handle;
	struct sg_table *sg_table;
	int ret = 0;

	handle = ion_import_dma_buf(psIonClient, fd);
	if (IS_ERR_OR_NULL(handle)) {
		ret = PTR_ERR(handle);
		HDE_ERROR("ion_import_dma_buf failed (fd=%d, err=%d)\n", fd, ret);
		goto err_out;
	}
	mapping->handle = handle;

	sg_table = ion_sg_table(psIonClient, handle);
	if (IS_ERR_OR_NULL(sg_table)) {
		ret = PTR_ERR(sg_table);
		HDE_ERROR("ion_sg_table failed (handle=%p, err=%d)\n", handle, ret);
		goto err_out;
	}
	mapping->sg_table = sg_table;

err_out:
	if (ret < 0)
		ion_buffer_cleanup(mapping);

	return ret;
}

static void ion_mmu_buffer_cleanup(struct ion_mmu_buffer_mapping *mapping)
{
	if (mapping->addr_virt) {
		ion_unmap_kernel(psIonClient, mapping->mapping.handle);
		mapping->addr_virt = NULL;
		mapping->addr_phys = 0;
		mapping->buf_len = 0;
	}

	ion_buffer_cleanup((struct ion_buffer_mapping *)mapping);
}

static int ion_mmu_buffer_map(int fd, struct ion_mmu_buffer_mapping *mapping)
{
	void *addr;
	int ret;
	ret = ion_buffer_map(fd, (struct ion_buffer_mapping *)mapping);
	if (ret)
		goto err_out;

	ret = ion_phys(psIonClient, mapping->mapping.handle, &mapping->addr_phys, &mapping->buf_len);
	if (ret) {
		HDE_ERROR("ion_phys failed (handle=%p, err=%d)\n", mapping->mapping.handle, ret);
		goto err_out;
	}

	addr = ion_map_kernel(psIonClient, mapping->mapping.handle);
	if (IS_ERR_OR_NULL(addr)) {
		ret = PTR_ERR(addr);
		HDE_ERROR("ion_map_kernel failed (handle=%p, err=%d)\n", mapping->mapping.handle, ret);
		goto err_out;
	}
	mapping->addr_virt = addr;

err_out:
	if (ret < 0)
		ion_mmu_buffer_cleanup(mapping);

	return ret;
}

static int mmu_fill(struct mmu_fill_package *mmupkg)
{
	struct ion_buffer_mapping *buf_mapping = NULL;
	struct ion_mmu_buffer_mapping *mmu_mapping = NULL;
	u32 tiles_per_entry = (mmupkg->common.w + HDE_TILE_SIZE - 1) / HDE_TILE_SIZE;
	u32 pages_per_entry = tiles_per_entry * NUM_PAGES_PER_TILE;
	u32 total_entrys = (mmupkg->common.h + HDE_TILE_SIZE - 1) / HDE_TILE_SIZE;
	int i, ret = 0;

	if (pages_per_entry > NUM_PAGES_PER_MMU_ENTRY) {
		HDE_ERROR("unsupportd buffer size %u x %u\n", mmupkg->common.w, mmupkg->common.h);
		ret = -EINVAL;
		goto err_out;
	}

	buf_mapping = kzalloc(sizeof(*buf_mapping) * mmupkg->n_bufs, GFP_KERNEL);
	mmu_mapping = kzalloc(sizeof(*mmu_mapping) * mmupkg->n_bufs, GFP_KERNEL);
	if (!buf_mapping || !mmu_mapping) {
		HDE_ERROR("kzalloc failed\n");
		ret = -ENOMEM;
		goto err_out;
	}

	for (i = 0; i < mmupkg->n_bufs; i++) {
		struct scatterlist *sg;
		struct mmu_entry *mmu_entry, *mmu_entry_end;
		u32 n_page = 0;
		int j;

		ret = ion_buffer_map(mmupkg->private[i].fd_buf, &buf_mapping[i]);
		if (ret)
			goto err_out;

		ret = ion_mmu_buffer_map(mmupkg->private[i].fd_mmu, &mmu_mapping[i]);
		if (ret)
			goto err_out;

		if (mmu_mapping[i].buf_len < total_entrys * sizeof(struct mmu_entry)) {
			HDE_ERROR("mmu buffer too small (len=%zu)\n", mmu_mapping[i].buf_len);
			ret = -EINVAL;
			goto err_out;
		}

		mmupkg->private[i].phys_addr = (u32)mmu_mapping[i].addr_phys;
		mmu_entry = (struct mmu_entry *)mmu_mapping[i].addr_virt;
		mmu_entry_end = mmu_entry + total_entrys;

		for_each_sg(buf_mapping[i].sg_table->sgl, sg, buf_mapping[i].sg_table->nents, j) {
			u32 offset;
			dma_addr_t phys_base = sg_phys(sg);
			for (offset = 0; offset < sg->length; offset += HDE_PAGE_SIZE) {
				mmu_entry->phys[n_page] = (u32)(phys_base + offset);
				if (++n_page == pages_per_entry) {
					n_page = 0;
					if (++mmu_entry == mmu_entry_end)
						goto exit_loop;
				}
			}
		}

	exit_loop:
		/* Make sure the cache of mmu buffer is clean. */
		dma_sync_sg_for_device(NULL, mmu_mapping[i].mapping.sg_table->sgl,
				mmu_mapping[i].mapping.sg_table->nents, DMA_TO_DEVICE);
	}

err_out:
	if (buf_mapping) {
		for (i = 0; i < mmupkg->n_bufs; i++)
			ion_buffer_cleanup(&buf_mapping[i]);
		kfree(buf_mapping);
	}

	if (mmu_mapping) {
		for (i = 0; i < mmupkg->n_bufs; i++)
			ion_mmu_buffer_cleanup(&mmu_mapping[i]);
		kfree(mmu_mapping);
	}

	return ret;
}

int hde_ioctl_fill_mmu(unsigned long arg)
{
	struct mmu_fill_package __user *upackage = (struct mmu_fill_package __user *)arg;
	struct mmu_fill_package *kpackage = NULL;
	u32 n_bufs;
	int err = 0;

	if (get_user(n_bufs, &upackage->n_bufs))
		return -EFAULT;

	kpackage = kmalloc(sizeof(*kpackage) + n_bufs * sizeof(kpackage->private[0]), GFP_KERNEL);
	if (!kpackage) {
		err = -ENOMEM;
		goto err_clean;
	}
	kpackage->n_bufs = n_bufs;

	err = copy_from_user(&kpackage->common, &upackage->common,
			sizeof(kpackage->common) + n_bufs * sizeof(kpackage->private[0]));
	if (err)
		goto err_clean;

	err = mmu_fill(kpackage);
	if (err)
		goto err_clean;

	err = copy_to_user(&upackage->private[0], &kpackage->private[0],
			n_bufs * sizeof(kpackage->private[0]));
err_clean:
	if (kpackage)
		kfree(kpackage);

	return err;
}
static struct platform_driver hde_platform_driver = {
	.driver = {
	.name = DEVDRV_NAME_HDE,
	.owner = THIS_MODULE,
	.of_match_table = hde_of_match,
	},
	.suspend = hde_suspend,
	.resume = hde_resume,
	.probe = asoc_hde_probe,
	.remove = hde_remove,
};

static struct miscdevice hde_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_HDE,
	.fops = &hde_fops,
};

static int hde_init(void)
{
	int ret;

	printk("#### insmod hde driver!\n");

	/* start insmod, register device.*/
	ret = misc_register(&hde_miscdevice);
	if (ret) {
		printk("register hde misc device failed!\n");
		goto err0;
	}

	ret = platform_driver_register(&hde_platform_driver);
	if (ret) {
		printk("register gpu platform driver4pm error!\n");
		goto err1;
	}
	hde_mmu_init();

	mutex_init(&m_mutex);
	mutex_init(&m_freq_adjust_mutex);
	hde_cur_slot_num = 0;

	return 0;

err1:
	free_irq(GHDE->irq, 0);
	misc_deregister(&hde_miscdevice);

err0:
	return ret;
}

static void hde_exit(void)
{
	if (hde_irq_registered)
		free_irq(GHDE->irq, 0);

	hde_mmu_deinit();
	misc_deregister(&hde_miscdevice);
	platform_driver_unregister(&hde_platform_driver);

	printk("hde module unloaded\n");
}

module_init(hde_init);
module_exit(hde_exit);

MODULE_AUTHOR("Actions Semi Inc");
MODULE_DESCRIPTION("HDE kernel module");
MODULE_LICENSE("GPL");
