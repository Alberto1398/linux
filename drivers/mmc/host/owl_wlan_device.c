#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/bootafinfo.h>

#include "owl_wlan_plat_data.h"
#include "owl_mmc.h"

static void (*wifi_detect_func) (int card_present, void *dev_id);
static void *wifi_detect_param;
static unsigned int wifi_vol_scope[2];
static int use_ldo = 0;
static struct regulator *pwifiregulator;

struct regulator *pwifiregulator_vddio;
int use_wifi_bt_vddio = 0;

static void owl_wlan_status_check(int card_present, void *dev_id)
{
	struct owl_mmc_host *host = dev_id;

	pr_info("SDIO Wi-Fi check status, present (%d -> %d)\n",
		host->sdio_present, card_present);

	if (card_present == 0) {
		pr_info("MMC: emulate power off the SDIO card\n");
		owl_mmc_ctr_reset(host);
	}

	host->sdio_present = card_present;

	mmc_detect_change(host->mmc, 0);

}

static int
owl_localwlan_status_check_register(void (*callback)
				    (int card_present, void *dev_id),
				    void *dev_id)
{
	if (wifi_detect_func) {
		pr_err("wifi status check function has registered\n");
		return -EAGAIN;
	}
	wifi_detect_func = callback;
	wifi_detect_param = dev_id;
	return 0;
}

static struct wlan_plat_data *g_pdata;

struct wlan_plat_data *owl_get_wlan_plat_data(void)
{
	return g_pdata;
}

EXPORT_SYMBOL(owl_get_wlan_plat_data);

static inline void check_and_set_gpio(u32 gpio, int value)
{
	if (gpio_cansleep(gpio)) {
		gpio_set_value_cansleep(gpio, value);
	} else {
		gpio_set_value(gpio, value);
	}
}

void owl_wlan_bt_power(int on)
{
	struct wlan_plat_data *pdata = g_pdata;

	if (pdata != NULL) {
		if (use_ldo) {
			if (on) {
				if (++pdata->wl_bt_ref_count == 1) {
					regulator_enable(pwifiregulator);
					mdelay(100);
				}
			} else {
				if (--pdata->wl_bt_ref_count == 0)
					regulator_disable(pwifiregulator);
			}
		} else {
			if (gpio_is_valid(pdata->wifi_bt_power_gpios)) {
				if (on) {
					if (++pdata->wl_bt_ref_count == 1) {
						check_and_set_gpio(pdata->wifi_bt_power_gpios, 1);
						mdelay(20);
					}
				} else {
					if (--pdata->wl_bt_ref_count == 0)
						check_and_set_gpio(pdata->wifi_bt_power_gpios, 0);
				}
			} else {
				pr_err("owl_wlan_bt_power NULL\n");
			}
		}

		pr_info("Wlan or BT power %s, ref count:%d",
			(on ? "on" : "off"), pdata->wl_bt_ref_count);
	}
}

EXPORT_SYMBOL(owl_wlan_bt_power);

static int owl_wlan_init(struct wlan_plat_data *pdata)
{
	struct device_node *np = NULL;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "wifi,bt,power,ctl");
	if (NULL == np) {
		pr_err("No \"wifi,bt,power,ctl\" node found in dts\n");
		goto fail;
	}

	/* wifi en */
	if (of_find_property(np, "wifi_en_gpios", NULL)) {
		pdata->wifi_en_gpios = of_get_named_gpio(np,
							 "wifi_en_gpios", 0);
		if (gpio_is_valid(pdata->wifi_en_gpios)) {
			ret = gpio_request(pdata->wifi_en_gpios,
					   "wifi_en_gpios");
			if (ret < 0) {
				pr_err("couldn't claim wifi_en_gpios pin\n");
				goto fail;
			}
			gpio_direction_output(pdata->wifi_en_gpios, 0);
		} else {
			pr_err("gpio for sdio wifi_en_gpios invalid.\n");
		}
	}

	return 0;

fail:
	return -ENXIO;
}

static int owl_wlan_set_power(struct wlan_plat_data *pdata, int on)
{

	owl_wlan_bt_power(on);

	if (gpio_is_valid(pdata->wifi_en_gpios)) {
		if (on) {
			check_and_set_gpio(pdata->wifi_en_gpios, 1);
			mdelay(20);
		} else {
			check_and_set_gpio(pdata->wifi_en_gpios, 0);
		}
	}

	mdelay(20);

	return 0;
}

/*
 * Open or close wifi, from open
 */
static int owl_wlan_card_detect(int open)
{
	if (wifi_detect_func)
		wifi_detect_func(open, wifi_detect_param);
	else
		pr_warn("SDIO Wi-Fi card detect error\n");
	return 0;
}

static void owl_wlan_exit(struct wlan_plat_data *pdata)
{
	if (gpio_is_valid(pdata->wifi_en_gpios))
		gpio_free(pdata->wifi_en_gpios);
}

static struct wlan_plat_data wlan_control = {
	.set_init = owl_wlan_init,
	.set_exit = owl_wlan_exit,
	.set_power = owl_wlan_set_power,
	.set_carddetect = owl_wlan_card_detect,
};

static void owl_wlan_platform_release(struct device *dev)
{
	return;
}

struct platform_device owl_wlan_device = {
	.name = "owl_wlan",
	.id = 0,
	.dev = {
		.release = owl_wlan_platform_release,
		.platform_data = &wlan_control,
		},
};

int owl_wlan_bt_power_init(struct wlan_plat_data *pdata)
{
	struct device_node *np = NULL;
	int ret;
	unsigned int scope[2];
	const char *pm;
	g_pdata = pdata;

	np = of_find_compatible_node(NULL, NULL, "wifi,bt,power,ctl");
	if (NULL == np) {
		pr_err("No \"wifi,bt,power,ctl\" node found in dts\n");
		goto fail;
	}

	if (of_find_property(np, "reg_wl_bt_power_en", NULL)) {

		ret = of_property_read_string(np, "reg_wl_bt_power_en", &pm);
		if (ret < 0) {
			pr_err
			    ("can not read regulator for reg_wl_bt_power_en!\n");
			return -1;
		}

		pwifiregulator = regulator_get(NULL, pm);
		if (IS_ERR(pwifiregulator)) {
			pr_err("%s:failed to get regulator!\n", __func__);
			return -1;
		}

		if (of_property_read_u32_array
		    (np, "wifi_vol_range", wifi_vol_scope, 2)) {
			printk(" not get wifi voltage range\n");
		} else {
			if (regulator_set_voltage
			    (pwifiregulator, wifi_vol_scope[0],
			     wifi_vol_scope[1])) {
				printk("cannot set wifi ldo voltage\n!!!");
				regulator_put(pwifiregulator);
				return -1;
			}
			printk("get wifi ldo voltage:%d-%d\n",
			       wifi_vol_scope[0], wifi_vol_scope[1]);

		}
		use_ldo = 1;
		pdata->wl_bt_ref_count = 0;

		printk("Regulator wifi bt power init\n");
	}
	
	  /*wifi modules vddio 1.8V pin config init*/
		if (of_find_property(np, "reg_wl_bt_vddio", NULL)) {
		ret = of_property_read_string(np, "reg_wl_bt_vddio", &pm);
		if (ret < 0) {
			pr_err
			    ("can not read regulator for reg_wl_bt_vddio!\n");
			return -1;
		}

		pwifiregulator_vddio = regulator_get(NULL, pm);
		if (IS_ERR(pwifiregulator_vddio)) {
			pr_err("%s:failed to get regulator!\n", __func__);
			return -1;
		}

		if (of_property_read_u32_array
		    (np, "wifi_bt_vddio_vol_range", wifi_vol_scope, 2)) {
			printk(" not get wifi bt vddio voltage range\n");
		} else {
			if (regulator_set_voltage
			    (pwifiregulator_vddio, wifi_vol_scope[0],
			     wifi_vol_scope[1])) {
				printk("cannot set wifi bt vddio voltage\n!!!");
				regulator_put(pwifiregulator);
				return -1;
			}
			printk("get wifi bt vddio  voltage:%d-%d\n",
			       wifi_vol_scope[0], wifi_vol_scope[1]);

		}
		use_wifi_bt_vddio = 1;
		printk("Regulator wifi bt vddio 1.8V init\n");
	}

	if (of_find_property(np, "wifi_bt_power_gpios", NULL)) {
		pdata->wifi_bt_power_gpios = of_get_named_gpio(np,
							       "wifi_bt_power_gpios",
							       0);
		if (gpio_is_valid(pdata->wifi_bt_power_gpios)) {
			ret = gpio_request(pdata->wifi_bt_power_gpios,
					   "wifi_bt_power_gpios");
			if (ret < 0) {
				pr_err("couldn't claim wifi power gpio pin\n");
				goto fail;
			}
			gpio_direction_output(pdata->wifi_bt_power_gpios, 0);
			pdata->wl_bt_ref_count = 0;
		} else {
			pr_err("gpio for sdio wifi power supply invalid.\n");
		}
	}
	return 0;

fail:
	return -ENXIO;
}

void owl_wlan_bt_power_release(void)
{
	struct wlan_plat_data *pdata = g_pdata;

	if (gpio_is_valid(pdata->wifi_bt_power_gpios))
		gpio_free(pdata->wifi_bt_power_gpios);
}

void owl_wlan_status_check_register(struct owl_mmc_host *host)
{
	owl_localwlan_status_check_register(owl_wlan_status_check, host);
}
