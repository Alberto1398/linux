/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/8/20: Created by Lipeng.
 */
#define pr_fmt(fmt) "owl_ctrl: %s, " fmt, __func__

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include <video/owl_dss.h>

static LIST_HEAD(g_ctrl_list);
static DEFINE_MUTEX(g_ctrl_list_lock);

int owl_ctrl_init(void)
{
	pr_info("start\n");

	return 0;
}

int owl_ctrl_register(struct owl_display_ctrl *ctrl)
{
	struct owl_display_ctrl *c;

	pr_info("start\n");

	if (ctrl == NULL) {
		pr_err("ctrl is NULL\n");
		return -EINVAL;
	}
	pr_debug("type %d\n", ctrl->type);

	mutex_lock(&g_ctrl_list_lock);

	list_for_each_entry(c, &g_ctrl_list, list) {
		if (c->type == ctrl->type) {
			pr_err("same type ctrl is already registered\n");
			mutex_unlock(&g_ctrl_list_lock);
			return -EBUSY;
		}
	}

	list_add(&ctrl->list, &g_ctrl_list);

	mutex_unlock(&g_ctrl_list_lock);
	return 0;
}

void owl_ctrl_unregister(struct owl_display_ctrl *ctrl)
{
	/* TODO */
}

struct owl_display_ctrl *owl_ctrl_find_by_type(enum owl_display_type type)
{
	struct owl_display_ctrl *c;

	mutex_lock(&g_ctrl_list_lock);

	list_for_each_entry(c, &g_ctrl_list, list) {
		if (c->type == type) {
			mutex_unlock(&g_ctrl_list_lock);
			return c;
		}
	}
	mutex_unlock(&g_ctrl_list_lock);

	return NULL;
}

void owl_ctrl_set_drvdata(struct owl_display_ctrl *ctrl, void *data)
{
	ctrl->data = data;
}

void *owl_ctrl_get_drvdata(struct owl_display_ctrl *ctrl)
{
	return ctrl->data;
}

int owl_ctrl_add_panel(struct owl_panel *panel)
{
	struct owl_display_ctrl *c;

	pr_info("start\n");

	mutex_lock(&g_ctrl_list_lock);

	/* search for a suitable ctrl */
	list_for_each_entry(c, &g_ctrl_list, list) {
		if (c->type == panel->desc.type && c->panel == NULL) {
			pr_debug("got it\n");
			c->panel = panel;
			panel->ctrl = c;

			if (c->ops && c->ops->add_panel)
				c->ops->add_panel(c, panel);

			mutex_unlock(&g_ctrl_list_lock);
			return 0;
		}
	}

	mutex_unlock(&g_ctrl_list_lock);

	pr_err("failed\n");
	return -EINVAL;
}

void owl_ctrl_remove_panel(struct owl_panel *panel)
{
	struct owl_display_ctrl *c;

	pr_info("start\n");

	mutex_lock(&g_ctrl_list_lock);

	/* search for a suitable ctrl */
	list_for_each_entry(c, &g_ctrl_list, list) {
		if (c->panel == panel) {
			pr_debug("got it\n");

			if (c->ops && c->ops->remove_panel)
				c->ops->remove_panel(c, panel);

			c->panel = NULL;
			break;
		}
	}

	mutex_unlock(&g_ctrl_list_lock);
}
int owl_ctrl_power_off(struct owl_display_ctrl *ctrl)
{
	pr_info("ctrl power off ... ...\n");

	if (ctrl->ops && ctrl->ops->power_off)
		ctrl->ops->power_off(ctrl);
	return 0;
}
int owl_ctrl_power_on(struct owl_display_ctrl *ctrl)
{
	pr_info("controller power_on ... ...\n");

	if (ctrl->ops && ctrl->ops->power_on) {
		if (ctrl->ops->power_on(ctrl) < 0) {
			pr_err("controller power_on failed!\n");
			return -1;
		}
	}

	return 0;
}
int owl_ctrl_enable(struct owl_display_ctrl *ctrl)
{
	pr_info("controller enable start\n");

	if (ctrl->ops && ctrl->ops->enable) {
		if (ctrl->ops->enable(ctrl) < 0) {
			pr_err("controller enable failed!\n");
			return -1;
		}
	}

	return 0;
}
void owl_ctrl_disable(struct owl_display_ctrl *ctrl)
{
	pr_info("ctrl disable start\n");

	if (ctrl->ops && ctrl->ops->disable)
		ctrl->ops->disable(ctrl);
}
