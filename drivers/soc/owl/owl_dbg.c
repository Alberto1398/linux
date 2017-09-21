#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/pm_clock.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/kdebug.h>
#include <linux/sysrq.h>


static void __iomem *jtag_base;
static unsigned int jtag_val = 0xce19c0;
static unsigned int jtag_mask = 0xffffff;


static int jtag_handler(struct notifier_block *self,
			     unsigned long val,
			     void *data)
{
	unsigned int regv;

	regv = readl(jtag_base);
	pr_info("jtag read val = 0x%x\n", regv);
	regv = (regv & ~jtag_mask) | jtag_val;
	pr_info("jtag set pinctrl = 0x%x, mask=0x%x, sval=0x%x\n",
		regv, jtag_mask,  jtag_val);
	writel(regv, jtag_base);

	return NOTIFY_OK;
}
static void sysrq_change_jtag(int key)
{
	jtag_handler(NULL, 0, NULL);
}

static struct sysrq_key_op sysrq_jtag_op = {
	.handler	= sysrq_change_jtag,
	.help_msg	= "changejtag(x)",
	.action_msg	= "jtag",
};

static struct notifier_block die_jtag_notifier = {
	.notifier_call = jtag_handler,
	.priority = 200
};

static int owl_jtag_probe(struct platform_device *pdev)
{
	unsigned int panic_time = 0;
	jtag_base = of_iomap(pdev->dev.of_node, 0);
	if (jtag_base == NULL) {
		pr_info("jtag: get base addr fail\n");
		return -1;
	}
	of_property_read_u32(pdev->dev.of_node, "pinctl_val", &jtag_val);
	of_property_read_u32(pdev->dev.of_node, "pinctl_mask", &jtag_mask);
	of_property_read_u32(pdev->dev.of_node, "painc-timeout", &panic_time);
	panic_timeout = panic_time;
	pr_info("jtag dev init,(%x,%x), panic_time=%d\n", jtag_val, jtag_mask, panic_timeout);
	if (0 != register_die_notifier(&die_jtag_notifier)) {
		pr_info("jtag: register_die_notifier fail\n");
		return -1;
	}

	if (0 != register_sysrq_key('x', &sysrq_jtag_op)) {
		pr_info("jtag: register_sysrq_key fail\n");
		return -1;
	}
	return 0;
}

static const struct of_device_id owl_of_match[] = {
	{ .compatible = "actions,s700-jtag" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, owl_of_match);

static struct platform_driver owl_jtag_driver = {
	.driver = {
		.name = "owl-jtag",
		.owner = THIS_MODULE,
		.of_match_table = owl_of_match,
	},
	.probe = owl_jtag_probe,
};

static __init int owl_jtag_init(void)
{
	return platform_driver_register(&owl_jtag_driver);
}
arch_initcall(owl_jtag_init);

