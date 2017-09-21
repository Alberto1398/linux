#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h> 
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <linux/clk.h>			/* clk_enable */
#include "sndrv-owl.h"
#include "common-regs-owl.h"
#include <linux/io.h> 
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/mfd/atc260x/atc260x.h>

static const char *pcm_device_node = "actions,pcm-audio";
static struct atc260x_dev *pcm_dev;

static ssize_t error_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", error_switch);
	return cnt;
}

static ssize_t error_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		error_switch = tmp;
		break;
	default:
		printk(KERN_ERR"invalid input\n");
		break;
	}
	return count;
}

static ssize_t debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", debug_switch);
	return cnt;
}

static ssize_t debug_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		debug_switch = tmp;
		break;
	default:
		printk(KERN_INFO"invalid input\n");
		break;
	}
	return count;
}

static struct device_attribute bluetooth_pcm_attr[] = {
	__ATTR(error, S_IRUSR | S_IWUSR, error_show, error_store),
	__ATTR(debug, S_IRUSR | S_IWUSR, debug_show, debug_store),
};

static int bluetooth_pcm_audio_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	/* nothing should to do here now */
	return 0;
}
static int bluetooth_pcm_dai_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	/* nothing should to do here now */
	return 0;
}

static int bluetooth_pcm_audio_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	/* nothing should to do here now */
	return 0;
}

static int bluetooth_pcm_audio_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	/* nothing should to do here now */
	return 0;
}

static int bluetooth_pcm_audio_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	/* nothing should to do here now */
	return 0;
}

#define BLUETOOTH_PCM_RATES SNDRV_PCM_RATE_8000_192000
#define BLUETOOTH_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE \
| SNDRV_PCM_FMTBIT_S20_3LE | \
		SNDRV_PCM_FMTBIT_S24_LE)

struct snd_soc_dai_ops bluetooth_pcm_aif_dai_ops = {
	.hw_params = bluetooth_pcm_audio_hw_params,
	.prepare = bluetooth_pcm_audio_prepare,
	.set_fmt = bluetooth_pcm_audio_set_dai_fmt,
	.set_sysclk = bluetooth_pcm_audio_set_dai_sysclk,
	.hw_free = bluetooth_pcm_dai_hw_free,
};

struct snd_soc_dai_driver codec_bluetooth_pcm_dai[] = {
	{
		.name = "bluetooth-pcm-dai",
		.id = S900_AIF_PCM,
		.playback = {
			.stream_name = "bluetooth pcm playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = BLUETOOTH_PCM_RATES,
			.formats = BLUETOOTH_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "bluetooth pcm capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = BLUETOOTH_PCM_RATES,
			.formats = BLUETOOTH_PCM_FORMATS,
		},
		.ops = &bluetooth_pcm_aif_dai_ops,
	},
};

static int codec_bluetooth_pcm_probe(struct snd_soc_codec *codec)
{
	/* nothing should to do here now */
	printk(KERN_INFO"codec_bluetooth_pcm_probe!!\n");
	return 0;
}

static int codec_bluetooth_pcm_remove(struct snd_soc_codec *codec)
{
	/* nothing should to do here now */
	return 0;
}

static struct snd_soc_codec_driver soc_codec_bluetooth_pcm = {
	.probe = codec_bluetooth_pcm_probe,
	.remove = codec_bluetooth_pcm_remove,
};

static int bluetooth_pcm_probe(struct platform_device *pdev)
{

	int i;
	int ret = 0;

	printk(KERN_INFO"bluetooth_pcm_probe!\n");

	for (i = 0; i < ARRAY_SIZE(bluetooth_pcm_attr); i++) {
		ret = device_create_file(&pdev->dev, &bluetooth_pcm_attr[i]);
	}

	pcm_dev = dev_get_drvdata(pdev->dev.parent);
	platform_set_drvdata(pdev, pcm_dev);
	pdev->dev.init_name = "pcm-audio";

	return snd_soc_register_codec(&pdev->dev, &soc_codec_bluetooth_pcm,
			codec_bluetooth_pcm_dai, ARRAY_SIZE(codec_bluetooth_pcm_dai));
}

static int bluetooth_pcm_remove(struct platform_device *pdev)
{
	int i = 0;
	struct device *dev;

	dev = bus_find_device_by_name(&platform_bus_type, NULL, "pcm-audio");
	if (dev) {
		for (i = 0; i < ARRAY_SIZE(bluetooth_pcm_attr); i++) {
			device_remove_file(dev, &bluetooth_pcm_attr[i]);
		}
	} else {
		snd_err("Find platform device pcm-audio failed!\r\n");
		return -ENODEV;
	}

	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id pcm_audio_of_match[] = {
	{.compatible = "actions,pcm-audio",},
};

static struct platform_driver bluetooth_pcm_driver = {
	.driver = {
		.name = "pcm-audio",
		.owner = THIS_MODULE,
		.of_match_table = pcm_audio_of_match,
	},

	.probe = bluetooth_pcm_probe,
	.remove = bluetooth_pcm_remove,
};
 
static struct platform_device *bluetooth_pcm_device;
static int __init bluetooth_pcm_init(void)
{
	int ret = -1;
	int i = 0;
	struct device_node *dn;

	dn = of_find_compatible_node(NULL, NULL, pcm_device_node);
	if (!dn) {
		printk(KERN_ERR"Fail to get device_node for pcm\r\n");
		goto of_get_failed;
	}

	ret = platform_driver_register(&bluetooth_pcm_driver);
	if (ret) {
		printk(KERN_ERR"failed to register bluetooth_pcm_driver!\r\n");
		goto platform_driver_register_failed;
	}

	printk(KERN_INFO"%s ok!! line=%d\n", __func__, __LINE__);
	return 0;
	
platform_driver_register_failed:
of_get_failed:
	return ret;
}
static void __exit bluetooth_pcm_exit(void)
{
	platform_driver_unregister(&bluetooth_pcm_driver);
}

module_init(bluetooth_pcm_init);
module_exit(bluetooth_pcm_exit);

MODULE_AUTHOR("ganxiuliang <ganxiuliang@actions-semi.com>");
MODULE_DESCRIPTION("BLUETOOTH PCM AUDIO module");
MODULE_LICENSE("GPL");
