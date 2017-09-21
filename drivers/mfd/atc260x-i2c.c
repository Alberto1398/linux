#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/mfd/atc260x/atc260x.h>

#include "atc260x-core.h"

/*----------------------------------------------------------------------------*/
/* code for direct access */
static u32 board_flag = 0;
static void _atc260x_i2c_direct_access_init(struct atc260x_dev *atc260x)
{
}

static void _atc260x_i2c_direct_access_exit(struct atc260x_dev *atc260x)
{
}

static int _atc260x_i2c_direct_read_reg(struct atc260x_dev *atc260x, uint reg)
{
	return 0;
}

static int _atc260x_i2c_direct_write_reg(struct atc260x_dev *atc260x, uint reg,
					 u16 val)
{
	return 0;
}

/*----------------------------------------------------------------------------*/

static struct regmap_config atc2603c_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	/* TODO : add wr_table rd_table volatile_table precious_table */

	.cache_type = REGCACHE_NONE,	/* TODO : i2c reg_rw need a fast cache, REGCACHE_FLAT  */
	.max_register = ATC2603C_CHIP_VER,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static struct regmap_config atc2609a_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	/* TODO : add wr_table rd_table volatile_table precious_table */

	.cache_type = REGCACHE_NONE,	/* TODO : i2c reg_rw need a fast cache, REGCACHE_FLAT  */
	.max_register = ATC2609A_CHIP_VER,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static const struct of_device_id atc260x_i2c_of_match[] = {
	{.compatible = "actions,atc2603c", .data = (void *)ATC260X_ICTYPE_2603C},
	{.compatible = "actions,atc2609a", .data = (void *)ATC260X_ICTYPE_2609A},
	{},
};

MODULE_DEVICE_TABLE(of, atc260x_i2c_of_match);

static int atc260x_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id_unused)
{
	struct atc260x_dev *atc260x;
	struct regmap_config *p_regmap_cfg;
	const struct of_device_id *of_id;
	int ret;

	dev_info(&i2c->dev, "Probing...\n");

	ret = i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C);
	if (!ret) {
		dev_err(&i2c->dev, "I2C bus not functional\n");
		return -EFAULT;
	}

	atc260x = devm_kzalloc(&i2c->dev, sizeof(*atc260x), GFP_KERNEL);
	if (atc260x == NULL)
		return -ENOMEM;

	of_id = of_match_device(atc260x_i2c_of_match, &i2c->dev);	/* match again, get of_id */
	if (!of_id) {
		dev_err(&i2c->dev,
			"of_match failed, unable to get device data\n");
		return -ENODEV;
	}
	atc260x->ic_type = (ulong) of_id->data;

	i2c_set_clientdata(i2c, atc260x);
	atc260x->dev = &i2c->dev;	/* 不要创建新设备, 复用i2c的从设备即可. */
	atc260x->irq = i2c->irq;

	/* init direct-access functions */
	atc260x->bus_num = (typeof(atc260x->bus_num)) (i2c->adapter->nr);
	atc260x->bus_addr = i2c->addr;
	atc260x->direct_read_reg = _atc260x_i2c_direct_read_reg;
	atc260x->direct_write_reg = _atc260x_i2c_direct_write_reg;
	atc260x->direct_acc_init = _atc260x_i2c_direct_access_init;
	atc260x->direct_acc_exit = _atc260x_i2c_direct_access_exit;

	/* register regmap */
	switch (atc260x->ic_type) {
	case ATC260X_ICTYPE_2603C:
		p_regmap_cfg = &atc2603c_i2c_regmap_config;
		break;
	case ATC260X_ICTYPE_2609A:
		p_regmap_cfg = &atc2609a_i2c_regmap_config;
		break;
	default:
		BUG();
	}
	atc260x->regmap = devm_regmap_init_i2c(i2c, p_regmap_cfg);
	if (IS_ERR(atc260x->regmap)) {
		ret = PTR_ERR(atc260x->regmap);
		dev_err(atc260x->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}
	of_property_read_u32(atc260x->dev->of_node, "board_flag", &board_flag);
	printk(KERN_WARNING "board_flag = %d\n", board_flag);
	ret = atc260x_core_dev_init(atc260x);

	return ret;
}

static int atc260x_i2c_remove(struct i2c_client *i2c)
{
	struct atc260x_dev *atc260x = i2c_get_clientdata(i2c);
	atc260x_core_dev_exit(atc260x);
	return 0;
}

static int atc260x_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct atc260x_dev *atc260x = i2c_get_clientdata(client);
	return atc260x_core_dev_suspend(atc260x);
}

static int atc260x_i2c_suspend_late(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct atc260x_dev *atc260x = i2c_get_clientdata(client);
	return atc260x_core_dev_suspend_late(atc260x);
}

static int atc260x_i2c_resume_early(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct atc260x_dev *atc260x = i2c_get_clientdata(client);
	return atc260x_core_dev_resume_early(atc260x);
}

static int atc260x_i2c_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct atc260x_dev *atc260x = i2c_get_clientdata(client);
	return atc260x_core_dev_resume(atc260x);
}

static const struct dev_pm_ops s_atc260x_i2c_pm_ops = {
	.suspend = atc260x_i2c_suspend,
	.suspend_late = atc260x_i2c_suspend_late,
	.resume_early = atc260x_i2c_resume_early,
	.resume = atc260x_i2c_resume,
	.freeze = atc260x_i2c_suspend,
	.freeze_late = atc260x_i2c_suspend_late,
	.thaw_early = atc260x_i2c_resume_early,
	.thaw = atc260x_i2c_resume,
	.poweroff = atc260x_i2c_suspend,
	.poweroff_late = atc260x_i2c_suspend_late,
	.restore_early = atc260x_i2c_resume_early,
	.restore = atc260x_i2c_resume,
};

static const struct i2c_device_id atc260x_i2c_id_tbl[] = {
	{"atc2603a", 0},
	{"atc2603c", 0},
	{"atc2609a", 0},
	{},
};

static int atc260x_i2c_shutdown(struct i2c_client *i2c_client)
{
	struct atc260x_dev *atc260x = i2c_get_clientdata(i2c_client);
	if(board_flag) {
		printk(KERN_WARNING "gt7_ebox!\n");
		/*atc260x_set_bits(atc260x, ATC2603C_PMU_LDO6_CTL, 0x1F << 11, 0x14);*/
		/*atc260x_set_bits(atc260x, ATC2603C_PMU_DC1_CTL0, 0x1F << 7, 0x14);*/
	}
	return 0;
}


MODULE_DEVICE_TABLE(i2c, atc260x_i2c_id_tbl);

static struct i2c_driver atc260x_i2c_driver = {
	.probe = atc260x_i2c_probe,
	.remove = atc260x_i2c_remove,
	.driver = {
		   .name = "atc260x_i2c",
		   .owner = THIS_MODULE,
		   .pm = &s_atc260x_i2c_pm_ops,
		   .of_match_table = of_match_ptr(atc260x_i2c_of_match),
		   },
	.id_table = atc260x_i2c_id_tbl,
	.shutdown = atc260x_i2c_shutdown,
};

static int __init atc260x_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&atc260x_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register atc260x I2C driver: %d\n", ret);
	return ret;
}

subsys_initcall(atc260x_i2c_init);
				    /*module_init(atc260x_i2c_init); *//* for debug */

static void __exit atc260x_i2c_exit(void)
{
	i2c_del_driver(&atc260x_i2c_driver);
}

module_exit(atc260x_i2c_exit);

MODULE_DESCRIPTION("I2C support for atc260x PMICs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Actions Semi.");
