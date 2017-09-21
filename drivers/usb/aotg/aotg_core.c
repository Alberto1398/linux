/*
 * Actions OWL SoCs usb2.0 controller driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * dengtaiping <dengtaiping@actions-semi.com>
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>

#include <asm/irq.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
#include <asm/prom.h>
#include <linux/kallsyms.h>
#include <linux/bootafinfo.h>

#include "aotg_hcd.h"
#include "aotg_plat_data.h"
#include "aotg_hcd_debug.h"
#include "aotg_udc_debug.h"
#include "aotg_mon.h"
#include "aotg_udc.h"

struct kmem_cache *td_cache;
int hcd_ports_en_ctrl;
struct aotg_udc *acts_udc_controller;
unsigned int port_device_enable[2];
int vbus_otg_en_gpio[2][2];
enum aotg_mode_e aotg_mode[2];
static int aotg_initialized[2];
int is_ls_device[2]; /*if detect low speed device plug in,must disable usbh high speed*/
struct mutex aotg_onoff_mutex;
struct aotg_hcd *act_hcd_ptr[2];
unsigned int port_host_plug_detect[2];
int aotg_udc_enable[2];
unsigned int aotg_wake_lock[2] = {0};
static u64 aotg_dmamask = DMA_BIT_MASK(32);
struct aotg_plat_data aotg_data[2];
enum ic_type_e ic_type[2];

static void aotg_plat_data_fill(struct device *dev, int dev_id)
{
	aotg_data[dev_id].no_hs = 0;
	if (0 == dev_id) {
		if (ic_type[0] == S700)
			aotg_data[0].usbecs = devm_ioremap_nocache(dev, 0xE024c094, 4);
		else if (ic_type[0] == S900)
			aotg_data[0].usbecs = devm_ioremap_nocache(dev, 0xE0228094, 4);
		else
			BUG_ON(1);
	} else if (1 == dev_id) {
		if (ic_type[1] == S700)
			aotg_data[1].usbecs = devm_ioremap_nocache(dev, 0xE024c098, 4);
		else if (ic_type[1] == S900)
			aotg_data[1].usbecs = devm_ioremap_nocache(dev, 0xE0228098, 4);
		else
			BUG_ON(1);
	} else {
		BUG_ON(1);
	}
}

static int usb_current_calibrate(void)
{
	unsigned int usb_hsdp;
	int ret;
	int val;
	ret = owl_get_usb_hsdp(&usb_hsdp);

	if (ret == 0) {
		switch (usb_hsdp & USB2_PHY_TX_CURRENT) {
		case 1:
			val = 0x9;
			break;
		case 2:
			val = 0x8;
			break;
		case 3:
			val = 0x7;
			break;
		case 4:
			val = 0x6;
			break;
		case 5:
			val = 0x5;
			break;
		case 6:
			val = 0x4;
			break;
		default:
			val = 0x6;
			break;
		}
	} else
		val = 0x6;
	return val;
}

static void aotg_DD_set_phy(void __iomem *base, u8 reg, u8 value)
{
	u8 addrlow, addrhigh;
	int time = 1;

	addrlow = reg & 0x0f;
	addrhigh = (reg >> 4) & 0x0f;

	/*write vstatus: */
	writeb(value, base + VDSTATUS);
	mb();

	/*write vcontrol: */
	writeb(addrlow | 0x10, base + VDCTRL);
	udelay(time); /*the vload period should > 33.3ns*/
	writeb(addrlow & 0x0f, base + VDCTRL);
	udelay(time);
	mb();
	writeb(addrlow | 0x10, base + VDCTRL);
	udelay(time);
	writeb(addrhigh | 0x10, base + VDCTRL);
	udelay(time);
	writeb(addrhigh & 0x0f, base + VDCTRL);
	udelay(time);
	writeb(addrhigh | 0x10, base + VDCTRL);
	udelay(time);
	return;
}

static void aotg_set_udc_phy(int id)
{
	if (ic_type[id] == S900) {
		aotg_DD_set_phy(aotg_data[id].base, 0xe7, 0x1b);
		aotg_DD_set_phy(aotg_data[id].base, 0xe7, 0x1f);
		udelay(10);
		aotg_DD_set_phy(aotg_data[id].base, 0xe2, 0x48);
		aotg_DD_set_phy(aotg_data[id].base, 0xe0, 0xa3);
		aotg_DD_set_phy(aotg_data[id].base, 0x87, 0x1f);
	} else if (ic_type[id] == S700) {
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0xbb);
		aotg_DD_set_phy(aotg_data[id].base, 0xe1, 0xcf);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0x9b);
		aotg_DD_set_phy(aotg_data[id].base, 0xe6, 0xcc);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0xbb);
		aotg_DD_set_phy(aotg_data[id].base, 0xe2, 0x02);
		aotg_DD_set_phy(aotg_data[id].base, 0xe2, 0x12);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0x9b);
		aotg_DD_set_phy(aotg_data[id].base, 0xe7, 0xa1);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0xbb);
		aotg_DD_set_phy(aotg_data[id].base, 0xe0, 0x31);
		aotg_DD_set_phy(aotg_data[id].base, 0xe0, 0x35);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0x9b);
		aotg_DD_set_phy(aotg_data[id].base, 0xe4, 0xac);
	} else {
		BUG_ON(1);
	}

	return;
}
static void aotg_set_hcd_phy(int id)
{
	int value;

	if (ic_type[id] == S900) {
		aotg_DD_set_phy(aotg_data[id].base, 0xe7, 0x1b);
		aotg_DD_set_phy(aotg_data[id].base, 0xe7, 0x1f);
		udelay(10);
		aotg_DD_set_phy(aotg_data[id].base, 0xe0, 0xa3);
		aotg_DD_set_phy(aotg_data[id].base, 0x87, 0x0f);
		aotg_DD_set_phy(aotg_data[id].base, 0xe3, 0x0b);
	} else if (ic_type[id] == S700) {
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0xbb);
		aotg_DD_set_phy(aotg_data[id].base, 0xe1, 0xcf);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0x9b);
		aotg_DD_set_phy(aotg_data[id].base, 0xe6, 0xcc);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0xbb);
		aotg_DD_set_phy(aotg_data[id].base, 0xe2, 0x2);
		aotg_DD_set_phy(aotg_data[id].base, 0xe2, 0x16);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0x9b);
		aotg_DD_set_phy(aotg_data[id].base, 0xe7, 0xa1);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0xbb);
		aotg_DD_set_phy(aotg_data[id].base, 0xe0, 0x31);
		aotg_DD_set_phy(aotg_data[id].base, 0xe0, 0x35);
		aotg_DD_set_phy(aotg_data[id].base, 0xf4, 0x9b);

		value = usb_current_calibrate();
		dev_info(aotg_data[id].dev, "aotg[%d] CURRENT value: 0x%x\n",
			id, value);
		value |=  (0xa<<4);
		aotg_DD_set_phy(aotg_data[id].base, 0xe4, value);
		aotg_DD_set_phy(aotg_data[id].base, 0xf0, 0xfc);
	} else {
		BUG_ON(1);
	}

	return;
}

void aotg_powergate_on(int id)
{
	pm_runtime_enable(aotg_data[id].dev);
	pm_runtime_get_sync(aotg_data[id].dev);

	clk_prepare_enable(aotg_data[id].clk_usbh_phy);
	if (ic_type[id] != S700)
		clk_prepare_enable(aotg_data[id].clk_usbh_cce);
	clk_prepare_enable(aotg_data[id].clk_usbh_pllen);
}

void aotg_powergate_off(int id)
{
	clk_disable_unprepare(aotg_data[id].clk_usbh_pllen);
	clk_disable_unprepare(aotg_data[id].clk_usbh_phy);
	if (ic_type[id] != S700)
		clk_disable_unprepare(aotg_data[id].clk_usbh_cce);

	pm_runtime_put_sync(aotg_data[id].dev);
	pm_runtime_disable(aotg_data[id].dev);
}

int aotg_wait_reset(int id)
{
	int i = 0;
	while (((readb(aotg_data[id].base + USBERESET) & USBERES_USBRESET) != 0) && (i < 300000)) {
		i++;
		udelay(1);
	}

	if (!(readb(aotg_data[id].base + USBERESET) & USBERES_USBRESET)) {
		dev_info(aotg_data[id].dev, "usb reset OK: %x.\n", readb(aotg_data[id].base + USBERESET));
	} else {
		dev_err(aotg_data[id].dev, "usb reset ERROR: %x.\n", readb(aotg_data[id].base + USBERESET));
		return -EBUSY;
	}
	return 0;
}

void aotg_hardware_init(int id)
{
	u8 val8;
	unsigned long flags;
	struct aotg_plat_data *data = &aotg_data[id];

	local_irq_save(flags);
	/*aotg_hcd_controller_reset(acthcd->port_specific);*/
	aotg_powergate_on(id);
	aotg_wait_reset(id);
	/* fpga : new DMA mode */
	writel(0x1, data->base + HCDMABCKDOOR);

	if (aotg_mode[id] == HCD_MODE) {
		if (ic_type[id] == S700)
			usb_writel(0x37000000 | (0x3<<4), data->usbecs);
		else
			usb_writel(0x17000000 | (0x3<<4), data->usbecs);
		local_irq_restore(flags);
		udelay(100);
		aotg_set_hcd_phy(id);
		local_irq_save(flags);

		/***** TA_BCON_COUNT *****/
		writeb(0x0, data->base + TA_BCON_COUNT);	/*110ms*/
		/*set TA_SUSPEND_BDIS timeout never generate */
		usb_writeb(0xff, data->base + TAAIDLBDIS);
		/*set TA_AIDL_BDIS timeout never generate */
		usb_writeb(0xff, data->base + TAWAITBCON);
		/*set TA_WAIT_BCON timeout never generate */
		usb_writeb(0x28, data->base + TBVBUSDISPLS);
		usb_setb(1 << 7, data->base + TAWAITBCON);

		usb_writew(0x1000, data->base + VBUSDBCTIMERL);

		val8 = readb(data->base + BKDOOR);
		if (data && data->no_hs)
			val8 |= (1 << 7);
		else
			val8 &= ~(1 << 7);

		if (is_ls_device[id])
			val8 |= (1<<7);
		writeb(val8, data->base + BKDOOR);
	} else {
		if (acts_udc_controller->inited == 0) { /*don't enter device mode in poweron stage*/
			usb_writel(0x17000000 | (0x3<<4), data->usbecs);
			acts_udc_controller->inited = 1;
		} else {
			writel(0xc00003F, data->usbecs);
		}
		aotg_set_udc_phy(id);
		usb_setbitsb(1 << 4, data->base + BKDOOR); /*clk40m */

		writeb(0xff, data->base + USBIRQ);
		writeb(0xff, data->base + OTGIRQ);
		writeb(readb(data->base + USBEIRQ), data->base + USBEIRQ);
		writeb(0xff, data->base + OTGIEN);
		writeb(0x0D, data->base + USBEIRQ);
	}
		mb();
	local_irq_restore(flags);

	return;
}

struct of_device_id aotg_of_match[] = {
	{.compatible = "actions,s700-usb2.0-0"},
	{.compatible = "actions,s700-usb2.0-1"},
	{.compatible = "actions,s900-usb2.0-0"},
	{.compatible = "actions,s900-usb2.0-1"},
	{},
};

MODULE_DEVICE_TABLE(of, aotg_of_match);

static int aotg_hcd_get_dts(struct platform_device *pdev)
{
	struct device_node *of_node = pdev->dev.of_node;
	enum of_gpio_flags flags;

	if (of_device_is_compatible(of_node, aotg_of_match[0].compatible)) {
		pdev->id = 0;
		ic_type[0] = S700;
	} else if (of_device_is_compatible(of_node, aotg_of_match[1].compatible)) {
		pdev->id = 1;
		ic_type[1] = S700;
	} else if (of_device_is_compatible(of_node, aotg_of_match[2].compatible)) {
		pdev->id = 0;
		ic_type[0] = S900;
	} else if (of_device_is_compatible(of_node, aotg_of_match[3].compatible)) {
		pdev->id = 1;
		ic_type[1] = S900;
	} else {
		dev_err(&pdev->dev, "compatible ic type failed\n");
	}
	pr_info("ic_type[%d]:%d\n", pdev->id, ic_type[pdev->id]);

	if (!of_find_property(of_node, "aotg_udc_enable", NULL))
		pr_info("usb2-%d can't find aotg_udc_enable config\n", pdev->id);
	else
		aotg_udc_enable[pdev->id] = !!(be32_to_cpup((const __be32 *)of_get_property(of_node, "aotg_udc_enable", NULL)));
	pr_info("aotg_udc_enable[%d]=%d\n", pdev->id, aotg_udc_enable[pdev->id]);

	if (!of_find_property(of_node, "vbus_otg_en_gpio", NULL)) {
		pr_debug("can't find vbus_otg_en_gpio config\n");
		vbus_otg_en_gpio[pdev->id][0] = -1;
	}	else {
		vbus_otg_en_gpio[pdev->id][0] = of_get_named_gpio_flags(of_node,
			"vbus_otg_en_gpio", 0, &flags);
		vbus_otg_en_gpio[pdev->id][1] = flags & 0x01;
		if (gpio_request(vbus_otg_en_gpio[pdev->id][0], "usb2.0")) {
			dev_err(&pdev->dev, "fail to request vbus gpio [%d]\n",
				vbus_otg_en_gpio[pdev->id][0]);
		/*	return -3;*/
		}
		gpio_direction_output(vbus_otg_en_gpio[pdev->id][0], vbus_otg_en_gpio[pdev->id][1]);
	}
	pr_info("vbus_otg_en_gpio:%d %d\n", vbus_otg_en_gpio[pdev->id][0],
		vbus_otg_en_gpio[pdev->id][1]);

	if (!of_find_property(of_node, "port_host_plug_detect", NULL)) {
		pr_debug("can't find usb%d port_host_plug_detect config\n", pdev->id);
		port_host_plug_detect[pdev->id] = 0;
	} else {
		port_host_plug_detect[pdev->id] = be32_to_cpup((const __be32 *)
			of_get_property(of_node, "port_host_plug_detect", NULL));
	}
	pr_info("port_host_plug_detect[%d]:%d\n",
		pdev->id, port_host_plug_detect[pdev->id]);

	if (!of_find_property(of_node, "aotg_wake_lock", NULL)) {
		pr_debug("can't find usb%d aotg_wake_lock config\n", pdev->id);
		aotg_wake_lock[pdev->id] = 0;
	} else {
		aotg_wake_lock[pdev->id] = be32_to_cpup((const __be32 *)
			of_get_property(of_node, "aotg_wake_lock", NULL));
	}
	pr_info("aotg_wake_lock[%d]:%d\n", pdev->id, aotg_wake_lock[pdev->id]);

	pr_info("ic_type[%d]:%d\n", pdev->id, ic_type[pdev->id]);

	return 0;
}

int aotg_probe(struct platform_device *pdev)
{
	struct resource *res_mem;
	int irq;
	int retval;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		dev_err(&pdev->dev, "<HCD_PROBE>usb has no resource for mem!\n");
		retval = -ENODEV;
		goto err0;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "<HCD_PROBE>usb has no resource for irq!\n");
		retval = -ENODEV;
		goto err1;
	}

	if (!request_mem_region(res_mem->start, res_mem->end - res_mem->start + 1, dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "<HCD_PROBE>request_mem_region failed\n");
		retval = -EBUSY;
		goto err1;
	}

	if (aotg_hcd_get_dts(pdev) < 0) {
		retval = -ENODEV;
		goto err1;
	}
	pr_info("pdev->id:%x\n", pdev->id);

	aotg_data[pdev->id].base = devm_ioremap(&pdev->dev, res_mem->start, res_mem->end - res_mem->start + 1);
	if (!aotg_data[pdev->id].base) {
		dev_err(&pdev->dev, "<HCD_PROBE>ioremap failed\n");
		retval = -ENOMEM;
		goto err1;
	}

	aotg_plat_data_fill(&pdev->dev, pdev->id);
	pdev->dev.dma_mask = &aotg_dmamask;
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	aotg_data[pdev->id].rsrc_start = res_mem->start;
	aotg_data[pdev->id].rsrc_len = res_mem->end - res_mem->start + 1;
	aotg_data[pdev->id].irq = irq;
	aotg_data[pdev->id].dev = &pdev->dev;
	device_init_wakeup(&pdev->dev, true);
	if (pdev->id)
		aotg_data[pdev->id].clk_usbh_pllen = devm_clk_get(&pdev->dev, "usbh1_pllen");
	else
		aotg_data[pdev->id].clk_usbh_pllen = devm_clk_get(&pdev->dev, "usbh0_pllen");
	if (IS_ERR(aotg_data[pdev->id].clk_usbh_pllen)) {
		dev_err(&pdev->dev, "unable to get usbh_pllen\n");
		retval = -EINVAL;
		goto err1;
	}

	if (pdev->id)
		aotg_data[pdev->id].clk_usbh_phy = devm_clk_get(&pdev->dev, "usbh1_phy");
	else
		aotg_data[pdev->id].clk_usbh_phy = devm_clk_get(&pdev->dev, "usbh0_phy");
	if (IS_ERR(aotg_data[pdev->id].clk_usbh_phy)) {
		dev_err(&pdev->dev, "unable to get usbh_phy\n");
		retval =  -EINVAL;
		goto err1;
	}

	if (ic_type[pdev->id] == S900) {
		if (pdev->id)
			aotg_data[pdev->id].clk_usbh_cce = devm_clk_get(&pdev->dev, "usbh1_cce");
		else
			aotg_data[pdev->id].clk_usbh_cce = devm_clk_get(&pdev->dev, "usbh0_cce");
		if (IS_ERR(aotg_data[pdev->id].clk_usbh_cce)) {
			dev_err(&pdev->dev, "unable to get usbh_cce\n");
			retval =  -EINVAL;
			goto err1;
		}
	}

/*	aotg_uhost_mon_init(pdev->id);*/
	return 0;

err1:
	release_mem_region(res_mem->start, res_mem->end - res_mem->start + 1);
err0:
	dev_err(&pdev->dev, "%s: usb probe hcd  failed, error is %d", __func__, retval);
	return retval;
}

int aotg_remove(struct platform_device *pdev)
{
	aotg_uhost_mon_exit();
	release_mem_region(aotg_data[pdev->id].rsrc_start, aotg_data[pdev->id].rsrc_len);
	return 0;
}

static inline int aotg_device_calc_id(int dev_id)
{
	int id;

	if (hcd_ports_en_ctrl == 1) {
		id = 0;
	} else if (hcd_ports_en_ctrl == 2) {
		id = 1;
	} else if (hcd_ports_en_ctrl == 3) {
		if (dev_id) {
			id = 0;
		} else {
			id = 1;
		}
	} else {
		id = dev_id;
	}
	return id;
}

int aotg_device_init(int dev_id)
{
	int ret = 0;
	struct usb_hcd *hcd;
	struct aotg_hcd *acthcd;
	struct aotg_udc *udc;
	struct device *dev = aotg_data[dev_id].dev;

	mutex_lock(&aotg_onoff_mutex);
	if (aotg_initialized[dev_id]) {
		aotg_initialized[dev_id]++;
		pr_warn("aotg%d initialized allready! cnt:%d\n", dev_id, aotg_initialized[dev_id]);
		mutex_unlock(&aotg_onoff_mutex);
		return 0;
	}
	aotg_initialized[dev_id] = 1;

	if (aotg_mode[dev_id] == HCD_MODE) {
		hcd = usb_create_hcd(&act_hc_driver, dev, dev_name(dev));
		if (!hcd) {
			dev_err(dev, "<HCD_PROBE>usb create hcd failed\n");
			ret = -ENOMEM;
			goto err0;
		}
		aotg_hcd_init(hcd, dev_id);

		hcd->rsrc_start = aotg_data[dev_id].rsrc_start;
		hcd->rsrc_len = aotg_data[dev_id].rsrc_len;

		acthcd = hcd_to_aotg(hcd);
		act_hcd_ptr[dev_id] = acthcd;
		acthcd->dev = dev;
		acthcd->base = aotg_data[dev_id].base;
		hcd->regs = acthcd->base;
		acthcd->hcd_exiting = 0;
		acthcd->uhc_irq = aotg_data[dev_id].irq;
		acthcd->id = dev_id;

		aotg_hardware_init(dev_id);

		hcd->self.sg_tablesize = 32;

		hcd->has_tt = 1;
		hcd->self.uses_pio_for_control = 1;

		hrtimer_init(&acthcd->hotplug_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		acthcd->hotplug_timer.function = aotg_hub_hotplug_timer;

		init_timer(&acthcd->trans_wait_timer);
		acthcd->trans_wait_timer.function = aotg_hub_trans_wait_timer;
		acthcd->trans_wait_timer.data = (unsigned long)acthcd;

		init_timer(&acthcd->check_trb_timer);
		acthcd->check_trb_timer.function = aotg_check_trb_timer;
		acthcd->check_trb_timer.data = (unsigned long)acthcd;

		ret = usb_add_hcd(hcd, acthcd->uhc_irq, 0);
		if (likely(ret == 0)) {
			aotg_enable_irq(acthcd);
			create_debug_file(acthcd);
			if (!port_host_plug_detect[acthcd->id]) {
				aotg_power_onoff(acthcd->id, 1);
				device_wakeup_enable(&hcd->self.root_hub->dev);
			}
			dev_info(dev, "hcd controller initialized. OTGIRQ: 0x%02X, OTGSTATE: 0x%02X\n",
				readb(acthcd->base + OTGIRQ), readb(acthcd->base + OTGSTATE));

			writeb(readb(acthcd->base + USBEIRQ), acthcd->base + USBEIRQ);
			mutex_unlock(&aotg_onoff_mutex);
			return 0;
		} else {
			dev_err(dev, "%s:usb add hcd failed\n", __func__);
			hrtimer_cancel(&acthcd->hotplug_timer);
			del_timer_sync(&acthcd->trans_wait_timer);
			del_timer_sync(&acthcd->check_trb_timer);
			usb_put_hcd(hcd);
		}
	} else {
		udc = &memory;
		udc->port_specific = &aotg_data[dev_id];

		acts_udc_controller = udc;
		udc->irq = aotg_data[dev_id].irq;
		udc->dev = dev;
		udc->base = aotg_data[dev_id].base;
		udc->id = dev_id;

		aotg_hardware_init(dev_id);

		udc_reinit(udc);

		ret = request_irq(udc->irq, aotg_udc_irq, 0, udc_driver_name, udc);
		if (ret != 0) {
			dev_err(dev, "%s: can't get irq %i, err %d\n", udc_driver_name, udc->irq, ret);
			goto err0;
		}
		aotg_udc_endpoint_config(udc);
		pullup(udc, 1);
		usb_setbitsb(USBEIRQ_USBIEN, udc->base + USBEIEN);
		mutex_unlock(&aotg_onoff_mutex);
		return 0;
	}
err0:
	aotg_powergate_off(dev_id);
	mutex_unlock(&aotg_onoff_mutex);
	return ret;
}

int aotg_device_exit(int dev_id)
{
	struct aotg_hcd *acthcd;
	struct aotg_udc *udc;
	struct usb_hcd *hcd;
	struct aotg_hcep *ep;
	int i;

	mutex_lock(&aotg_onoff_mutex);
	if (!aotg_initialized[dev_id]) {
		pr_warn("aotg%d exit allready!\n", dev_id);
		mutex_unlock(&aotg_onoff_mutex);
		return -EINVAL;
	}

	aotg_initialized[dev_id]--;
	if (aotg_initialized[dev_id] > 0) {
		pr_warn("aotg%d_exit cnt:%d\n", dev_id, aotg_initialized[dev_id]);
		mutex_unlock(&aotg_onoff_mutex);
		return -EINVAL;
	}
	aotg_initialized[dev_id] = 0;

	if (aotg_mode[dev_id] == HCD_MODE) {
		acthcd = act_hcd_ptr[dev_id];
		hcd = aotg_to_hcd(acthcd);
		usb_remove_hcd(hcd);
		act_hcd_ptr[dev_id] = NULL;
		aotg_disable_irq(acthcd);
		aotg_powergate_off(dev_id);
		if (!port_host_plug_detect[dev_id])
			aotg_power_onoff(dev_id, 0);
		acthcd->hcd_exiting = 1;
		pr_warn("usbh_cce%d had been poweroff...\n", dev_id);

		tasklet_kill(&acthcd->urb_tasklet);
		del_timer_sync(&acthcd->trans_wait_timer);
		del_timer_sync(&acthcd->check_trb_timer);
		hrtimer_cancel(&acthcd->hotplug_timer);
		remove_debug_file(acthcd);
		aotg_hcd_release_queue(acthcd, NULL);

		for (i = 0; i < MAX_EP_NUM; i++) {
			ep = acthcd->ep0[i];
			if (ep) {
				ACT_HCD_DBG
				if (ep->q)
					ACT_HCD_DBG
				kfree(ep);
			}
		}

		for (i = 1; i < MAX_EP_NUM; i++) {
			ep = acthcd->inep[i];
			if (ep) {
				ACT_HCD_DBG
				if (ep->ring) {
					ACT_HCD_DBG
				}
				kfree(ep);
			}
		}
		for (i = 1; i < MAX_EP_NUM; i++) {
			ep = acthcd->outep[i];
			if (ep) {
				ACT_HCD_DBG
				if (ep->ring)
					ACT_HCD_DBG
				kfree(ep);
			}
		}

		usb_put_hcd(hcd);
		acthcd = NULL;
	} else {
		udc = acts_udc_controller;
		free_irq(udc->irq, udc);
		udc->transceiver = NULL;
		acts_udc_controller = NULL;

		aotg_powergate_off(dev_id);
	}
	aotg_mode[dev_id] = DEFAULT_MODE;

	mutex_unlock(&aotg_onoff_mutex);
	return 0;
}

void aotg_udc_init(int id)
{
	int ret;
	struct aotg_udc *udc = &memory;
	mutex_lock(&aotg_onoff_mutex);
	aotg_initialized[id] = 1;

	udc->port_specific = &aotg_data[id];
	acts_udc_controller = udc;
	udc->irq = aotg_data[id].irq;
	udc->dev = aotg_data[id].dev;
	udc->base = aotg_data[id].base;
	udc->id = id;
	udc_reinit(udc);

	aotg_hardware_init(id);
	ret = request_irq(udc->irq, aotg_udc_irq, 0, udc_driver_name, udc);
	if (ret != 0) {
		dev_err(udc->dev, "%s: can't get irq %i, err %d\n", udc_driver_name, udc->irq, ret);
		goto err;
	}
	ret = usb_add_gadget_udc(udc->dev, &udc->gadget);
	if (ret) {
		free_irq(udc->irq, udc);
		goto err;
	}
	mutex_unlock(&aotg_onoff_mutex);
	return;

err:
	aotg_powergate_off(id);
	dev_err(aotg_data[id].dev, "%s: usb probe hcd  failed, error is %d", __func__, ret);
	mutex_unlock(&aotg_onoff_mutex);
	return;
}

void aotg_udc_exit(int id)
{
	struct aotg_udc *udc = acts_udc_controller;
	usb_del_gadget_udc(&udc->gadget);
	usb_gadget_unregister_driver(udc->driver);
	free_irq(udc->irq, udc);
	aotg_powergate_off(id);
}

int aotg_hub_register(int dev_id)
{
	int proc_id, ret = -1;
	proc_id = aotg_device_calc_id(dev_id);
	if (aotg_mode[dev_id] != UDC_MODE) {
		aotg_mode[dev_id] = HCD_MODE;
		ret = aotg_device_init(proc_id);
	} else {
		pr_err("aotg%d being in device mode\n", dev_id);
	}
	return ret;
}
EXPORT_SYMBOL(aotg_hub_register);

void aotg_hub_unregister(int dev_id)
{
	int proc_id;
	proc_id = aotg_device_calc_id(dev_id);
	if (aotg_mode[dev_id] != UDC_MODE)
		aotg_device_exit(dev_id);
	else
		pr_err("aotg%d being in device mode\n", dev_id);
}
EXPORT_SYMBOL(aotg_hub_unregister);

void aotg_udc_add(void)
{
	int id;
	if (owl_get_boot_mode())
		return;
	if (aotg_udc_enable[0]) {
		id = 0;
	} else if (aotg_udc_enable[1]) {
		id = 1;
	} else {
		pr_info("No aotg_udc being enabled!\n");
		return;
	}
	aotg_mode[id] = UDC_MODE;
	aotg_udc_init(id);
}

void aotg_udc_remove(void)
{
	int id;
	if (aotg_udc_enable[0])
		id = 0;
	else if (aotg_udc_enable[1])
		id = 1;
	else
		return;

	aotg_udc_exit(id);
	aotg_mode[id] = DEFAULT_MODE;
}

static struct workqueue_struct *start_mon_wq;
static struct delayed_work start_mon_wker;

static void start_mon(struct work_struct *work)
{
	aotg_uhost_mon_init(0);
	aotg_uhost_mon_init(1);
}

static int __init aotg_init(void)
{
	mutex_init(&aotg_onoff_mutex);
	td_cache = kmem_cache_create("aotg_hcd", sizeof(struct aotg_td),
		0, SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);
	platform_driver_register(&aotg_hcd_driver);
	create_acts_hcd_proc();
	platform_driver_register(&aotg_udc_driver);
	create_acts_udc_proc();
	aotg_udc_add();
	start_mon_wq = create_singlethread_workqueue("aotg_start_mon_wq");
	INIT_DELAYED_WORK(&start_mon_wker, start_mon);
	queue_delayed_work(start_mon_wq, &start_mon_wker, msecs_to_jiffies(10000));
	return 0;
}

static void __exit aotg_exit(void)
{
	cancel_delayed_work_sync(&start_mon_wker);
	flush_workqueue(start_mon_wq);
	destroy_workqueue(start_mon_wq);
	aotg_udc_remove();
	remove_acts_hcd_proc();
	platform_driver_unregister(&aotg_hcd_driver);
	remove_acts_udc_proc();
	platform_driver_unregister(&aotg_udc_driver);
	kmem_cache_destroy(td_cache);
	return;
}

module_init(aotg_init);
module_exit(aotg_exit);

MODULE_DESCRIPTION("Actions OTG controller driver");
MODULE_LICENSE("GPL");
