/*
 * Actions OWL SoCs usb monitor driver
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
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/atc260x/atc260x.h>

#include "aotg_regs.h"
#include "umonitor_hal.h"

#define USB_PLUGED_PC			1
#define USB_PLUGED_ADP		2

static void dwc3_controllor_init(usb_hal_monitor_t *pdev);
static void dwc3_controllor_exit(usb_hal_monitor_t *pdev);

extern int atc260x_enable_vbusotg(int on);

void mon_dwc3_set_mode(usb_hal_monitor_t *pdev, int mode)
{
	u32 reg;

	reg = readl(pdev->io_base + DWC3_GCTL);
	reg &= ~(DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_OTG));
	reg |= DWC3_GCTL_PRTCAPDIR(mode);
	writel(reg, pdev->io_base + DWC3_GCTL);
}


static int usb_monitor_vbus_power(usb_hal_monitor_t *pdev, int is_on)
{
	pr_info("usb_monitor_vbus_power %04x\n", is_on);
	if (pdev->config->power_switch_gpio_no < 0)
		return -1;
	if (is_on) {
		atc260x_enable_vbusotg(is_on);
		gpio_set_value_cansleep(pdev->config->power_switch_gpio_no, !pdev->config->power_switch_active_level);
	} else {
		gpio_set_value_cansleep(pdev->config->power_switch_gpio_no, pdev->config->power_switch_active_level);
		/**
		 * There is need to delay here because vbus need time to drop below 3.5v(normally 1-5ms),
		 * or after pmu vbusotg is enabled, the diode will clamp the voltage.
		 * Actually, it happend once after delay 5ms, so delay 10ms in case.
		 */
		mdelay(10);
		atc260x_enable_vbusotg(is_on);
	}

	return 0;
}

static int usb_get_dc5v_state(usb_hal_monitor_t *pdev)
{
	/* no use. */
	return USB_DC5V_INVALID;
}

static int get_usb_plug_type(struct usb_hal_monitor *pdev)
{
	int ret = 0;
	int reg_bk;
	int reg;

	reg_bk = readl(pdev->usbecs);
	if ((reg_bk & (1 << 11))) {
		reg = reg_bk;
		writel(reg | 0x0000f000, pdev->usbecs);	 /* dp up enable, dp down disable */
		udelay(200);
		reg = readl(pdev->usbecs);
		if ((reg & 0x00000018) != 0) {			/* bit 3, bit 4 not 0, this is usb_adapter */
			ret = USB_PLUGED_ADP;
		} else {
			ret = USB_PLUGED_PC;
		}
		writel(reg_bk, pdev->usbecs);			/*restore the USB3_P0_CTL */
	}

	return ret;
}

static int usb_get_vbus_state(usb_hal_monitor_t *pdev)
{
	int ret = USB_VBUS_INVALID;
	s32 adc_val;
	umon_port_config_t *pconfig = pdev->config;

	if (pconfig->force_detect == 11)
		return USB_VBUS_HIGH;
	else if (pconfig->force_detect == 22)
		return USB_VBUS_LOW;

	switch (pdev->config->vbus_type) {
	case UMONITOR_USB_VBUS:
		/* vbus valid. */
		ret = readl(pdev->usbecs) & (1 << pdev->usb3_p0_ctl.VBUS_P0);
		ret = ret ? USB_VBUS_HIGH : USB_VBUS_LOW;
		break;
	case UMONITOR_GPIO_VBUS:
		break;
	case UMONITOR_DC5V_VBUS:
		ret = atc260x_ex_auxadc_read_by_name("VBUSV", &adc_val);
		if (ret < 0)
			break;
		ret = (adc_val > VBUS_THRESHOLD) ? USB_VBUS_HIGH : USB_VBUS_LOW;
		break;
	default:
		break;
	}
	if ((pdev->config->vbus_debug == 0) || (pdev->config->vbus_debug == 1))
		return pdev->config->vbus_debug;
	if (pdev->config->vbus_debug == 2)
		pr_info("\n  usb_get_vbus_state: %d \n", ret);
	MONITOR_PRINTK("%s state: %u\n", __func__, ret);
	return ret;
}

static int usb_hardware_init(usb_hal_monitor_t *pdev);
static int usb_get_idpin_state(usb_hal_monitor_t *pdev)
{
	int ret = USB_ID_STATE_INVALID;
	umon_port_config_t *pconfig = pdev->config;

	/*need to open usbpll before read idpin status*/
	dwc3_controllor_init(pdev);

	if (pconfig->force_detect == 33)
		return USB_ID_STATE_HOST;

	else if (pconfig->force_detect == 44)
		return USB_ID_STATE_DEVICE;

	switch (pdev->config->idpin_type) {
	case UMONITOR_USB_IDPIN:
		ret = (readl(pdev->usbecs) &
			(1 << pdev->usb3_p0_ctl.ID_P0)) ? USB_ID_STATE_DEVICE : USB_ID_STATE_HOST;
		break;
	case UMONITOR_SOFT_IDPIN:
		break;
	case UMONITOR_GPIO_IDPIN:
		if (0 == gpio_get_value_cansleep(pdev->config->idpin_gpio_no))
			ret = USB_ID_STATE_HOST;
		else
			ret = USB_ID_STATE_DEVICE;
		break;
	default:
		break;
	}
	/* need to close usbpll after read idpin status*/
	dwc3_controllor_exit(pdev);
	if ((pdev->config->idpin_debug == 0) || (pdev->config->idpin_debug == 1))
		return pdev->config->idpin_debug ? USB_ID_STATE_DEVICE : USB_ID_STATE_HOST;
	if (pdev->config->idpin_debug == 2)
		pr_info("\n usb_get_idpin_state: %s \n", (ret == 1) ? "device" : "host");
	return ret;
}

static unsigned int usb_get_linestates(usb_hal_monitor_t *pdev)
{
	unsigned int ls;

	/* need to open usbpll before read idpin status */
	dwc3_controllor_init(pdev);

	ls = ((readl(pdev->usbecs) & pdev->usb3_p0_ctl.LS_P0_MASK) >> pdev->usb3_p0_ctl.LS_P0_SHIFT);

	/* need to close usbpll after read idpin status*/
	dwc3_controllor_exit(pdev);
	return ls;
}

static int usb_set_dp_500k_15k(usb_hal_monitor_t *pdev, int enable_500k_up,
		int enable_15k_down)
{
	unsigned int val;

	val = readl(pdev->usbecs) & (~((1 << pdev->usb3_p0_ctl.DPPUEN_P0) |
		(1 << pdev->usb3_p0_ctl.DMPUEN_P0) |
		(1 << pdev->usb3_p0_ctl.DPPDDIS_P0) |
		(1 << pdev->usb3_p0_ctl.DMPDDIS_P0)));

	if (enable_500k_up != 0)
		val |= (1 << pdev->usb3_p0_ctl.DPPUEN_P0) | (1 << pdev->usb3_p0_ctl.DMPUEN_P0);

	if (enable_15k_down == 0)
		val |= (1 << pdev->usb3_p0_ctl.DPPDDIS_P0) | (1 << pdev->usb3_p0_ctl.DMPDDIS_P0);

	MONITOR_PRINTK("dpdm is %08x\n", val);
	writel(val, pdev->usbecs);	/* 500k up enable, 15k down disable; */
	return 0;
}

static int usb_set_soft_id(usb_hal_monitor_t *pdev, int en_softid,
	int id_state)
{
#if 0
	unsigned int val;

	if (pdev->config->idpin_type == UMONITOR_USB_IDPIN) {
		/* ignore soft idpin setting. */
		en_softid = 0;
	}
	val = readl(pdev->usbecs);
	if (en_softid != 0)
		val |= 0x1 << pdev->usb3_p0_ctl.SOFTIDEN_P0;	/* soft id enable. */
	else
		val &= ~(0x1 << pdev->usb3_p0_ctl.SOFTIDEN_P0);	/* soft id disable. */

	if (id_state != 0)
		val |= (0x1 << USB3_P0_CTL_SOFTID_P0);
	else
		val &= ~(0x1 << USB3_P0_CTL_SOFTID_P0);

	writel(val, pdev->usbecs);
#endif
	return 0;
}

static int usb_set_soft_vbus(usb_hal_monitor_t *pdev, int en_softvbus, int vbus_state)
{
	unsigned int val;

	if (pdev->config->vbus_type == UMONITOR_USB_VBUS) {
		/* ignore soft vbus setting. */
		en_softvbus = 0;
	}

	val = readl(pdev->usbecs);
	if (en_softvbus != 0)
		val |= 0x1 << pdev->usb3_p0_ctl.SOFTVBUSEN_P0;	/* soft id enable. */
	else
		val &= ~(0x1 << pdev->usb3_p0_ctl.SOFTVBUSEN_P0);	/* soft id disable. */

	if (vbus_state != 0)
		val |= (0x1 << pdev->usb3_p0_ctl.SOFTVBUS_P0);
	else
		val &= ~(0x1 << pdev->usb3_p0_ctl.SOFTVBUS_P0);
	writel(val, pdev->usbecs);

	return 0;
}

static int usb_hardware_init(usb_hal_monitor_t *pdev)
{
	writel((readl(pdev->usbecs) | pdev->usb3_p0_ctl.VBUS_DET_THRESHOLD), pdev->usbecs);

	MONITOR_PRINTK("usbecs value is %08x------>/n", readl(pdev->usbecs));
	return 0;
}

static int usb_hal_aotg_enable(usb_hal_monitor_t *pdev, int enable)
{
	if (enable != 0) {
		MONITOR_PRINTK("aotg mon enable\n");
		if (usb_hardware_init(pdev) != 0)
			return -1;
	} else {
		MONITOR_PRINTK("aotg mon disable\n");
	}
	return 0;
}

static int usb_hal_set_mode(usb_hal_monitor_t *pdev, int mode)
{
	if (mode == USB_IN_HOST_MOD)
		writel(readl(pdev->usbecs) & (~(pdev->usb3_p0_ctl.ECS_DPPDDIS | pdev->usb3_p0_ctl.ECS_DMPDDIS | pdev->usb3_p0_ctl.ECS_DPPUEN | pdev->usb3_p0_ctl.ECS_DMPUEN)), pdev->usbecs);

	return 0;
}

static void usb_hal_dp_up(usb_hal_monitor_t *pdev)
{
	return;
}

static void usb_hal_dp_down(usb_hal_monitor_t *pdev)
{
	return;
}

static int usb_hal_is_sof(usb_hal_monitor_t *pdev)
{
	return 0;
}

static int usb_hal_enable_irq(struct usb_hal_monitor *pdev, int enable)
{
	return 0;
}

static void usb_hal_debug(usb_hal_monitor_t *pdev)
{
	pr_info("%s:%d\n", __FILE__, __LINE__);
	return;
}

int usb_suspend_or_resume(usb_hal_monitor_t *pdev, int is_suspend)
{
	/* save/reload usbecs when suspend/resume */
	if (is_suspend)
		pdev->usbecs_val = readl(pdev->usbecs);
	else
		usb_hardware_init(pdev);

	return 0;
}
extern void dwc3_owl_clock_init(void);
extern void dwc3_owl_clock_exit(void);

static void dwc3_controllor_init(usb_hal_monitor_t *pdev)
{
	dwc3_owl_clock_init();
	return;
}

static void dwc3_controllor_exit(usb_hal_monitor_t *pdev)
{
	dwc3_owl_clock_exit();
	return;
}

void dwc3_controllor_mode_cfg(usb_hal_monitor_t *pdev)
{
	return;
}
int usb_hal_init_monitor_hw_ops(usb_hal_monitor_t *pdev,
	umon_port_config_t *config, void __iomem  *base, struct monitor_data *monitor_data)
{
	int ret = 0;

	pdev->name = "usb controller";
	pdev->io_base = base;
	pdev->usbecs = ioremap(monitor_data->usbecs, 4);
	pdev->ic_type = monitor_data->ic_type;
	pdev->usb3_p0_ctl = monitor_data->usb3_p0_ctl;

	pdev->vbus_power_onoff = usb_monitor_vbus_power;
	pdev->get_dc5v_state = usb_get_dc5v_state;
	pdev->get_vbus_state = usb_get_vbus_state;
	pdev->get_linestates = usb_get_linestates;
	pdev->get_idpin_state = usb_get_idpin_state;
	pdev->set_dp_500k_15k = usb_set_dp_500k_15k;
	pdev->set_soft_id = usb_set_soft_id;
	pdev->set_soft_vbus = usb_set_soft_vbus;
	pdev->aotg_enable = usb_hal_aotg_enable;
	pdev->set_mode = usb_hal_set_mode;
	pdev->dp_up = usb_hal_dp_up;
	pdev->dp_down = usb_hal_dp_down;
	pdev->is_sof = usb_hal_is_sof;
	pdev->enable_irq = usb_hal_enable_irq;
	pdev->debug = usb_hal_debug;
	pdev->monitor_get_usb_plug_type = get_usb_plug_type;
	pdev->suspend_or_resume = usb_suspend_or_resume;
	pdev->dwc3_otg_mode_cfg = dwc3_controllor_mode_cfg;
	pdev->dwc3_otg_exit = dwc3_controllor_exit;
	pdev->dwc3_otg_init = dwc3_controllor_init;
	pdev->config = config;
	return ret;
}
