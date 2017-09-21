/**
 * Sensortek STK3310/STK3311 Ambient Light and Proximity Sensor
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for STK3310/STK3311. 7-bit I2C address: 0x48.
 */

#include <linux/bitops.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>

#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/input.h>
#include <linux/pm_runtime.h>

//Reg define.
#define STK3311_REG_STATE                       0x00
#define STK3311_REG_PSCTRL                      0x01
#define STK3311_REG_ALSCTRL                     0x02
#define STK3311_REG_LEDCTRL                     0x03
#define STK3311_REG_INT                         0x04
#define STK3311_REG_WAIT                        0x05
#define STK3311_REG_THDH1_PS                    0x06
#define STK3311_REG_THDH2_PS                    0x07
#define STK3311_REG_THDL1_PS                    0x08
#define STK3311_REG_THDL2_PS                    0x09
#define STK3311_REG_FLAG                        0x10
#define STK3311_REG_PS_DATA_MSB                 0x11
#define STK3311_REG_PS_DATA_LSB                 0x12
#define STK3311_REG_ALS_DATA_MSB                0x13
#define STK3311_REG_ALS_DATA_LSB                0x14
#define STK3311_REG_ID                          0x3E

#define STK3311_FLAG_NF                         0x01

//Status define
#define STK3311_STATE_STANDBY                   0x00
#define STK3311_STATE_PSEN                      0x01
#define STK3311_INT_DISABLE                     0x00
#define STK3311_FLAG_MODE                       0x03
#define STK3311_INTERRUPT_MODE                  0x01

//Chipid define
#define STK3310_CHIP_ID_VAL                     0x13
#define STK3311_CHIP_ID_VAL                     0x12

#define STK3311_DRIVER_NAME                     "stk3311"
#define STK3311_EVENT                           "stk3311_irq"

#define DEFAULT_NEAR_HOLD                       400
#define DEFAULT_FAR_HOLD                        100

//#define STK3311_DEBUG

static unsigned int near_hold;
static unsigned int far_hold;
static bool is_resuming;
static bool is_irqpermit;
static bool is_wakeup;

struct regval_list {
	unsigned int reg_num;
	unsigned int value;
};

#define ENDMARKER       { 0xff, 0xff }

struct stk3311_data {
    struct i2c_client *client;
	struct input_dev *idev;
    struct mutex lock;
};

//Judge backlight is on/off.
extern int owl_backlight_is_on(void);

static inline int reg_read(struct i2c_client *client, unsigned int reg)
{
    unsigned int val;

    val = i2c_smbus_read_byte_data(client, reg);
    if (val < 0)
        pr_err("failed to read 0x%x!\n", reg);

    return val;
}

static inline int reg_write(struct i2c_client *client, unsigned int reg, unsigned int val)
{
    int ret;

    ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0)
        pr_err("failed to write 0x%x!\n", reg);

    return ret;
}

static int reg_write_array(struct i2c_client *client, const struct regval_list *vals)
{
	while (vals->reg_num != 0xff) {
        int ret = reg_write(client, vals->reg_num, vals->value);
        if (ret < 0) {
            pr_err("failed to write 0x%x!\n", vals->reg_num);
            return ret;
        }
        vals++;
	}

	return 0;
}

static int of_data_get(void)
{
	struct device_node *of_node;
	int ret = -1;

	of_node = of_find_compatible_node(NULL, NULL, "stk,stk3311");
	if (of_node == NULL) {
		pr_err("%s,%d,get lightsensor compatible fail!\n", __func__, __LINE__);
		return ret;
	}

    ret = of_property_read_u32(of_node, "near_threshold", &near_hold);
    ret |= of_property_read_u32(of_node, "far_threshold", &far_hold);
    if (ret != 0)
    {
        pr_err("Can't get near and far threshold in dts!Init as default.\n");
        near_hold = DEFAULT_NEAR_HOLD;
        far_hold = DEFAULT_FAR_HOLD;
        ret = 0;
    }

	return ret;
}

//init_reg
static const struct regval_list init_regs[] = {
    //disable all
	{STK3311_REG_STATE, 0x00},
    //set psctrl,alsctrl,ledctrl
	{STK3311_REG_PSCTRL, 0x31},
	{STK3311_REG_ALSCTRL, 0x39},
    //100ma--0xff;50ma--0xbf
	//{STK3311_REG_LEDCTRL, 0xbf},
	{STK3311_REG_LEDCTRL, 0xff},
    //int disable
	{STK3311_REG_INT, 0x00},
    //enable
    {STK3311_REG_STATE, 0x1},
    {STK3311_REG_INT, 0x01},

	ENDMARKER,
};

static int stk3311_init_config(struct i2c_client *client)
{
    int ret;
    int chipid;

    chipid = reg_read(client, STK3311_REG_ID);
    if (chipid < 0)
        return chipid;
    
    if (chipid != STK3310_CHIP_ID_VAL &&
        chipid != STK3311_CHIP_ID_VAL) {
        pr_err("invalid chip id: 0x%x\n", chipid);
        return -ENODEV;
    }

    ret = reg_write_array(client, init_regs);
    ret |= reg_write(client, STK3311_REG_THDH1_PS, (near_hold & 0xFF00) >> 8);
    ret |= reg_write(client, STK3311_REG_THDH2_PS, near_hold & 0x00FF);
    ret |= reg_write(client, STK3311_REG_THDL1_PS, (far_hold & 0xFF00) >> 8);
    ret |= reg_write(client, STK3311_REG_THDL2_PS, far_hold & 0x00FF);

    return ret;
}

static inline void input_events(struct input_dev *dev, unsigned int code)
{
    //Button down
    input_report_key(dev, code, 1); 
    //Button up
    input_report_key(dev, code, 0); 
    input_sync(dev);
}

static irqreturn_t stk3311_irq_handler(int irq, void *private)
{
    return IRQ_WAKE_THREAD;
}

static irqreturn_t stk3311_irq_event_handler(int irq, void *private)
{
    int value = 0;
	struct i2c_client *client = private;
	struct stk3311_data *data = i2c_get_clientdata(client);
    
    mutex_lock(&data->lock);

    if (!is_irqpermit) {
        pr_err("Get irq event before stk3311 resume, ignor it. \
                Backlight state is %d.\n", owl_backlight_is_on());
        goto finish;
    }

#ifdef STK3311_DEBUG
    //print all reg
    for (int i = 0; i < 0x81; i++) {
        if ((i >=0x19 && i < 0x3e) || i == 0xe || i == 0xf || 
                (i >= 0x3f && i <0x80))
            continue;
        value = reg_read(data->client, i);
		pr_err("%#2x: %#2x\n", i, value);
    }
#endif

    value = reg_read(data->client, STK3311_REG_FLAG);

    if (is_resuming && owl_backlight_is_on() <= 0) {
        pr_info("Get irq event while resuming. Backlight state is %d.\n", 
                owl_backlight_is_on());
        if (value & STK3311_FLAG_NF) {
            if (is_wakeup) {
                pr_info("stk3311: Get faraway event.\n");
                input_events(data->idev, KEY_POWER);
                is_wakeup = false;
            }
        }
        goto finish;
    }

    is_resuming = false;
    if (value & STK3311_FLAG_NF) {
       if (owl_backlight_is_on() > 0 || is_wakeup) {
           pr_info("stk3311: Get faraway event.\n");
           input_events(data->idev, KEY_POWER);
           is_wakeup = false;
       }
    } else {
       if (owl_backlight_is_on() <= 0 || !is_wakeup) {
           pr_info("stk3311: Get nearto event.\n");
           input_events(data->idev, KEY_WAKEUP);
           is_wakeup = true;
       }
    }

finish:
    //Clear the interrupt flag
    value = reg_write(data->client, STK3311_REG_FLAG, value & STK3311_FLAG_NF);
    if (value < 0)
        pr_err("failed to reset interrupts\n");

    pr_info("stk3311 interrupt finish...\n");
    mutex_unlock(&data->lock);
    
    return IRQ_HANDLED;
}

static int stk3311_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    int ret;
	struct input_dev *dev;
    struct stk3311_data *data;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c_check_functionality error\n");
		return -EIO;
	}

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	dev = devm_input_allocate_device(&client->dev);
	if (!data || !dev) {
		pr_err("failed to allocate driver data\n");
		ret = -ENOMEM;
		goto err_free_mem;
	}

	dev->id.bustype = BUS_I2C;
	dev->dev.parent = &client->dev;
	dev->name = STK3311_DRIVER_NAME;
	dev->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_WAKEUP, dev->keybit);
	set_bit(KEY_POWER, dev->keybit);
	data->idev = dev;
	data->client = client;
	i2c_set_clientdata(client, data);
    of_data_get();
    mutex_init(&data->lock);
    
    ret = stk3311_init_config(client);
    if (ret < 0) {
        pr_err("stk3311 init config failed! \n");
        return ret;
    }
    
	pm_runtime_set_active(&client->dev);

    is_resuming = false;
    is_irqpermit = true;
    is_wakeup = true;
    if (client->irq >= 0) {
        ret = devm_request_threaded_irq(&client->dev, client->irq,
                                        stk3311_irq_handler,
                                        stk3311_irq_event_handler,
                                        IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                        STK3311_EVENT, client);
        if (ret < 0) {
            pr_err("request irq %d failed\n", client->irq);
            goto err_standby;
        }
    }
    
	ret = input_register_device(dev);
	if (ret) {
		pr_err("failed to register input device\n");
		goto err_free_irq;
	}

	pm_runtime_enable(&client->dev);
    
    return 0;
    
err_standby:
    reg_write(data->client, STK3311_REG_STATE, STK3311_STATE_STANDBY);
    return ret;
err_free_irq:
	free_irq(client->irq, data);
err_free_mem:
	input_free_device(dev);

	return ret;
}

static int stk3311_remove(struct i2c_client *client)
{
	struct stk3311_data *data = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	free_irq(client->irq, data);
	input_unregister_device(data->idev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int stk3311_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stk3311_data *data = i2c_get_clientdata(client);

    mutex_lock(&data->lock);
    //disable interrupt, chage state to stanby.
    reg_write(data->client, STK3311_REG_STATE, STK3311_STATE_STANDBY);
    reg_write(data->client, STK3311_REG_INT, STK3311_INT_DISABLE);

    //enable flag mode, chage ps state enable.
    reg_write(data->client, STK3311_REG_STATE, STK3311_STATE_PSEN);
    reg_write(data->client, STK3311_REG_INT, STK3311_FLAG_MODE);
    is_resuming = false;
    is_irqpermit = false;
    is_wakeup = false;
    pr_info("stk3311 suspend finish...\n");
    mutex_unlock(&data->lock);

	return 0;
}

static int stk3311_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stk3311_data *data = i2c_get_clientdata(client);

    mutex_lock(&data->lock);
    //disable interrupt, chage state to stanby.
    reg_write(data->client, STK3311_REG_STATE, STK3311_STATE_STANDBY);
    reg_write(data->client, STK3311_REG_INT, STK3311_INT_DISABLE);

    //enable interrupt mode, chage ps state enable.
    reg_write(data->client, STK3311_REG_STATE, STK3311_STATE_PSEN);
    reg_write(data->client, STK3311_REG_INT, STK3311_INTERRUPT_MODE);
    is_resuming = true;
    is_irqpermit = true;
    is_wakeup = true;
    pr_info("stk3311 resume finish...\n");
    mutex_unlock(&data->lock);

	return 0;
}

static SIMPLE_DEV_PM_OPS(stk3311_pm_ops, stk3311_suspend, stk3311_resume);

#define STK3311_PM_OPS (&stk3311_pm_ops)
#else
#define STK3311_PM_OPS NULL
#endif

static const struct i2c_device_id stk3311_i2c_id[] = {
    {"stk3310", 0},
    {"stk3311", 1},
    {}
};
MODULE_DEVICE_TABLE(i2c, stk3311_i2c_id);

static const struct of_device_id stk_of_match[] = {
	{ .compatible = "stk,stk3311", },
	{ },
};
MODULE_DEVICE_TABLE(of, stk_of_match);

static struct i2c_driver stk3311_driver = {
    .probe =            stk3311_probe,
    .remove =           stk3311_remove,
    .id_table =         stk3311_i2c_id,
    .driver = {
            .name = STK3311_DRIVER_NAME,
            .owner  = THIS_MODULE,
            .pm = STK3311_PM_OPS,
            .of_match_table = stk_of_match,
    },
};

module_i2c_driver(stk3311_driver);

MODULE_AUTHOR("Yiguang <liuyiguang@actions-semi.com>");
MODULE_DESCRIPTION("STK3311 Ambient Light and Proximity Sensor driver");
MODULE_LICENSE("GPL v2");
