
#if SUPPORT_NOT_RMMOD_USBDRV

static void dwc3_gadget_usb2_phy_suspend(struct dwc3 *dwc, int suspend);
static void dwc3_gadget_usb3_phy_suspend(struct dwc3 *dwc, int suspend);
static void dwc3_disconnect_gadget(struct dwc3 *dwc);
static int dwc3_gadget_run_stop(struct dwc3 *dwc, int is_on);

static int __dwc3_gadget_ep_enable(struct dwc3_ep *dep,
		const struct usb_endpoint_descriptor *desc,
		const struct usb_ss_ep_comp_descriptor *comp_desc,
		bool ignore);
static int __dwc3_gadget_ep_disable(struct dwc3_ep *dep);

static struct usb_endpoint_descriptor dwc3_gadget_ep0_desc;

void dwc3_gadget_plug_usb2_phy_suspend(struct dwc3 *dwc, int suspend)
{
	dwc3_gadget_usb2_phy_suspend(dwc, suspend);
}

int dwc3_gadget_plugin_init(struct dwc3 *dwc)
{
	u32					reg;

	dwc->gadget.speed		= USB_SPEED_UNKNOWN;

	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg |= DWC3_DCFG_LPM_CAP;
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);

	/* Enable USB2 LPM and automatic phy suspend only on recent versions */
	if (dwc->revision >= DWC3_REVISION_194A) {
		dwc3_gadget_usb2_phy_suspend(dwc, false);
		dwc3_gadget_usb3_phy_suspend(dwc, false);
	}

	return 0;
}

void dwc3_gadget_plug_disconnect(struct dwc3 *dwc)
{
	unsigned long		flags;

	dev_vdbg(dwc->dev, "%s\n", __func__);

	spin_lock_irqsave(&dwc->lock, flags);

	dwc3_disconnect_gadget(dwc);

	dwc->start_config_issued = false;

	dwc->gadget.speed = USB_SPEED_UNKNOWN;
	dwc->setup_packet_pending = false;

	spin_unlock_irqrestore(&dwc->lock, flags);
}

static int gadget_is_plugin;
static int dwc3_gadget_is_plugin(void)
{
	return gadget_is_plugin;
}

void set_dwc3_gadget_plugin_flag(int flag)
{
	gadget_is_plugin = flag;
}
EXPORT_SYMBOL(set_dwc3_gadget_plugin_flag);

int dwc3_gadget_plug_pullup(struct usb_gadget *g, int is_on)
{
	struct dwc3		*dwc = gadget_to_dwc(g);
	int			ret;

	is_on = !!is_on;

	ret = dwc3_gadget_run_stop(dwc, is_on);

	return ret;
}

int dwc3_gadget_plug_resume(struct dwc3 *dwc)
{
	struct dwc3_ep		*dep;
	int			ret = 0;
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg &= ~(DWC3_DCFG_SPEED_MASK);

	/**
	 * WORKAROUND: DWC3 revision < 2.20a have an issue
	 * which would cause metastability state on Run/Stop
	 * bit if we try to force the IP to USB2-only mode.
	 *
	 * Because of that, we cannot configure the IP to any
	 * speed other than the SuperSpeed
	 *
	 * Refers to:
	 *
	 * STAR#9000525659: Clock Domain Crossing on DCTL in
	 * USB 2.0 Mode
	 */
	if (dwc->revision < DWC3_REVISION_220A)
		reg |= DWC3_DCFG_SUPERSPEED;
	else
		reg |= dwc->maximum_speed;
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);

	dwc->start_config_issued = false;

	/* Start with SuperSpeed Default */
	dwc3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(512);

	dep = dwc->eps[0];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc, NULL, false);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		goto err0;
	}

	dep = dwc->eps[1];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc, NULL, false);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		goto err1;
	}

	/* begin to receive SETUP packets */
	dwc->ep0state = EP0_SETUP_PHASE;
	dwc3_ep0_out_start(dwc);

	return 0;

err1:
	__dwc3_gadget_ep_disable(dwc->eps[0]);
err0:
	return ret;
}


#endif
