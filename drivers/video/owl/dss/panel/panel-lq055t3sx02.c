/*
 * drivers/video/owl/display/dsi/panel_lq055t3sx02.c
 *
 * lq055t3sx02 MIPI Panel.
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
#define DEBUGX
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

/* sharp panel no disable cmd, or screen will splash */
static uint32_t gmc_disable[] = {};
#define GMC_DIS_LENS	(ARRAY_SIZE(gmc_disable))

struct dsi_init_cmd {
	uint32_t cmd_nums;
	uint32_t *mipi_cmd;
};
struct panel_lq055t3sx02_data {
	struct owl_dss_gpio	reset_gpio;
	struct owl_dss_gpio	power_gpio;
	struct owl_dss_gpio	power1_gpio;
	struct regulator	*vddio_avdd;

	enum owl_dss_state	state;

	struct platform_device *pdev;

	/* Specific data can be added here */
	struct dsi_init_cmd	*cmd;
};

static void __panel_lq055t3sx02_power_on(struct panel_lq055t3sx02_data *lq055t3sx02)
{
	int ret;
	pr_info("%s ... ...\n", __func__);

	/* resest low */
	owl_dss_gpio_active(&lq055t3sx02->reset_gpio);
	msleep(450);

	/* supplay VDDI */
	ret = owl_dss_gpio_active(&lq055t3sx02->power_gpio);
	if (ret < 0) {
		if (lq055t3sx02->vddio_avdd != NULL) {
			regulator_enable(lq055t3sx02->vddio_avdd);
			regulator_set_voltage(lq055t3sx02->vddio_avdd, 1800000, 1800000);
		}
	}
	msleep(10);

	/* reset hight */
	owl_dss_gpio_deactive(&lq055t3sx02->reset_gpio);
	msleep(80);

	/* reset low */
	owl_dss_gpio_active(&lq055t3sx02->reset_gpio);
	msleep(10);

	/* reset hight */
	owl_dss_gpio_deactive(&lq055t3sx02->reset_gpio);
	msleep(10);
	/* supplay VSP VNP */
	owl_dss_gpio_active(&lq055t3sx02->power1_gpio);
}

static void __panel_lq055t3sx02_power_off(struct panel_lq055t3sx02_data *lq055t3sx02)
{
	int ret;
	pr_info("%s ... ...\n", __func__);
	/* assert */
	owl_dss_gpio_active(&lq055t3sx02->reset_gpio);
	msleep(20);
	/* power off VSN VSP */
	owl_dss_gpio_deactive(&lq055t3sx02->power1_gpio);
	msleep(20);

	/* power off VDDI */
	ret = owl_dss_gpio_deactive(&lq055t3sx02->power_gpio);
	if (ret < 0) {
		if (lq055t3sx02->vddio_avdd != NULL) {
			regulator_set_voltage(lq055t3sx02->vddio_avdd, 1800000, 1800000);
			/* the regulator enable times need to equal to regualator disable times TODO */
			regulator_disable(lq055t3sx02->vddio_avdd);
		}
	}
	msleep(10);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_lq055t3sx02_power_on(struct owl_panel *panel)
{
	struct panel_lq055t3sx02_data *lq055t3sx02 = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);

	dev_info(&lq055t3sx02->pdev->dev, "%s, ctrl_is_enabled? %d\n",
				__func__, ctrl_is_enabled);

	if (lq055t3sx02->state != OWL_DSS_STATE_ON) {
		lq055t3sx02->state = OWL_DSS_STATE_ON;
		if (!ctrl_is_enabled)
			__panel_lq055t3sx02_power_on(lq055t3sx02);
	}
	pr_info("%s end\n", __func__);
	return 0;
}

static int panel_lq055t3sx02_power_off(struct owl_panel *panel)
{
	struct panel_lq055t3sx02_data *lq055t3sx02 = panel->pdata;

	dev_info(&lq055t3sx02->pdev->dev, "%s\n", __func__);

	if (lq055t3sx02->state != OWL_DSS_STATE_OFF) {
		lq055t3sx02->state = OWL_DSS_STATE_OFF;
		__panel_lq055t3sx02_power_off(lq055t3sx02);
	}

	return 0;
}

static int panel_lq055t3sx02_enable(struct owl_panel *panel)
{
	struct panel_lq055t3sx02_data *lq055t3sx02 = panel->pdata;
	struct device *dev = &lq055t3sx02->pdev->dev;
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
			ctrl->ops->aux_write(ctrl, (char *)lq055t3sx02->cmd->mipi_cmd,
					lq055t3sx02->cmd->cmd_nums);
			/* general mipi command TODO*/
			ctrl->ops->aux_write(ctrl, (char *)&gmc_init[0], 1);
			/* the delay time is necessary, at least 150ms */
			msleep(200);
			ctrl->ops->aux_write(ctrl, (char *)&gmc_init[1], 1);
		}
	}
}
static int panel_lq055t3sx02_disable(struct owl_panel *panel)
{
	struct panel_lq055t3sx02_data *lq055t3sx02 = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	int i;

	dev_info(&lq055t3sx02->pdev->dev, "%s\n", __func__);

	/* send disable general mipi command, gmc */
	if (ctrl->ops && ctrl->ops->aux_write)
		for (i = 0; i < GMC_DIS_LENS; i++) {
			ctrl->ops->aux_write(ctrl, (char *)&gmc_disable[i], 1);
			msleep(100);
		}
}

static struct owl_panel_ops panel_lq055t3sx02_panel_ops = {
	.power_on = panel_lq055t3sx02_power_on,
	.power_off = panel_lq055t3sx02_power_off,

	.enable = panel_lq055t3sx02_enable,
	.disable = panel_lq055t3sx02_disable,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_lq055t3sx02_of_match[] = {
	{
		.compatible	= "actions,panel-lq055t3sx02",
	},
	{},
};
static int panel_parse_info(struct device_node *of_node, struct device *dev,
		struct owl_panel *panel, struct panel_lq055t3sx02_data *lq055t3sx02)
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

	lq055t3sx02->cmd = devm_kzalloc(dev, sizeof(struct dsi_init_cmd),
				GFP_KERNEL);
	if (!lq055t3sx02->cmd) {
		dev_err(dev, "alloc cmd failed\n");
		return -ENOMEM;
	}
	if (len > 4) {
		lq055t3sx02->cmd->mipi_cmd = devm_kzalloc(dev, len, GFP_KERNEL);
		if (!lq055t3sx02->cmd->mipi_cmd) {
			dev_err(dev, "alloc mipi_cmd failed\n");
			return -ENOMEM;
		} else {
			ret = of_property_read_u32_array(of_node,
					"mipi_cmd", lq055t3sx02->cmd->mipi_cmd,
					len / sizeof(uint32_t));
			if (ret < 0) {
				dev_err(dev, "%s:parse mipi initail command failed!\n",
					__func__);
				return ret;
			}
			lq055t3sx02->cmd->cmd_nums = len / sizeof(uint32_t);
		}
	} else {
		lq055t3sx02->cmd->mipi_cmd == NULL;
		lq055t3sx02->cmd->cmd_nums == 0;
		dev_dbg(dev, "%s: No mipi initail command!\n", __func__);
	}

	/*
	 * parse mipi panel power on gpio,  It is not necessary!!!
	 *
	 * */
	/* parse power gpio ... */
	ret = owl_dss_gpio_parse(of_node, "power-gpio", &lq055t3sx02->power_gpio);
	if (ret < 0) {
		dev_dbg(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
	} else {
		ret = devm_gpio_request(dev, lq055t3sx02->power_gpio.gpio, NULL);
		if (ret < 0) {
			dev_err(dev, "request power_gpio failed: %d\n", ret);
			return ret;
		}
	}

	/* parse reset gpio ... */
	ret = owl_dss_gpio_parse(of_node, "reset-gpio", &lq055t3sx02->reset_gpio);
	if (ret < 0) {
		dev_dbg(dev, "%s, parse reset_gpio failed(%d)\n", __func__,
			ret);
	} else {
		ret = devm_gpio_request(dev, lq055t3sx02->reset_gpio.gpio, NULL);
		if (ret < 0) {
			dev_err(dev, "request reset_gpio failed: %d\n", ret);
			return ret;
		}
	}

	/* parse power1 gpio ...*/
	ret = owl_dss_gpio_parse(of_node, "power1-gpio", &lq055t3sx02->power1_gpio);
	if (ret < 0) {
		dev_dbg(dev, "%s, parse power1_gpio failed(%d)\n", __func__,
			ret);
	} else {
		ret = devm_gpio_request(dev, lq055t3sx02->power1_gpio.gpio, NULL);
		if (ret < 0) {
			dev_err(dev, "request power1_gpio failed: %d\n", ret);
			return ret;
		}
	}

	/* in some cases, we uses LDO control the power of panel.(instead of gpio control) */
	lq055t3sx02->vddio_avdd = regulator_get(dev, "vddio-avdd");
	if (IS_ERR(lq055t3sx02->vddio_avdd)) {
		dev_info(dev, "no vddio-avdd\n");
		lq055t3sx02->vddio_avdd = NULL;
	} else {
		dev_dbg(dev, "vddio_avdd %p, current is %duv\n",
			lq055t3sx02->vddio_avdd,
			regulator_get_voltage(lq055t3sx02->vddio_avdd));
			/* need enable, or regulator will disable TODO */
			regulator_enable(lq055t3sx02->vddio_avdd);
	}

	return 0;
}

static int panel_lq055t3sx02_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_lq055t3sx02_data *lq055t3sx02;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_lq055t3sx02_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	lq055t3sx02 = devm_kzalloc(dev, sizeof(*lq055t3sx02), GFP_KERNEL);
	if (!lq055t3sx02) {
		dev_err(dev, "alloc lq055t3sx02 failed\n");
		return -ENOMEM;
	}
	lq055t3sx02->pdev = pdev;
	panel = owl_panel_alloc("mipi", OWL_DISPLAY_TYPE_DSI);
	if (panel) {
		dev_set_drvdata(dev, panel);

		panel_parse_info(of_node, dev, panel, lq055t3sx02);

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
	panel->desc.ops = &panel_lq055t3sx02_panel_ops;
	panel->pdata = lq055t3sx02;

	lq055t3sx02->state = OWL_DSS_STATE_OFF;
	return 0;
}

static int panel_lq055t3sx02_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	panel_lq055t3sx02_power_off(panel);
	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_lq055t3sx02_driver = {
	.probe          = panel_lq055t3sx02_probe,
	.remove         = panel_lq055t3sx02_remove,
	.driver         = {
		.name   = "panel-lq055t3sx02",
		.owner  = THIS_MODULE,
		.of_match_table	= panel_lq055t3sx02_of_match,
	},
};

int __init panel_lq055t3sx02_init(void)
{
	int r = 0;

	pr_info("%s\n", __func__);
	r = platform_driver_register(&panel_lq055t3sx02_driver);
	if (r)
		pr_err("Failed to initialize dsi platform driver\n");
	return r;
}

void __exit panel_lq055t3sx02_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_lq055t3sx02_driver);
}

module_init(panel_lq055t3sx02_init);
module_exit(panel_lq055t3sx02_exit);
MODULE_LICENSE("GPL");
