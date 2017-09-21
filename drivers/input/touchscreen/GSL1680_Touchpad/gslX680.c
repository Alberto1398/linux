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

/*
	history:
	mbgalex@163.com_2013-07-16_14:12
	add tp for Q790 OGS project ,tp modules is EC8031-01
*/


#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/input/mt.h>

#include <linux/i2c.h>
#include <linux/input.h>

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
#include <asm/io.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>


#include "gslX680.h"
#include "gslX680_VR.h"
#include <linux/workqueue.h>

/*("touchPannel_power")*/
#define CTP_POWER_ID			("ldo8")
#define CTP_POWER_MIN_VOL	(3100000)
#define CTP_POWER_MAX_VOL	(3110000)
#define	TP_I2C_ADAPTER		(2)
//#define GSL_TIMER

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
	unsigned int use_click_mode;
};
static struct gsl_cfg_dts cfg_dts;

static unsigned gpio_reset;


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


#define HAVE_TOUCH_KEY

struct gslX680_fw_array {
	const char* name;
	unsigned int size;
	const struct fw_data *fw;
} gslx680_fw_grp[] = {
	{"gslX680_VR"  ,  ARRAY_SIZE(GSLX680_VR_FW),GSLX680_VR_FW},
};

unsigned int *gslX680_config_data[16] = {
    gsl_config_data_id_VR,     
};

#define FOR_TSLIB_TEST

//#define HAVE_TOUCH_KEY
#ifdef TPD_PROC_DEBUG
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
static struct proc_dir_entry *gsl_config_proc = NULL;
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = {0};
static u8 gsl_proc_flag = 0;
#endif



#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define PRESS_MAX    			255
#define MAX_FINGERS 		5//5 //最大手指个数
#define MAX_CONTACTS 		5
#define DMA_TRANS_LEN		0x10

#define PHO_CFG2_OFFSET	(0X104)
#define PHO_DAT_OFFSET		(0X10C)
#define PHO_PULL1_OFFSET	(0X11C)
#define GPIOF_CON			0x7f0080a0
#define GPIOF_DAT			0x7f0080a4
#define GPIOF_PUD			0x7f0080a8

//#define GSL_NOID_VERSION

#ifdef GSL_MONITOR
static struct delayed_work gsl_monitor_work;
static struct workqueue_struct *gsl_monitor_workqueue = NULL;
static char int_1st[4] = {0};
static char int_2nd[4] = {0};
#endif 
#define HAVE_CLICK_TIMER

#ifdef HAVE_CLICK_TIMER

bool send_key = false;
struct semaphore my_sem;
static struct workqueue_struct *gsl_timer_workqueue = NULL;

#endif

#define KEY_SIZE_Y   80
#define KEY_SIZE_X   120

#define ENTER_SIZE 30

static u16 x_old_wb = 0;
static u16 y_old_wb = 0;

//int max_size_x = 0;
//int min_size_x = 0;
//int max_size_y = 0;
//int min_size_y = 0;
#define KEY_SIZE_CLICK   180
#define KEY_SIZE_XOFFSET   0
#define KEY_SIZE_YOFFSET   60
//int avarage_size_x = 0;
//int avarage_size_y = 0;

int keynum = 0;
#ifdef HAVE_TOUCH_KEY
static u16 key = 0;
static int key_state_flag = 0;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;
};

//int key_x[512];
//int key_y[512];
int largest_id = 0;

#define POINTS_NUM   512
struct gslx680_data{
	int key_count;
	int key_x[POINTS_NUM];
	int key_y[POINTS_NUM];
	int max_size_x;
	int min_size_x;
	int max_size_y;
	int min_size_y;
	int avarage_size_x;
	int avarage_size_y;
};
struct gslx680_data datas[MAX_CONTACTS] = {0};
bool longpress_enabled = false;

u16 key_array[]={
     KEY_RIGHT,
     KEY_LEFT,
     KEY_UP,
     KEY_DOWN,
     KEY_ENTER,
     KEY_BACK,
}; 



#define MAX_KEY_NUM     (sizeof(key_array)/sizeof(key_array[0]))

struct key_data gsl_key_data[MAX_KEY_NUM] = {
	{KEY_BACK,  816, 836,115, 125},
	{KEY_HOME,  816, 836,259 ,269},	
	{KEY_MENU,  816, 836,398, 410},
	{KEY_SEARCH, 2048, 2048, 2048, 2048},
};
#endif


#if defined (HAVE_CLICK_TIMER)
	struct work_struct click_work;
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
	struct gsl_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	u8 prev_touches;
	bool is_suspended;
	bool int_pending;
	struct mutex sus_lock;
	int irq;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
#if defined (HAVE_CLICK_TIMER)
	struct work_struct click_work;
#endif

#ifdef GSL_TIMER
	struct timer_list gsl_timer;
#endif

};



static u32 id_sign[MAX_CONTACTS+1] = {0};
static u8 id_state_flag[MAX_CONTACTS+1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS+1] = {0};
static u16 x_old[MAX_CONTACTS+1] = {0};
static u16 y_old[MAX_CONTACTS+1] = {0};
static u16 x_new = 0;
static u16 y_new = 0;


///////////////////////////////////////////////
//specific tp related macro: need be configured for specific tp

#define GSLX680_I2C_NAME 	        "gslX680"


#define CTP_IRQ_MODE			    (IRQF_TRIGGER_HIGH | IRQF_DISABLED)

#define CTP_NAME			        GSLX680_I2C_NAME
#define SCREEN_MAX_X		        (screen_max_x)
#define SCREEN_MAX_Y		        (screen_max_y)

static char* fwname;
static int fw_index = -1;


#define GSLX680_I2C_ADDR 0x40
						 
#define GSLX680_USED     "\n \
												  \n+++++++++++++++++++++++++++++++++ \
												  \n++++++ GSLX680 new used +++++++++ \
												  \n+++++++++++++++++++++++++++++++++ \
												  \n"
													 
#define GSLX680_IC_INFO  "\n============================================================== \
												  \nIC	 :GSLX680 \
												  \nAUTHOR :mbgalex@163.com \
												  \nVERSION:2013-07-20_10:41\n"

static int screen_max_x = 0;
static int screen_max_y = 0;
static int revert_x_flag = 0;
static int revert_y_flag = 0;
static int exchange_x_y_flag = 0;


static __u32 twi_id = 0;

static u32 debug_mask = 0;
enum{
	DEBUG_INIT = 1U << 0,
	DEBUG_SUSPEND = 1U << 1,
	DEBUG_INT_INFO = 1U << 2,
	DEBUG_X_Y_INFO = 1U << 3,
	DEBUG_KEY_INFO = 1U << 4,
	DEBUG_WAKEUP_INFO = 1U << 5,
	DEBUG_OTHERS_INFO = 1U << 6,
};

#define dprintk(level_mask,fmt,arg...)    if(unlikely(debug_mask & level_mask)) \
        printk("***CTP***"fmt, ## arg)


/* Addresses to scan */
static const unsigned short normal_i2c[2] = {GSLX680_I2C_ADDR, I2C_CLIENT_END};

static void glsX680_init_events(struct work_struct *work);
static void glsX680_resume_events(struct work_struct *work);
struct workqueue_struct *gslX680_wq;
struct workqueue_struct *gslX680_resume_wq;
static DECLARE_WORK(glsX680_init_work, glsX680_init_events);///
static DECLARE_WORK(glsX680_resume_work, glsX680_resume_events);
struct i2c_client *glsX680_i2c;
struct gsl_ts *ts_init;

struct gslx680_curve_data{
	int key_count;
	int key_x[511];
	int key_y[511];
};
static struct gslx680_curve_data curve_data;
static bool recording_curve_data = false;

spinlock_t gsl_lock;	
#define GSL_OFFSET	0
static ssize_t gslx680_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	int	retval = 0,i;
/*
	if (unlikely(off >= attr->size))
		return 0;
	if (unlikely(off < 0))
		return -EINVAL;
	
	if ((off + count) > attr->size){
		count = attr->size - off;
	}
	count = (count<curve_data.key_count) ? count : curve_data.key_count;
	
	off += GSL_OFFSET;
*/
	if(count < sizeof(curve_data)){
		printk("\n err buf size is not enought, current count:%d/%d \n",count,sizeof(curve_data));
		return -EINVAL;
	}
	spin_lock(&gsl_lock);
	memcpy(buf, &curve_data, sizeof(curve_data));	
	spin_unlock(&gsl_lock);
	for(i = 0; i<curve_data.key_count; i++){
		if(((struct gslx680_curve_data *)buf)->key_x[i]!= curve_data.key_x[i] ||
		   ((struct gslx680_curve_data *)buf)->key_y[i]!= curve_data.key_y[i]){
			printk("gslx680 points does not match \n");
			break;
		}
	}
	if(i==curve_data.key_count){
		printk("\n gslx680 points match, all count :%d \n", curve_data.key_count);
	}

	retval = sizeof(curve_data);
	return retval;
}

static ssize_t gslx680_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	int	retval = 0;
	char val = *(buf);
	
	printk(" off: %d , count: %d ,val: %d\n", off, count,val);
	if(val == '1'){
		recording_curve_data = true;
		printk(" recording_curve_data opened \n");
	}else if(val == '0'){
		recording_curve_data = false;
		printk(" recording_curve_data closed \n");
	}else{
		printk(" invalid val.. \n");
	}
	
	retval = count;
	return retval;
}

static struct bin_attribute gslx680 = {
	.attr = {
		.name	= "gslx680",
		.mode	= S_IRUGO | S_IWUSR |S_IRWXUGO,
	},

	.read	= gslx680_read,
	.write	= gslx680_write,
	/* size gets set up later */
	.size   = sizeof(struct gslx680_curve_data),
};



int ctp_i2c_write_bytes(struct i2c_client *client, uint8_t *data, uint16_t len)
{
	struct i2c_msg msg;
	int ret=-1;
	
	msg.flags = !I2C_M_RD;
	msg.addr = client->addr;
	msg.len = len;
	msg.buf = data;		
	
	ret=i2c_transfer(client->adapter, &msg,1);
	return ret;
}

bool ctp_i2c_test(struct i2c_client * client)
{
	int ret,retry;
	uint8_t test_data[1] = { 0 };	//only write a data address.

	for(retry=0; retry < 2; retry++)
	{
		ret =ctp_i2c_write_bytes(client, test_data, 1);	//Test i2c.
		if (ret == 1)
			break;
		msleep(50);
	}

	return ret==1 ? true : false;
}

static int ctp_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int ret;

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
                return -ENODEV;

	if(twi_id == adapter->nr){
    	pr_info("%s: addr= %x\n",__func__,client->addr);
        ret = ctp_i2c_test(client);
        if(!ret){
        	pr_info("%s:I2C connection might be something wrong \n",__func__);
        	return -ENODEV;
        }else{      
            pr_info("I2C connection sucess!\n");
            strlcpy(info->type, CTP_NAME, I2C_NAME_SIZE);
			pr_info("%s", GSLX680_USED);
    		return 0;	
	    }

	}else{
		return -ENODEV;
	}
}

static int gslX680_chip_init(void)
{
	gpio_direction_output(gpio_reset, 1);
         msleep(20);
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
static u32 gsl_read_interface(struct i2c_client *client, u8 reg, u8 *buf, u32 num)
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

	return i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg)) == ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}
#endif

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static int gsl_ts_write(struct i2c_client *client, u8 addr, u8 *pdata, int datalen)
{
	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen = 0;
	if (datalen > 125){
		printk("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}
	
	tmp_buf[0] = addr;
	bytelen++;
	
	if (datalen != 0 && pdata != NULL){
		memcpy(&tmp_buf[bytelen], pdata, datalen);
		bytelen += datalen;
	}
	
	ret = i2c_master_send(client, tmp_buf, bytelen);
	return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr, u8 *pdata, unsigned int datalen)
{
	int ret = 0;

	if (datalen > 126){
		printk("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0){
		printk("%s set data address fail!\n", __func__);
		return ret;
	}
	
	return i2c_master_recv(client, pdata, datalen);
}

static ssize_t gslX680_reg_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
         u8 mem_buf[4]  = {0};
         u8 int_buf[4]  = {0};
	u8 power_buf[4]  = {0};
         u8 point_buf = 0;

         gsl_ts_read(ts_init->client,0xb0, mem_buf, sizeof(mem_buf));
         printk("check mem read 0xb0  = %x %x %x %x \n",
	                  mem_buf[3], mem_buf[2], mem_buf[1], mem_buf[0]);
         gsl_ts_read(ts_init->client,0xb4, int_buf, sizeof(int_buf));
         printk("int  num  read  0xb4  = %d \n",
                           (int_buf[3]<<24) |( int_buf[2]<<16 ) |(int_buf[1]<<8) |int_buf[0]);
         gsl_ts_read(ts_init->client,0xbc, power_buf, sizeof(power_buf));
         printk("power check read 0xbc = %4x \n",
                           (power_buf[3]<<24) |( power_buf[2]<<16 ) |(power_buf[1]<<8) |power_buf[0]);
         gsl_ts_read(ts_init->client,0x80, &point_buf, 1);
         printk("point count read  0x80 = %d \n",point_buf);

         return sprintf(buf, "[check mem read = 0x%4x ]  [int num read = %d ]  [power check read = 0x%4x ]  [point count read = %d ] \n",
		(mem_buf[3]<<24) |( mem_buf[2]<<16 ) |(mem_buf[1]<<8) |mem_buf[0] ,
		(int_buf[3]<<24) |( int_buf[2]<<16 ) |(int_buf[1]<<8) |int_buf[0],
		(power_buf[3]<<24) |( power_buf[2]<<16 ) |(power_buf[1]<<8) |power_buf[0],point_buf);
}

static DEVICE_ATTR(debug_reg, 0664, gslX680_reg_show, NULL);

static __inline__ void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

static int gsl_find_fw_idx(const char* name)
{
	int i = 0;

	if (NULL != name) {
		for (i=0; i<ARRAY_SIZE(gslx680_fw_grp); i++) {
			if (!strcmp(name, gslx680_fw_grp[i].name))
				return i;
		}
	}
	return -1;
}


static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN*4 + 1] = {0};
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len;
	const struct fw_data *ptr_fw;
		
	printk("=============gsl_load_fw start==============\n");

	ptr_fw = gslx680_fw_grp[fw_index].fw;
	source_len = gslx680_fw_grp[fw_index].size;			

	for (source_line = 0; source_line < source_len; source_line++) 
	{
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == ptr_fw[source_line].offset)
		{
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		}
		else 
		{
			if (1 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
	    			buf[0] = (u8)ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (0 == send_flag % (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20)) 
			{
	    			gsl_write_interface(client, buf[0], buf, cur - buf - 1);
	    			cur = buf + 1;
			}

			send_flag++;
		}
		//mdelay(10);
	}

	printk("=============gsl_load_fw end==============\n");

}

static void startup_chip(struct i2c_client *client)
{
	u8 tmp = 0x00;

#ifdef GSL_NOID_VERSION
	gsl_DataInit(gslX680_config_data[fw_index]);
#endif
	gsl_ts_write(client, 0xe0, &tmp, 1);
	msleep(10);
}

static void reset_chip(struct i2c_client *client)
{
	u8 tmp = 0x88;
	u8 buf[4] = {0x00};
	
	gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
	msleep(10);

	tmp = 0x04;
	gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
	msleep(10);
	gsl_ts_write(client, 0xbc, buf, sizeof(buf));
	msleep(10);
}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4]	= {0};

	write_buf[0] = 0x88;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(20);
	write_buf[0] = 0x03;
	gsl_ts_write(client, 0x80, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x04;
	gsl_ts_write(client, 0xe4, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x00;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(20);
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
static int init_chip(struct i2c_client *client)
{
	int rc = 0;
	struct i2c_adapter *adapter = client->adapter;
	printk("adapter->nr:%d\n",adapter->nr);

	gslX680_shutdown_low();
	msleep(50);
	gslX680_shutdown_high();
	msleep(30);
	
	rc = test_i2c(client);
	if( rc<0 ||(!ctp_i2c_test(client))){
		pr_err("------gslX680 test_i2c error------\n");
		return -1;
	}else{
		pr_err("------gslX680 test_i2c ok------\n");
	}
	
	clr_reg(client);
	reset_chip(client);
	
	gsl_load_fw(client);
	
	startup_chip(client);
	reset_chip(client);
	startup_chip(client);
	printk("init done\n" );
	
	return 0;
}

static void check_mem_data(struct i2c_client *client)
{
	u8 read_buf[4]  = {0};

	//if(gsl_chipType_new == 1)	
	{
	         if(ts_init->is_suspended != false)
                         msleep(30);
		gsl_ts_read(client,0xb0, read_buf, sizeof(read_buf));
		printk("#########check mem read 0xb0 = %x %x %x %x #########\n", read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
	
		if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a || read_buf[0] != 0x5a)
		{
			init_chip(client);////
		}
	}
}
#ifdef STRETCH_FRAME
static void stretch_frame(u16 *x, u16 *y)
{
	u16 temp_x = *x;
	u16 temp_y = *y;
	u16 temp_0, temp_1, temp_2;

	if(temp_x < X_STRETCH_MAX + X_STRETCH_CUST)
	{
		temp_0 = temp_1 = temp_2 = 0;
		temp_0 = X_STRETCH_MAX + X_STRETCH_CUST - temp_x;
		temp_0 = temp_0 > X_STRETCH_CUST ? X_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + X_RATIO_CUST)/100;
		if(temp_x < X_STRETCH_MAX)
		{
			temp_1 = X_STRETCH_MAX - temp_x;
			temp_1 = temp_1 > X_STRETCH_MAX/4 ? X_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*XL_RATIO_1)/100;
		}	
		if(temp_x < 3*X_STRETCH_MAX/4)
		{
			temp_2 = 3*X_STRETCH_MAX/4 - temp_x;
			temp_2 = temp_2*(100 + 2*XL_RATIO_2)/100;
		}
		*x = (temp_0 + temp_1 +temp_2) < (X_STRETCH_MAX + X_STRETCH_CUST) ? ((X_STRETCH_MAX + X_STRETCH_CUST) - (temp_0 + temp_1 +temp_2)) : 1;
	}
	else if(temp_x > (CTP_MAX_X -X_STRETCH_MAX - X_STRETCH_CUST))
	{
		temp_0 = temp_1 = temp_2 = 0;
		temp_0 = temp_x - (CTP_MAX_X -X_STRETCH_MAX - X_STRETCH_CUST);
		temp_0 = temp_0 > X_STRETCH_CUST ? X_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + X_RATIO_CUST)/100;
		if(temp_x > (CTP_MAX_X -X_STRETCH_MAX))
		{
			temp_1 = temp_x - (CTP_MAX_X -X_STRETCH_MAX);
			temp_1 = temp_1 > X_STRETCH_MAX/4 ? X_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*XR_RATIO_1)/100;
		}	
		if(temp_x > (CTP_MAX_X -3*X_STRETCH_MAX/4))
		{
			temp_2 = temp_x - (CTP_MAX_X -3*X_STRETCH_MAX/4);
			temp_2 = temp_2*(100 + 2*XR_RATIO_2)/100;
		}
		*x = (temp_0 + temp_1 +temp_2) < (X_STRETCH_MAX + X_STRETCH_CUST) ? ((CTP_MAX_X -X_STRETCH_MAX - X_STRETCH_CUST) + (temp_0 + temp_1 +temp_2)) : (CTP_MAX_X - 1);
	}

	if(temp_y < Y_STRETCH_MAX + Y_STRETCH_CUST)
	{
		temp_0 = temp_1 = temp_2 = 0;
		temp_0 = Y_STRETCH_MAX + Y_STRETCH_CUST - temp_y;
		temp_0 = temp_0 > Y_STRETCH_CUST ? Y_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + Y_RATIO_CUST)/100;
		if(temp_y < Y_STRETCH_MAX)
		{
			temp_1 = Y_STRETCH_MAX - temp_y;
			temp_1 = temp_1 > Y_STRETCH_MAX/4 ? Y_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*YL_RATIO_1)/100;
		}	
		if(temp_y < 3*Y_STRETCH_MAX/4)
		{
			temp_2 = 3*Y_STRETCH_MAX/4 - temp_y;
			temp_2 = temp_2*(100 + 2*YL_RATIO_2)/100;
		}
		*y = (temp_0 + temp_1 +temp_2) < (Y_STRETCH_MAX + Y_STRETCH_CUST) ? ((Y_STRETCH_MAX + Y_STRETCH_CUST) - (temp_0 + temp_1 +temp_2)) : 1;
	}
	else if(temp_y > (CTP_MAX_Y -Y_STRETCH_MAX - Y_STRETCH_CUST))
	{
		temp_0 = temp_1 = temp_2 = 0;	
		temp_0 = temp_y - (CTP_MAX_Y -Y_STRETCH_MAX - Y_STRETCH_CUST);
		temp_0 = temp_0 > Y_STRETCH_CUST ? Y_STRETCH_CUST : temp_0;
		temp_0 = temp_0*(100 + Y_RATIO_CUST)/100;
		if(temp_y > (CTP_MAX_Y -Y_STRETCH_MAX))
		{
			temp_1 = temp_y - (CTP_MAX_Y -Y_STRETCH_MAX);
			temp_1 = temp_1 > Y_STRETCH_MAX/4 ? Y_STRETCH_MAX/4 : temp_1;
			temp_1 = temp_1*(100 + 2*YR_RATIO_1)/100;
		}	
		if(temp_y > (CTP_MAX_Y -3*Y_STRETCH_MAX/4))
		{
			temp_2 = temp_y - (CTP_MAX_Y -3*Y_STRETCH_MAX/4);
			temp_2 = temp_2*(100 + 2*YR_RATIO_2)/100;
		}
		*y = (temp_0 + temp_1 +temp_2) < (Y_STRETCH_MAX + Y_STRETCH_CUST) ? ((CTP_MAX_Y -Y_STRETCH_MAX - Y_STRETCH_CUST) + (temp_0 + temp_1 +temp_2)) : (CTP_MAX_Y - 1);
	}
}
#endif

#ifdef FILTER_POINT
static void filter_point(u16 x, u16 y , u8 id)
{
	u16 x_err =0;
	u16 y_err =0;
	u16 filter_step_x = 0, filter_step_y = 0;

	id_sign[id] = id_sign[id] + 1;
	if(id_sign[id] == 1)
	{
		x_old[id] = x;
		y_old[id] = y;
	}

	x_err = x > x_old[id] ? (x -x_old[id]) : (x_old[id] - x);
	y_err = y > y_old[id] ? (y -y_old[id]) : (y_old[id] - y);

	if( (x_err > FILTER_MAX && y_err > FILTER_MAX/3) || (x_err > FILTER_MAX/3 && y_err > FILTER_MAX) )
	{
		filter_step_x = x_err;
		filter_step_y = y_err;
	}
	else
	{
		if(x_err > FILTER_MAX)
			filter_step_x = x_err;
		if(y_err> FILTER_MAX)
			filter_step_y = y_err;
	}

	if(x_err <= 2*FILTER_MAX && y_err <= 2*FILTER_MAX)
	{
		filter_step_x >>= 2;
		filter_step_y >>= 2;
	}
	else if(x_err <= 3*FILTER_MAX && y_err <= 3*FILTER_MAX)
	{
		filter_step_x >>= 1;
		filter_step_y >>= 1;
	}
	else if(x_err <= 4*FILTER_MAX && y_err <= 4*FILTER_MAX)
	{
		filter_step_x = filter_step_x*3/4;
		filter_step_y = filter_step_y*3/4;
	}

	x_new = x > x_old[id] ? (x_old[id] + filter_step_x) : (x_old[id] - filter_step_x);
	y_new = y > y_old[id] ? (y_old[id] + filter_step_y) : (y_old[id] - filter_step_y);

	x_old[id] = x_new;
	y_old[id] = y_new;
}
#else

static void record_point(u16 x, u16 y , u8 id)
{
	u16 x_err =0;
	u16 y_err =0;

	id_sign[id]=id_sign[id]+1;

	if(id_sign[id]==1){
		x_old[id]=x;
		y_old[id]=y;
	}

	x = (x_old[id] + x)/2;
	y = (y_old[id] + y)/2;

	if(x>x_old[id]){
		x_err=x -x_old[id];
	}
	else{
		x_err=x_old[id]-x;
	}

	if(y>y_old[id]){
		y_err=y -y_old[id];
	}
	else{
		y_err=y_old[id]-y;
	}

	if( (x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3) ){
		x_new = x;     x_old[id] = x;
		y_new = y;     y_old[id] = y;
	}
	else{
		if(x_err > 3){
			x_new = x;     x_old[id] = x;
		}
		else
			x_new = x_old[id];
		if(y_err> 3){
			y_new = y;     y_old[id] = y;
		}
		else
			y_new = y_old[id];
	}

	if(id_sign[id]==1){
		x_new= x_old[id];
		y_new= y_old[id];
	}

}
#endif
#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
	if(ch>='0' && ch<='9')
		return (ch-'0');
	else
		return (ch-'a'+10);
}

static int gsl_config_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *ptr = page;
	char temp_data[5] = {0};
	unsigned int tmp=0;
	if('v'==gsl_read[0]&&'s'==gsl_read[1])
	{
#ifdef GSL_NOID_VERSION
		tmp=gsl_version_id();
#else 
		tmp=0x20121215;
#endif
		ptr += sprintf(ptr,"version:%x\n",tmp);
	}
	else if('r'==gsl_read[0]&&'e'==gsl_read[1])
	{
		if('i'==gsl_read[3])
		{
#ifdef GSL_NOID_VERSION 
			tmp=(gsl_data_proc[5]<<8) | gsl_data_proc[4];
			ptr +=sprintf(ptr,"gsl_config_data_id[%d] = ",tmp);
			if(tmp>=0&&tmp<256)
				ptr +=sprintf(ptr,"%d\n",gsl_config_data_id[tmp]); 
#endif
		}
		else 
		{
			gsl_ts_write(glsX680_i2c,0xf0,&gsl_data_proc[4],4);
			gsl_ts_read(glsX680_i2c,gsl_data_proc[0],temp_data,4);
			gsl_ts_read(glsX680_i2c,gsl_data_proc[0],temp_data,4);
			ptr +=sprintf(ptr,"offset : {0x%02x,0x",gsl_data_proc[0]);
			ptr +=sprintf(ptr,"%02x",temp_data[3]);
			ptr +=sprintf(ptr,"%02x",temp_data[2]);
			ptr +=sprintf(ptr,"%02x",temp_data[1]);
			ptr +=sprintf(ptr,"%02x};\n",temp_data[0]);
		}
	}
	*eof = 1;
	return (ptr - page);
}
static int gsl_config_write_proc(struct file *file, const char *buffer, unsigned long count, void *data)
{
	u8 buf[8] = {0};
	int tmp = 0;
	int tmp1 = 0;

	
	if(count > CONFIG_LEN)
	{
		printk("size not match [%d:%ld]\n", CONFIG_LEN, count);
        return -EFAULT;
	}
	
	if(copy_from_user(gsl_read, buffer, (count<CONFIG_LEN?count:CONFIG_LEN)))
	{
		printk("copy from user fail\n");
        return -EFAULT;
	}
	dprintk(DEBUG_OTHERS_INFO,"[tp-gsl][%s][%s]\n",__func__,gsl_read);

	buf[3]=char_to_int(gsl_read[14])<<4 | char_to_int(gsl_read[15]);	
	buf[2]=char_to_int(gsl_read[16])<<4 | char_to_int(gsl_read[17]);
	buf[1]=char_to_int(gsl_read[18])<<4 | char_to_int(gsl_read[19]);
	buf[0]=char_to_int(gsl_read[20])<<4 | char_to_int(gsl_read[21]);
	
	buf[7]=char_to_int(gsl_read[5])<<4 | char_to_int(gsl_read[6]);
	buf[6]=char_to_int(gsl_read[7])<<4 | char_to_int(gsl_read[8]);
	buf[5]=char_to_int(gsl_read[9])<<4 | char_to_int(gsl_read[10]);
	buf[4]=char_to_int(gsl_read[11])<<4 | char_to_int(gsl_read[12]);
	if('v'==gsl_read[0]&& 's'==gsl_read[1])//version //vs
	{
		printk("gsl version\n");
	}
	else if('s'==gsl_read[0]&& 't'==gsl_read[1])//start //st
	{
		gsl_proc_flag = 1;
		reset_chip(glsX680_i2c);
	}
	else if('e'==gsl_read[0]&&'n'==gsl_read[1])//end //en
	{
		msleep(20);
		reset_chip(glsX680_i2c);
		startup_chip(glsX680_i2c);
		
#ifdef GSL_NOID_VERSION
		gsl_DataInit(gslX680_config_data[fw_index]);		
#endif
		gsl_proc_flag = 0;
	}
	else if('r'==gsl_read[0]&&'e'==gsl_read[1])//read buf //
	{
		memcpy(gsl_data_proc,buf,8);
	}
	else if('w'==gsl_read[0]&&'r'==gsl_read[1])//write buf
	{
		gsl_ts_write(glsX680_i2c,buf[4],buf,4);
	}

#ifdef GSL_NOID_VERSION
	else if('i'==gsl_read[0]&&'d'==gsl_read[1])//write id config //
	{
		tmp1=(buf[7]<<24)|(buf[6]<<16)|(buf[5]<<8)|buf[4];
		tmp=(buf[3]<<24)|(buf[2]<<16)|(buf[1]<<8)|buf[0];
		if(tmp1>=0 && tmp1<256)
		{
			gslX680_config_data[fw_index][tmp1] = tmp;
		}
	}
#endif
	return count;
}
#endif
#ifdef HAVE_TOUCH_KEY
static void report_key(struct gsl_ts *ts, u16 x, u16 y)
{
	u16 i = 0;
	for(i = 0; i < MAX_KEY_NUM; i++) {
		if((gsl_key_data[i].x_min < x) && (x < gsl_key_data[i].x_max)&&(gsl_key_data[i].y_min < y) &&\
		  (y < gsl_key_data[i].y_max)){
			key = gsl_key_data[i].key;
			dprintk(DEBUG_KEY_INFO,"key=%d\n",key);
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
	dprintk(DEBUG_X_Y_INFO,"source data:ID:%d, X:%d, Y:%d, W:%d\n", id, x, y,pressure);
	if(1 == exchange_x_y_flag){
		swap(x, y);
	}
	if(1 == revert_x_flag){
		x = SCREEN_MAX_X - x;
	}
	if(1 == revert_y_flag){
		y = SCREEN_MAX_Y - y;
	}
	dprintk(DEBUG_X_Y_INFO,"report data:ID:%d, X:%d, Y:%d, W:%d\n", id, x, y, pressure);

	if(x>SCREEN_MAX_X||y>SCREEN_MAX_Y)
	{
	#ifdef HAVE_TOUCH_KEY
		
		report_key(ts,x,y);

	#endif
		return;
	}

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
	input_report_abs(ts->input, ABS_MT_POSITION_X,x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);
	input_mt_sync(ts->input);
#endif
}


static void max_x(int num,int id){
   if(num == 0){
     datas[id].max_size_x = datas[id].min_size_x = datas[id].key_x[num] ;
   }else{
     if(datas[id].key_x[num] < datas[id].min_size_x){
	 	datas[id].min_size_x = datas[id].key_x[num];
	 }else if(datas[id].key_x[num] > datas[id].max_size_x){
		datas[id].max_size_x = datas[id].key_x[num];
	 }
   }
}


static void max_y(int num,int id){
   if(num == 0){
     datas[id].max_size_y = datas[id].min_size_y = datas[id].key_y[num] ;
   }else{
     if(datas[id].key_y[num] < datas[id].min_size_y){
	 	datas[id].min_size_y = datas[id].key_y[num];
	 }else if(datas[id].key_y[num] > datas[id].max_size_y){
		datas[id].max_size_y = datas[id].key_y[num];
	 }
   }
}

static void old_wb_key(int num,int id){

   if(num == 0)
   	return ;
	x_old_wb  = datas[id].key_x[num];
	y_old_wb  = datas[id].key_y[num];
	
}

unsigned long int_sqrt(unsigned long x)
{
    unsigned long op, res, one;
    op = x;
    res = 0;
    one = 1UL << (BITS_PER_LONG - 2);
    while (one > op)
        one >>= 2;
  
    while (one != 0) {
        if (op >= res + one) {
            op = op - (res + one);
            res = res +  2 * one;
        }
        res /= 2;
        one /= 4;
    }
    return res;
}

static bool report_back_event(struct gsl_ts *ts, int cout, int id)
{
	int mid = cout/2;
	int div=(cout<=10) ? cout : 10;
	int k=0,b=0,d0=0,d1=0,i=0;
	bool ret = false;
	int maxd1 = int_sqrt(cfg_dts.xMax*cfg_dts.xMax+cfg_dts.yMax*cfg_dts.yMax);
	//printk("\n\n ====%d, %d, %d\n",int_sqrt(1000),int_sqrt(100),int_sqrt(9));
	int key_count = datas[largest_id].key_count;
	int *key_x = datas[largest_id].key_x;
	int *key_y = datas[largest_id].key_y;;
	int max_size_x = datas[largest_id].max_size_x;
	int min_size_x = datas[largest_id].min_size_x;
	int max_size_y = datas[largest_id].max_size_y;
	int min_size_y = datas[largest_id].min_size_y;

	if(cout<8 ||
	   ((max_size_y - min_size_y)*2 <= KEY_SIZE_X) ||
	   ((max_size_x - min_size_x)/2 <= KEY_SIZE_Y)){
		return false;
	}
	
	if((key_x[0]==key_x[cout-1])){
		d0 = key_y[0]-key_y[cout-1];
		d0 = d0>0 ? d0 :d0*(-1);
		d1 = key_x[mid]-key_x[0];
		d1 = d1>0 ? d1 : d1*(-1);
	}else if((key_y[0]==key_y[cout-1])){
		d0 = key_x[0]-key_x[cout-1];
		d0 = d0>0 ? d0 :d0*(-1);
		d1 = key_y[mid]-key_y[0];
		d1 = d1>0 ? d1 : d1*(-1);
	}else if ((key_x[0]!=key_x[cout-1])&&(key_y[0]!=key_y[cout-1])){
	
		k = ((key_y[0] - key_y[cout-1])/(key_x[0] - key_x[cout-1]));
		if(k==0){
			printk("find back event : k try again...\n");
			cout = cout-(cout/div);
			div=(cout<=10) ? cout : 10;
			mid  = cout/2;
			k = ((key_y[0] - key_y[cout-1])/(key_x[0] - key_x[cout-1]));
		}
		b =	key_y[0]-k*key_x[0];
		d0 = int_sqrt((key_x[0] - key_x[cout-1])*(key_x[0] - key_x[cout-1])+(key_y[0] - key_y[cout-1])*(key_y[0] - key_y[cout-1]));
		for(i=1; i<=div-2; i++){
			int tmp =0;
			printk("find back event : d1 try...mid:%d/%d\n",mid,cout);
			if(k != 0){
				d1 =(k*key_x[mid]+(-1)*key_y[mid]+b);
				d1 = d1>0 ? d1 : d1*(-1);
				d1 = d1/(int_sqrt(k*k+1));
			}else{
				d1 = key_y[mid]-(key_y[0]+key_y[cout-1])/2;
				d1 = d1>0 ? d1 : d1*(-1);
			}
			//printk("find back event key_x[mid]: %d, key_y[mid]: %d, d1: %d \n",key_x[mid],key_y[mid],d1);
			if(d1>=(d0/3) && d1<maxd1){
				break;
			}
			tmp = i%2>0 ? 1 :-1;
			mid += tmp*i*((cout/div > 2)?(cout/div-1):(cout/div));
		}

	}
	/*
	printk("find back event cout: %d\n",cout);
	printk("find back event key_x[0]: %d, key_y[0]: %d\n",key_x[0],key_y[0]);
	printk("find back event key_x[cout-1]: %d, key_y[cout-1]: %d\n",key_x[cout-1],key_y[cout-1]);
	printk("find back event key_x[mid]: %d, key_y[mid]: %d\n",key_x[mid],key_y[mid]);
	printk("find back event k : %d, b: %d\n",k ,b);
	printk("find back event d0 : %d, d1: %d\n",d0 ,d1);
	*/
	if(d1>=maxd1){
		printk("find back event : d1 out of range\n");
		ret = false;
	}else if(d1>=(d0/3)){
		printk(" ====send back key by wubing\n");
		input_report_key(ts->input, key_array[5], 1);
		input_sync(ts->input);	
		input_report_key(ts->input, key_array[5], 0);
		input_sync(ts->input);	
		ret = true;
	}else{
		ret = false;
	}
	return ret;
}
static void process_gslX680_data(struct gsl_ts *ts)
{
	u8 id, touches;
	u16 x, y;
	int i = 0;
	int tmp1 = 0;
	u8 buf[4]={0};
#ifdef GSL_NOID_VERSION
    struct gsl_touch_info cinfo;
#endif
	touches = ts->touch_data[ts->dd->touch_index];

#ifdef GSL_NOID_VERSION
	cinfo.finger_num = touches;
	dprintk(DEBUG_OTHERS_INFO,"tp-gsl  finger_num = %d\n",cinfo.finger_num); 
	
	for(i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS); i ++)
	{
		cinfo.x[i] = join_bytes( ( ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->x_index + 4 * i]);
		cinfo.y[i] = join_bytes(( ts->touch_data[ts->dd->y_index + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->y_index + 4 * i ]);
	}
	cinfo.finger_num = ts->touch_data[0] | (ts->touch_data[1]<<8)|(ts->touch_data[2]<<16)|
		(ts->touch_data[3]<<24);
	
	gsl_alg_id_main(&cinfo);////
	/*if gsl_alg_id library jumps the first touch , print out */
	for(i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS); i ++)
	{ 
		if(i==0 &&(cinfo.id[i]<1 || cinfo.id[i]>MAX_FINGERS)){
			printk("---Total %d touches, but(%d ,%d) for touch:%d is not processed by library, and cinfo.id is %d \n", touches,cinfo.x[i] ,cinfo.y[i],(i+1),cinfo.id[i]);
		}
	}
	dprintk(DEBUG_OTHERS_INFO,"tp-gsl  finger_num = %d\n",cinfo.finger_num);
	
	tmp1=gsl_mask_tiaoping();////
	if(tmp1>0&&tmp1<0xffffffff)
	{
		buf[0]=0xa;
		buf[1]=0;
		buf[2]=0;
		buf[3]=0;
		gsl_ts_write(ts->client,0xf0,buf,4);
		
		buf[0]=(u8)(tmp1 & 0xff);
		buf[1]=(u8)((tmp1>>8) & 0xff);
		buf[2]=(u8)((tmp1>>16) & 0xff);
		buf[3]=(u8)((tmp1>>24) & 0xff);
		printk("tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,buf[3]=%02x\n",
			tmp1,buf[0],buf[1],buf[2],buf[3]);
		gsl_write_interface(ts->client,0x8,buf,4);
	}
	
	touches = cinfo.finger_num;
#endif

	for(i=1;i<=MAX_CONTACTS;i++){
		
		if(touches == 0)
			id_sign[i] = 0;
		
		id_state_flag[i] = 0;
	}
	
	for(i= 0;i < (touches > MAX_FINGERS ? MAX_FINGERS : touches);i++)
	{
	#ifdef GSL_NOID_VERSION
		id = cinfo.id[i];
		x =  cinfo.x[i];
		y =  cinfo.y[i];	
	#else
		x = join_bytes( ( ts->touch_data[ts->dd->x_index  + 4 * i + 1] & 0xf),
				ts->touch_data[ts->dd->x_index + 4 * i]);
		y = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
				ts->touch_data[ts->dd->y_index + 4 * i ]);
		id = ts->touch_data[ts->dd->id_index + 4 * i] >> 4;
    #endif
		if(1 <=id && id <= MAX_CONTACTS)
		{
			if (cfg_dts.XYSwap == 1) {
				int tmp;
				tmp = x;
				x = y;
				y = tmp;
				//printk("----xyswap----\n");
			}
			
			if (cfg_dts.xRevert == 1) {
				x = cfg_dts.xMax - x;
				//printk("----xRevert----\n");
			}

			if (cfg_dts.yRevert == 1) {
				y = cfg_dts.yMax - y;
				//printk("----yRevert----\n");
			}

		#ifdef STRETCH_FRAME
			stretch_frame(&x, &y);

		#endif
		#ifdef FILTER_POINT
			filter_point(x, y ,id);
		#else
			record_point(x, y , id);
		#endif
			//report_data(ts, x_new, y_new, 10, id);
			//printk("==================================\n");
			if(datas[id].key_count <= POINTS_NUM)///
			{
                                //old_wb_key(key_count,id);
                                datas[id].key_x[datas[id].key_count] = x_new;
				datas[id].key_y[datas[id].key_count] = y_new;
				max_x(datas[id].key_count,id);
                                max_y(datas[id].key_count,id);
				datas[id].key_count++;
				if(datas[id].key_count>datas[largest_id].key_count)
				{
					largest_id = id;
				}
				
				if(cfg_dts.use_click_mode==1){
					datas[id].avarage_size_x += x_new;
					datas[id].avarage_size_y += y_new;
				}
				//printk(" test in key store in here, x_new is %d , y_new is %d , key_count is %d \n", x_new ,y_new,key_count);
				//printk("max_size_x - min_size_x ==%d\n",max_size_x - min_size_x);
				//printk("max_size_y - min_size_y ==%d\n",max_size_y - min_size_y);
			}
			id_state_flag[id] = 1;
		}
	}
	
	
	for(i = 1;i <= MAX_CONTACTS ; i++)
	{
		if( (0 == touches) || ((0 != id_state_old_flag[i]) && (0 == id_state_flag[i])) ){
		#ifdef REPORT_DATA_ANDROID_4_0
			input_mt_slot(ts->input, i);
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		#endif
			id_sign[i]=0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}
	
	//printk("touches == %d\n",touches);
	
#ifndef REPORT_DATA_ANDROID_4_0
	if(0 == touches || 
	   (cfg_dts.use_click_mode==1 && longpress_enabled && datas[largest_id].key_count > 15 )){
		int cout = 0;
		int key_count = datas[largest_id].key_count;
		int *key_x = datas[largest_id].key_x;
		int *key_y = datas[largest_id].key_y;;
		int max_size_x = datas[largest_id].max_size_x;
		int min_size_x = datas[largest_id].min_size_x;
		int max_size_y = datas[largest_id].max_size_y;
		int min_size_y = datas[largest_id].min_size_y;
		int avarage_size_x = datas[largest_id].avarage_size_x;
		int avarage_size_y = datas[largest_id].avarage_size_y;
		int x_factor, y_factor;
		if (cfg_dts.XYSwap == 1) {
			x_factor = 1;
			y_factor = 2;
		}else{
			x_factor = 2;
			y_factor = 1;
		}
		//input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 0);
	    //input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 0);
		//input_mt_sync(ts->input);
		if(key_count<=0){
			return;
		}

		if(cfg_dts.use_click_mode==1){
			int key_id = -1;
			//int bound_x = (cfg_dts.xMax/4)-60;
			//int bound_y = (cfg_dts.yMax/4)-30;
			int bound_x = (KEY_SIZE_CLICK+25)*x_factor;
			int bound_y = (KEY_SIZE_CLICK+25)*y_factor;
			/*discard the last point,which is a fake point provided by closed source library*/
			int tmp = (key_count >= 2)?(key_count-1):key_count;
			
			/*
			printk("--x:%d ,y:%d ,key_count:%d, tmp:%d\n", (key_x[key_count -1] - key_x[0]), (key_y[key_count -1] - key_y[0]), key_count, tmp);
			printk("--x:%d ,y:%d ,key_count:%d, tmp:%d\n", (key_x[tmp -1] - key_x[0]), (key_y[tmp -1] - key_y[0]), key_count, tmp);
			printk("--x:%d ,y:%d ,key_count:%d, tmp:%d\n", (key_x[tmp/2] - key_x[0]), (key_y[tmp/2] - key_y[0]), key_count, tmp);
			*/
			if( (key_x[tmp -1] - key_x[0] < 80*x_factor)  && 
				(key_x[tmp -1] - key_x[0] >= -80*x_factor) && 
				(key_y[tmp -1] - key_y[0] < 80*y_factor) &&
				(key_y[tmp -1] - key_y[0] >= -80*y_factor) && 
				(key_x[tmp/2] - key_x[0] < 80*x_factor)  && 
				(key_x[tmp/2] - key_x[0] >= -80*x_factor) && 
				(key_y[tmp/2] - key_y[0] < 80*y_factor) &&
				(key_y[tmp/2] - key_y[0] >= -80*y_factor) && 
				(key_x[0] != 0) && (key_y[0] != 0))
			{
				avarage_size_x /= key_count;
				avarage_size_y /= key_count;
				avarage_size_x = avarage_size_x -(cfg_dts.xMax/2) + KEY_SIZE_XOFFSET;
				avarage_size_y = avarage_size_y -(cfg_dts.yMax/2) + KEY_SIZE_YOFFSET;
				printk("--point( %d , %d ) , KEY_SIZE_click:%d ,key_count:%d\n", avarage_size_x ,avarage_size_y, KEY_SIZE_CLICK,key_count);

				if(avarage_size_x >= bound_x && avarage_size_y >=-1*KEY_SIZE_CLICK*y_factor && avarage_size_y <=KEY_SIZE_CLICK*y_factor){
					key_id = 2;
					printk(" get up key by wubing\n");

				}else if (avarage_size_x <= ((-1)*bound_x) && avarage_size_y >=-1*KEY_SIZE_CLICK*y_factor && avarage_size_y <=KEY_SIZE_CLICK*y_factor){
					key_id = 3;
					printk(" get down key by wubing\n");

				}else if (avarage_size_y >= bound_y && avarage_size_x >=-1*KEY_SIZE_CLICK*x_factor && avarage_size_x <=KEY_SIZE_CLICK*x_factor){
					key_id = 1;
					printk(" get left key by wubing\n");

				}else if(avarage_size_y <= ((-1)*bound_y) && avarage_size_x >=-1*KEY_SIZE_CLICK*x_factor && avarage_size_x <=KEY_SIZE_CLICK*x_factor){
					key_id = 0;
					printk(" get right key by wubing\n");

				}else if(avarage_size_x >=-1*KEY_SIZE_CLICK*x_factor && avarage_size_x <= KEY_SIZE_CLICK*x_factor &&
						 avarage_size_y >=-1*KEY_SIZE_CLICK*y_factor && avarage_size_y <= KEY_SIZE_CLICK*y_factor){
					key_id = 4;
					printk(" get enter key by wubing\n");
				}
				
				if(key_id>=0 && key_id<= 4 && 0 == touches){
					printk(" send key :%d by wubing \n",key_array[key_id]);
					input_report_key(ts->input, key_array[key_id], 1);
					input_sync(ts->input);	
					input_report_key(ts->input, key_array[key_id], 0);
					input_sync(ts->input);
					
				}else if(key_id>=0 && key_id < 4 && 0 != touches){
					queue_work(gsl_timer_workqueue,&ts->click_work);///click_timer_worker
					printk(" send_key==%d \n",send_key);
					if(send_key)
					{
						printk(" send key :%d by wubing \n",key_array[key_id]);
						input_report_key(ts->input, key_array[key_id], 1);
						input_sync(ts->input);	
						input_report_key(ts->input, key_array[key_id], 0);
						input_sync(ts->input);
						send_key = false;						
					}else{
						send_key = true;
					}
				}
				
			}
			
			/*clear them for the next*/
			avarage_size_x = 0;
			avarage_size_y = 0;
			
		}else{
			cout = key_count <= 512 ? key_count : 512;
	/*		if(report_back_event(ts, cout,largest_id)){
				goto done;
			}*/

			if( key_count <= 512 )
			{
				if(((key_x[key_count  -1] - key_x[0]) > KEY_SIZE_X)&&((max_size_y - min_size_y)/y_factor < (key_x[key_count  -1] - key_x[0])/x_factor) )
					{
					    send_key = false;
						//keynum = 0;
						printk(" send up key by wubing \n");
						input_report_key(ts->input, key_array[2], 1);
						input_sync(ts->input);
						input_report_key(ts->input, key_array[2], 0);
						input_sync(ts->input);
				}
				else if(((key_x[0] - key_x[key_count-1]) > KEY_SIZE_X)&&((max_size_y - min_size_y)/y_factor < (key_x[0] - key_x[key_count-1])/x_factor))
				{
							send_key = false;
							//keynum = 0;
							printk(" send down key by wubing \n");
							input_report_key(ts->input, key_array[3], 1);
							input_sync(ts->input);
							input_report_key(ts->input, key_array[3], 0);
							input_sync(ts->input);
				}

						
				if(((key_y[key_count-1] - key_y[0]) > KEY_SIZE_Y)&&((max_size_x - min_size_x)/x_factor < (key_y[key_count-1] - key_y[0])/y_factor))
				{
							send_key = false;
							//keynum = 0;
							printk(" send left key by wubing \n");
							input_report_key(ts->input, key_array[1], 1);
							input_sync(ts->input);	
							input_report_key(ts->input, key_array[1], 0);
							input_sync(ts->input);									
				}else if(((key_y[0] - key_y[key_count-1]) > KEY_SIZE_Y)&&((max_size_x - min_size_x)/x_factor < (key_y[0] - key_y[key_count-1])/y_factor))
				{
							send_key = false;
							//keynum = 0;
							printk(" send right key by wubing \n");
							input_report_key(ts->input, key_array[0], 1);
							input_sync(ts->input);	
							input_report_key(ts->input, key_array[0], 0);
							input_sync(ts->input);	
				} 
				
				//printk(" key_x[key_count -1],  key_x[0], key_y[key_count -1], key_y[0] is %d ,%d , %d , %d , \n",key_x[key_count -1],key_x[0],key_y[key_count -1],key_y[0]);
				//printk(" key_x[key_count -1],  key_x[0], key_x[key_count -1]-key_x[0] is %d ,%d , %d \n",key_x[key_count -1],key_x[0],key_x[key_count -1]-key_x[0]);
				//printk(" key_y[key_count -1],  key_y[0], key_y[key_count -1]-key_y[0] is %d ,%d , %d \n",key_x[key_count -1],key_x[0],key_x[key_count -1]-key_x[0]);
				if( (key_x[key_count -1] - key_x[0] < 50*x_factor)  && 
					(key_x[key_count -1] - key_x[0] >= -50*x_factor) && 
					(key_y[key_count -1] - key_y[0] < 50*y_factor) &&
					(key_y[key_count -1] - key_y[0] >= -50*y_factor) && 
					(key_x[0] != 0) && (key_y[0] != 0))
	            //else if(max_size_x - min_size_x < ENTER_SIZE && max_size_y - min_size_y < ENTER_SIZE)
				{
							queue_work(gsl_timer_workqueue,&ts->click_work);
							printk(" send_key==%d \n",send_key);
							//if(key_count > 3 && send_key&&max_size_x - min_size_x < ENTER_SIZE && max_size_y - min_size_y < ENTER_SIZE)
							if(send_key)
							{
											printk(" send enter key by wubing \n");
											input_report_key(ts->input, key_array[4], 1);
											input_sync(ts->input);	
											input_report_key(ts->input, key_array[4], 0);
											input_sync(ts->input);
											send_key = false;
											//keynum = 0;
											
							}else
							{
											//down(&my_sem);
											send_key = true;
											//up(&my_sem);
							}
				}

							
			}
			else if(key_count > 512 ) {
					
							if((key_x[511] - key_x[0]) > KEY_SIZE_X)
							{
									
									send_key = false;
									//keynum = 0;
									input_report_key(ts->input, key_array[2], 1);
									input_sync(ts->input);
									input_report_key(ts->input, key_array[2], 0);
									input_sync(ts->input);	
							}
							else if((key_x[0] - key_x[511]) > KEY_SIZE_X)
							{
								
									send_key = false;
									//keynum = 0;
									input_report_key(ts->input, key_array[3], 1);
									input_sync(ts->input);
									input_report_key(ts->input, key_array[3], 0);
									input_sync(ts->input);
							}
						
							if((key_y[511] - key_y[0]) > KEY_SIZE_Y)
							{
									
									send_key = false;
									//keynum = 0;
									input_report_key(ts->input, key_array[1], 1);
									input_sync(ts->input);							
									input_report_key(ts->input, key_array[1], 0);
									input_sync(ts->input);				
							}else if((key_y[0] - key_y[511]) > KEY_SIZE_Y)
							{
									send_key = false;
									//keynum = 0;
									input_report_key(ts->input, key_array[0], 1);
									input_sync(ts->input);	
									input_report_key(ts->input, key_array[0], 0);
									input_sync(ts->input);	
							}						
			}
			
		}

done:
		if(recording_curve_data){
			spin_lock(&gsl_lock);
			curve_data.key_count = (key_count <= 511) ? key_count : 511;
			memcpy(curve_data.key_x, key_x, (sizeof(int))*key_count);
			memcpy(curve_data.key_y, key_y, (sizeof(int))*key_count);
			spin_unlock(&gsl_lock);
		}

		memset(key_y,0,sizeof(int)*512);
		memset(key_x,0,sizeof(int)*512);
		key_count = 0;
		memset(datas,0,sizeof(struct gslx680_data)*MAX_CONTACTS);
		largest_id =0;
					
		#ifdef HAVE_TOUCH_KEY
		if(key_state_flag){
			input_report_key(ts->input, key, 0);
			input_sync(ts->input);
			key_state_flag = 0;
		}
		#endif
				
	}
	
#endif


	input_sync(ts->input);
	ts->prev_touches = touches;
}


static void gsl_ts_xy_worker(struct work_struct *work)
{
	int rc;
	u8 read_buf[4] = {0};
	struct gsl_ts *ts = container_of(work, struct gsl_ts,work);
#ifndef GSL_TIMER
	disable_irq_nosync(cfg_dts.sirq);
#endif
	dprintk(DEBUG_X_Y_INFO,"---gsl_ts_xy_worker---\n");
#ifdef TPD_PROC_DEBUG
	if(gsl_proc_flag == 1){
		goto schedule;
	}
#endif
	/* read data from DATA_REG */
	rc = gsl_ts_read(ts->client, 0x80, ts->touch_data, ts->dd->data_size);
	dprintk(DEBUG_X_Y_INFO,"---touches: %d ---\n",ts->touch_data[0]); 
	if (rc < 0) {
		dev_err(&ts->client->dev, "read failed\n");
		goto schedule;
	}

	if (ts->touch_data[ts->dd->touch_index] == 0xff) {
		goto schedule;
	}

	rc = gsl_ts_read( ts->client, 0xbc, read_buf, sizeof(read_buf));
	if (rc < 0) {
		dev_err(&ts->client->dev, "read 0xbc failed\n");
		goto schedule;
	}
	dprintk(DEBUG_X_Y_INFO,"reg %x : %x %x %x %x\n",0xbc, read_buf[3], read_buf[2], read_buf[1], read_buf[0]); 
	if (read_buf[3] == 0 && read_buf[2] == 0 && read_buf[1] == 0 && read_buf[0] == 0){
		process_gslX680_data(ts);
	}
	else
	{
		printk("err!!! state is invalid\n");
		reset_chip(ts->client);
		startup_chip(ts->client);
	}
	
schedule:

#ifndef GSL_TIMER
   enable_irq(cfg_dts.sirq);
#endif
	return;
}



#ifdef HAVE_CLICK_TIMER

static void click_timer_worker(struct work_struct *work)
{
	while(true)
	{
		msleep(400);
		//down(&my_sem); 
		send_key = true;
		//up(&my_sem);
	}		
}

#endif


#ifdef GSL_MONITOR
static void gsl_monitor_worker(struct work_struct *work)
{
	char read_buf[4]  = {0};
	
	dprintk(DEBUG_OTHERS_INFO,"---------------gsl_monitor_worker-----------------\n");

	gsl_ts_read(glsX680_i2c, 0xb4, read_buf, 4);
	int_2nd[3] = int_1st[3];
	int_2nd[2] = int_1st[2];
	int_2nd[1] = int_1st[1];
	int_2nd[0] = int_1st[0];
	int_1st[3] = read_buf[3];
	int_1st[2] = read_buf[2];
	int_1st[1] = read_buf[1];
	int_1st[0] = read_buf[0];

	if (int_1st[3] == int_2nd[3] && int_1st[2] == int_2nd[2] &&int_1st[1] == int_2nd[1] && int_1st[0] == int_2nd[0]) 
	{
		printk("======int_1st: %x %x %x %x , int_2nd: %x %x %x %x ======\n",int_1st[3], int_1st[2], int_1st[1], int_1st[0], int_2nd[3], int_2nd[2],int_2nd[1],int_2nd[0]);
		init_chip(glsX680_i2c);
	}
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 300);
}
#endif


irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{
	//struct gsl_ts *ts = (struct gsl_ts *)dev_id;
	dprintk(DEBUG_INT_INFO,"==========GSLX680 Interrupt============\n");
	queue_work(ts_init->wq, &ts_init->work);
	
#ifdef GSL_TIMER
	mod_timer(&ts_init->gsl_timer, jiffies + msecs_to_jiffies(30));
#endif
	return IRQ_HANDLED;
}


#ifdef GSL_TIMER
static void gsl_timer_handle(unsigned long data)
{
	struct gsl_ts *ts = (struct gsl_ts *)data;

#ifdef GSL_DEBUG	
	printk("----------------gsl_timer_handle-----------------\n");	
#endif
	enable_irq(cfg_dts.sirq);

	check_mem_data(ts->client);
	ts->gsl_timer.expires = jiffies + 3 * HZ;
	add_timer(&ts->gsl_timer);
	//enable_irq(ts->irq);
	
}
#endif

static int gsl_ts_init_ts(struct i2c_client *client, struct gsl_ts *ts)
{
	struct input_dev *input_device;
	int  rc = 0;
#ifdef HAVE_TOUCH_KEY
	int i= 0;
#endif
	printk("[GSLX680] Enter %s\n", __func__);
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

	ts->prev_touches = 0;

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
	input_mt_init_slots(input_device, (MAX_CONTACTS+1));
#else
	input_set_abs_params(input_device,ABS_MT_TRACKING_ID, 0, (MAX_CONTACTS+1), 0, 0);
	set_bit(EV_ABS, input_device->evbit);
	set_bit(EV_KEY, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_device->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif
#ifdef HAVE_TOUCH_KEY
		//input_device->evbit[0] = BIT_MASK(EV_KEY);
	for (i = 0; i < MAX_KEY_NUM; i++)
		set_bit(key_array[i], input_device->keybit);

#endif

	set_bit(ABS_MT_POSITION_X, input_device->absbit);
	set_bit(ABS_MT_POSITION_Y, input_device->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

	input_set_abs_params(input_device,ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_device,ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_device,ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_device,ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		dev_err(&client->dev, "Could not create workqueue\n");
		goto error_wq_create;
	}
	flush_workqueue(ts->wq);

	INIT_WORK(&ts->work, gsl_ts_xy_worker);

	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	destroy_workqueue(ts->wq);
error_wq_create:
	input_free_device(input_device);
error_alloc_dev:
	kfree(ts->touch_data);
	return rc;
}

static void glsX680_resume_events (struct work_struct *work)
{
	gslX680_shutdown_high();
	msleep(10);
	reset_chip(glsX680_i2c);
	startup_chip(glsX680_i2c);
	check_mem_data(glsX680_i2c);
#ifndef GSL_TIMER
	enable_irq(cfg_dts.sirq);
#endif
}

#ifdef CONFIG_PM
static int gsl_ts_suspend(struct device *dev)
{
        struct gsl_ts *ts = dev_get_drvdata(dev);
        printk("%s,start\n",__func__);
        cancel_work_sync(&glsX680_resume_work);
        flush_workqueue(gslX680_resume_wq);

#ifndef CONFIG_HAS_EARLYSUSPEND
        ts->is_suspended = true;
#endif

#ifdef HAVE_CLICK_TIMER
	//cancel_work_sync(&ts->click_work);
#endif


#ifdef GSL_TIMER
	dprintk(DEBUG_SUSPEND,"gsl_ts_suspend () : delete gsl_timer\n");
	del_timer(&ts->gsl_timer);
#endif
        if(ts->is_suspended == true ){
#ifndef GSL_TIMER
                disable_irq_nosync(cfg_dts.sirq);
#endif
                flush_workqueue(gslX680_resume_wq);
                cancel_work_sync(&ts->work);
                flush_workqueue(ts->wq);
                gslX680_shutdown_low(); 
        }
        printk("%s,end\n",__func__);
        return 0;

}

static int gsl_ts_resume(struct device *dev)
{
	
	struct gsl_ts *ts = dev_get_drvdata(dev);

	ts->is_suspended = true;
  	printk("%s,start\n",__func__);
  	cancel_work_sync(&ts->work);
	flush_workqueue(ts->wq);
	queue_work(gslX680_resume_wq, &glsX680_resume_work);
#ifdef HAVE_CLICK_TIMER
		//queue_work(gsl_timer_workqueue,&ts->click_work);
#endif

#ifdef GSL_TIMER
	dprintk(DEBUG_SUSPEND, "gsl_ts_resume () : add gsl_timer\n");
	init_timer(&ts->gsl_timer);
	ts->gsl_timer.expires = jiffies + 3 * HZ;
	ts->gsl_timer.function = &gsl_timer_handle;
	ts->gsl_timer.data = (unsigned long)ts;
	add_timer(&ts->gsl_timer);
#endif
	printk("%s,end\n",__func__);
	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void gsl_ts_early_suspend(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
#ifndef GSL_TIMER
	int ret = 0;
#endif
	dprintk(DEBUG_SUSPEND,"[GSL1680] Enter %s\n", __func__);
#ifdef TPD_PROC_DEBUG
		if(gsl_proc_flag == 1){
			return;
		}
#endif
#ifdef GSL_MONITOR
	cancel_delayed_work_sync(&gsl_monitor_work);
	flush_workqueue(gsl_monitor_workqueue);
#endif
	cancel_work_sync(&glsX680_resume_work);
	flush_workqueue(gslX680_resume_wq);
        ts->is_suspended = false;
#ifndef GSL_TIMER
    disable_irq_nosync(cfg_dts.sirq);
	if (ret < 0)
		dprintk(DEBUG_SUSPEND,"%s irq disable failed\n", __func__);
#endif

	cancel_work_sync(&ts->work);
	flush_workqueue(ts->wq);
	gslX680_shutdown_low();
	
#ifdef SLEEP_CLEAR_POINT
		msleep(10); 		
	#ifdef REPORT_DATA_ANDROID_4_0
		for(i = 1; i <= MAX_CONTACTS ;i ++)
		{	
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

}

static void gsl_ts_late_resume(struct early_suspend *h)
{
	struct gsl_ts *ts = container_of(h, struct gsl_ts, early_suspend);
#ifndef GSL_TIMER
		int ret = 0;
#endif
        dprintk(DEBUG_SUSPEND,"[GSL1680] Enter %s\n", __func__);
#ifdef TPD_PROC_DEBUG
		if(gsl_proc_flag == 1){
			return;
		}
#endif
	cancel_work_sync(&ts->work);
	flush_workqueue(ts->wq);
	
#ifndef CONFIG_PM
        gsl_ts_resume(ts->client);
#else
        if(ts->is_suspended == false){
                 gslX680_shutdown_high();
	        msleep(10);
	        reset_chip(glsX680_i2c);
	        startup_chip(glsX680_i2c);
#ifndef GSL_TIMER
	        enable_irq(cfg_dts.sirq);
#endif
        }
#endif
#ifdef GSL_MONITOR
	printk( "gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 300);
#endif

}
#endif

static void glsX680_init_events (struct work_struct *work)
{
	int ret = 0;

	gslX680_chip_init();
	ret = init_chip(glsX680_i2c);
	if(ret<0){
		printk( "err!!! init_chip failed\n");
		return;
	}
	check_mem_data(glsX680_i2c);

#ifndef GSL_TIMER
	ret = request_irq(cfg_dts.sirq, gsl_ts_irq, CTP_IRQ_MODE, GSLX680_I2C_NAME, ts_init);
	if (ret) {
		printk( "glsX680_init_events: request irq failed\n");
	}
#else
	printk( "add gsl_timer\n");
	init_timer(&ts_init->gsl_timer);
	ts_init->gsl_timer.expires = jiffies + msecs_to_jiffies(500);
	ts_init->gsl_timer.function = &gsl_ts_irq;
	ts_init->gsl_timer.data = (unsigned long)ts_init;
	add_timer(&ts_init->gsl_timer);
#endif

	return;
}

static int gsl_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct gsl_ts *ts;
	int rc = 0;

	printk("GSLX680 Enter %s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts){
	        printk("allocate data fail!\n");
		return -ENOMEM;
	}

    gslX680_wq = create_singlethread_workqueue("gslX680_init");
	if (gslX680_wq == NULL) {
		printk("create gslX680_wq fail!\n");
		return -ENOMEM;
	}

	gslX680_resume_wq = create_singlethread_workqueue("gslX680_resume");
	if (gslX680_resume_wq == NULL) {
		printk("create gslX680_resume_wq fail!\n");
		return -ENOMEM;
	}

    glsX680_i2c = client;
	ts->client = client;
	i2c_set_clientdata(client, ts);
	ts->device_id = id->driver_data;

	ts->is_suspended = false;
	ts->int_pending = false;
	mutex_init(&ts->sus_lock);

	rc = gsl_ts_init_ts(client, ts);
	if (rc < 0) {
		dev_err(&client->dev, "GSLX680 init failed\n");
		goto error_mutex_destroy;
	}
	ts_init = ts;
	queue_work(gslX680_wq, &glsX680_init_work);

        //device_create_file(&ts->input->dev, &dev_attr_debug_reg);
	rc = device_create_bin_file(&ts->input->dev, &gslx680);
	if (rc < 0) {
		printk("can't create gslx680 file? %d\n", rc);
	}
	spin_lock_init(&gsl_lock);
	device_enable_async_suspend(&client->dev);

#ifdef TPD_PROC_DEBUG
	gsl_config_proc = create_proc_entry(GSL_CONFIG_PROC_FILE, 0666, NULL);
	if (gsl_config_proc == NULL)
	{
		printk("create_proc_entry %s failed\n", GSL_CONFIG_PROC_FILE);
	}
	else
	{
		gsl_config_proc->read_proc = gsl_config_read_proc;
		gsl_config_proc->write_proc = gsl_config_write_proc;
	}
	gsl_proc_flag = 0;
#endif


#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	ts->early_suspend.suspend = gsl_ts_early_suspend;
	ts->early_suspend.resume = gsl_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

#ifdef GSL_MONITOR
	printk( "gsl_ts_probe () : queue gsl_monitor_workqueue\n");

	INIT_DELAYED_WORK(&gsl_monitor_work, gsl_monitor_worker);
	gsl_monitor_workqueue = create_singlethread_workqueue("gsl_monitor_workqueue");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 1000);
#endif

#ifdef HAVE_CLICK_TIMER

	sema_init(&my_sem, 1);
	INIT_WORK(&ts->click_work,click_timer_worker);
	gsl_timer_workqueue = create_singlethread_workqueue("click_timer");
	//queue_work(gsl_timer_workqueue,&ts->click_work);
#endif
	
	dprintk(DEBUG_INIT,"[GSLX680] End %s\n", __func__);
	return 0;

error_mutex_destroy:
	mutex_destroy(&ts->sus_lock);
	input_free_device(ts->input);
	kfree(ts);
	return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);
	printk("==gsl_ts_remove=\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	//device_remove_file(&ts->input->dev, &dev_attr_debug_reg);
#ifdef GSL_MONITOR
	cancel_delayed_work_sync(&gsl_monitor_work);
	destroy_workqueue(gsl_monitor_workqueue);
#endif

#ifdef HAVE_CLICK_TIMER
	cancel_work_sync(&ts->click_work);
	destroy_workqueue(gsl_timer_workqueue);
#endif
	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	cancel_work_sync(&glsX680_init_work);
	cancel_work_sync(&glsX680_resume_work);

#ifndef GSL_TIMER

	free_irq(cfg_dts.sirq, ts);

#else
		del_timer(&ts->gsl_timer);
#endif
	destroy_workqueue(ts->wq);
	destroy_workqueue(gslX680_wq);
	destroy_workqueue(gslX680_resume_wq);
	input_unregister_device(ts->input);
	mutex_destroy(&ts->sus_lock);
	kfree(ts->touch_data);
	kfree(ts);

	return 0;
}

static const struct i2c_device_id gsl_ts_id[] = {
	{GSLX680_I2C_NAME, 0},
	{}
};

static const struct of_device_id gsl_of_match[] = {
	{	"gslX680"},
	{}
};


static struct dev_pm_ops tp_pm_ops = {
	//.resume_early = tp_resume_early,
	.suspend = gsl_ts_suspend,
	.resume = gsl_ts_resume,
};

MODULE_DEVICE_TABLE(i2c, gsl_ts_id);
MODULE_DEVICE_TABLE(of, gsl_of_match);

static struct i2c_driver gsl_ts_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = GSLX680_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(gsl_of_match),
		.pm = &tp_pm_ops,
	},
	.probe		= gsl_ts_probe,
	.remove		= gsl_ts_remove,
	.id_table		= gsl_ts_id,
	.address_list	= normal_i2c,
	//.detect   = ctp_detect,
};


static int tp_of_data_get(void)
{
	struct device_node *of_node;
	enum of_gpio_flags flags;
	unsigned int scope[2];
	int ret = -1;
	unsigned int ok_keycode = 0;
	
	of_node = of_find_compatible_node(NULL, NULL, "gslX680");
	if (of_node == NULL) {
		pr_err("%s,%d,find the gslX680 dts err!\n", __func__, __LINE__);
		return -1;
	}

	/* load  regulator */
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

	
	/* load irq num  */
	
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

 
	/* load i2c info */

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
		cfg_dts.xMax = 1920;
	}

	ret = of_property_read_u32(of_node, "y_pixel", &cfg_dts.yMax);
	if (ret) {
		pr_err("failed to get y_pixel\r\n,set default:2048");
		cfg_dts.yMax = 1080;
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
	/*This is needed, if XYSwap is happened*/
	if (cfg_dts.XYSwap == 1) {
		int tmp;
		tmp = cfg_dts.xMax;
		cfg_dts.xMax = cfg_dts.yMax;
		cfg_dts.yMax = tmp;
		printk("----xymax swap----\n");
	}

	ret = of_property_read_u32(of_node, "rotate_degree", &cfg_dts.rotate);
	if (ret) {
		pr_err("failed to get rotate, set default:0\r\n");
		cfg_dts.rotate = 0;
	}

	ret = of_property_read_u32(of_node, "use_click_mode", &cfg_dts.use_click_mode);
	if (ret) {
		pr_err("failed to get use_click_mode");
		cfg_dts.use_click_mode = 0;
	}
	
	ret = of_property_read_u32(of_node, "ok_keycode", &ok_keycode);
	if (ret==0 && key_array[4]!= ok_keycode){
		key_array[4] = ok_keycode;
		printk("ok keycode is overrided with %d\n",ok_keycode);
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

	int ret = -1;
	fwname ="gslX680_VR"; //config_info.name;
	dprintk(DEBUG_INIT,"%s:fwname:%s\n",__func__,fwname);
	fw_index = gsl_find_fw_idx(fwname);
	printk("fw_index = %d, fwname=%s\n",fw_index, fwname);

	tp_of_data_get();
	tp_regulator = regulator_init(cfg_dts.regulator,
			cfg_dts.vol_min, cfg_dts.vol_max);
	if (!tp_regulator) {
		pr_err("Nova tp init power failed");
		ret = -EINVAL;
		return ret;
	}

	gpio_request(gpio_reset, GSLX680_I2C_NAME);

	ret = i2c_add_driver(&gsl_ts_driver);
	return ret;
}
static void __exit gsl_ts_exit(void)
{
	printk("==gsl_ts_exit==\n");
	i2c_del_driver(&gsl_ts_driver);
	return;
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);
module_param_named(debug_mask,debug_mask,int,S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680 touchscreen controller driver");
MODULE_AUTHOR("Guan Yuwei, guanyuwei@basewin.com");
MODULE_ALIAS("platform:gsl_ts");

