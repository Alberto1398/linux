/*
 * eta355c-regulator.c
 *
 * Support for Intersil eta355c voltage regulator
 *
 * Copyright (C) 2010 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/of_regulator.h>

/* ETA DCDC ChpID */
#define ETA_CHIP_ID_ETA355C		0x9c
#define ETA_CHIP_ID_ETA3555		0x98

/* ETA355C Registers */
#define ETA355C_REG_VSEL0		0x0
#define ETA355C_REG_CHIPID		0x3

#define	ETA355C_VOLTAGE_STEP_COUNT	64
#define	ETA355C_VOLTAGE_MIN		603000
#define	ETA355C_VOLTAGE_STEP		12750

#define	ETA3555_VOLTAGE_MIN		600000
#define	ETA3555_VOLTAGE_STEP		10000

struct eta355c_regulator_config {
	const char *supply_name;
	const char *input_supply;

	/* Voltage range and step(linear) */
	unsigned int vsel_min;
	unsigned int vsel_step;

	int gpio;
	unsigned startup_delay;
	unsigned enable_high:1;
	unsigned enabled_at_boot:1;
	struct regulator_init_data *init_data;
};

/* PMIC details */
struct eta355c_regulator {
	struct regulator_desc	desc;
	struct regulator_dev	*rdev;
	struct i2c_client	*client;
	struct mutex		mtx;
	int			chip_id;
};


static int eta355c_get_voltage_sel(struct regulator_dev *dev)
{
	struct eta355c_regulator *eta = rdev_get_drvdata(dev);
	int val;

	mutex_lock(&eta->mtx);

	val = i2c_smbus_read_byte_data(eta->client, ETA355C_REG_VSEL0);

	dev_dbg(&eta->client->dev, "%s: val 0x%x\n", __func__, val);

	if (val < 0)
		dev_err(&eta->client->dev, "Error getting voltage\n");
	else
		val &= 0x3f;

	mutex_unlock(&eta->mtx);
	return val;
}

static int eta355c_set_voltage_sel(struct regulator_dev *dev,
				    unsigned selector)
{
	struct eta355c_regulator *eta = rdev_get_drvdata(dev);
	int err, val;

	dev_dbg(&eta->client->dev, "%s: selector 0x%x\n", __func__, selector);

	mutex_lock(&eta->mtx);

	selector &= 0x3f;

	val = i2c_smbus_read_byte_data(eta->client, ETA355C_REG_VSEL0);
	if (val < 0) {
		dev_err(&eta->client->dev, "Error getting voltage\n");
		mutex_unlock(&eta->mtx);
		return val;
	}

	dev_dbg(&eta->client->dev, "%s: original val 0x%x\n", __func__, val);

	val &= ~0x3f;
	val |= selector;

	err = i2c_smbus_write_byte_data(eta->client, 0, val);
	if (err < 0)
		dev_err(&eta->client->dev, "Error setting voltage\n");

	mutex_unlock(&eta->mtx);
	return err;
}

static void eta355c_dump_regs(struct eta355c_regulator *eta)
{
	int i, val = 0;

	for (i = 0; i < 4; i++) {
		val = i2c_smbus_read_byte_data(eta->client, i);
		if (val < 0)
			dev_err(&eta->client->dev, "Error read register\n");

		dev_info(&eta->client->dev, "%s: [%d]: 0x%x\n", __func__, i, val);
	}
}

static struct regulator_ops eta_regulator_voltage_ops = {
	.get_voltage_sel	= eta355c_get_voltage_sel,
	.set_voltage_sel	= eta355c_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

static struct eta355c_regulator_config *
of_get_eta355c_regulator_config(struct device *dev, struct device_node *np)
{
	struct eta355c_regulator_config *config;

	config = devm_kzalloc(dev, sizeof(*config), GFP_KERNEL);
	if (!config)
		return ERR_PTR(-ENOMEM);

	config->init_data = of_get_regulator_init_data(dev, np);
	if (!config->init_data)
		return ERR_PTR(-EINVAL);

	config->supply_name = config->init_data->constraints.name;

	if (of_property_read_bool(np, "enable-active-high"))
		config->enable_high = true;

	if (of_property_read_bool(np, "enable-at-boot"))
		config->enabled_at_boot = true;

	of_property_read_u32(np, "startup-delay-us", &config->startup_delay);

	config->gpio = of_get_named_gpio(np, "enable-gpio", 0);

	dev_info(dev, "  config: supply_name:%s, startup-delay-us:%d\n",
		config->supply_name, config->startup_delay);

	dev_info(dev, "  config: enable-gpio: %d, enable-active-high:%d, enable-at-boot:%d\n",
		config->gpio, config->enable_high, config->enabled_at_boot);

	return config;
}

static int eta355c_device_setup(struct eta355c_regulator *eta,
				struct eta355c_regulator_config *config)
{
	struct device *dev = &eta->client->dev;
	unsigned int gpio_flag;
	int ret = 0;

	if (config->gpio >= 0) {
		gpio_flag = GPIOF_DIR_OUT;
		if (config->enable_high)
			gpio_flag |= GPIOF_OUT_INIT_HIGH;
		else
			gpio_flag |= GPIOF_OUT_INIT_LOW;

		ret = gpio_request_one(config->gpio, gpio_flag, "eta355x");
		if (ret) {
			dev_err(dev, "Failed to request gpio %d, ret %d\n",
				config->gpio, ret);
			return ret;
		}
	}

	eta355c_dump_regs(eta);

	/* Get chip ID */
	eta->chip_id = i2c_smbus_read_byte_data(eta->client, ETA355C_REG_CHIPID);
	if (eta->chip_id < 0) {
		dev_err(dev, "Failed to get chip ID!\n");
		goto gpio_exit;
	}

	/* Init voltage range and step */
	switch (eta->chip_id) {
	case ETA_CHIP_ID_ETA3555:
		config->vsel_min = 600000;
		config->vsel_step = 10000;
		dev_info(dev, "ETA3555 detected!\n");
		break;
	case ETA_CHIP_ID_ETA355C:
		config->vsel_min = 603000;
		config->vsel_step = 12750;
		dev_info(dev, "ETA355C detected!\n");
		break;
	default:
		dev_err(dev, "ChipID [%d] not supported!\n", eta->chip_id);
		ret = -EINVAL;
		goto gpio_exit;
	}

gpio_exit:
	if (config->gpio >= 0)
		gpio_free(config->gpio);

	return ret;
}

static int eta355c_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct eta355c_regulator_config *config = i2c->dev.platform_data;
	struct device_node *np = i2c->dev.of_node;
	struct eta355c_regulator *eta;
	struct regulator_config cfg = { };
	int ret;

	dev_info(&i2c->dev, "probe eta355c regulator\n");

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	if (i2c->dev.of_node) {
		config = of_get_eta355c_regulator_config(&i2c->dev, np);
		if (IS_ERR(config))
			return PTR_ERR(config);
	} else {
		config = i2c->dev.platform_data;
	}

	if (!config)
		return -EINVAL;

	eta = devm_kzalloc(&i2c->dev, sizeof(*eta), GFP_KERNEL);
	if (!eta)
		return -ENOMEM;

	eta->client = i2c;

	/* Device init */
	ret = eta355c_device_setup(eta, config);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to setup device!\n");
		return ret;
	}

	eta->desc.name = kstrdup(config->supply_name, GFP_KERNEL);
	if (eta->desc.name == NULL) {
		dev_err(&i2c->dev, "Failed to allocate supply name\n");
		ret = -ENOMEM;
		goto err;
	}
	eta->desc.type = REGULATOR_VOLTAGE;
	eta->desc.owner = THIS_MODULE;
	eta->desc.ops = &eta_regulator_voltage_ops;
	eta->desc.n_voltages = ETA355C_VOLTAGE_STEP_COUNT;
	eta->desc.min_uV = config->vsel_min;
	eta->desc.uV_step = config->vsel_step;
	eta->desc.enable_time = config->startup_delay;

	mutex_init(&eta->mtx);

	if (config->input_supply) {
		eta->desc.supply_name = kstrdup(config->input_supply,
							GFP_KERNEL);
		if (!eta->desc.supply_name) {
			dev_err(&i2c->dev,
				"Failed to allocate input supply\n");
			ret = -ENOMEM;
			goto err_name;
		}
	}

	if (config->gpio >= 0)
		cfg.ena_gpio = config->gpio;
	cfg.ena_gpio_invert = !config->enable_high;
	if (config->enabled_at_boot) {
		if (config->enable_high) {
			cfg.ena_gpio_flags |= GPIOF_OUT_INIT_HIGH;
		} else {
			cfg.ena_gpio_flags |= GPIOF_OUT_INIT_LOW;
		}
	} else {
		if (config->enable_high) {
			cfg.ena_gpio_flags |= GPIOF_OUT_INIT_LOW;
		} else {
			cfg.ena_gpio_flags |= GPIOF_OUT_INIT_HIGH;
		}
	}

	cfg.dev = &i2c->dev;
	cfg.init_data = config->init_data;
	cfg.driver_data = eta;
	cfg.of_node = np;

	eta->rdev = regulator_register(&eta->desc, &cfg);
	if (IS_ERR(eta->rdev)) {
		ret = PTR_ERR(eta->rdev);
		dev_err(&i2c->dev, "Failed to register regulator: %d\n", ret);
		goto err_input;
	}

	i2c_set_clientdata(i2c, eta);

	return 0;

err_input:
	kfree(eta->desc.supply_name);
err_name:
	kfree(eta->desc.name);
err:
	return ret;
}

static int eta355c_remove(struct i2c_client *i2c)
{
	struct eta355c_regulator *eta = i2c_get_clientdata(i2c);

	regulator_unregister(eta->rdev);

	return 0;
}

static const struct i2c_device_id eta355c_id[] = {
	{.name = "eta355c", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, eta355c_id);

static struct of_device_id eta355c_dt_match[] = {
	{ .compatible = "eta,eta355c-regulator" },
	{},
};
MODULE_DEVICE_TABLE(of, eta355c_dt_match);


static struct i2c_driver eta355c_i2c_driver = {
	.driver = {
		.name = "eta355c",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(eta355c_dt_match),
	},
	.probe = eta355c_probe,
	.remove = eta355c_remove,
	.id_table = eta355c_id,
};

static int __init eta355c_init(void)
{
	pr_info("%s %d\n", __func__, __LINE__);
	return i2c_add_driver(&eta355c_i2c_driver);
}

static void __exit eta355c_cleanup(void)
{
	i2c_del_driver(&eta355c_i2c_driver);
}

subsys_initcall(eta355c_init);
module_exit(eta355c_cleanup);

MODULE_AUTHOR("Liu Wei <liuwei@actions-semi.com>");
MODULE_DESCRIPTION("ETA eta355c voltage regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:eta355c-regulator");
