/*
 * drivers/input/touchscreen/gslX680.c
 *
 * Copyright (c) 2012 Shanghai Basewin
 *	Guan Yuwei<guanyuwei@basewin.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <asm/prom.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>

#define printlf printk("func : %s --line %d \n", __func__, __LINE__)

struct gsl_cfg_dts {
	unsigned int sirq;
	unsigned int i2cNum;
	unsigned int i2cAddr;
	unsigned int xMax;
	unsigned int yMax;
	unsigned int rotate;
	unsigned int xRevert;
	unsigned int yRevert;
	unsigned int XYSwap;
	char const *regulator;
	unsigned int vol_max;
	unsigned int vol_min;
};
static struct gsl_cfg_dts cfg_dts;

static unsigned gpio_reset;

static struct i2c_client *this_client;
static struct gsl_ts *this_ts;

struct gsl_ts *gt9xx_data;
struct i2c_client *gt9xx_client;

static int gsl_ts_suspend(void);
static int gsl_ts_resume(void);

#define RESUME_INIT_CHIP_WORK

#include "gslX680.h"

#define REPORT_DATA_ANDROID_4_0/* binzhang(2012/8/31):undef  it only in test  */

#define	GSLX680_I2C_NAME	"gslX680"
#define	GSLX680_I2C_ADDR	0x40
#define	TP_I2C_ADAPTER		(4)

#define	GSL_DATA_REG		0x80
#define	GSL_STATUS_REG		0xe0
#define	GSL_PAGE_REG		0xf0

#define	PRESS_MAX			255
#define	MAX_FINGERS			10
#define	MAX_CONTACTS		10
/*0X10 milo 0621  一次下载多少寄存器，0x20是一页32X4字节 */
#define	DMA_TRANS_LEN		0x10

#define	DEBUG				0

/*#define TPD_PROC_DEBUG */
#ifdef	TPD_PROC_DEBUG
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
static struct proc_dir_entry *gsl_config_proc;
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = {0};
static u8 gsl_proc_flag;
#endif

#ifdef GSL_MONITOR
static struct delayed_work gsl_monitor_work;
static struct workqueue_struct *gsl_monitor_workqueue;
static char int_1st[4] = {0};
static char int_2nd[4] = {0};
static char bc_counter;
static char b0_counter;
static char i2c_lock_flag;
#endif
#ifdef GSLX680_COMPATIBLE
#define GSL3680A	1
#define GSL3680B	2
static char chip_type = GSL3680A;
#endif

#ifdef HAVE_TOUCH_KEY
static u16 key;
static int key_state_flag;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;
};

const u16 key_array[] = {
	KEY_BACK,
	KEY_HOME,
	KEY_MENU,
	KEY_SEARCH,
};
#define MAX_KEY_NUM     (sizeof(key_array)/sizeof(key_array[0]))

struct key_data gsl_key_data[MAX_KEY_NUM] = {
	{	KEY_BACK, 2048, 2048, 2048, 2048},
	{	KEY_HOME, 2048, 2048, 2048, 2048},
	{	KEY_MENU, 2048, 2048, 2048, 2048},
	{	KEY_SEARCH, 2048, 2048, 2048, 2048},
};
#endif

struct gsl_ts_data {
	u8 x_index;
	u8 y_index;
	u8 z_index;
	u8 id_index;
	u8 touch_index;
	u8 data_reg;
	u8 status_reg;
	u8 data_size;
	u8 touch_bytes;
	u8 update_data;
	u8 touch_meta_data;
	u8 finger_size;
};

static struct gsl_ts_data devices[] = {
	{
		.x_index = 6,
		.y_index = 4,
		.z_index = 5,
		.id_index = 7,
		.data_reg = GSL_DATA_REG,
		.status_reg = GSL_STATUS_REG,
		.update_data = 0x4,
		.touch_bytes = 4,
		.touch_meta_data = 4,
		.finger_size = 70,
	},
};

struct gsl_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
#ifdef RESUME_INIT_CHIP_WORK
	struct work_struct init_work;
	struct workqueue_struct *init_wq;
	u8 is_suspended;
#endif
	struct gsl_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	int irq;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
};

#ifdef GSL_DEBUG
#define print_info(fmt, args...)   \
			printk(fmt, ##args)
#else
#define print_info(fmt, args...)
#endif

static struct i2c_client *this_client;
static u32 id_sign[MAX_CONTACTS + 1] = { 0 };
static u8 id_state_flag[MAX_CONTACTS + 1] = { 0 };
static u8 id_state_old_flag[MAX_CONTACTS + 1] = { 0 };
static u16 x_old[MAX_CONTACTS + 1] = { 0 };
static u16 y_old[MAX_CONTACTS + 1] = { 0 };
static u16 x_new;
static u16 y_new;

/*("touchPannel_power")*/
#define CTP_POWER_ID			("ldo8")
#define CTP_POWER_MIN_VOL	(3100000)
#define CTP_POWER_MAX_VOL	(3110000)

volatile int current_val;

static struct regulator *gpower;
static struct regulator *tp_regulator;
static inline void regulator_deinit(struct regulator *);
static struct regulator *regulator_init(const char *, int, int);

static struct regulator *regulator_init(const char *name, int minvol,
		int maxvol)
{

	struct regulator *power;
	power = regulator_get(NULL, cfg_dts.regulator);/*"ldo8"*/
	if (IS_ERR(power)) {
		pr_err("Nova err,regulator_get fail\n!!!");
		return NULL;
	}
	gpower = power;

	if (regulator_set_voltage(power, minvol, maxvol)) {
		pr_err("Nova err,cannot set voltage\n!!!");
		regulator_put(power);

		return NULL;
	}
	regulator_enable(power);
	return power;
}

static inline void regulator_deinit(struct regulator *power)
{
	regulator_disable(power);
	regulator_put(power);
}

static int gslX680_init(void)
{
	gpio_direction_output(gpio_reset, 1);

	return 0;
}

static int gslX680_shutdown_low(void)
{
	gpio_direction_output(gpio_reset, 0);
	return 0;
}

static int gslX680_shutdown_high(void)
{
	gpio_direction_output(gpio_reset, 1);
	return 0;
}

static inline u16 join_bytes(u8 a, u8 b)
{
	u16 ab = 0;
	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}
#if 0
static u32 gsl_read_interface(struct i2c_client *client, u8 reg, u8 *buf,
		u32 num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = &reg;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags |= I2C_M_RD;
	xfer_msg[1].buf = buf;

	if (reg < 0x80) {
		i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));
		msleep(5);
	}

	return i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg))
			== ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}
#endif
static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf,
		u32 num) {
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static int gsl_ts_write(struct i2c_client *client, u8 addr, u8 *pdata,
		int datalen)
{
	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen = 0;
	if (datalen > 125) {
		pr_err("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	tmp_buf[0] = addr;
	bytelen++;

	if (datalen != 0 && pdata != NULL) {
		memcpy(&tmp_buf[bytelen], pdata, datalen);
		bytelen += datalen;
	}

	ret = i2c_master_send(client, tmp_buf, bytelen);
	return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr, u8 *pdata,
		unsigned int datalen)
{
	int ret = 0;

	if (datalen > 126) {
		pr_err("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0) {
		pr_err("%s set data address fail!\n", __func__);
		return ret;
	}

	return i2c_master_recv(client, pdata, datalen);
}

#ifdef GSLX680_COMPATIBLE
static void judge_chip_type(struct i2c_client *client)
{
	u8 read_buf[4] = {0};

	msleep(50);
	gsl_ts_read(client, 0xfc, read_buf, sizeof(read_buf));

	if (read_buf[2] != 0x36 && read_buf[2] != 0x88) {
		msleep(50);
		gsl_ts_read(client, 0xfc, read_buf, sizeof(read_buf));
	}

	if (0x36 == read_buf[2])	{
		chip_type = GSL3680B;
	} else {
		chip_type = GSL3680A;
	}
}
#endif

static __inline__ void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *) buf;
	*u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN * 4 + 1] = { 0 };
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len;
	const struct fw_data *ptr_fw;

	pr_info("=============gsl_load_fw start==============\n");

#ifdef GSLX680_COMPATIBLE
	if (GSL3680B == chip_type) {
		ptr_fw = GSL3680B_FW;
		source_len = ARRAY_SIZE(GSL3680B_FW);
	} else {
		ptr_fw = GSL3680A_FW;
		source_len = ARRAY_SIZE(GSL3680A_FW);
	}
#endif
	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == ptr_fw[source_line].offset) {
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		} else {
			if (1 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
				buf[0] = (u8) ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (0 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20)) {
				gsl_write_interface(client, buf[0], buf, cur - buf - 1);
				cur = buf + 1;
			}
			send_flag++;
		}
	}

	pr_info("=============gsl_load_fw end==============\n");

}

static int test_i2c (struct i2c_client *client)
{
	u8 read_buf = 0;
	u8 write_buf = 0x12;
	int ret, rc = 1;

	ret = gsl_ts_read(client, 0xf0, &read_buf, sizeof(read_buf));
	if (ret < 0)
		rc--;
	msleep(2);
	ret = gsl_ts_write(client, 0xf0, &write_buf, sizeof(write_buf));

	msleep(2);
	ret = gsl_ts_read(client, 0xf0, &read_buf, sizeof(read_buf));
	if (ret < 0)
		rc--;

	return rc;
}

static void startup_chip (struct i2c_client *client)
{
	u8 tmp = 0x00;

#ifdef GSL_NOID_VERSION
	if (chip_type == GSL3680B)
		gsl_DataInit(gsl_config_data_id_3680B);
	else
		gsl_DataInit(gsl_config_data_id_3680A);
#endif
	gsl_ts_write(client, 0xe0, &tmp, 1);
	msleep(10);
}

static void reset_chip (struct i2c_client *client)
{
	u8 tmp = 0x88;
	u8 buf[4] = { 0x00 };

	gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
	msleep(5);
	tmp = 0x04;
	gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
	msleep(5);
	gsl_ts_write(client, 0xbc, buf, sizeof(buf));
	msleep(5);
}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4] = { 0 };

	write_buf[0] = 0x88;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x03;
	gsl_ts_write(client, 0x80, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x04;
	gsl_ts_write(client, 0xe4, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x00;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(5);
}

static void init_chip(struct i2c_client *client)
{
	int rc;

	gslX680_shutdown_low();
	msleep(5);
	gslX680_shutdown_high();
	msleep(5);
	rc = test_i2c(client);
	if (rc < 0) {
		pr_err("------gslX680 test_i2c error------\n");
		return;
	}
	clr_reg(client);
	reset_chip(client);
	gsl_load_fw(client);
	startup_chip(client);
	reset_chip(client);
	startup_chip(client);
}

static void check_mem_data(struct i2c_client *client)
{
	u8 read_buf[4] = { 0 };

	msleep(30);
	gsl_ts_read(client, 0xb0, read_buf, sizeof(read_buf));

	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a
			|| read_buf[0] != 0x5a) {
		pr_debug("#########check mem read 0xb0 = %x %x %x %x #########\n", read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip(client);
	}
}

#ifdef TPD_PROC_DEBUG
#ifdef GSL_APPLICATION
static int gsl_read_MorePage(struct i2c_client *client, u32 addr, u8 *buf, u32 num)
{
	int i;
	u8 tmp_buf[4] = {0};
	u8 tmp_addr;
	for (i = 0; i < num / 8; i++) {
		tmp_buf[0] = (char)((addr+i*8)/0x80);
		tmp_buf[1] = (char)(((addr+i*8)/0x80)>>8);
		tmp_buf[2] = (char)(((addr+i*8)/0x80)>>16);
		tmp_buf[3] = (char)(((addr+i*8)/0x80)>>24);
		gsl_ts_write(client, 0xf0, tmp_buf, 4);
		tmp_addr = (char)((addr + i * 8) % 0x80);
		gsl_read_interface(client, tmp_addr, (buf + i * 8), 8);
	}
	if (i * 8 < num) {
		tmp_buf[0] = (char)((addr + i * 8) / 0x80);
		tmp_buf[1] = (char)(((addr + i * 8) / 0x80) >> 8);
		tmp_buf[2] = (char)(((addr + i * 8) / 0x80) >> 16);
		tmp_buf[3] = (char)(((addr + i * 8) / 0x80) >> 24);
		gsl_ts_write(client, 0xf0, tmp_buf, 4);
		tmp_addr = (char)((addr + i * 8) % 0x80);
		gsl_read_interface(client, tmp_addr, (buf + i * 8), 4);
	}
}
#endif
static int char_to_int(char ch)
{
	char ret;
	if (ch >= '0' && ch <= '9')
		ret = ch - '0';
	else
		ret = ch - 'a' + 10;
	return ret;
}

static int gsl_config_read_proc(struct seq_file *m, void *v)
{
	char temp_data[5] = {0};
	unsigned int tmp = 0;
	if ('v' == gsl_read[0] && 's' == gsl_read[1]) {
#ifdef GSL_NOID_VERSION
		tmp = gsl_version_id();
#else
		tmp = 0x20121215;
#endif
		seq_printf(m, "version:%x\n", tmp);
	} else if ('r' == gsl_read[0] && 'e' == gsl_read[1]) {
		if ('i' == gsl_read[3]) {
#ifdef GSL_NOID_VERSION
			tmp = (gsl_data_proc[5]<<8) | gsl_data_proc[4];
#endif
		} else {
			gsl_ts_write(this_client, 0xf0, &gsl_data_proc[4], 4);
			gsl_read_interface(this_client, gsl_data_proc[0], temp_data, 4);
			seq_printf(m, "offset : {0x%02x,0x", gsl_data_proc[0]);
			seq_printf(m, "%02x", temp_data[3]);
			seq_printf(m, "%02x", temp_data[2]);
			seq_printf(m, "%02x", temp_data[1]);
			seq_printf(m, "%02x};\n", temp_data[0]);
		}
	}
#ifdef GSL_APPLICATION
	else if ('a' == gsl_read[0] && 'p' == gsl_read[1]) {
		char *buf;
		int temp1;
		tmp = (unsigned int)(((gsl_data_proc[2] << 8) | gsl_data_proc[1]) & 0xffff);
		buf = kzalloc(tmp, GFP_KERNEL);
		if (buf == NULL)
			return -1;
		if (3 == gsl_data_proc[0]) {
			gsl_read_interface(this_client, gsl_data_proc[3], buf, tmp);
			if (tmp < m->size) {
				memcpy(m->buf, buf, tmp);
			}
		} else if (4 == gsl_data_proc[0]) {
			temp1 = ((gsl_data_proc[6]<<24) | (gsl_data_proc[5]<<16)|
					(gsl_data_proc[4]<<8) | gsl_data_proc[3]);
			gsl_read_MorePage(this_client, temp1, buf, tmp);
			if (tmp < m->size) {
				memcpy(m->buf, buf, tmp);
			}
		}
		kfree(buf);
	}
#endif
	return 0;
}
static int gsl_config_write_proc(struct file *file, const char *buffer, unsigned long count, void *data)
{
	u8 buf[8] = {0};
	char temp_buf[CONFIG_LEN];
	char *path_buf;
	int tmp = 0;
	int tmp1 = 0;
	print_info("[tp-gsl][%s] \n", __func__);
	if (count > 512) {
		print_info("size not match [%d:%ld]\n", CONFIG_LEN, count);
		return -EFAULT;
	}
	path_buf = kzalloc(count, GFP_KERNEL);
	if (!path_buf) {
		pr_err("alloc path_buf memory error \n");
		return -1;
	}
	if (copy_from_user(path_buf, buffer, count))	{
		print_info("copy from user fail\n");
		goto exit_write_proc_out;
	}
	memcpy(temp_buf, path_buf, (count < CONFIG_LEN ? count : CONFIG_LEN));
	print_info("[tp-gsl][%s][%s]\n", __func__, temp_buf);
#ifdef GSL_APPLICATION
	if ('a' != temp_buf[0] || 'p' != temp_buf[1]) {
#endif
		buf[3] = char_to_int(temp_buf[14])<<4 | char_to_int(temp_buf[15]);
		buf[2] = char_to_int(temp_buf[16])<<4 | char_to_int(temp_buf[17]);
		buf[1] = char_to_int(temp_buf[18])<<4 | char_to_int(temp_buf[19]);
		buf[0] = char_to_int(temp_buf[20])<<4 | char_to_int(temp_buf[21]);

		buf[7] = char_to_int(temp_buf[5])<<4 | char_to_int(temp_buf[6]);
		buf[6] = char_to_int(temp_buf[7])<<4 | char_to_int(temp_buf[8]);
		buf[5] = char_to_int(temp_buf[9])<<4 | char_to_int(temp_buf[10]);
		buf[4] = char_to_int(temp_buf[11])<<4 | char_to_int(temp_buf[12]);
#ifdef GSL_APPLICATION
	}
#endif
	if ('v' == temp_buf[0] && 's' == temp_buf[1]) {/*version */
		memcpy(gsl_read, temp_buf, 4);
		pr_info("gsl version\n");
	} else if ('s' == temp_buf[0] && 't' == temp_buf[1]) {/*start */
#ifdef GSL_MONITOR
		cancel_delayed_work_sync(&gsl_monitor_work);
#endif
		gsl_proc_flag = 1;
		reset_chip(this_client); /*gsl_reset_core*/
	} else if ('e' == temp_buf[0] && 'n' == temp_buf[1]) {/*end */
		msleep(20);
		reset_chip(this_client); /*gsl_reset_core*/
		startup_chip(this_client); /* gsl_start_core */
		gsl_proc_flag = 0;
	} else if ('r' == temp_buf[0] && 'e' == temp_buf[1]) {/*read buf */
		memcpy(gsl_read, temp_buf, 4);
		memcpy(gsl_data_proc, buf, 8);
	} else if ('w' == temp_buf[0] && 'r' == temp_buf[1]) {/*write buf*/
		gsl_ts_write(this_client, buf[4], buf, 4);
	}
#ifdef GSL_NOID_VERSION
	else if ('i' == temp_buf[0] && 'd' == temp_buf[1]) {/*write id config*/

		tmp1 = (buf[7]<<24)|(buf[6]<<16)|(buf[5]<<8)|buf[4];
		tmp = (buf[3]<<24)|(buf[2]<<16)|(buf[1]<<8)|buf[0];
		if (tmp1 >= 0 && tmp1 < 512)	{
			gsl_config_data_id_3680A[tmp1] = tmp;
		}
	}
#endif
#ifdef GSL_APPLICATION
	else if ('a' == temp_buf[0] && 'p' == temp_buf[1]) {
		if (1 == path_buf[3]) {
			tmp = ((path_buf[5] << 8) | path_buf[4]);
			gsl_ts_write(this_client, path_buf[6], &path_buf[10], tmp);
		} else if (2 == path_buf[3]) {
			tmp = ((path_buf[5] << 8) | path_buf[4]);
			tmp1 = ((path_buf[9] << 24) | (path_buf[8] << 16) | (path_buf[7] << 8)
					|path_buf[6]);
			buf[0] = (char)((tmp1 / 0x80) & 0xff);
			buf[1] = (char)(((tmp1 / 0x80) >> 8) & 0xff);
			buf[2] = (char)(((tmp1 / 0x80) >> 16) & 0xff);
			buf[3] = (char)(((tmp1 / 0x80) >> 24) & 0xff);
			buf[4] = (char)(tmp1 % 0x80);
			gsl_ts_write(this_client, 0xf0, buf, 4);
			gsl_ts_write(this_client, buf[4], &path_buf[10], tmp);
		} else if (3 == path_buf[3] || 4 == path_buf[3]) {
			memcpy(gsl_read, temp_buf, 4);
			memcpy(gsl_data_proc, &path_buf[3], 7);
		}
	}
#endif
	exit_write_proc_out:
	kfree(path_buf);
	return count;
}
static int gsl_server_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, gsl_config_read_proc, NULL);
}
static const struct file_operations gsl_seq_fops = {
	.open = gsl_server_list_open,
	.read = seq_read,
	.release = single_release,
	.write = gsl_config_write_proc,
	.owner = THIS_MODULE,
};
#endif

#if 0
static void record_point(u16 x, u16 y , u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;

	id_sign[id] = id_sign[id]+1;

	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x = (x_old[id] + x)/2;
	y = (y_old[id] + y)/2;

	if (x > x_old[id]) {
		x_err = x - x_old[id];
	} else {
		x_err = x_old[id]-x;
	}

	if (y > y_old[id]) {
		y_err = y - y_old[id];
	} else {
		y_err = y_old[id] - y;
	}

	if ((x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3)) {
		x_new = x; x_old[id] = x;
		y_new = y; y_old[id] = y;
	} else {
		if (x_err > 3) {
			x_new = x; x_old[id] = x;
		} else
			x_new = x_old[id];
		if (y_err > 3) {
			y_new = y; y_old[id] = y;
		} else
			y_new = y_old[id];
	}

	if (id_sign[id] == 1) {
		x_new = x_old[id];
		y_new = y_old[id];
	}

}
#endif

#ifdef HAVE_TOUCH_KEY
static void report_key(struct gsl_ts *ts, u16 x, u16 y)
{
	u16 i = 0;

	for (i = 0; i < MAX_KEY_NUM; i++) {
		if ((gsl_key_data[i].x_min < x) && (x < gsl_key_data[i].x_max) && (gsl_key_data[i].y_min < y) && (y < gsl_key_data[i].y_max)) {
			key = gsl_key_data[i].key;
			input_report_key(ts->input, key, 1);
			input_sync(ts->input);
			key_state_flag = 1;
			break;
		}
	}
}
#endif

static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id)
{
	print_info("#####id=%d,x=%d,y=%d######\n", id, x, y);

	if (cfg_dts.xRevert == 1) {
		x = cfg_dts.xMax - x;
	}

	if (cfg_dts.yRevert == 1) {
		y = cfg_dts.yMax - y;
	}

	if (cfg_dts.XYSwap == 1) {
		int tmp;
		tmp = x;
		x = y;
		y = tmp;
	}

	if (cfg_dts.rotate == 90) { /* anticlockwise 90 angle */
		int tmp;
		tmp = x;
		x = y;
		y = cfg_dts.xMax - tmp;
	} else if (cfg_dts.rotate == 180) { /* anticlockwise 180 angle */
		x = cfg_dts.xMax - x;
		y = cfg_dts.yMax - y;
	} else if (cfg_dts.rotate == 270) { /* anticlockwise 270 angle */
		int tmp;
		tmp = x;
		x = cfg_dts.yMax - y;
		y = tmp;
	}

#if 0
	if (x > cfg_dts.xMax || y > cfg_dts.yMax) {
#ifdef HAVE_TOUCH_KEY
		report_key(ts, x, y);
#endif
		return;
	}
#endif

#ifdef HAVE_TOUCH_KEY
	if (x > cfg_dts.xMax || y > cfg_dts.yMax) {
		report_key(ts, x, y);
		return;
	}
#endif

#ifdef REPORT_DATA_ANDROID_4_0
	input_mt_slot(ts->input, id);
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
#else
	input_report_abs(ts->input, ABS_MT_TRACKING_ID, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pressure);
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
	input_mt_sync(ts->input);
#endif
}

static void gslX680_ts_worker(struct work_struct *work)
{
	int rc, i;
	u8 id, touches;
	u16 x, y;

#ifdef GSL_NOID_VERSION
	u32 tmp1;
	u8 buf[4] = {0};
	struct gsl_touch_info cinfo = { {0},};
#endif

	struct gsl_ts *ts = container_of(work, struct gsl_ts, work);
	print_info("=====gslX680_ts_worker=====\n");

#ifdef GSL_MONITOR
	if (i2c_lock_flag != 0)
		goto i2c_lock_schedule;
	else
		i2c_lock_flag = 1;
#endif
#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1)
		goto schedule;
#endif

	rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
	if (rc < 0)	{
		dev_err(&ts->client->dev, "read failed\n");
		goto schedule;
	}

	touches = ts->touch_data[ts->dd->touch_index];
	print_info("-----touches: %d -----\n", touches);
#ifdef GSL_NOID_VERSION
	cinfo.finger_num = touches;
	print_info("tp-gsl  finger_num = %d\n", cinfo.finger_num);
	for (i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS); i++)	{
		cinfo.x[i] = join_bytes((ts->touch_data[ts->dd->x_index + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->x_index + 4 * i]);
		cinfo.y[i] = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
				ts->touch_data[ts->dd->y_index + 4 * i]);
		print_info("tp-gsl  x = %d y = %d \n", cinfo.x[i], cinfo.y[i]);
	}
	cinfo.finger_num = (ts->touch_data[3] << 24) | (ts->touch_data[2] << 16)
	|(ts->touch_data[1] << 8) | (ts->touch_data[0]);
	gsl_alg_id_main(&cinfo);
	tmp1 = gsl_mask_tiaoping();
	print_info("[tp-gsl] tmp1=%x\n", tmp1);
	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		buf[0] = 0xa; buf[1] = 0; buf[2] = 0; buf[3] = 0;
		gsl_ts_write(ts->client, 0xf0, buf, 4);
		buf[0] = (u8)(tmp1 & 0xff);
		buf[1] = (u8)((tmp1>>8) & 0xff);
		buf[2] = (u8)((tmp1>>16) & 0xff);
		buf[3] = (u8)((tmp1>>24) & 0xff);
		print_info("tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,buf[3]=%02x\n",
				tmp1, buf[0], buf[1], buf[2], buf[3]);
		gsl_ts_write(ts->client, 0x8, buf, 4);
	}
	touches = cinfo.finger_num;
#endif

	for (i = 1; i <= MAX_CONTACTS; i++) {
		if (touches == 0)
			id_sign[i] = 0;
		id_state_flag[i] = 0;
	}
	for (i = 0; i < (touches > MAX_FINGERS ? MAX_FINGERS : touches); i++) {
#ifdef GSL_NOID_VERSION
		id = cinfo.id[i];
		x = cinfo.x[i];
		y = cinfo.y[i];
#else
		x = join_bytes((ts->touch_data[ts->dd->x_index + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->x_index + 4 * i]);
		y = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
				ts->touch_data[ts->dd->y_index + 4 * i]);
		id = ts->touch_data[ts->dd->id_index + 4 * i] >> 4;
#endif

		if (1 <= id && id <= MAX_CONTACTS) {
			report_data(ts, x, y, 10, id);
			id_state_flag[id] = 1;
		}
	}
	for (i = 1; i <= MAX_CONTACTS; i++) {
		if ((0 == touches) || ((0 != id_state_old_flag[i]) && (0 == id_state_flag[i]))) {
#ifdef REPORT_DATA_ANDROID_4_0
			input_mt_slot(ts->input, i);
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
#endif
			id_sign[i] = 0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}
#ifndef REPORT_DATA_ANDROID_4_0
	if (0 == touches) {
		input_mt_sync(ts->input);
#ifdef HAVE_TOUCH_KEY
		if (key_state_flag) {
			input_report_key(ts->input, key, 0);
			input_sync(ts->input);
			key_state_flag = 0;
		}
#endif
	}
#endif
	input_sync(ts->input);

	schedule:
#ifdef GSL_MONITOR
	i2c_lock_flag = 0;
	i2c_lock_schedule:
#endif
	enable_irq(ts->irq);

}

#ifdef GSL_MONITOR
static void gsl_monitor_worker(void)
{
	char read_buf[4] = {0};
	char download_flag = 0;
	/*
	 if(states == 0 )
	 {
	 ret = gpio_direction_output(gpio_reset, 0);
	 if(ret < 0)
	 {
	 printk("set gpio 0 fail \n");
	 }
	 else
	 {
	 printk("set gpio 0 \n");
	 }
	 msleep(1000);
	 states = 1;
	 }
	 else{
	 ret = gpio_direction_output(gpio_reset, 1);
	 if(ret < 0)
	 {
	 printk("set gpio 1 fail \n");
	 }
	 else
	 {
	 printk("set gpio 1 \n");
	 }
	 msleep(1000);
	 states = 0;
	 }
	 */
#if 1

	if (i2c_lock_flag != 0)
		goto queue_monitor_work;
	else
		i2c_lock_flag = 1;

	gsl_ts_read(this_client, 0xb0, read_buf, 4);
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a ||
		read_buf[1] != 0x5a || read_buf[0] != 0x5a)
		b0_counter++;
	else
		b0_counter = 0;

	if (b0_counter > 1)	{
		pr_debug("======read 0xb0: %x %x %x %x ======\n",
			read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		download_flag = 1;
		b0_counter = 0;
	}

	gsl_ts_read(this_client, 0xb4, read_buf, 4);
	int_2nd[3] = int_1st[3];
	int_2nd[2] = int_1st[2];
	int_2nd[1] = int_1st[1];
	int_2nd[0] = int_1st[0];
	int_1st[3] = read_buf[3];
	int_1st[2] = read_buf[2];
	int_1st[1] = read_buf[1];
	int_1st[0] = read_buf[0];

	if (int_1st[3] == int_2nd[3] && int_1st[2] == int_2nd[2] &&
			int_1st[1] == int_2nd[1] && int_1st[0] == int_2nd[0]) {
		pr_debug("======int_1st: %x %x %x %x , int_2nd: %x %x %x %x ======\n",
			int_1st[3], int_1st[2], int_1st[1], int_1st[0], int_2nd[3], int_2nd[2], int_2nd[1], int_2nd[0]);
		download_flag = 1;
	}

	gsl_ts_read(this_client, 0xbc, read_buf, 4);
	if (read_buf[3] != 0 || read_buf[2] != 0 ||
		read_buf[1] != 0 || read_buf[0] != 0)
		bc_counter++;
	else
		bc_counter = 0;

	if (bc_counter > 0)	{
		pr_debug("======read 0xbc: %x %x %x %x ======\n", read_buf[3], read_buf[2], read_buf[1], read_buf[0]);

		download_flag = 1;
		bc_counter = 0;
	}

	if (1 == download_flag) {
		if (tp_regulator) {
			current_val = regulator_get_voltage(tp_regulator);
			regulator_disable(tp_regulator);
			pr_debug("Nova disable regulator %d\n", current_val);
		}
		msleep(100);
		if (tp_regulator && gpower) {
			regulator_set_voltage(gpower, CTP_POWER_MIN_VOL, CTP_POWER_MAX_VOL);
			regulator_enable(gpower);
			mdelay(100);
			pr_info("gsl_ts_resume resume hw_reset\n");
		} else {
			pr_info("gsl_ts_resume resume err branch !!!\n");
		}
		init_chip(this_client);
	}
	i2c_lock_flag = 0;

	queue_monitor_work:
#endif
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 100);
}
#endif

static irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{
	struct gsl_ts *ts = dev_id;

	print_info("========gslX680 Interrupt=========\n");

	disable_irq_nosync(ts->irq);

	if (!work_pending(&ts->work)) {
		queue_work(ts->wq, &ts->work);
	}

	return IRQ_HANDLED;

}

#ifdef RESUME_INIT_CHIP_WORK
static void gslX680_init_worker (struct work_struct *work)
{
#if 0
	if (tp_regulator && gpower)	{
		regulator_set_voltage(gpower, CTP_POWER_MIN_VOL, CTP_POWER_MAX_VOL);
		regulator_enable(gpower);
		mdelay(50);
		pr_info("gsl_ts_resume resume hw_reset\n");
	} else {
		pr_info("gsl_ts_resume resume err branch !!!\n");
	}
#endif
	init_chip(this_ts->client);
	check_mem_data(this_ts->client);
}

static int gsl_ts_suspend()
{
	pr_info("[GSLX680] Enter %s\n", __func__);
	struct gsl_ts *this_ts = gt9xx_data;
#ifdef GSL_MONITOR
	cancel_delayed_work_sync(&gsl_monitor_work);
#endif

	disable_irq_nosync(this_ts->irq);
	flush_workqueue(this_ts->init_wq);

	if (tp_regulator) {
		current_val = regulator_get_voltage(tp_regulator);
		pr_debug("Nova disable regulator %d\n", current_val);
	}
	msleep(20);
	return 0;
}

static int gsl_ts_resume()
{
	pr_info("[GSLX680] Enter %s\n", __func__);
	struct gsl_ts *this_ts = gt9xx_data;
	this_ts->is_suspended = true;
	flush_workqueue(this_ts->init_wq);
	queue_work(this_ts->init_wq, &this_ts->init_work);

	enable_irq(this_ts->irq);

#ifdef GSL_MONITOR
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 300);
#endif

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void gsl_ts_early_suspend(struct early_suspend *h)
{
	pr_info("[GSLX680] Enter %s\n", __func__);

#ifdef GSL_MONITOR
	pr_info("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&gsl_monitor_work);
#endif
	disable_irq_nosync(this_ts->irq);
}

static void gsl_ts_late_resume(struct early_suspend *h)
{
	pr_info("[GSLX680] Enter %s\n", __func__);
	enable_irq(this_ts->irq);
	/*
	 if(this_ts->is_suspended == false)
	 {
	 gslX680_shutdown_high();
	 msleep(10);
	 reset_chip(this_ts->client);
	 startup_chip(this_ts->client);
	 check_mem_data(this_ts->client);
	 enable_irq(this_ts->irq);
	 }
	 else
	 {
	 msleep(400);///milo modify 500
	 }*/
#ifdef GSL_MONITOR
	pr_info("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 300);
#endif

}
#endif
#else
static int gsl_ts_suspend(struct device *dev)
{
	struct gsl_ts *ts = dev_get_drvdata(dev);
	int i;

	pr_info("I'am in gsl_ts_suspend() start\n");

#ifdef GSL_MONITOR
	pr_info("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&gsl_monitor_work);
#endif

	disable_irq_nosync(ts->irq);

	gslX680_shutdown_low();

#ifdef SLEEP_CLEAR_POINT
	msleep(10);
#ifdef REPORT_DATA_ANDROID_4_0
	for (i = 1; i <= MAX_CONTACTS; i++) {
		input_mt_slot(ts->input, i);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
#else
	input_mt_sync(ts->input);
#endif
	input_sync(ts->input);
	msleep(10);
	report_data(ts, 1, 1, 10, 1);
	input_sync(ts->input);
#endif

	msleep(10);
	if (tp_regulator) {
		current_val = regulator_get_voltage(tp_regulator);
		regulator_disable(tp_regulator);
		pr_debug("Nova disable regulator %d\n", current_val);
	}
	msleep(20);
	return 0;
}

static int gsl_ts_resume(struct device *dev)
{
	struct gsl_ts *ts = dev_get_drvdata(dev);
	int i;

	pr_info("I'am in gsl_ts_resume() start\n");

	if (tp_regulator && gpower)	{
		regulator_set_voltage(gpower, CTP_POWER_MIN_VOL, CTP_POWER_MAX_VOL);
		regulator_enable(gpower);
		mdelay(50);
		pr_debug("gsl_ts_resume resume hw_reset\n");
	} else {
		pr_debug("gsl_ts_resume resume err branch !!!\n");
	}

	gslX680_shutdown_high();
	msleep(20);
	reset_chip(ts->client);
	startup_chip(ts->client);
	check_mem_data(ts->client);

#ifdef SLEEP_CLEAR_POINT
#ifdef REPORT_DATA_ANDROID_4_0
	for (i = 1; i <= MAX_CONTACTS; i++) {
		input_mt_slot(ts->input, i);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
#else
	input_mt_sync(ts->input);
#endif
	input_sync(ts->input);
#endif
#ifdef GSL_MONITOR
	pr_debug("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 300);
#endif

	enable_irq(ts->irq);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void gsl_ts_early_suspend(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
	pr_debug("[GSLX680] Enter %s\n", __func__);
	gsl_ts_suspend(&ts->client->dev);
}

static void gsl_ts_late_resume(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
	pr_info("[GSLX680] Enter %s\n", __func__);
	gsl_ts_resume(&ts->client->dev);
}
#endif
#endif
#if 0
static void ts_early_suspend()
{
	pr_info("[GSLX680] Enter %s\n", __func__);

#ifdef GSL_MONITOR
	pr_info("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&gsl_monitor_work);
#endif

	disable_irq_nosync(this_ts->irq);
}
#endif

#if 0
static void ts_late_resume()
{
	pr_info("[GSLX680] Enter %s\n", __func__);
	enable_irq(this_ts->irq);
#ifdef GSL_MONITOR
	pr_info("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 300);
#endif

}
#endif

static int tp_suspend(struct i2c_client *client, pm_message_t mesg)
{
	pr_info("----[GSLX680] Enter %s---\n", __func__);

	flush_workqueue(this_ts->init_wq);
	flush_work(&this_ts->init_work);
	gslX680_shutdown_low();

	if (tp_regulator) {
		current_val = regulator_get_voltage(tp_regulator);
		regulator_disable(tp_regulator);
		pr_debug("Nova disable regulator %d\n", current_val);
	}

	this_ts->is_suspended = true;
	return 0;
}

static int tp_resume_early(struct device *dev)
{
	int ret;

	pr_info("----[GSLX680] Enter %s-----\n", __func__);
	if (false == this_ts->is_suspended) {
		pr_info("impossible: power is turned on!\n");
		return 0;
	}

	if (tp_regulator) {
		regulator_set_voltage(tp_regulator, CTP_POWER_MIN_VOL, CTP_POWER_MAX_VOL);
		ret = regulator_enable(tp_regulator);
	}

#ifdef RESUME_INIT_CHIP_WORK
	queue_work(this_ts->init_wq, &this_ts->init_work);
#endif

	this_ts->is_suspended = false;
	return 0;
}

static int tp_resume(struct i2c_client *client)
{
	int ret;
	pr_info("----[GSLX680] Enter %s-----\n", __func__);

	if (false == this_ts->is_suspended) {
		return 0;
	}

	if (tp_regulator) {
		regulator_set_voltage(tp_regulator, CTP_POWER_MIN_VOL, CTP_POWER_MAX_VOL);
		ret = regulator_enable(tp_regulator);
	}

#ifdef RESUME_INIT_CHIP_WORK
	queue_work(this_ts->init_wq, &this_ts->init_work);
#endif
	this_ts->is_suspended = false;
	return 0;
}

/********************TP DEBUG************************/

/**************************************************************************/
static ssize_t tp_rotate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", cfg_dts.rotate);
}
/**************************************************************************/
static ssize_t tp_rotate_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data = 0;
	int error;
	error = strict_strtol(buf, 10, &data);
	if (error)
		return error;
	cfg_dts.rotate = data;
	return count;
}
/**************************************************************************/
static ssize_t tp_xrevert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", cfg_dts.xRevert);
}
/**************************************************************************/
static ssize_t tp_xrevert_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data = 0;
	int error;
	error = strict_strtol(buf, 10, &data);
	if (error)
		return error;
	cfg_dts.xRevert = data;
	return count;
}

/**************************************************************************/
static ssize_t tp_yrevert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", cfg_dts.yRevert);
}
/**************************************************************************/
static ssize_t tp_yrevert_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data = 0;
	int error;
	error = strict_strtol(buf, 10, &data);
	if (error)
		return error;
	cfg_dts.yRevert = data;
	return count;
}

/**************************************************************************/
static ssize_t tp_xyswap_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", cfg_dts.XYSwap);
}
/**************************************************************************/
static ssize_t tp_xyswap_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data = 0;
	int error;
	error = strict_strtol(buf, 10, &data);
	if (error)
		return error;
	cfg_dts.XYSwap = data;
	return count;
}

static DEVICE_ATTR(tp_rotate, S_IWUSR|S_IWGRP|S_IRUSR|S_IRGRP,
		tp_rotate_show, tp_rotate_store);
static DEVICE_ATTR(tp_xrevert, S_IWUSR|S_IWGRP|S_IRUSR|S_IRGRP,
		tp_xrevert_show, tp_xrevert_store);
static DEVICE_ATTR(tp_yrevert, S_IWUSR|S_IWGRP|S_IRUSR|S_IRGRP,
		tp_yrevert_show, tp_yrevert_store);
static DEVICE_ATTR(tp_xyswap, S_IWUSR|S_IWGRP|S_IRUSR|S_IRGRP,
		tp_xyswap_show, tp_xyswap_store);

static struct attribute *tp_attributes[] = {
	&dev_attr_tp_rotate.attr,
	&dev_attr_tp_xrevert.attr,
	&dev_attr_tp_yrevert.attr,
	&dev_attr_tp_xyswap.attr,
	NULL
};

static const struct attribute_group tp_attr_group = {
	.attrs = tp_attributes,
};

static int gslX680_ts_init(struct i2c_client *client, struct gsl_ts *ts)
{
	struct input_dev *input_device;
	int rc = 0;
	pr_info("[GSLX680] Enter %s\n", __func__);

	ts->dd = &devices[ts->device_id];

	if (ts->device_id == 0) {
		ts->dd->data_size = MAX_FINGERS * ts->dd->touch_bytes + ts->dd->touch_meta_data;
		ts->dd->touch_index = 0;
	}

	ts->touch_data = kzalloc(ts->dd->data_size, GFP_KERNEL);
	if (!ts->touch_data) {
		pr_err("%s: Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}

	input_device = input_allocate_device();
	if (!input_device) {
		rc = -ENOMEM;
		goto error_alloc_dev;
	}

	ts->input = input_device;
	input_device->name = GSLX680_I2C_NAME;
	input_device->id.bustype = BUS_I2C;
	input_device->dev.parent = &client->dev;
	input_set_drvdata(input_device, ts);

#ifdef REPORT_DATA_ANDROID_4_0
	__set_bit(EV_ABS, input_device->evbit);
	__set_bit(EV_KEY, input_device->evbit);
	__set_bit(EV_REP, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_mt_init_slots(input_device, (MAX_CONTACTS+1), 0);
#else
	input_set_abs_params(input_device, ABS_MT_TRACKING_ID, 0, (MAX_CONTACTS+1), 0, 0);
	set_bit(EV_ABS, input_device->evbit);
	set_bit(EV_KEY, input_device->evbit);

#endif

#ifdef HAVE_TOUCH_KEY
	input_device->evbit[0] = BIT_MASK(EV_KEY);

	for (i = 0; i < MAX_KEY_NUM; i++)
	set_bit(key_array[i], input_device->keybit);
#endif

	set_bit(ABS_MT_POSITION_X, input_device->absbit);
	set_bit(ABS_MT_POSITION_Y, input_device->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

	input_set_abs_params(input_device, ABS_MT_POSITION_X, 0, cfg_dts.xMax, 0, 0);
	input_set_abs_params(input_device, ABS_MT_POSITION_Y, 0, cfg_dts.yMax, 0, 0);
	input_set_abs_params(input_device, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_device, ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

	client->irq = cfg_dts.sirq;
	ts->irq = client->irq;

	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		dev_err(&client->dev, "Could not create workqueue\n");
		goto error_wq_create;
	}
	flush_workqueue(ts->wq);

	INIT_WORK(&ts->work, gslX680_ts_worker);

#ifdef RESUME_INIT_CHIP_WORK
	ts->init_wq = create_singlethread_workqueue("ts_init_wq");
	if (!ts->init_wq) {
		dev_err(&client->dev, "Could not create ts_init_wq workqueue\n");
		goto error_wq_create;
	}
	flush_workqueue(ts->init_wq);
	INIT_WORK(&ts->init_work, gslX680_init_worker);
#endif
	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;
	if (sysfs_create_group(&input_device->dev.kobj, &tp_attr_group) < 0) {
		pr_err("create tp sysfs group error!");
	}

	return 0;

	error_unreg_device:
	destroy_workqueue(ts->wq);
	error_wq_create:
	input_free_device(input_device);
	error_alloc_dev:
	kfree(ts->touch_data);
	return rc;
}
static int gsl_ts_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct gsl_ts *ts;
	int rc;

	pr_info("GSLX680 Enter %s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	pr_info("==kzalloc success=\n");

	ts->client = client;
	i2c_set_clientdata(client, ts);
	ts->device_id = id->driver_data;

	rc = gslX680_ts_init(client, ts);
	if (rc < 0) {
		dev_err(&client->dev, "GSLX680 init failed\n");
		goto error_mutex_destroy;
	}

	this_client = client;
	this_ts = ts;

	gslX680_init();
#ifdef GSLX680_COMPATIBLE
	judge_chip_type(ts->client);
#endif

#ifdef RESUME_INIT_CHIP_WORK
	queue_work(this_ts->init_wq, &this_ts->init_work);
#else
	init_chip(ts->client);
	check_mem_data(ts->client);
#endif

	rc = request_irq(client->irq, gsl_ts_irq, IRQF_TRIGGER_HIGH | IRQF_DISABLED, client->name, ts);
	if (rc < 0) {
		pr_err("gsl_probe: request irq failed\n");
		goto error_req_irq_fail;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 10;
	ts->early_suspend.suspend = gsl_ts_early_suspend;
	ts->early_suspend.resume = gsl_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

#ifdef GSL_MONITOR
	pr_debug("gsl_ts_probe () : queue gsl_monitor_workqueue\n");

	INIT_DELAYED_WORK(&gsl_monitor_work, gsl_monitor_worker);
	gsl_monitor_workqueue = create_singlethread_workqueue("gsl_monitor_workqueue");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 1000);
#endif
#ifdef TPD_PROC_DEBUG
#if 0
	gsl_config_proc = create_proc_entry(GSL_CONFIG_PROC_FILE, 0666, NULL);
	pr_debug("[tp-gsl] [%s] gsl_config_proc = %x \n", __func__, gsl_config_proc);
	if (gsl_config_proc == NULL) {
		print_info("create_proc_entry %s failed\n", GSL_CONFIG_PROC_FILE);
	} else {
		gsl_config_proc->read_proc = gsl_config_read_proc;
		gsl_config_proc->write_proc = gsl_config_write_proc;
	}
#else
	proc_create(GSL_CONFIG_PROC_FILE, 0666, NULL, &gsl_seq_fops);
#endif
	gsl_proc_flag = 0;
#endif

	gt9xx_data = ts;
	i2c_set_clientdata(client, (void *)ts);
	pr_debug("[GSLX680] End %s\n", __func__);

	return 0;

	/*exit_set_irq_mode:*/
	error_req_irq_fail:
	free_irq(ts->irq, ts);

	error_mutex_destroy:
	input_free_device(ts->input);
	kfree(ts);
	return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);
	pr_info("==gsl_ts_remove=\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif

#ifdef GSL_MONITOR
	cancel_delayed_work_sync(&gsl_monitor_work);
	destroy_workqueue(gsl_monitor_workqueue);
#endif
	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	free_irq(ts->irq, ts);
	destroy_workqueue(ts->wq);
	sysfs_remove_group(&ts->input->dev.kobj, &tp_attr_group);
	input_unregister_device(ts->input);
	kfree(ts->touch_data);
	kfree(ts);

	return 0;
}

static const struct i2c_device_id gsl_ts_id[] = {
	{	GSLX680_I2C_NAME, 0},
	{}
};

static const struct of_device_id gsl_of_match[] = {
	{	"gslX680"},
	{}
};

MODULE_DEVICE_TABLE(i2c, gsl_ts_id);
MODULE_DEVICE_TABLE(of, gsl_of_match);

static unsigned short gsl_addresses[] = {
	GSLX680_I2C_ADDR,
	I2C_CLIENT_END,
};

static struct dev_pm_ops tp_pm_ops = {
	.resume_early = tp_resume_early,
	.suspend = tp_suspend,
	.resume = tp_resume,
};

static struct i2c_driver gsl_ts_driver = {
	.driver = {
		.name = GSLX680_I2C_NAME,
		.owner = THIS_MODULE,
		.pm = &tp_pm_ops,
		.of_match_table = of_match_ptr(gsl_of_match),
	},
	.probe = gsl_ts_probe,
	.remove = gsl_ts_remove,
	.id_table = gsl_ts_id,
	.address_list = gsl_addresses,
};

/* static struct i2c_board_info tp_info = {
	.type = GSLX680_I2C_NAME,
	.addr = GSLX680_I2C_ADDR,
}; */

static int suspend_switch = 1;

static ssize_t suspend_show(struct bus_type *bus, const char *buf, size_t count)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 0: suspend, 1:resume)\n", suspend_switch);
	return cnt;
}

static ssize_t suspend_store(struct bus_type *bus, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	gsl_ts_suspend();
	suspend_switch = tmp;
	break;

	case 1:
	gsl_ts_resume();
	suspend_switch = tmp;
	break;
	default:
		pr_err("invalid input\n");
	break;
	}
	return count;
}

static struct bus_attribute suspend_attr =
__ATTR(tp_early_suspend, 0664, suspend_show, suspend_store);

static int tp_of_data_get(void)
{
	struct device_node *of_node;
	enum of_gpio_flags flags;
	unsigned int scope[2];
	int ret = -1;

	of_node = of_find_compatible_node(NULL, NULL, "gslX680");
	if (of_node == NULL) {
		pr_err("%s,%d,find the gslX680 dts err!\n", __func__, __LINE__);
		return -1;
	}

	/* load tp regulator */
	if (of_find_property(of_node, "tp_vcc", NULL)) {
		ret = of_property_read_string(of_node, "tp_vcc", &cfg_dts.regulator);
		if (ret < 0) {
			pr_err("can not read tp_vcc power source\n");
			cfg_dts.regulator = CTP_POWER_ID;
		}

		if (of_property_read_u32_array(of_node, "vol_range", scope, 2)) {
			pr_err(" failed to get voltage range\n");
			scope[0] = CTP_POWER_MIN_VOL;
			scope[1] = CTP_POWER_MAX_VOL;
		}
		cfg_dts.vol_min = scope[0];
		cfg_dts.vol_max = scope[1];
	}
	/* load irq number */
	cfg_dts.sirq = irq_of_parse_and_map(of_node, 0);
	if (cfg_dts.sirq < 0) {
		pr_err("No IRQ resource for tp\n");
		return -ENODEV;
	}

	/* load gpio info */
	if (!of_find_property(of_node, "reset_gpios", NULL)) {
		pr_err("<isp>err: no config gpios\n");
		goto fail;
	}
	gpio_reset = of_get_named_gpio_flags(of_node, "reset_gpios", 0, &flags);

	cfg_dts.i2cNum = TP_I2C_ADAPTER;
	/* load tp i2c addr */
	ret = of_property_read_u32(of_node, "reg", &cfg_dts.i2cAddr);
	if (ret) {
		pr_err(" failed to get i2c_addr\n");
		goto fail;
	}
	/* load other options */
	ret = of_property_read_u32(of_node, "x_pixel", &cfg_dts.xMax);
	if (ret) {
		pr_err("failed to get x_pixel\r\n,set default:1536");
		cfg_dts.xMax = 1536;
	}

	ret = of_property_read_u32(of_node, "y_pixel", &cfg_dts.yMax);
	if (ret) {
		pr_err("failed to get y_pixel\r\n,set default:2048");
		cfg_dts.yMax = 2048;
	}

	ret = of_property_read_u32(of_node, "x_revert_en", &cfg_dts.xRevert);
	if (ret) {
		pr_err("failed to get x_revert_en\r\n,set default:0");
		cfg_dts.xRevert = 0;
	}

	ret = of_property_read_u32(of_node, "y_revert_en", &cfg_dts.yRevert);
	if (ret) {
		pr_err("failed to get y_revert_en\r\n,set default:0");
		cfg_dts.yRevert = 0;
	}

	ret = of_property_read_u32(of_node, "xy_swap_en", &cfg_dts.XYSwap);
	if (ret) {
		pr_err("failed to get xy_swap_en, set default:0\r\n");
		cfg_dts.XYSwap = 0;
	}
	ret = of_property_read_u32(of_node, "rotate_degree", &cfg_dts.rotate);
	if (ret) {
		pr_err("failed to get rotate, set default:0\r\n");
		cfg_dts.rotate = 0;
	}
	pr_debug("gpio num:%d, i2c_addr:%02x, irq_number:%d,x_pixel:%d, y_pixel:%d, xRevert:%d, yRevert:%d, XYSwap:%d rotate:%d, i2cNum:%d\n",
			gpio_reset,
			cfg_dts.i2cAddr,
			cfg_dts.sirq,
			cfg_dts.xMax,
			cfg_dts.yMax,
			cfg_dts.xRevert,
			cfg_dts.yRevert,
			cfg_dts.XYSwap,
			cfg_dts.rotate,
			cfg_dts.i2cNum);
	return 0;

fail:
	return -1;
}

static int __init gsl_ts_init(void)
{
	int ret;
	tp_of_data_get();
	pr_info("==gsl_ts_init==\n");

	tp_regulator = regulator_init(cfg_dts.regulator,
			cfg_dts.vol_min, cfg_dts.vol_max);
	if (!tp_regulator) {
		pr_err("Nova tp init power failed");
		ret = -EINVAL;
		return ret;
	}

	gpio_request(gpio_reset, GSLX680_I2C_NAME);

	ret = i2c_add_driver(&gsl_ts_driver);
	pr_debug("ret=%d\n", ret);

	ret = bus_create_file(&i2c_bus_type, &suspend_attr);
	if (ret)
		pr_err("Add bus file failed");

	return ret;
}
static void __exit gsl_ts_exit(void)
{
	pr_info("==gsl_ts_exit==\n");
	pr_emerg("%s:%d\n", __func__, __LINE__);
	bus_remove_file(&i2c_bus_type, &suspend_attr);
	pr_emerg("%s:%d\n", __func__, __LINE__);
	i2c_del_driver(&gsl_ts_driver);
	return;
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680 touchscreen controller driver");
MODULE_AUTHOR("Guan Yuwei, guanyuwei@basewin.com");
MODULE_ALIAS("platform:gsl_ts");
