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

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/uaccess.h>
#include "imx_drv.h"

/*--------------IMX--------------*/
#define     IMX_BASE        0xE0278000
#define     IMX_CTRL        (IMX_BASE+0x0000)
#define     IMX_CONFIG1     (IMX_BASE+0x0004)
#define     IMX_CONFIG2     (IMX_BASE+0x0008)
#define     IMX_CONFIG3     (IMX_BASE+0x000C)
#define     IMX_SIZE        (IMX_BASE+0x0010)
#define     IMX_YADDR_IN    (IMX_BASE+0x0014)
#define     IMX_UADDR_IN    (IMX_BASE+0x0018)
#define     IMX_VADDR_IN    (IMX_BASE+0x001C)
#define     IMX_YADDR_OUT   (IMX_BASE+0x0020)
#define     IMX_UADDR_OUT   (IMX_BASE+0x0024)
#define     IMX_VADDR_OUT   (IMX_BASE+0x0028)

/*--------------SPS_PG--------------*/
#define     SPS_PG_BASE     0xE012e000
#define     SPS_PG_CTL      (SPS_PG_BASE+0x0000)
#define     SPS_PG_ACK      (SPS_PG_BASE+0x0004)

/*--------------CMU--------------*/
#define     CMU_BASE        0xE0160000
#define     CMU_DEVPLL      (CMU_BASE+0x0004)
#define     CMU_IMXCLK      (CMU_BASE+0x0038)
#define     CMU_DEVCLKEN1   (CMU_BASE+0x00A4)
#define     CMU_DEVRST0     (CMU_BASE+0x00A8)


#define DEVDRV_NAME_IMX "imx"
/*#define IRQ_OWL_IMX 34*/

struct imx_dev_t {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct clk *clk_parent;
	struct reset_control *rst;
	int irq;
	unsigned int setting;
};

static struct imx_dev_t *imx_dev;
static int imx_is_powered;
static int imx_is_clked;

struct imx_data_t {
	int open_id;
	struct imx_reg_t reg_list;
	int status;
};

#define MAX_INSTANCE 1024
void *ins_list[MAX_INSTANCE];
static int imx_open_cnt;

static DEFINE_MUTEX(imx_ioctl_mutex);
static wait_queue_head_t imx_wait;
static struct completion imx_complete;
#define WAIT_TIMEOUT HZ
#define WAIT_TIMEOUT_MS 30000

#define IMX_FIN_FLAG 2

#define imx_read(reg) readl_relaxed(imx_dev->base + (reg - IMX_BASE))
#define imx_write(val, reg) \
	writel_relaxed(val, imx_dev->base + (reg - IMX_BASE))

/*****************************************/
/*#define CFG_BY_REG*/
#ifdef CFG_BY_REG
static void __iomem *imx_sps_base;
#define PWR_IMX    0x00000020
#define ACK_IMX    0x00000020

static void __iomem *imx_cmu_base;
#define CLKEN_IMX  0x00020000
#define RST_IMX    0x00800000

#define sps_write(val, reg) \
	writel_relaxed(val, imx_sps_base + (reg - SPS_PG_BASE))
#define sps_read(reg) readl_relaxed(imx_sps_base + (reg - SPS_PG_BASE))
#define cmu_write(val, reg) writel_relaxed(val, imx_cmu_base + (reg - CMU_BASE))
#define cmu_read(reg) readl_relaxed(imx_cmu_base + (reg - CMU_BASE))
#endif
/*****************************************/

#define DBUG_I(fmt, args...) \
	pr_info("AIMX, %s %d, " fmt, __func__, __LINE__, ##args)
#define DBUG_E(fmt, args...) \
	pr_err("AIMX, %s %d, " fmt, __func__, __LINE__, ##args)

static void *imx_malloc(uint32_t size)
{
	return kzalloc(size, GFP_KERNEL | GFP_DMA);
}

static void imx_free(void *ptr)
{
	kfree(ptr);
	ptr = NULL;
	return;
}

static void print_imx_reg(void)
{
	DBUG_I("IMX_CTRL: 0x%x\n", imx_read(IMX_CTRL));
	DBUG_I("IMX_CONFIG1: 0x%x\n", imx_read(IMX_CONFIG1));
	DBUG_I("IMX_CONFIG2: 0x%x\n", imx_read(IMX_CONFIG2));
	DBUG_I("IMX_CONFIG3: 0x%x\n", imx_read(IMX_CONFIG3));
	DBUG_I("IMX_SIZE: 0x%x\n", imx_read(IMX_SIZE));
	DBUG_I("IMX_YADDR_IN: 0x%x\n", imx_read(IMX_YADDR_IN));
	DBUG_I("IMX_UADDR_IN: 0x%x\n", imx_read(IMX_UADDR_IN));
	DBUG_I("IMX_VADDR_IN: 0x%x\n", imx_read(IMX_VADDR_IN));
	DBUG_I("IMX_YADDR_OUT: 0x%x\n", imx_read(IMX_YADDR_OUT));
	DBUG_I("IMX_UADDR_OUT: 0x%x\n", imx_read(IMX_UADDR_OUT));
	DBUG_I("IMX_VADDR_OUT: 0x%x\n", imx_read(IMX_VADDR_OUT));

	/*
	   DBUG_I("CMU_DEVRST0: 0x%x\n", cmu_read(CMU_DEVRST0));
	   DBUG_I("CMU_DEVCLKEN1: 0x%x\n", cmu_read(CMU_DEVCLKEN1));
	   DBUG_I("CMU_DEVPLL: 0x%x\n", cmu_read(CMU_DEVPLL));
	   DBUG_I("CMU_IMXCLK: 0x%x\n", cmu_read(CMU_IMXCLK));
	   DBUG_I("SPS_PG_CTL: 0x%x\n", sps_read(SPS_PG_CTL));
	   DBUG_I("SPS_PG_ACK: 0x%x\n", sps_read(SPS_PG_ACK));
	 */

	return;
}

static int get_imx_status(void)
{
	uint32_t status;
	int ret = IMX_IDLE;

	status = imx_read(IMX_CTRL);
	if ((status & 0x1) != 0 && (status & 0x2) == 0)
		return IMX_BUSY;

	return ret;
}

#ifdef CFG_BY_REG
static void imx_cfg(void)
{
	uint32_t tmp;

	/*DBUG_I("CMU_DEVPLL: 0x%x\n", cmu_read(CMU_DEVPLL)); */
	/*DBUG_I("CMU_IMXCLK: 0x%x\n", cmu_read(CMU_IMXCLK)); */

	cmu_write(0x12, CMU_IMXCLK);

	tmp = cmu_read(CMU_DEVRST0);
	tmp &= (~RST_IMX);
	cmu_write(tmp, CMU_DEVRST0);

	tmp = sps_read(SPS_PG_CTL);
	tmp |= PWR_IMX;
	sps_write(tmp, SPS_PG_CTL);

loop:
	tmp = sps_read(SPS_PG_ACK);
	tmp &= ACK_IMX;
	if (0 == tmp)
		goto loop;

	tmp = cmu_read(CMU_DEVCLKEN1);
	tmp |= CLKEN_IMX;
	cmu_write(tmp, CMU_DEVCLKEN1);

	tmp = cmu_read(CMU_DEVRST0);
	tmp |= RST_IMX;
	cmu_write(tmp, CMU_DEVRST0);

	return;
}

static void imx_cfg_close(void)
{
	uint32_t tmp;

	tmp = sps_read(SPS_PG_CTL);
	tmp &= (~PWR_IMX);
	sps_write(tmp, SPS_PG_CTL);

	return;
}
#endif

static void imx_wait_stop(void)
{
	if (imx_is_powered) {
		int i = 0x10000000;
		while (IMX_BUSY == get_imx_status()) {
			mdelay(1);
			if (i-- < 0)
				break;
		}
		DBUG_I("\n");
	}
	return;
}

static void imx_power_on(void)
{
	DBUG_I("\n");
	if (imx_is_powered)
		DBUG_I("already on\n");

	imx_wait_stop();

	/*pm_runtime_get_sync(imx_dev->dev); */

	return;
}

static void imx_power_off(void)
{
	DBUG_I("\n");
	if (!imx_is_powered)
		DBUG_I("already off\n");

	/*pm_runtime_put_sync(imx_dev->dev); */

	return;
}

static void imx_reset(void)
{
	DBUG_I("\n");
	reset_control_reset(imx_dev->rst);
	return;
}

static void imx_clk_init(void)
{
	long rate;
	rate = clk_get_rate(imx_dev->clk);
	dev_info(imx_dev->dev, "imx clk default: %ld\n", rate);

	imx_dev->clk_parent = devm_clk_get(imx_dev->dev, "assist_pll");
	clk_set_parent(imx_dev->clk, imx_dev->clk_parent);
	clk_set_rate(imx_dev->clk, 500000000);
	rate = clk_get_rate(imx_dev->clk);
	dev_info(imx_dev->dev, "imx clk init: %ld\n", rate);

	return;
}

static void imx_clk_enable(void)
{
	DBUG_I("\n");

	imx_power_on();

	if (1 == imx_open_cnt)
		imx_reset();

	if (1 == imx_open_cnt)
		imx_is_powered = 1;

	clk_prepare_enable(imx_dev->clk);

	imx_is_clked = 1;
	return;
}

static void imx_clk_disable(void)
{
	DBUG_I("\n");

	clk_disable_unprepare(imx_dev->clk);

	imx_power_off();
	if (0 == imx_open_cnt)
		imx_is_powered = 0;

	imx_is_clked = 0;
	return;
}

static void imx_irq_disable(void)
{
	uint32_t imx_ctl = imx_read(IMX_CTRL);
	imx_ctl = imx_ctl | (1 << IMX_FIN_FLAG);
	imx_write(imx_ctl, IMX_CTRL);

	return;
}

irqreturn_t imx_isr(int irq, void *dev_id)
{
	uint32_t imx_ctl = imx_read(IMX_CTRL);
	/*DBUG_I("IMX_CTRL:0x%x\n", imx_ctl); */

	if (imx_ctl & (1 << IMX_FIN_FLAG)) {
		imx_irq_disable();
		complete(&imx_complete);
		wake_up_interruptible(&imx_wait);
	} else {
		DBUG_E("err! unknown IRQ, IMX_CTRL:0x%x\n", imx_ctl);
		imx_reset();
	}

	return IRQ_HANDLED;
}

static void set_imx_reg(struct imx_reg_t *in)
{
	imx_write(in->imx_cfg1, IMX_CONFIG1);
	imx_write(in->imx_cfg2, IMX_CONFIG2);
	imx_write(in->imx_cfg3, IMX_CONFIG3);
	imx_write(in->imx_size, IMX_SIZE);
	imx_write(in->imx_yin, IMX_YADDR_IN);
	imx_write(in->imx_uin, IMX_UADDR_IN);
	imx_write(in->imx_vin, IMX_VADDR_IN);
	imx_write(in->imx_yout, IMX_YADDR_OUT);
	imx_write(in->imx_uout, IMX_UADDR_OUT);
	imx_write(in->imx_vout, IMX_VADDR_OUT);

	return;
}

static long imx_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
	struct imx_data_t *data = (struct imx_data_t *) flip->private_data;
	void __user *from;
	/*void __user *to; */

	switch (cmd) {
	case IMX_IOC_VERSION:
		break;

	case IMX_IOC_START:
		{
			uint32_t status;
			unsigned long time_left;

			/*DBUG_I("IMX_IOC_START...\n"); */
			/*DBUG_I("imx clk rt: %d\n",
			*clk_get_rate(imx_dev->clk)); */

			mutex_lock(&imx_ioctl_mutex);
			from = (void __user *)arg;

			status = imx_read(IMX_CTRL);
			/*DBUG_I("IMX_CTRL: %x\n", status); */
			time_left =
			    wait_event_interruptible_timeout(imx_wait,
							     ((status & 0x1) ==
							      0), WAIT_TIMEOUT);
			if (unlikely(0 == time_left)) {
				DBUG_E
				    ("timeout enable running! IMX_CTRL:0x%x\n",
				     status);
				imx_reset();
				mutex_unlock(&imx_ioctl_mutex);
				return -1;
			}

			if (copy_from_user
			    (&data->reg_list, from, sizeof(struct imx_reg_t))) {
				DBUG_E("err! copy_from_user failed!\n");
				mutex_unlock(&imx_ioctl_mutex);
				return -1;
			}

			init_completion(&imx_complete);

			set_imx_reg(&data->reg_list);
			/*print_imx_reg(); */
			imx_write((data->reg_list.
				   imx_ctl | 0x1) /* & (~0x2) */ , IMX_CTRL);

			mutex_unlock(&imx_ioctl_mutex);
		}
		break;

	case IMX_IOC_QUERY:
		{
			unsigned long timeout;
			unsigned long time_left;

			/*DBUG_I("IMX_IOC_QUERY...\n"); */

			mutex_lock(&imx_ioctl_mutex);
			/*to = (void __user *)arg; */

			/*print_imx_reg(); */

			if (get_imx_status() != IMX_BUSY) {
				DBUG_I("IMX idle, query done! IMX_CTRL:0x%x\n",
				       imx_read(IMX_CTRL));
				mutex_unlock(&imx_ioctl_mutex);
				break;
			}

			timeout = msecs_to_jiffies(WAIT_TIMEOUT_MS) + 1;
			time_left =
			    wait_for_completion_timeout(&imx_complete, timeout);
			if (unlikely(0 == time_left)) {
				DBUG_E
				    ("timeout query! IMX_CTRL:0x%x\n",
				     imx_read(IMX_CTRL));
				imx_irq_disable();
				imx_reset();
				mutex_unlock(&imx_ioctl_mutex);
				return -1;
			} else {
				/*DBUG_I("query done.\n"); */
				mutex_unlock(&imx_ioctl_mutex);
			}
		}
		break;

	case IMX_IOC_GET_STATUS:
		break;

	case IMX_IOC_SET_FREQ:
		break;

	case IMX_IOC_GET_FREQ:
		break;

	default:
		DBUG_E("err! no such command: %d\n", cmd);
		return -EIO;
	}

	return 0;
}

static long imx_compat_ioctl(struct file *flip, unsigned int cmd,
			     unsigned long arg)
{
	return imx_ioctl(flip, cmd, arg);
}

static int imx_open(struct inode *inode, struct file *flip)
{
	struct imx_data_t *data;

	mutex_lock(&imx_ioctl_mutex);

	DBUG_I("\n");

	imx_open_cnt++;
	if (imx_open_cnt > MAX_INSTANCE) {
		DBUG_E("err! exceeded the maximum number\n");
		imx_open_cnt--;
		mutex_unlock(&imx_ioctl_mutex);
		return -1;
	}

	data = (struct imx_data_t *) imx_malloc(sizeof(struct imx_data_t));
	if (NULL == data) {
		DBUG_E("err! malloc failed %d\n", __LINE__);
		imx_open_cnt--;
		mutex_unlock(&imx_ioctl_mutex);
		return -1;
	}

	/*if(1 == imx_open_cnt) */
	{
		init_waitqueue_head(&imx_wait);
		init_completion(&imx_complete);
		imx_clk_enable();
		imx_irq_disable();
	}

	/*if (1 == imx_open_cnt)*/
		/*set frequency */

	/*print_imx_reg(); */

	data->open_id = imx_open_cnt;
	data->status = 0;
	flip->private_data = (void *)data;
	ins_list[imx_open_cnt - 1] = (void *)data;

	DBUG_I("imx clk rt: %ld\n", clk_get_rate(imx_dev->clk));

	mutex_unlock(&imx_ioctl_mutex);

	return 0;
}

static int imx_release(struct inode *inode, struct file *flip)
{
	struct imx_data_t *data = flip->private_data;

	mutex_lock(&imx_ioctl_mutex);
	DBUG_I("\n");

	/*multi-instance */

	imx_open_cnt--;
	if (NULL != data) {
		imx_free(data);
		flip->private_data = NULL;
	}

	imx_wait_stop();
	if (0 == imx_open_cnt)
		imx_clk_disable();

	mutex_unlock(&imx_ioctl_mutex);

	return 0;
}

static const struct file_operations imx_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = imx_ioctl,
	.compat_ioctl = imx_compat_ioctl,
	.open = imx_open,
	.release = imx_release,
};

static struct miscdevice imx_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_IMX,
	.fops = &imx_fops,
};

static const struct of_device_id owl_imx_match[] = {
	{.compatible = "actions,s900-imx"},
	{},
};

MODULE_DEVICE_TABLE(of, owl_imx_match);

static int __init owl_imx_probe(struct platform_device *pdev)
{
	/*struct device_node *np = pdev->dev.of_node;*/
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct resource *iores;
	int ret;

	dev_info(dev, "probe IMX device\n");

	match = of_match_device(of_match_ptr(owl_imx_match), dev);
	if (!match) {
		dev_err(dev, "err! no device match found\n");
		return -ENODEV;
	}

	imx_dev = devm_kzalloc(dev, sizeof(*imx_dev), GFP_KERNEL);
	if (!imx_dev)
		return -ENOMEM;

	imx_dev->dev = dev;
	platform_set_drvdata(pdev, imx_dev);

	/* powergate */
	pm_runtime_enable(dev);

	/* clk */
	imx_dev->clk = devm_clk_get(imx_dev->dev, "imx");
	if (IS_ERR(imx_dev->clk)) {
		dev_err(imx_dev->dev, "err! failed to get clock\n");
		return PTR_ERR(imx_dev->clk);
	}
	/*set clk*/
	imx_clk_init();

	/* reset */
	imx_dev->rst = devm_reset_control_get(imx_dev->dev, NULL);
	if (IS_ERR(imx_dev->rst)) {
		dev_err(imx_dev->dev, "err! failed to get reset control\n");
		return PTR_ERR(imx_dev->rst);
	}

	/* irq */
	imx_dev->irq = platform_get_irq(pdev, 0);
	if (imx_dev->irq < 0)
		return imx_dev->irq;

	ret =
	    devm_request_irq(imx_dev->dev, imx_dev->irq, (void *)imx_isr, 0,
			     "imx_isr", 0);
	if (ret) {
		dev_err(imx_dev->dev, "err! failed to request irq\n");
		return ret;
	}

	/* res */
	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	imx_dev->base = devm_ioremap_resource(imx_dev->dev, iores);
	if (IS_ERR(imx_dev->base))
		return PTR_ERR(imx_dev->base);

	dev_info(imx_dev->dev, "resource: iomem: %p remapped to %p, irq %d\n",
		 iores, imx_dev->base, imx_dev->irq);

	/*
	   if (np) {
	   ret = of_property_read_u32(np, "imx-setting", &imx_dev->setting);
	   if (ret) {
		dev_err(imx_dev->dev,
		"err! failed to read imx-setting, err %d\n", ret);
		return ret;
		}
	   }
	   else {
		imx_dev->setting = 0x55aa;
	   }
	   dev_info(imx_dev->dev, "imx-setting %d\n", imx_dev->setting);
	 */

	/*
	   imx_sps_base = ioremap(SPS_PG_BASE, 0x22);
	   imx_cmu_base = ioremap(CMU_BASE, 0x100);
	 */

	/* initial global static var. */
	imx_open_cnt = 0;
	imx_is_powered = 0;
	imx_is_clked = 0;

	return 0;
}

static int owl_imx_suspend(struct platform_device *dev, pm_message_t state)
{
	DBUG_I("\n");
	mutex_lock(&imx_ioctl_mutex);

	imx_wait_stop();
	imx_irq_disable();
	imx_clk_disable();
	disable_irq(imx_dev->irq);

	mutex_unlock(&imx_ioctl_mutex);
	return 0;
}

static int owl_imx_resume(struct platform_device *dev)
{
	DBUG_I("\n");
	mutex_lock(&imx_ioctl_mutex);

	imx_clk_enable();
	imx_irq_disable();
	enable_irq(imx_dev->irq);

	mutex_unlock(&imx_ioctl_mutex);
	return 0;
}

static int owl_imx_remove(struct platform_device *pdev)
{
	struct imx_dev_t *imx_dev = platform_get_drvdata(pdev);

	DBUG_I("\n");

	return 0;
}

static struct platform_driver owl_imx_driver = {
	.probe = owl_imx_probe,
	.suspend = owl_imx_suspend,
	.resume = owl_imx_resume,
	.remove = owl_imx_remove,
	.driver = {
		   .name = DEVDRV_NAME_IMX,
		   .owner = THIS_MODULE,
		   .of_match_table = owl_imx_match,
		   },
};

static int __init owl_imx_init(void)
{
	int ret;

	DBUG_I("registering imx driver\n");

	ret = misc_register(&imx_miscdevice);
	if (ret) {
		DBUG_E("failed to register IMX misc device!\n");
		goto err0;
	}

	ret = platform_driver_register(&owl_imx_driver);
	if (ret) {
		DBUG_E("failed to register IMX platform driver!\n");
		goto err1;
	}

	return 0;

err1:
	misc_deregister(&imx_miscdevice);
err0:
	return ret;
}

static void __exit owl_imx_exit(void)
{
	DBUG_I("remove imx driver\n");

	misc_deregister(&imx_miscdevice);
	platform_driver_unregister(&owl_imx_driver);

	return;
}

late_initcall(owl_imx_init);
module_exit(owl_imx_exit);

MODULE_AUTHOR("Kevin Deng, Actions Semiconductor Cor., Ltd.");
MODULE_DESCRIPTION("Actions s900 IMX driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("Platform: OWL-IMX");
