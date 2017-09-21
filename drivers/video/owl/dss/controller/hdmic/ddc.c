/*
 * HDMI Display Data Channel (I2C bus protocol)
 *
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
 *	2015/9/23: Created by Lipeng.
 */
#define DEBUGX
#define pr_fmt(fmt) "hdmi_ddc: %s, " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/cdev.h>

#include "hdmi.h"

#define HDMI_DDC_ADDR		(0x60 >> 1)

#define DDC_EDID_ADDR		(0xa0 >> 1)
#define DDC_HDCP_ADDR		(0x74 >> 1)

struct hdmi_ddc_dev {
	struct i2c_client *client;
};

static struct hdmi_ddc_dev *ddc_devp;

static const struct i2c_device_id hdmi_edid_id[] = {
	{ "hdmi_read_edid", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hdmi_edid_id);

static const struct of_device_id hdmi_edid_of_match[] = {
	{ "actions,hdmi_read_edid" },	/* TODO, rename */
	{ }
};
MODULE_DEVICE_TABLE(of, hdmi_edid_of_match);

static int hdmi_iic_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	pr_info("OK\n");

	ddc_devp->client = client;
	return 0;
}

static int hdmi_iic_remove(struct i2c_client *i2c)
{
	pr_info("OK\n");

	ddc_devp->client = NULL;

	return 0;
}

static struct i2c_driver hdmi_iic_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "hdmi_iic",
		.of_match_table = of_match_ptr(hdmi_edid_of_match),
	},
	.id_table = hdmi_edid_id,
	.probe = hdmi_iic_probe,
	.remove = hdmi_iic_remove,
};

int hdmi_ddc_init(void)
{
	pr_debug("start\n");

	ddc_devp = kzalloc(sizeof(struct hdmi_ddc_dev), GFP_KERNEL);

	if (i2c_add_driver(&hdmi_iic_driver)) {
		pr_err("i2c_add_driver hdmi_iic_driver error!!!\n");
		goto err;
	}

	return 0;
err:
	kfree(ddc_devp);
	return -EFAULT;
}

int hdmi_ddc_edid_read(char segment_index, char segment_offset, char *pbuf)
{
	int ret;
	int retry_num = 0;
	struct i2c_msg msg[3];
	struct i2c_adapter *adap;
	struct i2c_client *client;

	pr_debug("start\n");

	if (ddc_devp->client == NULL) {
		pr_err("no I2C adater\n");
		return -ENODEV;
	}

	adap = ddc_devp->client->adapter;
	client = ddc_devp->client;

RETRY:
	retry_num++;

	/* set segment pointer */
	msg[0].addr = HDMI_DDC_ADDR;
	msg[0].flags = client->flags | I2C_M_IGNORE_NAK;
	msg[0].buf = &segment_index;
	msg[0].len = 1;
	ret = i2c_transfer(adap, msg, 1);
	if (ret != 1) {
		pr_err("fail to read EDID 1\n");
		ret = -1;
		goto RETURN1;
	}


	msg[0].addr = DDC_EDID_ADDR;
	msg[0].flags = client->flags;
	msg[0].buf = &segment_offset;
	msg[0].len = 1;
	ret = i2c_transfer(adap, msg, 1);
	if (ret != 1) {
		pr_err("fail to read EDID 2\n");
		ret = -1;
		goto RETURN1;
	}

	msg[0].addr = DDC_EDID_ADDR;
	msg[0].flags = client->flags  | I2C_M_RD;
	msg[0].buf = pbuf;
	msg[0].len = 128;
	ret = i2c_transfer(adap, msg, 1);
	if (ret != 1) {
		pr_err("fail to read EDID 3\n");
		ret = -1;
		goto RETURN1;
	}

	pr_debug("finished\n");

RETURN1:
	if ((ret < 0) && (retry_num < 3)) {
		pr_debug("ret_val1 is %d, retry_num is %d\n", ret, retry_num);
		pr_debug("the %dth read EDID error, try again\n", retry_num);

		goto RETRY;
	} else {
		return ret;
	}
}

int hdmi_ddc_hdcp_write(const char *buf, unsigned short offset, int count)
{
	int ret;

	struct i2c_client *client;
	struct i2c_adapter *adap;
	struct i2c_msg msg;

	if (ddc_devp->client == NULL) {
		pr_err("no I2C adater\n");
		return -ENODEV;
	}

	client = ddc_devp->client;
	adap = client->adapter;

	msg.addr = DDC_HDCP_ADDR;
	msg.flags = client->flags | I2C_M_IGNORE_NAK;
	msg.len = count;
	msg.buf = (char *)buf;

	ret = i2c_transfer(adap, &msg, 1);

	/*
	 * If everything went ok (i.e. 1 msg transmitted), return #bytes
	 * transmitted, else error code.
	 */
	return (ret == 2 ? count : ret);

}

int hdmi_ddc_hdcp_read(char *buf, unsigned short offset, int count)
{
	int i;
	int ret;

	struct i2c_client *client;
	struct i2c_adapter *adap;
	struct i2c_msg msg[2];

	pr_debug("%d\n", count);

	if (ddc_devp->client == NULL) {
		pr_err("no I2C adater\n");
		return -ENODEV;
	}

	client = ddc_devp->client;
	adap = client->adapter;

	msg[0].addr = DDC_HDCP_ADDR;
	msg[0].flags = client->flags | I2C_M_IGNORE_NAK;
	msg[0].buf = (unsigned char *)&offset;
	msg[0].len = 1;
	msg[1].addr = DDC_HDCP_ADDR;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;
	ret = i2c_transfer(adap, msg, 2);
	for (i = 0; i < count; i++)
		pr_debug("i2c hdcp read :buf[%d] %d\n", i, msg[1].buf[i]);

	/*
	 * If everything went ok (i.e. 1 msg received), return #bytes received,
	 * else error code.
	 */
	return (ret == 2 ? count : ret);
}
