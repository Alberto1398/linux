/*
 * Actions OWL SoCs usb monitor driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * dengtaiping <dengtaiping@actions-semi.com>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _UMONITOR_CONFIG_H_
#define _UMONITOR_CONFIG_H_
#define  SUPPORT_NOT_RMMOD_USBDRV 1


#define CHECK_TIMER_INTERVAL  1000

#define __GPIO_GROUP(x)     ((x) >> 5)
#define __GPIO_NUM(x)       ((x) & 0x1f)

typedef struct umon_port_config {
#define UMONITOR_DISABLE               0
#define UMONITOR_DEVICE_ONLY           1
#define UMONITOR_HOST_ONLY             2
#define UMONITOR_HOST_AND_DEVICE       3
	unsigned char detect_type;	/* usb port detect request. */
	/* if detect_type == UMONITOR_DISABLE, below is no use. */

	unsigned char port_type;
#define PORT_DWC3            0
#define PORT_USB2            1


#define UMONITOR_USB_IDPIN             0
#define UMONITOR_SOFT_IDPIN            1
#define UMONITOR_GPIO_IDPIN            2  /* gpio detect idpin. */
	unsigned char idpin_type;
	/*
	 * if idpin_type set to UMONITOR_USB_IDPIN or UMONITOR_SOFT_IDPIN,
	 * below is no use.
	 */
	unsigned char idpin_gpio_group;
	unsigned int idpin_gpio_no;

#define UMONITOR_USB_VBUS              0
#define UMONITOR_GPIO_VBUS             1  /* gpio detect vbus. */
#define UMONITOR_DC5V_VBUS             2  /* use dc5v to detect vbus. */
	unsigned char vbus_type;
	unsigned char force_detect; /*11: force b_in; 22 force b_out,33:force a_in,44:force a_out*/
	unsigned char suspend_keep_vbus; /*0: shut down vbus when suspend; 1: keep vbus status when suspend*/
	/*
	 * if vbus_type set to UMONITOR_USB_VBUS, below is no use.
	 */
	unsigned char vbus_gpio_group;
	unsigned int vbus_gpio_no;

	/* in host state, if vbus power switch onoff use gpio, set it. */
	unsigned char power_switch_gpio_group;
	unsigned int power_switch_gpio_no;
	unsigned char power_switch_active_level;
#ifdef SUPPORT_NOT_RMMOD_USBDRV
	/* add a node to receive vold msg ,to open close controlers */
	char usb_con_msg[32];
#endif
    unsigned char idpin_debug;
    unsigned char vbus_debug;
} umon_port_config_t;


#ifdef DEBUG_MONITOR
#define MONITOR_PRINTK(fmt, args...)    pr_info(fmt, ## args)
#else
#define MONITOR_PRINTK(fmt, args...)    /*not printk*/
#endif

#ifdef ERR_MONITOR
#define MONITOR_ERR(fmt, args...)    pr_err(fmt, ## args)
#else
#define MONITOR_ERR(fmt, args...)    /*not printk*/
#endif

extern bool monitor_resume_complete_need_set_noattached(void);
extern void clear_monitor_resume_complete_need_set_noattached(void);

#endif  /* _UMONITOR_CONFIG_H_ */
/*! \endcond*/

