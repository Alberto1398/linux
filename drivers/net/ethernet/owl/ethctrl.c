/*
 * Actions OWL ethernet driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * ouyang <ouyang@actions-semi.com>
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <asm/io.h>
#include <linux/clk.h>

#include <linux/ctype.h>
#include <linux/reset.h>

#include "ethctrl.h"
#include <linux/stddef.h>
#include <linux/of_mdio.h>
#include <linux/io.h>

#define SETUP_FRAME_LEN 192
#define SETUP_FRAME_PAD 16

/* 0xD0, reserve 16 bytes for align */
#define SETUP_FRAME_RESV_LEN  (SETUP_FRAME_LEN + SETUP_FRAME_PAD)

#define EC_SKB_ALIGN_BITS_MASK  0x3
#define EC_SKB_ALIGNED  0x4

/* 'mii -v' will read first 8 phy registers to get status */
#define PHY_REG_ADDR_RANGE 0x7
#define PHY_ADDR_MASK (PHY_ADDR_LIMIT - 1)
#define PHY_REG_NUM_MASK 0x7
#define PHY_REG_BITS 16

#ifdef EC_DEBUG
#define NET_MSG_ENABLE  0
/*( NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK )*/
#else
#define NET_MSG_ENABLE  0
#endif
#define EC_TX_TIMEOUT (2*HZ)	/* 2s */
#define MAX_DEVICES 1

static struct delayed_work resume_work;
static struct workqueue_struct *resume_queue;

#ifdef DETECT_POWER_SAVE
static struct workqueue_struct *power_save_queue;
#endif

static char g_default_mac_addr[ETH_MAC_LEN] = {
	0x00, 0x18, 0xFE, 0x61, 0xD5, 0xD6 };
static struct net_device *g_eth_dev[MAX_DEVICES];
static struct clk *ethernet_clk;
static int clk_count;

static char *macaddr = "?";
module_param(macaddr, charp, 0);
MODULE_PARM_DESC(macaddr, "MAC address");

static const char g_banner[] __initdata = KERN_INFO "Ethernet controller driver\
    for Actions GL5203, @2012 Actions.\n";

#ifdef ETH_TEST_MODE
static void print_frame_data(void *frame, int len);
static struct sk_buff *get_skb_aligned(unsigned int len);
static int ec_netdev_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void print_mac_register(ec_priv_t *ecp);
static void print_phy_register(ec_priv_t *ecp);
static void set_mac_according_aneg(ec_priv_t *ecp);
#endif

static int owl_mdio_init(struct net_device *dev);
static void owl_mdio_remove(struct net_device *dev);

ssize_t netif_msg_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	ec_priv_t *ecp = netdev_priv(ndev);

	return sprintf(buf, "0x%x\n", ecp->msg_enable);
}

ssize_t netif_msg_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct net_device *ndev = to_net_dev(dev);
	ec_priv_t *ecp = netdev_priv(ndev);
	char *endp;
	unsigned int new;
	int ret = -EINVAL;

	new = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		goto err;

	ecp->msg_enable = (int)new;
	return count;
err:
	return ret;
}

DEVICE_ATTR(netif_msg, S_IRUGO | S_IWUSR, netif_msg_show, netif_msg_store);

#ifdef ETH_TEST_MODE
/* default -1 denote send single frame, [0, n]*/
/* denote interval n us between 2 frames */
static long continuity_interval = -1;

ssize_t continuity_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "continuity_interval: %ld\n", continuity_interval);
}

ssize_t continuity_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	char *endp;
	unsigned int new;
	int ret = -EINVAL;

	new = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		goto err;

	continuity_interval = (long)new;
	return count;
err:
	return ret;
}

DEVICE_ATTR(continuity, S_IRUGO | S_IWUGO, continuity_show, continuity_store);

ssize_t send_pattern_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return sprintf(buf,
		       "pattern 0: all zero, 1: all one, 2: pseudo-random\n");
}

static void gen_frame_pattern(char *buf, int len, int pattern)
{
	switch (pattern) {
	case 0:
		memset(buf, 0, len);
		break;
	case 1:
		memset(buf, 0xff, len);
		break;
	case 2:
		get_random_bytes(buf, len);
		break;
	default:
		EC_INFO("not supported pattern: %d\n", pattern);
		break;
	}
}

static int send_pattern_continuous(int n, int pat, struct net_device *ndev)
{
	int ret = -EINVAL;
	struct sk_buff *skb;
	int i = 0;

	while (i++ < n) {
		skb = get_skb_aligned(PKG_MAX_LEN)£»
		if (NULL == skb) {
			EC_INFO("no memory!\n");
			goto err;
		}
		skb->len = 1500;
		gen_frame_pattern(skb->data, skb->len, pat);

		ec_netdev_start_xmit(skb, ndev);
		if (continuity_interval > 0)
			udelay(continuity_interval);
	}
	return 0;
err:
	return ret;
}

ssize_t send_pattern_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct net_device *ndev = to_net_dev(dev);
	char *endp;
	unsigned int pat = 5;
	int ret = -EINVAL;
	struct sk_buff *skb;

	pat = simple_strtoul(buf, &endp, 0);
	if ((endp == buf) || (pat > 2))
		goto err;
	skb = get_skb_aligned(PKG_MAX_LEN)£»
	if (NULL == skb) {
		EC_INFO("no memory!\n");
		goto err;
	}
	skb->len = 1500;

	gen_frame_pattern(skb->data, skb->len, (int)pat);

	ec_netdev_start_xmit(skb, ndev);

	if (continuity_interval >= 0) {
		if (continuity_interval > 0)
			udelay(continuity_interval);
		send_pattern_continuous(TX_RING_SIZE - 1, (int)pat, ndev);
	}

	return count;
err:
	return ret;
}

DEVICE_ATTR(send_pattern, S_IRUGO | S_IWUGO,
	    send_pattern_show, send_pattern_store);

ssize_t test_mode_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return sprintf(buf, "test mode: 10 or 100 Mbps\n");
}

ssize_t test_mode_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct net_device *ndev = to_net_dev(dev);
	ec_priv_t *ecp = netdev_priv(ndev);
	char *endp;
	unsigned int mode = 0;
	int ret = -EINVAL;
	ushort temp;

	mode = simple_strtoul(buf, &endp, 0);
	if ((endp == buf) || ((mode != 10) && (mode != 100)))
		goto err;

	ecp->test_mode = mode;
	ecp->speed = mode;
	if (ETH_SPEED_10M == ecp->speed) {
		ecp->duplex = ETH_DUP_FULL;
		temp = read_phy_reg(ecp, PHY_REG_FTC1);
		temp &= ~0x40;	/*clear bit 6*/
		temp |= 0x01;	/*bit 0: force 10M link; bit6: force 100M*/
		write_phy_reg(ecp, PHY_REG_FTC1, temp);	/*Set bit0*/
	} else {
		ecp->duplex = ETH_DUP_FULL;
		temp = read_phy_reg(ecp, PHY_REG_FTC1);
		temp &= ~0x01;	/*clear bit 0, 100M*/
		temp |= 0x40;	/* bit6: force 100M*/
		write_phy_reg(ecp, PHY_REG_FTC1, temp);	/*clear bit0*/

		/* adjust tx current */
		temp = read_phy_reg(ecp, 0x12);
		temp &= ~0x780;

		temp |= 0x100;

		write_phy_reg(ecp, 0x12, temp);
	}

	EC_INFO("PHY_REG_FTC1 = 0x%x\n", (u32) read_phy_reg(ecp, PHY_REG_FTC1));
	EC_INFO("PHY_REG_TXPN = 0x%x\n", (u32) read_phy_reg(ecp, 0x12));

	/* shut auto-negoiation */
	temp = read_phy_reg(ecp, MII_BMCR);
	temp &= ~BMCR_ANENABLE;
	write_phy_reg(ecp, MII_BMCR, temp);

	set_mac_according_aneg(ecp);

	EC_INFO("new_duplex = 0x%x\n", ecp->duplex);
	EC_INFO("new_speed = 0x%x\n", ecp->speed);
	print_phy_register(ecp);
	print_mac_register(ecp);

	return count;
err:
	return ret;
}

DEVICE_ATTR(test_mode, S_IRUGO | S_IWUGO, test_mode_show, test_mode_store);
#endif /* ETH_TEST_MODE */

static int ethernet_set_ref_clk(struct clk *clk, ulong tfreq)
{
	int ret = -1;

	EC_INFO("target freq: %lu\n", tfreq);

	ret = clk_set_rate(clk, tfreq);
	if (ret)
		EC_ERR("wrong RMII_REF_CLK: %lu\n", tfreq);

	return ret;
}

unsigned short read_phy_reg(ec_priv_t *ecp, unsigned short reg_addr)
{
	if (ecp->phy) {
		if (ecp->phy->bus)
			return ecp->phy->bus->read(ecp->phy->bus, reg_addr,
						   reg_addr);
	}
	return 0;
}

/**
 * write_phy_reg - MII interface  to write  @val to @reg_addr register of phy at @phy_addr
 * return zero if success, negative value if fail
 * may be used by other standard ethernet phy
 */
int write_phy_reg(ec_priv_t *ecp, unsigned short reg_addr, unsigned short val)
{
	if (ecp->phy) {
		if (ecp->phy->bus)
			ecp->phy->bus->write(ecp->phy->bus, reg_addr, reg_addr,
					     val);
	}
	return 0;
}

static int ethernet_clock_config(int phy_mode)
{
	ec_priv_t *ecp = netdev_priv(g_eth_dev[0]);
	struct clk *clk = ecp->clk;

	int ret = -1;
	ulong tfreq;

	EC_INFO("phy_mode: %d\n", phy_mode);

	switch (phy_mode) {
	case ETH_PHY_MODE_RMII:
		tfreq = 50 * 1000 * 1000;
		ret = ethernet_set_ref_clk(clk, tfreq);
		break;
	case ETH_PHY_MODE_SMII:
		tfreq = 125 * 1000 * 1000;
		ret = ethernet_set_ref_clk(clk, tfreq);
		break;
	default:
		EC_ERR("not support phy mode: %d\n", phy_mode);
	}

	return ret;
}

static inline void phy_reg_set_bits(ec_priv_t *ecp, unsigned short reg_addr,
				    int bits)
{
	unsigned short reg_val;

	reg_val = read_phy_reg(ecp, reg_addr);
	reg_val |= (unsigned short)bits;
	write_phy_reg(ecp, reg_addr, reg_val);
}

static inline void phy_reg_clear_bits(ec_priv_t *ecp, unsigned short reg_addr,
				      int bits)
{
	unsigned short reg_val;

	reg_val = read_phy_reg(ecp, reg_addr);
	reg_val &= ~(unsigned short)bits;
	write_phy_reg(ecp, reg_addr, reg_val);
}

static void ethernet_clock_enable(void)
{
	ec_priv_t *ecp = netdev_priv(g_eth_dev[0]);
	struct platform_device *pdev = ecp->pdev;
	struct reset_control *rst;
	int ret;
	printk("ethernet_clock_enable:%d\n", clk_count);
	if (clk_count > 0)
		return;
	clk_count++;

	ret = clk_prepare(ethernet_clk);
	EC_INFO("####ethernet_clk: %ld #####\n", clk_get_rate(ethernet_clk));
	if (ret)
		EC_ERR("prepare ethernet clock faild,errno: %d\n", -ret);
	ret = clk_prepare_enable(ethernet_clk);
	if (ret)
		EC_ERR("prepare and enable ethernet clock failed, errno: %d\n",
		       ret);

	ret = clk_enable(ethernet_clk);
	if (ret)
		EC_ERR("enable ethernet clock failed, errno: %d\n", -ret);
	udelay(100);

	/* reset ethernet clk */
	rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rst)) {
		dev_err(&pdev->dev, "Couldn't get ethernet reset\n");
		return;
	}

	/* Reset the UART controller to clear all previous status. */
	reset_control_assert(rst);
	udelay(10);
	reset_control_deassert(rst);

	udelay(100);

	ethernet_clock_config(ecp->phy_mode);
}

static void ethernet_clock_disable(void)
{
	ec_priv_t *ecp = netdev_priv(g_eth_dev[0]);
	struct platform_device *pdev = ecp->pdev;
	printk("ethernet_clock_disable:%d\n", clk_count);
	if (clk_count <= 0)
		return;
	clk_count--;
	clk_disable(ethernet_clk);
	clk_unprepare(ethernet_clk);
	devm_clk_put(&pdev->dev, ethernet_clk);

	udelay(100);
}

/* data is a ethernet frame */
static void check_icmp_sequence(const void *data, const char *msg)
{
#define ptr_to_u32(data, off) (ntohl(*(u32 *)((char *)data + off)))
	EC_INFO("-- %s -- %p, icmp: 0x%x\n", msg, (char *)data + 0x14,
	       (ptr_to_u32(data, 0x14) & 0xff));
	if ((ptr_to_u32(data, 0x14) & 0xff) == 0x01) {	/* protocol icmp 0x01*/
		EC_INFO("ICMP ");
		/*icmp type*/
		if (((ptr_to_u32(data, 0x20) >> 8) & 0xff) == 0x8)
			EC_INFO("ping echo request, ");
		else if (((ptr_to_u32(data, 0x20) >> 8) & 0xff) == 0x0)
			EC_INFO("ping echo reply, ");
		else
			EC_INFO("not ping echo request or reply, ");
		EC_INFO("sequence number: %u\n", ptr_to_u32(data, 0x28) >> 16);
	} else {
		EC_INFO("not a ICMP packet\n");
	}
}

static void print_mac_address(const char *mac)
{
	int i;
	for (i = 0; i < ETH_MAC_LEN - 1; i++)
        printk("%02x-", (unsigned int)mac[i] & 0xFF);
    printk("%02x\n", (unsigned int)mac[i] & 0xFF);
	return;
}

static int ctox(int c)
{
	int tmp;

	if (!isxdigit(c)) {
		EC_ERR("'%c' is not hex digit\n", (char)c);
		return -1;
	}

	if ((c >= '0') && (c <= '9'))
		tmp = c - '0';
	else
		tmp = (c | 0x20) - 'a' + 10;

	return tmp;
}

static int parse_mac_address(const char *mac, int len)
{
	int tmp, tmp2;
	int i = 0;
	int j = 0;
	char s[16] = "";
	int c;

	if (17 == len) {
		if (strlen(mac) > 17) {
			EC_ERR("ethernet mac address string too long\n");
			return -1;
		}
		while ((c = mac[i++]) != '\0') {
			if (c == ':')
				continue;
			s[j++] = c;
		}
		s[j] = '\0';
		EC_INFO("mac address string stripped colon: %s\n", s);
	} else if (12 == len) {
		if (strlen(mac) > 12) {
			EC_ERR("ethernet mac address string too long\n");
			return -1;
		}
		memcpy(s, mac, 12);
		s[12] = '\0';
	} else {
		EC_ERR("length of ethernet mac address is not 12 or 17\n");
		return -1;
	}

	for (i = 0; i < ETH_MAC_LEN; i++) {
		tmp = ctox(s[i * 2]);
		tmp2 = ctox(s[i * 2 + 1]);
		tmp = (tmp * 16) + tmp2;
		*(char *)(g_default_mac_addr + i) = tmp & 0xFF;
	}

	return 0;
}

static int read_mac_address(struct file *filp, char *mac, int *len)
{
	loff_t l;
	loff_t offset = 0;
	mm_segment_t fs;
	int _len, i;

	l = vfs_llseek(filp, 0, SEEK_END);
	offset = vfs_llseek(filp, 0, SEEK_SET);
	EC_INFO("file's actual len: %d\n", (int)l);
	if (l >= 17) {
		_len = 17;
	} else if (l >= 12) {
		_len = 12;
	} else {
		EC_ERR("mac address is too short\n");
		return -1;
	}
	EC_INFO("file's len to be read: %d\n", _len);

	fs = get_fs();
	set_fs(get_ds());

	l = vfs_read(filp, (char __user *)mac, (size_t) _len, &offset);
	set_fs(fs);
	EC_INFO("mac string len actually read: %d\n", (int)l);
	if (l > 12) {
		if ((mac[2] == ':') && (mac[5] == ':'))
			_len = 17;
		else
			_len = 12;
	} else if (12 == l) {
		_len = 12;
	} else {
		EC_INFO("ethernet mac address not valid: %s\n", mac);
		return -1;
	}

	*len = _len;
	mac[_len] = '\0';
	EC_INFO("ethernet mac address read from file: %s, len: %d\n", mac,
		_len);
	for (i = 0; i < _len; i++) {
		if (!isxdigit(mac[i]) && ':' != mac[i]) {
			EC_ERR("mac address has invalid char: %c\n", mac[i]);
			return -1;
		}
	}

	return 0;
}

/* if all the paths are not usable, use random mac each boot */
static const char *get_def_mac_addr(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct file *filp;
	loff_t offset = 0;
	char def_mac[20] = "";
	const char *str;
	mm_segment_t fs;
	int len;
	int ret;

#define ETH_MAC_ADDR_BURNED_PATH "/sys/miscinfo/infos/ethmac"
#define ETH_MAC_ADDR_PATH "/config/mac_address.bin"
#define ETH_MAC_ADDR_RANDDOM_PATH "/data/mac_address_random"

	filp = filp_open(ETH_MAC_ADDR_BURNED_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp)) {
		EC_INFO("file %s can't be opened\n", ETH_MAC_ADDR_BURNED_PATH);
	} else {
		if (!read_mac_address(filp, def_mac, &len)) {
			parse_mac_address(def_mac, len);
			EC_INFO("use burned mac address: ");
			print_mac_address(g_default_mac_addr);
			filp_close(filp, current->files);
			return g_default_mac_addr;
		}
		filp_close(filp, current->files);
	}

	str = of_get_property(np, "local-mac-address", NULL);
	if (str == NULL) {
		EC_INFO("no local-mac-address in dts\n");
	} else {
		EC_INFO("local-mac-address: ");
		print_mac_address(str);
		memcpy(g_default_mac_addr, str, ETH_MAC_LEN);
	}

	ret = of_property_read_string(np, "random-mac-address", &str);
	if (ret) {
		EC_INFO("no random-mac-address in dts\n");
	} else {
		EC_INFO("random-mac-address: %s\n", str);
		if (!strcmp("okay", str))
			goto random_mac;
	}

	EC_INFO("no mac burned, use default mac address: ");
	print_mac_address(g_default_mac_addr);
	return g_default_mac_addr;

random_mac:
	filp = filp_open(ETH_MAC_ADDR_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp)) {
		EC_INFO("file %s can't be opened\n", ETH_MAC_ADDR_PATH);
	} else {
		if (!read_mac_address(filp, def_mac, &len)) {
			parse_mac_address(def_mac, len);
			EC_INFO("use mac stored in file: ");
			print_mac_address(g_default_mac_addr);
			filp_close(filp, current->files);
			return g_default_mac_addr;
		}
		filp_close(filp, current->files);
	}

	get_random_bytes(def_mac, ETH_MAC_LEN);
	memcpy(g_default_mac_addr + 3, def_mac + 3, ETH_MAC_LEN - 3);

	fs = get_fs();
	set_fs(get_ds());

	filp = filp_open(ETH_MAC_ADDR_RANDDOM_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp)) {
		filp =
		    filp_open(ETH_MAC_ADDR_RANDDOM_PATH, O_WRONLY | O_CREAT,
			      0640);
		if (!IS_ERR_OR_NULL(filp)) {
			EC_INFO("use random mac generated: ");
			print_mac_address(g_default_mac_addr);
			vfs_write(filp, (char __user *)g_default_mac_addr,
				  ETH_MAC_LEN, &offset);
			filp_close(filp, current->files);
		}
	} else {
		if (vfs_read(filp, (char __user *)def_mac, ETH_MAC_LEN, &offset)
		    == ETH_MAC_LEN) {
			memcpy(g_default_mac_addr, def_mac, ETH_MAC_LEN);
			EC_INFO("use random mac stored: ");
			print_mac_address(g_default_mac_addr);
		}
		filp_close(filp, current->files);
	}

	set_fs(fs);

	return g_default_mac_addr;
}

static void print_frame_data(void *frame, int len)
{
	int i;
	unsigned char *tmp = (unsigned char *)frame;

	for (i = 0; i < len; i++) {
		printk("%02x ", (unsigned int)(*tmp));
		if (0xF == (i & 0xF))
			printk("\n");
		tmp++;
	}
	printk("\n");
}

static void print_tx_bds(ec_priv_t *priv)
{
	int i;
	ec_bd_t *tx_bds = priv->tx_bd_base;
	ec_bd_t *buf;

	printk("---- tx ring status ----\n");
	printk("tx_bd_base = 0x%p, tx_full = %u\n", priv->tx_bd_base,
	       (unsigned)priv->tx_full);
	printk("cur_tx = 0x%p, skb_cur = %u\n", priv->cur_tx,
	       (unsigned)priv->skb_cur);
	printk("dirty_tx = 0x%p, skb_dirty = %u\n", priv->dirty_tx,
	       (unsigned)priv->skb_dirty);

	printk("---- tx bds ----\n");
	printk("     status\t control\t buf addr\n");
	for (i = 0; i < TX_RING_SIZE; i++) {
		buf = &tx_bds[i];
		printk("%03d: 0x%08x\t 0x%08x\t 0x%08x\n", i,
		       (unsigned int)buf->status, (unsigned int)buf->control,
		       (unsigned int)buf->buf_addr);
	}
}

static void print_mac_register(ec_priv_t *ecp)
{
	ethregs_t *hw_regs = ecp->hwrp;

	/* CSR0~20 */
	pr_info("MAC_CSR0:0x%x, address:%p\n", hw_regs->er_busmode,
	       &hw_regs->er_busmode);
	pr_info("MAC_CSR1:0x%x, address:%p\n", hw_regs->er_txpoll,
	       &hw_regs->er_txpoll);
	pr_info("MAC_CSR2:0x%x, address:%p\n", hw_regs->er_rxpoll,
	       &hw_regs->er_rxpoll);
	pr_info("MAC_CSR3:0x%x, address:%p\n", hw_regs->er_rxbdbase,
	       &hw_regs->er_rxbdbase);
	pr_info("MAC_CSR4:0x%x, address:%p\n", hw_regs->er_txbdbase,
	       &hw_regs->er_txbdbase);
	pr_info("MAC_CSR5:0x%x, address:%p\n", hw_regs->er_status,
	       &hw_regs->er_status);
	pr_info("MAC_CSR6:0x%x, address:%p\n", hw_regs->er_opmode,
	       &hw_regs->er_opmode);
	pr_info("MAC_CSR7:0x%x, address:%p\n", hw_regs->er_ienable,
	       &hw_regs->er_ienable);
	pr_info("MAC_CSR8:0x%x, address:%p\n", hw_regs->er_mfocnt,
	       &hw_regs->er_mfocnt);
	pr_info("MAC_CSR9:0x%x, address:%p\n", hw_regs->er_miimng,
	       &hw_regs->er_miimng);
	pr_info("MAC_CSR10:0x%x, address:%p\n", hw_regs->er_miism,
	       &hw_regs->er_miism);
	pr_info("MAC_CSR11:0x%x, address:%p\n", hw_regs->er_imctrl,
	       &hw_regs->er_imctrl);
	pr_info("MAC_CSR16:0x%x, address:%p\n", hw_regs->er_maclow,
	       &hw_regs->er_maclow);
	pr_info("MAC_CSR17:0x%x, address:%p\n", hw_regs->er_machigh,
	       &hw_regs->er_machigh);
	pr_info("MAC_CSR18:0x%x, address:%p\n", hw_regs->er_cachethr,
	       &hw_regs->er_cachethr);
	pr_info("MAC_CSR19:0x%x, address:%p\n", hw_regs->er_fifothr,
	       &hw_regs->er_fifothr);
	pr_info("MAC_CSR20:0x%x, address:%p\n", hw_regs->er_flowctrl,
	       &hw_regs->er_flowctrl);
	pr_info("MAC_CTRL:0x%x, address:%p\n", hw_regs->er_macctrl,
	       &hw_regs->er_macctrl);

}

static inline void raw_tx_bd_init(ec_priv_t *ecp)
{
	int i;
	volatile ec_bd_t *tx_bds = ecp->tx_bd_base;

	for (i = 0; i < TX_RING_SIZE; i++) {
		tx_bds[i] = (ec_bd_t) {
			.status = 0,	/* host own it */
		.control = TXBD_CTRL_IC, .buf_addr = 0, .reserved = 0};
	}
	tx_bds[i - 1].control |= TXBD_CTRL_TER;

	return;
}

static inline void raw_rx_bd_init(ec_priv_t *ecp)
{
	int i;
	volatile ec_bd_t *rx_bds = ecp->rx_bd_base;

	for (i = 0; i < RX_RING_SIZE; i++) {
		rx_bds[i] = (ec_bd_t) {
		.status = RXBD_STAT_OWN, .control =
			    RXBD_CTRL_RBS1(PKG_MAX_LEN), .buf_addr =
			    0, .reserved = 0};
	}
	rx_bds[i - 1].control |= RXBD_CTRL_RER;

	return;
}

/**
 * get_skb_aligned - get a skb which the address of skb->data is 4B aligned
 */
static struct sk_buff *get_skb_aligned(unsigned int len)
{
	int offset;
	struct sk_buff *skb = NULL;
	skb = dev_alloc_skb(len);
	if (NULL == skb)
		return NULL;

	offset = (unsigned int)skb->data & EC_SKB_ALIGN_BITS_MASK;
	if (unlikely(offset))
		skb_reserve(skb, EC_SKB_ALIGNED - offset);

	return skb;
}

static inline void free_rxtx_skbs(struct sk_buff **array, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (NULL != array[i]) {
			dev_kfree_skb_any(array[i]);
			array[i] = NULL;
		}
	}
	return;
}

/**
 * prepare_tx_bd -- preparation for tx buffer descripters
 *
 * always success
 */
static inline int prepare_tx_bd(ec_priv_t *ecp)
{
	volatile ec_bd_t *tx_bds_head = ecp->tx_bd_base;

	ecp->cur_tx = (ec_bd_t *) tx_bds_head;
	ecp->dirty_tx = (ec_bd_t *) tx_bds_head;
	ecp->skb_cur = 0;
	ecp->skb_dirty = 0;
	ecp->tx_full = false;

	free_rxtx_skbs(ecp->tx_skb, TX_RING_SIZE);
	raw_tx_bd_init(ecp);

	return 0;
}

/**
 * prepare_rx_bd -- preparation for rx buffer descripters
 *
 * return 0 if success, return -1 if fail
 */
static int prepare_rx_bd(ec_priv_t *ecp)
{
	int i;
	struct sk_buff *skb = NULL;
    volatile ec_bd_t *rx_bds_head = ecp->rx_bd_base;

	EC_INFO("ecp->rx_bd_base: %p\n", ecp->rx_bd_base);
	ecp->cur_rx = (ec_bd_t *) rx_bds_head;
	raw_rx_bd_init(ecp);

	free_rxtx_skbs(ecp->rx_skb, RX_RING_SIZE);

	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = get_skb_aligned(PKG_MAX_LEN);
		if (skb) {
			ecp->rx_skb[i] = skb;
			/* should be 4-B aligned */
			rx_bds_head[i].buf_addr =
			    dma_map_single(&ecp->netdev->dev, skb->data,
					   PKG_MAX_LEN, DMA_FROM_DEVICE);
		} else {
			EC_ERR("can't alloc skb\n");
			free_rxtx_skbs(ecp->rx_skb, i);
			raw_rx_bd_init(ecp);
			return -1;
		}
	}
	EC_INFO("ecp->cur_rx: %p\n", ecp->cur_rx);

	return 0;
}

/* suitable for less than 7 chars of string */
static inline int string_to_hex(char *str, int len)
{
	int val;
	int i;
	char ch;

	val = 0;
	for (i = 0; i < len; i++) {
		ch = str[i];
		if ('0' <= ch && ch <= '9')
			val = (val << 4) + ch - '0';
		else if ('a' <= ch && ch <= 'f')
			val = (val << 4) + 10 + ch - 'a';
		else if ('A' <= ch && ch <= 'F')
			val = (val << 4) + 10 + ch - 'A';
		else
			return -1;
	}

	return val;
}

/**
 * parse_mac_addr -- parse string of mac address to number mac address
 *
 * return 0 if success, negative value if fail
 */
static inline int parse_mac_addr(ec_priv_t *ecp, char *mac)
{
	int i;
	int j;
	int result;

	/* string of mac - such as "01:02:03:04:05:06" */
	if (17 != strlen(mac))
		return -1;

	for (i = 0, j = 2; i < 5; i++, j += 3) {
		if (':' != mac[j])
			return -1;
	}

	for (i = 0, j = 0; i < 6; i++, j += 3) {
		result = string_to_hex(mac + j, 2);
		if (-1 != result)
			ecp->overrided_mac[i] = (char)result;
		else
			return result;
	}

	return 0;
}

static inline void fill_macaddr_regs(ec_priv_t *ecp, const char *mac_addr)
{
	volatile ethregs_t *hw_regs = ecp->hwrp;

	hw_regs->er_maclow = *(unsigned int *)mac_addr;
	hw_regs->er_machigh = *(unsigned short *)(mac_addr + 4);
	return;
}

static inline void set_mac_addr(ec_priv_t *ecp)
{
	if (ecp->mac_addr) {
		fill_macaddr_regs(ecp, ecp->mac_addr);
		EC_INFO("using previous one\n");
		return;
	}

	if ('?' != macaddr[0]) {
		EC_INFO("parse mannual mac address\n");
		if ((0 == parse_mac_addr(ecp, macaddr)) &&
		    compare_ether_addr(macaddr, g_default_mac_addr)) {

			fill_macaddr_regs(ecp, ecp->overrided_mac);
			ecp->mac_addr = ecp->overrided_mac;
			ecp->mac_overrided = true;
			return;
		}
	}
	EC_INFO("set default mac address\n");
	fill_macaddr_regs(ecp, g_default_mac_addr);
	ecp->mac_addr = g_default_mac_addr;
	ecp->mac_overrided = false;
	return;
}

/* NOTE: it has side effect for dest parameter */
#define COPY_MAC_ADDR(dest, mac)  do {\
	*(unsigned short *)(dest) = *(unsigned short *)(mac); \
	*(unsigned short *)((dest) + 4) = *(unsigned short *)((mac) + 2); \
	*(unsigned short *)((dest) + 8) = *(unsigned short *)((mac) + 4); \
	(dest) += 12; \
	} while (0)

/**
 * build_setup_frame -- build setup-frame of mac address filter  in @buffer
 *
 * @buf_len should be longer than or equal  SETUP_FRAME_LEN (192 bytes), but we only
 * use SETUP_FRAME_LEN bytes exactly.
 *
 * return the address of @buffer if success, or NULL if not
 */
static char *build_setup_frame(ec_priv_t  *ecp, char *buffer, int buf_len)
{
	char broadcast_mac[ETH_MAC_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	char *frame = buffer;
	char *mac;

	if (NULL == buffer || buf_len < SETUP_FRAME_LEN) {
		EC_ERR("error parameters\n");
		return NULL;
	}

	memset(frame, 0, SETUP_FRAME_LEN);

	mac = (char *)ecp->mac_addr;
	COPY_MAC_ADDR(frame, mac);

	mac = broadcast_mac;
	COPY_MAC_ADDR(frame, mac);

	/* fill multicast addresses */
	if (!ecp->all_multicast && ecp->multicast) {
		int i;
		int count = ecp->multicast_list.count;

		if (count > MULTICAST_LIST_LEN)
			count = MULTICAST_LIST_LEN;

		for (i = 0; i < count; i++) {
			mac = ecp->multicast_list.mac_array[i];
			COPY_MAC_ADDR(frame, mac);
		}
	}

	if (ecp->msg_enable) {
		INFO_GREEN("overrided : %s -- multicast : %s\n",
			   ecp->mac_overrided ? "true" : "false",
			   ecp->multicast ? "true" : "false");
	}

	return buffer;
}

/*transmit_setup_frame -- transmit setup-frame of  mac*/
/*address filter the function is not thread-safety, */
/*thus in multithread envirement should hold */
/*ec_prive_t{.lock}. and before call it, */
/*ec_prive_t{.mac_addr} should point to a*/
/* suitalbe mac address used MAC will raise*/
/* CSR5.ETI interrupt after transmission of*/
/* setup-frame, so we use uniform manner to */
/*deal with it. return 0 if success, -1 if fail*/
static int transmit_setup_frame(ec_priv_t *ecp)
{
	struct sk_buff *skb = NULL;
	volatile ec_bd_t *buf_des = ecp->cur_tx;

	if (ecp->tx_full) {
		/* may happen when change macs if not in open ethdev  */
		EC_ERR("error : tx buffer is full.\n");
		return -1;
	}

	/* will build a setup-frame in a skb */
	skb = get_skb_aligned(SETUP_FRAME_RESV_LEN);
	if (NULL == skb) {
		EC_ERR("error : no memory for setup frame.\n");
		return -1;
	}

	skb_put(skb, SETUP_FRAME_LEN);

	/* address of skb->data should be 4-bytes aligned */
	if (NULL == build_setup_frame(ecp, skb->data, SETUP_FRAME_LEN)) {
		EC_ERR("error : building of setup-frame failed.\n");
		dev_kfree_skb_any(skb);
		return -1;
	}

	/* send it out as normal packet */
	ecp->tx_skb[ecp->skb_cur] = skb;
	ecp->skb_cur = (ecp->skb_cur + 1) & TX_RING_MOD_MASK;

	/* Push the data cache so the NIC does not get*/
	/* stale memory data.*/
	buf_des->buf_addr =
	    dma_map_single(&ecp->netdev->dev, skb->data, PKG_MAX_LEN,
			   DMA_TO_DEVICE);

	buf_des->control &= (TXBD_CTRL_TER | TXBD_CTRL_IC);
	/* maintain these bits */
	buf_des->control |= TXBD_CTRL_SET;
	buf_des->control |= TXBD_CTRL_TBS1(SETUP_FRAME_LEN);
	mb();
	buf_des->status = TXBD_STAT_OWN;
	mb();

	/* when call the routine, TX and Rx should have already stopped */
	ecp->hwrp->er_opmode |= EC_OPMODE_ST;
	ecp->hwrp->er_txpoll = EC_TXPOLL_ST;

	if (buf_des->control & TXBD_CTRL_TER)
		ecp->cur_tx = ecp->tx_bd_base;
	else
		ecp->cur_tx++;

	if (ecp->cur_tx == ecp->dirty_tx) {
		ecp->tx_full = true;
		netif_stop_queue(ecp->netdev);
	}

	/* resume old status */
	ecp->hwrp->er_opmode &= ~EC_OPMODE_ST;
	netif_stop_queue(ecp->netdev);
	EC_INFO("The end of transmit_setup_frame\n");
	return 0;
}

/**
  * when reconfigrate mac's mode, should stop tx and rx first.  so before call the following
  * tow function, make sure tx and rx have stopped
 */
static inline void set_mode_promisc(ec_priv_t *ecp, bool supported)
{
	volatile ethregs_t *hw_regs = ecp->hwrp;

	ecp->promiscuous = supported;
	if (supported) {
		hw_regs->er_opmode |= EC_OPMODE_PR;
		EC_NOP;
	} else {
		hw_regs->er_opmode &= ~EC_OPMODE_PR;
		EC_NOP;
	}
	return;
}

static inline void set_mode_all_multicast(ec_priv_t *ecp, bool supported)
{
	volatile ethregs_t *hw_regs = ecp->hwrp;

	ecp->all_multicast = supported;
	if (supported)
		hw_regs->er_opmode |= EC_OPMODE_PM;
	else
		hw_regs->er_opmode &= ~EC_OPMODE_PM;

	return;
}

static void mac_init(ec_priv_t *ecp)
{
	volatile ethregs_t *hw_regs = ecp->hwrp;

	EC_INFO("MAC init start.\n");

	/* hardware soft reset, and set bus mode */
	hw_regs->er_busmode |= EC_BMODE_SWR;
	do {
		udelay(10);
	} while (hw_regs->er_busmode & EC_BMODE_SWR);

	if (ETH_PHY_MODE_SMII == ecp->phy_mode)
		hw_regs->er_macctrl = MAC_CTRL_SMII;	/* use smii interface */
	else
		hw_regs->er_macctrl = MAC_CTRL_RMII;	/* use rmii interface */

	hw_regs->er_miism |= MII_MNG_SB | MII_MNG_CLKDIV(4) | MII_MNG_OPCODE(3);
	EC_INFO("MII Serial Control: 0x%x\n", hw_regs->er_miism);

	/* select clk input from external phy */

	hw_regs->er_macctrl |= 0x100;
	EC_INFO("MAC_CTRL: 0x%x\n", hw_regs->er_macctrl);

	/* physical address */
	hw_regs->er_txbdbase = ecp->tx_bd_paddr;
	hw_regs->er_rxbdbase = ecp->rx_bd_paddr;
	EC_INFO("csr4-txbdbase:0x%x\n", (unsigned)hw_regs->er_txbdbase);
	EC_INFO("csr3-rxbdbase:0x%x\n", (unsigned)hw_regs->er_rxbdbase);

	/* set flow control mode, force transmitor pause about 100ms */
	hw_regs->er_cachethr =
	    EC_CACHETHR_CPTL(0x0) | EC_CACHETHR_CRTL(0x0) |
	    EC_CACHETHR_PQT(0x4FFF);
	hw_regs->er_fifothr = EC_FIFOTHR_FPTL(0x40) | EC_FIFOTHR_FRTL(0x10);
	hw_regs->er_flowctrl = EC_FLOWCTRL_ENALL;

	hw_regs->er_opmode |= EC_OPMODE_FD;
	hw_regs->er_opmode |= EC_OPMODE_SPEED(0);	/*100M*/

	EC_INFO("hw_regs->er_busmode:0x%x\n", (unsigned)hw_regs->er_busmode);

	/* default support PR, here clear it*/
	/* XXX: due to MAC constraint, after */
	/* write a reg, can't read it immediately*/
	/* (write of regs has tow beats delay).*/
	hw_regs->er_opmode &= ~EC_OPMODE_PR;
	EC_INFO("hw_regs->er_opmode:0x%x\n", (unsigned)hw_regs->er_opmode);

	/*interrupt mitigation control register */
	hw_regs->er_imctrl = 0x004e0000;	/*NRP =7,RT =1,CS=0 */

#if defined(ETHENRET_MAC_LOOP_BACK) || defined(ETHENRET_PHY_LOOP_BACK)
	hw_regs->er_opmode |= EC_OPMODE_RA;
#endif

#ifdef ETHENRET_MAC_LOOP_BACK
	/* mac internal loopback */
	hw_regs->er_opmode |= EC_OPMODE_LP;
	EC_INFO("MAC operation mode: 0x%x\n", (unsigned)hw_regs->er_opmode);
#endif
}

static int phy_init(ec_priv_t *ecp)
{
	int reg_val;
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned int cnt = 0;
	int temp = hw_regs->er_miism;
	EC_INFO("phy_init:%x-%x-%x\n", MII_BMCR,
	    temp, read_phy_reg(ecp, MII_BMCR));
	phy_reg_set_bits(ecp, MII_BMCR, BMCR_RESET);
	do {
		reg_val = read_phy_reg(ecp, MII_BMCR);
		if (cnt++ > 1000) {
			static int initflag;
			EC_INFO("ethernet phy BMCR_RESET timeout!!!\n");
			write_phy_reg(ecp, 0xff, 0);
			cnt = 0;
			initflag++;
			phy_reg_set_bits(ecp, MII_BMCR, BMCR_RESET);
			if (initflag >= 3)
				break;
		}
	} while (reg_val & BMCR_RESET);
	EC_INFO("phy_init:%x-%x\n", hw_regs->er_miism, read_phy_reg(ecp, 31));
	return 0;
}

static int _init_hardware(ec_priv_t *ecp)
{
	ethernet_clock_enable();

	/* ATC2605 ONLY support output 50MHz RMII_REF_CLK*/
	/* to MAC, so phy hw init first */
	/*if (ecp->phy_ops->phy_hw_init)*/
	/*  ecp->phy_ops->phy_hw_init(ecp); */
	mac_init(ecp);

	set_mac_addr(ecp);
	INFO_BLUE("mac address: ");
	print_mac_address(ecp->mac_addr);

	if (phy_init(ecp))
		EC_ERR("error : initialize PHY fail\n");

	return 0;
}

#ifdef DETECT_POWER_SAVE
static void detect_power_save_timer_func(unsigned int data);

static void init_power_save_timer(ec_priv_t *ecp)
{
	EC_INFO("\n");
	init_timer(&ecp->detect_timer);
	ecp->detect_timer.data = (unsigned int)ecp;
	ecp->detect_timer.function = detect_power_save_timer_func;
}

static void start_power_save_timer(ec_priv_t *ecp, const unsigned ms)
{
	EC_INFO("\n");
	mod_timer(&ecp->detect_timer, jiffies + msecs_to_jiffies(ms));
}

static void stop_power_save_timer(ec_priv_t *ecp)
{
	EC_INFO("\n");
	if (timer_pending(&ecp->detect_timer))
		del_timer_sync(&ecp->detect_timer);

	cancel_work_sync(&ecp->power_save_work);
	flush_workqueue(power_save_queue);
}

static void detect_power_save_timer_func(unsigned int data)
{
	ec_priv_t *ecp = (ec_priv_t *) data;
	unsigned long flags;

	EC_INFO("ecp->enable: %u\n", ecp->enable);
	if (!ecp->opened) {
		EC_ERR("not opened yet\n");
		return;
	}

	spin_lock_irqsave(&ecp->lock, flags);
	if (ecp->linked) {
		spin_unlock_irqrestore(&ecp->lock, flags);
		EC_ERR("not linked yet\n");
		return;
	}
	spin_unlock_irqrestore(&ecp->lock, flags);

	if (ecp->enable)
		mod_timer(&ecp->detect_timer, jiffies + msecs_to_jiffies(4000));
	else
		mod_timer(&ecp->detect_timer, jiffies + msecs_to_jiffies(4000));

	queue_work(power_save_queue, &ecp->power_save_work);
}
#endif /* DETECT_POWER_SAVE */

static void set_phy_according_aneg(ec_priv_t *ecp)
{
	unsigned short old_bmcr;

	old_bmcr = read_phy_reg(ecp, MII_BMCR);
	EC_INFO("old MII_BMCR: 0x%04x\n", (unsigned)old_bmcr);

	if (ETH_SPEED_10M == ecp->speed)
		old_bmcr &= ~BMCR_SPEED100;
	else
		old_bmcr |= BMCR_SPEED100;

	if (ETH_DUP_FULL == ecp->duplex)
		old_bmcr |= BMCR_FULLDPLX;
	else
		old_bmcr &= ~BMCR_FULLDPLX;

	write_phy_reg(ecp, MII_BMCR, old_bmcr);
}

static void set_mac_according_aneg(ec_priv_t *ecp)
{
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned int old_mode;

	old_mode = hw_regs->er_opmode;
	EC_INFO("opmode regs old value - 0x%x\n", (int)old_mode);

	hw_regs->er_opmode &= ~(EC_OPMODE_ST | EC_OPMODE_SR);

	if (ETH_SPEED_10M == ecp->speed)
		old_mode |= EC_OPMODE_10M;
	else
		old_mode &= ~EC_OPMODE_10M;

	if (ETH_DUP_FULL == ecp->duplex)
		old_mode |= EC_OPMODE_FD;
	else
		/* always set full duplex to work around*/
		/*for both half/full mode! */
		old_mode |= EC_OPMODE_FD;	/* mac bug! */

	/*set phy during mac stopped */
	set_phy_according_aneg(ecp);

	hw_regs->er_opmode = old_mode;
	EC_INFO("hw_regs->er_opmode:0x%x\n", hw_regs->er_opmode);
}

/**
 * ec_enet_tx -- sub-isr for tx interrupt
 */
static void subisr_enet_tx(ec_priv_t *ecp)
{

	struct net_device *dev = ecp->netdev;
	volatile ec_bd_t *bdp;
	struct sk_buff *skb;
	unsigned int status;


	spin_lock(&ecp->lock);
	bdp = ecp->dirty_tx;
    if (0 == ((status = bdp->status) & TXBD_STAT_OWN)) {
		/* don't enable CSR11 interrupt mitigation */
		skb = ecp->tx_skb[ecp->skb_dirty];
		dma_unmap_single(&ecp->netdev->dev, bdp->buf_addr,
				 skb->len, DMA_TO_DEVICE);
		/* check for errors */
		if (status & TXBD_STAT_ES) {
			EC_ERR("tx error status : 0x%x\n",
			       (unsigned int)status);
			if (netif_msg_tx_err(ecp)) {
				EC_INFO("position: %ld\n",
				       ecp->dirty_tx - ecp->tx_bd_base);
				print_tx_bds(ecp);
			}
			dev->stats.tx_errors++;
			if (status & TXBD_STAT_UF)
				dev->stats.tx_fifo_errors++;
			if (status & TXBD_STAT_EC)
				dev->stats.tx_aborted_errors++;
			if (status & TXBD_STAT_LC)
				dev->stats.tx_window_errors++;
			if (status & TXBD_STAT_NC)
				dev->stats.tx_heartbeat_errors++;
			if (status & TXBD_STAT_LO)
				dev->stats.tx_carrier_errors++;
		} else {
			dev->stats.tx_packets++;
		}

		/* some collions occurred, but sent packet ok eventually */
		if (status & TXBD_STAT_DE)
			dev->stats.collisions++;

		if (netif_msg_tx_err(ecp)) {
			check_icmp_sequence(skb->data, __func__);
			EC_ERR("bdp->buf_addr:0x%x\n", bdp->buf_addr);
			EC_ERR("tx frame:\n");
			print_frame_data(skb->data, skb->len);
		}
		dev_kfree_skb_any(skb);

		ecp->tx_skb[ecp->skb_dirty] = NULL;
		ecp->skb_dirty = (ecp->skb_dirty + 1) & TX_RING_MOD_MASK;

		if (bdp->control & TXBD_CTRL_TER)
			bdp = ecp->tx_bd_base;
		else
			bdp++;

		if (ecp->tx_full) {
			EC_INFO("tx bds available, skb_dirty:%d\n",
				ecp->skb_dirty);
			ecp->tx_full = false;
			if (netif_queue_stopped(dev))
				netif_wake_queue(dev);
		}
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
	} else if (ecp->tx_full) {
		ec_bd_t *bdp_next;

		/* handle the case that bdp->status own bit not */
		/* cleared by hw but the interrupt still comes */
		if (bdp->control & TXBD_CTRL_TER)
			bdp_next = ecp->tx_bd_base;
		else
			bdp_next = bdp + 1;

		while (bdp_next != bdp) {
			/* when tx full, if we find that some bd(s) has*/
			/*own bit is 0, which indicates that mac hw has*/
			/*transmitted it but the own bit left not cleared*/
			if (!(bdp_next->status & TXBD_STAT_OWN)) {
				EC_ERR("tx bd own bit not cleared!!!\n");
#ifdef TX_DEBUG
				print_mac_register(ecp);
				print_tx_bds(ecp);
#endif
				bdp->status &= ~TXBD_STAT_OWN;
				/* clear own bit */
				skb = ecp->tx_skb[ecp->skb_dirty];
				dma_unmap_single(&ecp->netdev->dev,
						 bdp->buf_addr, skb->len,
						 DMA_TO_DEVICE);
				dev_kfree_skb_any(skb);

				ecp->tx_skb[ecp->skb_dirty] = NULL;
				ecp->skb_dirty =
				    (ecp->skb_dirty + 1) & TX_RING_MOD_MASK;

				if (bdp->control & TXBD_CTRL_TER)
					bdp = ecp->tx_bd_base;
				else
					bdp++;

				ecp->tx_full = false;
				if (netif_queue_stopped(dev))
					netif_wake_queue(dev);
				break;
			}
			if (bdp_next->control & TXBD_CTRL_TER)
				bdp_next = ecp->tx_bd_base;
			else
				bdp_next++;
		}
	}

	ecp->dirty_tx = (ec_bd_t *) bdp;

	spin_unlock(&ecp->lock);
	return;
}

/**
 * ec_enet_rx -- sub-isr for rx interrupt
 */
static void subisr_enet_rx(ec_priv_t *ecp)
{
	struct net_device *dev = ecp->netdev;
	volatile ec_bd_t *bdp;
	struct sk_buff *new_skb;
	struct sk_buff *skb_to_upper;
	unsigned int status;
	unsigned int pkt_len;
	int index;

#define RX_ERROR_CARED \
	(RXBD_STAT_DE | RXBD_STAT_RF | RXBD_STAT_TL | RXBD_STAT_CS \
	| RXBD_STAT_DB | RXBD_STAT_CE | RXBD_STAT_ZERO)

	spin_lock(&ecp->lock);
	BUG_ON(!ecp->opened);
	bdp = ecp->cur_rx;
#ifdef RX_DEBUG
	if (unlikely(netif_msg_rx_err(ecp)))
		EC_INFO("bdp: 0x%p\n", bdp);
#endif

	while (0 == ((status = bdp->status) & RXBD_STAT_OWN)) {
		/*EC_INFO("bdp->status:0x%08lx\n", bdp->status); */
		if (!(status & RXBD_STAT_LS))
			EC_ERR("not last descriptor of a frame: 0x%x.\n", status);
#ifdef RX_DEBUG
		if (unlikely(netif_msg_rx_err(ecp))) {
			EC_INFO("bdp: 0x%p, bdp->status: 0x%08x\n", bdp,
				(u32) bdp->status);
			EC_INFO("status: 0x%08x\n", (u32) status);
		}
#endif

		/* check for rx errors. RXBD_STAT_ES includes */
		/*RXBD_STAT_RE, and RXBD_STAT_RE always set, coz*/
		/*RE pin of 5201 ether mac is NC to 5302 ether phy*/
		/*Don't care it now, it'll be fixed in 5203. */
		if (status & RX_ERROR_CARED) {
			EC_ERR("RX_ERROR status:0x%x\n", status);
			dev->stats.rx_errors++;
			if (status & RXBD_STAT_TL)
				dev->stats.rx_length_errors++;
			if (status & RXBD_STAT_CE)
				dev->stats.rx_crc_errors++;
			if (status & (RXBD_STAT_RF | RXBD_STAT_DB))
				dev->stats.rx_frame_errors++;
			if (status & RXBD_STAT_ZERO)
				dev->stats.rx_fifo_errors++;
			if (status & RXBD_STAT_DE)
				dev->stats.rx_over_errors++;
			if (status & RXBD_STAT_CS)
				dev->stats.collisions++;
			goto rx_done;
		}

		pkt_len = RXBD_STAT_FL(status);
		if (pkt_len > ETH_PKG_MAX) {	/* assure skb_put() not panic */
			EC_ERR("pkt_len = %u\n", pkt_len);
			dev->stats.rx_length_errors++;
			goto rx_done;
		}
		new_skb = get_skb_aligned(PKG_MAX_LEN);
		if (NULL == new_skb) {
			dev->stats.rx_dropped++;
			EC_INFO("no memory, just drop it.\n");
			/* no release version ?? */
			goto rx_done;
		}

		dma_unmap_single(&ecp->netdev->dev, bdp->buf_addr,
				 PKG_MAX_LEN, DMA_FROM_DEVICE);

		dev->stats.rx_packets++;
		dev->stats.rx_bytes += pkt_len;

		index = bdp - ecp->rx_bd_base;
		skb_to_upper = ecp->rx_skb[index];
		ecp->rx_skb[index] = new_skb;

		skb_put(skb_to_upper, pkt_len - ETH_CRC_LEN);
		/* modify its data length, remove CRC */

#ifndef RX_DEBUG
		if (unlikely(netif_msg_rx_err(ecp))) {
			check_icmp_sequence(skb_to_upper->data, __func__);
			printk("receive %u bytes\n", pkt_len);
			printk("source mac:");
			print_mac_address(skb_to_upper->data + ETH_MAC_LEN);
			printk("dest mac:");
			print_mac_address(skb_to_upper->data);
			printk("receive data:\n");
			print_frame_data(skb_to_upper->data, skb_to_upper->len);
		}
#endif
		skb_to_upper->protocol = eth_type_trans(skb_to_upper, dev);
		netif_rx(skb_to_upper);

		bdp->buf_addr =
		    dma_map_single(&ecp->netdev->dev, new_skb->data,
				   PKG_MAX_LEN, DMA_FROM_DEVICE);
		if (!bdp->buf_addr)
			EC_ERR("dma map new_skb->data:%p failed\n",
			       new_skb->data);

rx_done:
		/* mark MAC AHB owns the buffer, and clear other status */
		bdp->status = RXBD_STAT_OWN;
#ifdef RX_DEBUG
		if (unlikely(netif_msg_rx_err(ecp)))
			EC_INFO("bdp->status: 0x%08x\n", (u32) bdp->status);
#endif
		if (bdp->control & RXBD_CTRL_RER)
			bdp = ecp->rx_bd_base;
		else
			bdp++;
#ifdef RX_DEBUG
		if (unlikely(netif_msg_rx_err(ecp)))
			EC_INFO("bdp: 0x%p\n", bdp);
#endif
		/* start to receive packets, may be good in */
		/*heavily loaded net */
	}			/* while */

	ecp->cur_rx = (ec_bd_t *) bdp;
	spin_unlock(&ecp->lock);
	return;
}

/**
 * ec_netdev_isr -- interrupt service routine for ethernet controller
 */
static irqreturn_t ec_netmac_isr(int irq, void *cookie)
{

	ec_priv_t *ecp = netdev_priv((struct net_device *)cookie);
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned int status = 0;
	unsigned int intr_bits = 0;

	static unsigned int tx_cnt, rx_cnt;
	unsigned int mac_status;
	int ru_cnt = 0;

	intr_bits = EC_STATUS_NIS | EC_STATUS_AIS;

	/* xmit setup frame raise ETI, but not TI,*/
	/*this is only reason to pay attention to it */

	while ((status = hw_regs->er_status) & intr_bits) {
		hw_regs->er_status = status;	/* clear status */

		if (netif_msg_intr(ecp)) {
			EC_INFO("interrupt status: 0x%8x\n", (int)status);
			EC_INFO("status after clear: 0x%x\n",
			       (int)hw_regs->er_status);
		}

		if (status & (EC_STATUS_TI | EC_STATUS_ETI)) {
			subisr_enet_tx(ecp);
			tx_cnt = 0;
			mac_status = status & EC_STATUS_RSM;
			if ((mac_status == EC_RX_fetch_dsp)
				 || (mac_status == EC_RX_run_dsp)
			    || (mac_status == EC_RX_close_dsp))
				rx_cnt++;
		}
		/* RI & RU may come at same time, if RI handled, then RU*/
		/* needn't handle.If RU comes & RI not comes, then we must*/
		/* handle RU interrupt. */
		if (status & EC_STATUS_RI) {
			subisr_enet_rx(ecp);
			rx_cnt = 0;
			mac_status = status & EC_STATUS_TSM;
			if ((mac_status == EC_STATUS_TSM) || \
				(mac_status == EC_TX_run_dsp))
				tx_cnt++;
		} else if (status & EC_STATUS_RU) {
			ru_cnt++;
			/* set RPD could help if rx suspended & bd available */
			if (ru_cnt == 2)
				hw_regs->er_rxpoll = 0x1;
			EC_INFO("---RU interrupt---, status: 0x%08x\n",
				(u32) status);
#ifdef RX_DEBUG
			print_rx_bds(ecp);
			ecp->msg_enable |= 0x40;
#endif
			subisr_enet_rx(ecp);
#ifdef RX_DEBUG
			ecp->msg_enable &= ~0x40;
			print_rx_bds(ecp);
#endif
			/* guard against too many RU interrupts to avoid */
			/*long time ISR handling */
			if (ru_cnt > 3)
				break;
		}
	}
	if ((tx_cnt > 10) || (rx_cnt > 10)) {
		if (tx_cnt > 10)
			EC_INFO("TX ERROR status: 0x%08x\n", (u32) status);
		else
			EC_INFO("RX ERROR status: 0x%08x\n", (u32) status);
		rx_cnt = 0;
		tx_cnt = 0;
		schedule_work(&ecp->hardware_reset_work);
	}
	return IRQ_HANDLED;
}

/**
 * ec_netdev_open -- open ethernet controller
 *
 * return 0 if success, negative value if fail
 */
static int ec_netdev_open(struct net_device *dev)
{
	int ret = 0;
	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned long flags = 0;

	if (ecp->opened) {
		EC_INFO("already opened\n");
		return 0;
	}
#ifdef ETH_TEST_MODE
	ecp->test_mode = 0;
#endif

	if (prepare_rx_bd(ecp) || prepare_tx_bd(ecp)) {
		EC_ERR("error: NO memery for bds.\n");
		return -1;
	}

	if (_init_hardware(ecp)) {
		EC_ERR("error: harware initialization failed.\n");
		return -1;
	}

	/* should after set_mac_addr() which will set ec_priv_t{.mac_addr} */
	memcpy(dev->dev_addr, ecp->mac_addr, ETH_MAC_LEN);

	spin_lock_irqsave(&ecp->lock, flags);

	/* send out a mac setup frame */
	if (transmit_setup_frame(ecp)) {
		EC_ERR("error : transmit setup frame failed.\n");
		spin_unlock_irqrestore(&ecp->lock, flags);
		return -1;
	}

	/* start to tx & rx packets */
	hw_regs->er_ienable = EC_IEN_ALL;
	hw_regs->er_opmode |= EC_OPMODE_ST | EC_OPMODE_SR;

#ifdef DETECT_POWER_SAVE
	ecp->enable = true;
#endif
	ecp->opened = true;

	spin_unlock_irqrestore(&ecp->lock, flags);

	if (ecp->link)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);

	pr_info("%s link %s.\n", dev->name, ecp->link ? "on" : "off");

		ret =
		    request_irq(ecp->mac_irq,
				(irq_handler_t) ec_netmac_isr, 0,
				"ethernet_mac", dev);
	if (ret < 0) {
		EC_ERR
		    ("Unable to request MAC IRQ: %d-%d, ec_netmac_isr\n",
		     ecp->mac_irq, ret);
		goto err_irq;
	}
	EC_INFO("MAC IRQ %d requested\n", ecp->mac_irq);
	/*print_mac_register(ecp);*/

	phy_start_aneg(ecp->phy);
	phy_start(ecp->phy);
	netif_start_queue(dev);
	return 0;

err_irq:
	return ret;
}

/**
 * ec_netdev_close -- close ethernet controller
 *
 * return 0 if success, negative value if fail
 */
static int ec_netdev_close(struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned long flags = 0;

	EC_INFO("\n");
	if (!ecp->opened) {
		EC_INFO("already closed\n");
		return 0;
	}
#ifdef DETECT_POWER_SAVE
	stop_power_save_timer(ecp);
#endif

	netif_stop_queue(dev);

	spin_lock_irqsave(&ecp->lock, flags);
#ifdef DETECT_POWER_SAVE
	ecp->enable = false;
#endif
	ecp->opened = false;
	ecp->link = false;
	hw_regs->er_opmode &= ~(EC_OPMODE_ST | EC_OPMODE_SR);

	hw_regs->er_ienable = 0;

	spin_unlock_irqrestore(&ecp->lock, flags);

	free_irq(ecp->mac_irq, dev);
	phy_stop(ecp->phy);
	if (ecp->phy_model != ETH_PHY_MODEL_ATC2605)
		goto phy_not_atc2605;

phy_not_atc2605:

	ethernet_clock_disable();
	return 0;
}

/* * ec_netdev_start_xmit -- transmit a skb*/
/* NOTE: if CSR6.ST is not set, the xmit frame will fail*/
/* return NETDEV_TX_OK if success, others if fail*/
static int ec_netdev_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = ecp->hwrp;
	volatile ec_bd_t *bdp;
	unsigned long flags = 0;
#ifndef ETH_TEST_MODE
	if (!ecp->link) {
		EC_ERR("haven't setup linkage\n");
		return -2;
	}
#endif
	if (ecp->tx_full) {
		if (printk_ratelimit())
			EC_INFO("warnig : tx buffer list is full\n");
		return NETDEV_TX_BUSY;
	}

	spin_lock_irqsave(&ecp->lock, flags);
	bdp = ecp->cur_tx;

	if (bdp->status & TXBD_STAT_OWN) {
		EC_INFO("%s: tx is full. should not happen\n", dev->name);
		spin_unlock_irqrestore(&ecp->lock, flags);
		return NETDEV_TX_BUSY;
	}

	/* Push the data cache so the NIC does not get stale memory data. */
	bdp->buf_addr =
	    dma_map_single(&ecp->netdev->dev, skb->data, skb->len,
			   DMA_TO_DEVICE);
	if (!bdp->buf_addr)
		EC_ERR("dma map skb->data:%p failed\n", skb->data);

	bdp->status = 0;
	bdp->control &= TXBD_CTRL_IC | TXBD_CTRL_TER;	/* clear others */
	bdp->control |= TXBD_CTRL_TBS1(skb->len);
	bdp->control |= TXBD_CTRL_FS | TXBD_CTRL_LS;
	mb();
	bdp->status = TXBD_STAT_OWN;
	mb();
	{
		volatile u32 tmp;
		tmp = (u32) (bdp->status + bdp->control + bdp->buf_addr);
	}
	if (skb->len > 1518)
		EC_INFO("tx length:%d\n", skb->len);


	/* leave xmit suspended mode to xmit running mode; another*/
	/* method is that first stop xmit, then start xmit through*/
	/* clearing CSR6.13-ST then setting it again.*/
	/* before call this function, CSR6.13-ST should be set*/
	hw_regs->er_txpoll = EC_TXPOLL_ST;

	dev->stats.tx_bytes += skb->len;
	dev->trans_start = jiffies;

	ecp->tx_skb[ecp->skb_cur] = skb;
	ecp->skb_cur = (ecp->skb_cur + 1) & TX_RING_MOD_MASK;

	if (!(hw_regs->er_status & EC_STATUS_TSM))
		EC_ERR("tx stopped, hw_regs->er_status:0x%x\n",
		       hw_regs->er_status);

	if (netif_msg_tx_err(ecp)) {
		check_icmp_sequence(skb->data, __func__);
		EC_ERR("%d, hw_regs->er_status:0x%x\n", __LINE__,
		       hw_regs->er_status);
		EC_ERR("bdp->status:0x%x\n", (unsigned int)bdp->status);
		EC_ERR("bdp->control:0x%x\n", (unsigned int)bdp->control);
		EC_ERR("bdp->buf_addr:0x%x\n", (unsigned int)bdp->buf_addr);
		EC_ERR("tx src mac: ");
		print_mac_address(skb->data + ETH_MAC_LEN);
		EC_ERR("tx dst mac: ");
		print_mac_address(skb->data);
		EC_ERR("tx frame:\n");
		print_frame_data(skb->data, skb->len);
	}

	if (bdp->control & TXBD_CTRL_TER)
		bdp = ecp->tx_bd_base;
	else
		bdp++;

	if (bdp == ecp->dirty_tx) {
		EC_INFO("tx is full, skb_dirty:%d\n", ecp->skb_dirty);
		ecp->tx_full = true;
		netif_stop_queue(dev);
	}
	netif_stop_queue(dev);

	ecp->cur_tx = (ec_bd_t *) bdp;
	spin_unlock_irqrestore(&ecp->lock, flags);

	return NETDEV_TX_OK;
}

/**
 * ec_netdev_query_stats -- query statistics of ethernet controller
 */
static struct net_device_stats *ec_netdev_query_stats(struct net_device
						      *dev)
{
	return &dev->stats;
}

/**
 * ec_netdev_set_mac_addr -- set mac a new address
 *
 * NOTE: when ethernet device has opened,
 * can't change mac address, otherwise return
 * EBUSY error code.
 *
 * return 0 if success, others if fail
 */
static int ec_netdev_set_mac_addr(struct net_device *dev, void *addr)
{
	struct sockaddr *address = (struct sockaddr *)addr;
	ec_priv_t *ecp = netdev_priv(dev);
	unsigned long flags = 0;
	char old_mac_addr[ETH_MAC_LEN];
	bool old_overrided;

	EC_INFO("\n");

	if (!is_valid_ether_addr(address->sa_data)) {
		EC_INFO("not valid mac address\n");
		return -EADDRNOTAVAIL;
	}

	/* if new mac address is the same as the old one, nothing to do */
	if (!compare_ether_addr(ecp->mac_addr, address->sa_data))
		return 0;

	if (netif_running(dev)) {
		EC_INFO("error : queue is busy\n");
		return -EBUSY;
	}

	spin_lock_irqsave(&ecp->lock, flags);

	memcpy(old_mac_addr, ecp->mac_addr, ETH_MAC_LEN);
	old_overrided = ecp->mac_overrided;

	memcpy(ecp->overrided_mac, address->sa_data, dev->addr_len);

	if (compare_ether_addr(g_default_mac_addr, ecp->overrided_mac)) {
		ecp->mac_addr = ecp->overrided_mac;
		ecp->mac_overrided = true;
	} else {
		ecp->mac_addr = g_default_mac_addr;
		ecp->mac_overrided = false;
	}

	memcpy(dev->dev_addr, ecp->mac_addr, dev->addr_len);

	/* if netdev is close now, just save new addr tmp, */
	/*set it when open it */
	if (!ecp->opened) {
		spin_unlock_irqrestore(&ecp->lock, flags);
		return 0;
	}

	fill_macaddr_regs(ecp, ecp->mac_addr);	/* for flow control */

	/** errors only occur before frame is put into tx bds.*/
	/* in fact if frame is successfully built,*/
	/* xmit never fail, otherwise mac controller may be something wrong.*/
	if (transmit_setup_frame(ecp)) {
		EC_ERR("error : transmit setup frame failed\n");

		/* set back to old one */
		fill_macaddr_regs(ecp, old_mac_addr);

		if (old_overrided) {
			memcpy(ecp->overrided_mac, old_mac_addr, ETH_MAC_LEN);
			ecp->mac_addr = ecp->overrided_mac;
		} else {
			ecp->mac_addr = g_default_mac_addr;
		}
		ecp->mac_overrided = old_overrided;
		memcpy(dev->dev_addr, ecp->mac_addr, dev->addr_len);
		spin_unlock_irqrestore(&ecp->lock, flags);
		return -1;
	}

	spin_unlock_irqrestore(&ecp->lock, flags);

	return 0;
}

/* copy_multicast_list -- copy @list to local ec_priv_t{.multicast_list}*/
/* may use it if multicast is supported when building setup-frame*/
static inline void copy_multicast_list(ec_priv_t *ecp,
				       struct netdev_hw_addr_list *list)
{
	char (*mmac_list)[MULTICAST_LIST_LEN][ETH_MAC_LEN] = NULL;
	struct netdev_hw_addr *ha = NULL;
	int mmac_sum = 0;

	mmac_list = &ecp->multicast_list.mac_array;

	netdev_hw_addr_list_for_each(ha, list) {
		if (mmac_sum >= MULTICAST_LIST_LEN)
			break;

		if (!is_multicast_ether_addr(ha->addr))
			continue;

		memcpy((*mmac_list)[mmac_sum], ha->addr, ETH_MAC_LEN);
		mmac_sum++;
	}

	ecp->multicast_list.count = mmac_sum;

	return;
}

static inline void parse_interface_flags(ec_priv_t *ecp, unsigned int flags)
{
	EC_INFO("\n");
	set_mode_promisc(ecp, (bool) (flags & IFF_PROMISC));
	set_mode_all_multicast(ecp, (bool) (flags & IFF_ALLMULTI));
	ecp->multicast = (bool) (flags & IFF_MULTICAST);
	return;
}

/**
 * ec_netdev_set_multicast_list -- set mac multicast address, meanwhile set promiscuous
 * and all_multicast mode according to dev's flags
 */
static void ec_netdev_set_multicast_list(struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned long flags = 0;
	unsigned int old_srst_bits;

#if EC_TRACED
	EC_INFO("--- enter %s()\n", __func__);
	EC_INFO("dev->flags - 0x%x\n", dev->flags);
#endif

	spin_lock_irqsave(&ecp->lock, flags);

	old_srst_bits = hw_regs->er_opmode & (EC_OPMODE_SR | EC_OPMODE_ST);

	/* stop tx & rx first */
	hw_regs->er_opmode &= ~(EC_OPMODE_SR | EC_OPMODE_ST);

	parse_interface_flags(ecp, dev->flags);

	if (!ecp->all_multicast && ecp->multicast && dev->mc.count) {
		if (dev->mc.count <= MULTICAST_LIST_LEN) {
			copy_multicast_list(ecp, &dev->mc);
			transmit_setup_frame(ecp);
		} else {
			EC_INFO("too multicast addrs, open all_multi\n");
			/* list is too long to support,*/
			/*so receive all multicast packets */
			set_mode_all_multicast(ecp, true);
		}
	}
	EC_INFO("\n");

	hw_regs->er_opmode |= old_srst_bits;

	spin_unlock_irqrestore(&ecp->lock, flags);

	return;
}

static inline void modify_mii_info(struct mii_ioctl_data *data,
				   struct mii_if_info *info)
{
	unsigned short val = data->val_in;

	if (data->phy_id != info->phy_id)
		return;

	switch (data->reg_num) {
	case MII_BMCR:
		{
			info->force_media =
			    (val & (BMCR_RESET | BMCR_ANENABLE)) ? 0 : 1;

			if (info->force_media && (val & BMCR_FULLDPLX))
				info->full_duplex = 1;
			else
				info->full_duplex = 0;
			break;
		}
	case MII_ADVERTISE:
		info->advertising = val;
		break;
	default:
		/* nothing to do */
		break;
	}

	return;
}

/**
 * ec_netdev_ioctrl -- net device's ioctrl hook
 */
static int ec_netdev_ioctrl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	ec_priv_t *ecp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);
	struct mii_if_info *info = &ecp->mii_info;
	int err = 0;

#if EC_TRACED
	EC_INFO("--- enter %s()\n", __func__);
	EC_INFO("phy reg num - 0x%x, data - 0x%x, cmd:0x%x\n",
	       data->reg_num, data->val_in, cmd);
#endif

	data->phy_id &= info->phy_id_mask;
	data->reg_num &= info->reg_num_mask;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = info->phy_id;
		/* FALL THROUGH */
	case SIOCGMIIREG:
		data->val_out = read_phy_reg(ecp, 0x500 + data->reg_num);
		break;

	case SIOCSMIIREG:
		{
			unsigned short val = data->val_in;

			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			modify_mii_info(data, info);

			write_phy_reg(ecp, 0x500 + data->reg_num, val);
			break;
		}		/* end SIOCSMIIREG */

	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

/**
 * do hardware reset,this function is called when transmission timeout.
*/
static void hardware_reset_do_work(struct work_struct *work)
{
	struct net_device *dev = g_eth_dev[0];
	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = ecp->hwrp;

	netif_stop_queue(dev);

	hw_regs->er_opmode &= ~(EC_OPMODE_SR | EC_OPMODE_ST);
	/* stop tx and rx*/
	hw_regs->er_ienable = 0;

	disable_irq(ecp->mac_irq);

	ethernet_clock_disable();

	/* set default value of status */
	ecp->link = false;
	ecp->opened = false;
	ecp->speed = ETH_SPEED_100M;
	ecp->duplex = ETH_DUP_FULL;


	if (prepare_rx_bd(ecp) || prepare_tx_bd(ecp)) {
		INFO_RED("error : prepare bds failed\n");
		return;
	}

	if (_init_hardware(ecp)) {
		INFO_RED("error : harware init failed.\n");
		return;
	}

	memcpy(dev->dev_addr, ecp->mac_addr, ETH_MAC_LEN);

	parse_interface_flags(ecp, dev->flags);

	if (transmit_setup_frame(ecp)) {
		INFO_RED("error : xmit setup frame failed\n");
		return;
	}

	hw_regs->er_ienable = EC_IEN_ALL;
	hw_regs->er_opmode |= (EC_OPMODE_SR | EC_OPMODE_ST);
	/* start tx, rx */

#ifdef DETECT_POWER_SAVE
	ecp->enable = true;
	start_power_save_timer(ecp, 4000);
#endif
	ecp->opened = true;

	enable_irq(ecp->mac_irq);
	return;
}

/* ec_netdev_transmit_timeout -- process tx fails to complete within a period*/
/*This function is called when a packet transmission fails*/
/*to complete within a resonable period, on the assumption that*/
/*an interrupts have been failed or the*/
/*interface is locked up. This function will reinitialize the hardware*/
static void ec_netdev_transmit_timeout(struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);
	static int reset;
	volatile ethregs_t *hw_regs = ecp->hwrp;

	reset++;
	EC_INFO("---TX timeout reset: %d\n", reset);
	EC_INFO("MAC flowctrl : 0x%x\n", (int)hw_regs->er_flowctrl);
	EC_INFO("MAC reg status : 0x%x\n", (int)hw_regs->er_status);
	schedule_work(&ecp->hardware_reset_work);

	return;
}

/*Internal function. Flush all scheduled work from the*/
/*Ethernet work queue. */
static void ethernet_flush_scheduled_work(void)
{
	flush_workqueue(resume_queue);
}

int suspend_flag;
static int asoc_ethernet_suspend(struct platform_device *pdev, pm_message_t m)
{
	struct net_device *dev = g_eth_dev[0];
	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned long flags = 0;

	dev_info(&pdev->dev, "asoc_ethernet_suspend()\n");

	suspend_flag = 1;
	cancel_delayed_work(&resume_work);
	ethernet_flush_scheduled_work();

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	spin_lock_irqsave(&ecp->lock, flags);

	hw_regs->er_opmode &= ~(EC_OPMODE_SR | EC_OPMODE_ST);
	/* stop tx and rx*/
	hw_regs->er_ienable = 0;

	free_rxtx_skbs(ecp->rx_skb, RX_RING_SIZE);
	free_rxtx_skbs(ecp->tx_skb, TX_RING_SIZE);

	ecp->link = false;

	spin_unlock_irqrestore(&ecp->lock, flags);

	phy_stop(ecp->phy);

	disable_irq(ecp->mac_irq);
	ethernet_clock_disable();
	return 0;
}

static void ethernet_resume_handler(struct work_struct *work)
{
	struct net_device *dev = g_eth_dev[0];

	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = ecp->hwrp;
	unsigned long flags = 0;

	EC_INFO("\n");
	suspend_flag = 0;

	if (ecp->opened == false) {
		EC_ERR("The ethernet is turned off\n");
		return;
	}

	spin_lock_irqsave(&ecp->lock, flags);
	if (prepare_rx_bd(ecp) || prepare_tx_bd(ecp)) {
		EC_ERR("error: prepare bds failed\n");
		goto err_unlock;
	}
	spin_unlock_irqrestore(&ecp->lock, flags);

	/*can't sleep */
	if (_init_hardware(ecp)) {
		EC_ERR("error: harware init failed.\n");
		return;
	}

	spin_lock_irqsave(&ecp->lock, flags);

	memcpy(dev->dev_addr, ecp->mac_addr, ETH_MAC_LEN);

	parse_interface_flags(ecp, dev->flags);

	/* send out a mac setup frame */
	if (transmit_setup_frame(ecp)) {	/* recovery previous macs*/
		EC_ERR("error: xmit setup frame failed\n");
		goto err_unlock;
	}

	hw_regs->er_ienable = EC_IEN_ALL;
	hw_regs->er_opmode |= (EC_OPMODE_SR | EC_OPMODE_ST);
	spin_unlock_irqrestore(&ecp->lock, flags);

	enable_irq(ecp->mac_irq);
#ifdef DETECT_POWER_SAVE
	ecp->enable = true;
	start_power_save_timer(ecp, 4000);
#endif

	phy_start(ecp->phy);
	return;

err_unlock:
	spin_unlock_irqrestore(&ecp->lock, flags);
}

static int asoc_ethernet_resume(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "asoc_ethernet_resume()\n");

	queue_delayed_work(resume_queue, &resume_work, 2 * HZ);
	return 0;
}

/**
 * ec_netdev_init -- initialize ethernet controller
 *
 * return 0 if success, others if fail
 */
static int ec_netdev_init(struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);
	volatile ethregs_t *hw_regs = NULL;

	/*EC_INFO("ETHERNET_BASE: 0x%x\n", ETHERNET_BASE);*/
	EC_INFO("ecp->hwrp: 0x%p\n", ecp->hwrp);
	/* ETHERNET_BASE address is taken from dts when driver probe,*/
	/*instead of hard-coding here */

	ecp->netdev = dev;
	hw_regs = ecp->hwrp;

	/* set default value of status */
#ifdef DETECT_POWER_SAVE
	ecp->enable = false;
#endif
	ecp->link = false;
	ecp->opened = false;
	ecp->mac_overrided = false;
	ecp->multicast = false;
	ecp->all_multicast = false;
	ecp->promiscuous = false;
	ecp->autoneg = true;
	ecp->speed = ETH_SPEED_100M;
	ecp->duplex = ETH_DUP_FULL;
	ecp->mac_addr = NULL;
	memset(&ecp->multicast_list, 0, sizeof(struct mac_list));
	spin_lock_init(&ecp->lock);

	/*uncache address*/
	ecp->tx_bd_base =
	    (ec_bd_t *) dma_alloc_coherent(&ecp->pdev->dev,
					   sizeof(ec_bd_t) *
					   TX_RING_SIZE,
					   &ecp->tx_bd_paddr, GFP_KERNEL);
	ecp->rx_bd_base =
	    (ec_bd_t *) dma_alloc_coherent(&ecp->pdev->dev,
					   sizeof(ec_bd_t) *
					   RX_RING_SIZE,
					   &ecp->rx_bd_paddr, GFP_KERNEL);
	EC_INFO("ecp->tx_bd_base:%p, ecp->tx_bd_paddr:%p\n",
		ecp->tx_bd_base, (void *)ecp->tx_bd_paddr);
	EC_INFO("ecp->rx_bd_base:%p, ecp->rx_bd_paddr:%p\n",
		ecp->rx_bd_base, (void *)ecp->rx_bd_paddr);

	if (ecp->tx_bd_base == NULL || ecp->rx_bd_base == NULL) {
		EC_ERR
		    ("dma_alloc mem failed, tx_bd_base:%p, rx_bd_base:%p\n",
		     ecp->tx_bd_base, ecp->rx_bd_base);

		if (ecp->tx_bd_base)
			dma_free_coherent(NULL,
					  sizeof(ec_bd_t) *
					  TX_RING_SIZE, ecp->tx_bd_base,
					  ecp->tx_bd_paddr);
		if (ecp->rx_bd_base)
			dma_free_coherent(NULL,
					  sizeof(ec_bd_t) *
					  RX_RING_SIZE, ecp->rx_bd_base,
					  ecp->rx_bd_paddr);

		return -ENOMEM;
	}

	if (ecp->phy_model == ETH_PHY_MODEL_ATC2605) {
		/*For gl5302 INT Pending unusual */
		ethernet_clock_enable();
		ecp->phy_ops->phy_hw_init(ecp);
	}
	ecp->mac_addr = g_default_mac_addr;
	ecp->mac_overrided = false;

	/* should after set_mac_addr() which will set ec_priv_t{.mac_addr} */
	memcpy(dev->dev_addr, ecp->mac_addr, ETH_MAC_LEN);

	ether_setup(dev);

	INFO_GREEN("net device : %s init over.\n", dev->name);

	return 0;
}

static void ec_netdev_uninit(struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);


	/* after all works have been completed and destroyed */
	free_rxtx_skbs(ecp->rx_skb, RX_RING_SIZE);
	free_rxtx_skbs(ecp->tx_skb, TX_RING_SIZE);

	if (ecp->tx_bd_base)
		dma_free_coherent(&ecp->pdev->dev,
				  sizeof(ec_bd_t) * TX_RING_SIZE,
				  ecp->tx_bd_base, ecp->tx_bd_paddr);
	if (ecp->rx_bd_base)
		dma_free_coherent(&ecp->pdev->dev,
				  sizeof(ec_bd_t) * RX_RING_SIZE,
				  ecp->rx_bd_base, ecp->rx_bd_paddr);
}

static const struct net_device_ops ec_netdev_ops = {
	.ndo_init = ec_netdev_init,
	.ndo_uninit = ec_netdev_uninit,
	.ndo_open = ec_netdev_open,
	.ndo_stop = ec_netdev_close,
	.ndo_start_xmit = ec_netdev_start_xmit,
	.ndo_set_rx_mode = ec_netdev_set_multicast_list,
	.ndo_set_mac_address = ec_netdev_set_mac_addr,
	.ndo_get_stats = ec_netdev_query_stats,
	.ndo_tx_timeout = ec_netdev_transmit_timeout,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_do_ioctl = ec_netdev_ioctrl,
};

static struct pinctrl *ppc;

static void ethernet_put_pin_mux(void)
{
	if (ppc)
		pinctrl_put(ppc);
}

static void owl_handle_link_change(struct net_device *dev)
{

	ec_priv_t *ecp = netdev_priv(dev);

	struct phy_device *phydev = ecp->phy;
	unsigned long flags;
	int status_change = 0;

	EC_INFO("owl_handle_link_change:(%d-%d)-(%d-%d)-(%d-%d)\n",
	       ecp->link, phydev->link, ecp->speed, phydev->speed,
	       ecp->duplex, phydev->duplex);
	if (phydev->link) {
		if (ecp->speed != phydev->speed) {
			spin_lock_irqsave(&ecp->lock, flags);
			ecp->speed = phydev->speed;

			spin_unlock_irqrestore(&ecp->lock, flags);
			status_change = 1;
		}

		if (ecp->duplex != phydev->duplex) {
			spin_lock_irqsave(&ecp->lock, flags);
			ecp->duplex = phydev->duplex;

			spin_unlock_irqrestore(&ecp->lock, flags);
			status_change = 1;
		}
	}

	if (phydev->link != ecp->link) {
		if (!phydev->link) {
			ecp->speed = 0;
			ecp->duplex = -1;
		}
		ecp->link = phydev->link;
		if (ecp->link)
			set_mac_according_aneg(ecp);
		status_change = 1;
	}
	if (phydev->link) {
		netif_carrier_on(dev);
		if (netif_queue_stopped(ecp->netdev))
			netif_wake_queue(ecp->netdev);
	} else {
		netif_carrier_off(dev);
	}

	if (status_change) {
		if (phydev->link)
			netif_carrier_on(dev);
		 else
			netif_carrier_off(dev);

		phy_print_status(phydev);
	}

}

static int owl_mdio_init(struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);

	/* to-do: PHY interrupts are currently not supported */

	/* attach the mac to the phy */
	ecp->phy = of_phy_connect(dev, ecp->phy_node,
				  &owl_handle_link_change, 0,
				  ecp->phy_interface);
	if (!ecp->phy) {
		EC_ERR("could not find the PHY\n");
		return -ENODEV;
	}

	EC_INFO("connect the phy:-%d\n", ecp->phy->irq);
	/* mask with MAC supported features */
	ecp->phy->supported &= PHY_BASIC_FEATURES;
	ecp->phy->advertising = ecp->phy->supported;

	ecp->link = 0;
	ecp->speed = 0;
	ecp->duplex = -1;

	return 0;
}

static void owl_mdio_remove(struct net_device *dev)
{
	ec_priv_t *ecp = netdev_priv(dev);

	phy_disconnect(ecp->phy);
	ecp->phy = NULL;
}

static int __init asoc_ethernet_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	ec_priv_t *ecp;
	int ret;

	struct resource *res;

	int phy_mode = ETH_PHY_MODE_RMII;	/* use rmii as default */
	const char *phy_mode_str;
	const char *phy_status_str;
	struct device_node *phy_node;

	if (!get_def_mac_addr(pdev))
		EC_INFO("use same default mac\n");

	ret =
	    of_property_read_string(pdev->dev.of_node, "status",
				    &phy_status_str);
	if (ret == 0 && strcmp(phy_status_str, "okay") != 0) {
		dev_info(&pdev->dev, "disabled by DTS\n");
		return -ENODEV;
	}
	ret =
	    of_property_read_string(pdev->dev.of_node, "phy-mode",
				    &phy_mode_str);
	if (ret) {
		/* if get phy mode failed, use default rmii */
		EC_ERR("get phy mode failed, use rmii\n");
	} else {
		EC_INFO("phy_mode_str: %s\n", phy_mode_str);
		if (!strcmp(phy_mode_str, "rmii"))
			phy_mode = ETH_PHY_MODE_RMII;
		else if (!strcmp(phy_mode_str, "smii"))
			phy_mode = ETH_PHY_MODE_SMII;
		else
			EC_ERR("unknown phy mode: %s, use rmii\n",
			       phy_mode_str);
	}

	EC_INFO("pdev->name: %s\n", pdev->name ? pdev->name : "<null>");
	dev = alloc_etherdev(sizeof(struct dev_priv));
	if (NULL == dev)
		return -ENOMEM;
	sprintf(dev->name, "eth%d", 0);
	ecp = netdev_priv(dev);
	ecp->pdev = pdev;
	g_eth_dev[0] = dev;
	ecp->phy_mode = phy_mode;
	EC_INFO("phy_mode: %u\n", ecp->phy_mode);

	SET_NETDEV_DEV(dev, &pdev->dev);
	platform_set_drvdata(pdev, dev);

	ethernet_clk = devm_clk_get(&pdev->dev, "eth_mac");

	ecp->clk = devm_clk_get(&pdev->dev, "rmii_ref");
	EC_INFO("####ecp->clk: %ld #####\n", clk_get_rate(ecp->clk));
	if (IS_ERR(ecp->clk)) {
		EC_ERR("get clk failed!\n");
		return ret;
	}

	phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (!phy_node) {
		EC_ERR("phy-handle of ethernet node can't be parsed\n");
		return -EINVAL;
	}
	ecp->phy_node = phy_node;
	ecp->phy_model = ETH_PHY_MODEL_KSZ8081;

	ret = -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		EC_ERR("no mem resource\n");
		goto err_free;
	}

	ecp->hwrp = devm_ioremap(&pdev->dev, res->start, resource_size(res));

	EC_INFO("res->start: %p, ecp->hwrp: %p\n",
		(void *)res->start, (void *)ecp->hwrp);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		EC_ERR("no irq resource\n");
		goto err_free;
	}
	ecp->mac_irq = res->start;

	EC_INFO("ecp->mac_irq: %u, dev->irq: %u\n", ecp->mac_irq, dev->irq);

	dev->watchdog_timeo = EC_TX_TIMEOUT;
	dev->netdev_ops = &ec_netdev_ops;

	if (_init_hardware(ecp)) {
		EC_ERR("error: harware initialization failed.\n");
		return -1;
	}

	/*NOTE:don't forget to set dma_mask, otherwise*/
	/*dma_map_single() will get wrong phy addr */
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	ret = register_netdev(dev);
	if (ret < 0) {
		EC_ERR
		    ("register netdev ret:%d, [irq:%d, dev name:%s]\n",
		     ret, dev->irq, dev->name ? dev->name : "Unknown");
		g_eth_dev[0] = NULL;
		goto err_free;
	}

	ret = owl_mdio_init(dev);
	if (ret < 0) {
		EC_ERR("cannot probe MDIO bus\n");
		return ret;
	}

	device_create_file(&dev->dev, &dev_attr_netif_msg);
#ifdef ETH_TEST_MODE
	device_create_file(&dev->dev, &dev_attr_continuity);
	device_create_file(&dev->dev, &dev_attr_send_pattern);
	device_create_file(&dev->dev, &dev_attr_test_mode);
#endif
	resume_queue = create_workqueue("kethernet_resume_work");
	if (!resume_queue) {
		EC_ERR("create workqueue kethernet_resume_work failed\n");
		ret = -ENOMEM;
		goto err_remove;
	}

	INIT_DELAYED_WORK(&resume_work, ethernet_resume_handler);

	INIT_WORK(&ecp->hardware_reset_work, hardware_reset_do_work);
	return 0;

err_remove:
	device_remove_file(&dev->dev, &dev_attr_netif_msg);
#ifdef ETH_TEST_MODE
	device_remove_file(&dev->dev, &dev_attr_send_pattern);
	device_remove_file(&dev->dev, &dev_attr_test_mode);
	device_remove_file(&dev->dev, &dev_attr_continuity);
#endif
	unregister_netdev(dev);

err_free:
	if (dev) {
		free_netdev(dev);
		dev = NULL;
	}
	return ret;
}

static int __exit asoc_ethernet_remove(struct platform_device *pdev)
{
	struct net_device *dev = g_eth_dev[0];

	EC_INFO("\n");
	if (dev) {
		device_remove_file(&dev->dev, &dev_attr_netif_msg);
#ifdef ETH_TEST_MODE
		device_remove_file(&dev->dev, &dev_attr_send_pattern);
		device_remove_file(&dev->dev, &dev_attr_test_mode);
		device_remove_file(&dev->dev, &dev_attr_continuity);
#endif
		unregister_netdev(dev);
		free_netdev(dev);
	}
	if (resume_queue)
		destroy_workqueue(resume_queue);

#ifdef DETECT_POWER_SAVE
	destroy_workqueue(power_save_queue);
#endif
	owl_mdio_remove(dev);

	ethernet_put_pin_mux();
	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id actions_ethernet_of_match[] __initconst = {
	{.compatible = "actions,s900-ethernet",},
	{},
};

MODULE_DEVICE_TABLE(of, actions_ethernet_of_match);

static struct platform_driver asoc_ethernet_driver = {
	.probe = asoc_ethernet_probe,
	.remove = __exit_p(asoc_ethernet_remove),
	.driver = {
		   .name = "asoc-ethernet",
		   .owner = THIS_MODULE,
		   .of_match_table = actions_ethernet_of_match,
		   },
	.suspend = asoc_ethernet_suspend,
	.resume = asoc_ethernet_resume,
};

static int __init asoc_ethernet_init(void)
{
	return platform_driver_register(&asoc_ethernet_driver);
}

module_init(asoc_ethernet_init);

static void __exit asoc_ethernet_exit(void)
{
	platform_driver_unregister(&asoc_ethernet_driver);
}

module_exit(asoc_ethernet_exit);

MODULE_ALIAS("platform:asoc-ethernet");
MODULE_DESCRIPTION("asoc_ethernet pin");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Actions Semi, Inc");
