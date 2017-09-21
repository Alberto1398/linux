#ifndef __ATC260X_CHARGER_H__
#define __ATC260X_CHARGER_H__

/* If defined, enable printk. */
#ifdef DEBUG
#define power_dbg(format, arg...) \
	pr_info(format, ## arg)
#else
#define power_dbg(format, arg...) \
	do {} while (0)
#endif

enum ADP_PLUGGED_TYPE {
	NO_PLUGGED = 0,
	WALL_PLUGGED = 1 << 0,
	USB_PLUGGED = 1 << 1
};

enum USB_PLUGGED_TYPE {
	USB_NO_PLUGGED = 0,
	USB_PLUGGED_PC,
	USB_PLUGGED_ADP
};

/*
* Support adapter type as follows :
* 1.two interface:wall plugged only or wall and usb plugged or usb plugged only;
* 2.single usb:usb plugged and wall plugged.
* 3.single dcin:wall plugged only.
*/
enum SUPPORT_ADAPTER {
	SUPPORT_DCIN_ONLY = 0x1,
	SUPPORT_USB_ONLY = 0x2,
	SUPPORT_DCIN_USB = 0x3
};

enum VBUS_CTL_MODE {
	CANCEL_LIMITED,
	CURRENT_LIMITED,
	VOLTAGE_LIMITED
};

enum BATTERY_TYPE {
	BAT_TYPE_4180MV,
	BAT_TYPE_4200MV,
	BAT_TYPE_4300MV,
	BAT_TYPE_4350MV,
};

enum AUXADC_CHAN {
	WALLV_CHAN,
	VBUSV_CHAN,
	BATV_CHAN,
	AUXADC_CHAN_MAX
};

struct charger_dts_items {
	int bl_on_current_usb_pc;
	int bl_off_current_usb_pc;
	int bl_on_current_usb_adp;
	int bl_on_current_usb_adp_high;
	int bl_off_current_usb_adp;
	int bl_on_current_wall_adp;
	int bl_off_current_wall_adp;
	int backlight_on_vol_diff;
	int backlight_off_vol_diff;
	int support_adaptor_type;
	int change_current_temp;
	int ot_shutoff_enable;
	int cur_rise_enable;
	int bat_type;
	int usb_adapter_as_ac;
	int usb_pc_ctl_mode;
	int no_bat_solution;
	int ext_buck_exist;
};

struct atc260x_charger_ops {
	void (*set_onoff) (struct atc260x_dev *atc260x, bool enable);
	bool(*get_onoff) (struct atc260x_dev *atc260x);
	void (*set_vbus_ctl_en) (struct atc260x_dev *atc260x, bool enable);
	bool(*get_vbus_ctl_en) (struct atc260x_dev *atc260x);
	void (*set_vbus_ctlmode) (struct atc260x_dev *atc260x,
				  enum VBUS_CTL_MODE mode);
	int (*get_vbus_ctlmode) (struct atc260x_dev *atc260x);
	int (*get_vbus_vol_lmt) (struct atc260x_dev *atc260x);
	int (*get_vbus_current_lmt) (struct atc260x_dev *atc260x);
	int (*cc_filter) (struct atc260x_dev *atc260x, int cc);
	void (*set_cc) (struct atc260x_dev *atc260x, int cc);
	int (*get_cc) (struct atc260x_dev *atc260x);
	int (*get_trick_current) (struct atc260x_dev *atc260x);
	void (*set_cv) (struct atc260x_dev *atc260x, enum BATTERY_TYPE type);
	void (*set_wall_pd) (struct atc260x_dev *atc260x, bool enable);
	int (*get_vbus_onoff) (struct atc260x_dev *atc260x);
	void (*set_vbus_onoff) (struct atc260x_dev *atc260x, bool enable);
	void (*set_vbus_pd) (struct atc260x_dev *atc260x, bool enable);
	void (*init_base) (struct atc260x_dev *atc260x);
	int (*chk_bat_online) (struct atc260x_dev *atc260x);
	void (*set_syspwr_steady) (struct atc260x_dev *atc260x, bool enable);
	void (*set_otint_en) (struct atc260x_dev *atc260x, bool enable);
	void (*clear_otint_pending) (struct atc260x_dev *atc260x);
};

extern struct charger_dts_items items;
#ifdef CONFIG_CHARGER_ATC2609A
extern struct atc260x_charger_ops atc2609a_charger_ops;
#elif CONFIG_CHARGER_ATC2603C
extern struct atc260x_charger_ops atc2603c_charger_ops;
#endif
#endif
