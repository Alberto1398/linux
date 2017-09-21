/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/8/24: Created by Lipeng.
 */
#define DEBUGX
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>

#include <video/owl_dss.h>

#define EDP2DSI_ADDR		0x1c >> 1 /*TODO*/

struct edp_i2c_dev {
	struct mutex lock;
	struct i2c_client *client;
	
	struct regulator		*i2c4_vcc;
};

static struct edp_i2c_dev *edp_i2c_pdev;

static const struct i2c_device_id edp2dsi_id[] = {
	{ "edp2dsi_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, edp2dsi_id);

/*
 * Struct used for matching a device
 */
static const struct of_device_id edp_i2c_of_match[] = {
	{ "actions,edp2dsi_i2c" },	/* TODO, rename */
	{ }
};
MODULE_DEVICE_TABLE(of, edp_i2c_of_match);


struct panel_tc358860_data {
	struct owl_dss_gpio	reset_gpio;
	struct owl_dss_gpio	reset1_gpio;
	struct owl_dss_gpio	power_gpio;
	struct owl_dss_gpio	power1_gpio;
	struct owl_dss_gpio	power2_gpio;
	enum owl_dss_state	state;

	struct platform_device	*pdev;

	/* others can be added here */
};

static void __panel_tc358860_power_on(struct panel_tc358860_data *tc358860)
{
	unsigned int val;

	pr_info("%s, start!", __func__);

	/* tc358860 IC deassert */
	owl_dss_gpio_deactive(&tc358860->reset_gpio);
	msleep(10);
	/* tc358860 IC power on */
	owl_dss_gpio_active(&tc358860->power_gpio);
	msleep(10);

	/* tc358860 IC assert */
	owl_dss_gpio_active(&tc358860->reset_gpio);
	msleep(10);
	/* tc358860 IC deassert */
	owl_dss_gpio_deactive(&tc358860->reset_gpio);

	/* panel assert */
	owl_dss_gpio_active(&tc358860->reset1_gpio);

	/* pull low level, panel vddio */
	owl_dss_gpio_deactive(&tc358860->power1_gpio);
	/* pull low level, panel vsp */
	owl_dss_gpio_deactive(&tc358860->power2_gpio);
	mdelay(10);

	/* pull hight level, panel vddio */
	owl_dss_gpio_active(&tc358860->power1_gpio);
	mdelay(200);
	/* pull hight level, panel vsp */
	owl_dss_gpio_active(&tc358860->power2_gpio);
	mdelay(10);

	/* panel deassert */
	owl_dss_gpio_deactive(&tc358860->reset1_gpio);
}

static void __panel_tc358860_power_off(struct panel_tc358860_data *tc358860)
{
	/* panel assert */
	owl_dss_gpio_active(&tc358860->reset1_gpio);
	mdelay(10);
	/* pull low level, panel vsp */
	owl_dss_gpio_deactive(&tc358860->power2_gpio);
	mdelay(100);
	/* pull low level, panel vddio */
	owl_dss_gpio_deactive(&tc358860->power1_gpio);

	/* tc358860 IC assert */
	owl_dss_gpio_active(&tc358860->reset_gpio);
	mdelay(10);
	/* pull low level, tc358860 power */
	owl_dss_gpio_deactive(&tc358860->power_gpio);

}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_tc358860_power_on(struct owl_panel *panel)
{
	struct panel_tc358860_data *tc358860 = panel->pdata;
	struct owl_display_ctrl	*ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);

	dev_info(&tc358860->pdev->dev, "%s, ctrl_is_enabled %d\n",
			__func__, ctrl_is_enabled);

	if (tc358860->state != OWL_DSS_STATE_ON) {
		tc358860->state = OWL_DSS_STATE_ON;
		if (!ctrl_is_enabled)
			__panel_tc358860_power_on(tc358860);
	}

	return 0;
}

static int panel_tc358860_power_off(struct owl_panel *panel)
{
	struct panel_tc358860_data *tc358860 = panel->pdata;
	dev_info(&tc358860->pdev->dev, "%s\n", __func__);

	if (tc358860->state != OWL_DSS_STATE_OFF) {
		tc358860->state = OWL_DSS_STATE_OFF;
		__panel_tc358860_power_off(tc358860);
	}

	return 0;
}

void new_edp_i2c_write(u32 addr, u32 cnt, u64 dat)
{
	u32 tmp,i;
	int ret;
	char buffer[6];

	struct i2c_client *client;
	struct i2c_adapter *adap;
	struct i2c_msg msg;

	pr_debug("%s, addr 0x%x, cnt %d, data 0x%lx\n", __func__, addr, cnt, dat);

	if (edp_i2c_pdev->client == NULL) {
		pr_err("no I2C adater\n");
		return -ENODEV;
	}
	
	client = edp_i2c_pdev->client;
	adap = client->adapter;

	for (i = 0; i < cnt; i++) {
		buffer[i] = (dat >> (8 * (cnt - i - 1))) & 0xff;
		pr_debug("buffer[%d] 0x%x\n", i, buffer[i]);
	}

	msg.addr = EDP2DSI_ADDR;
	msg.flags = client->flags | I2C_M_IGNORE_NAK;
	msg.len = cnt;
	msg.buf = (uint8_t *)&buffer;

	ret = i2c_transfer(adap, &msg, 1);
	if (ret < 0)
		pr_err("i2c_transfer error!\n");
}

void new_i2c1_edp2dsi_write32(u32 addr, u32 dat)
{
	u64 dat_tmp, tmp[4] = {0};

	tmp[0] = dat & 0xff;
	tmp[1] = (dat >> 8) & 0xff;
	tmp[2] = (dat >> 16) & 0xff;
	tmp[3] = (dat >> 24) & 0xff;

	dat = (tmp[0] << 24) | (tmp[1] << 16) | (tmp[2] << 8) | (tmp[3]);

	dat_tmp = ((u64)addr << 32) | dat;

	new_edp_i2c_write(addr, 0x06, dat_tmp);
}
void new_i2c1_edp2dsi_write8(u32 addr,u32 dat)
{
	u64 dat_tmp;
	dat_tmp = (addr << 8) | dat;

	new_edp_i2c_write(0x68, 0x03, dat_tmp);
}

u32 new_edp_i2c_read(u32 addr, uint8_t *buffer, u32 cnt)
{
	u32 tmp, dat, i;
	int ret;

	struct i2c_client *client;
	struct i2c_adapter *adap;
	struct i2c_msg msg[2];

	char addr_buffer[2];

	pr_debug("%s, cnt %d\n", __func__, cnt);

	if (edp_i2c_pdev->client == NULL) {
		pr_err("no I2C adater\n");
		return -ENODEV;
	}

	client = edp_i2c_pdev->client;
	adap = client->adapter;

	/*
	 * chip addr Big-endian and little-endian swap TODO
	 * */
	for (i = 0; i < 2; i++) {
		addr_buffer[i] = (addr >> (8 * (2 - i - 1))) & 0xff;
		pr_debug("addr_buffer[%d] 0x%x\n", i, addr_buffer[i]);
	}

	msg[0].addr = EDP2DSI_ADDR;
	msg[0].flags = client->flags | I2C_M_IGNORE_NAK;
	msg[0].buf = (unsigned char *)&addr_buffer;
	msg[0].len = 2; /* chip addr is 2 byte */

	msg[1].addr = EDP2DSI_ADDR;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buffer;
	msg[1].len = cnt;

	ret = i2c_transfer(adap, msg, 2);
	for (i = 0; i < cnt; i++)
		pr_debug("i2c edp2dsi read :buf[%d] 0x%x\n", i, msg[1].buf[i]);

	if (ret < 0) {
		pr_info("%s, fail to read edp i2c data(%d)\n", __func__, ret);
	}
}

u32 new_i2c1_edp2dsi_read32(u32 addr)
{
	u32 buffer;

	//new_edp_i2c_write(0x68, 0x2, addr);
	new_edp_i2c_read(addr, (uint8_t *)&buffer, 0x4);

	return buffer;
}

u32 new_i2c1_edp2dsi_read8(u32 addr)
{
	u32 buffer;

	//new_edp_i2c_write(0x68,0x2,addr);
	new_edp_i2c_read(addr, (uint8_t *)&buffer, 0x1);

	return buffer;
}
void owl_tc358860_colorbar()
{
	//unsigned char rdatac;							
	unsigned int rdatai;
	unsigned long rdatal;

	pr_info("%s, start!\n", __func__);

	// IO Voltahge Setting
	new_i2c1_edp2dsi_write32(0x0800,0x00000000); // IOB_CTRL1
	// Boot Settings
	new_i2c1_edp2dsi_write32(0x1000,0x00006978); // BootWaitCount
	new_i2c1_edp2dsi_write32(0x1004,0x00040907); // Boot Set0
	new_i2c1_edp2dsi_write32(0x1008,0x0366000C); // Boot Set1
	new_i2c1_edp2dsi_write32(0x100C,0x130002D5); // Boot Set2
	new_i2c1_edp2dsi_write32(0x1010,0x00640020); // Boot Set3
	new_i2c1_edp2dsi_write32(0x1014,0x00000005); // Boot Ctrl
	mdelay(1);
	while((rdatal = new_i2c1_edp2dsi_read32(0x1018))!=0x00000002){;} // Check if 0x1018<bit2:0> is expected value
	// Internal PCLK Setting for Non Preset or REFCLK=26MHz
	new_i2c1_edp2dsi_write8(0xB005,0x06); // SET CG_VIDPLL_CTRL1
	new_i2c1_edp2dsi_write8(0xB006,0x00); // SET CG_VIDPLL_CTRL2
	new_i2c1_edp2dsi_write8(0xB007,0x09); // SET CG_VIDPLL_CTRL3
	new_i2c1_edp2dsi_write8(0xB008,0x00); // SET CG_VIDPLL_CTRL4
	new_i2c1_edp2dsi_write8(0xB009,0x21); // SET CG_VIDPLL_CTRL5
	new_i2c1_edp2dsi_write8(0xB00A,0x06); // SET CG_VIDPLL_CTRL6
	new_i2c1_edp2dsi_write32(0x1014,0x00000007); // Boot Ctrl
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x1018))!=0x00000007){;} // Check if 0x1018<bit2:0> is expected value
	new_i2c1_edp2dsi_write32(0x4158,0x00240008); // PPI_DPHY_TCLK_HEADERCNT
	new_i2c1_edp2dsi_write32(0x4160,0x000E0007); // PPI_DPHY_THS_HEADERCNT
	new_i2c1_edp2dsi_write32(0x4164,0x00002134); // PPI_DPHY_TWAKEUPCNT
	new_i2c1_edp2dsi_write32(0x4168,0x0000000D); // PPI_DPHY_TCLK_POSTCNT
	// DSI Start
	new_i2c1_edp2dsi_write32(0x407C,0x00000081); // DSI_DSITX_MODE
	new_i2c1_edp2dsi_write32(0x4050,0x00000000); // DSI_FUNC_MODE
	new_i2c1_edp2dsi_write32(0x401C,0x00000001); // DSI_DSITX_START
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4060))!=0x00000003){;} // Check if 0x2060/4060<bit1:0>=11b.
	// GPIO setting for LCD.  (Depends on LCD specification and System configuration)
	new_i2c1_edp2dsi_write32(0x0804,0x00000000); // IOB_CTRL2
	new_i2c1_edp2dsi_write32(0x0080,0x0000000F); // GPIOC
	new_i2c1_edp2dsi_write32(0x0084,0x0000000F); // GPIOO
	new_i2c1_edp2dsi_write32(0x0084,0x00000000); // GPIOO
	new_i2c1_edp2dsi_write32(0x0084,0x0000000F); // GPIOO
	mdelay(50);
	// DSI Hs Clock Mode
	new_i2c1_edp2dsi_write32(0x4050,0x00000020); // DSI_FUNC_MODE
	mdelay(100);
	// Command Transmission Before Video Start. (Depeds on LCD specification)
	// LCD Initialization
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000000B0); // DSIG_CQ_PAYLOAD
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000001D6); // DSIG_CQ_PAYLOAD
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000018B3); // DSIG_CQ_PAYLOAD
	mdelay(1);

	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000239); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x0000FF51); // DSIG_CQ_PAYLOAD
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000239); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x00000C53); // DSIG_CQ_PAYLOAD
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1

	new_i2c1_edp2dsi_write32(0x42FC,0x83000239); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x00000035); // DSIG_CQ_PAYLOAD
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000003B0); // DSIG_CQ_PAYLOAD
	mdelay(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x81002905); // DSIG_CQ_HEADER
	mdelay(200);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x81001105); // DSIG_CQ_HEADER
	mdelay(200);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x2A10,0x80040010); // DSI0_CQMODE
	new_i2c1_edp2dsi_write32(0x3A10,0x80040010); // DSI1_CQMODE
	new_i2c1_edp2dsi_write32(0x2A04,0x00000001); // DSI0_VideoSTART
	new_i2c1_edp2dsi_write32(0x3A04,0x00000001); // DSI1_VideoSTART
	// Color Bar Setting
	new_i2c1_edp2dsi_write32(0x0300,0x003C003C); // CBR00_HTIM1
	new_i2c1_edp2dsi_write32(0x0304,0x00B405A0); // CBR00_HTIM2
	new_i2c1_edp2dsi_write32(0x0308,0x00040004); // CBR00_VTIM1
	new_i2c1_edp2dsi_write32(0x030C,0x000C0A00); // CBR00_VTIM2
	new_i2c1_edp2dsi_write32(0x0310,0x00000001); // CBR00_MODE
	new_i2c1_edp2dsi_write32(0x0314,0x00FF0000); // CBR00_COLOR
	new_i2c1_edp2dsi_write32(0x0318,0x00000001); // CBR00_ENABLE
	new_i2c1_edp2dsi_write32(0x031C,0x00000001); // CBR00_START
	// Command Transmission After Video Start. (Depends on LCD specification)
	mdelay(1000);

	pr_info("%s, end\n", __func__);
}

/*
 * eDP timing
 * pclk= 250MHz.
 * hsync_width = 32
 * hbp = 80
 * hfp = 80
 * vsync_width= 2
 * vbp= 6
 * vfp = 8 
 * pclk = 250  2560*1440
 * */
void owl_tc358860_init_cmd(void)
{
	unsigned int temp;
	unsigned char rdatac;
	unsigned long rdatal;
	pr_info("%s, start\n", __func__);

	/* IO Voltahge Setting */
	new_i2c1_edp2dsi_write32(0x0800, 0x00000000); // IOB_CTRL1
	/* Boot Settings */
	new_i2c1_edp2dsi_write32(0x1000,0x00006978); // BootWaitCount
	new_i2c1_edp2dsi_write32(0x1004,0x00040907); // Boot Set0
	new_i2c1_edp2dsi_write32(0x1008,0x03610008); // Boot Set1
	new_i2c1_edp2dsi_write32(0x100C,0x23000332); // Boot Set2
	new_i2c1_edp2dsi_write32(0x1010,0x00E60020); // Boot Set3
	new_i2c1_edp2dsi_write32(0x1014,0x00000001); // Boot Ctrl
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x1018))!=0x00000002){;} // Check if 0x1018<bit2:0> is expected value
	// Internal PCLK Setting for Non Preset or REFCLK=26MHz
	new_i2c1_edp2dsi_write8(0xB005,0x06); // SET CG_VIDPLL_CTRL1
	new_i2c1_edp2dsi_write8(0xB006,0x04); // SET CG_VIDPLL_CTRL2
	new_i2c1_edp2dsi_write8(0xB007,0x3A); // SET CG_VIDPLL_CTRL3
	new_i2c1_edp2dsi_write8(0xB008,0x00); // SET CG_VIDPLL_CTRL4
	new_i2c1_edp2dsi_write8(0xB009,0x21); // SET CG_VIDPLL_CTRL5
	new_i2c1_edp2dsi_write8(0xB00A,0x08); // SET CG_VIDPLL_CTRL6

	new_i2c1_edp2dsi_write32(0x1014,0x00000003); // Boot Ctrl
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x1018))!=0x00000006){;} // Check if 0x1018<bit2:0> is expected value
	// Additional Setting for eDP
	new_i2c1_edp2dsi_write8(0x8003,0x41); // Max Downspread
	new_i2c1_edp2dsi_write8(0xB400,0x0D); // AL Mode Control Link
	// DPRX CAD Register Setting
	new_i2c1_edp2dsi_write8(0xB88E,0xFF); // Set CR_OPT_WCNT0
	new_i2c1_edp2dsi_write8(0xB88F,0xFF); // Set CR_OPT_WCNT1
	new_i2c1_edp2dsi_write8(0xB89A,0xFF); // Set CR_OPT_WCNT2
	new_i2c1_edp2dsi_write8(0xB89B,0xFF); // Set CR_OPT_WCNT3
	new_i2c1_edp2dsi_write8(0xB800,0x0E); // Set CDR_PHASE_LP_EN
	new_i2c1_edp2dsi_write8(0xBB26,0x02); // RX_VREG_VALUE
	new_i2c1_edp2dsi_write8(0xBB01,0x20); // RX_VREG_ENABLE
	new_i2c1_edp2dsi_write8(0xB8C0,0xF1); // RX_CDR_LUT1
	new_i2c1_edp2dsi_write8(0xB8C1,0xF1); // RX_CDR_LUT2
	new_i2c1_edp2dsi_write8(0xB8C2,0xF0); // RX_CDR_LUT3
	new_i2c1_edp2dsi_write8(0xB8C3,0xF0); // RX_CDR_LUT4
	new_i2c1_edp2dsi_write8(0xB8C4,0xF0); // RX_CDR_LUT5
	new_i2c1_edp2dsi_write8(0xB8C5,0xF0); // RX_CDR_LUT6
	new_i2c1_edp2dsi_write8(0xB8C6,0xF0); // RX_CDR_LUT7
	new_i2c1_edp2dsi_write8(0xB8C7,0xF0); // RX_CDR_LUT8
	new_i2c1_edp2dsi_write8(0xB80B,0x00); // PLL_CP1P1
	new_i2c1_edp2dsi_write8(0xB833,0x00); // PLL_CP1P2
	new_i2c1_edp2dsi_write8(0xB85B,0x00); // PLL_CP1P3
	new_i2c1_edp2dsi_write8(0xB810,0x00); // PLL_CP2P1
	new_i2c1_edp2dsi_write8(0xB838,0x00); // PLL_CP2P2
	new_i2c1_edp2dsi_write8(0xB860,0x00); // PLL_CP2P3
	new_i2c1_edp2dsi_write8(0xB815,0x00); // PLL_CP2P4
	new_i2c1_edp2dsi_write8(0xB83D,0x00); // PLL_CP2P5
	new_i2c1_edp2dsi_write8(0xB865,0x00); // PLL_CP2P6
	new_i2c1_edp2dsi_write8(0xB81A,0x00); // PLL_CP2P7
	new_i2c1_edp2dsi_write8(0xB842,0x00); // PLL_CP2P8
	new_i2c1_edp2dsi_write8(0xB86A,0x00); // PLL_CP2P9
	new_i2c1_edp2dsi_write8(0xB81F,0x00); // PLL_CP3P1
	new_i2c1_edp2dsi_write8(0xB847,0x00); // PLL_CP3P2
	new_i2c1_edp2dsi_write8(0xB86F,0x00); // PLL_CP3P3
	new_i2c1_edp2dsi_write8(0xB824,0x00); // PLL_CP4P1
	new_i2c1_edp2dsi_write8(0xB84C,0x00); // PLL_CP4P2
	new_i2c1_edp2dsi_write8(0xB874,0x00); // PLL_CP4P3
	new_i2c1_edp2dsi_write8(0xB829,0x00); // PLL_CP4P4
	new_i2c1_edp2dsi_write8(0xB851,0x00); // PLL_CP4P5
	new_i2c1_edp2dsi_write8(0xB879,0x00); // PLL_CP4P6
	new_i2c1_edp2dsi_write8(0xB82E,0x00); // PLL_CP5P7
	new_i2c1_edp2dsi_write8(0xB856,0x00); // PLL_CP5P2
	new_i2c1_edp2dsi_write8(0xB87E,0x00); // PLL_CP5P3
	new_i2c1_edp2dsi_write8(0xBB90,0x10); // ctle_em_data_rate_control_0[7:0]
	new_i2c1_edp2dsi_write8(0xBB91,0x0F); // ctle_em_data_rate_control_1[7:0]
	new_i2c1_edp2dsi_write8(0xBB92,0xF6); // ctle_em_data_rate_control_2[7:0]
	new_i2c1_edp2dsi_write8(0xBB93,0x10); // ctle_em_data_rate_control_3[7:0]
	new_i2c1_edp2dsi_write8(0xBB94,0x0F); // ctle_em_data_rate_control_4[7:0]
	new_i2c1_edp2dsi_write8(0xBB95,0xF6); // ctle_em_data_rate_control_5[7:0]
	new_i2c1_edp2dsi_write8(0xBB96,0x10); // ctle_em_data_rate_control_6[7:0]
	new_i2c1_edp2dsi_write8(0xBB97,0x0F); // ctle_em_data_rate_control_7[7:0]
	new_i2c1_edp2dsi_write8(0xBB98,0xF6); // ctle_em_data_rate_control_8[7:0]
	new_i2c1_edp2dsi_write8(0xBB99,0x10); // ctle_em_data_rate_control_A[7:0]
	new_i2c1_edp2dsi_write8(0xBB9A,0x0F); // ctle_em_data_rate_control_B[7:0]
	new_i2c1_edp2dsi_write8(0xBB9B,0xF6); // ctle_em_data_rate_control_0[7:0]
	new_i2c1_edp2dsi_write8(0xB88A,0x03); // CR_OPT_CTRL
	new_i2c1_edp2dsi_write8(0xB896,0x03); // EQ_OPT_CTRL
	new_i2c1_edp2dsi_write8(0xBBD1,0x07); // ctle_em_contro_1
	new_i2c1_edp2dsi_write8(0xBBB0,0x07); // eye_configuration_0
	new_i2c1_edp2dsi_write8(0xB88B,0x04); // CR_OPT_MIN_EYE_VALID
	new_i2c1_edp2dsi_write8(0xB88C,0x45); // CR_OPT_WCNT0_EYE
	new_i2c1_edp2dsi_write8(0xB88D,0x05); // CT_OPT_WCNT1_EYE
	new_i2c1_edp2dsi_write8(0xB897,0x04); // EQ_OPT_MIN_EYE_VALID
	new_i2c1_edp2dsi_write8(0xB898,0xE0); // EQ_OPT_WCNT0_FEQ
	new_i2c1_edp2dsi_write8(0xB899,0x2E); // EQ_OPT_WCNT1_FEQ
	new_i2c1_edp2dsi_write8(0x800E,0x00); // TRAINING_AUX_RD_INTERVAL
	new_i2c1_edp2dsi_write32(0x1014,0x00000007); // Boot Ctrl
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x1018))!=0x00000007){;} // Check if 0x1018<bit2:0> is expected value
	// eDP Settings for Link Training
	while((rdatac=new_i2c1_edp2dsi_read8(0xB631))!=0x01){;} // Check if 0xB631<bit1:0>=01b.
	new_i2c1_edp2dsi_write8(0x8000,0x11); // DPCD Rev
	new_i2c1_edp2dsi_write8(0x8001,0x0A); // Max Link Rate
	new_i2c1_edp2dsi_write8(0x8002,0x04); // Max Lane Count
	new_i2c1_edp2dsi_write8(0xB608,0x0B); // Set AUXTXHSEN
	new_i2c1_edp2dsi_write8(0xB800,0x1E); // Set CDR_PHASE_LP_EN
	new_i2c1_edp2dsi_write8(0x8700,0x00); // DPCD 0700h
	new_i2c1_edp2dsi_write32(0x5010,0x009D0000); // Monitor Signal Selection
	new_i2c1_edp2dsi_write32(0x008C,0x00000040); // GPIOOUTMODE
	new_i2c1_edp2dsi_write32(0x0080,0x00000002); // GPIOC

	// DSI Transition Time Setting for Non Preset
	new_i2c1_edp2dsi_write32(0x4158,0x00280009); // PPI_DPHY_TCLK_HEADERCNT
	new_i2c1_edp2dsi_write32(0x4160,0x000F0007); // PPI_DPHY_THS_HEADERCNT
	new_i2c1_edp2dsi_write32(0x4164,0x00002328); // PPI_DPHY_TWAKEUPCNT
	new_i2c1_edp2dsi_write32(0x4168,0x0000000E); // PPI_DPHY_TCLK_POSTCNT
	// DSI Start
	new_i2c1_edp2dsi_write32(0x407C,0x00000081); // DSI_DSITX_MODE
	new_i2c1_edp2dsi_write32(0x4050,0x00000000); // DSI_FUNC_MODE
	new_i2c1_edp2dsi_write32(0x401C,0x00000001); // DSI_DSITX_START
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4060))!=0x00000003){;} // Check if 0x2060/4060<bit1:0>=11b.

	// GPIO setting for LCD control.  (Depends on LCD specification and System configuration)
	new_i2c1_edp2dsi_write32(0x0804,0x00000000); // IOB_CTRL2
	new_i2c1_edp2dsi_write32(0x0080,0x0000000F); // GPIOC
	new_i2c1_edp2dsi_write32(0x0084,0x0000000F); // GPIOO
	new_i2c1_edp2dsi_write32(0x0084,0x00000000); // GPIOO
	new_i2c1_edp2dsi_write32(0x0084,0x0000000F); // GPIOO
	msleep(50);
	// DSI Hs Clock Mode
	// Command Transmission Before Video Start. (Depeds on LCD specification)
	// LCD Initialization
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000000B0); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000001D6); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000018B3); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1


	new_i2c1_edp2dsi_write32(0x42FC,0x83000239); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x0000FF51); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1

	new_i2c1_edp2dsi_write32(0x42FC,0x83000239); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x00000C53); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1

	new_i2c1_edp2dsi_write32(0x42FC,0x83000239); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x00000035); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000004C1); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x83000229); // DSIG_CQ_HEADER
	new_i2c1_edp2dsi_write32(0x4300,0x000003B0); // DSIG_CQ_PAYLOAD
	msleep(1);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x81002905); // DSIG_CQ_HEADER
	msleep(200);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x42FC,0x81001105); // DSIG_CQ_HEADER
	msleep(200);
	while((rdatal=new_i2c1_edp2dsi_read32(0x4200))!=0x00000001){;} // Check if <bit0>=1
	new_i2c1_edp2dsi_write32(0x2A10,0x80040010); // DSI0_CQMODE
	new_i2c1_edp2dsi_write32(0x3A10,0x80040010); // DSI1_CQMODE
	new_i2c1_edp2dsi_write32(0x2A04,0x00000001); // DSI0_VideoSTART
	new_i2c1_edp2dsi_write32(0x3A04,0x00000001); // DSI1_VideoSTART
	/* Check if eDP video is coming	*/
	new_i2c1_edp2dsi_write32(0x0154,0x00000001); /* Set_DPVideoEn */
	/* Command Transmission After Video Start. (Depends on LCD specification) */
	msleep(100);

	pr_info("%s, end\n", __func__);

}
static int panel_tc358860_enable(struct owl_panel *panel)
{
	uint32_t val;
	/*TODO*/
	void __iomem *gpio_pullctl2 = ioremap(0xE01B0068, 4);

	struct owl_display_ctrl	*ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);

	pr_debug("%s, %d!\n", __func__, ctrl_is_enabled);

	/* i2c 1 internal pull up, TODO */
	val = readl(gpio_pullctl2);
	val |= (0x5 << 16);
	writel(val, gpio_pullctl2);

	if (edp_i2c_pdev->i2c4_vcc != NULL) {
		regulator_enable(edp_i2c_pdev->i2c4_vcc);
		regulator_set_voltage(edp_i2c_pdev->i2c4_vcc, 3100000, 3100000);
	}

	if (!ctrl_is_enabled) {
		owl_tc358860_init_cmd();
	#if 0
		/* sharp tc358860 colorbar for test */
		owl_tc358860_colorbar();
	#endif

		if (ctrl->ops && ctrl->ops->aux_write)
			ctrl->ops->aux_write(ctrl, NULL, 0);

		pr_debug("%s, end\n", __func__);
	}
	return 0;
}

static int panel_tc358860_disable(struct owl_panel *panel)
{

	return 0;
}
static struct owl_panel_ops panel_tc358860_panel_ops = {
	.power_on = panel_tc358860_power_on,
	.power_off = panel_tc358860_power_off,

	.enable = panel_tc358860_enable,
	.disable = panel_tc358860_disable,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_tc358860_of_match[] = {
	{
		.compatible	= "actions,panel-tc358860",
	},
	{},
};

static int panel_tc358860_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_tc358860_data *tc358860;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_tc358860_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	tc358860 = devm_kzalloc(dev, sizeof(*tc358860), GFP_KERNEL);
	if (!tc358860) {
		dev_err(dev, "alloc tc358860 failed\n");
		return -ENOMEM;
	}
	tc358860->pdev = pdev;

	panel = owl_panel_alloc("edp", OWL_DISPLAY_TYPE_EDP);
	if (panel) {
		dev_set_drvdata(dev, panel);

		owl_panel_parse_panel_info(of_node, panel);

		if (owl_panel_register(dev, panel) < 0) {
			dev_err(dev, "%s, fail to regitser dss device\n",
				__func__);
			owl_panel_free(panel);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s, fail to alloc panel\n", __func__);
		return -ENOMEM;
	}
	panel->desc.ops = &panel_tc358860_panel_ops;


	ret = owl_dss_gpio_parse(of_node, "power-gpio", &tc358860->power_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, tc358860->power_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power_gpio failed: %d\n", ret);
		return ret;
	}

	ret = owl_dss_gpio_parse(of_node, "power1-gpio", &tc358860->power1_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power1_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, tc358860->power1_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power1_gpio failed: %d\n", ret);
		return ret;
	}

	ret = owl_dss_gpio_parse(of_node, "power2-gpio", &tc358860->power2_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power2_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, tc358860->power2_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power2_gpio failed: %d\n", ret);
		return ret;
	}

	ret = owl_dss_gpio_parse(of_node, "reset-gpio", &tc358860->reset_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse reset_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, tc358860->reset_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request reset_gpio failed: %d\n", ret);
		return ret;
	}
	ret = owl_dss_gpio_parse(of_node, "reset1-gpio", &tc358860->reset1_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse reset1_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, tc358860->reset1_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request reset1_gpio failed: %d\n", ret);
		return ret;
	}

	panel->pdata = tc358860;

	tc358860->state = OWL_DSS_STATE_OFF;
	
	return 0;
}

static int panel_tc358860_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	panel_tc358860_power_off(panel);

	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_tc358860_driver = {
	.probe			= panel_tc358860_probe,
	.remove			= panel_tc358860_remove,
	.driver = {
		.name		= "panel-tc358860",
		.owner		= THIS_MODULE,
		.of_match_table	= panel_tc358860_of_match,
	},
};

static int edp_iic_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	pr_info("%s, OK\n", __func__);
	struct device *dev = &client->dev;

	edp_i2c_pdev->client = client;

	edp_i2c_pdev->i2c4_vcc = regulator_get(dev, "tc358860");
	if (IS_ERR(edp_i2c_pdev->i2c4_vcc)) {
		dev_info(dev, "no i2c4_vcc\n");
		edp_i2c_pdev->i2c4_vcc = NULL;
	} else {
		dev_dbg(dev, "i2c4_vcc %p, current is %duv\n",
				edp_i2c_pdev->i2c4_vcc,
				regulator_get_voltage(edp_i2c_pdev->i2c4_vcc));
	}


	return 0;
}

static int edp_iic_remove(struct i2c_client *i2c)
{
	pr_info("OK\n");

	edp_i2c_pdev->client = NULL;

	return 0;
}

static struct i2c_driver edp_iic_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "edp_iic",
		.of_match_table = of_match_ptr(edp_i2c_of_match),
	},
	.id_table = edp2dsi_id,
	.probe = edp_iic_probe,
	.remove = edp_iic_remove,
};

int owl_edp2dsi_i2c_init(void)
{
	pr_info("%s, start\n", __func__);

	edp_i2c_pdev = kzalloc(sizeof(struct edp_i2c_dev), GFP_KERNEL);

	if (i2c_add_driver(&edp_iic_driver)) {
		pr_err("i2c_add_driver edp_iic_driver error!!!\n");
		goto err;
	}

	mutex_init(&edp_i2c_pdev->lock);

	return 0;
err:
	kfree(edp_i2c_pdev);
	return -EFAULT;
}

int __init panel_tc358860_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&panel_tc358860_driver);
	if (ret)
		pr_err("Failed to register platform driver\n");

	owl_edp2dsi_i2c_init();

	return ret;
}

void __exit panel_tc358860_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_tc358860_driver);
}

module_init(panel_tc358860_init);
module_exit(panel_tc358860_exit);
MODULE_LICENSE("GPL");
