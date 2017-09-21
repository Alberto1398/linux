/*
 * Driver for Actions OWL SOC family serial device and console
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(CONFIG_SERIAL_OWL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/time.h>

#define DRIVER_NAME			"serial-owl"

#define OWL_MAX_UART			7
#define OWL_UART_MAX_BARD_RATE		3000000		/* HOSC	/ 8 */
#define OWL_UART_FIFO_SIZE		32
#define OWL_UART_RX_BUF_SIZE		4096
#define OWL_UART_DMA_RX_POLL_RATE_MS	10		/* 10 ms */
#define OWL_UART_DMA_RX_POLL_TIMEOUT_MS	500		/* 500 ms */

/* Max time to wait the uart fifo emtpy (in us) */
#define OWL_UART_CONSOLE_FLUSH_TIMEOUT	100000		/* 100ms */

/*
 * note: This workaround is only neeeded by S500.
 * S900 don't has his bug.
 */
/* #define OWL_SERIAL_BUG_DMA_WAIT_TXFIFO */

/* UART name and device definitions */
#define OWL_SERIAL_NAME			"ttyS"
#define OWL_SERIAL_MAJOR		204
#define OWL_SERIAL_MINOR		0

/* UART registers */
#define UART_CTL			(0x0000)
#define UART_RXDAT			(0x0004)
#define UART_TXDAT			(0x0008)
#define UART_STAT			(0x000C)

/* UART0_CTL */
#define UART_CTL_DTCR			(0x1 << 22)
#define UART_CTL_DRCR			(0x1 << 21)
#define UART_CTL_LBEN			(0x1 << 20)
#define UART_CTL_TXIE			(0x1 << 19)
#define UART_CTL_RXIE			(0x1 << 18)
#define UART_CTL_TXDE			(0x1 << 17)
#define UART_CTL_RXDE			(0x1 << 16)
#define UART_CTL_EN				(0x1 << 15)
#define UART_CTL_TRFS			(0x1 << 14)
#define		UART_CTL_TRFS_RX		(0x0 << 14)
#define		UART_CTL_TRFS_TX		(0x1 << 14)
#define UART_CTL_AFE			(0x1 << 12)
#define UART_CTL_PRS_MASK		(0x7 << 4)
#define UART_CTL_PRS(x)			(((x) & 0x7) <<	4)
#define		UART_CTL_PRS_NONE		UART_CTL_PRS(0)
#define		UART_CTL_PRS_ODD		UART_CTL_PRS(4)
#define		UART_CTL_PRS_MARK		UART_CTL_PRS(5)
#define		UART_CTL_PRS_EVEN		UART_CTL_PRS(6)
#define		UART_CTL_PRS_SPACE		UART_CTL_PRS(7)
#define UART_CTL_STPS			(0x1 << 2)
#define		UART_CTL_STPS_1BITS		(0x0 << 2)
#define		UART_CTL_STPS_2BITS		(0x1 << 2)
#define UART_CTL_DWLS_MASK		(0x3 << 0)
#define UART_CTL_DWLS(x)		(((x) & 0x3) <<	0)
#define		UART_CTL_DWLS_5BITS		UART_CTL_DWLS(0)
#define		UART_CTL_DWLS_6BITS		UART_CTL_DWLS(1)
#define		UART_CTL_DWLS_7BITS		UART_CTL_DWLS(2)
#define		UART_CTL_DWLS_8BITS		UART_CTL_DWLS(3)

/* UART0_RXDAT */
#define UART_RXDAT_MASK			(0xFF << 0)

/* UART0_TXDAT */
#define UART_TXDAT_MASK			(0xFF << 0)

/* UART_STAT */
#define UART_STAT_UTBB			(0x1 <<	17)
#define UART_STAT_TRFL_MASK		(0x3F << 11)
#define UART_STAT_TRFL_SET(x)		(((x) & 0x3F) << 11)
#define UART_STAT_TFES			(0x1 << 10)
#define UART_STAT_RFFS			(0x1 << 9)
#define UART_STAT_RTSS			(0x1 << 8)
#define UART_STAT_CTSS			(0x1 << 7)
#define UART_STAT_TFFU			(0x1 << 6)
#define UART_STAT_RFEM			(0x1 << 5)
#define UART_STAT_RXST			(0x1 << 4)
#define UART_STAT_TFER			(0x1 << 3)
#define UART_STAT_RXER			(0x1 << 2)
#define UART_STAT_TIP			(0x1 << 1)
#define UART_STAT_RIP			(0x1 << 0)

struct owl_uart_port {
	struct uart_port	port;
	char			name[16];
	struct clk		*clk;
	struct reset_control	*rst;

	struct dma_chan		*rx_dma_chan;
	struct dma_chan		*tx_dma_chan;

	int sysrq_rx_index;

	bool			using_rx_dma;
	bool			using_tx_dma;

	bool			dma_is_txing;
	bool			dma_is_rxing;

	dma_addr_t		rx_addr;
	dma_addr_t		tx_addr;

	unsigned int		tx_size;
	unsigned int		rx_size;

	void			*rx_buf;
	unsigned int		rx_buf_tail;

	dma_cookie_t            rx_dma_cookie;
	struct timer_list	rx_dma_timer;
	unsigned int		rx_dma_poll_rate;
	unsigned int		rx_dma_poll_timeout;

	unsigned long		port_activity;
};

/* from  serial_core.c */
#ifdef CONFIG_SERIAL_CORE_CONSOLE
#define uart_console(port)	((port)->cons && (port)->cons->index == (port)->line)
#else
#define uart_console(port)	(0)
#endif


static struct owl_uart_port *owl_ports[OWL_MAX_UART];

static void owl_serial_stop_tx(struct	uart_port *port);
static void owl_serial_dma_start_tx(struct owl_uart_port *aport);
static int owl_serial_dma_start_rx(struct owl_uart_port *aport);
static void owl_serial_stop_dma_rx(struct uart_port *port);

static inline struct owl_uart_port *to_owl_port(struct uart_port *port)
{
	return container_of(port, struct owl_uart_port, port);
}

static inline void owl_serial_write(struct uart_port *port, unsigned int val,
		unsigned int off)
{
	__raw_writel(val, port->membase	+ off);
}

static inline unsigned int owl_serial_read(struct uart_port *port,
		unsigned int off)
{
	return __raw_readl(port->membase + off);
}

static int owl_serial_check_error(struct uart_port *port)
{
	unsigned int stat = owl_serial_read(port, UART_STAT);

	if (stat & (UART_STAT_RXER | UART_STAT_TFER | UART_STAT_RXST))
		return 1;

	return 0;
}

static int owl_serial_clear_error(struct uart_port *port)
{
	unsigned int stat = owl_serial_read(port, UART_STAT);

	/* clear error */
	stat |= (UART_STAT_RXER | UART_STAT_TFER | UART_STAT_RXST);
	/* reserve interrupt pending bits */
	stat &= ~(UART_STAT_RIP | UART_STAT_TIP);
	owl_serial_write(port, stat, UART_STAT);

	return 0;
}

static void owl_serial_reset(struct uart_port *port)
{
	unsigned int ctl;

	ctl = owl_serial_read(port, UART_CTL);
	owl_serial_write(port, ctl & ~UART_CTL_EN, UART_CTL);
	owl_serial_write(port, ctl, UART_CTL);
}

#ifdef SUPPORT_SYSRQ
static int owl_is_break_button_down(struct uart_port *port, char c)
{
	const char breakbuf[] = {0x02, 0x12, 0x05, 0x01, 0x0b}; /* ctrl + break */
	struct owl_uart_port *aport = to_owl_port(port);

	if (c == breakbuf[aport->sysrq_rx_index]) {
		aport->sysrq_rx_index++;
		if (aport->sysrq_rx_index == sizeof(breakbuf)) {
			aport->sysrq_rx_index = 0;
			return 1;
		}
	} else
		aport->sysrq_rx_index = 0;

	return 0;
}
#else
static int owl_is_break_button_down(struct uart_port *port, char c)
{
}
#endif
/*
 * Characters received (called from interrupt handler)
 */
static void owl_serial_pio_rx(struct uart_port *port)
{
	unsigned int stat, c;
	char flag = TTY_NORMAL;

	/* and now the main RX loop */
	while (!((stat = owl_serial_read(port, UART_STAT)) & UART_STAT_RFEM)) {
		c = owl_serial_read(port, UART_RXDAT);

		if (stat & UART_STAT_RXER)
			port->icount.overrun++;

		if (stat & UART_STAT_RXST) {
			/* we are not able to distinguish the error type */
			port->icount.brk++;
			port->icount.frame++;

			/* Mask	conditions we're ignorning. */
			stat &= port->read_status_mask;
			if (stat & UART_STAT_RXST)
				flag = TTY_PARITY;
		} else {
			port->icount.rx++;
		}
#ifdef SUPPORT_SYSRQ
		if (owl_is_break_button_down(port, c)) {
			if (uart_handle_break(port))
				continue;
		}
#endif

		if (uart_handle_sysrq_char(port, c))
			continue;

		uart_insert_char(port, stat, stat & UART_STAT_RXER, c, flag);
	}

	tty_flip_buffer_push(&port->state->port);
}

static void owl_serial_pio_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	if (uart_tx_stopped(port))
		return;

	if (port->x_char) {
		while (owl_serial_read(port, UART_STAT) & UART_STAT_TFFU)
			;
		owl_serial_write(port, port->x_char, UART_TXDAT);
		port->icount.tx++;
		port->x_char = 0;
	}

	while (!(owl_serial_read(port, UART_STAT) & UART_STAT_TFFU)) {
		if (uart_circ_empty(xmit))
			break;

		owl_serial_write(port, xmit->buf[xmit->tail], UART_TXDAT);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE	- 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		owl_serial_stop_tx(port);
}

/* Caller must hold uart port lock */
static unsigned int owl_serial_dma_rx_receive_chars(
		struct owl_uart_port *aport)
{
	struct uart_port *port = &aport->port;
	struct tty_port *tport = &port->state->port;
	struct dma_tx_state state;
	enum dma_status status;
	unsigned int rx_buf_head, count = 0;

	status = dmaengine_tx_status(aport->rx_dma_chan,
			aport->rx_dma_cookie, &state);
	rx_buf_head = aport->rx_size - state.residue;

	if (rx_buf_head > aport->rx_buf_tail) {
		count = rx_buf_head - aport->rx_buf_tail;

		tty_insert_flip_string(tport,
				aport->rx_buf + aport->rx_buf_tail,
				count);
		tty_flip_buffer_push(tport);
		port->icount.rx += count;

		aport->port_activity = jiffies;
	}

	aport->rx_buf_tail = rx_buf_head;

	return count;
}

static void owl_serial_dma_poll_rx(struct owl_uart_port *aport)
{
	struct uart_port *port = &aport->port;
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&port->lock, flags);

	owl_serial_dma_rx_receive_chars(aport);

	if (aport->rx_dma_poll_timeout != 0 &&
		time_is_before_jiffies(aport->port_activity +
				       aport->rx_dma_poll_timeout)) {
		owl_serial_stop_dma_rx(port);

		del_timer(&aport->rx_dma_timer);

		/* disable RX DRQ and enable RX IRQ */
		val = owl_serial_read(&aport->port, UART_CTL);
		val &= ~UART_CTL_RXDE;
		val |= UART_CTL_RXIE;
		owl_serial_write(&aport->port, val, UART_CTL);
	} else {
		/* set next poll time */
		mod_timer(&aport->rx_dma_timer,
			jiffies + aport->rx_dma_poll_rate);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static void owl_serial_dma_rx_callback(void *data)
{
	struct owl_uart_port *aport = data;
	struct uart_port *port = &aport->port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	owl_serial_dma_rx_receive_chars(aport);

	if (aport->dma_is_rxing)
		owl_serial_dma_start_rx(aport);

	spin_unlock_irqrestore(&port->lock, flags);
}

static int owl_serial_dma_start_rx(struct owl_uart_port *aport)
{
	struct dma_chan	*chan = aport->rx_dma_chan;
	struct device *dev = aport->port.dev;
	struct dma_async_tx_descriptor *desc;
	unsigned int val;

	desc = dmaengine_prep_slave_single(chan, aport->rx_addr,
			aport->rx_size, DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "We cannot prepare for the RX slave dma!\n");
		return -EINVAL;
	}

	dma_sync_single_for_device(dev, aport->rx_addr,
			aport->rx_size, DMA_FROM_DEVICE);

	desc->callback = owl_serial_dma_rx_callback;
	desc->callback_param = aport;

	/* enable RX DRQ */
	val = owl_serial_read(&aport->port, UART_CTL);
	val |= UART_CTL_RXDE | UART_CTL_DRCR;
	owl_serial_write(&aport->port, val, UART_CTL);

	dev_dbg(dev, "RX: prepare for the DMA.\n");

	aport->dma_is_rxing = 1;
	aport->rx_buf_tail = 0;
	aport->rx_dma_cookie = dmaengine_submit(desc);
	dma_async_issue_pending(chan);

	mod_timer(&aport->rx_dma_timer, jiffies + aport->rx_dma_poll_rate);

	return 0;
}

static void owl_serial_dma_tx_callback(void *data)
{
	struct owl_uart_port *aport = data;
	struct uart_port *port = &aport->port;
	struct circ_buf *xmit = &aport->port.state->xmit;
	unsigned long flags;
	unsigned int val;
#ifdef OWL_SERIAL_BUG_DMA_WAIT_TXFIFO
	unsigned int timeout = 1000;	/* 1000 us */
#endif

	spin_lock_irqsave(&aport->port.lock, flags);

	aport->dma_is_txing = 0;
	xmit->tail = (xmit->tail + aport->tx_size) & (UART_XMIT_SIZE - 1);
	aport->port.icount.tx += aport->tx_size;

	aport->port_activity = jiffies;

	/* wake up the possible processes. */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&aport->port);

#ifdef OWL_SERIAL_BUG_DMA_WAIT_TXFIFO
	/*
	 * note: This workaround is only neeeded by S500.
	 * S900 don't has his bug.
	 *
	 * we must wait until the TX FIFO is empty before
	 * we start next dma transfer, otherwise we will
	 * lost some bytes.
	 */
	while (owl_serial_read(aport, UART_STAT) & UART_STAT_UTBB) {
		if (0 == timeout) {
			dev_err(port->dev, "TX DMA timeout! UART_STAT 0x%x\n",
				owl_serial_read(aport, UART_STAT));
			return;
		}
		timeout--;
		udelay(1);
	}
#endif

	/* disable TX DRQ */
	val = owl_serial_read(&aport->port, UART_CTL);
	val &= ~UART_CTL_TXDE;
	owl_serial_write(&aport->port, val, UART_CTL);

	/* clear IRQ Pending */
	owl_serial_write(&aport->port, UART_STAT_TIP, UART_STAT);

	/* check error */
	if (owl_serial_check_error(port)) {
		/* don't printk to console uart to avoid dead lock */
		if (!uart_console(port))
			dev_err(port->dev, "dma transfer error UART_CTL %x, "
				"UART_STAT %x\n",
				owl_serial_read(port, UART_CTL),
				owl_serial_read(port, UART_STAT));

		owl_serial_clear_error(port);
	}

	if (!uart_circ_empty(xmit) && !uart_tx_stopped(&aport->port))
		owl_serial_dma_start_tx(aport);

	spin_unlock_irqrestore(&aport->port.lock, flags);

	dev_dbg(aport->port.dev, "we finish the TX DMA (%d bytes).\n",
		aport->tx_size);
}

static void owl_serial_dma_start_tx(struct owl_uart_port *aport)
{
	struct uart_port *port = &aport->port;
	struct circ_buf *xmit = &aport->port.state->xmit;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan	*chan = aport->tx_dma_chan;
	struct device *dev = aport->port.dev;
	unsigned int val;

	if (uart_tx_stopped(&aport->port) || aport->dma_is_txing ||
		uart_circ_empty(xmit))
		return;

	aport->tx_size = CIRC_CNT_TO_END(xmit->head, xmit->tail,
			UART_XMIT_SIZE);

	desc = dmaengine_prep_slave_single(chan,
			aport->tx_addr + xmit->tail,
			aport->tx_size, DMA_MEM_TO_DEV,
			DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "We cannot prepare for the TX slave dma!\n");
		return;
	}

	dma_sync_single_for_device(port->dev, aport->tx_addr,
			UART_XMIT_SIZE, DMA_TO_DEVICE);

	desc->callback = owl_serial_dma_tx_callback;
	desc->callback_param = aport;

	/* enable tx DRQ */
	val = owl_serial_read(port, UART_CTL);
	val &= ~UART_CTL_TXIE;
	val |= UART_CTL_TXDE | UART_CTL_DRCR;
	owl_serial_write(port, val, UART_CTL);

	/* fire it */
	aport->dma_is_txing = 1;
	dmaengine_submit(desc);
	dma_async_issue_pending(chan);
}

/*
 * Interrupt handler
 */
static irqreturn_t owl_serial_interrupt(int irq, void *dev_id)
{
	struct owl_uart_port *aport = dev_id;
	struct uart_port *port = &aport->port;
	unsigned int stat, val;

	spin_lock(&port->lock);

	/* check error */
	if (owl_serial_check_error(port)) {
		/* don't printk to console uart to avoid dead lock */
		if (!uart_console(port))
			dev_err(port->dev, "transfer error UART_CTL %x, "
				"UART_STAT %x\n",
				owl_serial_read(port, UART_CTL),
				owl_serial_read(port, UART_STAT));

		owl_serial_clear_error(port);
	}

	stat = owl_serial_read(port, UART_STAT);

	/* clear TX/RX IRQ pending*/
	owl_serial_write(port, stat, UART_STAT);

	/* receive in irq to avoid data lost */
	if (stat & UART_STAT_RIP) {
		if (aport->using_rx_dma) {
			/* disable RX IRQ */
			val = owl_serial_read(port, UART_CTL);
			val &= ~UART_CTL_RXIE;
			owl_serial_write(port, val, UART_CTL);

			owl_serial_dma_start_rx(aport);
		} else {
			owl_serial_pio_rx(port);
		}
	}

	if ((stat & UART_STAT_TIP) && !aport->using_tx_dma)
		owl_serial_pio_tx(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

/*
 * Return TIOCSER_TEMT when transmitter FIFO and Shift register is empty.
 */
static unsigned	int owl_serial_tx_empty(struct uart_port *port)
{
	struct owl_uart_port *aport = to_owl_port(port);
	int ret;

	ret = (owl_serial_read(port, UART_STAT) & UART_STAT_TFES) ?
		TIOCSER_TEMT : 0;

	/* If the TX DMA is working, return 0. */
	if (aport->using_tx_dma && aport->dma_is_txing)
		ret = 0;

	return ret;
}

/*
 * Start transmitting.
 */
static void owl_serial_start_tx(struct uart_port *port)
{
	struct owl_uart_port *aport = to_owl_port(port);
	unsigned int val;

	if (aport->using_tx_dma) {
		owl_serial_dma_start_tx(aport);
		return;
	}

	/* clear TX IRQ pending */
	val = owl_serial_read(port, UART_STAT);
	val |= UART_STAT_TIP;
	val &= ~UART_STAT_RIP;
	owl_serial_write(port, val, UART_STAT);

	/* enable TX IRQ */
	val = owl_serial_read(port, UART_CTL);
	val |= UART_CTL_TXIE;
	owl_serial_write(port, val, UART_CTL);
}

/*
 * Stop	transmitting.
 */
static void owl_serial_stop_tx(struct	uart_port *port)
{
	struct owl_uart_port *aport = to_owl_port(port);
	unsigned int val;

	if (aport->using_tx_dma) {
		dmaengine_terminate_all(aport->tx_dma_chan);
		aport->dma_is_txing = 0;
	}

	/* disable TX IRQ & DRQ */
	val = owl_serial_read(port, UART_CTL);
	val &= ~(UART_CTL_TXIE | UART_CTL_TXDE);
	val |= UART_CTL_DTCR;
	owl_serial_write(port, val, UART_CTL);

	/* clear TX IRQ pending */
	val = owl_serial_read(port, UART_STAT);
	val |= UART_STAT_TIP;
	val &= ~UART_STAT_RIP;
	owl_serial_write(port, val, UART_STAT);
}

static void owl_serial_stop_dma_rx(struct uart_port *port)
{
	struct owl_uart_port *aport = to_owl_port(port);

	dmaengine_terminate_all(aport->rx_dma_chan);
	aport->dma_is_rxing = 0;
}

/*
 * Stop receiving - port is in process of being closed.
 */
static void owl_serial_stop_rx(struct uart_port *port)
{
	struct owl_uart_port *aport = to_owl_port(port);
	unsigned int val;

	if (aport->using_rx_dma) {
		owl_serial_stop_dma_rx(port);
	}

	/* disable RX IRQ & DRQ */
	val = owl_serial_read(port, UART_CTL);
	val &= ~(UART_CTL_RXIE | UART_CTL_RXDE);
	val |= UART_CTL_DRCR;
	owl_serial_write(port, val, UART_CTL);

	/* clear RX IRQ pending */
	val = owl_serial_read(port, UART_STAT);
	val |= UART_STAT_RIP;
	val &= ~UART_STAT_TIP;
	owl_serial_write(port, val, UART_STAT);
}



/*
 * Enable modem	status interrupts
 */
static void owl_serial_enable_ms(struct uart_port *port)
{
}

/* modem control is not implemented */
static unsigned int owl_serial_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

static void owl_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void owl_serial_break_ctl(struct uart_port *port, int break_ctl)
{
}

static int owl_serial_set_baud_rate(struct uart_port *port, unsigned int baud)
{
	struct owl_uart_port *aport = to_owl_port(port);

	if (aport->clk)
		clk_set_rate(aport->clk, baud * 8);

	return baud;
}

static int owl_serial_dma_init_rx(struct owl_uart_port *aport)
{
	struct dma_slave_config slave_config = {};
	struct device *dev = aport->port.dev;

	/* init for RX */
	aport->rx_dma_chan = dma_request_slave_channel(dev, "rx");
	if (!aport->rx_dma_chan) {
		//dev_info(dev, "cannot get the RX DMA channel!\n");
		return -ENODEV;
	}

	slave_config.direction = DMA_DEV_TO_MEM;
	slave_config.src_addr = aport->port.mapbase + UART_RXDAT;
	slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;

	dmaengine_slave_config(aport->rx_dma_chan, &slave_config);

	aport->rx_size = OWL_UART_RX_BUF_SIZE;
	aport->rx_buf = dma_alloc_coherent(dev, aport->rx_size,
			&aport->rx_addr, GFP_KERNEL);
	if (!aport->rx_buf) {
		dma_release_channel(aport->rx_dma_chan);
		return -ENOMEM;
	}

	/*init timer*/
	aport->rx_dma_timer.data = (unsigned long)aport;
	aport->rx_dma_timer.function =
		(void *)owl_serial_dma_poll_rx;

	aport->rx_dma_poll_rate =
		msecs_to_jiffies(OWL_UART_DMA_RX_POLL_RATE_MS);
	aport->rx_dma_poll_timeout =
		msecs_to_jiffies(OWL_UART_DMA_RX_POLL_TIMEOUT_MS);

	aport->dma_is_rxing = 0;

	return 0;
}

static int owl_serial_dma_init_tx(struct owl_uart_port *aport)
{
	struct dma_slave_config slave_config = {};
	struct device *dev = aport->port.dev;

	/* init for TX */
	aport->tx_dma_chan = dma_request_slave_channel(dev, "tx");
	if (!aport->tx_dma_chan) {
		//dev_info(dev, "cannot get the TX DMA channel!\n");
		return -ENODEV;
	}

	slave_config.direction = DMA_MEM_TO_DEV;
	slave_config.dst_addr = aport->port.mapbase + UART_TXDAT;
	slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	dmaengine_slave_config(aport->tx_dma_chan, &slave_config);

	/* TX buffer */
	aport->tx_addr = dma_map_single(dev,
		aport->port.state->xmit.buf,
		UART_XMIT_SIZE,
		DMA_TO_DEVICE);

	aport->dma_is_txing = 0;

	return 0;
}

static int owl_serial_dma_init(struct owl_uart_port *aport)
{
	struct device *dev = aport->port.dev;
	int ret;

	ret = owl_serial_dma_init_rx(aport);
	if (!ret)
		aport->using_rx_dma = true;
	else
		aport->using_rx_dma = false;

	ret = owl_serial_dma_init_tx(aport);
	if (!ret)
		aport->using_tx_dma = true;
	else
		aport->using_tx_dma = false;

	/* The DMA buffer is now the FIFO the TTY subsystem can use */
	aport->port.fifosize = UART_XMIT_SIZE;
	aport->port_activity = 0;

	if (aport->using_rx_dma || aport->using_tx_dma) {
		dev_info(dev, "ttyS%d: %s %s\n",
			aport->port.line,
			aport->using_rx_dma ? "DMA RX" : " ",
			aport->using_tx_dma ? "DMA TX" : " ");
	}

	return 0;
}

static int owl_serial_dma_exit(struct owl_uart_port *aport)
{
	if (aport->rx_dma_chan)	{
		dma_release_channel(aport->rx_dma_chan);
		aport->rx_dma_chan = NULL;
		aport->using_rx_dma = false;
	}

	if (aport->tx_dma_chan)	{
		dma_release_channel(aport->tx_dma_chan);
		aport->tx_dma_chan = NULL;
		aport->using_tx_dma = false;
	}

	return 0;
}

/*
 * Perform initialization and enable port for reception
 */
static int owl_serial_startup(struct uart_port *port)
{
	struct owl_uart_port *aport = to_owl_port(port);
	unsigned long flags;
	unsigned int val;
	int ret;

	snprintf(aport->name, sizeof(aport->name),
		DRIVER_NAME "%d", port->line);

	/* already opened */
	if (port->flags & ASYNC_INITIALIZED)
		return 0;

	ret = request_irq(port->irq, owl_serial_interrupt, IRQF_TRIGGER_HIGH,
		  aport->name, aport);
	if (unlikely(ret))
		return ret;

	if (aport->clk)
		clk_enable(aport->clk);

	owl_serial_dma_init(aport);

	spin_lock_irqsave(&port->lock, flags);

	/* clear IRQ pending */
	val = owl_serial_read(port, UART_STAT);
	val |= UART_STAT_RIP | UART_STAT_TIP;
	owl_serial_write(port, val, UART_STAT);

	/* enable module/IRQs */
	val = owl_serial_read(port, UART_CTL);
	val |= UART_CTL_RXIE | UART_CTL_EN;
	owl_serial_write(port, val, UART_CTL);

	port->flags |= ASYNC_INITIALIZED;

	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

/*
 * Disable the port
 */
static void owl_serial_shutdown(struct uart_port *port)
{
	struct owl_uart_port *aport = to_owl_port(port);
	unsigned int val;
	unsigned long flags;

	/* already shutdown */
	if (!(port->flags & ASYNC_INITIALIZED))
		return;

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Ensure everything is stopped.
	 */
	owl_serial_stop_rx(port);
	owl_serial_stop_tx(port);

	spin_unlock_irqrestore(&port->lock, flags);

	/* cannot hold spinlock to call del_timer_sync */
	if (aport->rx_dma_poll_rate)
		del_timer_sync(&aport->rx_dma_timer);

	spin_lock_irqsave(&port->lock, flags);

	/* free dma channels */
	owl_serial_dma_exit(aport);

	/* disable module/IRQs */
	val = owl_serial_read(port, UART_CTL);
	val &= ~(UART_CTL_RXIE | UART_CTL_RXDE |
		 UART_CTL_TXIE | UART_CTL_TXDE | UART_CTL_EN);
	owl_serial_write(port, val, UART_CTL);

	spin_unlock_irqrestore(&port->lock, flags);

	free_irq(port->irq, port);

	if (aport->clk)
		clk_disable(aport->clk);

	port->flags &= ~ASYNC_INITIALIZED;
}

/*
 * Flush any TX data submitted for DMA and PIO. Called when the
 * TX circular buffer is reset.
 */
static void owl_serial_flush_buffer(struct uart_port *port)
{
	struct owl_uart_port *aport = (struct owl_uart_port *)port;

	aport->tx_size = 0;
	if (aport->tx_dma_chan)
		dmaengine_terminate_all(aport->tx_dma_chan);
}

/*
 * Change the port parameters
 */
static void owl_serial_set_termios(struct uart_port *port,
		struct ktermios *termios, struct ktermios *old)
{
	unsigned long flags;
	unsigned int ctl, baud;

	baud = uart_get_baud_rate(port, termios, old, 0,
			OWL_UART_MAX_BARD_RATE);
	baud = owl_serial_set_baud_rate(port, baud);

	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * We don't support modem control lines.
	 */
	termios->c_cflag &= ~(HUPCL | CMSPAR);
	termios->c_cflag |= CLOCAL;

	/*
	 * We don't support BREAK character recognition.
	 */
	termios->c_iflag &= ~(IGNBRK | BRKINT);

	ctl = owl_serial_read(port, UART_CTL);
	ctl &= ~(UART_CTL_DWLS_MASK | UART_CTL_STPS
		| UART_CTL_PRS_MASK | UART_CTL_AFE);

	/* byte size */
	ctl &= ~UART_CTL_DWLS_MASK;
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		ctl |= UART_CTL_DWLS(0);
		break;
	case CS6:
		ctl |= UART_CTL_DWLS(1);
		break;
	case CS7:
		ctl |= UART_CTL_DWLS(2);
		break;
	case CS8:
	default:
		ctl |= UART_CTL_DWLS(3);
		break;
	}

	/* stop bits */
	if (termios->c_cflag & CSTOPB)
		ctl |= UART_CTL_STPS_2BITS;
	else
		ctl |= UART_CTL_STPS_1BITS;

	/* parity */
	if (termios->c_cflag & PARENB) {
		/* Mark or Space parity */
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				ctl |= UART_CTL_PRS_MARK;
			else
				ctl |= UART_CTL_PRS_SPACE;
		} else if (termios->c_cflag & PARODD)
			ctl |= UART_CTL_PRS_ODD;
		else
			ctl |= UART_CTL_PRS_EVEN;
	} else {
		ctl |= UART_CTL_PRS_NONE;
	}

	/* hardware	handshake (RTS/CTS) */
	if (termios->c_cflag & CRTSCTS)
		ctl |= UART_CTL_AFE;

	owl_serial_write(port, ctl, UART_CTL);

	/* Configure status bits to ignore based on termio flags. */

	/*
	 * Normally we need to mask the bits we do care about as there is
	 * no hardware support for (termios->c_iflag & INPACK/BRKINT/PARMRK)
	 * and it seems the interrupt happened only for tx/rx we do nothing
	 * about the port.read_status_mask
	 */
	port->read_status_mask |= UART_STAT_RXER;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UART_STAT_RXST;

	/* update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, baud);

	spin_unlock_irqrestore(&port->lock, flags);
}

/*
 * Return string describing the specified port
 */
static const char *owl_serial_type(struct uart_port *port)
{
	return port->type == PORT_OWL ? DRIVER_NAME : NULL;
}

static void owl_serial_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, SZ_4K);
}

static int owl_serial_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, SZ_4K, DRIVER_NAME)
			!= NULL ? 0 : -EBUSY;
}

static void owl_serial_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_OWL;
		owl_serial_request_port(port);
	}
}

static int owl_serial_verify_port(struct uart_port *port,
		struct serial_struct *ser)
{
	if (unlikely(ser->type != PORT_UNKNOWN && ser->type != port->type))
		return -EINVAL;
	if (unlikely(port->irq != ser->irq))
		return -EINVAL;
	if ((void *)port->membase != ser->iomem_base)
		return -EINVAL;
	return 0;
}

#ifdef CONFIG_CONSOLE_POLL
static int owl_serial_poll_get_char(struct uart_port *port)
{
	unsigned int val, old_ctl, c;

	/* backup old ctrl register */
	old_ctl = owl_serial_read(port, UART_CTL);

	/* disable RX/TX IRQ */
	val = owl_serial_read(port, UART_CTL);
	val &= ~(UART_CTL_RXIE | UART_CTL_TXIE);
	owl_serial_write(port, val, UART_CTL);

	if (owl_serial_read(port, UART_STAT) & UART_STAT_RFEM)
		c = NO_POLL_CHAR;
	else
		c = owl_serial_read(port, UART_RXDAT);

	/* clear RX IRQ pending */
	val = owl_serial_read(port, UART_STAT);
	val |= UART_STAT_RIP;
	owl_serial_write(port, val, UART_STAT);

	/* restore old ctrl register */
	owl_serial_write(port, val, UART_CTL);

	return c;
}

static void owl_serial_poll_put_char(struct uart_port *port,
		unsigned char ch)
{
	unsigned int val, old_ctl;

	/* backup old ctrl register */
	old_ctl = owl_serial_read(port, UART_CTL);

	/* disable RX/TX IRQ */
	val = owl_serial_read(port, UART_CTL);
	val &= ~(UART_CTL_RXIE | UART_CTL_TXIE);
	owl_serial_write(port, val, UART_CTL);

	while (owl_serial_read(port, UART_STAT) & UART_STAT_TFFU)
		cpu_relax();

	owl_serial_write(port, ch, UART_TXDAT);

	/* wait transfer over */
	while (owl_serial_read(port, UART_STAT) & UART_STAT_UTBB)
		cpu_relax();

	/* clear TX IRQ pending */
	val = owl_serial_read(port, UART_STAT);
	val |= UART_STAT_TIP;
	owl_serial_write(port, val, UART_STAT);

	/* restore old ctrl register */
	owl_serial_write(port, old_ctl, UART_CTL);
}
#endif

static struct uart_ops owl_uart_pops = {
	.tx_empty	= owl_serial_tx_empty,
	.set_mctrl	= owl_serial_set_mctrl,
	.get_mctrl	= owl_serial_get_mctrl,
	.stop_tx	= owl_serial_stop_tx,
	.start_tx	= owl_serial_start_tx,
	.stop_rx	= owl_serial_stop_rx,
	.flush_buffer	= owl_serial_flush_buffer,
	.enable_ms	= owl_serial_enable_ms,
	.break_ctl	= owl_serial_break_ctl,
	.startup	= owl_serial_startup,
	.shutdown	= owl_serial_shutdown,
	.set_termios	= owl_serial_set_termios,
	.type		= owl_serial_type,
	.release_port	= owl_serial_release_port,
	.request_port	= owl_serial_request_port,
	.config_port	= owl_serial_config_port,
	.verify_port	= owl_serial_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= owl_serial_poll_get_char,
	.poll_put_char	= owl_serial_poll_put_char,
#endif
};

#ifdef CONFIG_SERIAL_OWL_CONSOLE
static void owl_console_putchar(struct uart_port *port, int c)
{
	while (owl_serial_read(port, UART_STAT) & UART_STAT_TFFU)
		cpu_relax();
	owl_serial_write(port, c, UART_TXDAT);
}

static void owl_console_write(struct console *co, const char *s,
		  unsigned int count)
{
	struct owl_uart_port *aport;
	struct uart_port *port;
	unsigned int old_ctl, val;
	unsigned long flags;
	int locked = 1;
	int timeout = 0;

	BUG_ON(co->index < 0 || co->index >= OWL_MAX_UART);

	aport = owl_ports[co->index];
	if (aport == NULL)
		return;

	port = &aport->port;

	local_irq_save(flags);

	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&port->lock);
	else
		spin_lock(&port->lock);

	if (aport->clk)
		clk_enable(aport->clk);

	/* backup old control register */
	old_ctl = owl_serial_read(port, UART_CTL);

	/* disable IRQ */
	val = old_ctl | UART_CTL_EN;
	val &= ~(UART_CTL_TXIE | UART_CTL_RXIE);
	owl_serial_write(port, val, UART_CTL);

	uart_console_write(port, s, count, owl_console_putchar);

	/* wait until all content have been sent out */
	while (owl_serial_read(port, UART_STAT) & UART_STAT_UTBB) {
		if (timeout >= OWL_UART_CONSOLE_FLUSH_TIMEOUT) {
			/* something error, reset the uart */
			owl_serial_reset(port);
			break;
		}
		timeout++;
		udelay(1);
	}

	/* clear IRQ pending */
	val = owl_serial_read(port, UART_STAT);
	val |= UART_STAT_TIP | UART_STAT_RIP;
	owl_serial_write(port, val, UART_STAT);

	/* restore old ctl */
	owl_serial_write(port, old_ctl, UART_CTL);

	if (aport->clk)
		clk_disable(aport->clk);

	if (locked)
		spin_unlock(&port->lock);

	local_irq_restore(flags);
}

static int owl_console_setup(struct console *co, char *options)
{
	struct owl_uart_port *aport;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (unlikely(co->index >= OWL_MAX_UART || co->index < 0))
		return -ENXIO;

	aport = owl_ports[co->index];
	if (aport == NULL)
		return -ENODEV;

	aport->port.cons = co;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&aport->port, co, baud, parity, bits, flow);
}

static struct uart_driver owl_uart_driver;
static struct console owl_console = {
	.name = OWL_SERIAL_NAME,
	.write = owl_console_write,
	.device = uart_console_device,
	.setup = owl_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &owl_uart_driver,
};

#define OWL_CONSOLE	(&owl_console)

#else
#define OWL_CONSOLE	NULL
#endif

static struct uart_driver owl_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = DRIVER_NAME,
	.dev_name = OWL_SERIAL_NAME,
	.nr = OWL_MAX_UART,
	.cons = OWL_CONSOLE,
	.major = OWL_SERIAL_MAJOR,
	.minor = OWL_SERIAL_MINOR,
};

#ifdef CONFIG_PM_SLEEP
static int owl_serial_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct owl_uart_port *aport = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "suspend port, UART_CTL 0x%x\n",
		owl_serial_read(&aport->port, UART_CTL));

	return uart_suspend_port(&owl_uart_driver, &aport->port);;
}

static int owl_serial_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct owl_uart_port *aport = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "resume port, UART_CTL 0x%x\n",
		owl_serial_read(&aport->port, UART_CTL));

	return uart_resume_port(&owl_uart_driver, &aport->port);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops owl_serial_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(owl_serial_suspend, owl_serial_resume)
};

static int owl_serial_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct owl_uart_port *aport;
	struct uart_port *port;
	struct resource *res;
	int id, ret;

	aport = devm_kzalloc(&pdev->dev, sizeof(*aport), GFP_KERNEL);
	if (!aport)
		return -ENOMEM;

	if (np)
		id = of_alias_get_id(np, "serial");
	else
		id = pdev->id;

	if (id < 0 || id >= OWL_MAX_UART) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", id);
		return -ENODEV;
	}

	dev_info(&pdev->dev, "detected port %d\n", id);

	port = &aport->port;
	port->line = id;
	port->type = PORT_OWL;
	port->iotype = UPIO_MEM;
	port->flags = UPF_BOOT_AUTOCONF;
	port->ops = &owl_uart_pops;
	port->fifosize = OWL_UART_FIFO_SIZE;
	port->dev = &pdev->dev;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	port->mapbase = res->start;
	port->membase = devm_ioremap_resource(&pdev->dev, res);

	port->irq = platform_get_irq(pdev, 0);
	if (!port->irq)
		return -ENODEV;

	aport->clk = devm_clk_get(&pdev->dev, "uart");
	if (IS_ERR(aport->clk)) {
		dev_err(&pdev->dev, "unable	to get UART clock\n");
		return PTR_ERR(aport->clk);
	}

	clk_prepare_enable(aport->clk);

	aport->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(aport->rst)) {
		dev_err(&pdev->dev, "Couldn't get the reset\n");
		return PTR_ERR(aport->rst);
	}

	/* Reset the UART controller to clear all previous status.*/
	reset_control_assert(aport->rst);
	udelay(10);
	reset_control_deassert(aport->rst);

	init_timer(&aport->rx_dma_timer);

	owl_ports[port->line] = aport;

	platform_set_drvdata(pdev, aport);

	ret = uart_add_one_port(&owl_uart_driver, port);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add uart port, err %d\n", ret);
		return ret;
	}

	return ret;
}

static int owl_serial_remove(struct platform_device *pdev)
{
	struct owl_uart_port *aport = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	platform_set_drvdata(pdev, NULL);

	uart_remove_one_port(&owl_uart_driver, &aport->port);

	clk_disable_unprepare(aport->clk);

	return 0;
}

static const struct of_device_id owl_serial_dt_ids[] = {
	{ .compatible = "actions,s900-serial" },
	{ .compatible = "actions,s700-serial" },
	{ }
};

MODULE_DEVICE_TABLE(of, owl_serial_dt_ids);

static struct platform_driver owl_serial_platform_driver = {
	.probe		= owl_serial_probe,
	.remove		= owl_serial_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
		.pm		= &owl_serial_pm_ops,
		.of_match_table = of_match_ptr(owl_serial_dt_ids),
	},
};

static int owl_serial_init(void)
{
	int ret;

	ret = uart_register_driver(&owl_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&owl_serial_platform_driver);
	if (ret)
		uart_unregister_driver(&owl_uart_driver);

	pr_info("[OWL] serial driver initialized\n");

	return ret;
}

static void __exit owl_serial_exit(void)
{
	platform_driver_unregister(&owl_serial_platform_driver);
	uart_unregister_driver(&owl_uart_driver);
}

module_init(owl_serial_init);
module_exit(owl_serial_exit);

MODULE_AUTHOR("David Liu <liuwei@actions-semi.com>");
MODULE_DESCRIPTION("serial driver for Actions OWL SOC family");
MODULE_LICENSE("GPL");
