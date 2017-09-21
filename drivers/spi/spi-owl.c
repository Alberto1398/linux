/*
 * Actions OWL SoCs SPI master controller driver
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/ctype.h>

/* debug stuff */
#define OWL_SPI_DBG_LEVEL_OFF		0
#define OWL_SPI_DBG_LEVEL_ON		1
#define OWL_SPI_DBG_LEVEL_VERBOSE	2
#define OWL_SPI_DEFAULT_DBG_LEVEL	OWL_SPI_DBG_LEVEL_VERBOSE

/* SPI controller registers */
#define SPI_CTL				0x00
#define SPI_CLKDIV			0x04
#define SPI_STAT			0x08
#define SPI_RXDAT			0x0C
#define SPI_TXDAT			0x10
#define SPI_TCNT			0x14
#define SPI_SEED			0x18
#define SPI_TXCR			0x1C
#define SPI_RXCR			0x20

/* SPI_CTL */
#define SPI_CTL_SDT_MASK		(0x7 << 29)
#define SPI_CTL_SDT(x)			(((x) & 0x7) << 29)
#define SPI_CTL_BM				(0x1 << 28)
#define SPI_CTL_GM				(0x1 << 27)
#define SPI_CTL_CEB				(0x1 << 26)
#define SPI_CTL_RANEN(x)		(0x1 << 24)
#define SPI_CTL_RDIC_MASK		(0x3 << 22)
#define SPI_CTL_RDIC(x)			(((x) & 0x3) << 22)
#define SPI_CTL_TDIC_MASK		(0x3 << 20)
#define SPI_CTL_TDIC(x)			(((x) & 0x3) << 20)
#define SPI_CTL_TWME			(0x1 << 19)
#define SPI_CTL_EN				(0x1 << 18)
#define SPI_CTL_RWC_MASK		(0x3 << 16)
#define SPI_CTL_RWC(x)			(((x) & 0x3) << 16)
#define SPI_CTL_DTS			(0x1 << 15)
#define SPI_CTL_SSATEN			(0x1 << 14)
#define SPI_CTL_DM_MASK			(0x3 << 12)
#define SPI_CTL_DM(x)			(((x) & 0x3) << 12)
#define SPI_CTL_LBT			(0x1 << 11)
#define SPI_CTL_MS			(0x1 << 10)
#define SPI_CTL_DAWS_MASK		(0x3 << 8)
#define SPI_CTL_DAWS(x)			(((x) & 0x3) << 8)
#define		SPI_CTL_DAWS_8BIT		(SPI_CTL_DAWS(0))
#define		SPI_CTL_DAWS_16BIT		(SPI_CTL_DAWS(1))
#define		SPI_CTL_DAWS_32BIT		(SPI_CTL_DAWS(2))
#define SPI_CTL_CPOS_MASK		(0x3 << 6)
#define SPI_CTL_CPOS(x)			(((x) & 0x3) << 6)
#define		SPI_CTL_CPOS_CPHA		(0x1 << 7)
#define		SPI_CTL_CPOS_CPOL		(0x1 << 6)
#define SPI_CTL_LMFS				(0x1 << 5)
#define SPI_CTL_SSCO				(0x1 << 4)
#define SPI_CTL_TIEN				(0x1 << 3)
#define SPI_CTL_RIEN				(0x1 << 2)
#define SPI_CTL_TDEN				(0x1 << 1)
#define SPI_CTL_RDEN				(0x1 << 0)

/* SPI_CLKDIV */
#define SPI_CLKDIV_CLKDIV_MASK		(0x3FF << 0)
#define SPI_CLKDIV_CLKDIV(x)		(((x) & 0x3FF) << 0)

/******************************************************************************/
/*SPI_STAT*/
/*bit 10-31 Reserved*/
#define SPI_STAT_TFEM			(0x1 << 9)
#define SPI_STAT_RFFU			(0x1 << 8)
#define SPI_STAT_TFFU			(0x1 << 7)
#define SPI_STAT_RFEM			(0x1 << 6)
#define SPI_STAT_TFER			(0x1 << 5)
#define SPI_STAT_RFER			(0x1 << 4)
#define SPI_STAT_BEB			(0x1 << 3)
#define SPI_STAT_TCOM			(0x1 << 2)
#define SPI_STAT_TIP			(0x1 << 1)
#define SPI_STAT_PIP			(0x1 << 0)

#define msecs_to_loops(t)		(loops_per_jiffy / 1000 * HZ * t)

static int debug_level = OWL_SPI_DEFAULT_DBG_LEVEL;
module_param(debug_level, uint, 0644);
MODULE_PARM_DESC(debug_level, "module debug level (0=off,1=on,2=verbose)");

/*
 * Debug macros
 */
#define spi_dbg(master, format, args...)	\
	do { \
		if (debug_level >= OWL_SPI_DBG_LEVEL_ON) \
			dev_dbg(&(master)->dev, \
				"[spi%d] %s: " format, \
				(master)->bus_num, __func__, ##args); \
	} while (0)

#define spi_vdbg(master, format, args...)	\
	do { \
		if (debug_level >= OWL_SPI_DBG_LEVEL_VERBOSE) \
			dev_dbg(&(master)->dev, \
				"[spi%d] %s: " format, \
				(master)->bus_num, __func__, ##args); \
	} while (0)

#define spi_err(master, format, args...) \
	dev_err(&(master)->dev, "[spi%d] " format, \
		(spi_dev)->adapter.nr, ##args)

#define spi_warn(master, format, args...) \
	dev_warn(&(master)->dev, "[spi%d] " format, \
		(master)->bus_num, ##args)

#define spi_info(master, format, args...) \
	dev_info(&(master)->dev, "[spi%d] " format, \
		(master)->bus_num, ##args)


struct owl_spi_data {
	struct spi_master	*master;
	spinlock_t		lock;

	struct clk		*clk;
	void __iomem		*base;
	unsigned long		phys;

	unsigned int		cur_speed;
	unsigned int		cur_mode;
	unsigned int		cur_bits_per_word;

	struct completion	xfer_completion;
};

static inline unsigned int owl_spi_readl(struct owl_spi_data *aspi,
			unsigned int reg)
{
	return __raw_readl(aspi->base + reg);
}

static inline void owl_spi_writel(struct owl_spi_data *aspi,
		unsigned int val, unsigned int reg)
{
	__raw_writel(val, aspi->base + reg);
}

#ifdef DEBUG
static void owl_spi_dump_regs(struct owl_spi_data *aspi)
{
	spi_dbg(aspi->master, "dump phys %08x regs:\n"
		"  ctl:      %.8x  clkdiv: %.8x  stat:    %.8x\n",
		(u32)aspi->phys,
		owl_spi_readl(aspi, SPI_CTL),
		owl_spi_readl(aspi, SPI_CLKDIV),
		owl_spi_readl(aspi, SPI_STAT));
}

static void owl_spi_dump_mem(char *label, const void *base, int len)
{
	int i, j;
	char *data = base;
	char buf[10], line[80];

	/* only for verbose debug */
	if (debug_level < OWL_SPI_DBG_LEVEL_VERBOSE)
		return;

	pr_debug("%s: dump of %d bytes of data at 0x%p\n",
		label, len, data);

	for (i = 0; i < len; i += 16) {
		sprintf(line, "%.8x: ", i);
		for (j = 0; j < 16; j++) {
			if ((i + j < len))
				sprintf(buf, "%02x ", data[i + j]);
			else
				sprintf(buf, "   ");
			strcat(line, buf);
		}
		strcat(line, " ");
		buf[1] = 0;
		for (j = 0; (j < 16) && (i + j < len); j++) {
			buf[0] = isprint(data[i + j]) ? data[i + j] : '.';
			strcat(line, buf);
		}
		pr_debug("%s\n", line);
	}
}
#else
static void owl_spi_dump_regs(struct owl_spi_data *aspi)
{
}

static inline void owl_spi_dump_mem(char *label, const void *base, int len)
{
}
#endif

static void enable_cs(struct spi_device *spi)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(spi->master);
	unsigned int val;

	if (gpio_is_valid(spi->cs_gpio))
		gpio_direction_output(spi->cs_gpio, 0);

	/* enable spi controller */
	val = owl_spi_readl(aspi, SPI_CTL);
	val &= ~SPI_CTL_SSCO;
	val |= SPI_CTL_EN;
	owl_spi_writel(aspi, val, SPI_CTL);
}

static void disable_cs(struct spi_device *spi)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(spi->master);
	unsigned int val;

	if (gpio_is_valid(spi->cs_gpio))
		gpio_direction_output(spi->cs_gpio, 1);

	/* disable spi controller */
	val = owl_spi_readl(aspi, SPI_CTL);
	val |= SPI_CTL_SSCO;
	val &= ~SPI_CTL_EN;
	owl_spi_writel(aspi, val, SPI_CTL);
}

static int owl_spi_setup_cs(struct spi_device *spi)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(spi->master);
	unsigned int val;
	int ret;

	if (gpio_is_valid(spi->cs_gpio)) {
		ret = gpio_request_one(spi->cs_gpio, GPIOF_OUT_INIT_HIGH,
				dev_name(&spi->dev));
		if (ret) {
			dev_err(&spi->dev,
				"Failed to get /CS gpio [%d]: %d\n",
				spi->cs_gpio, ret);
			return ret;
		}
	}

	val = owl_spi_readl(aspi, SPI_CTL);
	val |= SPI_CTL_SSCO;
	val &= ~SPI_CTL_EN;
	owl_spi_writel(aspi, val, SPI_CTL);

	return 0;
}

static void owl_spi_cleanup_cs(struct spi_device *spi)
{
	if (gpio_is_valid(spi->cs_gpio))
		gpio_free(spi->cs_gpio);
}

static int owl_spi_baudrate_set(struct owl_spi_data *aspi, unsigned int speed)
{
	u32 spi_source_clk_hz;
	u32 clk_div;

	spi_source_clk_hz = clk_get_rate(aspi->clk);

	/* setup SPI clock register */
	/* SPICLK = HCLK/(CLKDIV*2) */
	clk_div = (spi_source_clk_hz + (2 * speed) - 1) / (speed) / 2;
	if (clk_div == 0)
		clk_div = 1;

	pr_debug("owl_spi: required speed = %d\n", speed);
	pr_debug("owl_spi: spi clock = %d KHz(hclk = %d,clk_div = %d)\n",
		spi_source_clk_hz / (clk_div * 2) / 1000,
		spi_source_clk_hz, clk_div);

	owl_spi_writel(aspi, SPI_CLKDIV_CLKDIV(clk_div), SPI_CLKDIV);

	return 0;
}

static inline int owl_spi_config(struct owl_spi_data *aspi)
{
	unsigned int val, mode;

	mode = aspi->cur_mode;
	val = owl_spi_readl(aspi, SPI_CTL);

	val &= ~(SPI_CTL_CPOS_MASK | SPI_CTL_LMFS | SPI_CTL_LBT |
			SPI_CTL_DAWS_MASK | SPI_CTL_CEB);

	if (mode & SPI_CPOL)
		val |= SPI_CTL_CPOS_CPOL;

	if (mode & SPI_CPHA)
		val |= SPI_CTL_CPOS_CPHA;

	if (mode & SPI_LSB_FIRST)
		val |= SPI_CTL_LMFS;
	else
		val |= SPI_CTL_CEB;

	if (mode & SPI_LOOP)
		val |= SPI_CTL_LBT;

	switch (aspi->cur_bits_per_word) {
	case 16:
		val |= SPI_CTL_DAWS_16BIT;
		break;
	case 32:
		val |= SPI_CTL_DAWS_32BIT;
		break;
	default:
		val |= SPI_CTL_DAWS_8BIT;
		break;
	}

	owl_spi_writel(aspi, val, SPI_CTL);

	owl_spi_baudrate_set(aspi, aspi->cur_speed);

	return 0;
}

static inline void spi_clear_stat(struct owl_spi_data *aspi)
{
	owl_spi_writel(aspi,
		SPI_STAT_TFER	/* clear the rx FIFO */
		| SPI_STAT_RFER	/* clear the tx FIFO */
		| SPI_STAT_BEB	/* clear the Bus error bit */
		| SPI_STAT_TCOM	/* clear the transfer complete bit */
		| SPI_STAT_TIP	/* clear the tx IRQ pending bit */
		| SPI_STAT_PIP,	/* clear the rx IRQ pending bit */
		SPI_STAT);
}

static inline int owl_spi_wait_tcom(struct owl_spi_data *aspi)
{
	unsigned int stat;
	int timeout;

	/* transfer timeout: 500ms, 8 cycles per loop */
	timeout = msecs_to_loops(500 / 8);

	do {
		stat = owl_spi_readl(aspi, SPI_STAT);
	} while (!(stat & SPI_STAT_TCOM) && timeout--);

	if (timeout < 0) {
		pr_err("Error: spi transfer wait complete timeout\n");
		owl_spi_dump_regs(aspi);
		return -1;
	}

	/* clear transfer complete flag */
	owl_spi_writel(aspi, stat | SPI_STAT_TCOM, SPI_STAT);

	return 0;
}

static int owl_spi_write_read_8bit(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	unsigned int ctl;
	unsigned int count = xfer->len;
	const u8 *tx_buf = xfer->tx_buf;
	u8 *rx_buf = xfer->rx_buf;

	ctl = owl_spi_readl(aspi, SPI_CTL);
	ctl &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK |
			SPI_CTL_DTS | SPI_CTL_DAWS_MASK | SPI_CTL_TIEN |
			SPI_CTL_RIEN | SPI_CTL_TDEN | SPI_CTL_RDEN);
	ctl |= SPI_CTL_RWC(0) | SPI_CTL_DAWS(0);

	if (aspi->cur_speed > 20000000)
		ctl |= SPI_CTL_SDT(1);

	owl_spi_writel(aspi, ctl, SPI_CTL);

	do {
		if (tx_buf)
			owl_spi_writel(aspi, *tx_buf++, SPI_TXDAT);
		else
			owl_spi_writel(aspi, 0, SPI_TXDAT);

		if (owl_spi_wait_tcom(aspi) < 0) {
			pr_err("SPI: TXS timed out\n");
			return -ETIMEDOUT;
		}

		if (rx_buf)
			*rx_buf++ = owl_spi_readl(aspi, SPI_RXDAT) & 0xff;

		count -= 1;
	} while (count);

	return 0;
}

static int owl_spi_write_read_16bit(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	unsigned int ctl;
	unsigned int count = xfer->len;
	const u16 *tx_buf = xfer->tx_buf;
	u16 *rx_buf = xfer->rx_buf;

	ctl = owl_spi_readl(aspi, SPI_CTL);
	ctl &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK |
			SPI_CTL_DTS | SPI_CTL_DAWS_MASK | SPI_CTL_TIEN |
			SPI_CTL_RIEN | SPI_CTL_TDEN | SPI_CTL_RDEN);
	ctl |= SPI_CTL_RWC(0) | SPI_CTL_DAWS(1);

	if (aspi->cur_speed > 20000000)
		ctl |= SPI_CTL_SDT(1);

	owl_spi_writel(aspi, ctl, SPI_CTL);

	do {
		if (tx_buf)
			owl_spi_writel(aspi, *tx_buf++, SPI_TXDAT);
		else
			owl_spi_writel(aspi, 0, SPI_TXDAT);

		if (owl_spi_wait_tcom(aspi) < 0) {
			pr_err("SPI: TXS timed out\n");
			return -ETIMEDOUT;
		}

		if (rx_buf)
			*rx_buf++ = owl_spi_readl(aspi, SPI_RXDAT) & 0xffff;

		count -= 2;
	} while (count);

	return 0;
}

static int owl_spi_write_read_32bit(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	unsigned int ctl;
	unsigned int count = xfer->len;
	const u32 *tx_buf = xfer->tx_buf;
	u32 *rx_buf = xfer->rx_buf;

	ctl = owl_spi_readl(aspi, SPI_CTL);
	ctl &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK |
			SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN |
			SPI_CTL_TDEN | SPI_CTL_RDEN);
	ctl |= SPI_CTL_RWC(0) | SPI_CTL_DAWS(2);

	if (aspi->cur_speed > 20000000)
		ctl |= SPI_CTL_SDT(1);

	owl_spi_writel(aspi, ctl, SPI_CTL);

	do {
		if (tx_buf)
			owl_spi_writel(aspi, *tx_buf++, SPI_TXDAT);
		else
			owl_spi_writel(aspi, 0, SPI_TXDAT);

		if (owl_spi_wait_tcom(aspi) < 0) {
			pr_err("SPI: TXS timed out\n");
			return -ETIMEDOUT;
		}

		if (rx_buf)
			*rx_buf++ = owl_spi_readl(aspi, SPI_RXDAT);

		count -= 4;
	} while (count);

	return 0;
}


static int owl_spi_write_read(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	int word_len, len, count;

	word_len = aspi->cur_bits_per_word;
	len = xfer->len;

	spi_clear_stat(aspi);

	switch (word_len) {
	case 8:
		count = owl_spi_write_read_8bit(aspi, xfer);
		break;
	case 16:
		count = owl_spi_write_read_16bit(aspi, xfer);
		break;
	case 32:
		count = owl_spi_write_read_32bit(aspi, xfer);
		break;
	default:
		count = 0;
		break;
	}

	if (count < 0)
		return count;

	return len - count;
}

static int owl_spi_handle_msg(struct owl_spi_data *aspi,
		struct spi_message *msg)
{
	struct spi_device *spi = msg->spi;
	struct spi_transfer *xfer = NULL;
	int ret, status = 0;
	u32 speed;
	u8 bits_per_word;

	spi_dbg(aspi->master, "%d:\n", __LINE__);

	msg->status = -EINPROGRESS;

	aspi->cur_bits_per_word = spi->bits_per_word;
	aspi->cur_speed = spi->max_speed_hz;
	aspi->cur_mode = spi->mode;
	owl_spi_config(aspi);

	enable_cs(spi);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* Only BPW and Speed may change across transfers */
		bits_per_word = xfer->bits_per_word ? : spi->bits_per_word;
		speed = xfer->speed_hz ? : spi->max_speed_hz;

		spi_dbg(aspi->master, "%d: bits_per_word %d, len %d\n", __LINE__,
			bits_per_word, xfer->len);

		if ((bits_per_word != 8) && (bits_per_word != 16) &&
			(bits_per_word != 32)) {
			dev_err(&spi->dev, "invalid bits per word - %d!\n",
					bits_per_word);
			status = -EINVAL;
			goto out;
		}

		/*transfer length should be alignd according to bits_per_word*/
		if (xfer->len & ((bits_per_word >> 3) - 1)) {
			dev_err(&spi->dev, "bad transfer length - %d!\n",
					xfer->len);
			status = -EINVAL;
			goto out;
		}

		if (bits_per_word != aspi->cur_bits_per_word ||
				speed != aspi->cur_speed) {
			aspi->cur_bits_per_word = bits_per_word;
			aspi->cur_speed = speed;
			owl_spi_config(aspi);
		}

		if (xfer->tx_buf)
			owl_spi_dump_mem("[msg] tx buf",
					xfer->tx_buf, xfer->len);

		if (xfer->cs_change)
			enable_cs(spi);

		ret = owl_spi_write_read(aspi, xfer);
		if (unlikely(ret < 0)) {
			status = ret;
			goto out;
		}

		if (xfer->rx_buf)
			owl_spi_dump_mem("[msg] rx buf", xfer->rx_buf,
					xfer->len);

		msg->actual_length += ret;

		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);

		if (xfer->cs_change)
			disable_cs(spi);
	}

out:
	disable_cs(spi);

	msg->status = status;
	if (status < 0)
		dev_warn(&spi->dev, "spi transfer failed with %d\n", status);

	spi_finalize_current_message(aspi->master);

	spi_dbg(aspi->master, "%d:\n", __LINE__);

	return status;
}

static int owl_spi_transfer_one_message(struct spi_master *master,
				      struct spi_message *msg)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(master);
	int ret;

	ret = owl_spi_handle_msg(aspi, msg);

	return ret;
}

static int owl_spi_setup(struct spi_device *spi)
{
	int ret = 0;

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	ret = owl_spi_setup_cs(spi);
	if (ret)
		return ret;

	return ret;
}

static void owl_spi_cleanup(struct spi_device *spi)
{
	owl_spi_cleanup_cs(spi);
}

/* init hardware by default value */
static int owl_spi_init_hw(struct owl_spi_data *aspi)
{
	unsigned int val;

	val = owl_spi_readl(aspi, SPI_CTL);
	val &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK |
		SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN | SPI_CTL_TDEN |
		SPI_CTL_RDEN);
	val |= SPI_CTL_EN | SPI_CTL_SSCO;
	owl_spi_writel(aspi, val, SPI_CTL);

	spi_clear_stat(aspi);

	/* 2MHz by default */
	owl_spi_baudrate_set(aspi, 2000000);

	return 0;
}

static int owl_spi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spi_master *master;
	struct owl_spi_data *aspi;
	struct resource *res;
	int ret, num_cs;

	master = spi_alloc_master(&pdev->dev, sizeof(*aspi));
	if (!master)
		return -ENOMEM;

	aspi = spi_master_get_devdata(master);
	master->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, master);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto free_master;
	}
	aspi->phys = res->start;

	aspi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(aspi->base)) {
		ret = PTR_ERR(aspi->base);
		goto free_master;
	}

	aspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(aspi->clk)) {
		dev_err(&pdev->dev, "spi clock not found.\n");
		ret = PTR_ERR(aspi->clk);
		goto free_master;
	}

	ret = clk_prepare_enable(aspi->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APB clock.\n");
		goto free_master;
	}

	/* SPI controller initializations */
	owl_spi_init_hw(aspi);

	ret = of_property_read_u32(np, "num-cs", &num_cs);
	if (ret < 0)
		master->num_chipselect = 1;
	else
		master->num_chipselect = num_cs;

	master->setup = owl_spi_setup;
	master->cleanup = owl_spi_cleanup;
	master->transfer_one_message = owl_spi_transfer_one_message;
	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->flags = SPI_MASTER_HALF_DUPLEX;
	master->bits_per_word_mask = BIT(32 - 1) | BIT(16 - 1) | BIT(8 - 1);
	master->dev.of_node = np;

	aspi->master = master;
	spin_lock_init(&aspi->lock);
	init_completion(&aspi->xfer_completion);

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "cannot register SPI master\n");
		goto disable_clk;
	}

	return 0;

disable_clk:
	clk_disable_unprepare(aspi->clk);
free_master:
	spi_master_put(master);
	return ret;
}

static int owl_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = dev_get_drvdata(&pdev->dev);
	struct owl_spi_data *aspi = spi_master_get_devdata(master);

	spi_unregister_master(master);
	clk_disable_unprepare(aspi->clk);

	return 0;
}

static const struct of_device_id owl_spi_dt_ids[] = {
	{ .compatible = "actions,s500-spi" },
	{ },
};

static struct platform_driver owl_spi_driver = {
	.probe = owl_spi_probe,
	.remove = owl_spi_remove,
	.driver = {
		.name = "spi-owl",
		.of_match_table = of_match_ptr(owl_spi_dt_ids),
	},
};

static int __init owl_spi_init(void)
{
	pr_info("[OWL] SPI controller initialization\n");

	return platform_driver_register(&owl_spi_driver);
}

static void __exit owl_spi_exit(void)
{
	platform_driver_unregister(&owl_spi_driver);
}

subsys_initcall(owl_spi_init);
module_exit(owl_spi_exit);

MODULE_AUTHOR("David Liu <liuwei@actions-semi.com>");
MODULE_DESCRIPTION("SPI controller driver for Actions SOC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spi-owl");
