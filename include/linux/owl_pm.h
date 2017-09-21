#ifndef _LINUX_OWL_PM_H
#define _LINUX_OWL_PM_H

/* for PMIC operations ------------------------------------------------------ */

/* wakeup sources */
#define OWL_PMIC_WAKEUP_SRC_IR                  (1 << 0)
#define OWL_PMIC_WAKEUP_SRC_RESET               (1 << 1)
#define OWL_PMIC_WAKEUP_SRC_HDSW                (1 << 2)
#define OWL_PMIC_WAKEUP_SRC_ALARM               (1 << 3)
#define OWL_PMIC_WAKEUP_SRC_REMCON              (1 << 4)
#define OWL_PMIC_WAKEUP_SRC_TP                  (1 << 5)  /* 2603a */
#define OWL_PMIC_WAKEUP_SRC_WKIRQ               (1 << 6)  /* 2603a */
#define OWL_PMIC_WAKEUP_SRC_ONOFF_SHORT         (1 << 7)
#define OWL_PMIC_WAKEUP_SRC_ONOFF_LONG          (1 << 8)
#define OWL_PMIC_WAKEUP_SRC_WALL_IN             (1 << 9)
#define OWL_PMIC_WAKEUP_SRC_VBUS_IN             (1 << 10)
#define OWL_PMIC_WAKEUP_SRC_RESTART             (1 << 11)  /* 2603c */
#define OWL_PMIC_WAKEUP_SRC_SGPIOIRQ            (1 << 12)  /* 2603c */
#define OWL_PMIC_WAKEUP_SRC_WALL_OUT            (1 << 13)  /* 2603c */
#define OWL_PMIC_WAKEUP_SRC_VBUS_OUT            (1 << 14)  /* 2603c */
#define OWL_PMIC_WAKEUP_SRC_CNT                 (15)
#define OWL_PMIC_WAKEUP_SRC_ALL                 ((1U<<OWL_PMIC_WAKEUP_SRC_CNT)-1U)

/* reboot target */
#define OWL_PMIC_REBOOT_TGT_NORMAL              (0) /* with charger_check etc. */
#define OWL_PMIC_REBOOT_TGT_SYS                 (1) /* no charger ... */
#define OWL_PMIC_REBOOT_TGT_ADFU                (2)
#define OWL_PMIC_REBOOT_TGT_RECOVERY            (3)
#define OWL_PMIC_REBOOT_TGT_BOOTLOADER          (4)
#define OWL_PMIC_REBOOT_TGT_FASTBOOT            (5)

struct owl_pmic_pm_ops {
	int (*set_wakeup_src)(uint wakeup_mask, uint wakeup_src);
	int (*get_wakeup_src)(void);
	int (*get_wakeup_flag)(void);       /* wakeup reason flag */

        int (*shutdown_prepare_upgrade)(void);
	int (*shutdown_prepare)(void);
	int (*powerdown)(uint deep_pwrdn, uint for_upgrade);
        int (*reboot_prepare_upgrade)(void);
	int (*reboot)(uint tgt);

	int (*suspend_prepare)(void);
	int (*suspend_enter)(void);
	int (*suspend_wake)(void);
	int (*suspend_finish)(void);

	int (*get_bus_info)(uint *bus_num, uint *addr, uint *ic_type);
};

/* for atc260x_pm */
extern void owl_pmic_set_pm_ops(struct owl_pmic_pm_ops *ops);

/* other drivers (IR/TP/REMCON/SGPIO...) can use this API to setup their own wakeup source */
extern int owl_pmic_setup_aux_wakeup_src(uint wakeup_src, uint on);

#endif /* _LINUX_OWL_PM_H */
