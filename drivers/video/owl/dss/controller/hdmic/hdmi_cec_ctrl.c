/*
 * hdmi_cec_ctrl.c
 *
 * HDMI OWL IP driver Library
 *
 * Copyright (C) 2014 Actions Corporation
 * Author: HaiYu Huang  <huanghaiyu@actions-semi.com>
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

#define DEBUGX
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/stddef.h>
#include <video/owl_dss.h>

#define CEC_DEBUG
#include "cec.h"

#define CEC_MESSAGE_BROADCAST_MASK	0x0F
#define CEC_MESSAGE_BROADCAST		0x0F

void hdmi_cec_hw_init(struct cec_data *cec)
{
	uint32_t reg;

	reg = cec_read_reg(cec, HDMI_CECCR);
	/* clear bit31:24 */
	reg &= 0x00ffffff;
	/* PreDiv = 33; timerdiv = 0.8MHz/0.04MHz;disable DAC test */
	reg |= (1 << 28);
	cec_write_reg(cec, HDMI_CECCR, reg);

	reg = cec_read_reg(cec, HDMI_CECRTCR);
	reg &= 0xfffffff0;
	reg |= 5;
	cec_write_reg(cec, HDMI_CECRTCR, reg);

	cec_write_reg(cec, HDMI_CECRXTCR, 0x8cbc2a51);


	/* CECTxTCR Register */
	cec_write_reg(cec, HDMI_CECTXTCR0, 0x9420);
	cec_write_reg(cec, HDMI_CECTXTCR1, 0x182424);

	/* enable CEC mode */
	reg = cec_read_reg(cec, HDMI_CECCR);
	reg |= (1<<30);
	reg |= (4<<24);
	cec_write_reg(cec, HDMI_CECCR, reg);

	reg = cec_read_reg(cec, CEC_DDC_HPD);
	reg |= 0x10;
	reg &= 0xfffffffe;/* enable cec internal pull up resistor */
	cec_write_reg(cec, CEC_DDC_HPD, reg);
}

void hdmi_cec_enable_rx(struct cec_data *cec)
{
	uint32_t reg;

	reg = cec_read_reg(cec, HDMI_CECRXCR);
	reg |= CES_RX_CTRL_ENABLE;
	cec_write_reg(cec, HDMI_CECRXCR, reg);
}

/*we can't disable rx ,used reset replace disable rx*/
void hdmi_cec_disable_rx(struct cec_data *cec)
{
	uint32_t reg;
	reg = cec_read_reg(cec, HDMI_CECRXCR);

	reg |= CES_RX_CTRL_RESET;

	cec_write_reg(cec, HDMI_CECRXCR, reg);

	while ((cec_read_reg(cec, HDMI_CECRXCR) & 0x4000) != 0)
		;
}

void hdmi_cec_disable_tx(struct cec_data *cec)
{
	uint32_t reg;
	reg = cec_read_reg(cec, HDMI_CECTXCR);
	reg &= ~CES_TX_CTRL_ENABLE;
	cec_write_reg(cec, HDMI_CECTXCR, reg);
}

void hdmi_cec_enable_tx(struct cec_data *cec)
{
	uint32_t reg;
	reg = cec_read_reg(cec, HDMI_CECTXCR);
	reg |= CES_TX_CTRL_ENABLE;
	cec_write_reg(cec, HDMI_CECTXCR, reg);
}

void hdmi_cec_mask_rx_interrupts(struct cec_data *cec)
{
	uint32_t reg;

	reg = cec_read_reg(cec, HDMI_CECRXCR);
	reg &= ~CES_RX_IRQ_ENABLE;		/*close cec rx interrupt*/
	reg &= ~CES_RX_IRQ_PENDDING;
	cec_write_reg(cec, HDMI_CECRXCR, reg);
}

void hdmi_cec_unmask_rx_interrupts(struct cec_data *cec)
{
	uint32_t reg;
	reg = cec_read_reg(cec, HDMI_CECRXCR);
	reg |= CES_RX_IRQ_ENABLE;		/*enable cec rx interrupt*/
	reg |= CES_RX_IRQ_PENDDING;		/*set 1 reset cec rx interrupt*/
	cec_write_reg(cec, HDMI_CECRXCR, reg);
}

void hdmi_cec_mask_tx_interrupts(struct cec_data *cec)
{
	uint32_t reg;
	reg = cec_read_reg(cec, HDMI_CECTXCR);
	reg &= ~CES_TX_IRQ_ENABLE;
	reg &= ~CES_TX_IRQ_PENDDING;
	cec_write_reg(cec, HDMI_CECTXCR, reg);

}

void hdmi_cec_unmask_tx_interrupts(struct cec_data *cec)
{
	uint32_t reg;
	reg = cec_read_reg(cec, HDMI_CECTXCR);
	reg |= CES_TX_IRQ_ENABLE;
	reg |= CES_TX_IRQ_PENDDING;
	cec_write_reg(cec, HDMI_CECTXCR, reg);
}

void hdmi_cec_reset(struct cec_data *cec)
{
	uint32_t reg;

	reg = cec_read_reg(cec, HDMI_CECRXCR);

	reg |= CES_RX_CTRL_RESET;

	cec_write_reg(cec, HDMI_CECRXCR, reg);

	while (cec_read_reg(cec, HDMI_CECRXCR) & 0x4000)
		;

	reg = cec_read_reg(cec, HDMI_CECTXCR);

	reg |= CES_TX_CTRL_RESET;

	cec_write_reg(cec, HDMI_CECTXCR, reg);

	while (cec_read_reg(cec, HDMI_CECTXCR) & 0x4000)
		;

}

void hdmi_cec_tx_reset(struct cec_data *cec)
{
	uint32_t reg;

	reg = cec_read_reg(cec, HDMI_CECTXCR);

	reg |= CES_TX_CTRL_RESET;

	cec_write_reg(cec, HDMI_CECTXCR, reg);

	while (cec_read_reg(cec, HDMI_CECTXCR) & 0x4000)
		;
}

void hdmi_cec_rx_reset(struct cec_data *cec)
{
	uint32_t reg;

	reg = cec_read_reg(cec, HDMI_CECRXCR);

	reg |= CES_RX_CTRL_RESET;

	cec_write_reg(cec, HDMI_CECRXCR, reg);

	while (cec_read_reg(cec, HDMI_CECRXCR) & 0x4000)
		;
}

void hdmi_cec_set_tx_state(struct cec_data *cec, enum cec_state state)
{
	atomic_set(&cec->cec_tx_struct.state, state);
}

void hdmi_cec_set_rx_state(struct cec_data *cec, enum cec_state state)
{
	atomic_set(&cec->cec_rx_struct.state, state);
}

void hdmi_cec_copy_packet(struct cec_data *cec, char *data, size_t count)
{
	int i = 1;
	u8 initiator;
	u8 destination;
	uint32_t reg;

	initiator = ((data[0] >> 4) & 0x0f);
	destination = (data[0] & 0x0f);

	hdmi_cec_dbg("%s count %d initiator %d destination %d\n",
				__func__, count, initiator, destination);
	/*reset TX */
	hdmi_cec_tx_reset(cec);

	/*write data */
	while (i < count) {
		cec_write_reg(cec, HDMI_CECTXDR, data[i]);
		hdmi_cec_dbg("0x%x\n", data[i]);
		i++;
	}
	/*config destination*/
	reg = cec_read_reg(cec, HDMI_CECTXCR);

	reg &= ~CES_TX_CTRL_BCAST;

	reg |= (destination << 8);

	cec_write_reg(cec, HDMI_CECTXCR, reg);

	/*config initiator*/
	reg = cec_read_reg(cec, HDMI_CECCR);

	reg &= ~(0x0f << 24);
	reg |= ((initiator & 0x0F) << 24);

	cec_write_reg(cec, HDMI_CECCR, reg);

	/*set count*/
	reg = cec_read_reg(cec, HDMI_CECTXCR);
	reg &= ~0x0f;
	reg |= count;
	cec_write_reg(cec, HDMI_CECTXCR, reg);

	/*set tx */
	hdmi_cec_set_tx_state(cec, STATE_TX);

	/*enable tx*/
	hdmi_cec_enable_tx(cec);

	/*enable tx interrupts*/
	hdmi_cec_unmask_tx_interrupts(cec);
}

void hdmi_cec_set_addr(struct cec_data *cec, uint32_t addr)
{
	uint32_t reg;
	hdmi_cec_dbg("hdmi_cec_set_addr addr %d\n", addr);
	reg = cec_read_reg(cec, HDMI_CECCR);

	reg &= ~(0x0f << 24);
	reg |= ((addr & 0x0F) << 24);

	cec_write_reg(cec, HDMI_CECCR, reg);

}

uint32_t hdmi_cec_get_status(struct cec_data *cec)
{
	uint32_t status = 0;

	status = (cec_read_reg(cec, HDMI_CECTXCR) & 0xff);
	status |= (cec_read_reg(cec, HDMI_CECRXCR) & 0xff) << 8;

	if ((status & CES_TX_IRQ_PENDDING) != 0) {
		status |= CES_TX_IRQ_PENDDING;
		if (!((cec_read_reg(cec, HDMI_CECCR) >> 29) & 0x1)) {
			status |= CES_TX_FIFO_RRROR;
		}
	}
	return status;
}

void hdmi_cec_clr_pending_tx(struct cec_data *cec)
{
	uint32_t reg;

	reg = cec_read_reg(cec, HDMI_CECTXCR);

	reg |= CES_TX_IRQ_PENDDING;

	cec_write_reg(cec, HDMI_CECTXCR, reg);
}

void hdmi_cec_clr_pending_rx(struct cec_data *cec)
{

	uint32_t reg;
	reg = cec_read_reg(cec, HDMI_CECRXCR);

	reg |= CES_RX_IRQ_PENDDING;

	cec_write_reg(cec, HDMI_CECRXCR, reg);
}
u8 hdmi_cec_get_rx_header(struct cec_data *cec)
{
	u8 initiator;
	u8 destination;
	u8 rx_header;

	initiator = (cec_read_reg(cec, HDMI_CECRXCR) >> 8) & 0xf;

	if ((cec_read_reg(cec, HDMI_CECRXCR) >> 16) & 0x1)
		destination = 0xf;
	else
		destination = (cec_read_reg(cec, HDMI_CECCR) >> 24) & 0xf;

	initiator = (cec_read_reg(cec, HDMI_CECRXCR) >> 8) & 0xf;
	rx_header = (initiator << 4) | destination;
	hdmi_cec_dbg("rx_header: %x\n", rx_header);
	return rx_header;
}
void hdmi_cec_get_rx_buf(struct cec_data *cec, uint32_t size, u8 *buffer)
{
	uint32_t i = 1;
	uint32_t reg;
	u8 count = size + 1;
	while (cec_read_reg(cec, HDMI_CECRXCR) & 0x1F) {
		if (i < count) {
			buffer[i] = cec_read_reg(cec, HDMI_CECRXDR);
			i++;
		}
	}

#if 0
	hdmi_cec_dbg("hdmi_cec_get_rx_buf size 0x%x: ", size);
	for (i = 0; i < size; i++)
		hdmi_cec_dbg(" 0x%x ", buffer[i]);
#endif

	if ((cec_read_reg(cec, HDMI_CECRXCR) >> 7) & 0x1) {/* Rx EOM */
		reg = cec_read_reg(cec, HDMI_CECRXCR);
		reg |= 1 << 14;
		cec_write_reg(cec, HDMI_CECRXCR, reg);

		while ((cec_read_reg(cec, HDMI_CECRXCR) & 0x4000) != 0)
			;
	}
}

static void cec_regs_dump(void)
{
#define DUMPREG(name, r) pr_info("%s %08x\n", name, cec_read_reg(cec, r))
	DUMPREG("HDMI_CECCR     value is ", HDMI_CECCR);
	DUMPREG("HDMI_CECRTCR   value is ", HDMI_CECRTCR);
	DUMPREG("HDMI_CECRXCR   value is ", HDMI_CECRXCR);
	DUMPREG("HDMI_CECTXCR   value is ", HDMI_CECTXCR);
	DUMPREG("HDMI_CECTXDR   value is ", HDMI_CECTXDR);
	DUMPREG("HDMI_CECRXTCR  value is ", HDMI_CECRXTCR);
	DUMPREG("HDMI_CECTXTCR0 value is ", HDMI_CECTXTCR0);
	DUMPREG("HDMI_CECTXTCR1 value is ", HDMI_CECTXTCR1);
	DUMPREG("HDMI_CRCCR     value is ", HDMI_CRCCR);
	DUMPREG("HDMI_CRCDOR    value is ", HDMI_CRCDOR);
	DUMPREG("HDMI_TX_1      value is ", HDMI_TX_1);
	DUMPREG("HDMI_TX_2      value is ", HDMI_TX_2);
	DUMPREG("CEC_DDC_HPD    value is ", CEC_DDC_HPD);
#undef DUMPREG
}

struct cec_data *cec;

int  hdmi_cec_ctrl_init(struct hdmi_ip *ip)
{
	int ret = 0;
	u8 *buffer;

	pr_debug("%s\n", __func__);

	cec = kzalloc(sizeof(*cec), GFP_KERNEL);
	if (cec == NULL)
		return -ENOMEM;

	cec->cec_base = ip->base;
	if (IS_ERR(cec->cec_base)) {
		pr_err("failed to get register base for cec\n");
		return PTR_ERR(cec->cec_base);
	}

	spin_lock_init(&cec->cec_rx_struct.lock);
	init_waitqueue_head(&cec->cec_rx_struct.waitq);
	init_waitqueue_head(&cec->cec_tx_struct.waitq);

	buffer = kzalloc(CEC_TX_BUFF_SIZE, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	cec->cec_rx_struct.buffer = buffer;

	cec->cec_rx_struct.size = 0;

#if 0
	cec_regs_dump();
#endif
	return ret;
}

