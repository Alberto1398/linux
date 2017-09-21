/**
 * Actions OWL SoCs dwc3 plug driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * tangshaoqing <tangshaoqing@actions-semi.com>
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

#include <linux/kallsyms.h>


#if SUPPORT_NOT_RMMOD_USBDRV
extern void dwc3_gadget_plug_usb2_phy_suspend(struct dwc3 *dwc, int suspend);
extern int dwc3_gadget_plugin_init(struct dwc3 *dwc);
extern void dwc3_gadget_plug_disconnect(struct dwc3 *dwc);
extern void set_dwc3_gadget_plugin_flag(int flag);
extern int dwc3_gadget_plug_pullup(struct usb_gadget *g, int is_on);
extern int dwc3_gadget_plug_resume(struct dwc3 *dwc);
#ifdef USB_CHARGE_DETECT
extern void dwc3_plug_usb_charge_detect_init(void);
extern void dwc3_plug_usb_charge_detect_exit(void);
#endif

static int dwc3_core_init(struct dwc3 *dwc);
static int dwc3_event_buffers_setup(struct dwc3 *dwc);

enum plugstate{
	PLUGSTATE_A_OUT=0,
	PLUGSTATE_B_OUT,
	PLUGSTATE_A_IN,
	PLUGSTATE_B_IN,
	PLUGSTATE_A_SUSPEND,
	PLUGSTATE_A_RESUME,
	PLUGSTATE_B_SUSPEND,
	PLUGSTATE_B_RESUME,
};

struct dwc3_plug {
	int state;

	struct dwc3	*dwc;
	struct usb_gadget_driver *dwc_gadget_driver;
};
static struct dwc3_plug _dwc_plug;

static DEFINE_MUTEX(dwc_plug_lock);

typedef void (*FUNC)(void);
static FUNC dwc3_owl_plug_clock_init;
static FUNC dwc3_owl_plug_clock_exit;
static FUNC dwc3_owl_plug_suspend;
static FUNC dwc3_owl_plug_resume;

static int dwc3_gadget_plugout(struct dwc3_plug *dwc_plug)
{
	struct dwc3 *dwc = dwc_plug->dwc;
	struct usb_gadget *gadget = &dwc->gadget;
	unsigned long		flags;
	
	dwc_plug->dwc_gadget_driver = dwc->gadget_driver;

	spin_lock_irqsave(&dwc->lock, flags);
	set_dwc3_gadget_plugin_flag(0);
	dwc3_gadget_plug_pullup(&dwc->gadget, 0);
	spin_unlock_irqrestore(&dwc->lock, flags);
	
	dwc3_gadget_plug_disconnect(dwc);
	if (dwc->gadget_driver != NULL)
		gadget->ops->udc_stop(&dwc->gadget, dwc->gadget_driver);
	return 0;
}

static int dwc3_gadget_plugin(struct dwc3_plug *dwc_plug)
{
	struct dwc3 *dwc = dwc_plug->dwc;
	struct usb_gadget *gadget = &dwc->gadget;
	unsigned long		flags;

	dwc3_gadget_plugin_init(dwc);

	gadget->ops->udc_start(gadget, dwc_plug->dwc_gadget_driver);

	spin_lock_irqsave(&dwc->lock, flags);
	dwc3_gadget_plug_pullup(gadget, 1);
	set_dwc3_gadget_plugin_flag(1);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_plug_out(struct dwc3_plug *dwc_plug, int s)
{
	struct dwc3 *dwc = dwc_plug->dwc;
	
	pr_info("%s %d\n", __func__, __LINE__);

	if((dwc_plug->state == PLUGSTATE_A_OUT)||(dwc_plug->state == PLUGSTATE_B_OUT)) {
		dwc_plug->state = s;
		return 0;
	}

	if(( s == PLUGSTATE_A_OUT)&&(dwc_plug->state != PLUGSTATE_A_IN)) {
		return -1;
	}

	if((s == PLUGSTATE_B_OUT)&&(dwc_plug->state != PLUGSTATE_B_IN)) {
		return -1;
	}

	if(s == PLUGSTATE_B_OUT) {
		pr_info("%s %d B_OUT\n", __func__, __LINE__);

		if (dwc->gadget_driver)
			dwc3_gadget_plugout(dwc_plug);
#ifdef USB_CHARGE_DETECT
		dwc3_plug_usb_charge_detect_exit();
#endif
	}

	if(s == PLUGSTATE_A_OUT) {
		pr_info("%s %d A_OUT\n", __func__, __LINE__);

		if(dwc->xhci)
			dwc3_host_exit(dwc);

		dwc->xhci = 0;
	}

	pm_runtime_put_sync(dwc->dev);
	pm_runtime_disable(dwc->dev);

	dwc3_owl_plug_clock_exit();

	dwc_plug->state = s;

	return 0;
}

static void dwc3_set_host_in_auto_retry(void)
{
	struct dwc3 *dwc = _dwc_plug.dwc;
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_GUCTL);
	/* fix asix usbnet dongle retry hangup */
	reg |= (1<<14);
	dwc3_writel(dwc->regs, DWC3_GUCTL, reg);
}

static int dwc3_plug_in(struct dwc3_plug *dwc_plug,int s)
{
	struct dwc3 *dwc = dwc_plug->dwc;
	int ret=0;

	pr_info("%s %d\n", __func__, __LINE__);

	if(s == dwc_plug->state)
		return 0;

	if((dwc_plug->state != PLUGSTATE_A_OUT)&&(dwc_plug->state != PLUGSTATE_B_OUT))
		return -1;

	dwc3_owl_plug_clock_init();

	pm_runtime_enable(dwc->dev);
	pm_runtime_get_sync(dwc->dev);
	pm_runtime_forbid(dwc->dev);

	ret = dwc3_core_init(dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to initialize core\n");
		goto err1;
	}
	dwc3_event_buffers_setup(dwc);

	if(s == PLUGSTATE_A_IN) {
		pr_info("%s %d A_IN\n", __func__, __LINE__);

		dwc3_set_host_in_auto_retry();
		dwc3_gadget_plug_usb2_phy_suspend(dwc, false);
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_HOST);

		ret = dwc3_host_init(dwc);
		if (ret) {
			goto err1;
		}
	}
	else if(s == PLUGSTATE_B_IN) {
		pr_info("%s %d B_IN\n", __func__, __LINE__);

		owl_dwc3_usb2phy_param_setup(1);
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_DEVICE);
		dwc3_gadget_plugin(dwc_plug);
#ifdef USB_CHARGE_DETECT
		dwc3_plug_usb_charge_detect_init();
#endif
	}

	pm_runtime_allow(dwc->dev);

	dwc_plug->state = s;

	return 0;

err1:
	pm_runtime_allow(dwc->dev);
	pm_runtime_put_sync(dwc->dev);
	pm_runtime_disable(dwc->dev);

	dwc3_owl_plug_clock_exit();

	return -1;
}

static int __dwc3_set_plugstate(struct dwc3_plug *dwc_plug, int s)
{
	int ret;

   	if(dwc_plug->dwc ==NULL) {
		pr_info("%s %d dwc is not exit\n", __func__, __LINE__);
		return -ENODEV;
	}

	if((s==PLUGSTATE_A_OUT)||(s==PLUGSTATE_B_OUT)) {
        	ret = dwc3_plug_out(dwc_plug,s);
	}
	else if((s==PLUGSTATE_A_IN)||(s==PLUGSTATE_B_IN)) {
		ret = dwc3_plug_in(dwc_plug,s);
	}

	return dwc_plug->state;
}

int dwc3_set_plugstate(int s)
{
	int ret;

	mutex_lock(&dwc_plug_lock);
	ret = __dwc3_set_plugstate(&_dwc_plug,s);
	mutex_unlock(&dwc_plug_lock);

	 return ret;
}
EXPORT_SYMBOL_GPL(dwc3_set_plugstate);

int dwc3_plug_init(struct dwc3	*dwc)
{
	_dwc_plug.dwc = 0;
	_dwc_plug.dwc_gadget_driver = 0;
	_dwc_plug.state = PLUGSTATE_B_IN;

	dwc3_owl_plug_clock_init = (FUNC)kallsyms_lookup_name("dwc3_owl_clock_init");
	dwc3_owl_plug_clock_exit = (FUNC)kallsyms_lookup_name("dwc3_owl_clock_exit");

	dwc3_owl_plug_suspend = (FUNC)kallsyms_lookup_name("dwc3_owl_plug_suspend");
	dwc3_owl_plug_resume= (FUNC)kallsyms_lookup_name("dwc3_owl_plug_resume");

	if((!dwc3_owl_plug_clock_init)||(!dwc3_owl_plug_clock_exit)) {
		pr_info("%s %d get dwc3 clock function fail\n", __func__, __LINE__);
		return -1;
	}

	_dwc_plug.dwc = dwc;
	return 0;
}

int dwc3_plug_exit(struct dwc3	*dwc)
{
	_dwc_plug.dwc = 0;
	dwc3_owl_plug_clock_exit = 0;
	dwc3_owl_plug_clock_init = 0;

	dwc3_owl_plug_suspend = 0;
	dwc3_owl_plug_resume= 0;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwc3_plug_prepare(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	unsigned long	flags;

	if((_dwc_plug.state != PLUGSTATE_A_IN) && (_dwc_plug.state != PLUGSTATE_B_IN))
		return 0;

	spin_lock_irqsave(&dwc->lock, flags);

	if(_dwc_plug.state == PLUGSTATE_B_IN)
		dwc3_gadget_prepare(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static void dwc3_plug_complete(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	unsigned long	flags;
	
	if((_dwc_plug.state != PLUGSTATE_A_IN) && (_dwc_plug.state != PLUGSTATE_B_IN))
		return;

	spin_lock_irqsave(&dwc->lock, flags);

	if(_dwc_plug.state == PLUGSTATE_B_IN) {
		dwc->pullups_connected = true;
		dwc3_gadget_complete(dwc);
	}

	spin_unlock_irqrestore(&dwc->lock, flags);
}

static int dwc3_plug_gadget_suspend(struct dwc3_plug *dwc_plug)
{
	dwc3_gadget_plugout(dwc_plug);

	return 0;
}

static int dwc3_plug_suspend(struct device *dev)
{
	pr_info("%s start\n", __func__);

	if((_dwc_plug.state != PLUGSTATE_A_IN)&&(_dwc_plug.state != PLUGSTATE_B_IN)) {
		return 0;
	}

	if(_dwc_plug.state == PLUGSTATE_B_IN) {
		pr_info("%s %d B_SUSPEND\n", __func__, __LINE__);
		dwc3_plug_gadget_suspend(&_dwc_plug);
	}

	dwc3_owl_plug_suspend();

	if(_dwc_plug.state == PLUGSTATE_A_IN)
		_dwc_plug.state = PLUGSTATE_A_SUSPEND;
	else
		_dwc_plug.state = PLUGSTATE_B_SUSPEND;

	pr_info("%s end\n", __func__);

	return 0;
}

static int dwc3_plug_gadget_resume(struct dwc3_plug *dwc_plug)
{
	struct dwc3 *dwc = dwc_plug->dwc;
	struct usb_gadget *gadget = &dwc->gadget;
	unsigned long		flags;

	dwc3_gadget_plugin_init(dwc);

	gadget->ops->udc_start(gadget, dwc_plug->dwc_gadget_driver);

	spin_lock_irqsave(&dwc->lock, flags);
	//dwc3_gadget_plug_pullup(gadget, 1);
	set_dwc3_gadget_plugin_flag(1);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_plug_resume(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	int ret=0;

	pr_info("%s start\n", __func__);

	if((_dwc_plug.state != PLUGSTATE_A_SUSPEND)&&(_dwc_plug.state != PLUGSTATE_B_SUSPEND))
		return 0;

	dwc3_owl_plug_resume();

	ret = dwc3_core_init(dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to initialize core\n");
	}

	dwc3_event_buffers_setup(dwc);

	if(_dwc_plug.state == PLUGSTATE_A_SUSPEND) {
		pr_info("%s %d A_RESUME\n", __func__, __LINE__);
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_HOST);
		_dwc_plug.state = PLUGSTATE_A_IN;
	}
	else if(_dwc_plug.state == PLUGSTATE_B_SUSPEND) {
		pr_info("%s %d B_RESUME\n", __func__, __LINE__);
		owl_dwc3_usb2phy_param_setup(1);
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_DEVICE);
		dwc3_plug_gadget_resume(&_dwc_plug);
		_dwc_plug.state = PLUGSTATE_B_IN;
	}

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	pr_info("%s end\n", __func__);
	
	return 0;
}

static const struct dev_pm_ops dwc3_plug_dev_pm_ops = {
	.prepare	= dwc3_plug_prepare,
	.complete	= dwc3_plug_complete,

	SET_SYSTEM_SLEEP_PM_OPS(dwc3_plug_suspend, dwc3_plug_resume)
};

#define DWC3_PM_OPS	&(dwc3_plug_dev_pm_ops)
#endif

void dwc3_ss_lfps_init(void)
{
	struct dwc3 *dwc = _dwc_plug.dwc;
	u32 reg;

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	reg &= ~(1<<8);
	dwc3_writel(dwc->regs, DWC3_GCTL, reg);

	reg = dwc3_readl(dwc->regs, DWC3_GUSB3PIPECTL(0));
	reg &= ~(1<<9);
	reg |= (1<<12);
	dwc3_writel(dwc->regs, DWC3_GUSB3PIPECTL(0), reg);

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	reg &= ~(3<<12);
	reg |= (1<<12);
	dwc3_writel(dwc->regs, DWC3_GCTL, reg);
}
EXPORT_SYMBOL_GPL(dwc3_ss_lfps_init);

void dwc3_ss_phy_softreset(void)
{
	struct dwc3 *dwc = _dwc_plug.dwc;
	u32			reg;

	/* Assert USB3 PHY reset */
	reg = dwc3_readl(dwc->regs, DWC3_GUSB3PIPECTL(0));
	reg |= DWC3_GUSB3PIPECTL_PHYSOFTRST;
	dwc3_writel(dwc->regs, DWC3_GUSB3PIPECTL(0), reg);

	mdelay(100);

	/* Clear USB3 PHY reset */
	reg = dwc3_readl(dwc->regs, DWC3_GUSB3PIPECTL(0));
	reg &= ~DWC3_GUSB3PIPECTL_PHYSOFTRST;
	dwc3_writel(dwc->regs, DWC3_GUSB3PIPECTL(0), reg);
}
EXPORT_SYMBOL_GPL(dwc3_ss_phy_softreset);

void dwc3_ss_config_GUCTL(void)
{
	struct dwc3 *dwc = _dwc_plug.dwc;
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_GUCTL);
	/* fix asix usbnet dongle retry hangup */
	reg |= (1<<14);
	/* fix some udisk control transfer too slow,set config fail */
	reg |= (1<<17);
	dwc3_writel(dwc->regs, DWC3_GUCTL, reg);
}
EXPORT_SYMBOL_GPL(dwc3_ss_config_GUCTL);

void dwc3_ss_select_port2(void)
{
	struct dwc3 *dwc = _dwc_plug.dwc;
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_GDBGFIFOSPACE);
	reg &= ~(0xff);
	reg |= 0x2;
	dwc3_writel(dwc->regs, DWC3_GDBGFIFOSPACE, reg);
}
EXPORT_SYMBOL_GPL(dwc3_ss_select_port2);

void dwc3_mask_supper_speed(void)
{
	maximum_speed = "high";
}
EXPORT_SYMBOL_GPL(dwc3_mask_supper_speed);
#endif

#define MASK_SUPPER_SPEED_FOR_ADFUS
#ifdef MASK_SUPPER_SPEED_FOR_ADFUS
static struct dwc3 *adfus_dwc;
static void dwc3_set_dwc_for_adfus(struct dwc3 *dwc)
{
	adfus_dwc = dwc;
}

void dwc3_mask_supper_speed_for_adfus(void)
{
	if (!adfus_dwc) {
		pr_debug("%s %d _dwc is NULL\n", __func__, __LINE__);
		return;
	}

	pr_debug("%s %d\n", __func__, __LINE__);
	adfus_dwc->maximum_speed = DWC3_DCFG_HIGHSPEED;
	adfus_dwc->gadget.max_speed		= USB_SPEED_HIGH;
}
EXPORT_SYMBOL_GPL(dwc3_mask_supper_speed_for_adfus);
#endif
