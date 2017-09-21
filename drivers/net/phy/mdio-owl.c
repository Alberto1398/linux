/*
 * Actions OWL MDIO BUS driver
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

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/reset.h>
#include <linux/io.h>

/*
 * Should not read and write er_reserved* elements
 */
typedef struct ethregs {
	unsigned int er_busmode;	/* offset 0x0; bus mode reg. */
	unsigned int er_reserved0;
	unsigned int er_txpoll;	/* 0x08; transmit poll demand reg. */
	unsigned int er_reserved1;
	unsigned int er_rxpoll;	/* 0x10; receive poll demand reg. */
	unsigned int er_reserved2;
	unsigned int er_rxbdbase;
	/* 0x18; receive descriptor list base address reg. */
	unsigned int er_reserved3;
	unsigned int er_txbdbase;
	/* 0x20; transmit descriptor list base address reg. */
	unsigned int er_reserved4;
	unsigned int er_status;	/* 0x28; status reg. */
	unsigned int er_reserved5;
	unsigned int er_opmode;	/* 0x30; operation mode reg. */
	unsigned int er_reserved6;
	unsigned int er_ienable;	/* 0x38; interrupt enable reg. */
	unsigned int er_reserved7;
	unsigned int er_mfocnt;
	/* 0x40; missed frames and overflow counter reg. */
	unsigned int er_reserved8;
	unsigned int er_miimng;	/* 0x48; software mii, don't use it here  */
	unsigned int er_reserved9;
	unsigned int er_miism;	/* 0x50; mii serial management */
	unsigned int er_reserved10;
	unsigned int er_imctrl;
	/* 0x58; general-purpose timer and interrupt mitigation control */
	unsigned int er_reserved11[9];
	unsigned int er_maclow;	/* 0x80; mac address low */
	unsigned int er_reserved12;
	unsigned int er_machigh;	/* 0x88; mac address high */
	unsigned int er_reserved13;
	unsigned int er_cachethr;
	/* 0x90; pause time and cache thresholds */
	unsigned int er_reserved14;
	unsigned int er_fifothr;
	/* 0x98; pause control fifo thresholds */
	unsigned int er_reserved15;
	unsigned int er_flowctrl;
	/* 0xa0; flow control setup and status */
	unsigned int er_reserved16[3];
	unsigned int er_macctrl;	/* 0xb0; mac control */
	unsigned int er_reserved17[83];

	unsigned int er_rxstats[31];
	/* 0x200~0x278; receive statistics regs. */
	unsigned int er_reserved18[33];
	unsigned int er_txstats[41];
	/* 0x300~0x3A0; transmit statistics regs. */
	unsigned int er_reserved19[23];
} ethregs_t;

#define ASOC_ETHERNET_PHY_ADDR (0x3)
#define MII_MNG_SB  (0x1 << 31)	/*start transfer or busy */
#define MII_MNG_CLKDIV(x) (((x) & 0x7) << 28)	/*clock divider */
#define MII_MNG_OPCODE(x) (((x) & 0x3) << 26)	/*operation mode */
#define MII_MNG_PHYADD(x) (((x) & 0x1F) << 21)
/*physical layer address */
#define MII_MNG_REGADD(x) (((x) & 0x1F) << 16)	/*register address */
#define MII_MNG_DATAM (0xFFFF)	/*register data mask */
#define MII_MNG_DATA(x)   ((MII_MNG_DATAM) & (x))	/* data to write */
#define MII_OP_WRITE 0x1
#define MII_OP_READ  0x2
#define MII_OP_CDS   0x3

#define PHY_BASIC_STATUS (0x1)
#define MII_ICSR (0x1b)		/* interrupt control & status register */

#define ICSR_LINKUP_EN   (0x1 << 8)
#define ICSR_LINKDOWN_EN (0x1 << 10)

#define ICSR_LINKUP   (0x1 << 0)
#define ICSR_LINKDOWN (0x1 << 2)

#define MII_PHY_CTL1 (0x1e)
#define MII_PHY_CTL2 (0x1f)
#define PHY_CTL2_INT_LEVEL (0x1 << 9)	/* interrupt pin active high:1, low:0 */
#define PHY_CTL2_RMII_50M (0x1 << 7)

#define MDIO_TIMEOUT		(msecs_to_jiffies(100))

struct owl_mdio_data {
	volatile ethregs_t *hwrp;
	struct regulator *regulator;
	struct mii_bus *bus;
	struct platform_device *dev;
};
struct owl_mdio_data data;


static int resume_flag;
static struct clk *ethernet_clk;

static void enablemdio(int first);
static void disablemdio(int first);
static int write_phy_reg(struct mii_bus *bus, int regnum, int value);
static int read_phy_reg(struct mii_bus *bus, int regnum);
static int first;
static int phyaddr;

static int read_phy_reg(struct mii_bus *bus, int regnum)
{
	u32 op_reg;

	if (resume_flag) {
		if (regnum)
			printk("read  reg:%x but not init\n", regnum);
		return -1;
	}
	do {
		op_reg = data.hwrp->er_miism;
	} while (op_reg & MII_MNG_SB);

	data.hwrp->er_miism =
	    MII_MNG_SB | MII_MNG_OPCODE(MII_OP_READ) | MII_MNG_REGADD(regnum) |
	    MII_MNG_PHYADD(phyaddr);

	do {
		op_reg = data.hwrp->er_miism;
	} while (op_reg & MII_MNG_SB);

	return (u16) MII_MNG_DATA(op_reg);
}

static int write_phy_reg(struct mii_bus *bus, int regnum, int value)
{
	u32 op_reg;
	if (regnum == 0xff) {
		disablemdio(0);
		mdelay(200);
		enablemdio(0);
		return 0;
	}
	if (resume_flag) {
		if (regnum)
			printk("write reg:%x but return\n", regnum);
		return -1;
	}
	do {
		op_reg = data.hwrp->er_miism;
	} while (op_reg & MII_MNG_SB);

	data.hwrp->er_miism =
	    MII_MNG_SB | MII_MNG_OPCODE(MII_OP_WRITE) | MII_MNG_REGADD(regnum) |
	    MII_MNG_PHYADD(phyaddr) | value;
	do {
		op_reg = data.hwrp->er_miism;
	} while (op_reg & MII_MNG_SB);

	return 0;
}

static inline void phy_reg_set_bits(struct mii_bus *ecp,
				    unsigned short reg_addr, int bits)
{
	unsigned short reg_val;

	reg_val = read_phy_reg(ecp, reg_addr);
	reg_val |= (unsigned short)bits;
	write_phy_reg(ecp, reg_addr, reg_val);

	reg_val = read_phy_reg(ecp, reg_addr);
}

static int phy_init(struct mii_bus *ecp)
{
	int reg_val;
	unsigned int cnt = 0;

	phy_reg_set_bits(ecp, MII_BMCR, BMCR_RESET);
	do {
		reg_val = read_phy_reg(ecp, MII_BMCR);
		if (cnt++ > 10000) {
			printk("ethernet phy BMCR_RESET timeout!!!\n");
			break;
		}
	} while (reg_val & BMCR_RESET);

	{
		/* only turn on link up/down phy interrupt */
		phy_reg_set_bits(ecp, MII_ICSR,
				 ICSR_LINKUP_EN | ICSR_LINKDOWN_EN);

		phy_reg_set_bits(ecp, MII_PHY_CTL2, PHY_CTL2_INT_LEVEL);
		printk("phy init finish\n");
	}
	return 0;
}

static void mdio_clock_disable(struct platform_device *pdev)
{
	clk_disable(ethernet_clk);
	clk_unprepare(ethernet_clk);
	devm_clk_put(&pdev->dev, ethernet_clk);
	udelay(100);
}

static void mdio_clock_enable(struct platform_device *pdev)
{
	struct reset_control *rst;
	int ret;

	ret = clk_prepare(ethernet_clk);
	printk("####ethernet_clk: %ld #####\n", clk_get_rate(ethernet_clk));
	if (ret)
		printk("prepare ethernet clock faild,errno: %d\n", -ret);

	ret = clk_prepare_enable(ethernet_clk);
	if (ret)
		printk("prepare and enable ethernet clock failed, errno: %d\n",
		       ret);

	ret = clk_enable(ethernet_clk);
	if (ret)
		printk("enable ethernet clock failed, errno: %d\n", -ret);
	udelay(100);

	/* reset ethernet clk */
	rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rst)) {
		printk("Couldn't get ethernet reset\n");
		return;
	}

	/* Reset the UART controller to clear all previous status. */
	reset_control_assert(rst);
	udelay(10);
	reset_control_deassert(rst);
	udelay(100);
	data.hwrp->er_miism |= MII_MNG_SB | MII_MNG_CLKDIV(4) | MII_MNG_OPCODE(3);
	udelay(100);
}

static void enablemdio(int first)
{
	int ret = regulator_enable(data.regulator);
	mdelay(500);

	if (first)
		mdio_clock_enable(data.dev);
	printk("enable mdio:%d-%d\n", first, ret);
}

static void disablemdio(int first)
{
	printk("disable mdio:%d\n", first);

	if (first)
		mdio_clock_disable(data.dev);
	regulator_disable(data.regulator);
}

static int owl_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	return read_phy_reg(bus, regnum);
}

static int owl_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
			  u16 value)
{
	write_phy_reg(bus, regnum, value);
	return 0;
}

static int owl_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mii_bus *bus;

	struct resource *res;
	int ret, i;
	const char *pm;

	bus = mdiobus_alloc_size(sizeof(data));
	if (!bus)
		return -ENOMEM;

	bus->name = "owl_mii_bus";

	bus->read = &owl_mdio_read;
	bus->write = &owl_mdio_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(&pdev->dev));
	bus->parent = &pdev->dev;

	bus->irq = devm_kzalloc(&pdev->dev, sizeof(int) * PHY_MAX_ADDR,
				GFP_KERNEL);
	if (!bus->irq) {
		ret = -ENOMEM;
		goto err_out_free_mdiobus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		bus->irq[i] = PHY_POLL;

	data.bus = bus;
	data.dev = pdev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ethernet_clk = devm_clk_get(&pdev->dev, "eth_mac");
	data.hwrp = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	printk("devm_ioremap:%p\n", data.hwrp);
	if (IS_ERR((void *)data.hwrp)) {
		ret = PTR_ERR(data.hwrp);
		goto err_out_free_mdiobus;
	}
	if (of_find_property(np, "phy-power-gpios", NULL)) {
		ret = of_property_read_string(np, "phy-power-gpios", &pm);
		if (ret < 0) {
			printk
			    ("can not read regulator for ethernet phy power!\n");
			return -1;
		}

		data.regulator = regulator_get(NULL, pm);
		if (IS_ERR(data.regulator)) {
			data.regulator = NULL;
			printk("%s:failed to get regulator!\n", __func__);
			return -1;
		}
		first = 1;
		enablemdio(first);
		printk("get ethernet phy regulator success.%d-%d\n", ret, first);

	}
	if (of_find_property(np, "phy-addr", NULL)) {
		ret = of_property_read_string(np, "phy-addr", &pm);
		if (ret < 0) {
			printk("can not read phy addr\n");
			return -1;
		}

		phyaddr = simple_strtoul(pm, NULL, 0);
		printk("get phy addr success.%d-%d\n", phyaddr);

	}
	phy_init(bus);

	ret = of_mdiobus_register(bus, np);
	if (ret < 0)
		goto err_out_disable_regulator;

	platform_set_drvdata(pdev, bus);

	return 0;

err_out_disable_regulator:
	regulator_disable(data.regulator);
err_out_free_mdiobus:
	mdiobus_free(bus);
	return ret;
}

static int owl_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);
	mdiobus_free(bus);

	if (data.regulator) {
		regulator_disable(data.regulator);
		printk("Regulator ethernet phy power off when remove\n");
	}

	return 0;
}

static int owl_mdio_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (data.regulator) {
		printk("owl_mdio_suspend:%x-%x-%x\n", MII_BMCR,
	    data.hwrp->er_miism, read_phy_reg(data.bus, 0));

	 {
		/* only turn on link up/down phy interrupt */
		  write_phy_reg(data.bus, MII_ICSR, 0);
	 }
	 disablemdio(first);
		first = 0;
		printk("owl_mdio_suspend disabe regulator:%d-\n", first);
		resume_flag = 1;
	}
	return 0;
}

static int owl_mdio_resume(struct platform_device *pdev)
{
	if (data.regulator) {
		printk("owl_mdio_resume:%x-%x\n", MII_BMCR,
		data.hwrp->er_miism);
		enablemdio(first);
		resume_flag = 0;
	}
	return 0;
}

static const struct of_device_id owl_mdio_dt_ids[] = {
	{.compatible = "actions,owl-mdio-bus"},

	/* Deprecated */
	{.compatible = "actions,mdio-bus"},
	{}
};

MODULE_DEVICE_TABLE(of, owl_mdio_dt_ids);

static struct platform_driver owl_mdio_driver = {
	.probe = owl_mdio_probe,
	.remove = owl_mdio_remove,
	.suspend = owl_mdio_suspend,
	.resume = owl_mdio_resume,
	.driver = {
		   .name = "mdio-bus",
		   .of_match_table = owl_mdio_dt_ids,
		   },
};

module_platform_driver(owl_mdio_driver);

MODULE_DESCRIPTION("ACTIONS EMAC MDIO interface driver");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_LICENSE("GPL");
