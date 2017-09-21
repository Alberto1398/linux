/*
 * drivers/video/owl/display/dsi/panel_amoled.c
 *
 * Generic MIPI Panel(amoled).
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
#define DEBUG
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fb.h>
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
#include <linux/backlight.h>

extern int owl_bl_register(struct backlight_device *bl_device);
extern int owl_bl_notifier_call_chain(unsigned long val);

struct panel_mipi_oled_bl {
	struct backlight_device	*bd;

	unsigned int		brightness;
	unsigned int		max_brightness;
	unsigned int		min_brightness;

	/* 0: disabled, 1: enabled */
	int			power_state;

	/*
	 * a snapshot of bl->props.state,
	 * used for detect the state changing.
	 */
	unsigned int		state;
};


static uint32_t gmc_init[] = {0x00110500, 0x00290500};
#define GMC_INIT_LENS	(ARRAY_SIZE(gmc_init))

static uint32_t gmc_disable[] = {0x00280500, 0x00100500};
#define GMC_DIS_LENS	(ARRAY_SIZE(gmc_disable))

struct dsi_init_cmd {
	uint32_t cmd_nums;
	uint32_t *mipi_cmd;
};
struct panel_amoled_data {
	struct owl_dss_gpio	reset_gpio;
	struct owl_dss_gpio	power_gpio;
	struct owl_dss_gpio	power1_gpio;
	enum owl_dss_state	state;

	struct platform_device *pdev;

	/* Specific data can be added here */
	struct dsi_init_cmd	*cmd;

	int			bl_is_oled;
	struct panel_mipi_oled_bl mipi_bl;
};

static void __panel_amoled_power_on(struct panel_amoled_data *amoled)
{
	pr_info("%s ... ...\n", __func__);

	owl_dss_gpio_active(&amoled->reset_gpio);
	mdelay(10);

	owl_dss_gpio_active(&amoled->power_gpio);  /* high active */
	owl_dss_gpio_active(&amoled->power1_gpio);/* low active */
	mdelay(10);

	owl_dss_gpio_deactive(&amoled->reset_gpio);
	mdelay(10);
}

static void __panel_amoled_power_off(struct panel_amoled_data *amoled)
{
	pr_info("%s ... ...\n", __func__);
	owl_dss_gpio_active(&amoled->reset_gpio);
	mdelay(10);

	owl_dss_gpio_deactive(&amoled->power_gpio);
	owl_dss_gpio_deactive(&amoled->power1_gpio);
	mdelay(10);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_amoled_power_on(struct owl_panel *panel)
{
	struct panel_amoled_data *amoled = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);

	dev_info(&amoled->pdev->dev, "%s, ctrl_is_enabled? %d\n",
				__func__, ctrl_is_enabled);

	if (amoled->state != OWL_DSS_STATE_ON) {
		amoled->state = OWL_DSS_STATE_ON;
		if (!ctrl_is_enabled)
			__panel_amoled_power_on(amoled);
	}
	pr_info("%s end\n", __func__);
	return 0;
}

static int panel_amoled_power_off(struct owl_panel *panel)
{
	struct panel_amoled_data *amoled = panel->pdata;

	struct owl_dss_panel_desc *desc = &panel->desc;

	dev_info(&amoled->pdev->dev, "%s, state %d\n", __func__, amoled->state);

	if (amoled->state != OWL_DSS_STATE_OFF) {
		amoled->state = OWL_DSS_STATE_OFF;
		__panel_amoled_power_off(amoled);
	}

	return 0;
}


static int panel_amoled_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

/*
 * bl_dcs format:
 * 	bit 31:24---> parameters(backlight range 0x00~0xff)
 * 	bit 23:16---> DCS
 * 	bit 15:8----> data type
 * 	bit 7:0-----> cmd delay
 * */
static int panel_amoled_bl_value_set(struct owl_panel *panel, unsigned int val)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;
	unsigned int bl_dcs = 0x511500;

	/* disable video*/
	if (ctrl->ops && ctrl->ops->disable)
		ctrl->ops->disable(ctrl);
	/* delay at least a frame time TODO*/
	mdelay(20);

	/*get bl DCS*/
	bl_dcs |= val << 24;

	/* send backlight command */
	if (ctrl->ops && ctrl->ops->aux_write)
		ctrl->ops->aux_write(ctrl, &bl_dcs, 1);

	/* enable video*/
	if (ctrl->ops && ctrl->ops->enable)
		ctrl->ops->enable(ctrl);

}

static int panel_amoled_set_brightness(struct backlight_device *bd)
{
	/* the percent of backlight, used to adjust charging cuurent */
	int brightness_percent;

	int ret = 0, brightness = bd->props.brightness;

	struct owl_panel *panel = bl_get_data(bd);
	struct panel_amoled_data *amoled = panel->pdata;
	struct panel_mipi_oled_bl *mipi_bl = &amoled->mipi_bl;

	struct backlight_properties *props = &bd->props;

	dev_dbg(&bd->dev, "owl oled bl, update status\n");

	if ((props->state & BL_CORE_FBBLANK) != (mipi_bl->state & BL_CORE_FBBLANK)) {
		/*
		 * state of FBBLAK changed, 'power' should be changed
		 * along with it
		 */
		if ((props->state & BL_CORE_FBBLANK) == 0)
			props->power = FB_BLANK_UNBLANK;
		else
			props->power = FB_BLANK_POWERDOWN;

		mipi_bl->state = props->state;
	}
	/*
	 * in other case, if we change 'props->power' directly from sysfs,
	 * let 'props->power' act upon the setting.
	 */

	if (props->power != FB_BLANK_UNBLANK)
		brightness = 0;

	dev_dbg(&bd->dev, "bd->props.power = %d\n", bd->props.power);
	dev_dbg(&bd->dev, "bd->props.fb_blank = %d\n", bd->props.fb_blank);

	if (brightness > bd->props.max_brightness)
		return -EINVAL;

	if (brightness > 0)
		brightness_percent = bd->props.brightness * 100
					/ bd->props.max_brightness;
	else
		brightness_percent = 0;

	owl_bl_notifier_call_chain(brightness_percent);

	dev_dbg(&bd->dev, "%s: brightness = %d, power_state = %d\n",
		__func__, brightness, mipi_bl->power_state);

	if (brightness == 0) {
		if (mipi_bl->power_state == 0)
			return 0;
		panel_amoled_bl_value_set(panel, 0);
		mipi_bl->power_state = 0;
	} else {
		/* send backlight value */
		panel_amoled_bl_value_set(panel, brightness);
		mipi_bl->power_state = 1;
	}

	return ret;
}

/*==============================================================
			oled backlight panel ops
 *============================================================*/
static const struct backlight_ops panel_amoled_backlight_ops = {
	.get_brightness = panel_amoled_get_brightness,
	.update_status = panel_amoled_set_brightness,
};

static int panel_amoled_enable(struct owl_panel *panel)
{
	struct panel_amoled_data *amoled = panel->pdata;
	struct device *dev = &amoled->pdev->dev;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;
	int i;

	dev_info(dev, "%s\n", __func__);

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);
	if (!ctrl_is_enabled) {
		/* send mipi initail command */
		if (ctrl->ops && ctrl->ops->aux_write) {
			/* send special mipi initail command */
			ctrl->ops->aux_write(ctrl, (char *)amoled->cmd->mipi_cmd,
					amoled->cmd->cmd_nums);

			/* send general mipi command TODO*/
			ctrl->ops->aux_write(ctrl, (char *)&gmc_init[0], 1);
			/* the delay time is necessary, at least 150ms */
			mdelay(200);
			ctrl->ops->aux_write(ctrl, (char *)&gmc_init[1], 1);
		}
	}
}
static int panel_amoled_disable(struct owl_panel *panel)
{
	struct panel_amoled_data *amoled = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	int i;

	dev_info(&amoled->pdev->dev, "%s\n", __func__);

	/* send disable general mipi command, gmc */
	if (ctrl->ops && ctrl->ops->aux_write) {
			/*oled screen send this command will lead to Splash screen!!!*/
			#if 0
			ctrl->ops->aux_write(ctrl, (char *)&gmc_disable[0], 1);
			mdelay(100);
			ctrl->ops->aux_write(ctrl, (char *)&gmc_disable[1], 1);
			#endif
	}
}

static struct owl_panel_ops panel_amoled_panel_ops = {
	.power_on = panel_amoled_power_on,
	.power_off = panel_amoled_power_off,

	.enable = panel_amoled_enable,
	.disable = panel_amoled_disable,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_amoled_of_match[] = {
	{
		.compatible	= "actions,panel-amoled",
	},
	{},
};
static int panel_parse_info(struct device_node *of_node, struct device *dev,
		struct owl_panel *panel, struct panel_amoled_data *amoled)
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

	amoled->cmd = devm_kzalloc(dev, sizeof(struct dsi_init_cmd),
				GFP_KERNEL);
	if (!amoled->cmd) {
		dev_err(dev, "alloc cmd failed\n");
		return -ENOMEM;
	}
	if (len > 4) {
		amoled->cmd->mipi_cmd = devm_kzalloc(dev, len, GFP_KERNEL);
		if (!amoled->cmd->mipi_cmd) {
			dev_err(dev, "alloc mipi_cmd failed\n");
			return -ENOMEM;
		} else {
			ret = of_property_read_u32_array(of_node,
					"mipi_cmd", amoled->cmd->mipi_cmd,
					len / sizeof(uint32_t));
			if (ret < 0) {
				dev_err(dev, "%s:parse mipi initail command failed!\n",
					__func__);
				return ret;
			}
			amoled->cmd->cmd_nums = len / sizeof(uint32_t);
		}
	} else {
		amoled->cmd->mipi_cmd == NULL;
		amoled->cmd->cmd_nums == 0;
		dev_dbg(dev, "%s: No mipi initail command!\n", __func__);
	}

	/*
	 * parse mipi backlight command values
	 * */
	if (of_property_read_u32(of_node, "backlight_is_oled", &amoled->bl_is_oled))
		dev_err(dev, "%s, parse backlight_is_oled error\n", __func__);
	if (amoled->bl_is_oled) {
		if (of_property_read_u32(of_node, "brightness",
					&amoled->mipi_bl.brightness))
			dev_err(dev, "parse mipi bl brightness error\n");

		if (of_property_read_u32(of_node, "max_brightness",
					&amoled->mipi_bl.max_brightness))
			dev_err(dev, "parse mipi bl maxbrightness error\n");

		if (of_property_read_u32(of_node, "min_brightness",
					&amoled->mipi_bl.min_brightness))
			dev_err(dev, "parse mipi bl minbrightness error\n");
	}
	/*
	 * parse mipi panel power on gpio,  It is not necessary!!!
	 *
	 * */
	/* parse power gpio ... */
	ret = owl_dss_gpio_parse(of_node, "power-gpio", &amoled->power_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, amoled->power_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power_gpio failed: %d\n", ret);
		return ret;
	}
	/* parse reset gpio ... */
	ret = owl_dss_gpio_parse(of_node, "reset-gpio", &amoled->reset_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse reset_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, amoled->reset_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request reset_gpio failed: %d\n", ret);
		return ret;
	}
	/* parse power1 gpio ...*/
	ret = owl_dss_gpio_parse(of_node, "power1-gpio", &amoled->power1_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power1_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, amoled->power1_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power1_gpio failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static struct backlight_device *owl_bl_device;

static int panel_amoled_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct property *prop;
	struct panel_amoled_data *amoled;
	struct owl_panel *panel;
	struct backlight_properties props;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_amoled_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	amoled = devm_kzalloc(dev, sizeof(*amoled), GFP_KERNEL);
	if (!amoled) {
		dev_err(dev, "alloc amoled failed\n");
		return -ENOMEM;
	}
	amoled->pdev = pdev;
	panel = owl_panel_alloc("mipi", OWL_DISPLAY_TYPE_DSI);
	if (panel) {

		panel_parse_info(of_node, dev, panel, amoled);

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
	panel->desc.ops = &panel_amoled_panel_ops;

	if (amoled->bl_is_oled) {
		/*
		 * let power state be "off".
		 *
		 * If the backlight is enabled at boot stage, kernel need enable it
		 * after booting, driver just need re-enable it and change the state
		 * to "on".
		 *
		 * If the backlight is enabled at boot stage, kernel do not need
		 * enable it after booting, it will keep "on", which is boot stage's
		 * continuity.
		 * NOTE: in the case, 'owl_backlight_is_on' will return wrong value.
		 */
		amoled->mipi_bl.power_state = 0;

		memset(&props, 0, sizeof(props));
		props.type = BACKLIGHT_RAW;

		props.brightness = amoled->mipi_bl.brightness;
		props.max_brightness  = amoled->mipi_bl.max_brightness;

		/* same as 'pb->power_state' */
		props.power = FB_BLANK_POWERDOWN;
		props.state |= BL_CORE_FBBLANK;
		amoled->mipi_bl.state = props.state;

		amoled->mipi_bl.bd = backlight_device_register("owl_backlight",
				dev, panel, &panel_amoled_backlight_ops, &props);
		if (IS_ERR(amoled->mipi_bl.bd)) {
			dev_err(dev, "failed to register backlight ops.\n");
			ret = PTR_ERR(amoled->mipi_bl.bd);
			return ret;
		}
		/*TODO*/
		owl_bl_device = amoled->mipi_bl.bd;
	}
	dev_set_drvdata(dev, panel);
	panel->pdata = amoled;

	amoled->state = OWL_DSS_STATE_OFF;
	dev_info(dev, "%s, end.\n", __func__);
	return 0;
}

static int panel_amoled_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;
	struct panel_amoled_data *amoled;
	struct backlight_device *bd;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);
	amoled = panel->pdata;

	if (amoled->bl_is_oled) {
		bd = amoled->mipi_bl.bd;
		backlight_device_unregister(bd);
	}

	panel_amoled_power_off(panel);
	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

int owl_oled_backlight_is_on(void)
{
	struct backlight_device *bl = owl_bl_device;
	struct panel_amoled_data *amoled;
	struct owl_panel *panel;

	if (NULL == bl) {
		return -ENODEV;
	}
	panel = bl_get_data(bl);
	amoled = panel->pdata;

	dev_dbg(&bl->dev, "%s: power_state = %d\n", __func__, amoled->mipi_bl.power_state);
	return amoled->mipi_bl.power_state;
}
EXPORT_SYMBOL(owl_oled_backlight_is_on);

/*
 * set backlight on/off, 0 is off, 1 is on.
 *
 * NOTE: this interface SHOULD ONLY change the on/off
 *	status, CAN NOT change the brightness value.
 */
int owl_oled_backlight_set_onoff(int onoff)
{
	struct backlight_device *bl = owl_bl_device;

	if (NULL == bl) {
		return -ENODEV;
	}

	dev_dbg(&bl->dev, "%s: onoff = %d\n", __func__, onoff);

	bl->props.power = (onoff == 0 ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);

	backlight_update_status(bl);
	return 0;
}
EXPORT_SYMBOL(owl_oled_backlight_set_onoff);


static struct platform_driver panel_amoled_driver = {
	.probe          = panel_amoled_probe,
	.remove         = panel_amoled_remove,
	.driver         = {
		.name   = "panel-amoled",
		.owner  = THIS_MODULE,
		.of_match_table	= panel_amoled_of_match,
	},
};

int __init panel_amoled_init(void)
{
	int r = 0;

	pr_info("%s\n", __func__);
	r = platform_driver_register(&panel_amoled_driver);
	if (r)
		pr_err("Failed to initialize dsi platform driver\n");
	return r;
}

void __exit panel_amoled_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_amoled_driver);
}

module_init(panel_amoled_init);
module_exit(panel_amoled_exit);
MODULE_LICENSE("GPL");
