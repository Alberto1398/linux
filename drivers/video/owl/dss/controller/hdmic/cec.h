/*
 * cec.h
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


#ifndef _OWL_HDMI_CEC_H_
#define _OWL_HDMI_CEC_H_ __FILE__
#include "ip-sx00.h"
#include "hdmi.h"
/*****************************************************************************
 * This file includes declarations for external functions of
 * Samsung TVOUT-related hardware. So only external functions
 * to be used by higher layer must exist in this file.
 *
 * Higher layer must use only the declarations included in this file.
 ****************************************************************************/

#define to_tvout_plat(d) (to_platform_device(d)->dev.platform_data)


#ifndef hdmi_cec_dbg
#ifdef CEC_DEBUG
#define hdmi_cec_dbg(fmt, ...)					\
		printk(KERN_DEBUG "[%s] %s(): " fmt,		\
			"HDMI-CEC", __func__, ##__VA_ARGS__)
#else
#define hdmi_cec_dbg(fmt, ...)
#endif
#endif
/* CEC Rx buffer size */
#define CEC_RX_BUFF_SIZE            16
/* CEC Tx buffer size */
#define CEC_TX_BUFF_SIZE            16

#define CES_RX_CTRL_ENABLE					(1 << 15)
#define CES_RX_CTRL_RESET					(1 << 14)
#define CES_RX_IRQ_ENABLE					(1 << 12)
#define CES_RX_EOM						(1 << 7)
#define CES_RX_IRQ_PENDDING					(1 << 6)
#define CES_RX_FIFO_RRROR					(1 << 5)
#define CES_RX_FIFO_NUM_MASK					(1 << 5)

#define CES_TX_ADDR_EN						(1 << 20)
#define CES_TX_CTRL_ENABLE					(1 << 15)
#define CES_TX_CTRL_RESET					(1 << 14)
#define CES_TX_IRQ_ENABLE					(1 << 12)
#define CES_TX_EOM						(1 << 7)
#define CES_TX_IRQ_PENDDING					(1 << 6)
#define CES_TX_FIFO_RRROR					(1 << 5)
#define CES_TX_FIFO_NUM_MASK					(1 << 5)
#define CES_TX_CTRL_BCAST					(0x0f << 8)

enum cec_state {
	STATE_RX,
	STATE_TX,
	STATE_DONE,
	STATE_ERROR
};

struct cec_rx_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
	u8 *buffer;
	unsigned int size;
};

struct cec_tx_struct {
	wait_queue_head_t waitq;
	atomic_t state;
};

struct cec_data {

	void __iomem		*cec_base;

	struct cec_rx_struct	cec_rx_struct;
	struct cec_tx_struct	cec_tx_struct;
};

extern struct cec_data *cec;

static inline void cec_write_reg(struct cec_data *cec, const u16 index, u32 val)
{
	writel(val, cec->cec_base + index);
}

static inline u32 cec_read_reg(struct cec_data *cec, const u16 index)
{
	return readl(cec->cec_base + index);
}

int  hdmi_cec_ctrl_init(struct hdmi_ip *ip);

void hdmi_cec_hw_init(struct cec_data *cec);
void hdmi_cec_set_divider(struct cec_data *cec);
void hdmi_cec_enable_rx(struct cec_data *cec);
void hdmi_cec_disable_rx(struct cec_data *cec);
void hdmi_cec_enable_tx(struct cec_data *cec);
void hdmi_cec_disable_tx(struct cec_data *cec);
void hdmi_cec_mask_rx_interrupts(struct cec_data *cec);
void hdmi_cec_unmask_rx_interrupts(struct cec_data *cec);
void hdmi_cec_mask_tx_interrupts(struct cec_data *cec);
void hdmi_cec_unmask_tx_interrupts(struct cec_data *cec);
void hdmi_cec_reset(struct cec_data *cec);
void hdmi_cec_tx_reset(struct cec_data *cec);
void hdmi_cec_rx_reset(struct cec_data *cec);
void hdmi_cec_set_tx_state(struct cec_data *cec, enum cec_state state);
void hdmi_cec_set_rx_state(struct cec_data *cec, enum cec_state state);
void hdmi_cec_copy_packet(struct cec_data *cec, char *data, size_t count);
void hdmi_cec_set_addr(struct cec_data *cec, u32 addr);
u32  hdmi_cec_get_status(struct cec_data *cec);
void hdmi_cec_clr_pending_tx(struct cec_data *cec);
void hdmi_cec_clr_pending_rx(struct cec_data *cec);
u8 hdmi_cec_get_rx_header(struct cec_data *cec);
void hdmi_cec_get_rx_buf(struct cec_data *cec, u32 size, u8 *buffer);
int __init hdmi_cec_ctrl_probe(struct platform_device *pdev);

#endif /* _OWL_HDMI_CEC_H_ */
