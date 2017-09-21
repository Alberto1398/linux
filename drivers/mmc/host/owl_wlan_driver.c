#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/atc260x/atc260x.h>

#include "owl_wlan_plat_data.h"

int g_wifi_type ;
extern int use_wifi_bt_vddio;
extern struct regulator *pwifiregulator_vddio;

static void (*wifi_hook) (void) ;
static struct completion wlan_complete;

/* Wi-Fi platform driver */
static int owl_wlan_init(struct wlan_plat_data *pdata)
{
	int ret;

	if (pdata && pdata->set_init) {
		ret = pdata->set_init(pdata);
		if (ret < 0)
			pr_err("sdio wifi: init sdio wifi failed\n");
		return ret;
	}

	return 0;
}

int owl_wlan_set_power(struct wlan_plat_data *pdata, int on, unsigned long msec)
{
	if (pdata && pdata->set_power)
		pdata->set_power(pdata, on);

	if (msec)
		mdelay(msec);
	return 0;
}

EXPORT_SYMBOL(owl_wlan_set_power);

static int owl_wlan_carddetect(int on, struct wlan_plat_data *pdata)
{
	if (pdata && pdata->set_carddetect)
		pdata->set_carddetect(on);

	return 0;
}

static void owl_wlan_exit(struct wlan_plat_data *pdata)
{
	if (pdata && pdata->set_exit)
		pdata->set_exit(pdata);
}

static int owl_wlan_probe(struct platform_device *pdev)
{
	struct wlan_plat_data *pdata =
	    (struct wlan_plat_data *)(pdev->dev.platform_data);

	if (owl_wlan_init(pdata)) {
		pr_err("sdio wifi device probe failed\n");
		return -ENXIO;
	}

  if (use_wifi_bt_vddio == 1)
	{
		regulator_enable(pwifiregulator_vddio);
		mdelay(100);
	}
	owl_wlan_set_power(pdata, 1, 0);

	if (g_wifi_type != WIFI_TYPE_BCMDHD) {
		printk("wlan card detection to detect SDIO card!");
		owl_wlan_carddetect(1, pdata);
	}

	complete(&wlan_complete);
	return 0;
}

static int owl_wlan_remove(struct platform_device *pdev)
{
	struct wlan_plat_data *pdata =
	    (struct wlan_plat_data *)(pdev->dev.platform_data);

  if (use_wifi_bt_vddio == 1)
	{
		regulator_disable(pwifiregulator_vddio);
	}
	owl_wlan_set_power(pdata, 0, 0);

	if (g_wifi_type != WIFI_TYPE_BCMDHD) {
		printk("wlan card detection to remove SDIO card!");
		owl_wlan_carddetect(0, pdata);
	}

	owl_wlan_exit(pdata);
	complete(&wlan_complete);
	return 0;
}

static int owl_wlan_shutdown(struct platform_device *pdev)
{
	struct wlan_plat_data *pdata =
	    (struct wlan_plat_data *)(pdev->dev.platform_data);

	printk("%s, %d\n", __FUNCTION__, __LINE__);

	if (wifi_hook != NULL)
		wifi_hook();

	owl_wlan_set_power(pdata, 0, 0);
	
	if (use_wifi_bt_vddio == 1)
	{
		regulator_disable(pwifiregulator_vddio);
	}

	if (g_wifi_type != WIFI_TYPE_BCMDHD) {
		printk("wlan card detection to remove SDIO card!");
		owl_wlan_carddetect(0, pdata);
	}

	complete(&wlan_complete);
	return 0;
}

static int owl_wlan_suspend(struct platform_device *pdev, pm_message_t state)
{
    //pr_info("##> %s, use_wifi_bt_vddio:%d\n", __func__, use_wifi_bt_vddio);
    if (use_wifi_bt_vddio == 1)
	  {
				regulator_disable(pwifiregulator_vddio);
	  }
	  return 0;
}

static int owl_wlan_resume(struct platform_device *pdev)
{
    //pr_info("##> %s, use_wifi_bt_vddio:%d\n", __func__, use_wifi_bt_vddio);
	  if (use_wifi_bt_vddio == 1)
    {
				regulator_enable(pwifiregulator_vddio); 	
				mdelay(100);
	  }
	  return 0;
}

static struct platform_driver wlan_driver = {
	.probe = owl_wlan_probe,
	.remove = owl_wlan_remove,
	.suspend = owl_wlan_suspend,
	.resume = owl_wlan_resume,
	.driver = {
		   .name = "owl_wlan",
		   }
};

/* symbols exported to wifi driver
 *
 * Param :
 *  type : For wifi type, they define in "wlan_plat_data.h", please don't use '0'
 *     p : If in this module to callback wifi driver function, can call through it. Don't need can add value "NULL"
 */
int owl_wifi_init(int type, void *p)
{
	int ret;

	g_wifi_type = type;
	wifi_hook = p;

	init_completion(&wlan_complete);
	ret = platform_driver_register(&wlan_driver);
	if (!wait_for_completion_timeout(&wlan_complete,
					 msecs_to_jiffies(3300))) {
		pr_err("%s: wifi driver register failed\n", __func__);
		goto fail;
	}

	return ret;
fail:
	platform_driver_unregister(&wlan_driver);
	return -1;
}

EXPORT_SYMBOL(owl_wifi_init);

void owl_wifi_cleanup(void)
{
	platform_driver_unregister(&wlan_driver);

	g_wifi_type = 0;
	wifi_hook = NULL;
}

EXPORT_SYMBOL(owl_wifi_cleanup);
