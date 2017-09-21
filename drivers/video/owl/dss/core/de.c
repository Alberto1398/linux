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
 *	2015/8/21: Created by Lipeng.
 */
#define DEBUGX
#define pr_fmt(fmt) "owl_de: %s, " fmt, __func__

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>

#include <video/owl_dss.h>

/*
 * a test definition to switch on/off
 * gamma/histogram functions
 */
#define OWL_DE_GAMMA_ENABLE

/*
 * Wait DE's VB flag after receiving preline interrupt,
 * if panel's preline time(us) > OWL_DE_WAIT_VB_THRESHOLD,
 * it's more effective that wait VB in work.
 * or it's more effective that wait VB in IRQ handler.
 */
#define OWL_DE_WAIT_VB_THRESHOLD	(300)	/* us */

#define OWL_DE_KEEP_PRLINE_IRQ_ON

static struct owl_de_device		*g_cur_de;

static int				de_hscaler_min;

static int				de_gamma_test = 0;
static int				is_gamma_opening = 0;

#ifdef OWL_DE_KEEP_PRLINE_IRQ_ON
static int				vsync_always_on = 1;
#else
static int				vsync_always_on;
#endif

/*=============================================================================
			external functions for others
 *===========================================================================*/

static int owl_de_debugfs_create(struct owl_de_device *de);
static irqreturn_t de_irq_handler(int irq, void *data);
static void de_path_vsync_work_func(struct work_struct *work);

/* some static initialization for DE device */
static void de_device_init(struct owl_de_device *de)
{
	int i;
	struct owl_de_path *path;
	struct owl_de_video *video;

	de->state = OWL_DSS_STATE_OFF;
	mutex_init(&de->state_mutex);

	for (i = 0; i < de->num_paths; i++) {
		path = &de->paths[i];

		path->dirty = false;
		INIT_LIST_HEAD(&path->videos);
		INIT_WORK(&path->vsync_work, de_path_vsync_work_func);
		atomic_set(&path->vsync_enable_cnt, 0);
		init_waitqueue_head(&path->vsync_wq);
	}

	for (i = 0; i < de->num_videos; i++) {
		video = &de->videos[i];

		video->path = NULL;
		video->dirty = false;
	}
}

/* for simple, let DE be powered on after probing */
static void de_power_on_init(struct owl_de_device *de)
{
	pm_runtime_get_sync(&de->pdev->dev);
	mdelay(1);

	if (!IS_ERR(de->rst)) {
		reset_control_deassert(de->rst);
		mdelay(1);
	}

	if (de->ops && de->ops->power_on)
		de->ops->power_on(de);

	de->state = OWL_DSS_STATE_ON;
}

int owl_de_register(struct owl_de_device *de)
{
	int ret = 0, tmp, i;

	struct resource			*res;
	struct platform_device		*pdev;

	pr_info("start\n");

	if (de == NULL || de->pdev == NULL) {
		pr_err("de or de->pdev is NULL\n");
		return -EINVAL;
	}

	if (g_cur_de != NULL) {
		pr_err("another de is already registered\n");
		return -EEXIST;
	}

	pdev = de->pdev;

	/*
	 * get resources
	 */

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		pr_err("get 'regs' error\n");
		return -EINVAL;
	}
	de->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(de->base)) {
		pr_err("can't remap IORESOURCE_MEM\n");
		return -EINVAL;
	}
	pr_debug("base: 0x%p\n", de->base);

	de->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(de->rst)) {
		pr_err("can't get the reset\n");
		return PTR_ERR(de->rst);
	}

	de->irq = platform_get_irq(pdev, 0);
	if (de->irq < 0) {
		pr_err("can't get irq\n");
		return de->irq;
	}

	if (devm_request_irq(&pdev->dev, de->irq, de_irq_handler,
			     0, dev_name(&pdev->dev), de)) {
		pr_err("request irq(%d) failed\n", de->irq);
		return -EFAULT;
	}

	g_cur_de = de;
	dev_set_drvdata(&pdev->dev, de);

	/* parse 'is_ott' property from DTS */
	if (of_property_read_u32(pdev->dev.of_node, "is_ott", &tmp))
		de->is_ott = false;
	else
		de->is_ott = (tmp == 1 ? true : false);

	/* parse 'gamma_adjust_needed' property from DTS */
	ret = of_property_read_u32(pdev->dev.of_node,
					"gamma_adjust_needed", &tmp);
	if (ret < 0)
		tmp = 0;
	/* initial gamma state, de gamma is or not open config from dts */
	for (i = 0; i < de->num_paths; i++)
			de->paths[i].info.gamma_adjust_needed = tmp;
	is_gamma_opening = tmp;
	de_gamma_test = tmp;

	de_device_init(de);

	pm_runtime_enable(&pdev->dev);

	de_power_on_init(de);
	/*
	 * until these point, DE registers can be access.
	 */

	if (owl_de_mmu_init(&pdev->dev) < 0) {
		pr_err("DE MMU init failed\n");
		return ret;
	}

	owl_de_debugfs_create(de);
	return 0;
}
EXPORT_SYMBOL(owl_de_register);

int owl_de_get_path_num(void)
{
	return g_cur_de->num_paths;
}
EXPORT_SYMBOL(owl_de_get_path_num);

int owl_de_get_video_num(void)
{
	return g_cur_de->num_videos;
}
EXPORT_SYMBOL(owl_de_get_video_num);

bool owl_de_is_s700(void)
{
	return g_cur_de->hw_id == DE_HW_ID_S700;
}
EXPORT_SYMBOL(owl_de_is_s700);

bool owl_de_is_s900(void)
{
	return g_cur_de->hw_id == DE_HW_ID_S900;
}
EXPORT_SYMBOL(owl_de_is_s900);

/*
 * for OTT, HDMI only.
 * however, only some chip(such as S700) care about it.
 * for others, just skip it.
 */
bool owl_de_is_ott(void)
{
	return g_cur_de->is_ott;
}
EXPORT_SYMBOL(owl_de_is_ott);

int owl_de_mmu_config(uint32_t base_addr)
{
	if (g_cur_de->ops->mmu_config)
		return g_cur_de->ops->mmu_config(g_cur_de, base_addr);

	return 0;
}
EXPORT_SYMBOL(owl_de_mmu_config);

int owl_de_generic_suspend(struct device *dev)
{
	int i;

	struct owl_de_device *de;

	de = dev_get_drvdata(dev);
	if (de == NULL) {
		pr_err("de is NULL\n");
		return -EINVAL;
	}
	pr_info("de->state = %d\n", de->state);

	/*
	 * de->state will be updated, hold state_mutex
	 * during all this process, to avoid accident.
	 */
	mutex_lock(&de->state_mutex);

	if (de->state == OWL_DSS_STATE_SUSPENDED)
		goto out;

	/* gamma suspend states differ from gamma on/off states TODO */
	is_gamma_opening = !is_gamma_opening;
	/* make sure all paht's gamma is disabled */
	for (i = 0; i < de->num_paths; i++) {
		if (de->paths[i].info.gamma_adjust_needed)
			if (de->paths[i].ops->gamma_enable)
				de->paths[i].ops->gamma_enable(&de->paths[i], false);
	}

	/* make sure all path's preline IRQ is disabled */
	for (i = 0; i < de->num_paths; i++)
		de->paths[i].ops->preline_enable(&de->paths[i], false);

	if (de->ops && de->ops->backup_regs)
		de->ops->backup_regs(de);

	if (de->ops && de->ops->power_off)
		de->ops->power_off(de);

	if (!IS_ERR(de->rst))
		reset_control_assert(de->rst);

	pm_runtime_put_sync(dev);

	de->state = OWL_DSS_STATE_SUSPENDED;

out:
	mutex_unlock(&de->state_mutex);
	return 0;
}
EXPORT_SYMBOL(owl_de_generic_suspend);

int owl_de_generic_resume(struct device *dev)
{
	int i;

	struct owl_de_device *de;

	de = dev_get_drvdata(dev);
	if (de == NULL) {
		pr_err("de is NULL\n");
		return -EINVAL;
	}
	pr_info("de->state = %d\n", de->state);

	/*
	 * de->state will be updated, hold state_mutex
	 * during all this process, to avoid accident.
	 */
	mutex_lock(&de->state_mutex);

	if (de->state != OWL_DSS_STATE_SUSPENDED)
		goto out;

	pm_runtime_get_sync(dev);
	mdelay(1);

	if (!IS_ERR(de->rst)) {
		reset_control_deassert(de->rst);
		mdelay(1);
	}

	if (de->ops && de->ops->power_on)
		de->ops->power_on(de);

	if (de->ops && de->ops->restore_regs)
		de->ops->restore_regs(de);

	de->state = OWL_DSS_STATE_ON;

	/* resume all path's preline IRQ */
	for (i = 0; i < de->num_paths; i++) {
	#ifndef OWL_DE_KEEP_PRLINE_IRQ_ON
		if (atomic_read(&de->paths[i].vsync_enable_cnt) > 0)
	#endif
			de->paths[i].ops->preline_enable(&de->paths[i], true);
	}

out:
	mutex_unlock(&de->state_mutex);
	return 0;
}
EXPORT_SYMBOL(owl_de_generic_resume);

/*
 * functions for de path
 */

struct owl_de_path *owl_de_path_get_by_type(enum owl_display_type type)
{
	struct owl_de_path *path = NULL;
	int i;

	pr_debug("type = %d\n", type);

	for (i = 0; i < owl_de_get_path_num(); i++) {
		if ((g_cur_de->paths[i].supported_displays & type) != 0) {
			path = &g_cur_de->paths[i];
			break;
		}
	}

	return path;
}
EXPORT_SYMBOL(owl_de_path_get_by_type);

struct owl_de_path *owl_de_path_get_by_id(int id)
{
	struct owl_de_path *path = NULL;
	int i;

	pr_debug("id = %d\n", id);

	for (i = 0; i < owl_de_get_path_num(); i++) {
		if (g_cur_de->paths[i].id == id) {
			path = &g_cur_de->paths[i];
			break;
		}
	}

	return path;
}
EXPORT_SYMBOL(owl_de_path_get_by_id);

struct owl_de_path *owl_de_path_get_by_index(int index)
{
	pr_debug("index = %d\n", index);

	if (index >= owl_de_get_path_num())
		return NULL;
	else
		return &g_cur_de->paths[index];
}
EXPORT_SYMBOL(owl_de_path_get_by_index);

int owl_de_path_enable(struct owl_de_path *path)
{
	int ret = 0;
	struct owl_de_video *video;
	static is_first_enable = true;

	pr_debug("path %d\n", path->id);
	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;
	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state == OWL_DSS_STATE_ON) {
		/* restore the attach status for all its videos */
		list_for_each_entry(video, &path->videos, list)
			path->ops->attach(path, video);

		path->ops->enable(path, true);
		path->ops->set_fcr(path);

	#ifdef OWL_DE_KEEP_PRLINE_IRQ_ON
		path->ops->preline_enable(path, true);
	#endif

		/*
		 * Attention, for s700, gamma table must set after path enable
		 * if using gamma functions, or write register will die.
		 *
		 * Gamma will take effect in VB time, in kernel stage.
		 *
		 * 'is_gamma_opening != de_gamma_test' means suspend/resume ops.
		 * 'is_first_enable' is used as that set_gamma_table once.
		 *
		 * This code may be put in another position? TODO
		 * */
		if (path->info.gamma_adjust_needed) {
			if (is_gamma_opening != de_gamma_test || is_first_enable) {
				if (path->ops->set_gamma_table)
					path->ops->set_gamma_table(path);
				is_first_enable = false;
			}
		}
	}

	mutex_unlock(&g_cur_de->state_mutex);
	return ret;
}
EXPORT_SYMBOL(owl_de_path_enable);

bool owl_de_path_is_enabled(struct owl_de_path *path)
{
	bool enabled = false;

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return false;

	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state == OWL_DSS_STATE_ON)
		enabled = path->ops->is_enabled(path);

	mutex_unlock(&g_cur_de->state_mutex);

	pr_debug("path %d, enabled %d\n", path->id, enabled);
	return enabled;
}

int owl_de_path_disable(struct owl_de_path *path)
{
	int ret = 0;

	pr_debug("path %d\n", path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state == OWL_DSS_STATE_ON) {
		/*
		 * before path disabling, detach all its video layers,
		 * and wait enough time to make sure it can take effect.
		 *
		 * NOTE: path disabling should be called before panel disabling,
		 */
		path->ops->detach_all(path);
		path->ops->set_fcr(path);
		msleep(50);

	#ifdef OWL_DE_KEEP_PRLINE_IRQ_ON
		path->ops->preline_enable(path, false);
	#endif

		ret = path->ops->enable(path, false);
	}


	mutex_unlock(&g_cur_de->state_mutex);
	return ret;
}
EXPORT_SYMBOL(owl_de_path_disable);

static void __de_path_enable_vsync(struct owl_de_path *path, bool enable)
{
#ifndef OWL_DE_KEEP_PRLINE_IRQ_ON
	if (enable && atomic_inc_return(&path->vsync_enable_cnt) == 1)
		path->ops->preline_enable(path, true);
	else if (!enable && vsync_always_on == 0 &&
		 atomic_dec_and_test(&path->vsync_enable_cnt))
		path->ops->preline_enable(path, false);
	/*TODO*/
	if (atomic_read(&path->vsync_enable_cnt) < 0)
		atomic_set(&path->vsync_enable_cnt, 0);
#endif
}

int owl_de_path_enable_vsync(struct owl_de_path *path)
{
#ifndef OWL_DE_KEEP_PRLINE_IRQ_ON
	pr_debug("path %d, vsync_enable_cnt %d\n",
		 path->id, atomic_read(&path->vsync_enable_cnt));

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state == OWL_DSS_STATE_ON)
		__de_path_enable_vsync(path, true);

	mutex_unlock(&g_cur_de->state_mutex);
#endif
	return 0;
}
EXPORT_SYMBOL(owl_de_path_enable_vsync);

int owl_de_path_disable_vsync(struct owl_de_path *path)
{
#ifndef OWL_DE_KEEP_PRLINE_IRQ_ON
	pr_debug("path %d, vsync_enable_cnt %d\n",
		 path->id, atomic_read(&path->vsync_enable_cnt));

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;

	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state == OWL_DSS_STATE_ON)
		__de_path_enable_vsync(path, false);

	mutex_unlock(&g_cur_de->state_mutex);
#endif
	return 0;
}
EXPORT_SYMBOL(owl_de_path_disable_vsync);

/*
 * Attach/Detach DE video @video to DE path @path.
 * Return 0 on success or error code(<0) on failure.
 */
int owl_de_path_attach(struct owl_de_path *path, struct owl_de_video *video)
{
	pr_debug("attach video%d to path%d\n", video->id, path->id);

	if (video->path != NULL && video->path != path) {
		pr_err("already attached to another path(id = %d)\n",
		       video->path->id);
		return -EBUSY;
	}

	if (video->path == path)
		return 0;

	video->path = path;
	list_add_tail(&video->list, &path->videos);
	path->dirty = true;

	return 0;
}
EXPORT_SYMBOL(owl_de_path_attach);

int owl_de_path_detach(struct owl_de_path *path, struct owl_de_video *video)
{
	pr_debug("detach video%d from path%d\n", video->id, path->id);

	if (video->path == NULL || video->path != path) {
		pr_err("video is not attached on path\n");
		return -EINVAL;
	}

	video->path = NULL;
	list_del(&video->list);
	path->dirty = true;

	return 0;
}
EXPORT_SYMBOL(owl_de_path_detach);

/* detach all videos from 'path', temp in here, TODO */
int owl_de_path_detach_all(struct owl_de_path *path)
{
	struct owl_de_video *video, *next;

	pr_debug("detach all videos from path%d\n", path->id);

	list_for_each_entry_safe(video, next, &path->videos, list) {
		if (video->path == NULL || video->path != path)
			continue;

		mutex_lock(&g_cur_de->state_mutex);

		/*
		 * write register, which will take action after
		 * the nect FCR being written.
		 */
		if (g_cur_de->state == OWL_DSS_STATE_ON)
			path->ops->detach(path, video);

		mutex_unlock(&g_cur_de->state_mutex);

		video->path = NULL;
		list_del(&video->list);
		path->dirty = true;
	}

	return 0;
}
EXPORT_SYMBOL(owl_de_path_detach_all);

void owl_de_path_get_info(struct owl_de_path *path,
			  struct owl_de_path_info *info)
{
	pr_debug("path %d\n", path->id);
	memcpy(info, &path->info, sizeof(struct owl_de_path_info));
}
EXPORT_SYMBOL(owl_de_path_get_info);

void owl_de_path_set_info(struct owl_de_path *path,
			  struct owl_de_path_info *info)
{
	pr_debug("path %d\n", path->id);

	memcpy(&path->info, info, sizeof(struct owl_de_path_info));
	path->dirty = true;
}
EXPORT_SYMBOL(owl_de_path_set_info);

void owl_de_path_apply(struct owl_de_path *path)
{
	bool need_set_go = false, need_mmu = false;

	/*
	 * For the case that MMU from disable to enable,
	 * If skip_for_mmu_cnt is 0, disable video layers, using default color,
	 * and do not enable MMU.
	 * if skip_for_mmu_cnt is 1, enable MMU.
	 * if skip_for_mmu_cnt is 2, enable video layers.
	 */
	static int skip_for_mmu_cnt = 0;

	struct owl_de_video *video;

	pr_debug("path%d\n", path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return;

	/*
	 * ensure de->state is not modified
	 * during registers accessing proccess
	 */
	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state != OWL_DSS_STATE_ON)
		goto out;

	/* check if MMU will be enabled */
	list_for_each_entry(video, &path->videos, list) {
		if (video->info.mmu_enable) {
			need_mmu = true;

			/*
			 * when skip_for_mmu_cnt == 0, disable video layer,
			 * and do not enable MMU
			 */
			if (skip_for_mmu_cnt == 0)
				video->info.mmu_enable = false;
			else
				break;
		}
	}

	if (path->dirty) {
		need_set_go = true;

		/* apply path info */
		path->ops->apply_info(path);

		/*
		 * re-attach all videos
		 * NOTE: in such case, for example, detach video2
		 *	from path0, and attach it to path1.
		 *	detach must take action before attach,
		 *	or the displaying in path 1 will blur. FIXME
		 */
		path->ops->detach_all(path);

		if (path->ops->is_enabled(path)) {
			/* only attach the enabled path */

			if (need_mmu && skip_for_mmu_cnt < 2) {
				skip_for_mmu_cnt++;
			} else {
				list_for_each_entry(video, &path->videos, list)
					path->ops->attach(path, video);
			}
		}

		path->dirty = false;
	}

	/* apply for videos */
	list_for_each_entry(video, &path->videos, list) {
		if (video->dirty) {
			need_set_go = true;

			video->ops->apply_info(video);
			video->dirty = false;
		}
	}

	if (need_set_go)
		path->ops->set_fcr(path);

out:
	mutex_unlock(&g_cur_de->state_mutex);
}
EXPORT_SYMBOL(owl_de_path_apply);

void owl_de_path_wait_for_go(struct owl_de_path *path)
{
	bool vb_timeout = false;
	int ret = 0, i;
	int wait_cnt;
	int frame_period_ns;

	/* the real panel */
	struct owl_panel *panel = path->panel;

	pr_debug("path%d\n", path->id);

	if (panel->state != OWL_DSS_STATE_ON) {
		pr_debug("do not wait for inactive panels\n");
		return;
	}

	/*
	 * ensure de->state is not modified
	 * during registers accessing proccess
	 */
	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state != OWL_DSS_STATE_ON)
		goto out;

	frame_period_ns = owl_panel_get_refresh_rate(panel);
	if (frame_period_ns == 0)
		frame_period_ns = 1000 * 1000 * 30;
	else
		frame_period_ns = 1000 * 1000 * 1000 / frame_period_ns;

	if (owl_de_is_s700() && path->id == 0) {
		/*
		 * alwful trick for S700 HDMI & CVBS, because HDMI and CVBS
		 * share the same path, the same IRQ bit, we can only wait
		 * for the first one.
		 */
		for (i = 0; i < owl_de_get_path_num(); i++) {
			if (g_cur_de->paths[i].id == 0) {
				path = &g_cur_de->paths[i];
				break;
			}
		}
	}

	if (owl_de_is_s900() || PANEL_IS_PRIMARY(panel)) {
		__de_path_enable_vsync(path, true);
		ret = wait_event_hrtimeout(path->vsync_wq,
				(path->ops->is_vb_valid(path) ||
				 !path->ops->is_fcr_set(path)),
				 ns_to_ktime(frame_period_ns));
		__de_path_enable_vsync(path, false);

		if (ret == 0) {
			pr_debug("wait VB valid timeout\n");
			vb_timeout = true;
		}
	}

	pr_debug("wait FCR de-active\n");

	if (vb_timeout)
		wait_cnt = 10;	/* timeout, 1.5ms~2.5ms */
	else
		wait_cnt = 200;	/* timeout, 30ms~50ms */

	while (wait_cnt && path->ops->is_fcr_set(path)) {
		wait_cnt--;
		usleep_range(150, 250);
	}

	if (path->ops->is_fcr_set(path))
		pr_debug("wait FCR de-active timeout\n");

	pr_debug("wait FCR de-active, done\n");

out:
	mutex_unlock(&g_cur_de->state_mutex);
}
EXPORT_SYMBOL(owl_de_path_wait_for_go);

/* assign panel to the special DE path */
int owl_de_path_add_panel(struct owl_panel *panel)
{
	int i;
	struct owl_de_path *path;

	for (i = 0; i < owl_de_get_path_num(); i++) {
		path = &g_cur_de->paths[i];

		if (path->panel != NULL && path->panel != panel)
			continue;

		if ((PANEL_TYPE(panel) & path->supported_displays) != 0) {
			panel->path = path;
			path->panel = panel;

			return 0;
		}
	}

	return -ENODEV;
}
EXPORT_SYMBOL(owl_de_path_add_panel);

int owl_de_path_remove_panel(struct owl_panel *panel)
{
	int i;
	struct owl_de_path *path;

	for (i = 0; i < owl_de_get_path_num(); i++) {
		path = &g_cur_de->paths[i];

		if (path->panel == panel) {
			panel->path = NULL;
			path->panel = NULL;
			return 0;
		}
	}

	return -ENODEV;
}
EXPORT_SYMBOL(owl_de_path_remove_panel);

/*
 * video functions
 */

struct owl_de_video *owl_de_video_get_by_id(int id)
{
	struct owl_de_video *video = NULL;
	int i;

	pr_debug("id = %d\n", id);

	for (i = 0; i < owl_de_get_video_num(); i++) {
		if (g_cur_de->videos[i].id == id) {
			video = &g_cur_de->videos[i];
			break;
		}
	}

	return video;
}
EXPORT_SYMBOL(owl_de_video_get_by_id);

struct owl_de_video *owl_de_video_get_by_index(int index)
{
	pr_debug("index = %d\n", index);

	if (index < 0 || index >= owl_de_get_video_num())
		return NULL;
	else
		return &g_cur_de->videos[index];
}
EXPORT_SYMBOL(owl_de_video_get_by_index);

void owl_de_video_get_info(struct owl_de_video *video,
			   struct owl_de_video_info *info)
{
	pr_debug("video %d\n", video->id);
	memcpy(info, &video->info, sizeof(struct owl_de_video_info));
}
EXPORT_SYMBOL(owl_de_video_get_info);

void owl_de_video_set_info(struct owl_de_video *video,
			   struct owl_de_video_info *info)
{
	pr_debug("video %d\n", video->id);

	memcpy(&video->info, info, sizeof(struct owl_de_video_info));
	video->dirty = true;
}
EXPORT_SYMBOL(owl_de_video_set_info);

int owl_de_video_info_validate(struct owl_de_video *video,
			       struct owl_de_video_info *info)
{
	int plane;
	int tmp;

	const struct owl_de_video_capacities *caps;
	const struct owl_de_video_crop_limits *crop_limits;

	#define ERR_OUT(errno) \
		do { \
			pr_debug("failed(%d)\n", (errno)); \
			return errno; \
		} while (0)

	caps = &video->capacities;

	if (info == NULL)
		ERR_OUT(-1);

	if ((info->color_mode & caps->supported_colors) == 0)
		ERR_OUT(-2);

	if (info->n_planes > 3)
		ERR_OUT(-3);	/* TODO */

	if (owl_de_is_s900() && info->blending == OWL_BLENDING_PREMULT &&
	    info->alpha != 255)
		ERR_OUT(-4);

	if (owl_dss_color_is_rgb(info->color_mode))
		crop_limits = &caps->rgb_limits;
	else
		crop_limits = &caps->yuv_limits;

	if (info->is_original_scaled)
	    pr_debug("%dx%d->%dx%d, crop_limits->scaling_width.min = %d\n", info->width, info->height,
		 info->out_width, info->out_height, crop_limits->scaling_width.min);

	if (info->width < crop_limits->input_width.min ||
	    info->width > crop_limits->input_width.max ||
	    info->height < crop_limits->input_height.min ||
	    info->height > crop_limits->input_height.max ||
	    info->out_width < crop_limits->output_width.min ||
	    info->out_width > crop_limits->output_width.max ||
	    info->out_height < crop_limits->output_height.min ||
	    info->out_height > crop_limits->output_height.max)
		ERR_OUT(-5);

	/* NOTE: check this video layer scaling capacity, min scaling_width not equal 80 means have no scaler*/
	if (owl_de_is_s700() && (crop_limits->scaling_width.min != 80) &&
	        info->is_original_scaled)
		ERR_OUT(-12);

	/* scaling */
	if (info->width < info->out_width) {
		if (info->out_width * 10 >
		    info->width * crop_limits->scaling_width.max)
			ERR_OUT(-6);
	} else {
		if (info->width * 10 >
		    info->out_width * crop_limits->scaling_width.min)
			ERR_OUT(-7);
	}
	if (info->height < info->out_height) {
		if (info->out_height * 10 >
		    info->height * crop_limits->scaling_height.max)
			ERR_OUT(-8);
	} else {
		if (de_hscaler_min > 0)		/* testing */
			tmp = de_hscaler_min;
		else if (info->height >= 2160)	/* specail for 4K, 1.5 */
			tmp = 15;
		else if (info->height > 1080)	/* above 1920x1080, 2.5 */
			tmp = 25;
		else
			tmp = crop_limits->scaling_height.min;

		pr_debug("%d / %d = %d\n", info->height, info->out_height, tmp);
		if (info->height * 10 > info->out_height * tmp)
			ERR_OUT(-9);
	}

	for (plane = 0; plane < info->n_planes; plane++) {
		if ((info->pitch[plane] % caps->pitch_align) != 0)
			ERR_OUT(-10);

		if ((info->addr[plane] % caps->address_align) != 0)
			ERR_OUT(-11);
	}

	return 0;

	#undef ERR_OUT
}
EXPORT_SYMBOL(owl_de_video_info_validate);

/*=============================================================================
				DE irq handler
 *===========================================================================*/

/*
 * wait vb valid, and send vsync event.
 * also, check if we can continue to deal with gamma,
 * if yes, return true, or return false.
 *
 * NOTE: this interface maybe called in IEQ context
 */
static bool __de_path_wait_vb_valid(struct owl_de_path *path)
{
	bool is_safe_for_gamma = false;
	int ret;

	/* already valid, not safe for gamma */
	if (path->ops->is_vb_valid(path))
		goto send_vsync_and_out;

	pr_debug("wait VB valid\n");
	ret = WAIT_WITH_TIMEOUT(path->ops->is_vb_valid(path),
				owl_panel_get_preline_time(path->panel));

	/* wait timeout, not safe for gamma */
	if (ret == 0) {
		pr_debug("wait VB valid, timeout\n");
		goto send_vsync_and_out;
	}

	/* got VB */

#ifdef OWL_DE_GAMMA_ENABLE
	/* do not deal with gamma for dummy or hdmi device */
	if (path->info.type != OWL_DISPLAY_TYPE_DUMMY &&
	    path->info.type != OWL_DISPLAY_TYPE_HDMI)
		is_safe_for_gamma = true;

	if (path->info.gamma_adjust_needed) {
		if (path->ops->gamma_enable)
			path->ops->gamma_enable(path, de_gamma_test);

		if (is_gamma_opening != de_gamma_test)
			is_gamma_opening = de_gamma_test;

	}
#endif

send_vsync_and_out:
	owl_panel_vsync_notify(path->panel);

	wake_up(&path->vsync_wq);

	return is_safe_for_gamma;
}

static void de_path_vsync_work_func(struct work_struct *work)
{
	struct owl_de_path *path;

	path = container_of(work, struct owl_de_path, vsync_work);

	pr_debug("time from irq handler to vsync work is %lld us\n",
		 ktime_to_us(ktime_sub(ktime_get(), path->vsync_stamp)));

	mutex_lock(&g_cur_de->state_mutex);

	if (g_cur_de->state != OWL_DSS_STATE_ON)
		goto no_safe_vb;

	if (!__de_path_wait_vb_valid(path))
		goto no_safe_vb;

	/*
	 * got VB, can do other things, such as gamma. TODO
	 */

no_safe_vb:
	mutex_unlock(&g_cur_de->state_mutex);
	return;
}

static irqreturn_t de_irq_handler(int irq, void *data)
{
	int i;
	int preline_time;

	irqreturn_t ret = IRQ_NONE;

	struct owl_de_device *de = data;
	struct owl_de_path *path;

	if (g_cur_de->state != OWL_DSS_STATE_ON)
		return ret;

	/* poll all path's irq status */
	for (i = 0; i < de->num_paths; i++) {
		path = &de->paths[i];

		if (path->panel == NULL)
			continue;

		preline_time = owl_panel_get_preline_time(path->panel);

		if (path->ops->is_preline_enabled(path) &&
		    path->ops->is_preline_pending(path)) {
			pr_debug("path%d preline IRQ pending\n", path->id);

			/* Ack the interrupt */
			path->ops->clear_preline_pending(path);


			/* handling ... */

			path->vsync_stamp = ktime_get();

			if (preline_time <= OWL_DE_WAIT_VB_THRESHOLD)
				__de_path_wait_vb_valid(path);
			else
				schedule_work(&path->vsync_work);

			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

/*=============================================================================
			      debugfs attributes
 *===========================================================================*/

static int __debugfs_simple_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

/*
 * de device
 */
static ssize_t __debugfs_de_regs_read(struct file *filp, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct owl_de_device *de = filp->private_data;

	if (de->ops && de->ops->dump_regs)
		de->ops->dump_regs(de);

	return 0;
}

static const struct file_operations de_regs_fops = {
	.open = __debugfs_simple_open,
	.read = __debugfs_de_regs_read,
	.llseek = default_llseek,
};

static void de_device_debugfs_create(struct owl_de_device *de)
{
	debugfs_create_u32("hw_id", S_IRUGO, de->dir, &de->hw_id);
	debugfs_create_u32("state", S_IRUGO, de->dir, &de->state);

	/* regs dump */
	debugfs_create_file("regs", S_IRUGO, de->dir, de, &de_regs_fops);
}

/*
 * de path
 */
static ssize_t __debugfs_path_enable_read(struct file *filp,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	char buf[10];
	int len;

	struct owl_de_path *path = filp->private_data;

	if (path->ops && path->ops->is_enabled &&
	    path->ops->is_enabled(path))
		len = sprintf(buf, "1\n");
	else
		len = sprintf(buf, "0\n");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t __debugfs_path_enable_write(struct file *filp,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	int enable = 0;

	struct owl_de_path *path = filp->private_data;

	sscanf(user_buf, "%d\n", &enable);

	if (!!enable)
		owl_de_path_enable(path);
	else
		owl_de_path_disable(path);

	return count;
}

static const struct file_operations path_enable_fops = {
	.open = __debugfs_simple_open,
	.read = __debugfs_path_enable_read,
	.write = __debugfs_path_enable_write,
	.llseek = default_llseek,
};

static ssize_t __debugfs_path_info_apply_write(struct file *filp,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct owl_de_path *path = filp->private_data;

	/* dummy copy, no problem */
	owl_de_path_set_info(path, &path->info);
	owl_de_path_apply(path);

	return count;
}

static const struct file_operations path_info_apply_fops = {
	.open = __debugfs_simple_open,
	.write = __debugfs_path_info_apply_write,
	.llseek = default_llseek,
};


static void de_path_debugfs_create(struct owl_de_path *path)
{
	/* path info */
	debugfs_create_u32("type", S_IRUGO | S_IWUSR, path->dir,
			   &path->info.type);
	debugfs_create_u32("width", S_IRUGO | S_IWUSR, path->dir,
			   &path->info.width);
	debugfs_create_u32("height", S_IRUGO | S_IWUSR, path->dir,
			   &path->info.height);
	debugfs_create_u32("dither_mode", S_IRUGO | S_IWUSR, path->dir,
			   &path->info.dither_mode);

	/* path enable */
	debugfs_create_file("enable", S_IRUGO | S_IWUSR,
			    path->dir, path, &path_enable_fops);

	/* path info apply */
	debugfs_create_file("apply", S_IWUSR, path->dir, path,
			    &path_info_apply_fops);
}

/* de video */

static ssize_t __debugfs_video_info_apply_write(struct file *filp,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct owl_de_video *video = filp->private_data;

	if (video->path == NULL) {
		pr_err("video->path is NULL\n");
		return count;
	}

	/* dummy copy, no problem */
	owl_de_video_set_info(video, &video->info);
	owl_de_path_apply(video->path);

	return count;
}

static const struct file_operations video_info_apply_fops = {
	.open = __debugfs_simple_open,
	.write = __debugfs_video_info_apply_write,
	.llseek = default_llseek,
};

static void de_video_debugfs_create(struct owl_de_video *video)
{
	struct owl_de_video_info *info = &video->info;

	/* video info */
	debugfs_create_u64("addr0", S_IRUGO | S_IWUSR, video->dir,
			   (u64 *)&info->addr[0]);
	debugfs_create_u32("offset0", S_IRUGO | S_IWUSR, video->dir,
			   &info->offset[0]);
	debugfs_create_u32("pitch0", S_IRUGO | S_IWUSR, video->dir,
			   &info->pitch[0]);

	debugfs_create_u32("color_mode", S_IRUGO | S_IWUSR, video->dir,
			   &info->color_mode);

	debugfs_create_u16("xoff", S_IRUGO | S_IWUSR, video->dir, &info->xoff);
	debugfs_create_u16("yoff", S_IRUGO | S_IWUSR, video->dir, &info->yoff);
	debugfs_create_u16("width", S_IRUGO | S_IWUSR, video->dir,
			   &info->width);
	debugfs_create_u16("height", S_IRUGO | S_IWUSR, video->dir,
			   &info->height);

	debugfs_create_u16("pos_x", S_IRUGO | S_IWUSR, video->dir,
			   &info->pos_x);
	debugfs_create_u16("pos_y", S_IRUGO | S_IWUSR, video->dir,
			   &info->pos_y);
	debugfs_create_u16("out_width", S_IRUGO | S_IWUSR, video->dir,
			   &info->out_width);
	debugfs_create_u16("out_height", S_IRUGO | S_IWUSR, video->dir,
			   &info->out_height);

	debugfs_create_u32("rotation", S_IRUGO | S_IWUSR, video->dir,
			   &info->rotation);

	debugfs_create_u8("blending", S_IRUGO | S_IWUSR, video->dir, &info->blending);
	debugfs_create_u8("alpha", S_IRUGO | S_IWUSR, video->dir, &info->alpha);

	/* video info apply */
	debugfs_create_file("apply", S_IWUSR, video->dir, video,
			    &video_info_apply_fops);
}

static int owl_de_debugfs_create(struct owl_de_device *de)
{
	char buf[32];

	int i;
	int ret = 0;

	struct owl_de_path *path;
	struct owl_de_video *video;

	pr_info("start\n");

	de->dir = debugfs_create_dir("de", NULL);
	if (IS_ERR(de->dir)) {
		ret = PTR_ERR(de->dir);
		goto de_dir_failed;
	}
	de_device_debugfs_create(de);

	for (i = 0; i < de->num_paths; i++) {
		path = &de->paths[i];

		sprintf(buf, "path%d", path->id);

		path->dir = debugfs_create_dir(buf, de->dir);
		if (IS_ERR(path->dir)) {
			ret = PTR_ERR(path->dir);
			goto path_dir_failed;
		}
		de_path_debugfs_create(path);
	}

	for (i = 0; i < de->num_videos; i++) {
		video = &de->videos[i];

		sprintf(buf, "video%d", video->id);

		video->dir = debugfs_create_dir(buf, de->dir);
		if (IS_ERR(video->dir)) {
			ret = PTR_ERR(video->dir);
			goto video_dir_failed;
		}
		de_video_debugfs_create(video);
	}

	return 0;

video_dir_failed:
path_dir_failed:
	debugfs_remove_recursive(de->dir);

de_dir_failed:

	pr_err("failed(%d)\n", ret);
	return ret;
}

module_param(de_hscaler_min, int, 0644);
module_param(vsync_always_on, int, 0644);
module_param(de_gamma_test, int, 0644);
