/*
 * drivers/video/owl/display/dsi/panel_gmp.c
 *
 * Generic MIPI Panel(gmp).
 *
 * Author: Lipeng<lipeng@actions-semi.com>
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

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/bootafinfo.h>
#include <video/owl_dss.h>

static uint32_t gmc_init[] = {0x00110500, 0x00290500};
#define GMC_INIT_LENS	(ARRAY_SIZE(gmc_init))

static uint32_t gmc_disable[] = {0x00280500, 0x00100500};
#define GMC_DIS_LENS	(ARRAY_SIZE(gmc_disable))

struct dsi_init_cmd {
	uint32_t cmd_nums;
	uint32_t *mipi_cmd;
};
struct panel_gmp_data {
	struct owl_dss_gpio	reset_gpio;
	struct owl_dss_gpio	power_gpio;
	struct owl_dss_gpio	power1_gpio;
	enum owl_dss_state	state;

	struct platform_device *pdev;

	/* Specific data can be added here */
	struct dsi_init_cmd	*cmd;
};

static void __panel_gmp_power_on(struct panel_gmp_data *gmp)
{
	pr_info("%s ... ...\n", __func__);

	owl_dss_gpio_active(&gmp->reset_gpio);
	mdelay(10);

	owl_dss_gpio_active(&gmp->power_gpio);
	owl_dss_gpio_active(&gmp->power1_gpio);
	mdelay(10);

	owl_dss_gpio_deactive(&gmp->reset_gpio);
	mdelay(10);
}

static void __panel_gmp_power_off(struct panel_gmp_data *gmp)
{
	pr_info("%s ... ...\n", __func__);
	owl_dss_gpio_deactive(&gmp->reset_gpio);
	mdelay(10);

	owl_dss_gpio_deactive(&gmp->power_gpio);
	owl_dss_gpio_deactive(&gmp->power1_gpio);
	mdelay(10);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_gmp_power_on(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);

	dev_info(&gmp->pdev->dev, "%s, ctrl_is_enabled? %d\n",
				__func__, ctrl_is_enabled);

	if (gmp->state != OWL_DSS_STATE_ON) {
		gmp->state = OWL_DSS_STATE_ON;
		if (!ctrl_is_enabled)
			__panel_gmp_power_on(gmp);
	}
	pr_info("%s end\n", __func__);
	return 0;
}

static int panel_gmp_power_off(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;

	dev_info(&gmp->pdev->dev, "%s\n", __func__);

	if (gmp->state != OWL_DSS_STATE_OFF) {
		gmp->state = OWL_DSS_STATE_OFF;
		__panel_gmp_power_off(gmp);
	}

	return 0;
}

static int panel_gmp_enable(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;
	struct device *dev = &gmp->pdev->dev;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;
	int i;

	dev_info(dev, "%s\n", __func__);

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);
	if (!ctrl_is_enabled) {
		/* send mipi initail command */
		if (ctrl->ops && ctrl->ops->aux_write) {
			/* send mipi initail command */
			ctrl->ops->aux_write(ctrl, (char *)gmp->cmd->mipi_cmd,
					gmp->cmd->cmd_nums);
			/* general mipi command TODO*/
			ctrl->ops->aux_write(ctrl, (char *)&gmc_init[0], 1);
			/* the delay time is necessary, at least 150ms */
			mdelay(200);
			ctrl->ops->aux_write(ctrl, (char *)&gmc_init[1], 1);
		}
	}
}
static int panel_gmp_disable(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	int i;

	dev_info(&gmp->pdev->dev, "%s\n", __func__);

	/* send disable general mipi command, gmc */
	if (ctrl->ops && ctrl->ops->aux_write)
		for (i = 0; i < GMC_DIS_LENS; i++) {
			ctrl->ops->aux_write(ctrl, (char *)&gmc_disable[i], 1);
			mdelay(1);
		}
}

static struct owl_panel_ops panel_gmp_panel_ops = {
	.power_on = panel_gmp_power_on,
	.power_off = panel_gmp_power_off,

	.enable = panel_gmp_enable,
	.disable = panel_gmp_disable,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_gmp_of_match[] = {
	{
		.compatible	= "actions,panel-gmp",
	},
	{},
};
static int panel_parse_info(struct device_node *of_node, struct device *dev,
		struct owl_panel *panel, struct panel_gmp_data *gmp)
{
	int cmd_numbers = 0, ret;
	uint32_t *cmd, len;
	struct property *prop;
	pr_info("%s\n", __func__);
	/*
	 * parse mipi initial command
	 * */
	prop = of_find_property(of_node, "mipi_cmd", &len);
	if (!prop) {
		dev_dbg(dev, "Can not read property: cmds\n");
		return -EINVAL;
	}
	dev_dbg(dev, "%s, len  %d\n", __func__, len);

	gmp->cmd = devm_kzalloc(dev, sizeof(struct dsi_init_cmd),
				GFP_KERNEL);
	if (!gmp->cmd) {
		dev_err(dev, "alloc cmd failed\n");
		return -ENOMEM;
	}
	if (len > 4) {
		gmp->cmd->mipi_cmd = devm_kzalloc(dev, len, GFP_KERNEL);
		if (!gmp->cmd->mipi_cmd) {
			dev_err(dev, "alloc mipi_cmd failed\n");
			return -ENOMEM;
		} else {
			ret = of_property_read_u32_array(of_node,
					"mipi_cmd", gmp->cmd->mipi_cmd,
					len / sizeof(uint32_t));
			if (ret < 0) {
				dev_err(dev, "%s:parse mipi initail command failed!\n",
					__func__);
				return ret;
			}
			gmp->cmd->cmd_nums = len / sizeof(uint32_t);
		}
	} else {
		gmp->cmd->mipi_cmd == NULL;
		gmp->cmd->cmd_nums == 0;
		dev_dbg(dev, "%s: No mipi initail command!\n", __func__);
	}

	/*
	 * parse mipi panel power on gpio,  It is not necessary!!!
	 *
	 * */
	/* parse power gpio ... */
	ret = owl_dss_gpio_parse(of_node, "power-gpio", &gmp->power_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, gmp->power_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power_gpio failed: %d\n", ret);
		return ret;
	}
	/* parse reset gpio ... */
	ret = owl_dss_gpio_parse(of_node, "reset-gpio", &gmp->reset_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse reset_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, gmp->reset_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request reset_gpio failed: %d\n", ret);
		return ret;
	}
	/* parse power1 gpio ...*/
	ret = owl_dss_gpio_parse(of_node, "power1-gpio", &gmp->power1_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power1_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, gmp->power1_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power1_gpio failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int panel_gmp_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_gmp_data *gmp;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_gmp_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	gmp = devm_kzalloc(dev, sizeof(*gmp), GFP_KERNEL);
	if (!gmp) {
		dev_err(dev, "alloc gmp failed\n");
		return -ENOMEM;
	}
	gmp->pdev = pdev;
	panel = owl_panel_alloc("mipi", OWL_DISPLAY_TYPE_DSI);
	if (panel) {
		dev_set_drvdata(dev, panel);

		panel_parse_info(of_node, dev, panel, gmp);

		owl_panel_parse_panel_info(of_node, panel);

		if (owl_panel_register(dev, panel) < 0) {
			dev_err(dev, "%s, fail to regitser dss device\n",
				__func__);
			owl_panel_free(panel);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s, fail to alloc dss device\n", __func__);
		return -ENOMEM;
	}
	panel->desc.ops = &panel_gmp_panel_ops;
	panel->pdata = gmp;

	gmp->state = OWL_DSS_STATE_OFF;
	return 0;
}

static int panel_gmp_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	panel_gmp_power_off(panel);
	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_gmp_driver = {
	.probe          = panel_gmp_probe,
	.remove         = panel_gmp_remove,
	.driver         = {
		.name   = "panel-gmp",
		.owner  = THIS_MODULE,
		.of_match_table	= panel_gmp_of_match,
	},
};

int __init panel_gmp_init(void)
{
	int r = 0;

	pr_info("%s\n", __func__);
	r = platform_driver_register(&panel_gmp_driver);
	if (r)
		pr_err("Failed to initialize dsi platform driver\n");
	return r;
}

void __exit panel_gmp_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_gmp_driver);
}

module_init(panel_gmp_init);
module_exit(panel_gmp_exit);
MODULE_LICENSE("GPL");
