/*
 * drivers/net/phy/realtek.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/phy.h>
#include <linux/module.h>

#define RTL8201F_INSR		0x1e
#define RTL8201F_PGSR		0x1f
#define RTL8201F_INER		0x13
#define RTL8201F_INER_MASK	0x3800

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13

#define	RTL8211E_INER_LINK_STAT	0x10

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

static int rtl8201f_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL8201F_INSR);

	return (err < 0) ? err : 0;
}
static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static void rtl8201f_select_page(struct phy_device *phydev, int page)
{
	phy_write(phydev, RTL8201F_PGSR, page);
}

static int rtl8201f_config_intr(struct phy_device *phydev)
{
	int err;
	int temp;//zakktest
	
	rtl8201f_select_page(phydev, 7);

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL8201F_INER, RTL8201F_INER_MASK |	phy_read(phydev, RTL8201F_INER));
	else
		err = phy_write(phydev, RTL8201F_INER, ~RTL8201F_INER_MASK & phy_read(phydev, RTL8201F_INER));

//#if 1//zakktest
//	temp = phy_read(phydev, 16);
//	phy_write(phydev, 16, temp & ~0x1000);//page7 register16 bit12, Rg_rmii_clkdir set 0 for output clock to s900 ethernet MAC
//#endif

	rtl8201f_select_page(phydev, 0);


	return err;
}

static int rtl8201f_config_init(struct phy_device *phydev)
{
	int err;
	int temp;

	rtl8201f_select_page(phydev, 7);

	/* for s700 */
	temp = phy_read(phydev, 16);
#ifdef  CONFIG_S700_REALTEK_PHY

	/* for s700 */
	temp &= ~0x0FF2;	/* CRS/CRS_DV pin set as CRS_DV signal */
	phy_write(phydev, 16, (temp | (0x4 << 4) | (0x5 << 8)));		/* rmii tx/rx interface timing */
#else
	phy_write(phydev, 16, temp & ~0x1000);//page7 register16 bit12, Rg_rmii_clkdir set 0 for output clock to s900 etherne
#endif
	printk("%s,reg16:0x%x\n", __func__, phy_read(phydev, 16));
	rtl8201f_select_page(phydev, 0);


	return err;
}

static int rtl8211b_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER, RTL821x_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER, RTL8211E_INER_LINK_STAT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static inline void phy_reg_set_bits(struct phy_device *ecp,
					unsigned short reg_addr, int bits)
{
	unsigned short reg_val;

	reg_val = phy_read(ecp, reg_addr);
	reg_val |= (unsigned short)bits;
	phy_write(ecp, reg_addr, reg_val);

	reg_val = phy_read(ecp, reg_addr);
}

static int rtl8211e_resume(struct phy_device *phydev)
{
	int reg_val;
	int cnt = 0;

	phy_reg_set_bits(phydev, MII_BMCR, BMCR_RESET);
	do {
		reg_val = phy_read(phydev, MII_BMCR);
		if (cnt++ > 10000)
		{
			pr_warn("ethernet phy BMCR_RESET timeout!!!\n");
			return -EBUSY;
		}
	} while (reg_val & BMCR_RESET);

	return 0;
}

static int rtl8211e_config_init(struct phy_device *phydev)
{
	int reg_val;
	int cnt = 0;

	phy_reg_set_bits(phydev, MII_BMCR, BMCR_RESET);
	do {
		reg_val = phy_read(phydev, MII_BMCR);
		if (cnt++ > 10000)
		{
			pr_warn("ethernet phy BMCR_RESET timeout!!!\n");
			return -EBUSY;
		}
	} while (reg_val & BMCR_RESET);

	return 0;
}


/* RTL8211B */
static struct phy_driver rtl8211b_driver = {
	.phy_id		= 0x001cc912,
	.name		= "RTL8211B Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.ack_interrupt	= &rtl821x_ack_interrupt,
	.config_intr	= &rtl8211b_config_intr,
	.driver		= { .owner = THIS_MODULE,},
};

/* RTL8211E */
static struct phy_driver rtl8211e_driver = {
	.phy_id		= 0x001cc915,
	.name		= "RTL8211E Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
	.features	= PHY_GBIT_FEATURES,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.ack_interrupt	= &rtl821x_ack_interrupt,
	.config_intr	= &rtl8211e_config_intr,
	.config_init	= &rtl8211e_config_init,
	.suspend	= genphy_suspend,
	.resume		= rtl8211e_resume,
	.driver		= { .owner = THIS_MODULE,},
};

/* RTL8201F */
static struct phy_driver rtl8201f_driver = {
		.phy_id		= 0x001cc816,
		.name		= "RTL8201F 10/100Mbps Ethernet",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_BASIC_FEATURES,
		.config_aneg	= &genphy_config_aneg,
		.read_status	= &genphy_read_status,
		.ack_interrupt	= &rtl8201f_ack_interrupt,
		.config_intr  = &rtl8201f_config_intr,
		.config_init	= &rtl8201f_config_init,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.driver		= { .owner = THIS_MODULE,},
	#ifdef  CONFIG_S700_REALTEK_PHY
		//.flags		= PHY_HAS_INTERRUPT,
	#else
		.flags		= PHY_HAS_INTERRUPT,
	#endif
	};

static int __init realtek_init(void)
{
	int ret;

	ret = phy_driver_register(&rtl8201f_driver);
	if (ret < 0)	
		return -ENODEV;	

	ret = phy_driver_register(&rtl8211b_driver);
	if (ret < 0)
		return -ENODEV;
	return phy_driver_register(&rtl8211e_driver);
}

static void __exit realtek_exit(void)
{
	phy_driver_unregister(&rtl8201f_driver);
	phy_driver_unregister(&rtl8211b_driver);
	phy_driver_unregister(&rtl8211e_driver);
}

module_init(realtek_init);
module_exit(realtek_exit);

static struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ 0x001cc816, 0x001fffff },
	{ 0x001cc912, 0x001fffff },
	{ 0x001cc915, 0x001fffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
