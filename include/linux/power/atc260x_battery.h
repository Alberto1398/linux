#ifndef __ATC260X_BATTERY__
#define __ATC260X_BATTERY__

enum BAT_HEALTH {
	 BAT_NORMAL,
	 BAT_SHORTING,
	 BAT_ABNORMAL_EVER
};


/**
 * struct atc260x_battery_pdata - Platform data for atc260x battery
 * @name: 
 * @read: 
 */
struct atc260x_battery_pdata {
	int health;
	int online;
	int battery_soc;
	int bat_vol;
};

#endif

