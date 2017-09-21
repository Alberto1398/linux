/*
 * hdmi.h
 *
 * HDMI header definition for OWL IP.
 *
 * Copyright (C) 2014 Actions Corporation
 * Author: Guo Long  <guolong@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __CVBS_H
#define __CVBS_H
#include <video/owl_dss.h>

struct cvbs_data {
	struct platform_device  *pdev;
	struct owl_display_ctrl	ctrl;

	void __iomem		*base;
	struct owl_videomode	*cvbs_display_mode;

	int			current_vid;
	bool			is_connected;
	bool                    hpd_en;
	bool			tvout_enabled;

	struct workqueue_struct *wq;
	struct mutex		lock;

	struct work_struct	cvbs_in_work;
	struct work_struct	cvbs_out_work;
	struct delayed_work	cvbs_check_work;

	struct reset_control	*rst;
	struct clk		*cvbs_pll;
	struct clk		*tvout;
	int			irq;
};

#define	CVBS_IN		1
#define	CVBS_OUT	2

#define	OWL_TV_MOD_PAL	0
#define	OWL_TV_MOD_NTSC	1

static void enable_cvbs_output(struct cvbs_data *cvbs);
static void disable_cvbs_output(struct cvbs_data *cvbs);
static void dump_reg(struct cvbs_data *cvbs);
/*cvbs.c*/
void cvbs_fs_dump_regs(struct device *dev);
/*cvbs_sys.c*/
int owl_cvbs_create_sysfs(struct device *dev);

int owl_cvbs_suspend(struct device *dev);
int owl_cvbs_resume(struct device *dev);
static int owl_cvbs_display_disable(struct owl_display_ctrl *ctrl);
static int owl_cvbs_display_enable(struct owl_display_ctrl *ctrl);
#endif

