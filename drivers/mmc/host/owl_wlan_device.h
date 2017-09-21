#ifndef _OWL_WLAN_DEVICE_H_
#define _OWL_WLAN_DEVICE_H_

/* sdio wifi detect */
extern void owl_wlan_status_check_register(struct owl_mmc_host *host);
extern int owl_wlan_bt_power_init(struct wlan_plat_data *pdata);
extern void owl_wlan_bt_power_release(void);
extern struct platform_device owl_wlan_device;

#endif /* end of _OWL_WLAN_DEVICE_H_ */
