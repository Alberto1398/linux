/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File           apollo.c
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

/*
 * This is a device driver for the apollo testchip framework. It creates
 * platform devices for the pdp and ext sub-devices, and exports functions
 * to manage the shared interrupt handling
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#if defined(APOLLO_FAKE_INTERRUPTS)
#define FAKE_INTERRUPT_TIME_MS 16
#include <linux/timer.h>
#include <linux/time.h>
#endif

#if defined(CONFIG_MTRR)
#include <asm/mtrr.h>
#endif

#include "apollo_drv.h"

#include "apollo_regs.h"
#include "tcf_clk_ctrl.h"
#include "tcf_pll.h"

#include "pvrmodule.h"

#if defined(SUPPORT_ION)
#if defined(SUPPORT_RGX)
#define APOLLO_ION_HEAP_COUNT 3
#else
#define APOLLO_ION_HEAP_COUNT 2
#endif
#include "ion_lma_heap.h"
#endif

#if defined(SUPPORT_APOLLO_FPGA)
#include <linux/debugfs.h>
#endif

#define DRV_NAME "apollo"

/* How much memory to give to the PDP heap (used for pdp buffers). */
#define APOLLO_PDP_MEM_SIZE		((TC_DISPLAY_MEM_SIZE)*1024*1024)

/* This is a guess of what's a minimum sensible size for the ext heap
 * It is only used for a warning if the ext heap is smaller, and does
 * not affect the functional logic in any way
 */
#define APOLLO_EXT_MINIMUM_MEM_SIZE	(10*1024*1024)

#define PCI_VENDOR_ID_POWERVR		0x1010
#define DEVICE_ID_PCI_APOLLO_FPGA	0x1CF1
#define DEVICE_ID_PCIE_APOLLO_FPGA	0x1CF2

#define APOLLO_MEM_PCI_BASENUM		(2)

#define APOLLO_INTERRUPT_FLAG_PDP	(1 << PDP1_INT_SHIFT)
#define APOLLO_INTERRUPT_FLAG_EXT	(1 << EXT_INT_SHIFT)

MODULE_DESCRIPTION("APOLLO testchip framework driver");

static int apollo_core_clock = RGX_TC_CORE_CLOCK_SPEED;
static int apollo_mem_clock = RGX_TC_MEM_CLOCK_SPEED;

module_param(apollo_core_clock, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(apollo_core_clock, "Apollo core clock speed");
module_param(apollo_mem_clock, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(apollo_mem_clock, "Apollo memory clock speed");

static unsigned long apollo_pdp_mem_size = APOLLO_PDP_MEM_SIZE;

module_param(apollo_pdp_mem_size, ulong, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(apollo_pdp_mem_size,
	"Apollo PDP reserved memory size in bytes");

static int apollo_sys_clock = RGX_TC_SYS_CLOCK_SPEED;
module_param(apollo_sys_clock, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(apollo_sys_clock, "Apollo system clock speed (TCF5 only)");

enum apollo_version_t {
	APOLLO_VERSION_TCF_2,
	APOLLO_VERSION_TCF_5,
	APOLLO_VERSION_TCF_BONNIE,
};

struct apollo_interrupt_handler {
	bool enabled;
	void (*handler_function)(void *);
	void *handler_data;
};

struct apollo_region {
	resource_size_t base;
	resource_size_t size;
};

struct apollo_io_region {
	struct apollo_region region;
	void __iomem *registers;
};

struct apollo_device {
	struct pci_dev *pdev;

	struct apollo_io_region tcf;
	struct apollo_io_region tcf_pll;

	spinlock_t interrupt_handler_lock;
	spinlock_t interrupt_enable_lock;

	struct apollo_interrupt_handler
		interrupt_handlers[APOLLO_INTERRUPT_COUNT];

	struct apollo_region apollo_mem;

	struct platform_device *pdp_dev;

	resource_size_t pdp_heap_mem_base;
	resource_size_t pdp_heap_mem_size;

	struct platform_device *ext_dev;

	resource_size_t ext_heap_mem_base;
	resource_size_t ext_heap_mem_size;

	enum apollo_version_t version;

#if defined(APOLLO_FAKE_INTERRUPTS)
	struct timer_list timer;
#endif

#if defined(SUPPORT_ION)
	struct ion_device *ion_device;
	struct ion_heap *ion_heaps[APOLLO_ION_HEAP_COUNT];
	int ion_heap_count;
#endif

#if defined(CONFIG_MTRR) || (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	int mtrr;
#endif

#if defined(SUPPORT_APOLLO_FPGA)
	struct apollo_io_region fpga;
	struct dentry *debugfs_apollo_dir;
	struct dentry *debugfs_apollo_regs;
	struct dentry *debugfs_apollo_pll_regs;
	struct dentry *debugfs_fpga_regs;
	struct dentry *debugfs_apollo_mem;
#endif
};

#if defined(SUPPORT_APOLLO_FPGA)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))

static struct dentry *debugfs_create_file_size(const char *name, umode_t mode,
	struct dentry *parent, void *data, const struct file_operations *fops,
	loff_t file_size)
{
	struct dentry *de = debugfs_create_file(name, mode, parent, data, fops);

	if (de)
		de->d_inode->i_size = file_size;
	return de;
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0)) */

static ssize_t apollo_debugfs_sanity_check(size_t *count, loff_t *ppos,
	resource_size_t region_size)
{
	if (*ppos < 0)
		return -EFAULT;

	if (*ppos + *count > region_size)
		*count = region_size - *ppos;

	if ((*ppos) % sizeof(u32))
		return -EINVAL;

	if ((*count) % sizeof(u32))
		return -EINVAL;

	return 0;
}

static ssize_t apollo_debugfs_read_io(struct file *file,
	char __user *buf, size_t count, loff_t *ppos)
{
	struct apollo_io_region *io = file->private_data;
	ssize_t err;
	loff_t i;

	err = apollo_debugfs_sanity_check(&count, ppos, io->region.size);
	if (err)
		return err;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	for (i = 0; i < count; i += sizeof(u32), (*ppos) += sizeof(u32))
		*(u32 *)(buf + i) = ioread32(io->registers + *ppos);

	return count;
}

static ssize_t apollo_debugfs_write_io(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
	struct apollo_io_region *io = file->private_data;
	ssize_t err;
	loff_t i;

	err = apollo_debugfs_sanity_check(&count, ppos, io->region.size);
	if (err)
		return err;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	for (i = 0; i < count; i += sizeof(u32), (*ppos) += sizeof(u32))
		iowrite32(*(u32 *)(buf + i), io->registers + *ppos);

	return count;
}

static ssize_t apollo_debugfs_read_mem(struct file *file,
	char __user *buf, size_t count, loff_t *ppos)
{
	struct apollo_region *region = file->private_data;
	void *memory;
	ssize_t err;

	err = apollo_debugfs_sanity_check(&count, ppos, region->size);
	if (err)
		return err;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	memory = ioremap_wc(region->base + *ppos, count);
	if (!memory)
		return -EFAULT;

	memcpy(buf, memory, count);

	iounmap(memory);
	(*ppos) += count;
	return count;
}

static ssize_t apollo_debugfs_write_mem(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
	struct apollo_region *region = file->private_data;
	void *memory;
	ssize_t err;

	err = apollo_debugfs_sanity_check(&count, ppos, region->size);
	if (err)
		return err;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	memory = ioremap_wc(region->base + *ppos, count);
	if (!memory)
		return -EFAULT;

	memcpy(memory, buf, count);

	/* Flush the write combiner? */
	ioread32(memory + count - sizeof(u32));

	iounmap(memory);
	(*ppos) += count;
	return count;
}

static const struct file_operations apollo_io_debugfs_fops = {
	.open	= simple_open,
	.read	= apollo_debugfs_read_io,
	.write	= apollo_debugfs_write_io,
	.llseek	= default_llseek,
};

static const struct file_operations apollo_mem_debugfs_fops = {
	.open	= simple_open,
	.read	= apollo_debugfs_read_mem,
	.write	= apollo_debugfs_write_mem,
	.llseek	= default_llseek,
};

#endif /* defined(SUPPORT_APOLLO_FPGA) */

static int request_pci_io_addr(struct pci_dev *pdev, u32 index,
	resource_size_t offset, resource_size_t length)
{
	resource_size_t start, end;

	start = pci_resource_start(pdev, index);
	end = pci_resource_end(pdev, index);

	if ((start + offset + length - 1) > end)
		return -EIO;
	if (pci_resource_flags(pdev, index) & IORESOURCE_IO) {
		if (request_region(start + offset, length, DRV_NAME) == NULL)
			return -EIO;
	} else {
		if (request_mem_region(start + offset, length, DRV_NAME)
			== NULL)
			return -EIO;
	}
	return 0;
}

static void release_pci_io_addr(struct pci_dev *pdev, u32 index,
	resource_size_t start, resource_size_t length)
{
	if (pci_resource_flags(pdev, index) & IORESOURCE_IO)
		release_region(start, length);
	else
		release_mem_region(start, length);
}

static void pll_write_reg(struct apollo_device *apollo,
	resource_size_t reg_offset, u32 reg_value)
{
	BUG_ON(reg_offset < TCF_PLL_PLL_CORE_CLK0);
	BUG_ON(reg_offset > apollo->tcf_pll.region.size +
		TCF_PLL_PLL_CORE_CLK0 - 4);

	/* Tweak the offset because we haven't mapped the full pll region */
	iowrite32(reg_value, apollo->tcf_pll.registers +
		reg_offset - TCF_PLL_PLL_CORE_CLK0);
}

static void apollo_set_clocks(struct apollo_device *apollo)
{
	u32 val;

	/* This is disabled for TCF2 since the current FPGA builds do not
	 * like their core clocks being set (it takes apollo down).
	 */
	if (apollo->version != APOLLO_VERSION_TCF_2) {
		val = apollo_core_clock / 1000000;
		pll_write_reg(apollo, TCF_PLL_PLL_CORE_CLK0, val);

		val = 0x1 << PLL_CORE_DRP_GO_SHIFT;
		pll_write_reg(apollo, TCF_PLL_PLL_CORE_DRP_GO, val);
	}

	val = apollo_mem_clock / 1000000;
	pll_write_reg(apollo, TCF_PLL_PLL_MEMIF_CLK0, val);

	val = 0x1 << PLL_MEM_DRP_GO_SHIFT;
	pll_write_reg(apollo, TCF_PLL_PLL_MEM_DRP_GO, val);

	if (apollo->version == APOLLO_VERSION_TCF_5) {
		val = apollo_sys_clock / 1000000;
		pll_write_reg(apollo, TCF_PLL_PLL_SYSIF_CLK0, val);

		val = 0x1 << PLL_MEM_DRP_GO_SHIFT;
		pll_write_reg(apollo, TCF_PLL_PLL_SYS_DRP_GO, val);
	}

	dev_dbg(&apollo->pdev->dev, "Setting clocks to %uMHz/%uMHz\n",
			 apollo_core_clock / 1000000,
			 apollo_mem_clock / 1000000);
	udelay(400);
}

#if defined(CONFIG_MTRR) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0))

/*
 * A return value of:
 *      0 or more means success
 *     -1 means we were unable to add an mtrr but we should continue
 *     -2 means we were unable to add an mtrr but we shouldn't continue
 */
static int mtrr_setup(struct pci_dev *pdev,
		      resource_size_t mem_start,
		      resource_size_t mem_size)
{
	int err;
	int mtrr;

	/* Reset MTRR */
	mtrr = mtrr_add(mem_start, mem_size, MTRR_TYPE_UNCACHABLE, 0);
	if (mtrr < 0) {
		dev_err(&pdev->dev, "%d - %s: mtrr_add failed (%d)\n",
			__LINE__, __func__, mtrr);
		mtrr = -2;
		goto err_out;
	}

	err = mtrr_del(mtrr, mem_start, mem_size);
	if (err < 0) {
		dev_err(&pdev->dev, "%d - %s: mtrr_del failed (%d)\n",
			__LINE__, __func__, err);
		mtrr = -2;
		goto err_out;
	}

	mtrr = mtrr_add(mem_start, mem_size, MTRR_TYPE_WRBACK, 0);
	if (mtrr < 0) {
		/* Stop, but not an error as this may be already be setup */
		dev_dbg(&pdev->dev, "%d - %s: mtrr_add failed (%d) - probably means the mtrr is already setup\n",
			__LINE__, __func__, mtrr);
		mtrr = -1;
		goto err_out;
	}

	err = mtrr_del(mtrr, mem_start, mem_size);
	if (err < 0) {
		dev_err(&pdev->dev, "%d - %s: mtrr_del failed (%d)\n",
			__LINE__, __func__, err);
		mtrr = -2;
		goto err_out;
	}

	if (mtrr == 0) {
		/* Replace 0 with a non-overlapping WRBACK mtrr */
		err = mtrr_add(0, mem_start, MTRR_TYPE_WRBACK, 0);
		if (err < 0) {
			dev_err(&pdev->dev, "%d - %s: mtrr_add failed (%d)\n",
				__LINE__, __func__, err);
			mtrr = -2;
			goto err_out;
		}
	}

	mtrr = mtrr_add(mem_start, mem_size, MTRR_TYPE_WRCOMB, 0);
	if (mtrr < 0) {
		dev_err(&pdev->dev, "%d - %s: mtrr_add failed (%d)\n",
			__LINE__, __func__, mtrr);
		mtrr = -1;
	}

err_out:
	return mtrr;
}

#endif /* defined(CONFIG_MTRR) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)) */

static void apollo_set_mem_mode(struct apollo_device *apollo)
{
	u32 val;

	val = ioread32(apollo->tcf.registers + TCF_CLK_CTRL_TEST_CTRL);
	val &= ~(ADDRESS_FORCE_MASK | PCI_TEST_MODE_MASK | HOST_ONLY_MODE_MASK
		| HOST_PHY_MODE_MASK);
	val |= (0x1 << ADDRESS_FORCE_SHIFT);
	iowrite32(val, apollo->tcf.registers + TCF_CLK_CTRL_TEST_CTRL);
}

static void apollo_devres_release(struct device *dev, void *res)
{
	/* No extra cleanup needed */
}

static void spi_write(struct apollo_device *apollo,
					  u32 off, u32 val)
{
	iowrite32(off, apollo->tcf.registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR);
	iowrite32(val, apollo->tcf.registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_WDATA);
	iowrite32(TCF_SPI_MST_GO_MASK, apollo->tcf.registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_GO);
	udelay(1000);
}

static int spi_read(struct apollo_device *apollo,
					u32 off, u32 *val)
{
	int cnt = 0;

	iowrite32(0x40000 | off, apollo->tcf.registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR);
	iowrite32(TCF_SPI_MST_GO_MASK, apollo->tcf.registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_GO);
	udelay(100);

	while ((ioread32(apollo->tcf.registers +
				TCF_CLK_CTRL_TCF_SPI_MST_STATUS) != 0x08) &&
		   (cnt < 10000))
		++cnt;

	if (cnt == 10000) {
		dev_err(&apollo->pdev->dev,
			"spi_read: Time out reading SPI register (0x%x)\n",
			off);
		return -1;
	}

	*val = ioread32(apollo->tcf.registers + TCF_CLK_CTRL_TCF_SPI_MST_RDATA);

	return 0;
}

static int is_interface_aligned_es2(u32 eyes, u32 clk_taps, u32 train_ack)
{
	u32	max_eye_start = eyes >> 16;
	u32	min_eye_end   = eyes & 0xffff;

	/* If either the training or training ack failed, we haven't aligned */
	if (!(clk_taps & 0x10000) || !(train_ack & 0x100))
		return 0;

	/* If the max eye >= min eye it means the readings are nonsense */
	if (max_eye_start >= min_eye_end)
		return 0;

	/* If we failed the ack pattern more than 4 times */
	if (((train_ack & 0xf0) >> 4) > 4)
		return 0;

	/* If there is less than 7 taps (240ps @40ps/tap, this number should be
	 * lower for the fpga, since its taps are bigger We should really
	 * calculate the "7" based on the interface clock speed.
	 */
	if ((min_eye_end - max_eye_start) < 7)
		return 0;

	return 1;
}

static u32 sai_read_es2(struct apollo_device *apollo, u32 addr)
{
	iowrite32(0x200 | addr, apollo->tcf.registers + 0x300);
	iowrite32(0x1 | addr, apollo->tcf.registers + 0x318);
	return ioread32(apollo->tcf.registers + 0x310);
}

static int iopol32_nonzero(u32 mask, void __iomem *addr)
{
	int polnum;
	u32 read_value;

	for (polnum = 0; polnum < 500; polnum++) {
		read_value = ioread32(addr) & mask;
		if (read_value != 0)
			break;
		/* NOTE: msleep() < 20 ms may sleep up to 20ms */
		msleep(1);
	}
	if (polnum == 500) {
		pr_err(DRV_NAME " iopol32_nonzero timeout\n");
		return -ETIME;
	}
	return 0;
}

static int apollo_align_interface_es2(struct apollo_device *apollo)
{
	int reg = 0;
	u32 reg_reset_n;
	int reset_cnt = 0;
	int err = -EFAULT;
	bool aligned = false;

	/* Try to enable the core clock PLL */
	spi_write(apollo, 0x1, 0x0);
	reg  = ioread32(apollo->tcf.registers + 0x320);
	reg |= 0x1;
	iowrite32(reg, apollo->tcf.registers + 0x320);
	reg &= 0xfffffffe;
	iowrite32(reg, apollo->tcf.registers + 0x320);
	msleep(1000);

	if (spi_read(apollo, 0x2, &reg)) {
		dev_err(&apollo->pdev->dev,
				"Unable to read PLL status\n");
		goto err_out;
	}

	if (reg == 0x1) {
		/* Select DUT PLL as core clock */
		reg  = ioread32(apollo->tcf.registers +
			TCF_CLK_CTRL_DUT_CONTROL_1);
		reg &= 0xfffffff7;
		iowrite32(reg, apollo->tcf.registers +
			TCF_CLK_CTRL_DUT_CONTROL_1);
	} else {
		dev_err(&apollo->pdev->dev,
			"PLL has failed to lock, status = %x\n", reg);
		goto err_out;
	}

	reg_reset_n = ioread32(apollo->tcf.registers +
		TCF_CLK_CTRL_CLK_AND_RST_CTRL);

	while (!aligned && reset_cnt < 10 &&
			apollo->version != APOLLO_VERSION_TCF_5) {
		int bank;
		u32 eyes;
		u32 clk_taps;
		u32 train_ack;

		++reset_cnt;

		/* Reset the DUT to allow the SAI to retrain */
		reg_reset_n &= ~(0x1 << DUT_RESETN_SHIFT);
		iowrite32(reg_reset_n, apollo->tcf.registers +
			  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
		udelay(100);
		reg_reset_n |= (0x1 << DUT_RESETN_SHIFT);
		iowrite32(reg_reset_n, apollo->tcf.registers +
			  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
		udelay(100);

		/* Assume alignment passed, if any bank fails on either DUT or
		 * FPGA we will set this to false and try again for a max of 10
		 * times.
		 */
		aligned = true;

		/* For each of the banks */
		for (bank = 0; bank < 10; bank++) {
			int bank_aligned = 0;
			/* Check alignment on the DUT */
			u32 bank_base = 0x7000 + (0x1000 * bank);

			spi_read(apollo, bank_base + 0x4, &eyes);
			spi_read(apollo, bank_base + 0x3, &clk_taps);
			spi_read(apollo, bank_base + 0x6, &train_ack);

			bank_aligned = is_interface_aligned_es2(
					eyes, clk_taps, train_ack);
			if (!bank_aligned) {
				dev_warn(&apollo->pdev->dev, "Alignment check failed, retrying\n");
				aligned = false;
				break;
			}

			/* Check alignment on the FPGA */
			bank_base = 0xb0 + (0x10 * bank);

			eyes = sai_read_es2(apollo, bank_base + 0x4);
			clk_taps = sai_read_es2(apollo, bank_base + 0x3);
			train_ack = sai_read_es2(apollo, bank_base + 0x6);

			bank_aligned = is_interface_aligned_es2(
					eyes, clk_taps, train_ack);

			if (!bank_aligned) {
				dev_warn(&apollo->pdev->dev, "Alignment check failed, retrying\n");
				aligned = false;
				break;
			}
		}
	}

	if (!aligned) {
		dev_err(&apollo->pdev->dev,
				"Unable to intialise the testchip (interface alignment failure), please restart the system.\n");
		goto err_out;
	}

	if (reset_cnt > 1) {
		dev_dbg(&apollo->pdev->dev, "Note: The testchip required more than one reset to find a good interface alignment!\n");
		dev_dbg(&apollo->pdev->dev, "      This should be harmless, but if you do suspect foul play, please reset the machine.\n");
		dev_dbg(&apollo->pdev->dev, "      If you continue to see this message you may want to report it to IMGWORKS.\n");
	}

	err = 0;
err_out:
	return err;
}

static int apollo_hard_reset(struct apollo_device *apollo)
{
	u32 reg;
	u32 reg_reset_n = 0;

	/* For displaying some build info */
	u32 build_inc;
	u32 build_owner;

	int err = 0;

	/* This is required for SPI reset which is not yet implemented. */
	/*u32 aux_reset_n;*/

	if (apollo->version == APOLLO_VERSION_TCF_2) {
		/* Power down */
		reg = ioread32(apollo->tcf.registers +
			TCF_CLK_CTRL_DUT_CONTROL_1);
		reg &= ~DUT_CTRL_VCC_0V9EN;
		reg &= ~DUT_CTRL_VCC_1V8EN;
		reg |= DUT_CTRL_VCC_IO_INH;
		reg |= DUT_CTRL_VCC_CORE_INH;
		iowrite32(reg, apollo->tcf.registers +
			TCF_CLK_CTRL_DUT_CONTROL_1);
		msleep(500);
	}

	/* Put everything into reset */
	iowrite32(reg_reset_n, apollo->tcf.registers +
		TCF_CLK_CTRL_CLK_AND_RST_CTRL);

	/* Set clock speed here, before reset. */
	apollo_set_clocks(apollo);

	/* Put take GLB_CLKG and SCB out of reset */
	reg_reset_n |= (0x1 << GLB_CLKG_EN_SHIFT);
	reg_reset_n |= (0x1 << SCB_RESETN_SHIFT);
	iowrite32(reg_reset_n, apollo->tcf.registers +
		  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
	msleep(100);

	if (apollo->version == APOLLO_VERSION_TCF_2) {
		/* Enable the voltage control regulators on DUT */
		reg = ioread32(apollo->tcf.registers +
			TCF_CLK_CTRL_DUT_CONTROL_1);
		reg |= DUT_CTRL_VCC_0V9EN;
		reg |= DUT_CTRL_VCC_1V8EN;
		reg &= ~DUT_CTRL_VCC_IO_INH;
		reg &= ~DUT_CTRL_VCC_CORE_INH;
		iowrite32(reg, apollo->tcf.registers +
			TCF_CLK_CTRL_DUT_CONTROL_1);
		msleep(300);
	}
	/* Take PDP1 and PDP2 out of reset */
	reg_reset_n |= (0x1 << PDP1_RESETN_SHIFT);
	reg_reset_n |= (0x1 << PDP2_RESETN_SHIFT);

	iowrite32(reg_reset_n, apollo->tcf.registers +
		TCF_CLK_CTRL_CLK_AND_RST_CTRL);
	msleep(100);

	/* Take DDR out of reset */
	reg_reset_n |= (0x1 << DDR_RESETN_SHIFT);
	iowrite32(reg_reset_n, apollo->tcf.registers +
		  TCF_CLK_CTRL_CLK_AND_RST_CTRL);

	/* Take DUT_DCM out of reset */
	reg_reset_n |= (0x1 << DUT_DCM_RESETN_SHIFT);
	iowrite32(reg_reset_n, apollo->tcf.registers +
		  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
	msleep(100);


	err = iopol32_nonzero(DCM_LOCK_STATUS_MASK,
		apollo->tcf.registers + TCF_CLK_CTRL_DCM_LOCK_STATUS);

	if (err != 0)
		goto err_out;

	if (apollo->version == APOLLO_VERSION_TCF_2) {
		/* Set ODT to a specific value that seems to provide the most
		 * stable signals.
		 */
		spi_write(apollo, 0x11, 0x413130);
	}

	/* Take DUT out of reset */
	reg_reset_n |= (0x1 << DUT_RESETN_SHIFT);
	iowrite32(reg_reset_n, apollo->tcf.registers +
		  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
	msleep(100);

	if (apollo->version != APOLLO_VERSION_TCF_5) {
		err = apollo_align_interface_es2(apollo);
		if (err)
			goto err_out;
	}

	if (apollo->version == APOLLO_VERSION_TCF_2) {
		/* Enable the temperature sensor */
		spi_write(apollo, 0xc, 0); /* power up */
		spi_write(apollo, 0xc, 2); /* reset */
		spi_write(apollo, 0xc, 6); /* init & run */
	}

	/* Check the build */
	reg = ioread32(apollo->tcf.registers + 0x10);
	build_inc = (reg >> 12) & 0xff;
	build_owner = (reg >> 20) & 0xf;

	if (build_inc) {
		dev_alert(&apollo->pdev->dev, "BE WARNED: You are not running a tagged release of the FPGA image!\n");
		dev_alert(&apollo->pdev->dev, "Owner: 0x%01x, Inc: 0x%02x\n",
			  build_owner, build_inc);
	}

	dev_dbg(&apollo->pdev->dev, "FPGA Release: %u.%02u\n", reg >> 8 & 0xf,
		reg & 0xff);

err_out:
	return err;
}

static int apollo_hw_init(struct apollo_device *apollo)
{
	u32 reg;

	apollo_hard_reset(apollo);
	apollo_set_mem_mode(apollo);

	if (apollo->version == APOLLO_VERSION_TCF_BONNIE) {
		/* Enable ASTC via SPI */
		if (spi_read(apollo, 0xf, &reg)) {
			dev_err(&apollo->pdev->dev, "Failed to read apollo ASTC register\n");
			goto err_out;
		}

		reg |= 0x1 << 4;
		spi_write(apollo, 0xf, reg);
	}

err_out:
	return 0;
}

int apollo_sys_info(struct device *dev, u32 *tmp, u32 *pll)
{
	int err = -ENODEV;
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);

	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		goto err_out;
	}

	if (apollo->version == APOLLO_VERSION_TCF_5) {
		/* Not implemented on TCF5 */
		err = 0;
		goto err_out;
	}

	if (apollo->version == APOLLO_VERSION_TCF_2) {
		if (spi_read(apollo, TCF_TEMP_SENSOR_SPI_OFFSET, tmp)) {
			dev_err(dev, "Failed to read apollo temperature sensor\n");
			goto err_out;
		}

		*tmp = TCF_TEMP_SENSOR_TO_C(*tmp);
	}

	if (spi_read(apollo, 0x2, pll)) {
		dev_err(dev, "Failed to read PLL status\n");
		goto err_out;
	}

	err = 0;
err_out:
	return err;
}
EXPORT_SYMBOL(apollo_sys_info);

int apollo_core_clock_speed(struct device *dev)
{
	return apollo_core_clock;
}
EXPORT_SYMBOL(apollo_core_clock_speed);

#define HEX2DEC(v) ((((v) >> 4) * 10) + ((v) & 0x0F))
int apollo_sys_strings(struct device *dev,
		       char *str_fpga_rev, size_t size_fpga_rev,
		       char *str_tcf_core_rev, size_t size_tcf_core_rev,
		       char *str_tcf_core_target_build_id,
		       size_t size_tcf_core_target_build_id,
		       char *str_pci_ver, size_t size_pci_ver,
		       char *str_macro_ver, size_t size_macro_ver)
{
	int err = 0;
	u32 val;
	resource_size_t host_fpga_base;
	void __iomem *host_fpga_registers;

	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);

	if (!str_fpga_rev || !size_fpga_rev ||
	    !str_tcf_core_rev || !size_tcf_core_rev ||
	    !str_tcf_core_target_build_id || !size_tcf_core_target_build_id ||
	    !str_pci_ver || !size_pci_ver ||
	    !str_macro_ver || !size_macro_ver) {
		err = -EINVAL;
		goto err_out;
	}

	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}

	/* To get some of the version information we need to read from a
	 * register that we don't normally have mapped. Map it temporarily
	 * (without trying to reserve it) to get the information we need.
	 */
	host_fpga_base =
		pci_resource_start(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM)
		+ 0x40F0;

	host_fpga_registers = ioremap_nocache(host_fpga_base, 0x04);
	if (!host_fpga_registers) {
		dev_err(&apollo->pdev->dev, "Failed to map host fpga registers\n");
		err = -EIO;
		goto err_out;
	}

	/* Create the components of the PCI and macro versions */
	val = ioread32(host_fpga_registers);
	snprintf(str_pci_ver, size_pci_ver, "%d",
		 HEX2DEC((val & 0x00FF0000) >> 16));
	snprintf(str_macro_ver, size_macro_ver, "%d.%d",
		 (val & 0x00000F00) >> 8,
		 HEX2DEC((val & 0x000000FF) >> 0));

	/* Unmap the register now that we no longer need it */
	iounmap(host_fpga_registers);

	/* Create the components of the FPGA revision number */
	val = ioread32(apollo->tcf.registers + TCF_CLK_CTRL_FPGA_REV_REG);
	snprintf(str_fpga_rev, size_fpga_rev, "%d.%d.%d",
		 HEX2DEC((val & FPGA_REV_REG_MAJOR_MASK)
			 >> FPGA_REV_REG_MAJOR_SHIFT),
		 HEX2DEC((val & FPGA_REV_REG_MINOR_MASK)
			 >> FPGA_REV_REG_MINOR_SHIFT),
		 HEX2DEC((val & FPGA_REV_REG_MAINT_MASK)
			 >> FPGA_REV_REG_MAINT_SHIFT));

	/* Create the components of the TCF core revision number */
	val = ioread32(apollo->tcf.registers + TCF_CLK_CTRL_TCF_CORE_REV_REG);
	snprintf(str_tcf_core_rev, size_tcf_core_rev, "%d.%d.%d",
		 HEX2DEC((val & TCF_CORE_REV_REG_MAJOR_MASK)
			 >> TCF_CORE_REV_REG_MAJOR_SHIFT),
		 HEX2DEC((val & TCF_CORE_REV_REG_MINOR_MASK)
			 >> TCF_CORE_REV_REG_MINOR_SHIFT),
		 HEX2DEC((val & TCF_CORE_REV_REG_MAINT_MASK)
			 >> TCF_CORE_REV_REG_MAINT_SHIFT));

	/* Create the component of the TCF core target build ID */
	val = ioread32(apollo->tcf.registers +
		       TCF_CLK_CTRL_TCF_CORE_TARGET_BUILD_CFG);
	snprintf(str_tcf_core_target_build_id, size_tcf_core_target_build_id,
		 "%d",
		 (val & TCF_CORE_TARGET_BUILD_ID_MASK)
		 >> TCF_CORE_TARGET_BUILD_ID_SHIFT);

err_out:
	return err;
}
EXPORT_SYMBOL(apollo_sys_strings);

static irqreturn_t apollo_irq_handler(int irq, void *data)
{
	u32 interrupt_status;
	u32 interrupt_clear = 0;
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;
	struct apollo_device *apollo = (struct apollo_device *)data;

	spin_lock_irqsave(&apollo->interrupt_handler_lock, flags);

#if defined(APOLLO_FAKE_INTERRUPTS)
	/* If we're faking interrupts pretend we got both ext and PDP ints */
	interrupt_status = APOLLO_INTERRUPT_FLAG_EXT
		| APOLLO_INTERRUPT_FLAG_PDP;
#else
	interrupt_status = ioread32(apollo->tcf.registers
		+ TCF_CLK_CTRL_INTERRUPT_STATUS);
#endif

	if (interrupt_status & APOLLO_INTERRUPT_FLAG_EXT) {
		struct apollo_interrupt_handler *ext_int =
			&apollo->interrupt_handlers[APOLLO_INTERRUPT_EXT];

		if (ext_int->enabled && ext_int->handler_function) {
			ext_int->handler_function(ext_int->handler_data);
			interrupt_clear |= APOLLO_INTERRUPT_FLAG_EXT;
		}
		ret = IRQ_HANDLED;
	}
	if (interrupt_status & APOLLO_INTERRUPT_FLAG_PDP) {
		struct apollo_interrupt_handler *pdp_int =
			&apollo->interrupt_handlers[APOLLO_INTERRUPT_PDP];

		if (pdp_int->enabled && pdp_int->handler_function) {
			pdp_int->handler_function(pdp_int->handler_data);
			interrupt_clear |= APOLLO_INTERRUPT_FLAG_PDP;
		}
		ret = IRQ_HANDLED;
	}

	if (apollo->version == APOLLO_VERSION_TCF_5) {
		/* On TC5 the interrupt is not  by the TC framework, but
		 * by the PDP itself. So we always have to callback to the tc5
		 * pdp code regardless of the interrupt status of the TCF.
		 */
		struct apollo_interrupt_handler *pdp_int =
			&apollo->interrupt_handlers[APOLLO_INTERRUPT_TC5_PDP];

		if (pdp_int->enabled && pdp_int->handler_function) {
			pdp_int->handler_function(pdp_int->handler_data);
			ret = IRQ_HANDLED;
		}
	}

	if (interrupt_clear)
		iowrite32(0xffffffff,
			apollo->tcf.registers + TCF_CLK_CTRL_INTERRUPT_CLEAR);

	spin_unlock_irqrestore(&apollo->interrupt_handler_lock, flags);

	return ret;
}

#if defined(APOLLO_FAKE_INTERRUPTS)
static void apollo_irq_fake_wrapper(unsigned long data)
{
	struct apollo_device *apollo = (struct apollo_device *)data;

	apollo_irq_handler(0, apollo);

	mod_timer(&apollo->timer,
		jiffies + msecs_to_jiffies(FAKE_INTERRUPT_TIME_MS));
}
#endif

static int apollo_enable_irq(struct apollo_device *apollo)
{
	int err = 0;

#if defined(APOLLO_FAKE_INTERRUPTS)
	setup_timer(&apollo->timer, apollo_irq_fake_wrapper,
		(unsigned long)apollo);
	mod_timer(&apollo->timer,
		jiffies + msecs_to_jiffies(FAKE_INTERRUPT_TIME_MS));
#else
	{
		u32 val;

		iowrite32(0, apollo->tcf.registers +
			TCF_CLK_CTRL_INTERRUPT_ENABLE);
		iowrite32(0xffffffff, apollo->tcf.registers +
			TCF_CLK_CTRL_INTERRUPT_CLEAR);

		/* Set sense to active high */
		val = ioread32(apollo->tcf.registers +
			TCF_CLK_CTRL_INTERRUPT_OP_CFG) & ~(INT_SENSE_MASK);
		iowrite32(val, apollo->tcf.registers +
			TCF_CLK_CTRL_INTERRUPT_OP_CFG);

		err = request_irq(apollo->pdev->irq, apollo_irq_handler,
			IRQF_SHARED, DRV_NAME, apollo);
	}
#endif
	return err;
}

static void apollo_disable_irq(struct apollo_device *apollo)
{
#if defined(APOLLO_FAKE_INTERRUPTS)
	del_timer_sync(&apollo->timer);
#else
	iowrite32(0, apollo->tcf.registers +
		TCF_CLK_CTRL_INTERRUPT_ENABLE);
	iowrite32(0xffffffff, apollo->tcf.registers +
		TCF_CLK_CTRL_INTERRUPT_CLEAR);

	free_irq(apollo->pdev->irq, apollo);
#endif
}

static int register_pdp_device(struct apollo_device *apollo)
{
	int err = 0;
	resource_size_t reg_start =
		pci_resource_start(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM);
	struct resource pdp_resources_es2[] = {
		DEFINE_RES_MEM_NAMED(reg_start + SYS_APOLLO_REG_PDP1_OFFSET,
				     SYS_APOLLO_REG_PDP1_SIZE, "pdp-regs"),
		DEFINE_RES_MEM_NAMED(reg_start +
				     SYS_APOLLO_REG_PLL_OFFSET +
				     TCF_PLL_PLL_PDP_CLK0,
				     TCF_PLL_PLL_PDP2_DRP_GO -
				     TCF_PLL_PLL_PDP_CLK0 + 4, "pll-regs"),
	};
	struct resource pdp_resources_tcf5[] = {
		DEFINE_RES_MEM_NAMED(reg_start + SYS_APOLLO_REG_PDP1_OFFSET,
				     SYS_APOLLO_REG_PDP1_SIZE, "pdp-regs"),
		DEFINE_RES_MEM_NAMED(reg_start +
				     SYS_APOLLO_REG_PLL_OFFSET +
				     TCF_PLL_PLL_PDP_CLK0,
				     TCF_PLL_PLL_PDP2_DRP_GO -
				     TCF_PLL_PLL_PDP_CLK0 + 4, "pll-regs"),
		DEFINE_RES_MEM_NAMED(pci_resource_start(apollo->pdev,
					TC5_SYS_APOLLO_REG_PCI_BASENUM)
				+ TC5_SYS_APOLLO_REG_PDP2_OFFSET,
			TC5_SYS_APOLLO_REG_PDP2_SIZE, "tc5-pdp2-regs"),

		DEFINE_RES_MEM_NAMED(pci_resource_start(apollo->pdev,
					TC5_SYS_APOLLO_REG_PCI_BASENUM)
				+ TC5_SYS_APOLLO_REG_PDP2_FBDC_OFFSET,
			TC5_SYS_APOLLO_REG_PDP2_FBDC_SIZE,
			"tc5-pdp2-fbdc-regs"),

		DEFINE_RES_MEM_NAMED(pci_resource_start(apollo->pdev,
					TC5_SYS_APOLLO_REG_PCI_BASENUM)
				+ TC5_SYS_APOLLO_REG_HDMI_OFFSET,
			 TC5_SYS_APOLLO_REG_HDMI_SIZE, "tc5-adv5711-regs"),
	};
	struct apollo_pdp_platform_data pdata = {
#if defined(SUPPORT_ION)
		.ion_device = apollo->ion_device,
		.ion_heap_id = ION_HEAP_APOLLO_PDP,
#endif
		.apollo_memory_base = apollo->apollo_mem.base,
		.pdp_heap_memory_base = apollo->pdp_heap_mem_base,
		.pdp_heap_memory_size = apollo->pdp_heap_mem_size,
	};
	struct platform_device_info pdp_device_info = {
		.parent = &apollo->pdev->dev,
		.name = APOLLO_DEVICE_NAME_PDP,
		.id = -2,
		.data = &pdata,
		.size_data = sizeof(pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	if (apollo->version == APOLLO_VERSION_TCF_5) {
		pdp_device_info.res = pdp_resources_tcf5;
		pdp_device_info.num_res = ARRAY_SIZE(pdp_resources_tcf5);
	} else if (apollo->version == APOLLO_VERSION_TCF_2 ||
			apollo->version == APOLLO_VERSION_TCF_BONNIE) {
		pdp_device_info.res = pdp_resources_es2;
		pdp_device_info.num_res = ARRAY_SIZE(pdp_resources_es2);
	} else {
		dev_err(&apollo->pdev->dev, "Unable to set PDP resource info for unknown apollo device\n");
	}

	apollo->pdp_dev = platform_device_register_full(&pdp_device_info);
	if (IS_ERR(apollo->pdp_dev)) {
		err = PTR_ERR(apollo->pdp_dev);
		dev_err(&apollo->pdev->dev,
			"Failed to register PDP device (%d)\n", err);
		apollo->pdp_dev = NULL;
		goto err;
	}
err:
	return err;
}

#if defined(SUPPORT_RGX)

static int register_ext_device(struct apollo_device *apollo)
{
	int err = 0;
	struct resource rogue_resources[] = {
		DEFINE_RES_MEM_NAMED(pci_resource_start(apollo->pdev,
				SYS_RGX_REG_PCI_BASENUM),
			 SYS_RGX_REG_REGION_SIZE, "rogue-regs"),
	};
	struct apollo_rogue_platform_data pdata = {
#if defined(SUPPORT_ION)
		.ion_device = apollo->ion_device,
		.ion_heap_id = ION_HEAP_APOLLO_ROGUE,
#endif
		.apollo_memory_base = apollo->apollo_mem.base,
		.pdp_heap_memory_base = apollo->pdp_heap_mem_base,
		.pdp_heap_memory_size = apollo->pdp_heap_mem_size,
		.rogue_heap_memory_base = apollo->ext_heap_mem_base,
		.rogue_heap_memory_size = apollo->ext_heap_mem_size,
	};
	struct platform_device_info rogue_device_info = {
		.parent = &apollo->pdev->dev,
		.name = APOLLO_DEVICE_NAME_ROGUE,
		.id = -2,
		.res = rogue_resources,
		.num_res = ARRAY_SIZE(rogue_resources),
		.data = &pdata,
		.size_data = sizeof(pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	apollo->ext_dev = platform_device_register_full(&rogue_device_info);
	if (IS_ERR(apollo->ext_dev)) {
		err = PTR_ERR(apollo->ext_dev);
		dev_err(&apollo->pdev->dev,
			"Failed to register rogue device (%d)\n", err);
		apollo->ext_dev = NULL;
		goto err;
	}
err:
	return err;
}

#elif defined(SUPPORT_APOLLO_FPGA)

static int register_ext_device(struct apollo_device *apollo)
{
	int err = 0;
	struct resource fpga_resources[] = {
		/* FIXME: Don't overload SYS_RGX_REG_xxx for FPGA */
		DEFINE_RES_MEM_NAMED(pci_resource_start(apollo->pdev,
				SYS_RGX_REG_PCI_BASENUM),
			 SYS_RGX_REG_REGION_SIZE, "fpga-regs"),
	};
	struct apollo_fpga_platform_data pdata = {
		.apollo_memory_base = apollo->apollo_mem.base,
		.pdp_heap_memory_base = apollo->pdp_heap_mem_base,
		.pdp_heap_memory_size = apollo->pdp_heap_mem_size,
	};
	struct platform_device_info fpga_device_info = {
		.parent = &apollo->pdev->dev,
		.name = APOLLO_DEVICE_NAME_FPGA,
		.id = -1,
		.res = fpga_resources,
		.num_res = ARRAY_SIZE(fpga_resources),
		.data = &pdata,
		.size_data = sizeof(pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	apollo->ext_dev = platform_device_register_full(&fpga_device_info);
	if (IS_ERR(apollo->ext_dev)) {
		err = PTR_ERR(apollo->ext_dev);
		dev_err(&apollo->pdev->dev,
			"Failed to register fpga device (%d)\n", err);
		apollo->ext_dev = NULL;
		/* Fall through */
	}

	return err;
}

#else /* defined(SUPPORT_APOLLO_FPGA) */

static inline int register_ext_device(struct apollo_device *apollo)
{
	return 0;
}

#endif /* defined(SUPPORT_RGX) */

#if defined(SUPPORT_ION)

static int apollo_ion_init(struct apollo_device *apollo)
{
	int i, err = 0;
	struct ion_platform_heap ion_heap_data[APOLLO_ION_HEAP_COUNT] = {
		{
			.type = ION_HEAP_TYPE_SYSTEM,
			.id = ION_HEAP_TYPE_SYSTEM,
			.name = "system",
		},
		{
			.type = ION_HEAP_TYPE_CUSTOM,
			.id = ION_HEAP_APOLLO_PDP,
			.size = apollo->pdp_heap_mem_size,
			.base = apollo->pdp_heap_mem_base,
			.name = "apollo-pdp",
		},
#if defined(SUPPORT_RGX)
		{
			.type = ION_HEAP_TYPE_CUSTOM,
			.id = ION_HEAP_APOLLO_ROGUE,
			.size = apollo->ext_heap_mem_size,
			.base = apollo->ext_heap_mem_base,
			.name = "apollo-rogue",
		}
#endif /* defined(SUPPORT_RGX) */
	};

	apollo->ion_device = ion_device_create(NULL);
	if (IS_ERR_OR_NULL(apollo->ion_device)) {
		err = PTR_ERR(apollo->ion_device);
		goto err_out;
	}

	err = request_pci_io_addr(apollo->pdev, APOLLO_MEM_PCI_BASENUM, 0,
		apollo->apollo_mem.size);
	if (err) {
		dev_err(&apollo->pdev->dev,
			"Failed to request APOLLO memory (%d)\n", err);
		goto err_free_device;
	}

	apollo->ion_heaps[0] = ion_heap_create(&ion_heap_data[0]);
	if (IS_ERR_OR_NULL(apollo->ion_heaps[0])) {
		err = PTR_ERR(apollo->ion_heaps[0]);
		apollo->ion_heaps[0] = NULL;
		goto err_free_device;
	}
	ion_device_add_heap(apollo->ion_device, apollo->ion_heaps[0]);

	for (i = 1; i < APOLLO_ION_HEAP_COUNT; i++) {
		apollo->ion_heaps[i] = ion_lma_heap_create(&ion_heap_data[i]);
		if (IS_ERR_OR_NULL(apollo->ion_heaps[i])) {
			err = PTR_ERR(apollo->ion_heaps[i]);
			apollo->ion_heaps[i] = NULL;
			goto err_free_heaps;
		}
		ion_device_add_heap(apollo->ion_device, apollo->ion_heaps[i]);
	}

	return 0;

err_free_heaps:
	ion_heap_destroy(apollo->ion_heaps[0]);

	for (i = 1; i < APOLLO_ION_HEAP_COUNT; i++) {
		if (!apollo->ion_heaps[i])
			break;
		ion_lma_heap_destroy(apollo->ion_heaps[i]);
	}

	release_pci_io_addr(apollo->pdev, APOLLO_MEM_PCI_BASENUM,
		apollo->apollo_mem.base, apollo->apollo_mem.size);
err_free_device:
	ion_device_destroy(apollo->ion_device);
err_out:
	/* If the ptr was NULL, it is possible that err is 0 in the err path */
	if (err == 0)
		err = -ENOMEM;
	return err;
}

static void apollo_ion_deinit(struct apollo_device *apollo)
{
	int i = 0;

	ion_device_destroy(apollo->ion_device);
	ion_heap_destroy(apollo->ion_heaps[0]);
	for (i = 1; i < APOLLO_ION_HEAP_COUNT; i++)
		ion_lma_heap_destroy(apollo->ion_heaps[i]);
	release_pci_io_addr(apollo->pdev, APOLLO_MEM_PCI_BASENUM,
		apollo->apollo_mem.base, apollo->apollo_mem.size);
}

#endif /* defined(SUPPORT_ION) */

static enum apollo_version_t
apollo_detect_tc_version(struct apollo_device *apollo)
{
	u32 val = ioread32(apollo->tcf.registers +
		       TCF_CLK_CTRL_TCF_CORE_TARGET_BUILD_CFG);

	switch (val) {
	default:
		dev_err(&apollo->pdev->dev,
			"Unknown TCF core target build ID (0x%x) - assuming Hood ES2 - PLEASE REPORT TO ANDROID TEAM\n",
			val);
		/* Fall-through */
	case 5:
		dev_err(&apollo->pdev->dev, "Looks like a Hood ES2 TC\n");
		return APOLLO_VERSION_TCF_2;
	case 1:
		dev_err(&apollo->pdev->dev, "Looks like a TCF5\n");
		return APOLLO_VERSION_TCF_5;
	case 6:
		dev_err(&apollo->pdev->dev, "Looks like a Bonnie TC\n");
		return APOLLO_VERSION_TCF_BONNIE;
	}
}

static int setup_io_region(struct pci_dev *pdev,
	struct apollo_io_region *region, u32 index,
	resource_size_t offset,	resource_size_t size)
{
	int err;

	err = request_pci_io_addr(pdev, index, offset, size);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to request apollo registers (err=%d)\n", err);
		return -EIO;
	}

	region->region.base = pci_resource_start(pdev, index) + offset;
	region->region.size = size;

	region->registers =
		ioremap_nocache(region->region.base, region->region.size);
	if (!region->registers) {
		dev_err(&pdev->dev, "Failed to map apollo registers\n");
		release_pci_io_addr(pdev, index,
			region->region.base, region->region.size);
		return -EIO;
	}

	return 0;
}

static int apollo_dev_init(struct apollo_device *apollo, struct pci_dev *pdev)
{
	int err;

	apollo->pdev = pdev;

	spin_lock_init(&apollo->interrupt_handler_lock);
	spin_lock_init(&apollo->interrupt_enable_lock);

	/* Reserve and map the tcf_clk / "sys" registers */
	err = setup_io_region(pdev, &apollo->tcf,
		SYS_APOLLO_REG_PCI_BASENUM,
		SYS_APOLLO_REG_SYS_OFFSET, SYS_APOLLO_REG_SYS_SIZE);
	if (err)
		goto err_out;

	/* Reserve and map the tcf_pll registers */
	err = setup_io_region(pdev, &apollo->tcf_pll,
		SYS_APOLLO_REG_PCI_BASENUM,
		SYS_APOLLO_REG_PLL_OFFSET + TCF_PLL_PLL_CORE_CLK0,
		TCF_PLL_PLL_DRP_STATUS - TCF_PLL_PLL_CORE_CLK0 + 4);
	if (err)
		goto err_unmap_sys_registers;

#if defined(SUPPORT_APOLLO_FPGA)
#define FPGA_REGISTERS_SIZE 4
	/* If this is a special 'fgpa' build, have the apollo driver manage
	 * the second register bar. */
	err = setup_io_region(pdev, &apollo->fpga,
		SYS_RGX_REG_PCI_BASENUM, 0, FPGA_REGISTERS_SIZE);
	if (err)
		goto err_unmap_pll_registers;
#endif

	/* Detect testchip version */
	apollo->version = apollo_detect_tc_version(apollo);

	/* Setup card memory */
	apollo->apollo_mem.base =
		pci_resource_start(pdev, APOLLO_MEM_PCI_BASENUM);
	apollo->apollo_mem.size =
		pci_resource_len(pdev, APOLLO_MEM_PCI_BASENUM);

	if (apollo->apollo_mem.size < apollo_pdp_mem_size) {
		dev_err(&pdev->dev, "Apollo MEM region (bar %d) has size of %lu which is smaller than the requested PDP heap of %lu",
			APOLLO_MEM_PCI_BASENUM,
			(unsigned long)apollo->apollo_mem.size,
			(unsigned long)apollo_pdp_mem_size);
		err = -EIO;
		goto err_unmap_fpga_registers;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	apollo->mtrr = arch_phys_wc_add(apollo->apollo_mem.base,
	                                apollo->apollo_mem.size);
	if (apollo->mtrr < 0)
		goto err_unmap_fpga_registers;
#elif defined(CONFIG_MTRR)
	apollo->mtrr = mtrr_setup(pdev, apollo->apollo_mem.base,
		apollo->apollo_mem.size);
	if (apollo->mtrr == -2)
		goto err_unmap_fpga_registers;
#endif

	/* Setup ranges for the device heaps */
	apollo->pdp_heap_mem_size = apollo_pdp_mem_size;

	/* We know ext_heap_mem_size won't underflow as we've compared
	 * apollo_mem.size against the apollo_pdp_mem_size value earlier
	 */
	apollo->ext_heap_mem_size =
		apollo->apollo_mem.size - apollo->pdp_heap_mem_size;

	if (apollo->ext_heap_mem_size < APOLLO_EXT_MINIMUM_MEM_SIZE) {
		dev_warn(&pdev->dev, "Apollo MEM region (bar %d) has size of %lu, with %lu apollo_pdp_mem_size only %lu bytes are left for ext device, which looks too small",
			APOLLO_MEM_PCI_BASENUM,
			(unsigned long)apollo->apollo_mem.size,
			(unsigned long)apollo_pdp_mem_size,
			(unsigned long)apollo->ext_heap_mem_size);
		/* Continue as this is only a 'helpful warning' not a hard
		 * requirement
		 */
	}

	apollo->ext_heap_mem_base = apollo->apollo_mem.base;
	apollo->pdp_heap_mem_base =
		apollo->apollo_mem.base + apollo->ext_heap_mem_size;

#if defined(SUPPORT_ION)
	err = apollo_ion_init(apollo);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialise ION\n");
		goto err_unmap_fpga_registers;
	}
#endif

#if defined(SUPPORT_APOLLO_FPGA)
	apollo->debugfs_apollo_dir =
		debugfs_create_dir("apollo", NULL);
	apollo->debugfs_apollo_regs =
		debugfs_create_file_size("apollo-regs", S_IRUGO,
			apollo->debugfs_apollo_dir, &apollo->tcf,
			&apollo_io_debugfs_fops, apollo->tcf.region.size);
	apollo->debugfs_apollo_pll_regs =
		debugfs_create_file_size("apollo-pll-regs", S_IRUGO,
			apollo->debugfs_apollo_dir, &apollo->tcf_pll,
			&apollo_io_debugfs_fops, apollo->tcf_pll.region.size);
	apollo->debugfs_fpga_regs =
		debugfs_create_file_size("fpga-regs", S_IRUGO,
			apollo->debugfs_apollo_dir, &apollo->fpga,
			&apollo_io_debugfs_fops, apollo->fpga.region.size);
	apollo->debugfs_apollo_mem =
		debugfs_create_file_size("apollo-mem", S_IRUGO,
			apollo->debugfs_apollo_dir, &apollo->apollo_mem,
			&apollo_mem_debugfs_fops, apollo->apollo_mem.size);
#endif /* defined(SUPPORT_APOLLO_FPGA) */

err_out:
	return err;
err_unmap_fpga_registers:
#if defined(SUPPORT_APOLLO_FPGA)
	iounmap(apollo->fpga.registers);
	release_pci_io_addr(pdev, SYS_RGX_REG_PCI_BASENUM,
		apollo->fpga.region.base, apollo->fpga.region.size);
err_unmap_pll_registers:
#endif /* defined(SUPPORT_APOLLO_FPGA) */
	iounmap(apollo->tcf_pll.registers);
	release_pci_io_addr(pdev, SYS_APOLLO_REG_PCI_BASENUM,
		apollo->tcf_pll.region.base, apollo->tcf_pll.region.size);
err_unmap_sys_registers:
	iounmap(apollo->tcf.registers);
	release_pci_io_addr(pdev, SYS_APOLLO_REG_PCI_BASENUM,
		apollo->tcf.region.base, apollo->tcf.region.size);
	goto err_out;
}

static void apollo_dev_cleanup(struct apollo_device *apollo)
{
#if defined(SUPPORT_APOLLO_FPGA)
	debugfs_remove(apollo->debugfs_apollo_mem);
	debugfs_remove(apollo->debugfs_fpga_regs);
	debugfs_remove(apollo->debugfs_apollo_pll_regs);
	debugfs_remove(apollo->debugfs_apollo_regs);
	debugfs_remove(apollo->debugfs_apollo_dir);
#endif

#if defined(SUPPORT_ION)
	apollo_ion_deinit(apollo);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	if (apollo->mtrr >= 0)
		arch_phys_wc_del(apollo->mtrr);
#elif defined(CONFIG_MTRR)
	if (apollo->mtrr >= 0) {
		int err;

		err = mtrr_del(apollo->mtrr, apollo->apollo_mem.base,
			apollo->apollo_mem.size);
		if (err < 0)
			dev_err(&apollo->pdev->dev, "%d - %s: mtrr_del failed (%d)\n",
					__LINE__, __func__, err);
	}
#endif

#if defined(SUPPORT_APOLLO_FPGA)
	iounmap(apollo->fpga.registers);
	release_pci_io_addr(apollo->pdev, SYS_RGX_REG_PCI_BASENUM,
		apollo->fpga.region.base, apollo->fpga.region.size);
#endif

	iounmap(apollo->tcf_pll.registers);
	release_pci_io_addr(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM,
		apollo->tcf_pll.region.base, apollo->tcf_pll.region.size);

	iounmap(apollo->tcf.registers);
	release_pci_io_addr(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM,
		apollo->tcf.region.base, apollo->tcf.region.size);
}

static int apollo_init(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct apollo_device *apollo;
	int err = 0;

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	apollo = devres_alloc(apollo_devres_release,
		sizeof(*apollo), GFP_KERNEL);
	if (!apollo) {
		err = -ENOMEM;
		goto err_out;
	}
	devres_add(&pdev->dev, apollo);

	/* FIXME: Auto-detection based on TCF registers? */

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device returned %d\n", err);
		goto err_out;
	}

	err = apollo_dev_init(apollo, pdev);
	if (err)
		goto err_disable_device;

	err = apollo_hw_init(apollo);
	if (err)
		goto err_dev_cleanup;

	err = apollo_enable_irq(apollo);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialise IRQ\n");
		goto err_dev_cleanup;
	}

#if defined(APOLLO_FAKE_INTERRUPTS)
	dev_err(&pdev->dev, "WARNING: Faking interrupts every %d ms",
		FAKE_INTERRUPT_TIME_MS);
#endif

	/* Register ext and pdp platform devices
	 * Failures here aren't critical?
	 */
	register_pdp_device(apollo);
	register_ext_device(apollo);

	devres_remove_group(&pdev->dev, NULL);

	return err;

err_dev_cleanup:
	apollo_dev_cleanup(apollo);
err_disable_device:
	pci_disable_device(pdev);
err_out:
	devres_release_group(&pdev->dev, NULL);
	dev_err(&pdev->dev, "Failed to initialise apollo device\n");
	return err;
}

static void apollo_exit(struct pci_dev *pdev)
{
	int i;
	struct apollo_device *apollo = devres_find(&pdev->dev,
		apollo_devres_release, NULL, NULL);

	if (apollo->pdp_dev)
		platform_device_unregister(apollo->pdp_dev);

	if (apollo->ext_dev)
		platform_device_unregister(apollo->ext_dev);

	for (i = 0; i < APOLLO_INTERRUPT_COUNT; i++)
		apollo_disable_interrupt(&pdev->dev, i);
	apollo_disable_irq(apollo);
	apollo_dev_cleanup(apollo);
	pci_disable_device(pdev);
}

struct pci_device_id apollo_pci_tbl[] = {
	{ PCI_VDEVICE(POWERVR, DEVICE_ID_PCI_APOLLO_FPGA) },
	{ PCI_VDEVICE(POWERVR, DEVICE_ID_PCIE_APOLLO_FPGA) },
	{ },
};

static struct pci_driver apollo_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= apollo_pci_tbl,
	.probe		= apollo_init,
	.remove		= apollo_exit,
};

module_pci_driver(apollo_pci_driver);

MODULE_DEVICE_TABLE(pci, apollo_pci_tbl);

static u32 apollo_interrupt_id_to_flag(int interrupt_id)
{
	switch (interrupt_id) {
	case APOLLO_INTERRUPT_PDP:
		return APOLLO_INTERRUPT_FLAG_PDP;
	case APOLLO_INTERRUPT_EXT:
		return APOLLO_INTERRUPT_FLAG_EXT;
	default:
		BUG();
	}
}

static void apollo_enable_interrupt_register(struct apollo_device *apollo,
	int interrupt_id)
{
	u32 val;

	if (interrupt_id == APOLLO_INTERRUPT_PDP ||
		interrupt_id == APOLLO_INTERRUPT_EXT) {
		val = ioread32(
			apollo->tcf.registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
		val |= apollo_interrupt_id_to_flag(interrupt_id);
		iowrite32(val,
			apollo->tcf.registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
	}
}

static void apollo_disable_interrupt_register(struct apollo_device *apollo,
	int interrupt_id)
{
	u32 val;

	if (interrupt_id == APOLLO_INTERRUPT_PDP ||
		interrupt_id == APOLLO_INTERRUPT_EXT) {
		val = ioread32(
			apollo->tcf.registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
		val &= ~(apollo_interrupt_id_to_flag(interrupt_id));
		iowrite32(val,
			apollo->tcf.registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
	}
}

int apollo_enable(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return pci_enable_device(pdev);
}
EXPORT_SYMBOL(apollo_enable);

void apollo_disable(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	pci_disable_device(pdev);
}
EXPORT_SYMBOL(apollo_disable);

int apollo_set_interrupt_handler(struct device *dev, int interrupt_id,
	void (*handler_function)(void *), void *data)
{
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);
	int err = 0;
	unsigned long flags;

	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}

	if (interrupt_id < 0 || interrupt_id >= APOLLO_INTERRUPT_COUNT) {
		dev_err(dev, "Invalid interrupt ID (%d)\n", interrupt_id);
		err = -EINVAL;
		goto err_out;
	}

	spin_lock_irqsave(&apollo->interrupt_handler_lock, flags);

	apollo->interrupt_handlers[interrupt_id].handler_function =
		handler_function;
	apollo->interrupt_handlers[interrupt_id].handler_data = data;

	spin_unlock_irqrestore(&apollo->interrupt_handler_lock, flags);

err_out:
	return err;
}
EXPORT_SYMBOL(apollo_set_interrupt_handler);

int apollo_enable_interrupt(struct device *dev, int interrupt_id)
{
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);
	int err = 0;
	unsigned long flags;

	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}
	if (interrupt_id < 0 || interrupt_id >= APOLLO_INTERRUPT_COUNT) {
		dev_err(dev, "Invalid interrupt ID (%d)\n", interrupt_id);
		err = -EINVAL;
		goto err_out;
	}
	spin_lock_irqsave(&apollo->interrupt_enable_lock, flags);

	if (apollo->interrupt_handlers[interrupt_id].enabled) {
		dev_warn(dev, "Interrupt ID %d already enabled\n",
			interrupt_id);
		err = -EEXIST;
		goto err_unlock;
	}
	apollo->interrupt_handlers[interrupt_id].enabled = true;

	apollo_enable_interrupt_register(apollo, interrupt_id);

err_unlock:
	spin_unlock_irqrestore(&apollo->interrupt_enable_lock, flags);
err_out:
	return err;
}
EXPORT_SYMBOL(apollo_enable_interrupt);

int apollo_disable_interrupt(struct device *dev, int interrupt_id)
{
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);
	int err = 0;
	unsigned long flags;

	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}
	if (interrupt_id < 0 || interrupt_id >= APOLLO_INTERRUPT_COUNT) {
		dev_err(dev, "Invalid interrupt ID (%d)\n", interrupt_id);
		err = -EINVAL;
		goto err_out;
	}
	spin_lock_irqsave(&apollo->interrupt_enable_lock, flags);

	if (!apollo->interrupt_handlers[interrupt_id].enabled) {
		dev_warn(dev, "Interrupt ID %d already disabled\n",
			interrupt_id);
	}
	apollo->interrupt_handlers[interrupt_id].enabled = false;

	apollo_disable_interrupt_register(apollo, interrupt_id);

	spin_unlock_irqrestore(&apollo->interrupt_enable_lock, flags);
err_out:
	return err;
}
EXPORT_SYMBOL(apollo_disable_interrupt);
