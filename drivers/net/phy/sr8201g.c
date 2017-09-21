/*
 * drivers/net/phy/micrel.c
 *
 * Driver for sr8201g PHYs
 *
 * Author: David J. Choi
 *
 * Copyright (c) 2010-2013 Micrel, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : sr8201g Phys:

 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/delay.h>
/*
 * phy SR8201G
 */
#define PHY_SR8201G_REG_RMSR           0xC
#define PHY_SR8201G_REG_INT_LED_FUNC   0xE
#define PHY_SR8201G_REG_PAGE_SELECT    0xF
#define PHY_SR8201G_REG_EXPAND         0x18

/* PHY_SR8201G_PAGE_SELECT */
#define PHY_SR8201G_REG_PAGE_SELECT_SEVEN    0x7
#define PHY_SR8201G_REG_PAGE_SELECT_ZERO     0x0

#define PHY_SR8201G_INT_PIN_SELECT             (0x1<<10)
#define PHY_SR8201G_INT_PIN_LINK_STATE_CHANGE  (0x1<<11)
#define PHY_SR8201G_CLK_DIR_INPUT	           (0x1<<12)

#define PHY_ID_SR8201G 0x001D2411

static int sr8201g_config_init(struct phy_device *phydev)
{

	phy_write(phydev, PHY_SR8201G_REG_PAGE_SELECT,
		  PHY_SR8201G_REG_PAGE_SELECT_SEVEN);

	/* only turn on link up/down phy interrupt */

	phy_write(phydev, PHY_SR8201G_REG_RMSR, PHY_SR8201G_CLK_DIR_INPUT);
	phy_write(phydev, PHY_SR8201G_REG_PAGE_SELECT,
		  PHY_SR8201G_REG_PAGE_SELECT_ZERO);
	return 0;
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

static int sr8201g_probe(struct phy_device *phydev)
{
	pr_info("sr8201g_probe:\n");
	int reg_val;
	int cnt = 0;

	phy_reg_set_bits(phydev, MII_BMCR, BMCR_RESET);
	do {
		reg_val = phy_read(phydev, MII_BMCR);
		if (cnt++ > 10000)
			pr_warn("ethernet phy BMCR_RESET timeout!!!\n");
	} while (reg_val & BMCR_RESET);

	phy_write(phydev, PHY_SR8201G_REG_PAGE_SELECT,
		  PHY_SR8201G_REG_PAGE_SELECT_SEVEN);

	/* only turn on link up/down phy interrupt */
	phy_write(phydev, PHY_SR8201G_REG_RMSR, PHY_SR8201G_CLK_DIR_INPUT);
	mdelay(1);
	phy_write(phydev, PHY_SR8201G_REG_PAGE_SELECT,
		  PHY_SR8201G_REG_PAGE_SELECT_ZERO);

	return 0;
}

static int kszphy_config_init(struct phy_device *phydev)
{
	return 0;
}

int sr8201g_suspend(struct phy_device *phydev)
{
	pr_info("sr8201g_suspend\n");
	return 0;
}

int sr8201g_resume(struct phy_device *phydev)
{
	unsigned int reg_val;
	int cnt = 0;
	pr_info("sr8201g_resume\n");
	phy_reg_set_bits(phydev, MII_BMCR, BMCR_RESET);
	do {
		reg_val = phy_read(phydev, MII_BMCR);
		if (cnt++ > 10000)
			pr_info("ethernet phy BMCR_RESET timeout!!!\n");
	} while (reg_val & BMCR_RESET);

	phy_write(phydev, PHY_SR8201G_REG_PAGE_SELECT,
		  PHY_SR8201G_REG_PAGE_SELECT_SEVEN);

	/* only turn on link up/down phy interrupt */
	phy_write(phydev, PHY_SR8201G_REG_RMSR, PHY_SR8201G_CLK_DIR_INPUT);
	mdelay(1);
	phy_write(phydev, PHY_SR8201G_REG_PAGE_SELECT,
		  PHY_SR8201G_REG_PAGE_SELECT_ZERO);

	return 0;
}

static struct phy_driver sr8201g_driver[] = {
	{
	 .phy_id = PHY_ID_SR8201G,
	 .name = "SR8201G",
	 .phy_id_mask = 0x00ffffff,
	 .features = (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	 .flags = PHY_HAS_MAGICANEG,
	 .config_init = kszphy_config_init,
	 .config_aneg = genphy_config_aneg,
	 .read_status = genphy_read_status,
	 .probe = sr8201g_probe,
	 .read_status = genphy_read_status,
	 .suspend = sr8201g_suspend,
	 .resume = sr8201g_resume,
	 .driver = {.owner = THIS_MODULE,},
	 }
};

static int __init sr8201g_init(void)
{
	return phy_drivers_register(sr8201g_driver, ARRAY_SIZE(sr8201g_driver));
}

static void __exit sr8201g_exit(void)
{
	phy_drivers_unregister(sr8201g_driver, ARRAY_SIZE(sr8201g_driver));
}

module_init(sr8201g_init);
module_exit(sr8201g_exit);

MODULE_DESCRIPTION("SR8201G PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused sr_tbl[] = {
	{PHY_ID_SR8201G, 0x000ffffe},
	{}
};

MODULE_DEVICE_TABLE(mdio, sr_tbl);
