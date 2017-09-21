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

#ifndef _UMONITOR_HAL_H_
#define _UMONITOR_HAL_H_

#include "umonitor_config.h"


/* VBUS detection threshold control. reg USB3_P0_CTL [1:0] */
#define VBUS_DET_THRESHOLD_LEVEL0_S700     0x00    /* 4.22v */
#define VBUS_DET_THRESHOLD_LEVEL1_S700     0x01    /* 4.00v */
#define VBUS_DET_THRESHOLD_LEVEL2_S700     0x02    /* 3.65v */
#define VBUS_DET_THRESHOLD_LEVEL3_S700     0x03    /* 3.11v */

/*S900. VBUS detection threshold control. reg USB3_P0_CTL [2:0]*/
#define VBUS_DET_THRESHOLD_MASK_S900     (7<<4)    /*100 4.75v*/
#define VBUS_DET_THRESHOLD_LEVEL0_S900     (0x00<<4)    /*100 4.75v*/
#define VBUS_DET_THRESHOLD_LEVEL1_S900     (0x01<<4)    /*4.45v*/
#define VBUS_DET_THRESHOLD_LEVEL2_S900     (0x02<<4)    /*4.00v*/
#define VBUS_DET_THRESHOLD_LEVEL3_S900     (0x03<<4)    /*3.65v*/
#define VBUS_DET_THRESHOLD_LEVEL4_S900     (0x04<<4)    /*3.00v*/


struct usb3_p0_ctl {
	unsigned int VBUS_P0;
	unsigned int ID_P0;
	unsigned int DPPUEN_P0;
	unsigned int DMPUEN_P0;
	unsigned int DPPDDIS_P0;
	unsigned int DMPDDIS_P0;
	unsigned int SOFTIDEN_P0;
	unsigned int SOFTID_P0;
	unsigned int SOFTVBUSEN_P0;
	unsigned int SOFTVBUS_P0;
	unsigned int LS_P0_SHIFT;
	unsigned int LS_P0_MASK;
	unsigned int ECS_DPPDDIS;
	unsigned int ECS_DMPDDIS;
	unsigned int ECS_DPPUEN;
	unsigned int ECS_DMPUEN;
	unsigned int VBUS_DET_THRESHOLD;
};

struct monitor_data {
	int ic_type;
	phys_addr_t io_base;	/* USB3_REGISTER_BASE */
	phys_addr_t usbecs;	/* USB3_ECS */
	struct usb3_p0_ctl usb3_p0_ctl;	/* USB3_P0_CTL */
};

typedef struct usb_hal_monitor {
	char *name;
	unsigned int usbecs_val;
	void __iomem *io_base;
	void __iomem *usbecs;
	struct usb3_p0_ctl usb3_p0_ctl;
	umon_port_config_t *config;

	int ic_type;
	int (*vbus_power_onoff)(struct usb_hal_monitor *pdev, int is_on);

#define USB_DC5V_LOW               0
#define USB_DC5V_HIGH              1
#define USB_DC5V_INVALID           2
	int (*get_dc5v_state)(struct usb_hal_monitor *pdev);

#define USB_VBUS_LOW               0
#define USB_VBUS_HIGH              1
#define USB_VBUS_INVALID           2
	int (*get_vbus_state)(struct usb_hal_monitor *pdev);

	/* return state of linestate[1:0]. */
	unsigned int (*get_linestates)(struct usb_hal_monitor *pdev);

#define USB_ID_STATE_INVALID    0
#define USB_ID_STATE_DEVICE     1
#define USB_ID_STATE_HOST       2
	int (*get_idpin_state)(struct usb_hal_monitor *pdev);

	int (*set_dp_500k_15k)(struct usb_hal_monitor *pdev, int enable_500k_up, int enable_15k_down);
	int (*set_soft_id)(struct usb_hal_monitor *pdev, int en_softid, int id_state);
	int (*set_soft_vbus)(struct usb_hal_monitor *pdev, int en_softvbus, int vbus_state);

	int (*aotg_enable)(struct usb_hal_monitor *pdev, int enable);

	/* used for enter certain otg states. */
#define USB_IN_DEVICE_MOD   1
#define USB_IN_HOST_MOD     0
	int (*set_mode)(struct usb_hal_monitor *pdev, int mode);
	void (*dwc_set_mode)(struct usb_hal_monitor *pdev, int mode);
	void (*dp_up)(struct usb_hal_monitor *pdev);
	void (*dp_down)(struct usb_hal_monitor *pdev);
	int (*is_sof)(struct usb_hal_monitor *pdev);
	int (*enable_irq)(struct usb_hal_monitor *pdev, int enable);
	int (*suspend_or_resume)(struct usb_hal_monitor *pdev, int is_suspend);
	void (*dwc3_otg_mode_cfg)(struct usb_hal_monitor *pdev);
	int (*monitor_get_usb_plug_type)(struct usb_hal_monitor *pdev);
	void (*dwc3_otg_init)(struct usb_hal_monitor *pdev);
	void (*dwc3_otg_exit)(struct usb_hal_monitor *pdev);
	void (*debug)(struct usb_hal_monitor *pdev);
} usb_hal_monitor_t;

int usb_hal_init_monitor_hw_ops(usb_hal_monitor_t *pdev, umon_port_config_t *config, void __iomem  *base, struct monitor_data *monitor_data);

/* todo */

#endif  /* _UMONITOR_HAL_H_ */
/*! \endcond*/
