/*
* Asoc  irkeypad driver
*
* Copyright (C) 2011 Actions Semiconductor, Inc
* Author:  chenbo <chenbo@actions-semi.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/input/owl-irkey.h>

#define UART_CTL            (0x0000)
#define UART_RXDAT          (0x0004)
#define UART_TXDAT          (0x0008)
#define UART_STAT           (0x000C)

#define OWL_IR_OFFSET (0x50)
#define OWL_IR_CTL (OWL_IR_OFFSET + 0x0)
#define OWL_IR_STAT (OWL_IR_OFFSET + 0x4)
#define OWL_IR_CC (OWL_IR_OFFSET + 0x8)
#define OWL_IR_KDC (OWL_IR_OFFSET + 0xc)
#define OWL_IR_RCC (OWL_IR_OFFSET + 0x10)
#define OWL_IR_FILTER (OWL_IR_OFFSET + 0x14)
#define OWL_IR_WK (OWL_IR_OFFSET + 0x18)

#define IR_LEN 1
#define IR_IDX 13
#define BOARDINFO_STR_LEN 16
extern int read_mi_item(char *name, void *buf, unsigned int count);
extern int write_mi_item(char *name, void *buf, unsigned int count);
static char *ir_config_str[] = {
	"size",
	"protocol",
	"user_code",
	"wk_code",
	"period",
};

typedef struct {
	struct list_head head;
	struct input_dev *input_dev;
	struct timer_list timer;
	struct work_struct work;

	int irq;
	void __iomem *reg_base;
	struct clk *uart_clk;
	struct clk *irc_switch_clk;

	unsigned int speed_up;
	unsigned int mouse_speed;
	bool is_mouse_mode;
	unsigned int ir_val;
	unsigned int user_code_val;
	unsigned int old_key_val;
	unsigned int new_key_val;
	struct irconfig *last_config;
} owl_irkey_t;

static int (*ir_wakeup_power_check)(void *data);
static void (*ir_wakeup_notifier)(struct irconfig *config, void *data);
static void *ir_notifier_data;
static bool initialized;

static void ir_work_handler(struct work_struct *work)
{
	owl_irkey_t *irkeypad = container_of(work, owl_irkey_t, work);
	struct irconfig *config = irkeypad->last_config;

	if (config && ir_wakeup_notifier) {
		ir_wakeup_notifier(irkeypad->last_config, ir_notifier_data);
		printk(KERN_NOTICE"owl-irkeypad detect ir controller changed.\n"
							"User code:0x%x,protocol:%d,wk code:0x%x\n",
							config->user_code, config->protocol, config->wk_code);
	} else
		printk(KERN_ERR"%s fatal error\n", __func__);
}

static void ir_timer_handler(unsigned long data)
{
	owl_irkey_t *irkeypad = (void *)data;
	unsigned int old = irkeypad->old_key_val;

	if (old) {
		input_report_key(irkeypad->input_dev, old, 0);
		input_sync(irkeypad->input_dev);
		printk("owl-irkey: %d key released.\n", old);
	}

	irkeypad->new_key_val = KEY_RESERVED;
	irkeypad->old_key_val = KEY_RESERVED;
}

static irqreturn_t ir_irq_handler(int irq, void *data)
{
	struct irconfig *irconfig;
	owl_irkey_t *irkeypad = data;
	unsigned int stat, *ir_val, i;
	struct input_dev *input_dev = irkeypad->input_dev;

	stat = readl(irkeypad->reg_base + OWL_IR_STAT);
	writel(stat, irkeypad->reg_base + OWL_IR_STAT);
	irkeypad->ir_val = readl(irkeypad->reg_base + OWL_IR_KDC);
	irkeypad->user_code_val = readl(irkeypad->reg_base + OWL_IR_RCC);

	list_for_each_entry(irconfig, &(irkeypad->head), list) {
		if (irconfig->user_code != irkeypad->user_code_val)
			continue;
		ir_convert(irconfig->protocol, &irkeypad->ir_val);
		for (i = 0; i < irconfig->size; i++) {
			ir_val = irconfig->ir_code + i;
			if (*ir_val == irkeypad->ir_val) {
				irkeypad->new_key_val = *(irconfig->key_code + i);
				break;
			}
		}
		if ((!irkeypad->last_config) || (irkeypad->user_code_val != irkeypad->last_config->user_code)) {
			pr_info("config wake up function to pmu ir\n");
			irkeypad->last_config = irconfig;
			schedule_work(&irkeypad->work);
		}
	}

	if (irkeypad->is_mouse_mode) {
		static unsigned int speed = 1;
		bool discarded = false;
#define SPEED_MAX (1 << 6)
		if (stat & IRC_STAT_RCD) {
			speed <<= 1;
			if (speed > SPEED_MAX)
				speed = SPEED_MAX;
		} else {
			speed = 1;
		}
		irkeypad->mouse_speed = speed + 10;
		switch (irkeypad->new_key_val) {
		case KEY_UP:
			input_report_rel(input_dev, REL_X, 0);
			input_report_rel(input_dev, REL_Y, -irkeypad->mouse_speed);
			input_sync(input_dev);
			discarded = true;
			break;
		case KEY_DOWN:
			input_report_rel(input_dev, REL_X, 0);
			input_report_rel(input_dev, REL_Y, irkeypad->mouse_speed);
			input_sync(input_dev);
			discarded = true;
			break;
		case KEY_LEFT:
			input_report_rel(input_dev, REL_X, -irkeypad->mouse_speed);
			input_report_rel(input_dev, REL_Y, 0);
			input_sync(input_dev);
			discarded = true;
			break;
		case KEY_RIGHT:
			input_report_rel(input_dev, REL_X, irkeypad->mouse_speed);
			input_report_rel(input_dev, REL_Y, 0);
			input_sync(input_dev);
			discarded = true;
			break;
		case KEY_SELECT:
			irkeypad->new_key_val = BTN_LEFT;
			break;
		case KEY_BACK:
			irkeypad->new_key_val = BTN_RIGHT;
			break;
		default:
			irkeypad->speed_up       = 0;
			irkeypad->mouse_speed    = 10;
			break;
		}
		if (discarded) {
			irkeypad->new_key_val = KEY_RESERVED;
			irkeypad->old_key_val = KEY_RESERVED;
			goto end;
		}
	}
	if (irkeypad->new_key_val && (irkeypad->new_key_val != irkeypad->old_key_val)) {
		if ((irkeypad->new_key_val == KEY_MOVE) && irkeypad->is_mouse_mode)
			irkeypad->is_mouse_mode = false;
		else if (irkeypad->new_key_val == KEY_MOVE) {
			irkeypad->is_mouse_mode = true;
			input_report_rel(input_dev, REL_X, 1);
			input_report_rel(input_dev, REL_Y, 1);
			input_report_key(input_dev, BTN_LEFT, 0);
			input_report_key(input_dev, BTN_RIGHT, 0);
			input_sync(input_dev);
			goto end;
		}

		if (irkeypad->old_key_val)
			input_report_key(input_dev, irkeypad->old_key_val, 0);
		input_report_key(input_dev, irkeypad->new_key_val, 1);
		input_sync(input_dev);
	}

	if (irkeypad->new_key_val)
		mod_timer(&irkeypad->timer, jiffies+msecs_to_jiffies(irkeypad->last_config->period));
	else
		pr_err("Unsolved ir val:0x%x,user code val:0x%x,stat:0x%x\n",
		irkeypad->ir_val, irkeypad->user_code_val, stat);

end:
	irkeypad->old_key_val = irkeypad->new_key_val;
	irkeypad->new_key_val = KEY_RESERVED;

	return IRQ_HANDLED;
}

static int get_irconfigs(struct list_head *head, struct device_node *dnp)
{
	struct device_node *node;
	struct irconfig *irconfig;
	int i, ret;

	if (!dnp)
		return -1;
	INIT_LIST_HEAD(head);

	for_each_child_of_node(dnp, node) {
		irconfig = kmalloc(sizeof(*irconfig), GFP_KERNEL);
		if (!irconfig) {
			printk(KERN_ERR"failed to malloc %ld memory\n", sizeof(*irconfig));
			return -1;
		}

		for (i = 0; i < ARRAY_SIZE(ir_config_str); i++) {
			ret = of_property_read_u32(node, ir_config_str[i], (&(irconfig->size) + i));
			if (ret != 0) {
				printk(KERN_INFO"get %d fail!\n",  i);
				goto failed;
			}
		}

		irconfig->ir_code =	kmalloc(sizeof(unsigned int) * (irconfig->size), GFP_KERNEL);
		if (!irconfig->ir_code) {
			printk(KERN_ERR"failed to malloc %ld memory\n",
			sizeof(unsigned int) * (irconfig->size));
			return -1;
		}
		ret = of_property_read_u32_array(node, "ir_code", (u32 *)irconfig->ir_code,	(irconfig->size));
		if (ret != 0) {
			printk(KERN_INFO"get ir_code fail!\n");
			goto free_ir_code;
		}

		irconfig->key_code = kmalloc(sizeof(unsigned int) * (irconfig->size), GFP_KERNEL);
		if (!irconfig->key_code) {
			printk(KERN_ERR"failed to malloc %ld memory\n",
			sizeof(unsigned int) * (irconfig->size));
			return -1;
		}
		ret = of_property_read_u32_array(node, "key_code", (u32 *)irconfig->key_code, (irconfig->size));
		if (ret != 0) {
			printk(KERN_INFO"get  key_code fail!\n");
			ret = -EINVAL;
			goto free_key_code;
		}

		list_add(&(irconfig->list), head);
		continue;

free_key_code:
		kfree(irconfig->key_code);

free_ir_code:
		kfree(irconfig->ir_code);
failed:
		kfree(irconfig);
	}

	if (list_empty(head)) {
		printk(KERN_ERR"need irkey code configs\n");
		return -1;
	}

	return 0;
}

static int ir_open(struct input_dev *dev)
{
	owl_irkey_t *irkeypad = input_get_drvdata(dev);

	if ((clk_prepare_enable(irkeypad->uart_clk) < 0)
			|| (clk_set_rate(irkeypad->uart_clk, 38000) < 0)
			|| (clk_prepare_enable(irkeypad->irc_switch_clk) < 0)) {
		dev_err(&dev->dev, "unable to get UART clock\n");
		return -EIO;
	}

	udelay(100);
	writel(0x8000, irkeypad->reg_base+UART_CTL);
	writel(readl(irkeypad->reg_base + OWL_IR_STAT), irkeypad->reg_base + OWL_IR_STAT);
	writel(0xffff, irkeypad->reg_base + OWL_IR_CC);
	writel((0x0<<5) | (0x1<<4) |
			(0x1<<3) | (0x1<<2)	|
			(IR_PROTOCOL_NEC8<<0),
			irkeypad->reg_base+OWL_IR_CTL);
	enable_irq(irkeypad->irq);
    
	return 0;
}

static void ir_close(struct input_dev *dev)
{
	owl_irkey_t *irkeypad = input_get_drvdata(dev);

	disable_irq(irkeypad->irq);
	writel(readl(irkeypad->reg_base+OWL_IR_CTL) & ~(0x3<<2),
		irkeypad->reg_base + OWL_IR_CTL);
	clk_disable_unprepare(irkeypad->uart_clk);
	clk_disable_unprepare(irkeypad->irc_switch_clk);

	return;
}
#if 0
static int set_vendor_id_test()
{
	char *buf = "123456789A08DFE3";
	int ret = write_mi_item("VENDOR", buf, 17);
	printk("[%s]begin...\n", __func__);
	if (ret < 0) {
		pr_err("write board info failed\n");
		return -1;
	}
	return 0;
}
#endif
static int get_bordinfo(char *ir_info)
{
	char buf[20];
	int str_len;
	int ret = 0;
	/*set_vendor_id_test();*/
	memset(buf, 0, sizeof(buf));
	str_len = read_mi_item("VENDOR", buf, sizeof(buf));
	pr_info("str_len:%d\n", str_len);

	if (str_len < 0 || str_len < BOARDINFO_STR_LEN) {
		pr_err("read vendor id failed\n");
		ret = -1;
		return ret;
	}
	pr_info("vendor id is:%s\n", buf);
	strncpy(ir_info, buf + (char)IR_IDX, IR_LEN);
	*(ir_info + IR_LEN) = '\0';
	pr_info("ir cfg info:%s\n", ir_info);
	return ret;
}

static bool is_multictrler_cfg(void)
{
	char ir_id[IR_LEN + 1] = "";
	int ret;
	ret = get_bordinfo(ir_id);

	if (ret < 0)
		return false;
	if (!strcmp(ir_id, "f") || !strcmp(ir_id, "F"))
		return true;
	else
		return false;
}

static int ir_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct input_dev *input_dev;
	struct device_node *dnp = pdev->dev.of_node;
	owl_irkey_t *irkeypad;
	struct irconfig *irconfig;
	int ret, i;

	pr_info("[%s]start...\n", __func__);

	if (!is_multictrler_cfg()) {
		dev_info(&pdev->dev, "multictrler disabled.\n");
		return -ENODEV;
	}
	irkeypad = kzalloc(sizeof(*irkeypad), GFP_KERNEL);
	if (irkeypad == NULL) {
		dev_err(&pdev->dev, "failed to allocate irkeypad driver data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, irkeypad);

	if (get_irconfigs(&irkeypad->head, dnp) < 0) {
		dev_err(&pdev->dev, "failed to get irconfigs from dts\n");
		goto fail_dts;
	}
	irkeypad->irq = platform_get_irq(pdev, 0);
	if (irkeypad->irq < 0) {
		dev_err(&pdev->dev, "failed to get irkeypad irq\n");
		goto fail_dts;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		printk(KERN_ERR"irkey can not get memory resource\n");
		goto fail_dts;
	}
	devm_request_mem_region(&pdev->dev, res->start, resource_size(res), "owl-irkey");
	irkeypad->reg_base = devm_ioremap_nocache(&pdev->dev, res->start, resource_size(res));
	if (!irkeypad->reg_base) {
		printk(KERN_ERR"irkey map memory resource\n");
		goto fail_dts;
	}
	irkeypad->uart_clk = devm_clk_get(&pdev->dev, "uart");
	irkeypad->irc_switch_clk = devm_clk_get(&pdev->dev, "irc_switch");
	if (IS_ERR(irkeypad->uart_clk) || IS_ERR(irkeypad->irc_switch_clk)) {
		dev_err(&pdev->dev, "unable to get UART clock\n");
		goto failed;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate  irkeypad input device\n");
		ret = -ENOMEM;
		goto fail_input;
	}
	input_dev->name = pdev->name;
	input_dev->phys = "owl-irkeypad/input0";
	input_dev->open = ir_open;
	input_dev->close = ir_close;
	input_dev->dev.parent = &pdev->dev;
	input_dev->keycodemax = KEY_MAX;
	input_dev->id.bustype = BUS_HOST;

	irkeypad->input_dev = input_dev;
	input_set_drvdata(input_dev, irkeypad);
	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	list_for_each_entry(irconfig, &(irkeypad->head), list) {
		for (i = 0; i < irconfig->size; i++) {
			if (irconfig->key_code[i] != KEY_RESERVED)
				__set_bit(irconfig->key_code[i],
					input_dev->keybit);
		}
	}
	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed;
	}
	init_timer(&irkeypad->timer);
	irkeypad->timer.function = ir_timer_handler;
	irkeypad->timer.data = (unsigned long)irkeypad;
	INIT_WORK(&irkeypad->work, ir_work_handler);

	ret = request_irq(irkeypad->irq, ir_irq_handler,
					IRQF_TRIGGER_HIGH, "owl-irkeypad", irkeypad);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irkeypad IRQ:%d\n", irkeypad->irq);
		goto failed;
	}
	disable_irq(irkeypad->irq);
	initialized = true;

failed:
fail_input:
fail_dts:
	pr_info("[%s]finish...\n", __func__);
	return 0;
}

static int ir_remove(struct platform_device *pdev)
{
	struct irconfig *irconfig, *temp;
	owl_irkey_t *irkeypad = platform_get_drvdata(pdev);

	free_irq(irkeypad->irq, irkeypad);
	del_timer_sync(&irkeypad->timer);
	cancel_work_sync(&irkeypad->work);
	ir_close(irkeypad->input_dev);
	input_unregister_device(irkeypad->input_dev);
	input_free_device(irkeypad->input_dev);

	list_for_each_entry_safe(irconfig, temp, &(irkeypad->head), list) {
		kfree(irconfig->key_code);
		kfree(irconfig->ir_code);
		kfree(irconfig);
	}
	kfree(irkeypad);

	return 0;
}

#ifdef CONFIG_PM
static int ir_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	owl_irkey_t *irkeypad = platform_get_drvdata(pdev);

	ir_close(irkeypad->input_dev);

	if (irkeypad->last_config && ir_wakeup_notifier) {
		ir_wakeup_notifier(irkeypad->last_config, ir_notifier_data);
	}
    
	return 0;
}

static int ir_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	owl_irkey_t *irkeypad = platform_get_drvdata(pdev);

	ir_open(irkeypad->input_dev);
    
	if (ir_wakeup_power_check != NULL && ir_wakeup_power_check(ir_notifier_data) >= 0) {
		input_report_key(irkeypad->input_dev, KEY_POWER, 1);
		input_report_key(irkeypad->input_dev, KEY_POWER, 0);
		input_sync(irkeypad->input_dev);
	}

	if (irkeypad->last_config && ir_wakeup_notifier) {
		ir_wakeup_notifier(irkeypad->last_config, ir_notifier_data);
	}
    
	return 0;
}
bool ir_register_wk_notifier(void (*cb1)(struct irconfig *, void *), int (*cb2)(void *), void *data)
{
	if (initialized) {
		ir_wakeup_notifier = cb1;
		ir_wakeup_power_check = cb2;
		ir_notifier_data = data;
		return true;
	} else
		return false;
}
EXPORT_SYMBOL(ir_register_wk_notifier);
static const struct dev_pm_ops ir_pm_ops = {
	.suspend    = ir_suspend,
	.resume     = ir_resume,
};
#endif

static const struct of_device_id owl_ir_match[] = {
	{ .compatible = "actions,owl-irkeypad", },
	{},
};
MODULE_DEVICE_TABLE(of, owl_ir_match);

static struct platform_driver ir_driver = {
	.probe      = ir_probe,
	.remove     = ir_remove,
	.driver     = {
		.name   = "owl-irkey",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(owl_ir_match),
#ifdef CONFIG_PM
		.pm = &ir_pm_ops,
#endif
	},
};
static int __init owl_irkeypad_init(void)
{
	return platform_driver_register(&ir_driver);
}
late_initcall(owl_irkeypad_init);
static void __exit owl_irkeypad_exit(void)
{
	platform_driver_unregister(&ir_driver);
}
module_exit(owl_irkeypad_exit);

MODULE_AUTHOR("Actions Semi, Inc");
MODULE_DESCRIPTION("owl irkeypad driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:owl-irkey");
MODULE_VERSION("Actions-v1-201601211320");