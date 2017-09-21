#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
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

#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include "vde_core.h"
#include "vde_drv.h"


void __iomem *share_mem_reg;
#define IC_TYPE_NEW
#define IC_TYPE_S900		0x900
#define IC_TYPE_S700		0x700
#define VDE_REGISTER_BASE       0xe0280000
#define IRQ_ASOC_VDE            (32 + 50)
#define DEVDRV_NAME_VDE         "mali0"
#define VDE_REG_BASE		VDE_REGISTER_BASE
#define SPS_BASE_PHY		0xe012e000
#define CMU_BASE_PHY_S900		0xe0160000
#define CMU_BASE_PHY_S700		0xe0168000
#define VDE_REG_BACKUP_NUM      76
#define MAX_INSTANCE_NUM	8
#define VDD_0_9V		900000
#define VDD_1_0V		1000000
#define VDD_1_05V		1050000
#define VDD_1_1V		1100000
#define VDD_1_15V		1150000
#define VDD_1_175V		1175000
#define VDD_1_2V		1200000
#define VDE_DEBUG_PRINTK	printk("%s %d\n", __FILE__, __LINE__)

#define VDE_DBG(...)	{ if (debug_enable == 1)  printk(__VA_ARGS__);  }
#define VDE_DBG2(...)	{ if (debug_enable == 2)  printk(__VA_ARGS__);  }

#define VDE_FREQ_DEFAULT	360

/*minimize 4 div*/
#define VDE_FREQ_D1		180

#define VDE_FREQ_720P		270
#define VDE_FREQ_1080P		360
#define VDE_FREQ_MULTI		720
#define VDE_FREQ_4Kx2K		720
#define VDE_FREQ_4Kx2K_S900	1080

#define CLK_NAME_DEVCLK "dev_clk"
#define CLK_NAME_DISPLAYPLL "display_pll"
#define CLK_NAME_VDE_CLK "vde"

static int vde_cur_slot_id = -1;

/*jpeg slice pattern, can't free if frame is not completed*/
static int vde_slice_slot_id = -1;

static unsigned int vde_cur_slot_num;

static int vde_set_voltage_limit;

/*0:normal, -1:error;*/
static int vde_last_status;

static int multi_instance_mode;

static int debug_enable;

static int autofreq_enable = 1;
static int adjust_freq_flag = 1;

static int vde_pre_freq;

static int ic_type = IC_TYPE_S900;

static void __iomem *VDE_SPS_PG_CTL;

static void __iomem *VDE_CMU_DEVRST0;

static void __iomem *VDE_CMU_DEVCLKEN0;

static void __iomem *VDE_CMU_VDE_CLK;

static void __iomem *VDE_CMU_COREPLL;

static int vde_irq_registered;

static int vde_open_count;

static unsigned long vde_cur_need_freq;

/*vde shoule be occupied when jpeg slice pattern that
	may needs some times when decoded*/
static int vde_occupied;

/*for multi process*/
static struct mutex m_mutex;
static struct mutex m_freq_adjust_mutex;

static DECLARE_WAIT_QUEUE_HEAD(waitqueue);

/*vde can accept a new RUN order if finish*/
static int vde_idle_flag = 1;

static int vde_clk_isenable;

typedef struct {

	unsigned int regs[VDE_REG_BACKUP_NUM];

} vde_user_data_t;

struct asoc_vde_dev {

	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	int irq;
	unsigned int setting;
	struct regulator *power;
};

static struct asoc_vde_dev *GVDE;

static struct clk *vde_clk;

static struct clk *display_pll;

static struct reset_control *reset;

static struct platform_device *gvdedev;

static void vde_write(struct asoc_vde_dev *vde, u32 val, u32 reg)
{
	/*printk("vde_write %x, val %x\n",(unsigned int)vde->base + reg, val); */
	void volatile *write_add = (void *)(vde->base + reg);

	writel_relaxed(val, write_add);

}
static u32 vde_read(struct asoc_vde_dev *vde, u32 reg)
{

	unsigned int data = 0;
	data = readl_relaxed((void volatile *)(vde->base + reg));

	/*VDE_DBG("vde_read %x, val %x\n", (unsigned int)vde->base + reg, data); */
	return data;
}

#define act_readl(a)		vde_read(GVDE, a)
#define act_writel(v, a)	vde_write(GVDE, v, a)
#define re_reg(a)		(act_readl(a))
#define wr_reg(a, v)		(act_writel(v, a))

struct ic_info {

	int ic_type;

};

static struct ic_info s900_data = {
	.ic_type = IC_TYPE_S900,
};

static struct ic_info s700_data = {
	.ic_type = IC_TYPE_S700,
};

static const struct of_device_id vde_of_match[] = {
	{.compatible = "actions,s900-vde", .data = &s900_data},
	{.compatible = "actions,s700-vde", .data = &s700_data},
	{}
};

MODULE_DEVICE_TABLE(of, vde_of_match);

typedef enum {
	SLOT_IDLE,
	SLOT_OCCUPIED,
	SLOT_COMPLETED
} slot_state_e;

typedef struct slot_s {

	struct completion isr_complete;
	slot_state_e state;
	unsigned long clientregptr;
	unsigned int vde_status;
	unsigned int slice_mode;
	int pid;
	vde_user_data_t user_data;

} slot_t;

static slot_t slot[MAX_INSTANCE_NUM];

static int slot_reset(int i)
{

init_completion(&slot[i].isr_complete);

	slot[i].state = SLOT_IDLE;
	slot[i].clientregptr = 0;
	slot[i].slice_mode = 0;
	slot[i].pid = -1;
	if (vde_cur_slot_num > 0)
		vde_cur_slot_num--;

	return 0;
}

static int slot_get(void)
{

	int i;
	for (i = 0; i < MAX_INSTANCE_NUM; i++) {
		if (slot[i].state == SLOT_IDLE) {
			init_completion(&slot[i].isr_complete);
			slot[i].state = SLOT_OCCUPIED;
			vde_cur_slot_num++;
			slot[i].pid = task_tgid_vnr(current);

			return i;
		}
	}

	if (i == MAX_INSTANCE_NUM) {
		printk("vde : no idle slot, max %d slots\n", MAX_INSTANCE_NUM);

		return -1;
	}

	return -1;
}

static int slot_complete(int i, unsigned int vde_status)
{
	if (slot[i].state != SLOT_OCCUPIED) {
		printk("vde : slot is idle, staus error\n");
		return -1;
	}

	slot[i].vde_status = vde_status;
	slot[i].state = SLOT_COMPLETED;

	return 0;
}

static int vde_query_interval(unsigned int reg4)
{
	unsigned int mb_w = (reg4 >> 23) & 0x1ff;
	unsigned int mb_h = (reg4 >> 15) & 0xff;
	unsigned int r = (mb_w * mb_h * 300) / (1000 * vde_pre_freq);

	if (r < 5)
		r = 5;

	return r;
}

static void vde_waitforidle(void)
{
	int ctr = 0;
	int v = vde_read(GVDE, 0x4);
	int v4 = vde_read(GVDE, 0x10);
	if ((v & 0x1) == 0x1) {
		VDE_DBG("vde : vde is on air\n");

		do {
			msleep(vde_query_interval(v4));

			ctr++;

			if (ctr == 100) {
				printk("vde : vde always busy\n");

				break;
			}
		} while (((vde_read(GVDE, 0x4)) & 0x1) == 0x1);

	}
}

static int is_rlc_mode(unsigned int reg4)
{
	if (((reg4 >> 12) & 0x7) == 0x3)
		return 0;

	return (reg4 >> 3) & 0x1;
}

int get_mode(void)
{
	unsigned int v = vde_read(GVDE, 0x4);

	if (((v >> 12) & 0x7) == 0x3)
		return 1;

	if (is_rlc_mode(v))
		return 1;

	return 0;
}

/*============== funcs:clock, power, reset, irq =============*/
static void vde_do_reset(void)
{
	int ctor = 0;
	unsigned int value_assigned;
	unsigned int vde_state = vde_read(GVDE, 0x4);
	int reset_bit = 0;

	if (ic_type == IC_TYPE_S900) {
			reset_bit = 19;
	} else if (ic_type == IC_TYPE_S700) {
			reset_bit = 10;
	} else {
			printk("vde :  error SDK \n");
	}
	/*avoid reset vde when vde is working*/
	while ((vde_state & 0x1) == 0x1) {
		msleep(5);

		vde_state = vde_read(GVDE, 0x4);

		if (vde_state & 0x1da000) {
			printk("warning:reset vde when working wrong. state: 0x%x\n", vde_state);
			break;
		}

		if (ctor > 10) {
			printk("warning, reset vde. state: 0x%x, ctor(%d) > 10\n",
				vde_state, ctor);
			break;
		}

		ctor++;
	}

	reset_control_reset(reset);

	VDE_DBG("vde : %d checking reset .............\n", __LINE__);
	value_assigned = readl_relaxed(VDE_CMU_DEVRST0);

	while (((value_assigned>>reset_bit) & 0x1) != 0x1) {
		printk("vde : Fail to reset vde, DEVEN0 = 0x%x, \
			CMU_DEVRST0 = 0x%x PG_CTL = 0x%x\n",
				readl_relaxed(VDE_CMU_DEVCLKEN0),
					readl_relaxed(VDE_CMU_DEVRST0),
						readl_relaxed(VDE_SPS_PG_CTL));

		value_assigned |= (0x1<<reset_bit);
		writel_relaxed(value_assigned, VDE_CMU_DEVRST0);
		usleep_range(50*1000, 50*1000);
		value_assigned = readl_relaxed(VDE_CMU_DEVRST0);
	}

	vde_idle_flag = 1;
	vde_occupied = 0;
	vde_last_status = 0;

	printk("vde : vde reset end\n");
	return;
}

static void vde_reset(void)
{
	usleep_range(5*1000, 5*1000);
	vde_do_reset();
}

static void vde_clk_enable(void)
{
	int res = 0;
	if (vde_clk_isenable != 0)
		return;

	/*power on*/

	printk("dev add %p,vde_clk %p\n", &gvdedev->dev, vde_clk);
	res = pm_runtime_get_sync(&gvdedev->dev);
	if (res < 0) {
		printk("vde_clk_enable: pm_runtime_get_sync failed\n");
		return;
	}

	/*enable clk*/
	clk_prepare_enable(vde_clk);

	printk("set vde clock to displaypll\n");
	if (clk_set_parent(vde_clk, display_pll))
		printk("failed to set vde parent to display_pll\n");

	vde_clk_isenable = 1;
}

static void vde_clk_disable(void)
{
	int res = 0;

	VDE_DBG("vde : vde_clk_disable, In\n");
	if (vde_clk_isenable == 0)
		return;

	vde_waitforidle();
	/*disable clk*/
	clk_disable_unprepare(vde_clk);

	vde_clk_isenable = 0;
	/*power off*/
	res = pm_runtime_put_sync(&gvdedev->dev);
	if (res < 0) {
		printk("vde_clk_disable: pm_runtime_put_sync failed\n");
		return;
	}

	VDE_DBG("vde : vde_clk_disable, Out\n");
}

#define MAX_VDE_REG_RETRY_TIME  5
/* enable int */
static inline void enable_vde_irq(void)
{
}

static inline void disable_vde_irq(void)
{
	unsigned int v;
	int c;

	c = MAX_VDE_REG_RETRY_TIME;

	v = vde_read(GVDE, 0x4);

	v &= ~(1<<8);
	vde_write(GVDE, v, 0x4);

	while ((vde_read(GVDE, 0x4)) & (0x1<<8) && c-- > 0) {
		printk("vde : can not disable irq, write %x %x\n",
			(VDE_REG_BASE) + 0x4, vde_read(GVDE, 0x4));
		vde_write(GVDE, v, 0x4);
	}
}
/*=====================================================*/

static void vde_drv_updatereg(int id)
{
	int i;
	/*we never handle reg0, reg1*/
	for (i = 1; i < VDE_REG_BACKUP_NUM; i++)
		slot[id].user_data.regs[i] = (unsigned int)(vde_read(GVDE, i*4));

}

static void vde_drv_showreg(int id)
{
	if (id >= 0) {
		VDE_DBG("vde : (showReg-1) 0x%08x 0x%08x 0x%08x 0x%08x\n",
			slot[id].user_data.regs[1], slot[id].user_data.regs[2],
				slot[id].user_data.regs[3], slot[id].user_data.regs[4]);
	} else {
		VDE_DBG("vde : (showReg-2) 0x%08x 0x%08x 0x%08x 0x%08x\n",
			(unsigned int)(vde_read(GVDE, 1*4)),
				(unsigned int)(vde_read(GVDE, 2*4)),
					(unsigned int)(vde_read(GVDE, 3*4)),
						(unsigned int)(vde_read(GVDE, 4*4)));
	}
}

/**
 * This function is VDE ISR.
 */
irqreturn_t VDE_ISR(int irq, void *dev_id)
{
	unsigned int s;
	disable_vde_irq();
	slot_complete(vde_cur_slot_id, vde_read(GVDE, 0x4));
	vde_drv_updatereg(vde_cur_slot_id);

	/*when bistream empty or jpeg slice ready, and not frame ready.*/
	s = vde_read(GVDE, 0x4);

	if (((s & (0x1<<17)) || (s & (0x1<<14))) && !(s & (0x1<<12))) {
		slot[vde_cur_slot_id].slice_mode = 1;
		vde_slice_slot_id = vde_cur_slot_id;
		slot[vde_slice_slot_id].state = SLOT_OCCUPIED;
		printk("vde 171412,status %x", s);
	} else {
		slot[vde_cur_slot_id].slice_mode = 0;
		vde_occupied = 0;
	}

	slot[vde_cur_slot_id].vde_status = vde_read(GVDE, 0x4);
	vde_idle_flag = 1;

	VDE_DBG2("isr : status = 0x%x\n", slot[vde_cur_slot_id].vde_status);

	if (slot[vde_cur_slot_id].vde_status & 0x1da000) {
		/*when meet some error.*/
		vde_drv_showreg(-1);
		vde_last_status = -1;
	} else {
		/*only if decoding normally, enable clk gating
			when decoder one frame.*/
		unsigned int r2 = vde_read(GVDE, 0x8);
		r2 |= (0x1 << 10);
		vde_write(GVDE, r2, 0x8);
	}

	complete(&(slot[vde_cur_slot_id].isr_complete));
	wake_up_interruptible(&waitqueue); /*wake up*/

	return IRQ_HANDLED;
}

static void vde_drv_writereg(unsigned int regno, unsigned int value)
{
	/*unsigned int value_recover;*/
	vde_write(GVDE, value, regno*4);
	/*
	value_recover = (unsigned int)(re_reg(regno*4));

	while(value_recover != value) {
		printk("vde : Fail to write reg 0x%x,
			(input,recover)=(0x%x, 0x%x)\n", regno, value, value_recover);
		wr_reg(regno*4, value);
		value_recover = (unsigned int)(re_reg(regno*4));
	}
	*/
}

static void vde_drv_flushreg(int id, void __user *v)
{
	int i, rt;
	unsigned int value, tmpval;
	unsigned int width;
	unsigned int mode;

	rt = copy_from_user(&(slot[id].user_data.regs[0]),
				v, sizeof(vde_user_data_t));
	if (rt != 0)
		printk("vde : Warning: copy_from_user failed, rt=%d\n", rt);

	value = slot[id].user_data.regs[2];
	tmpval = slot[id].user_data.regs[4];
	width = ((tmpval >> 23) & 0x1ff) << 4;
	 mode = (tmpval >> 12) & 0x7;

#if 1 /*IC_TYPE_S900*/
	value &= (~(1<<23));
#else
	if (mode == 0) {
		/*
		H264 all not only 4K &&((width > 2048))
		tmpval |= (1<<10); // end frame flag --- condition 2: bit[10] = 1
		slot[id].user_data.regs[4] = tmpval;
		*/

		value |= (1<<23);
	} else {
		value &= (~(1<<23));
	}
#endif
		/*
					value &= (~(1<<10)); //disable clock gating
		*/
	value &= (~(0xff));

	slot[id].user_data.regs[2] = value;

	/*we never handle reg0, reg1*/
	for (i = 2; i < VDE_REG_BACKUP_NUM; i++) {
		/*printk("vde : %d",i);*/
		vde_drv_writereg(i, slot[id].user_data.regs[i]);
	}
}

VDE_Status_t vde_query_status(unsigned int vde_status)
{
	if (vde_status & (0x1<<12)) {
		VDE_DBG2("vde : vde status gotframe, status: 0x%x\n", vde_status);
		return VDE_STATUS_GOTFRAME;

	}
#if 1 /*IC_TYPE_S900*/
	else if (vde_status & (0x1<<19)) {
		VDE_DBG("vde:vde status directmv full, status: 0x%x\n", vde_status);
		return VDE_STATUS_DIRECTMV_FULL;
	} else if (vde_status & (0x1<<20)) {
		printk("vde : vde status RLC error, status: 0x%x\n", vde_status);
		return VDE_STATUS_BUS_ERROR;
	}
#endif
	else if (vde_status & (0x1<<18)) {
		VDE_DBG("vde : vde status timeout, status: 0x%x\n", vde_status);
		return VDE_STATUS_TIMEOUT;
	} else if (vde_status & (0x1<<16)) {
		VDE_DBG("vde:vde status stream error, status: 0x%x\n", vde_status);
		return VDE_STATUS_STREAM_ERROR;
	} else if (vde_status & (0x1<<17)) {
		VDE_DBG("-vde:slice ready-");
		return VDE_STATUS_JPEG_SLICE_READY;
	} else if (vde_status & (0x1<<15)) {
		printk("vde : vde status ASO error, status: 0x%x\n", vde_status);
		return VDE_STATUS_UNKNOWN_ERROR;
	} else if (vde_status & (0x1<<14)) {
		VDE_DBG("vde:vde status stream empty, status: 0x%x\n", vde_status);
		return VDE_STATUS_STREAM_EMPTY;
	} else if (vde_status & (0x1<<13)) {
		printk("vde : vde status Bus error, status: 0x%x\n", vde_status);
		return VDE_STATUS_BUS_ERROR;
	} else {
		printk("vde:vde status unknow error, status: 0x%x\n", vde_status);
		return VDE_STATUS_UNKNOWN_ERROR;
	}
}

/*===============start============= fre interrelated ==================*/
typedef unsigned long VDE_FREQ_T;

VDE_FREQ_T vde_do_set_freq(VDE_FREQ_T new_rate)
{
	unsigned long rate;
	int ret;
	vde_clk = clk_get(NULL, (const char *)CLK_NAME_VDE_CLK);
	if (IS_ERR(vde_clk)) {
			printk("vde : clk_get_sys(CLK_NAME_VDE_CLK, NULL) failed\n");
			return 0;
  }
	ret = clk_set_rate(vde_clk, new_rate*1000*1000); /*设置clk 频率， 单位hz*/
	if (ret != 0) {
		printk("vde : clk_set_rate new_rate %dM,  failed! \n", (unsigned int)new_rate);
		return 0;
	}
  rate = clk_get_rate(vde_clk);/*获取clk 当前频率，单位hz，例如hosc：24000000*/
  printk("new :%d  old:%d\n", (int)(rate/(1000*1000)/10), (int)(new_rate/10));
  return rate/(1000*1000);
}


static int vde_set_corevdd(struct regulator *power, int voltage)
{
	if (!IS_ERR(power)) {
			if (regulator_set_voltage(power, voltage, INT_MAX)) {
			printk("cannot set corevdd to %duV !\n", voltage);
			return -EINVAL;
		}
	}
	return 0;
}

static int vde_set_parent_pll(const char *parent_name)
{
    struct clk *pre_parent;
    struct clk *cur_parent;
    struct clk *vde_clk;
    int ret;


	cur_parent = clk_get(NULL, (const char *)parent_name);/*根据clk_name获取clk结构体*/

	if (IS_ERR(cur_parent)) {
				printk("vde : clk_get_sys(..) failed\n");
				return -1;
	}
	vde_clk = clk_get(NULL, (const char *)CLK_NAME_VDE_CLK); /*根据clk_name获取clk结构体*/
	if (IS_ERR(vde_clk)) {
				printk("vde : clk_get_sys(CLK_NAME_VDE_CLK, NULL) failed\n");
				return -1;
	}
	pre_parent = clk_get_parent(vde_clk);

	/*Compare two clk sources like this??*/
	if (pre_parent == cur_parent) {
		/*printk("vde : vde_set_parent_pll: parent pll no changed! \n");*/
	    return 0;
	}

	ret = clk_set_parent(vde_clk, cur_parent);
	if (ret != 0) {
	    printk("vde : clk_set_parent failed(%d)\n", ret);
			/*fixme : now what?*/
	    return -1;
	}
    return 0;
}

/* set vde fre and return it.If fail, reruen 0.*/
static VDE_FREQ_T vde_setfreq(VDE_FREQ_T freq_mhz)
{
	unsigned long new_rate;

	new_rate = freq_mhz;

	if (ic_type == IC_TYPE_S700) {
		if (new_rate >= VDE_FREQ_4Kx2K) {
			if (vde_set_corevdd(GVDE->power, VDD_1_175V) != 0) {
				printk("vde : set vdd1 err \n");
				return 0;
			}
			if (vde_set_parent_pll(CLK_NAME_DISPLAYPLL) != 0) {
				printk("vde : set parent_pll1 err \n");
				return 0;
			}
		} else if (new_rate >= VDE_FREQ_MULTI) {
			if (vde_set_corevdd(GVDE->power, VDD_1_175V) != 0) {
				printk("vde : set vdd2 err \n");
				return 0;
			}
			if (vde_set_parent_pll(CLK_NAME_DISPLAYPLL) != 0) {
				printk("vde : set parent_pll2 err \n");
				return 0;
			}
		} else if (new_rate >= VDE_FREQ_1080P) {
			if (vde_set_corevdd(GVDE->power, VDD_1_05V) != 0) {
				printk("vde : set vdd3 err \n");
				return 0;
			}
			if (vde_set_parent_pll(CLK_NAME_DEVCLK) != 0) {
				printk("vde : set parent_pll3 err \n");
				return 0;
			}
		} else if (new_rate >= VDE_FREQ_720P) {
			if (vde_set_corevdd(GVDE->power, VDD_1_05V) != 0) {
				printk("vde : set vdd err4 \n");
				return 0;
			}
			if (vde_set_parent_pll(CLK_NAME_DEVCLK) != 0) {
				printk("vde : set parent_pll4 err \n");
				return 0;
			}
		} else {
			if (vde_set_corevdd(GVDE->power, VDD_1_05V) != 0) {
				printk("vde : set vdd err5 \n");
				return 0;
			}
			if (vde_set_parent_pll(CLK_NAME_DEVCLK) != 0) {
				printk("vde : set parent_pll5 err \n");
				return 0;
			}
		}
	}

	return vde_do_set_freq(new_rate);
}

/*extern int vde_status_changed(int status);*/
static int vde_adjust_freq(void)
{
	int f, ret;
	unsigned int v;
	unsigned int width;
	unsigned int height;
	unsigned int jpeg_mode;
	char *mode[8] = {"H264", "MPEG4", "H263", "JPEG", "VC-1", "RV89", "MPEG12", "Other"};
	VDE_DBG2("vde : vde_adjust_freq()\n");
	v = vde_read(GVDE, 0x10);
	width = ((v >> 23) & 0x1ff) << 4;
	height = ((v >> 15) & 0xff) << 4;
	jpeg_mode = (v >> 12) & 0x7;

	if (jpeg_mode == 0x11) {
		/*jpeg_mode*/
		f = VDE_FREQ_1080P;
	} else {
		if (width > 1920) {
#ifdef VDE_PLL_ALWAYS_BELOW_360M
			f = VDE_FREQ_1080P;
#else
		if (ic_type == IC_TYPE_S900)
			f = VDE_FREQ_4Kx2K_S900;
		else
			f = VDE_FREQ_4Kx2K;
#endif
		} else if (width > 1280) {
			f = VDE_FREQ_1080P;
		} else if (width > 720) {
			f = VDE_FREQ_720P;
		} else {
			f = VDE_FREQ_D1;
		}
	}

	if (vde_cur_slot_num > 1 || multi_instance_mode == 1) {
		VDE_DBG("vde : vde_adjust_freq: vde_cur_slot_num: %d, \
				multi_instance_mode: %d", vde_cur_slot_num,
					multi_instance_mode);
#ifdef VDE_PLL_ALWAYS_BELOW_360M
	if (f < VDE_FREQ_1080P)
		f = VDE_FREQ_1080P;
#else
	if (f < VDE_FREQ_MULTI)
		f = VDE_FREQ_MULTI;
#endif
	}

	if (autofreq_enable == 0)
		f = VDE_FREQ_DEFAULT;

	if (autofreq_enable == 2) {
#ifdef VDE_PLL_ALWAYS_BELOW_360M
		f = VDE_FREQ_1080P;
#else
		if (ic_type == IC_TYPE_S900)
			f = VDE_FREQ_4Kx2K_S900;
		else
			f = VDE_FREQ_4Kx2K;
#endif
	}

	if (autofreq_enable == 3)
		f = VDE_FREQ_MULTI;

	if (vde_pre_freq != f) {
		vde_cur_need_freq = (unsigned long)f * 1000000;

		ret = vde_setfreq(f);
		if (ret != 0) {
			vde_pre_freq = f;
			VDE_DBG("vde:adjust: [w, h, f] = [%d, %d, %s], autoFreq = %d, \
					mulIns = %d, freq.[need, real] = [%d, %d] \n",
				width, height, mode[jpeg_mode], autofreq_enable,
				multi_instance_mode, (int)(vde_cur_need_freq/1000000), ret);
		} else {
			VDE_DBG("vde : set %dM Failed, set default freq&pll %dM\n",
					f, VDE_FREQ_DEFAULT);
			vde_setfreq(VDE_FREQ_DEFAULT);
			vde_pre_freq = VDE_FREQ_DEFAULT;
		}
	}
	return 0;
}

/*========end================= fre interrelated ================*/

static long vde_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int left_time, id, s, slot_id, jpeg_on_going;

	switch (cmd) {
	case VDE_RUN:
	/* return 0; */

	if (arg != 0) {
	if (ic_type == IC_TYPE_S700) {
			int value;
			value = readl_relaxed(share_mem_reg);
			if (value != 0) {
					printk("vde : share_mem_reg : %x, Not use by VDE , need change\n", value);
					writel_relaxed(0, share_mem_reg);
					value = readl_relaxed(share_mem_reg);
					printk("vde2 : share_mem_reg %x\n", value);
					vde_reset();
			}
		}
		jpeg_on_going = 0;

		mutex_lock(&m_mutex);

		/*continue if completed, interrupted or timeout*/
		left_time = wait_event_interruptible_timeout(
				waitqueue, vde_idle_flag == 1,
					msecs_to_jiffies(5*1000) + 1);
		if (unlikely(left_time == 0))  {
			printk("vde :wait_event_interruptible_timeout 5s\n");
				vde_reset();
			mutex_unlock(&m_mutex);
			return VDE_STATUS_UNKNOWN_ERROR;
		}

		if (vde_last_status == -1)
			vde_reset();

		if (vde_occupied == 1) {
			/*FIXME: Arg should not change that
				next slice comes in slice pattern*/
			if (arg != slot[vde_slice_slot_id].clientregptr) {
				printk("vde : VDE_RUN --- UNKNOWN_ERROR\n");
				mutex_unlock(&m_mutex);
				return VDE_STATUS_UNKNOWN_ERROR;
			} else {
				jpeg_on_going = 1;
			}
		}

		if (jpeg_on_going) {
			id = vde_slice_slot_id;
		} else {
		/*Get a slot. It needs several slots
			if multitasks run VDE, that can't VDE_QUERY in time*/
			id = slot_get();
			if (id < 0) {
				mutex_unlock(&m_mutex);
				return VDE_STATUS_UNKNOWN_ERROR;
			}
		}

		/*backup data*/
		vde_cur_slot_id = id;
		slot[id].clientregptr = arg;
		vde_drv_flushreg(id, (void __user *)arg);

		vde_occupied = 1;
		vde_idle_flag = 0;
		mutex_lock(&m_freq_adjust_mutex);

		if (adjust_freq_flag == 1) {
				vde_adjust_freq();
		}

		if (ic_type == IC_TYPE_S700) {
				s = 0;
				s = 0xFFFF;
				vde_write(GVDE, s, 0xC0);
		}
		/*act_readl(0x4);*/
		s = 0;
		s |= 1;

		enable_vde_irq();

		vde_write(GVDE, s, 0x4);

		#if 0 /* for debug */
		if (tag == 1) {
			if (vde_read(GVDE, 0x4) != 0x1) {
				printk("vde: %d VDE_RUN 0x%x\n",
					__LINE__, vde_read(GVDE, 0x4));
			}
		}
		#endif
		mutex_unlock(&m_freq_adjust_mutex);
		mutex_unlock(&m_mutex);
		return id;

	} else {
		printk("vde:Fail to execute VDE RUN, maybe \
			arg (0x%lx) wrong\n", (unsigned long)arg);
		return VDE_STATUS_UNKNOWN_ERROR;
	}

	break;

	case VDE_QUERY:

		slot_id = (int)arg;
		VDE_DBG2("vde: -VDE_QUERY- 0. query NO.%d slot\n", slot_id);
		/* return VDE_STATUS_GOTFRAME;*/
		if (vde_irq_registered) {
			VDE_Status_t s;
			mutex_lock(&m_mutex);

			/*check interrupted*/
			left_time = wait_for_completion_timeout(
					&(slot[slot_id].isr_complete),
						msecs_to_jiffies(5*1000) + 1);

			if (unlikely(left_time == 0))  {
				printk("QUERY: wait timeout, VDE_STATUS_DEAD-> \
					vde_reset(%d, %d)\n", slot_id, vde_cur_slot_id);
						vde_reset();
				s = VDE_STATUS_DEAD;
			} else {
				/* normal case */
				int rt;
				rt = copy_to_user(
					(void __user *)slot[slot_id].clientregptr,
						&(slot[slot_id].user_data.regs[0]),
							sizeof(vde_user_data_t));
				if (rt != 0) {
					printk("vde : VDE_QUERY: ---> Warning: \
						copy_to_user failed, rt=%d\n", rt);
				}
				s = vde_query_status(slot[slot_id].vde_status);
			}

			/* free slot */
			if ((slot[vde_cur_slot_id].slice_mode == 0)
					||  (s == VDE_STATUS_DEAD)) {
				slot_reset(slot_id);
			}

			mutex_unlock(&m_mutex);

			return s;
		} else {
			printk("vde : should not be here\n");
			return -1;
		}

		break;

	case VDE_DISABLE_CLK:
		break;

	case VDE_ENABLE_CLK:
		break;

	case VDE_SET_FREQ:
		break;

	case VDE_GET_FREQ:
		break;

	case VDE_SET_MULTI:
		mutex_lock(&m_mutex);
		if (arg > 1)
			multi_instance_mode = 1;
		else
			multi_instance_mode = 0;
		mutex_unlock(&m_mutex);
		break;

	case VDE_DUMP:
		  printk("vde : vde VDE_DUMP..., but do nothing\n");
		vde_drv_showreg(-1);
		break;

	default:
		printk("vde : no such cmd 0x%x\n", cmd);
		return -EIO;
	}

	/* vde_reset_in_playing(); */
	return 0;
}

static int vde_open(struct inode *inode, struct file *file)
{
	VDE_DBG("\nvde : vde_open: In, vde_open_count: %d\n", vde_open_count);

	mutex_lock(&m_mutex);

	vde_open_count++;
	if (vde_open_count > 1) {
		printk("vde drv already open\n");
		mutex_unlock(&m_mutex);
		return 0;
	}
	vde_set_voltage_limit = 0;

	vde_clk_enable();

	disable_vde_irq();

	enable_irq(GVDE->irq);

	vde_idle_flag = 1;

	VDE_DBG("vde : vde_open: Out\n");
	mutex_unlock(&m_mutex);
	return 0;
}

static int vde_release(struct inode *inode, struct file *file)
{
	int i;
	mutex_lock(&m_mutex);

	VDE_DBG("vde : vde_release: In, vde_open_count: %d\n", vde_open_count);
	vde_open_count--;

	if (vde_open_count > 0) {
		VDE_DBG("vde : vde_release: count:%d pid(%d)\n",
			vde_open_count, task_tgid_vnr(current));
		vde_waitforidle();

		goto VDE_REL;
	} else if (vde_open_count < 0) {
		printk("vde : vde_release: module is closed before opened\n");
		vde_open_count = 0;
	}

	if (vde_open_count == 0) {
		VDE_DBG("vde : vde_release: disable vde irq\n");
		disable_vde_irq();
		VDE_DBG("vde : vde_release: disable IRQ_VDE\n");
		disable_irq(GVDE->irq);
		VDE_DBG("vde : vde_release: disable vde irq ok!\n");
	}

	vde_clk_disable();

	vde_pre_freq = 0;

VDE_REL:
	for (i = 0; i < MAX_INSTANCE_NUM; i++) {
		if (slot[i].pid == task_tgid_vnr(current)) {
			printk("vde : vde slot is leak by pid(%d), reset it\n",
				 task_tgid_vnr(current));
			if (slot[i].slice_mode == 1 && vde_occupied == 1)
				vde_occupied = 0;
			slot_reset(i);
		}
	}
	VDE_DBG("vde : vde_release: Out. vde_cur_slot_num: \
		%d vde_open_count: %d\n", vde_cur_slot_num, vde_open_count);
	mutex_unlock(&m_mutex);

	return 0;
}

static int vde_is_enable_before_suspend;
/*
 * guarantee that frame has been decoded before low power
 */
static int vde_suspend(struct platform_device *dev, pm_message_t state)
{
	printk("vde : vde_suspend: In , vde_clk_isenable=%d, RST0=0x%x\n",
		vde_clk_isenable, readl_relaxed(VDE_CMU_DEVRST0));
	if (vde_clk_isenable != 0) {
		mutex_lock(&m_mutex);
		vde_waitforidle();
		disable_vde_irq();
		vde_clk_disable();
		vde_is_enable_before_suspend = 1;

		mutex_unlock(&m_mutex);
	}

	disable_irq(GVDE->irq);

	/*Reset the voltage if the status is not clear after resumed.*/
	vde_pre_freq = 0;
	printk("vde : vde_suspend: Out\n");

	return 0;
}

static int vde_resume(struct platform_device *dev)
{
	printk(KERN_DEBUG"vde : vde_resume: In, \
		vde_is_enable_before_suspend=%d, RST0=0x%x\n",
			vde_is_enable_before_suspend, readl_relaxed(VDE_CMU_DEVRST0));
	if (vde_is_enable_before_suspend == 1) {
		vde_clk_enable();
		vde_is_enable_before_suspend = 0;
	} else {
		/*vde_power_off();*/
	}

	enable_irq(GVDE->irq);

	printk(KERN_DEBUG"vde : vde_resume: Out\n");

	return 0;
}

static struct file_operations vde_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vde_ioctl,
	.compat_ioctl   = vde_ioctl,/* compat_vde_ioctl */
	.open = vde_open,
	.release = vde_release,
};

/*#define ENABLE_COOLING*/

#ifdef ENABLE_COOLING
#include <linux/cpu_cooling.h>
static bool is_cooling;
static int thermal_notifier(struct notifier_block *nb,
								unsigned long event, void *data)
{
	printk("vde thermal_notifier event:%d \n", event);
	if (event == CPUFREQ_COOLING_START) {
		int ret = 0;
		if (is_cooling == 0) {
			mutex_lock(&m_freq_adjust_mutex);
			vde_waitforidle();
			adjust_freq_flag = 0;
			printk("vde  CPUFREQ_COOLING_START,event:%d,\n", event);
			if (vde_set_parent_pll(CLK_NAME_DEVCLK) != 0) {
				printk("set parent pll fail\n");
				mutex_unlock(&m_freq_adjust_mutex);
				return 0;
			}
		  if (vde_set_corevdd(GVDE->power, VDD_1_05V) != 0) {
					printk("set vdd VDD_1_05V fail\n");
					mutex_unlock(&m_freq_adjust_mutex);
					return 0;
			}
			ret = vde_do_set_freq(VDE_FREQ_720P);
			mutex_unlock(&m_freq_adjust_mutex);
			is_cooling = 1;
		}
		printk("-- CPUFREQ_COOLING_START --freq : %d\n", ret/30);
	}

	if (event == CPUFREQ_COOLING_STOP) {
		printk("vde  CPUFREQ_COOLING_STOP event:%d\n", event);
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



static int asoc_vde_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct asoc_vde_dev *vde;
	struct resource *iores;
	int ret;

	dev_info(&pdev->dev, "Probe vde device\n");

	id = of_match_device(vde_of_match, &pdev->dev);
	if (id != NULL) {
		struct ic_info *info = (struct ic_info *)id->data;
		if (info != NULL) {
			ic_type = info->ic_type;
			printk("info ic_type 0x%x\n", ic_type);
		} else {
			printk("info is null\n");
		}
	}

	vde = devm_kzalloc(&pdev->dev, sizeof(*vde), GFP_KERNEL);
	if (!vde)
		return -ENOMEM;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	vde->base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(vde->base))
		return PTR_ERR(vde->base);

	vde->irq = platform_get_irq(pdev, 0);
	if (vde->irq < 0)
		return vde->irq;

	dev_info(&pdev->dev, "resource: iomem: %pR mapping to %p, irq %d\n",
		iores, vde->base, vde->irq);

	ret = request_irq(vde->irq, (void *)VDE_ISR, 0, "vde_isr", 0);
	if (ret) {
		printk("vde : register vde irq failed!\n");
		vde_irq_registered = 0;
	} else {
		vde_irq_registered = 1;
	}

	pm_runtime_enable(&pdev->dev);

	/*get clock*/
	vde_clk  = clk_get(NULL, "vde");
	if (IS_ERR(vde_clk)) {
		printk("vde : Failed to get vde clock\n");
		return -1;
	}

	/*get display_pll struct*/
	display_pll = clk_get(NULL, "display_pll");
	if (IS_ERR(display_pll)) {
		printk("vde : clk_get display_pll failed\n");
		return 0;
	}
	/*get reset*/
	reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(reset)) {
		printk("devm_reset_control_get(&pdev->dev, NULL) failed\n");
		return PTR_ERR(reset);
	}

#ifdef ENABLE_COOLING
	cputherm_register_notifier(&thermal_notifier_block, CPUFREQ_COOLING_START);
#endif

	if (ic_type == IC_TYPE_S700 || ic_type == IC_TYPE_S900) {
			int regulator_return = 0;
			vde->power = devm_regulator_get(&pdev->dev, "corevdd");
			if (IS_ERR(vde->power))
					printk("can not get corevdd regulator,may be this board not need, or lost in dts!\n"); /*6082样机corevdd不可调，所以在dts里面不配置，这里也获取不到，也不会进入设置电压的流程*/
			else {
					if (regulator_set_voltage(vde->power, VDD_1_0V, INT_MAX)) {
					printk("cannot set corevdd to 1000000uV !\n");
					return -EINVAL;
			}
			regulator_return = regulator_enable(vde->power);
		}
	}



  share_mem_reg = ioremap_nocache(0xe0250004 , 0x4);
	/* ioremap sps and cmu. */
	VDE_SPS_PG_CTL = devm_ioremap_nocache(&pdev->dev, SPS_BASE_PHY, 0x8);
	if (ic_type == IC_TYPE_S900) {
			printk("--- SDK S900\n");
			VDE_CMU_COREPLL = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S900, 0x16);
			VDE_CMU_VDE_CLK = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S900 + 0x40, 0x8);
			VDE_CMU_DEVRST0 = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S900 + 0xa8, 0x8);
			VDE_CMU_DEVCLKEN0 = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S900 + 0xa0, 0x8);
	} else {
			printk("--- SDK S700\n");
			VDE_CMU_COREPLL = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S700, 0x16);
			VDE_CMU_VDE_CLK = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S700 + 0x40, 0x8);
			VDE_CMU_DEVRST0 = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S700 + 0xa8, 0x8);
			VDE_CMU_DEVCLKEN0 = devm_ioremap_nocache(&pdev->dev, CMU_BASE_PHY_S700 + 0xa0, 0x8);
	}
	printk("SPS_PG_CTL = 0x%p\n", VDE_SPS_PG_CTL);
	printk("CMU_COREPLL = 0x%p\n", VDE_CMU_COREPLL);
	printk("CMU_VDE_CLK = 0x%p\n", VDE_CMU_VDE_CLK);
	printk("CMU_DEVRST0 = 0x%p\n", VDE_CMU_DEVRST0);
	printk("CMU_DEVCLKEN0 = 0x%p\n", VDE_CMU_DEVCLKEN0);

	disable_irq(vde->irq);
	GVDE = vde;
	gvdedev = pdev;
	printk("dev add %p, vde_clk %p\n", &pdev->dev, vde_clk);
	vde_clk_isenable = 0;

	return 0;
}

static int vde_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	vde_clk_disable();

	clk_put(vde_clk);
	clk_put(display_pll);

	return 0;
}

static struct platform_driver vde_platform_driver = {
	.driver = {
		.name = DEVDRV_NAME_VDE,
		.owner = THIS_MODULE,
		.of_match_table = vde_of_match,
	},
	.suspend = vde_suspend,
	.resume = vde_resume,
	.probe = asoc_vde_probe,
	.remove = vde_remove,
};

static struct miscdevice vde_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_VDE,
	.fops = &vde_fops,
};

static int vde_init(void)
{
	int ret;

	printk("#### insmod vde driver!\n");

	/* start insmod，register device.*/
	ret = misc_register(&vde_miscdevice);
	if (ret) {
		printk("register vde misc device failed!\n");
		goto err0;
	}

	ret = platform_driver_register(&vde_platform_driver);
	if (ret) {
		printk("register gpu platform driver4pm error!\n");
		goto err1;
	}

	mutex_init(&m_mutex);
	mutex_init(&m_freq_adjust_mutex);
	vde_cur_slot_num = 0;

	return 0;

err1:

	free_irq(GVDE->irq, 0);

	misc_deregister(&vde_miscdevice);

err0:
	return ret;
}

static void vde_exit(void)
{
	if (vde_irq_registered)
		free_irq(GVDE->irq, 0);

	misc_deregister(&vde_miscdevice);
	platform_driver_unregister(&vde_platform_driver);

	printk("vde module unloaded\n");
}


module_init(vde_init);
module_exit(vde_exit);

MODULE_AUTHOR("Actions Semi Inc");
MODULE_DESCRIPTION("VDE kernel module");
MODULE_LICENSE("GPL");