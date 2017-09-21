/*
 * atc260x_dcdc.c  --  DCDC driver for ATC260X
 *
 * Copyright 2011 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

/* UTF-8 encoded.
 * 管理 PWM控制的外接DC-DC, 挂在atc260x下面主要为了使用方便,
 * 实际上只用了atc260x的一路auxadc,没有其它关系. */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/pwm.h>
#include <linux/delay.h>	/* for udelay() */

#include <linux/mfd/atc260x/atc260x.h>

#define CONFIG_ATC260x_PWM_DCDC_FULL_DBG 0
#if CONFIG_ATC260x_PWM_DCDC_FULL_DBG
#define dev_dbgl(DEV, FMT, ARGS...) dev_info(DEV, FMT, ## ARGS)
#else
#define dev_dbgl(DEV, FMT, ARGS...) do { } while (0)
#endif

#define TABLE_LEN_SIZE 16

struct ext_pwm_dcdc_dev {
	struct device *dev;
	struct atc260x_dev *atc260x;
	const char *name;
	struct pwm_device *pwm_dev;
	struct regulator_desc desc;
	struct regulator_dev *regulator;

	u32 pwm_config_table[TABLE_LEN_SIZE];
	u32 vdd_cpu_table[TABLE_LEN_SIZE];
	u32 table_len;
	u32 cur_vdd_index;
	uint auxadc_ch;
};
static unsigned char aux_ch;

//#include <mach/kinfo.h>
/* in uv*/
#define VOLTAGE_EXCESS_THRESHOLD	25000
#define VOLTAGE_LACK_THRESHOLD	10000

u32 vdd_cpu_voltage_get(struct atc260x_dev *atc260x, u32 max_value, u32 min_value)
{
	u32 value[5], value_temp;
	int j;
	value_temp = 0;
	for (j =0; j < 5; j++) {
		udelay(600);
		if(2 == aux_ch)
			value[j]=atc260x_reg_read(atc260x, 0x4f);
		else if(1 == aux_ch)
			value[j]=atc260x_reg_read(atc260x, 0x4e);
		else if(0 == aux_ch)
			value[j]=atc260x_reg_read(atc260x, 0x4d);
		else
		    value[j]=atc260x_reg_read(atc260x, 0x50);
		 if(value[j] > max_value || value[j] < min_value) {
			return -1;
		 }
			value_temp +=value[j];
	 }

	value_temp =  value_temp/5;
	return value_temp;
}
static  void act_writel_pwm(u32 val, u32 reg)
{

	void __iomem *reg_pad_drv1;
	reg_pad_drv1 = ioremap(reg, 4);
	if(NULL == reg_pad_drv1) {
		printk("%s:Unable to ioremap register region\n",__FUNCTION__);
		return;
	}

	writel(val, reg_pad_drv1);

}
u32 act_readl_pwm(u32 reg)
{
	u32 pad_drv_tmp;
	void __iomem *reg_pad_drv1;
	reg_pad_drv1 = ioremap(reg, 4);
	return readl(reg_pad_drv1);
}

extern unsigned long owl_get_cpu0_clk(void);
extern int owl_set_cpu0_clk_freq(int freq_random);
extern int owl_cpu0_clk_init(void);
static int ext_pwm_config_table_adjust(struct ext_pwm_dcdc_dev *dcdc)
{
	u32 	reg_value, adc_value, max_value, min_value, value_temp, act, act_temp;
	int  i, value_offset, first_flag=0, flag=0, times;
	struct atc260x_dev *atc260x =  dcdc->atc260x;

	atc260x_reg_write(atc260x, 0x3e, 0xFFFF);

	pr_info("adjust vdd cpu, num:%d\n", dcdc->table_len);
	for (i = 0; i < dcdc->table_len; i++) {
		pr_info("++%d uv,  %d\n", dcdc->vdd_cpu_table[i], dcdc->pwm_config_table[i]);

		act = dcdc->pwm_config_table[i];
		adc_value = ((dcdc->vdd_cpu_table[i]/1000) << 10) / 3000;
		max_value = adc_value + 0x80;
		min_value = adc_value - 0x80;

		reg_value = 0x100040 | (act << 10);
		act_writel_pwm(reg_value, 0xe01b0054);
		value_temp = vdd_cpu_voltage_get(atc260x, max_value, min_value);
		if (value_temp < 0) {
			pr_info("cpu pwm init data err\n");
			return -EAGAIN;
		}

		value_offset = value_temp - adc_value;
        /*printk("%s   %d   %d  %d \r\n",__func__,value_offset,value_temp,adc_value);*/
		 if (value_offset > 0) {
			 first_flag = 1;
		 } else if(value_offset < 0) {
			 first_flag = 0;
		 } else
			 continue;

		times = 0;
		/*pr_info("first_flag: %d\n",first_flag);*/

		do{
			if (first_flag)
				act_temp = act + 1;
			else
				act_temp = act - 1;

			/*pr_info("act_temp: %d\n",act_temp);*/
			 reg_value = 0x100040 | (act_temp << 10);
			 act_writel_pwm(reg_value, 0xe01b0054);

			 value_temp = vdd_cpu_voltage_get(atc260x, max_value, min_value);
			 if (value_temp < 0) {
				 pr_info("cpu pwm init data err\n");
				 return -EAGAIN;
			 }

			 value_offset = value_temp -adc_value;
			  if (value_offset > 0) {
				 flag = 1;
			 } else if(value_offset < 0) {
				 flag = 0;
			} else {
				pr_info("value_offset= 0 %d\n");
				act = act_temp;
			break;
				}
			 /*pr_info("flag: %d\n",flag);*/

			if (first_flag != flag) {
				if (flag)
					act = act_temp;
				break;
			} else
				act = act_temp;

			if (act <=1 || times++ > 15){
				pr_err("adjust error!%d, %d\n",act,times);
				break;
			}
		}while(1);

		dcdc->pwm_config_table[i] = act;
		if (act != act_temp) {
			 reg_value = 0x100040 | (act << 10);
			 act_writel_pwm(reg_value, 0xe01b0054);
			value_temp = vdd_cpu_voltage_get(atc260x, max_value, min_value);
		}
		value_temp = (value_temp*3000000)>>10;
		pr_info("--%d uv,  %d\n", value_temp, act);
	}
	for (i = 0; i < dcdc->table_len; i++) {
		printk(KERN_WARNING "dcdc->pwm_config_table[%d] = %d" ,
			i, dcdc->pwm_config_table[i]);
	}
	reg_value = 0x100040 | (dcdc->pwm_config_table[8]  << 10);
	act_writel_pwm(reg_value, 0xe01b0054);

	return 0;
}
static int ext_pwm_dcdc_reconfig_pwm(struct ext_pwm_dcdc_dev *dcdc,
				     uint voltage_index)
{
	uint pwm_level, pwm_active_time, pwm_period;
	int ret;

	pwm_period = pwm_get_period(dcdc->pwm_dev);
	pwm_level = dcdc->pwm_config_table[voltage_index];	/* DTS那边的表是预先减了1的. */
	pwm_active_time = ((u64) pwm_period * pwm_level) / 64U;	/* 取下整, 因为duty越低电压越高. */
	ret = pwm_config(dcdc->pwm_dev, pwm_active_time, pwm_period);

#if CONFIG_ATC260x_PWM_DCDC_FULL_DBG
	dev_info(dcdc->dev,
		 "%s() set pwm, pwm_level=%u active_time=%u period=%u ret=%d\n",
		 __func__, pwm_level, pwm_active_time, pwm_period, ret);
#endif
	return ret;
}

static int ext_pwm_dcdc_set_voltage(struct regulator_dev *rdev,
				    int min_uV, int max_uV, unsigned *selector)
{
	struct ext_pwm_dcdc_dev *dcdc;
	uint i;
	int ret;

	dcdc = rdev_get_drvdata(rdev);

	if (min_uV < dcdc->vdd_cpu_table[0]) {
		dev_err(dcdc->dev, "%s() set volt too low! volt=%d\n", __func__,
			min_uV);
		min_uV = dcdc->vdd_cpu_table[0];
	}
	if (min_uV > dcdc->vdd_cpu_table[dcdc->table_len - 1]) {
		dev_err(dcdc->dev, "%s() set volt too high! volt=%d\n",
			__func__, min_uV);
		min_uV = dcdc->vdd_cpu_table[dcdc->table_len - 1];
	}

	for (i = 0; i < dcdc->table_len; i++) {
		if (min_uV <= dcdc->vdd_cpu_table[i])
			break;
	}
	dcdc->cur_vdd_index = i;
	*selector = i;

	ret = ext_pwm_dcdc_reconfig_pwm(dcdc, i);
	if (ret == 0)
		mdelay(3);	/*extern dc-dc need ~1ms stable time, wait for 3ms here. */

	return ret;
}

static int ext_pwm_dcdc_get_voltage(struct regulator_dev *rdev)
{
	struct ext_pwm_dcdc_dev *dcdc;

	dcdc = rdev_get_drvdata(rdev);

#if 0
	s32 real_volt;
	int ret;
	ret =
	    atc260x_auxadc_get_translated(dcdc->atc260x, dcdc->auxadc_ch,
					  &real_volt);
	if (ret != 0) {
		dev_err(dcdc->dev, "failed to convert real output voltage\n");
		real_volt = 0;
	}
	return real_volt * 1000U;
#endif

	/* The regulator framework require to return the controll-value,
	 * not the real value. */
	return dcdc->vdd_cpu_table[dcdc->cur_vdd_index];
}

extern int owl_pwm_get_duty_cfg(struct pwm_device *pwm, uint *comparator_steps, uint *counter_steps);
static int ext_pwm_dcdc_get_init_voltage_selector(struct ext_pwm_dcdc_dev *dcdc)
{
#if 1				/* owl_pwm_get_duty_cfg not implement */
	uint i, comparator_steps, counter_steps, pwm_level;
	int ret;

	comparator_steps = 0;
	counter_steps = 0;
	ret =
	    owl_pwm_get_duty_cfg(dcdc->pwm_dev, &comparator_steps,
				 &counter_steps);
	if (ret)
		return ret;
	if (counter_steps == 0)
		return -EINVAL;

	pwm_level = (64U * comparator_steps) / counter_steps;
	pwm_level--;		/* DTS那边的表是预先减了1的. */
printk(KERN_WARNING "pwm_level = %d\n", pwm_level);
printk(KERN_WARNING "comparator_steps = %d\n", comparator_steps);
printk(KERN_WARNING "counter_steps = %d\n", counter_steps);
	for (i = 0; i < dcdc->table_len; i++) {
		if (pwm_level == dcdc->pwm_config_table[i])
			return i;
	}
#endif
	return -ENXIO;
}

static struct regulator_ops ext_pwm_dcdc_ops = {
	.get_voltage = ext_pwm_dcdc_get_voltage,
	.set_voltage = ext_pwm_dcdc_set_voltage,
};

static int ext_pwm_dcdc_probe(struct platform_device *pdev)
{
	struct ext_pwm_dcdc_dev *dcdc;
	struct regulator_init_data *init_data;
	struct regulator_config config = { };
	u32 pwm_revise = 0;
	int ret, len;
	uint __maybe_unused i;
	struct device_node *node;
	const __be32 *property;
	int freq_khz;

	dev_info(&pdev->dev, "Probing %s\n", pdev->name);

	dcdc =
	    devm_kzalloc(&pdev->dev, sizeof(struct ext_pwm_dcdc_dev),
			 GFP_KERNEL);
	if (dcdc == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}
	dcdc->dev = &pdev->dev;
	dcdc->atc260x = atc260x_get_parent_dev(&pdev->dev);
	dcdc->name = "unknown";

	platform_set_drvdata(pdev, dcdc);

	node = pdev->dev.of_node;
	property = of_get_property(node, "table_len", &len);
	if (property && len == sizeof(int)) {
		dcdc->table_len = (be32_to_cpup(property));
		if (dcdc->table_len > TABLE_LEN_SIZE) {
			dev_err(&pdev->dev, "table_len too leng \n");
			dcdc->table_len = 3;
		}
	} else {
		dev_err(&pdev->dev, "get table_len failed \n");
		dcdc->table_len = 3;
	}

	dev_info(&pdev->dev, "table_len = %d\n", dcdc->table_len);
	of_property_read_u32_array(node, "pwm_config_table",
				   dcdc->pwm_config_table, dcdc->table_len);
	of_property_read_u32_array(node, "vdd_cpu_table", dcdc->vdd_cpu_table,
				   dcdc->table_len);
#if CONFIG_ATC260x_PWM_DCDC_FULL_DBG
	for (i = 0; i < dcdc->table_len; i++) {
		dev_dbgl(&pdev->dev, "get pwm_config_table[%d]=%d\n", i,
			 dcdc->pwm_config_table[i]);
	}
	for (i = 0; i < dcdc->table_len; i++) {
		dev_dbgl(&pdev->dev, "get vdd_cpu_table[%d]=%d\n", i,
			 dcdc->vdd_cpu_table[i]);
	}
#endif
        property = of_get_property(node, "aux", &len);
        if (property && len == sizeof(int))
                aux_ch = be32_to_cpup(property);
        else
                aux_ch = 1;
	pr_info("aux:%d\n",aux_ch);

/*         property = of_get_property(node, "pwm_revise", &len);
        if (property && len == sizeof(int))
                pwm_revise = be32_to_cpup(property);
        else
                pwm_revise = 0;
	pr_info("pwm_revise:%d\n",pwm_revise);

	dev_info(&pdev->dev, "pwm_revise = %d\n", pwm_revise);
	if (pwm_revise) {
		ret = owl_cpu0_clk_init();
		if(ret) {
				pr_err("failed to get cpu0 clock: %d\n", ret);
				return -ENXIO;
		}

		freq_khz = owl_get_cpu0_clk() / 1000;
		pr_info("current clk %d\n", owl_get_cpu0_clk() / 1000);
		ret = owl_set_cpu0_clk_freq(0);
		pr_info("current clk %d\n", owl_get_cpu0_clk() / 1000);
		if(ret) {
				return -ENXIO;
		}
		ext_pwm_config_table_adjust(dcdc);
		ret = owl_set_cpu0_clk_freq(freq_khz);
		if(ret) {
				return -ENXIO;
		}
		pr_info("current clk %d\n", owl_get_cpu0_clk() / 1000);
	} */
	init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node);
	if (!init_data)
		return -ENOMEM;
	init_data->constraints.valid_modes_mask = REGULATOR_MODE_NORMAL;
	dcdc->name = init_data->constraints.name;

	ret = atc260x_auxadc_find_chan(dcdc->atc260x, "AUX0");
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to find auxadc channel: AUX0\n");
		return -ENXIO;
	}
	dcdc->auxadc_ch = ret;

	dcdc->pwm_dev = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(dcdc->pwm_dev)) {
		dev_err(&pdev->dev, "%s, unable to request pwm_device\n",
			__func__);
		return -EAGAIN;
	}
	if (0 == dcdc->pwm_dev->period) {
		dev_err(&pdev->dev, "%s, ext_dcdc_pwm->period is zero!\n",
			__func__);
		return -EINVAL;
	}
	pwm_enable(dcdc->pwm_dev);

	ret = ext_pwm_dcdc_get_init_voltage_selector(dcdc);
	if (ret >= 0) {
		dcdc->cur_vdd_index = ret;
		dev_info(&pdev->dev, "got init voltage selector %d\n", ret);
	} else {
		dcdc->cur_vdd_index = 0;
		dev_info(&pdev->dev,
			 "failed to get init voltage selector, assum 0，ret = %d\n", ret);
	}

	dcdc->desc.name = dcdc->name;
	dcdc->desc.type = REGULATOR_VOLTAGE;
	dcdc->desc.n_voltages = dcdc->table_len;
	dcdc->desc.ops = &ext_pwm_dcdc_ops;
	dcdc->desc.owner = THIS_MODULE;

	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.driver_data = dcdc;
	config.of_node = pdev->dev.of_node;

	dcdc->regulator = regulator_register(&dcdc->desc, &config);
	if (IS_ERR(dcdc->regulator)) {
		ret = PTR_ERR(dcdc->regulator);
		dev_err(&pdev->dev, "Failed to register VDD_CPU_DCDC: %d\n",
			ret);
		return -EAGAIN;
	}
#if CONFIG_ATC260x_PWM_DCDC_FULL_DBG && defined(CONFIG_REGULATOR_VIRTUAL_CONSUMER)
	/* for debug */
	platform_device_register_resndata(NULL, "reg-virt-consumer",
					  40 + pdev->id,
					  NULL, 0,
					  dcdc->name, strlen(dcdc->name) + 1);
#endif
	return 0;
}

static int ext_pwm_dcdc_remove(struct platform_device *pdev)
{
	struct ext_pwm_dcdc_dev *dcdc = platform_get_drvdata(pdev);

	pwm_disable(dcdc->pwm_dev);
	devm_pwm_put(&pdev->dev, dcdc->pwm_dev);

	platform_set_drvdata(pdev, NULL);

	regulator_unregister(dcdc->regulator);

	return 0;
}

static int ext_pwm_dcdc_suspend_late(struct device *dev)
{
	struct ext_pwm_dcdc_dev *__maybe_unused dcdc = dev_get_drvdata(dev);
	dev_dbgl(dcdc->dev, "%s() selector=%d cur_voltage=%uuV\n",
		 __func__,
		 dcdc->cur_vdd_index, dcdc->vdd_cpu_table[dcdc->cur_vdd_index]);
	return 0;
}

static int ext_pwm_dcdc_resume_early(struct device *dev)
{
	struct ext_pwm_dcdc_dev *dcdc = dev_get_drvdata(dev);
	int ret;
	if(!strcmp(dcdc->name, "vdd-gpu-dcdc")) {

		ret = ext_pwm_dcdc_reconfig_pwm(dcdc, dcdc->cur_vdd_index);
		if (ret) {
			dev_err(dcdc->dev,
				"%s() failed to restore seletor %u, ret=%d\n", __func__,
				dcdc->cur_vdd_index, ret);
			return ret;
		}
		pr_info("%s:restore selsctor", dcdc->name);
		dev_dbgl(dcdc->dev, "restore seletor: %u\n", dcdc->cur_vdd_index);
	}
	return 0;
}

static const struct dev_pm_ops s_ext_pwm_dcdc_pm_ops = {
	.suspend_late = ext_pwm_dcdc_suspend_late,
	.resume_early = ext_pwm_dcdc_resume_early,
	.freeze_late = ext_pwm_dcdc_suspend_late,
	.thaw_early = ext_pwm_dcdc_resume_early,
	.poweroff_late = ext_pwm_dcdc_suspend_late,
	.restore_early = ext_pwm_dcdc_resume_early,
};

static const struct of_device_id ext_pwm_dcdc_match[] = {
	{.compatible = "actions,atc2603a-ext-pwm-dcdc",},
	{.compatible = "actions,atc2603c-ext-pwm-dcdc",},
	{.compatible = "actions,atc2609a-ext-pwm-dcdc",},
	{.compatible = "actions,atc260x-ext-pwm-dcdc",},
	{},
};

MODULE_DEVICE_TABLE(of, ext_pwm_dcdc_match);

static struct platform_driver ext_pwm_dcdc_driver = {
	.driver = {
		   .name = "atc260x-ext-pwm-dcdc",
		   .owner = THIS_MODULE,
		   .pm = &s_ext_pwm_dcdc_pm_ops,
		   .of_match_table = of_match_ptr(ext_pwm_dcdc_match),
		   },
	.probe = ext_pwm_dcdc_probe,
	.remove = ext_pwm_dcdc_remove,
};

static int __init atc260x_ext_pwm_dcdc_init(void)
{
	int ret;
	ret = platform_driver_register(&ext_pwm_dcdc_driver);
	if (ret != 0)
		pr_err("%s() Failed to register driver: %d\n", __func__, ret);
	return ret;
}

subsys_initcall(atc260x_ext_pwm_dcdc_init);

static void __exit atc260x_ext_pwm_dcdc_exit(void)
{
	platform_driver_unregister(&ext_pwm_dcdc_driver);
}

module_exit(atc260x_ext_pwm_dcdc_exit);

/* Module information */
MODULE_AUTHOR("Actions Semi, Inc");
MODULE_DESCRIPTION("External PWM DCDC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atc260x-pwm-dcdc");
