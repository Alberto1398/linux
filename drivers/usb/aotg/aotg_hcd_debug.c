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
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>
#include <linux/timer.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/gpio.h>

#include "aotg_hcd.h"
#include "aotg_regs.h"
#include "aotg_plat_data.h"
#include "aotg_hcd_debug.h"
#include "aotg.h"


char aotg_hcd_proc_sign = 'n';
unsigned int aotg_trace_onff;

void aotg_dbg_proc_output_ep(void)
{
	return;
}

void aotg_dbg_output_info(void)
{
	return;
}

void aotg_dbg_put_q(struct aotg_queue *q, unsigned int num,
	unsigned int type, unsigned int len)
{
	return;
}

void aotg_dbg_finish_q(struct aotg_queue *q)
{
	return;
}

void aotg_dump_ep_reg(struct aotg_hcd *acthcd, int ep_index, int is_out)
{
	int index_multi = ep_index - 1;
	if (NULL == acthcd) {
		ACT_HCD_ERR
		return;
	}

	if (ep_index > 15) {
		pr_info("ep_index : %d too big, err!\n", ep_index);
		return;
	}

	pr_info("=== dump hc-%s ep%d reg info ===\n",
		is_out ? "out" : "in", ep_index);

	if (ep_index == 0) {
		pr_info(" HCIN0BC(0x%p) : 0x%X\n",
			acthcd->base + HCIN0BC, usb_readb(acthcd->base + HCIN0BC));
		pr_info(" EP0CS(0x%p) : 0x%X\n",
			acthcd->base + EP0CS, usb_readb(acthcd->base + EP0CS));
		pr_info(" HCOUT0BC(0x%p) : 0x%X\n",
			acthcd->base + HCOUT0BC, usb_readb(acthcd->base + HCOUT0BC));
		pr_info(" HCEP0CTRL(0x%p) : 0x%X\n",
			acthcd->base + HCEP0CTRL, usb_readb(acthcd->base + HCEP0CTRL));
		pr_info(" HCIN0ERR(0x%p) : 0x%X\n",
				acthcd->base + HCIN0ERR, usb_readb(acthcd->base + HCIN0ERR));
		return;
	}

	if (is_out) {
		pr_info(" HCOUT%dBC(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1BC + index_multi * 0x8,
			usb_readw(acthcd->base + HCOUT1BC + index_multi * 0x8));
		pr_info(" HCOUT%dCON(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1CON + index_multi * 0x8,
			usb_readb(acthcd->base + HCOUT1CON + index_multi * 0x8));
		pr_info(" HCOUT%dCS(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1CS + index_multi * 0x8,
			usb_readb(acthcd->base + HCOUT1CS + index_multi * 0x8));
		pr_info(" HCOUT%dCTRL(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1CTRL + index_multi * 0x4,
			usb_readb(acthcd->base + HCOUT1CTRL + index_multi * 0x4));
		pr_info(" HCOUT%dERR(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1ERR + index_multi * 0x4,
			usb_readb(acthcd->base + HCOUT1ERR + index_multi * 0x4));
		pr_info(" HCOUT%dSTADDR(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1STADDR + index_multi * 0x4,
			usb_readl(acthcd->base + HCOUT1STADDR + index_multi * 0x4));
		pr_info(" HCOUT%dMAXPCK(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1MAXPCK + index_multi * 0x2,
			usb_readw(acthcd->base + HCOUT1MAXPCK + index_multi * 0x2));

		pr_info(" HCOUT%dDMASTADDR(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1DMASTADDR + index_multi * 0x8,
			usb_readl(acthcd->base + HCOUT1DMASTADDR + index_multi * 0x8));
		pr_info(" HCOUT%dDMACOUNTER(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCOUT1DMACOUNTER + index_multi * 0x8,
			usb_readl(acthcd->base + HCOUT1DMACOUNTER + index_multi * 0x8));
	} else {
		pr_info(" HCIN%dBC(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1BC + index_multi * 0x8,
			usb_readw(acthcd->base + HCIN1BC + index_multi * 0x8));
		pr_info(" HCIN%dCON(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1CON + index_multi * 0x8,
			usb_readb(acthcd->base + HCIN1CON + index_multi * 0x8));
		pr_info(" HCIN%dCS(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1CS + index_multi * 0x8,
			usb_readb(acthcd->base + HCIN1CS + index_multi * 0x8));
		pr_info(" HCIN%dCS(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1CTRL + index_multi * 0x4,
			usb_readb(acthcd->base + HCIN1CTRL + index_multi * 0x4));
		pr_info(" HCIN%dERR(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1ERR + index_multi * 0x4,
			usb_readb(acthcd->base + HCIN1ERR + index_multi * 0x4));
		pr_info(" HCIN%dSTADDR(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1STADDR + index_multi * 0x4,
			usb_readl(acthcd->base + HCIN1STADDR + index_multi * 0x4));
		pr_info(" HCIN%dMAXPCK(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1MAXPCK + index_multi * 0x2,
			usb_readw(acthcd->base + HCIN1MAXPCK + index_multi * 0x2));

		pr_info(" HCIN%dDMASTADDR(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1DMASTADDR + index_multi * 0x8,
			usb_readl(acthcd->base + HCIN1DMASTADDR + index_multi * 0x8));
		pr_info(" HCIN%dDMACOUNTER(0x%p) : 0x%X\n", ep_index,
			acthcd->base + HCIN1DMACOUNTER + index_multi * 0x8,
			usb_readl(acthcd->base + HCIN1DMACOUNTER + index_multi * 0x8));
	}
}

#ifdef AOTG_REG_DUMP
void aotg_dbg_regs(struct aotg_hcd *acthcd)
{
	struct aotg_plat_data *data = acthcd->port_specific;
#if 1
	int i = 0;

	do {
		pr_info(" USB reg(0x%p):0x%X  ", acthcd->base + i,
			usb_readl(acthcd->base + i));
		i += 4;
		pr_info(":0x%X ", usb_readl(acthcd->base + i));
		i += 4;
		pr_info(":0x%X ", usb_readl(acthcd->base + i));
		i += 4;
		pr_info(":0x%X ", usb_readl(acthcd->base + i));
		i += 4;
		pr_info("\n");
	} while (i < 0x600);
#endif
	dev_info(acthcd->dev, "============== aotg regs ==================\n");

	pr_info("usbecs:0x%X ", usb_readl(data->usbecs));
#if 1
	pr_info(" USBEIRQ(0x%p) : 0x%X\n",
		acthcd->base + USBEIRQ, usb_readb(acthcd->base + USBEIRQ));
	pr_info(" USBEIEN(0x%p) : 0x%X\n",
		acthcd->base + USBEIEN, usb_readb(acthcd->base + USBEIEN));
	pr_info(" SRPCTRL(0x%p) : 0x%X\n",
		acthcd->base + SRPCTRL, usb_readb(acthcd->base + SRPCTRL));

	pr_info("HCINxSHORTPCKIRQ0(0x%p) : 0x%X\n",
		acthcd->base + HCINxSHORTPCKIRQ0 ,
		usb_readb(acthcd->base + HCINxSHORTPCKIRQ0));
	pr_info("HCINxSHORTPCKIRQ1 (0x%p) : 0x%X\n",
		acthcd->base + HCINxSHORTPCKIRQ1 ,
		usb_readb(acthcd->base + HCINxSHORTPCKIRQ1));
	pr_info("HCINxSHORTPCKIEN0 (0x%p) : 0x%X\n",
		acthcd->base + HCINxSHORTPCKIEN0 ,
		usb_readb(acthcd->base + HCINxSHORTPCKIEN0));
	pr_info("HCINxSHORTPCKIEN1 (0x%p) : 0x%X\n",
		acthcd->base + HCINxSHORTPCKIEN1 ,
		usb_readb(acthcd->base + HCINxSHORTPCKIEN1));

	pr_info("HCINxERRIRQ0(0x%p) : 0x%X\n",
		acthcd->base + HCINxERRIRQ0, usb_readw(acthcd->base + HCINxERRIRQ0));

	pr_info(" OTGIRQ(0x%p) : 0x%X\n",
		acthcd->base + OTGIRQ, usb_readb(acthcd->base + OTGIRQ));
	pr_info(" OTGSTATE(0x%p) : 0x%X\n",
		acthcd->base + OTGSTATE, usb_readb(acthcd->base + OTGSTATE));
	pr_info(" OTGCTRL(0x%p) : 0x%X\n",
		acthcd->base + OTGCTRL, usb_readb(acthcd->base + OTGCTRL));
	pr_info(" OTGSTATUS(0x%p) : 0x%X\n",
		acthcd->base + OTGSTATUS, usb_readb(acthcd->base + OTGSTATUS));
	pr_info(" OTGIEN(0x%p) : 0x%X\n",
		acthcd->base + OTGIEN, usb_readb(acthcd->base + OTGIEN));
	pr_info("\n");
	pr_info(" BKDOOR(0x%p) : 0x%X\n",
		acthcd->base + BKDOOR, usb_readb(acthcd->base + BKDOOR));
	pr_info(" USBIRQ(0x%p) : 0x%X\n",
		acthcd->base + USBIRQ, usb_readb(acthcd->base + USBIRQ));
	pr_info(" USBIEN(0x%p) : 0x%X\n",
		acthcd->base + USBIEN, usb_readb(acthcd->base + USBIEN));
	pr_info("\n");
#endif

	pr_info("HCINxPNGIEN0:%x\n", (u32)usb_readb(acthcd->base + HCINxPNGIEN0));
	pr_info(" HCIN1DMACOUNTER(0x%p) : 0x%X\n",
		acthcd->base + HCIN1DMACOUNTER, usb_readb(acthcd->base + HCIN1DMACOUNTER));
	pr_info(" HCIN2DMASTADDR(0x%p) : 0x%X\n",
		acthcd->base + HCIN2DMASTADDR, usb_readb(acthcd->base + HCIN2DMASTADDR));

	pr_info(" HCOUTxIRQ0(0x%p) : 0x%X\n",
		acthcd->base + HCOUTxIRQ0, usb_readb(acthcd->base + HCOUTxIRQ0));
	pr_info(" HCOUTxIEN0(0x%p) : 0x%X\n",
		acthcd->base + HCOUTxIEN0, usb_readb(acthcd->base + HCOUTxIEN0));
	pr_info(" HCINxIRQ0(0x%p) : 0x%X\n",
		acthcd->base + HCINxIRQ0, usb_readb(acthcd->base + HCINxIRQ0));
	pr_info(" HCINxIEN0(0x%p) : 0x%X\n",
		acthcd->base + HCINxIEN0, usb_readb(acthcd->base + HCINxIEN0));
	pr_info("\n");
#if 1
	pr_info(" HCIN0BC(0x%p) : 0x%X\n",
		acthcd->base + HCIN0BC, usb_readb(acthcd->base + HCIN0BC));
	pr_info(" EP0CS(0x%p) : 0x%X\n",
		acthcd->base + EP0CS, usb_readb(acthcd->base + EP0CS));
	pr_info(" HCOUT0BC(0x%p) : 0x%X\n",
		acthcd->base + HCOUT0BC, usb_readb(acthcd->base + HCOUT0BC));
	pr_info(" HCEP0CTRL(0x%p) : 0x%X\n",
		acthcd->base + HCEP0CTRL, usb_readb(acthcd->base + HCEP0CTRL));
	pr_info("\n");
	pr_info(" HCIN1BC(0x%p) : 0x%X\n",
		acthcd->base + HCIN1BCL, usb_readw(acthcd->base + HCIN1BCL));
	pr_info(" HCIN1CON(0x%p) : 0x%X\n",
		acthcd->base + HCIN1CON, usb_readb(acthcd->base + HCIN1CON));
	pr_info(" HCIN1CS(0x%p) : 0x%X\n",
		acthcd->base + HCIN1CS, usb_readb(acthcd->base + HCIN1CS));
	pr_info(" HCIN1CTRL(0x%p) : 0x%X\n",
		acthcd->base + HCIN1CTRL, usb_readb(acthcd->base + HCIN1CTRL));
	pr_info(" HCIN2BC(0x%p) : 0x%X\n",
		acthcd->base + HCIN2BCL, usb_readw(acthcd->base + HCIN2BCL));
	pr_info(" HCIN2CON(0x%p) : 0x%X\n",
		acthcd->base + HCIN2CON, usb_readb(acthcd->base + HCIN2CON));
	pr_info(" HCIN2CS(0x%p) : 0x%X\n",
		acthcd->base + HCIN2CS, usb_readb(acthcd->base + HCIN2CS));
	pr_info(" HCIN2CTRL(0x%p) : 0x%X\n",
		acthcd->base + HCIN2CTRL, usb_readb(acthcd->base + HCIN2CTRL));
	pr_info("\n");
	pr_info(" HCIN4DMASTADDR(0x%p) : 0x%X\n",
		acthcd->base + HCIN4DMASTADDR, usb_readw(acthcd->base + HCIN4DMASTADDR));
	pr_info(" HCIN4DMACOUNTER(0x%p) : 0x%X\n",
		acthcd->base + HCIN4DMACOUNTER, usb_readw(acthcd->base + HCIN4DMACOUNTER));
	pr_info("\n");
#endif
	pr_info(" HCOUT1BC(0x%p) : 0x%X\n",
		acthcd->base + HCOUT1BCL, usb_readw(acthcd->base + HCOUT1BCL));
	pr_info(" HCOUT1CON(0x%p) : 0x%X\n",
		acthcd->base + HCOUT1CON, usb_readb(acthcd->base + HCOUT1CON));
	pr_info(" HCOUT1CS(0x%p) : 0x%X\n",
		acthcd->base + HCOUT1CS, usb_readb(acthcd->base + HCOUT1CS));
	pr_info(" HCOUT1CTRL(0x%p) : 0x%X\n",
		acthcd->base + HCOUT1CTRL, usb_readb(acthcd->base + HCOUT1CTRL));
	pr_info(" HCOUT2BC(0x%p) : 0x%X\n",
		acthcd->base + HCOUT2BCL, usb_readw(acthcd->base + HCOUT2BCL));
	pr_info(" HCOUT2CON(0x%p) : 0x%X\n",
		acthcd->base + HCOUT2CON, usb_readb(acthcd->base + HCOUT2CON));
	pr_info(" HCOUT2CS(0x%p) : 0x%X\n",
		acthcd->base + HCOUT2CS, usb_readb(acthcd->base + HCOUT2CS));
	pr_info(" HCOUT2CTRL(0x%p) : 0x%X\n",
		acthcd->base + HCOUT2CTRL, usb_readb(acthcd->base + HCOUT2CTRL));
	pr_info("\n");
	pr_info("\n");
	return;
}

#else	/* AOTG_REG_DUMP */

void aotg_dbg_regs(struct aotg_hcd *acthcd)
{
	/* fpga5209 dump */
	pr_info("dump gl5209 reg\n");

/*
	pr_info("CMU_USBPLL(0x%p) : 0x%X\n",
		acthcd->base + CMU_USBPLL, usb_readl(acthcd->base + CMU_USBPLL));
	pr_info("CMU_DEVRST1(0x%p) : 0x%X\n",
		acthcd->base + CMU_DEVRST1, usb_readl(acthcd->base + CMU_DEVRST1));
	pr_info("HCDMABCKDOOR(0x%p) : 0x%X\n",
		acthcd->base + HCDMABCKDOOR, usb_readl(acthcd->base + HCDMABCKDOOR));
	pr_info("USBH_0ECS(0x%p) : 0x%X\n",
		acthcd->base + USBH_0ECS, usb_readl(acthcd->base + USBH_0ECS));
*/
	pr_info(" USBEIEN(0x%p) : 0x%X\n",
		acthcd->base + USBEIEN, usb_readb(acthcd->base + USBEIEN));
	pr_info(" OTGIRQ(0x%p) : 0x%X\n",
		acthcd->base + OTGIRQ, usb_readb(acthcd->base + OTGIRQ));
	pr_info(" OTGSTATE(0x%p) : 0x%X\n",
		acthcd->base + OTGSTATE, usb_readb(acthcd->base + OTGSTATE));
	pr_info(" OTGCTRL(0x%p) : 0x%X\n",
		acthcd->base + OTGCTRL, usb_readb(acthcd->base + OTGCTRL));
	pr_info(" OTGSTATUS(0x%p) : 0x%X\n",
		acthcd->base + OTGSTATUS, usb_readb(acthcd->base + OTGSTATUS));
	pr_info(" OTGIEN(0x%p) : 0x%X\n",
		acthcd->base + OTGIEN, usb_readb(acthcd->base + OTGIEN));

	return;
}

#endif	/* AOTG_REG_DUMP */


#ifdef AOTG_DEBUG_FILE

int aotg_dbg_proc_output_ep_state1(struct aotg_hcd *acthcd)
{
	struct aotg_hcep *tmp_ep;
	int i;
	struct aotg_queue *q, *next;
	struct aotg_hcep *ep;
	struct urb *urb;

	ep = acthcd->active_ep0;
	if (ep) {
		pr_info("------------- active ep0 queue:\n");
		pr_info("urb_enque_cnt:%d\n", ep->urb_enque_cnt);
		pr_info("urb_endque_cnt:%d\n", ep->urb_endque_cnt);
		pr_info("urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
		pr_info("urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);

		if (ep->q != NULL) {
			q = ep->q;
			pr_info("dma[0]: ep->index: %d, type: %d, dir : %s, transfer_buffer_length: %d, actual_length:%d\n",
				q->ep->index,
				usb_pipetype(q->urb->pipe), usb_pipeout(q->urb->pipe) ? "out" : "in",
				q->urb->transfer_buffer_length, q->urb->actual_length);
		}
	}

	for (i = 0; i < MAX_EP_NUM; i++) {
		ep = acthcd->ep0[i];
		if (ep) {
			pr_info("------------- ep0 list index:%d queue:\n", i);
			pr_info("urb_enque_cnt:%d\n", ep->urb_enque_cnt);
			pr_info("urb_endque_cnt:%d\n", ep->urb_endque_cnt);
			pr_info("urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
			pr_info("urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);
			pr_info("ep->epnum:%d\n", ep->epnum);

			if (ep->q != NULL) {
				q = ep->q;
				pr_info("ep->index: %d, type: %d, dir : %s, transfer_buffer_length: %d, actual_length:%d\n",
					q->ep->index,
					usb_pipetype(q->urb->pipe), usb_pipeout(q->urb->pipe) ? "out" : "in",
					q->urb->transfer_buffer_length, q->urb->actual_length);
			}
		}
	}

	for (i = 1; i < MAX_EP_NUM; i++) {
		tmp_ep = acthcd->inep[i];
		if (tmp_ep) {
			/*if (tmp_ep->urb_enque_cnt > (tmp_ep->urb_endque_cnt + tmp_ep->urb_stop_stran_cnt))*/
			{
				pr_info("inep:%d\n", i);
				pr_info("urb_enque_cnt:%d\n", tmp_ep->urb_enque_cnt);
				pr_info("urb_endque_cnt:%d\n", tmp_ep->urb_endque_cnt);
				pr_info("urb_stop_stran_cnt:%d\n", tmp_ep->urb_stop_stran_cnt);
				pr_info("urb_unlinked_cnt:%d\n", tmp_ep->urb_unlinked_cnt);

				pr_info("index:%d\n", tmp_ep->index);
				pr_info("maxpacket:%d\n", tmp_ep->maxpacket);
				pr_info("epnum:%d\n", tmp_ep->epnum);
			}
		}
	}
	for (i = 1; i < MAX_EP_NUM; i++) {
		tmp_ep = acthcd->outep[i];
		if (tmp_ep) {
			/*if (tmp_ep->urb_enque_cnt > (tmp_ep->urb_endque_cnt + tmp_ep->urb_stop_stran_cnt))*/
			{
				pr_info("outep:%d\n", i);
				pr_info("urb_enque_cnt:%d\n", tmp_ep->urb_enque_cnt);
				pr_info("urb_endque_cnt:%d\n", tmp_ep->urb_endque_cnt);
				pr_info("urb_stop_stran_cnt:%d\n", tmp_ep->urb_stop_stran_cnt);
				pr_info("urb_unlinked_cnt:%d\n", tmp_ep->urb_unlinked_cnt);

				pr_info("index:%d\n", tmp_ep->index);
				pr_info("maxpacket:%d\n", tmp_ep->maxpacket);
				pr_info("epnum:%d\n", tmp_ep->epnum);
			}
		}
	}

	pr_info("in hcd enqueue list:\n");
	list_for_each_entry_safe(q, next, &acthcd->hcd_enqueue_list, enqueue_list) {
		urb = q->urb;
		ep = q->ep;
		pr_info("ep->epnum:%d ", ep->epnum);
		pr_info("urb->transfer_buffer_length:%d ", urb->transfer_buffer_length);
		pr_info("usb_pipein(urb->pipe):%x\n", usb_pipein(urb->pipe));
		pr_info("usb_pipetype(urb->pipe):%x\n", usb_pipetype(urb->pipe));
	}
	return 0;
}

int aotg_dbg_proc_output_ep_state(struct aotg_hcd *acthcd)
{
	struct aotg_queue *q, *next;
	struct aotg_hcep *ep;
	struct urb *urb;
	int i = 0;

	list_for_each_entry_safe(q, next, &acthcd->hcd_enqueue_list, enqueue_list) {
		urb = q->urb;
		ep = q->ep;
		i++;
	}

	if (i > 2)
		pr_info("error, more enque.\n");

	if (i <= 1)
		i = 0;

	aotg_dbg_proc_output_ep_state1(acthcd);
	return i;
}

int aotg_dump_regs(struct aotg_hcd *acthcd)
{
	int i;
	struct aotg_hcep *ep;

	for (i = 0; i < MAX_EP_NUM; i++) {
		ep = acthcd->inep[i];
		if (ep)
			aotg_dump_linklist_reg_2(acthcd, ep->mask);
	}

	for (i = 0; i < MAX_EP_NUM; i++) {
		ep = acthcd->outep[i];
		if (ep)
			aotg_dump_linklist_reg_2(acthcd, ep->mask);
	}

	return 0;

}

void __dump_ring_info(struct aotg_hcep *ep)
{
	int i;
	struct aotg_ring *ring = NULL;
	struct aotg_td *td, *next;
	struct urb *urb;
	struct aotg_trb *trb;

	if (!ep)
		return;
	pr_info("\n------------- current %s ep%d ring :\n",
		ep->is_out ? "OUT" : "IN", ep->index);
	pr_info("urb_enque_cnt:%d\n", ep->urb_enque_cnt);
	pr_info("urb_endque_cnt:%d\n", ep->urb_endque_cnt);
	pr_info("urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
	pr_info("urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);
	pr_info("ep_num:%d\n", ep->epnum);
	pr_info("ep_type:%d\n", ep->type);

	ring = ep->ring;

	i = 0;
	if (ring) {
		trb = ring->first_trb;
		while (trb <= ring->last_trb) {
			pr_info("%d hw_buf_ptr:%x,hw_buf_len:%x,hw_buf_remain:%x,hw_token:%x\n", i,
				trb->hw_buf_ptr, trb->hw_buf_len, trb->hw_buf_remain, trb->hw_token);
			trb++;
			i++;
		}
	}

	if (ring) {
		pr_info("-----\n");
		pr_info("enring_cnt:%d\n", ring->enring_cnt);
		pr_info("dering_cnt:%d\n", ring->dering_cnt);
		pr_info("num_trbs_free:%d\n", (u32)atomic_read(&ring->num_trbs_free));
		pr_info("first_trb:0x%p, dma:0x%x\n", ring->first_trb,
			ring_trb_virt_to_dma(ring, ring->first_trb));
		pr_info("last_trb:0x%p, dma:0x%x\n", ring->last_trb,
			ring_trb_virt_to_dma(ring, ring->last_trb));
		pr_info("ring_enqueue:0x%p(%d)\n", ring->enqueue_trb,
			(int)(ring->enqueue_trb - ring->first_trb));
		pr_info("ring_dequeue:0x%p(%d)\n", ring->dequeue_trb,
			(int)(ring->dequeue_trb - ring->first_trb));
		pr_info("reg_linkaddr(0x%p):0x%x\n", ring->reg_dmalinkaddr,
			usb_readl(ring->reg_dmalinkaddr));
		pr_info("reg_curradr(0x%p):0x%x\n", ring->reg_curaddr,
			usb_readl(ring->reg_curaddr));
		pr_info("reg_dmactrl(0x%p):0x%x\n", ring->reg_dmactrl,
			usb_readl(ring->reg_dmactrl));

		pr_info("in eq_enqueue_td list:\n");
		i = 0;
		list_for_each_entry_safe(td, next, &ep->queue_td_list, queue_list) {
			urb = td->urb;
			i++;
			pr_info("-----\n");
			pr_info("urb->transfer_buffer_length:%d\n", urb->transfer_buffer_length);
			pr_info("usb_pipein(urb->pipe):%x\n", usb_pipein(urb->pipe));
			pr_info("usb_pipetype(urb->pipe):%x\n", usb_pipetype(urb->pipe));
		}
		if (i) {
			i = 0;
			pr_info("======td in queue num : %d\n", i);
		}

		pr_info("in eq_enring_td list:\n");
		list_for_each_entry_safe(td, next, &ep->enring_td_list, enring_list) {
			pr_info("-----\n");
			i++;
			trb = td->trb_vaddr;
			if (td->urb)
				pr_info("urb:%p\n", td->urb);
			pr_info("hw_buf_ptr:%x,hw_buf_len:%x,hw_buf_remain:%x,hw_token:%x\n", \
				trb->hw_buf_ptr, trb->hw_buf_len, trb->hw_buf_remain, trb->hw_token);
			pr_info("num_trbs:%d\n", td->num_trbs);
		}
		if (i)
			pr_info("======td in ring num : %d\n", i);
	}

/*
	seq_printf(s, "------------- current IN ep%d ring :\n", i);
	seq_printf(s, "urb_enque_cnt:%d\n", ep->urb_enque_cnt);
	seq_printf(s, "urb_endque_cnt:%d\n", ep->urb_endque_cnt);
	seq_printf(s, "urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
	seq_printf(s, "urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);
	seq_printf(s, "ep->epnum:%d\n", ep->epnum);

	ring = ep->ring;
	if (ring) {
		seq_printf(s, "enring_cnt:%d\n", ring->enring_cnt);
		seq_printf(s, "dering_cnt:%d\n", ring->dering_cnt);
		seq_printf(s, "num_trbs_free:%d\n", ring->num_trbs_free);
		seq_printf(s, "ring_enqueue_ptr:0x%x", ring->enqueue_trb);
		seq_printf(s, "ring_dequeue_ptr:0x%x", ring->dequeue_trb);

		seq_printf(s, "in eq_enqueue_td list:\n");
		list_for_each_entry_safe(td, next, &ep->queue_td_list, queue_td_list) {
			urb = td->urb;
			seq_printf(s, "urb->transfer_buffer_length:%d", urb->transfer_buffer_length);
			seq_printf(s, "usb_pipein(urb->pipe):%x\n", usb_pipein(urb->pipe));
			seq_printf(s, "usb_pipetype(urb->pipe):%x\n", usb_pipetype(urb->pipe));
		}

		seq_printf(s, "in eq_enring_td list:\n");
		list_for_each_entry_safe(td, next, &ep->enring_td_list, enring_list) {
			seq_printf(s, "num_trbs:%d\n", td->num_trbs);
			seq_printf(s, "trb_vaddr:0x%x\n", td->trb_vaddr);
			seq_printf(s, "trb_dma:0x%x\n", td->trb_dma);
			seq_printf(s, "cross_ring:0x%d\n", td->cross_ring);
		}
	}
*/
}

static int aotg_hcd_show_ring_info(struct aotg_hcd *acthcd)
{
	int i;
	struct aotg_hcep *ep;
	struct aotg_queue *q;

	ep = acthcd->active_ep0;
	if (ep) {
		pr_info("------------- active ep0 queue: \n");
		pr_info("urb_enque_cnt:%d\n", ep->urb_enque_cnt);
		pr_info("urb_endque_cnt:%d\n", ep->urb_endque_cnt);
		pr_info("urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
		pr_info("urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);

		if (ep->q != NULL) {
			q = ep->q;
			pr_info("dma[0]: ep->index: %d, type: %d, dir : %s, transfer_buffer_length: %d, actual_length:%d\n",
				q->ep->index,
				usb_pipetype(q->urb->pipe), usb_pipeout(q->urb->pipe) ? "out" : "in",
				q->urb->transfer_buffer_length, q->urb->actual_length);
		}
	}

	for (i = 0; i < MAX_EP_NUM; i++) {
		ep = acthcd->inep[i];
		__dump_ring_info(ep);
	}

	for (i = 0; i < MAX_EP_NUM; i++) {
		ep = acthcd->outep[i];
		__dump_ring_info(ep);
	}

	return 0;
}
#if 0
static int aotg_hcd_show_enque_info(struct seq_file *s, struct aotg_hcd	*acthcd)
{
	int i;
	struct aotg_queue *q, *next;
	struct aotg_hcep *ep;

	for (i = 0; i < AOTG_QUEUE_POOL_CNT; i++) {
		if (acthcd->queue_pool[i] != NULL) {
			seq_printf(s, "queue_pool[%d]->in_using: %d\n",
				i, acthcd->queue_pool[i]->in_using);
		}
	}

	seq_printf(s, "current dma queue:\n");

	ep = acthcd->active_ep0;
	if (ep) {
		seq_printf(s, "------------- active ep0 queue:\n");
		seq_printf(s, "urb_enque_cnt:%d\n", ep->urb_enque_cnt);
		seq_printf(s, "urb_endque_cnt:%d\n", ep->urb_endque_cnt);
		seq_printf(s, "urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
		seq_printf(s, "urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);

		if (ep->q != NULL) {
			q = ep->q;
			seq_printf(s, "dma[0]: ep->index: %d, type: %d, dir : %s, transfer_buffer_length: %d, actual_length:%d\n",
				q->ep->index,
				usb_pipetype(q->urb->pipe), usb_pipeout(q->urb->pipe) ? "out" : "in",
				q->urb->transfer_buffer_length, q->urb->actual_length);
		}
	}

	for (i = 0; i < MAX_EP_NUM; i++) {
		ep = acthcd->ep0[i];
		if (ep) {
			seq_printf(s, "------------- ep0 list index:%d queue:\n", i);
			seq_printf(s, "urb_enque_cnt:%d\n", ep->urb_enque_cnt);
			seq_printf(s, "urb_endque_cnt:%d\n", ep->urb_endque_cnt);
			seq_printf(s, "urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
			seq_printf(s, "urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);
			seq_printf(s, "ep->epnum:%d\n", ep->epnum);

			if (ep->q != NULL) {
				q = ep->q;
				seq_printf(s, "ep->index: %d, type: %d, dir : %s, transfer_buffer_length: %d, actual_length:%d\n",
					q->ep->index,
					usb_pipetype(q->urb->pipe), usb_pipeout(q->urb->pipe) ? "out" : "in",
					q->urb->transfer_buffer_length, q->urb->actual_length);
			}
		}
	}

	for (i = 1; i < MAX_EP_NUM; i++) {
		ep = acthcd->inep[i];
		if (ep) {
			seq_printf(s, "------------- current IN ep%d queue:\n", i);
			seq_printf(s, "urb_enque_cnt:%d\n", ep->urb_enque_cnt);
			seq_printf(s, "urb_endque_cnt:%d\n", ep->urb_endque_cnt);
			seq_printf(s, "urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
			seq_printf(s, "urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);
			seq_printf(s, "ep->epnum:%d\n", ep->epnum);

			if (ep->q != NULL) {
				q = ep->q;
				seq_printf(s, "ep->index: %d, type: %d, dir : %s, transfer_buffer_length: %d, actual_length:%d\n",
					q->ep->index,
					usb_pipetype(q->urb->pipe), usb_pipeout(q->urb->pipe) ? "out" : "in",
					q->urb->transfer_buffer_length, q->urb->actual_length);
			}
		}
	}

	for (i = 1; i < MAX_EP_NUM; i++) {
		ep = acthcd->outep[i];
		if (ep) {
			seq_printf(s, "------------- current OUT ep%d queue:\n", i);
			seq_printf(s, "urb_enque_cnt:%d\n", ep->urb_enque_cnt);
			seq_printf(s, "urb_endque_cnt:%d\n", ep->urb_endque_cnt);
			seq_printf(s, "urb_stop_stran_cnt:%d\n", ep->urb_stop_stran_cnt);
			seq_printf(s, "urb_unlinked_cnt:%d\n", ep->urb_unlinked_cnt);
			seq_printf(s, "ep->epnum:%d\n", ep->epnum);

			if (ep->q != NULL) {
				q = ep->q;
				seq_printf(s, "ep->index: %d, type: %d, dir : %s, transfer_buffer_length: %d, actual_length:%d\n",
					q->ep->index,
					usb_pipetype(q->urb->pipe), usb_pipeout(q->urb->pipe) ? "out" : "in",
					q->urb->transfer_buffer_length, q->urb->actual_length);
			}
		}
	}

	seq_printf(s, "\n");
	seq_printf(s, "in hcd enqueue list:\n");
	list_for_each_entry_safe(q, next, &acthcd->hcd_enqueue_list, enqueue_list) {
		ep = q->ep;
		seq_printf(s, "ep->epnum:%d ", ep->epnum);
		seq_printf(s, "urb->transfer_buffer_length:%d ", q->urb->transfer_buffer_length);
		seq_printf(s, "usb_pipein(urb->pipe):%x\n", usb_pipein(q->urb->pipe));
		seq_printf(s, "usb_pipetype(urb->pipe):%x\n", usb_pipetype(q->urb->pipe));
	}
	return 0;
}
#endif
/*
 * echo a value to controll the cat /proc/aotg_hcd output content.
 * echo h>/proc/aotg_hcd.0 to see help info.
 */
ssize_t aotg_hcd_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char c = 'n';

	if (count) {
		if (get_user(c, buf))
			return -EFAULT;
		aotg_hcd_proc_sign = c;
	}
	if (c == 'h') {
		pr_info(" a ---- all.\n");
		pr_info(" b ---- backup info.\n");
		pr_info(" d ---- dma related.\n");
		pr_info(" e ---- enque and outque info.\n");
		pr_info(" f ---- trace in info.\n");
		pr_info(" h ---- help info.\n");
		pr_info(" n ---- normal.\n");
		pr_info(" r ---- register info.\n");
		pr_info(" s ---- aotg state.\n");
		pr_info(" t ---- trace out info.\n");
		pr_info(" z ---- stop stace.\n");
	}
	return count;
}

int aotg_hcd_proc_show(struct seq_file *s, void *unused)
{
	struct aotg_hcd	*acthcd = s->private;
	struct usb_hcd *hcd = aotg_to_hcd(acthcd);
	/*struct aotg_plat_data *data = acthcd->port_specific;*/

	if (aotg_hcd_proc_sign == 'd') {
		/* todo.*/
	}

	if (aotg_hcd_proc_sign == 's') {
		aotg_dbg_proc_output_ep_state(acthcd);
		seq_printf(s, "hcd state : 0x%08X\n", hcd->state);
	}

	if (aotg_hcd_proc_sign == 'r')
		aotg_dump_regs(acthcd);

	if (aotg_hcd_proc_sign == 'e')
		aotg_hcd_show_ring_info(acthcd);

	if (aotg_hcd_proc_sign == 'b') {
		aotg_dbg_proc_output_ep();
		aotg_dbg_output_info();
	}

	if (aotg_hcd_proc_sign == 'a') {
	}

	seq_printf(s, "\n");
	return 0;
}


static int aotg_hcd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, aotg_hcd_proc_show, PDE_DATA(inode));
}

static const struct file_operations proc_ops = {
	.open = aotg_hcd_proc_open,
	.read = seq_read,
	.write = aotg_hcd_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void create_debug_file(struct aotg_hcd *acthcd)
{
	struct device *dev = aotg_to_hcd(acthcd)->self.controller;

	acthcd->pde = proc_create_data(dev_name(dev), 0, NULL, &proc_ops, acthcd);
	return;
}

void remove_debug_file(struct aotg_hcd *acthcd)
{
	struct device *dev = aotg_to_hcd(acthcd)->self.controller;

	if (acthcd->pde)
		remove_proc_entry(dev_name(dev), NULL);
	return;
}

#else	/* AOTG_DEBUG_FILE */

void create_debug_file(struct aotg_hcd *acthcd)
{
	return;
}

void remove_debug_file(struct aotg_hcd *acthcd)
{
	return;
}

#endif	/* AOTG_DEBUG_FILE */


void aotg_print_xmit_cnt(char *info, int cnt)
{
	if (aotg_hcd_proc_sign == 'e')
		pr_info("%s cnt:%d\n", info, cnt);

	return;
}


static struct proc_dir_entry *acts_hub_pde;

int acts_hcd_proc_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "hcd_ports_en_ctrl: %d\n", hcd_ports_en_ctrl);
	return 0;
}

static int acts_hub_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, acts_hcd_proc_show, PDE_DATA(inode));
}

static ssize_t acts_hub_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char c = 'n';

	if (count) {
		if (get_user(c, buf))
			return -EFAULT;
	}
	if ((c >= '0') && (c <= '3')) {
		hcd_ports_en_ctrl = c - '0';
		pr_info("hcd_hub en:%d\n", hcd_ports_en_ctrl);
	}
	if (c == 'h') {
		pr_info(" num ---- 0-all enable, 1-usb0 enable, 2-usb1 enable, 3-reversed. \n");
		pr_info("o ---- hcd_hub power on\n");
		pr_info("f ---- hcd_hub power off\n");
		pr_info("a ---- hcd_hub aotg0 add\n");
		pr_info("b ---- hcd_hub aotg0 remove\n");
		pr_info("c ---- hcd_hub aotg1 add\n");
		pr_info("d ---- hcd_hub aotg1 remove\n");
	}

	if (c == 'a') {
		pr_info("hcd_hub aotg0 add\n");
		aotg_hub_register(0);
	}
	if (c == 'b') {
		pr_info("hcd_hub aotg0 remove\n");
		aotg_hub_unregister(0);
	}

	if (c == 'c') {
		pr_info("hcd_hub aotg1 add\n");
		aotg_hub_register(1);
	}
	if (c == 'd') {
		pr_info("hcd_hub aotg1 remove\n");
		aotg_hub_unregister(1);
	}

	if (c == 'e')
		aotg_trace_onff = 1;

	if (c == 'f')
		aotg_trace_onff = 0;

	if (c == 'g')
		aotg_dbg_regs(act_hcd_ptr[0]);

	if (c == 'i')
		aotg_dbg_regs(act_hcd_ptr[1]);

		return count;
}

static const struct file_operations acts_hub_proc_ops = {
	.open = acts_hub_proc_open,
	.read = seq_read,
	.write = acts_hub_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void create_acts_hcd_proc(void)
{
	acts_hub_pde = proc_create_data("acts_hub", S_IRUSR|S_IWUSR |
		S_IRGRP|S_IWGRP | S_IROTH|S_IWOTH, NULL, &acts_hub_proc_ops, acts_hub_pde);
	return;
}

void remove_acts_hcd_proc(void)
{
	if (acts_hub_pde) {
		remove_proc_entry("acts_hub", NULL);
		acts_hub_pde = NULL;
	}
	return;
}
