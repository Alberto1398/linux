/*
 * Actions OWL SoC Power Manager driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define DEBUG
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/seq_file.h>
#include <linux/kobject.h>
#include <linux/power_supply.h>
#include <linux/owl_pm.h>
#include <asm/suspend.h>
#include <asm/cacheflush.h>
#include <asm/psci.h>
#include <asm/system_misc.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

static void __iomem *pm_reg_base;
static DEFINE_SPINLOCK(pm_reg_lock);

/* use the unused SOC register to transfer parameter to EL3 */
#define OWL_PM_WAKEUP_SRC_REG		(0x0)	/* LWER_EL_AARCH64_IRQ_VECTOR */
#define OWL_PM_FLAG_REG		(0x4)	/* LWER_EL_AARCH32_IRQ_VECTOR */

#define OWL_PM_FLAG_RESET_PSTORE	(1 << 0)
#define OWL_PM_FLAG_BATTERY_ONLINE	(1 << 1)
#define OWL_PM_FLAG_REBOOT_TGT_SHIFT	(16)
#define OWL_PM_FLAG_REBOOT_TGT_WIDTH	(3)
#define OWL_PM_FLAG_REBOOT_TGT_MASK	(7 << OWL_PM_FLAG_REBOOT_TGT_SHIFT)
#define OWL_PM_FLAG_REBOOT_TGT(x)	(((x) & 7) << OWL_PM_FLAG_REBOOT_TGT_SHIFT)
#define OWL_PM_FLAG_ALL			(~0u)

int owl_pmic_setup_aux_wakeup_src(uint wakeup_src, uint on)
{
#if 0
	uint aux_mask, bitpos;

	aux_mask = OWL_PMIC_WAKEUP_SRC_IR | OWL_PMIC_WAKEUP_SRC_ALARM |
	    OWL_PMIC_WAKEUP_SRC_REMCON | OWL_PMIC_WAKEUP_SRC_TP |
	    OWL_PMIC_WAKEUP_SRC_WKIRQ | OWL_PMIC_WAKEUP_SRC_SGPIOIRQ;
	if (wakeup_src & ~aux_mask)
		return -EINVAL;

	bitpos = __ffs(wakeup_src);	/* one bit per call. */

	set_bit(bitpos, &s_pmic_pending_aux_wakeup_src_mask);
	if (on) {
		set_bit(bitpos, &s_pmic_pending_aux_wakeup_src_val);
	} else {
		clear_bit(bitpos, &s_pmic_pending_aux_wakeup_src_val);
	}
#endif
	return 0;
}

EXPORT_SYMBOL_GPL(owl_pmic_setup_aux_wakeup_src);

void owl_pm_set_wakeup_src(unsigned int wakesrc_mask, unsigned int wakesrc)
{
	unsigned int val;

	val = ((wakesrc_mask & 0xffff) << 16) | (wakesrc & 0xffff);

	pr_info("%s: set temporary wakeup src (0x%x)\n", __func__, val);

	writel(val, pm_reg_base + OWL_PM_WAKEUP_SRC_REG);
}

void owl_pm_get_wakeup_src(unsigned int *wakesrc_mask, unsigned int *wakesrc)
{
	unsigned int val;

	if (!wakesrc_mask || !wakesrc)
		return;

	val = readl(pm_reg_base + OWL_PM_WAKEUP_SRC_REG);

	*wakesrc_mask = (val >> 16) & 0xffff;
	*wakesrc = val & 0xffff;

	pr_info("%s: get temporary wakeup src (0x%x)\n", __func__, val);
}

void owl_pm_set_flag(unsigned int flag)
{
	unsigned long flags;
	unsigned int val;

	pr_info("%s: set flag 0x%x\n", __func__, flag);

	spin_lock_irqsave(&pm_reg_lock, flags);
	val = readl(pm_reg_base + OWL_PM_FLAG_REG);
	val |= flag;
	writel(val, pm_reg_base + OWL_PM_FLAG_REG);
	spin_unlock_irqrestore(&pm_reg_lock, flags);
}

void owl_pm_clear_flag(unsigned int flag)
{
	unsigned long flags;
	unsigned int val;

	pr_info("%s: clear flag 0x%x\n", __func__, flag);

	spin_lock_irqsave(&pm_reg_lock, flags);
	val = readl(pm_reg_base + OWL_PM_FLAG_REG);
	val &= ~flag;
	writel(val, pm_reg_base + OWL_PM_FLAG_REG);
	spin_unlock_irqrestore(&pm_reg_lock, flags);
}

unsigned int owl_pm_get_flag(unsigned int flag)
{
	unsigned int val;

	val = readl(pm_reg_base + OWL_PM_FLAG_REG);
	val &= flag;

	pr_info("%s: get flag (flag 0x%x, val %d)\n", __func__, flag, val);

	return val;
}

static void owl_pm_set_reboot_tgt(unsigned int tgt)
{
	pr_info("%s: set reboot tgt 0x%x\n", __func__, tgt);
	owl_pm_clear_flag(OWL_PM_FLAG_REBOOT_TGT
			  ((1 << OWL_PM_FLAG_REBOOT_TGT_WIDTH) - 1));
	pr_info("clear flag %d\n", ((1 << OWL_PM_FLAG_REBOOT_TGT_WIDTH) - 1));
	owl_pm_set_flag(OWL_PM_FLAG_REBOOT_TGT(tgt));
}

enum owl_suspend_mode {
	OWL_SUSPEND_NONE = 0,
	OWL_SUSPEND_LP2,	/* CPU voltage off */
	OWL_SUSPEND_LP1,	/* CPU voltage off, DRAM self-refresh */
	OWL_SUSPEND_LP0,	/* CPU + core voltage off, DRAM self-refresh */
	OWL_MAX_SUSPEND_MODE,
};

static const char *owl_suspend_name[OWL_MAX_SUSPEND_MODE] = {
	[OWL_SUSPEND_NONE] = "none",
	[OWL_SUSPEND_LP2] = "lp2",
	[OWL_SUSPEND_LP1] = "lp1",
	[OWL_SUSPEND_LP0] = "lp0",
};

/* default suspend mode is enter S2(OWL_SUSPEND_LP0) */
static enum owl_suspend_mode current_suspend_mode = OWL_SUSPEND_LP0;

static int owl_suspend_valid(suspend_state_t state)
{
	int valid = 1;

	pr_info("%s %d:\n", __func__, __LINE__);

	return valid;
}

static int owl_suspend_prepare_late(void)
{
	pr_info("%s %d:\n", __func__, __LINE__);
	return 0;
}

static void set_suspend_wakup_src(void)
{
	union power_supply_propval ret = { 0, };
	struct power_supply *psy;
	unsigned int wakesrc;
	int wall_in = 0, usb_in = 0;

	wakesrc = OWL_PMIC_WAKEUP_SRC_RESET |
		OWL_PMIC_WAKEUP_SRC_ONOFF_SHORT |
		OWL_PMIC_WAKEUP_SRC_ONOFF_LONG |
		OWL_PMIC_WAKEUP_SRC_ALARM |
		OWL_PMIC_WAKEUP_SRC_IR;

	psy = power_supply_get_by_name("atc260x-wall");
	if (psy && !psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
		wall_in = ret.intval;

	if (!wall_in)
		wakesrc |= OWL_PMIC_WAKEUP_SRC_WALL_IN;

	psy = power_supply_get_by_name("atc260x-usb");
	if (psy && !psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
		usb_in = ret.intval;

	if (!usb_in)
		wakesrc |= OWL_PMIC_WAKEUP_SRC_VBUS_IN;
	else
		wakesrc |= OWL_PMIC_WAKEUP_SRC_VBUS_OUT;

	pr_info("[PM] %s: wall_in(%d) usb_in(%d), suspend wakeup src 0x%x\n",
		__func__, wall_in, usb_in, wakesrc);

	owl_pm_set_wakeup_src(OWL_PMIC_WAKEUP_SRC_ALL, wakesrc);
}

static int owl_do_suspend(suspend_state_t state)
{
	int ret;

	pr_info("%s %d: state %d\n", __func__, __LINE__, state);

	set_suspend_wakup_src();
	ret = cpu_suspend(2);

	pr_info("%s %d: state %d, ret %d\n", __func__, __LINE__, state, ret);

	return ret;
}

static int owl_suspend_enter(suspend_state_t state)
{
	pr_info("%s %d: state %d\n", __func__, __LINE__, state);
	switch (state) {
	case PM_SUSPEND_MEM:
		owl_do_suspend(state);
		break;

	default:
		return -EINVAL;
	}

	pr_info("%s %d: state %d\n", __func__, __LINE__, state);

	return 0;
}

static void owl_suspend_finish(void)
{
	pr_info("%s %d:\n", __func__, __LINE__);
}

static const struct platform_suspend_ops owl_suspend_ops = {
	.prepare_late = owl_suspend_prepare_late,
	.valid = owl_suspend_valid,
	.finish = owl_suspend_finish,
	.enter = owl_suspend_enter,
};

static ssize_t suspend_mode_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	char *start = buf;
	char *end = buf + PAGE_SIZE;

	start += scnprintf(start, end - start, "%s ",
			   owl_suspend_name[current_suspend_mode]);
	start += scnprintf(start, end - start, "\n");

	return start - buf;
}

static ssize_t suspend_mode_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t n)
{
	int len;
	const char *name_ptr;
	enum owl_suspend_mode new_mode;

	name_ptr = buf;
	while (*name_ptr && !isspace(*name_ptr))
		name_ptr++;
	len = name_ptr - buf;
	if (!len)
		goto bad_name;
	/* OWL_SUSPEND_NONE not allowed as suspend state */
	if (!(strncmp(buf, owl_suspend_name[OWL_SUSPEND_NONE], len))
	    || !(strncmp(buf, owl_suspend_name[OWL_SUSPEND_LP2], len))) {
		pr_info("Illegal owl suspend state: %s\n", buf);
		goto bad_name;
	}

	for (new_mode = OWL_SUSPEND_NONE;
	     new_mode < OWL_MAX_SUSPEND_MODE; ++new_mode) {
		if (!strncmp(buf, owl_suspend_name[new_mode], len)) {
			current_suspend_mode = new_mode;
			break;
		}
	}

bad_name:
	return n;
}

static struct kobj_attribute suspend_mode_attribute =
__ATTR(mode, 0644, suspend_mode_show, suspend_mode_store);

static struct kobject *suspend_kobj;

extern void (*pm_power_off) (void);
extern void (*pm_power_reset) (char str, const char *cmd);

static int _pmic_warn_null_cb(void)
{
	pr_err("[PM] owl_pmic_pm_ops not registered!\n");
	return -ENODEV;
}

static int _pmic_warn_null_cb_1ui(uint)
    __attribute__ ((alias("_pmic_warn_null_cb")));
static int _pmic_warn_null_cb_2ui(uint, uint)
    __attribute__ ((alias("_pmic_warn_null_cb")));
static int _pmic_warn_null_cb_3uip(uint *, uint *, uint *)
    __attribute__ ((alias("_pmic_warn_null_cb")));
static struct owl_pmic_pm_ops s_pmic_fallback_pm_ops = {
	.set_wakeup_src = _pmic_warn_null_cb_2ui,
	.get_wakeup_src = _pmic_warn_null_cb,
	.get_wakeup_flag = _pmic_warn_null_cb,
	.shutdown_prepare = _pmic_warn_null_cb,
	.powerdown = _pmic_warn_null_cb_2ui,
	.reboot = _pmic_warn_null_cb_1ui,
	.suspend_prepare = _pmic_warn_null_cb,
	.suspend_enter = _pmic_warn_null_cb,
	.suspend_wake = _pmic_warn_null_cb,
	.suspend_finish = _pmic_warn_null_cb,
	.get_bus_info = _pmic_warn_null_cb_3uip,
};

static struct owl_pmic_pm_ops *s_pmic_pm_ops = &s_pmic_fallback_pm_ops;
static volatile unsigned long s_pmic_pending_aux_wakeup_src_mask = 0;
static volatile unsigned long s_pmic_pending_aux_wakeup_src_val = 0;

/**
 *      pmic_suspend_set_ops - Set the global suspend method table.
 *      @ops:   Pointer to ops structure.
 */
void owl_pmic_set_pm_ops(struct owl_pmic_pm_ops *ops)
{
	pr_info("[PM] set pmic suspend ops 0x%lx\n", (ulong) ops);
	if (ops == NULL || IS_ERR(ops)) {
		s_pmic_pm_ops = &s_pmic_fallback_pm_ops;
	} else {
		s_pmic_pm_ops = ops;
	}
}

EXPORT_SYMBOL_GPL(owl_pmic_set_pm_ops);

int owl_pm_wakeup_flag(void)
{
	return s_pmic_pm_ops->get_wakeup_flag();
}

EXPORT_SYMBOL_GPL(owl_pm_wakeup_flag);

static void owl_pm_power_off(int enter_charge)
{
	int wakeup_src;

	/* default sleep mode and wakeup source */
	/*xyl :alarm wakesource has some problem, so forbidden it until it's OK */
	wakeup_src = OWL_PMIC_WAKEUP_SRC_RESET |
	    OWL_PMIC_WAKEUP_SRC_ONOFF_LONG | OWL_PMIC_WAKEUP_SRC_IR;

	if (enter_charge)
		wakeup_src |= OWL_PMIC_WAKEUP_SRC_WALL_IN |
		    OWL_PMIC_WAKEUP_SRC_VBUS_IN;

	/* Power off system */
	pr_info("[PM] %s: Powering off (wakesrc: 0x%x)\n",
		__func__, wakeup_src);

	owl_pm_set_wakeup_src(OWL_PMIC_WAKEUP_SRC_ALL, wakeup_src);
	if (pm_power_off)
		pm_power_off();

	/* never return to here */
	pr_err("[PM] %s() failed\n", __func__);
	while (1) ;
}

static void owl_pm_halt_upgrade(void)
{
	pr_info("[PM] %s()\n", __func__);
	owl_pm_power_off(0);
}

static void owl_pm_sys_poweroff(void)
{
	union power_supply_propval ret = { 0, };
	struct power_supply *psy;
	int battery_online = 0;

	psy = power_supply_get_by_name("battery");
	if (psy && !psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
		battery_online = ret.intval;

	pr_info("[PM] %s() poweroff, battery online %d\n", __func__,
		battery_online);

	/* if battery exist, we need to enter charger mode */
	if (battery_online) {
		owl_pm_set_flag(OWL_PM_FLAG_BATTERY_ONLINE);
		owl_pm_power_off(1);
	} else {
		owl_pm_power_off(0);
	}
}

static void owl_pm_sys_reset(char str, const char *cmd)
{
	pr_info("[PM] %s() cmd: %s\n", __func__, cmd ? cmd : "<null>");

	if (cmd) {
		if (!strcmp(cmd, "recovery")) {
			pr_info("cmd:%s----restart------\n", cmd);
			owl_pm_set_reboot_tgt(OWL_PMIC_REBOOT_TGT_RECOVERY);
		} else if (!strcmp(cmd, "adfu")) {
			pr_info("cmd:%s----restart------\n", cmd);
			owl_pm_set_reboot_tgt(OWL_PMIC_REBOOT_TGT_ADFU);
		} else if (!strcmp(cmd, "reboot")) {
			pr_info("cmd:%s----restart------\n", cmd);
			owl_pm_set_reboot_tgt(OWL_PMIC_REBOOT_TGT_SYS);	/* no charger */
		} else if (!strcmp(cmd, "upgrade_reboot")) {
			pr_info("cmd:%s----upgrade_reboot------\n", cmd);
			owl_pm_set_flag(OWL_PM_FLAG_RESET_PSTORE);
			owl_pm_set_reboot_tgt(OWL_PMIC_REBOOT_TGT_SYS);	/* no charger */
		} else if (!strcmp(cmd, "upgrade_halt")) {
			pr_info("cmd:%s----halt------\n", cmd);
			owl_pm_set_flag(OWL_PM_FLAG_RESET_PSTORE);
			owl_pm_halt_upgrade();
		} else if (!strcmp(cmd, "bootloader")) {
			pr_info("cmd:%s----restart------\n", cmd);
			owl_pm_set_reboot_tgt(OWL_PMIC_REBOOT_TGT_BOOTLOADER);
		} else if (!strcmp(cmd, "fastboot")) {
			pr_info("cmd:%s----restart------\n", cmd);
			owl_pm_set_reboot_tgt(OWL_PMIC_REBOOT_TGT_FASTBOOT);
		}
	}

	if (pm_power_reset)
		pm_power_reset(str, cmd);

	pr_err("[PM] %s() failed\n", __func__);
}

static int owl_pm_probe(struct platform_device *pdev)
{
	struct resource *res;

	pr_info("[OWL] pm probe\n");
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pm_reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pm_reg_base)) {
		pr_err("get pm_reg_base fail\n");
		return PTR_ERR(pm_reg_base);
	}

	owl_pm_clear_flag(OWL_PM_FLAG_ALL);
	arm_pm_restart = owl_pm_sys_reset;
	arm_pm_poweroff = owl_pm_sys_poweroff;

	return 0;
}

static const struct of_device_id owl_pm_match[] = {
	{.compatible = "actions,s900-pm"},
	{.compatible = "actions,s700-pm"},
	{}
};

static struct platform_driver owl_pm_platform_driver = {
	.driver = {
		   .name = "owl-pm",
		   .owner = THIS_MODULE,
		   .of_match_table = owl_pm_match,
		   },
	.probe = owl_pm_probe,
};

static int __init owl_pm_init(void)
{
	pr_info("[OWL] pm init\n");

	/* create /sys/power/suspend/type */
	suspend_kobj = kobject_create_and_add("suspend", power_kobj);
	if (suspend_kobj) {
		if (sysfs_create_file(suspend_kobj,
				      &suspend_mode_attribute.attr))
			pr_err("%s: sysfs_create_file suspend type failed!\n",
			       __func__);
	}
	suspend_set_ops(&owl_suspend_ops);

	return platform_driver_register(&owl_pm_platform_driver);
}

late_initcall(owl_pm_init);
