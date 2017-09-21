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

static int audio_clk_enable;

#define Audio60958	1	/*1--IEC60958; 0--IEC61937*/
#define HDMI_RAMPKT_AUDIO_SLOT	1
#define HDMI_RAMPKT_PERIOD	1

static int Speaker;			/*Speaker Placement, stereo*/

extern int atc2603c_audio_hdmia_prepare_and_enable_clk(void);
extern int atc2603c_audio_hdmia_disable_clk(void);
/* choose audio_hdmia clk of atc2603c/atc2609a */
extern int atc2609a_audio_hdmia_prepare_and_enable_clk(void);
extern int atc2609a_audio_hdmia_disable_clk(void);

/* get virtual base of i2s and hdmi.  i2s: I2S_SPDIF_NUM, hdmi: HDMI_NUM */
extern void __iomem *get_dai_reg_base(int num);

/* get pmu type to use */
extern int get_pmu_type_used(void);

/*for register io remap
struct asoc_hdmi_resource {
	void __iomem    *base[MAX_RES_NUM];
	void __iomem    *baseptr;
	struct clk      *clk;
	int             irq;
	unsigned int    setting;
};

static struct asoc_hdmi_resource hdmi_res;

static void set_hdmi_reg_base(int num)
{
	hdmi_res.baseptr = hdmi_res.base[num];
}

static u32 snd_hdmi_readl(u32 reg)
{
	return readl(hdmi_res.baseptr + reg);
}

static void snd_hdmi_writel(u32 val, u32 reg)
{
	writel(val, hdmi_res.baseptr + reg);
}
*/

/* write hdmi reg from owl-audio-i2s */
void snd_hdmi_write_reg(u32 val, const u16 idx)
{
	void __iomem *audio_hdmi_base = NULL;
	audio_hdmi_base = get_dai_reg_base(HDMI_NUM);
	writel(val, audio_hdmi_base + idx);
}
/* read hdmi reg from owl-audio-i2s */
int snd_hdmi_read_reg(const u16 idx)
{
	void __iomem *audio_hdmi_base = NULL;
	audio_hdmi_base = get_dai_reg_base(HDMI_NUM);
	return readl(audio_hdmi_base + idx);
}

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

static struct device_attribute hdmi_attr[] = {
	__ATTR(error, S_IRUSR | S_IWUSR, error_show, error_store),
	__ATTR(debug, S_IRUSR | S_IWUSR, debug_show, debug_store),
};

static int hdmi_EnableWriteRamPacket(void)
{
	int i;
	snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_OPCR) |
		(0x1 << 31), HDMI_OPCR);
	while ((snd_hdmi_read_reg(HDMI_OPCR) & (0x1 << 31)) != 0) {
		for (i = 0; i < 10; i++)
			;
	}
	return 0;
}

static int hdmi_SetRamPacket(unsigned no, unsigned char *pkt)
{
	unsigned char tpkt[36];
	unsigned int *reg = (unsigned int *) tpkt;
	unsigned int addr = 126 + no * 14;

	if (no > 5)
		return -EINVAL;

	/**
	 * according to change by genganan 2008-09-24
	 */
	/* Packet Header */
	tpkt[0] = pkt[0];
	tpkt[1] = pkt[1];
	tpkt[2] = pkt[2];
	tpkt[3] = 0;
	/* Packet Word0 */
	tpkt[4] = pkt[3];
	tpkt[5] = pkt[4];
	tpkt[6] = pkt[5];
	tpkt[7] = pkt[6];
	/* Packet Word1 */
	tpkt[8] = pkt[7];
	tpkt[9] = pkt[8];
	tpkt[10] = pkt[9];
	tpkt[11] = 0;
	/* Packet Word2 */
	tpkt[12] = pkt[10];
	tpkt[13] = pkt[11];
	tpkt[14] = pkt[12];
	tpkt[15] = pkt[13];
	/* Packet Word3 */
	tpkt[16] = pkt[14];
	tpkt[17] = pkt[15];
	tpkt[18] = pkt[16];
	tpkt[19] = 0;
	/* Packet Word4 */
	tpkt[20] = pkt[17];
	tpkt[21] = pkt[18];
	tpkt[22] = pkt[19];
	tpkt[23] = pkt[20];
	/* Packet Word5 */
	tpkt[24] = pkt[21];
	tpkt[25] = pkt[22];
	tpkt[26] = pkt[23];
	tpkt[27] = 0;
	/* Packet Word6 */
	tpkt[28] = pkt[24];
	tpkt[29] = pkt[25];
	tpkt[30] = pkt[26];
	tpkt[31] = pkt[27];
	/* Packet Word7 */
	tpkt[32] = pkt[28];
	tpkt[33] = pkt[29];
	tpkt[34] = pkt[30];
	tpkt[35] = 0;

	/* write mode */
	snd_hdmi_write_reg((1 << 8) | (((addr) & 0xFF) << 0), HDMI_OPCR);
	snd_hdmi_write_reg(reg[0], HDMI_ORP6PH);
	snd_hdmi_write_reg(reg[1], HDMI_ORSP6W0);
	snd_hdmi_write_reg(reg[2], HDMI_ORSP6W1);
	snd_hdmi_write_reg(reg[3], HDMI_ORSP6W2);
	snd_hdmi_write_reg(reg[4], HDMI_ORSP6W3);
	snd_hdmi_write_reg(reg[5], HDMI_ORSP6W4);
	snd_hdmi_write_reg(reg[6], HDMI_ORSP6W5);
	snd_hdmi_write_reg(reg[7], HDMI_ORSP6W6);
	snd_hdmi_write_reg(reg[8], HDMI_ORSP6W7);

	hdmi_EnableWriteRamPacket();

	return 0;
}

static int hdmi_SetRamPacketPeriod(unsigned int no, int period)
{
	if (no > 5)
		return -EINVAL;

	if ((period > 0xf) || (period < 0))
		return -EINVAL;

	/* disable */
	snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_RPCR) &
		(unsigned int) (~(1 << no)), HDMI_RPCR);
	snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_RPCR) &
		(unsigned int) (~(0xf << (no * 4 + 8))), HDMI_RPCR);

	if (period != 0) {
		/* enable and set period */
		snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_RPCR) |
		(unsigned int) (period << (no * 4 + 8)), HDMI_RPCR);
		snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_RPCR) |
			(unsigned int) (1 << no), HDMI_RPCR);
	}
	return 0;
}

static int hdmi_gen_audio_infoframe(int audio_channel)
{
		unsigned char pkt[32];
		unsigned int  checksum = 0;
		int i;
		memset(pkt, 0, 32);
		/* header */
		pkt[0] = 0x80 | 0x04;
		pkt[1] = 1;
		pkt[2] = 0x1f & 10;
		pkt[3] = 0x00;
		pkt[4] = audio_channel & 0x7;
		pkt[5] = 0x0;
		pkt[6] = 0x0;
		pkt[7] = Speaker;
		pkt[8] = (0x0 << 7) | (0x0 << 3);

		/* count checksum */
		for (i = 0; i < 31; i++)
			checksum += pkt[i];

		pkt[3] = (unsigned char)((~checksum + 1) & 0xff);
    /* set to RAM Packet */
		hdmi_SetRamPacket(HDMI_RAMPKT_AUDIO_SLOT, pkt);
		hdmi_SetRamPacketPeriod(HDMI_RAMPKT_AUDIO_SLOT,
		HDMI_RAMPKT_PERIOD);
		return 0;
}
void set_hdmi_audio_interface(int channel, int samplerate)
{
	unsigned int tmp03;
	unsigned int tmp47;
	unsigned int CRP_N = 0;
	unsigned int ASPCR = 0;
	unsigned int ACACR = 0;

	/*改变音频相关参数时需要首先disable audio, 配置完成后enable*/

	snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_ICR) &
		~(0x1 << 25), HDMI_ICR);
	snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_ACRPCR) |
		(0x1 << 31), HDMI_ACRPCR);
	snd_hdmi_read_reg(HDMI_ACRPCR); /*flush write buffer effect*/

	tmp03 = snd_hdmi_read_reg(HDMI_AICHSTABYTE0TO3);
	tmp03 &= (~(0xf << 24));

	tmp47 = snd_hdmi_read_reg(HDMI_AICHSTABYTE4TO7);
	tmp47 &= (~(0xf << 4));
	tmp47 |= 0xb;

	switch (samplerate) {
	/* 32000, 44100, 48000, 88200, 96000,
	176400, 192000, 352.8kHz, 384kHz */
	case 1:
		tmp03 |= (0x3 << 24);
		tmp47 |= (0xc << 4);
		CRP_N = 4096;
		break;

	case 2:
		tmp03 |= (0x0 << 24);
		tmp47 |= (0xf << 4);
		CRP_N = 6272;
		break;

	case 3:
		tmp03 |= (0x2 << 24);
		tmp47 |= (0xd << 4);
		CRP_N = 6144;
		break;

	case 4:
		tmp03 |= (0x8 << 24);
		tmp47 |= (0x7 << 4);
		CRP_N = 12544;
		break;

	case 5:
		tmp03 |= (0xa << 24);
		tmp47 |= (0x5 << 4);
		CRP_N = 12288;
		break;

	case 6:
		tmp03 |= (0xc << 24);
		tmp47 |= (0x3 << 4);
		CRP_N = 12288;
		break;

	case 7:
		tmp03 |= (0xe << 24);
		tmp47 |= (0x1 << 4);
		CRP_N = 24576;
		break;

	case 8:
		tmp03 |= (0x1 << 24);
		CRP_N = 12544;
		break;

	case 9:
		tmp03 |= (0x1 << 24);
		CRP_N = 12288;
		break;

	default:
		break;
	}
	snd_hdmi_write_reg(tmp03, HDMI_AICHSTABYTE0TO3);
	snd_hdmi_write_reg(tmp47, HDMI_AICHSTABYTE4TO7);

	snd_hdmi_write_reg(0x0, HDMI_AICHSTABYTE8TO11);
	snd_hdmi_write_reg(0x0, HDMI_AICHSTABYTE12TO15);
	snd_hdmi_write_reg(0x0, HDMI_AICHSTABYTE16TO19);
	snd_hdmi_write_reg(0x0, HDMI_AICHSTABYTE20TO23);

	switch (channel) {
	case 2:
		snd_hdmi_write_reg(0x20001, HDMI_AICHSTASCN);
		break;

	case 3:
		snd_hdmi_write_reg(0x121, HDMI_AICHSTASCN);
		break;

	case 4:
		snd_hdmi_write_reg(0x2121, HDMI_AICHSTASCN);
		break;

	case 5:
		snd_hdmi_write_reg(0x12121, HDMI_AICHSTASCN);
		break;

	case 6:
		snd_hdmi_write_reg(0x212121, HDMI_AICHSTASCN);
		break;

	case 7:
		snd_hdmi_write_reg(0x1212121, HDMI_AICHSTASCN);
		break;

	case 8:
		snd_hdmi_write_reg(0x21212121, HDMI_AICHSTASCN);
		break;

	default:
		break;
	}
	/* TODO samplesize 16bit, 20bit */
	/* 24 bit */
	snd_hdmi_write_reg((snd_hdmi_read_reg(HDMI_AICHSTABYTE4TO7) & ~0xf),
		HDMI_AICHSTABYTE4TO7);
	snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_AICHSTABYTE4TO7) |
		0xb, HDMI_AICHSTABYTE4TO7);
	if (Audio60958 == 1) {
		switch (channel) {
		case 2:
			ASPCR = 0x00000011;
			ACACR = 0xfac688;
			Speaker = 0x0;
			break;

		case 3:
			ASPCR = 0x0002d713;
			ACACR = 0x4008;
			Speaker = 0x1;
			break;

		case 4:
			ASPCR = 0x0003df1b;
			ACACR = 0x4608;
			Speaker = 0x3;
			break;

		case 5:
			ASPCR = 0x0003df3b;
			ACACR = 0x2c608;
			Speaker = 0x7;
			break;

		case 6:
			ASPCR = 0x0003df3f;
			ACACR = 0x2c688;
			Speaker = 0xb;
			break;

		case 7:
			ASPCR = 0x0007ff7f;
			ACACR = 0x1ac688;
			Speaker = 0xf;
			break;

		case 8:
			ASPCR = 0x0007ffff;
			ACACR = 0xfac688;
			Speaker = 0x13;
			break;

		default:
			break;
		}
	} else {
		ASPCR = 0x7f87c003;
		ACACR = 0xfac688;

		tmp03 = snd_hdmi_read_reg(HDMI_AICHSTABYTE0TO3);
		tmp03 |= 0x1;
		snd_hdmi_write_reg(tmp03, HDMI_AICHSTABYTE0TO3);
		snd_hdmi_write_reg(0x21, HDMI_AICHSTASCN);
	}

	/* enable Audio FIFO_FILL  disable wait cycle */
	snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_CR) | 0x50, HDMI_CR);

	if (samplerate > 7)
		ASPCR |= (0x1 << 31);

	snd_hdmi_write_reg(ASPCR, HDMI_ASPCR);
	snd_hdmi_write_reg(ACACR, HDMI_ACACR);
    /*非压缩格式23~30位写0
    * 如果针对压缩码流,
    则HDMI_AICHSTABYTE0TO3的bit[1:0]=0x2（5005新加）;
    * 如果针对线性PCM码流，则HDMI_AICHSTABYTE0TO3
    的bit[1:0]=0x0（同227A）;
    */
		snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_AICHSTABYTE0TO3) &
			~0x3, HDMI_AICHSTABYTE0TO3);

    /* 如果针对压缩码流，则
    HDMI_ASPCR的bit[30:23]=0xff（5005新加）;
     *  如果针对线性PCM码流,
     则HDMI_ASPCR的bit[30:23]=0x0（同227A）;
     */
		snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_ASPCR) &
			~(0xff << 23), HDMI_ASPCR);

		snd_hdmi_write_reg(CRP_N | (0x1 << 31), HDMI_ACRPCR);
		hdmi_gen_audio_infoframe(channel - 1);

    /*****配置完音频相关参数后enable audio*/
    /* enable CRP */
		snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_ACRPCR) &
			~(0x1 << 31), HDMI_ACRPCR);

    /* enable Audio Interface */
		snd_hdmi_write_reg(snd_hdmi_read_reg(HDMI_ICR) |
			(0x1 << 25), HDMI_ICR);
}

static int get_hdmi_audio_fs(int sample_rate)
{
	int AudioFS;
	int fs = sample_rate / 1000;

	/* for O_MODE_HDMI */
	/* 32000, 44100, 48000, 88200, 96000, 176400,
	192000, 352.8kHz, 384kHz */
	switch (fs) {
	case 32:
		AudioFS = 1;
		break;
	case 44:
		AudioFS = 2;
		break;
	case 48:
		AudioFS = 3;
		break;
	case 88:
		AudioFS = 4;
		break;
	case 96:
		AudioFS = 5;
		break;
	case 176:
		AudioFS = 6;
		break;
	case 192:
		AudioFS = 7;
		break;
	case 352:
		AudioFS = 8;
		break;
	case 384:
		AudioFS = 9;
		break;
	default:
		AudioFS = 2;
		break;
	}
	return AudioFS;
}

static int hdmi_audio_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	printk(KERN_INFO"hdmi_audio_hw_params: audio_clk_enable=%d\n",
		audio_clk_enable);

	int rate = params_rate(params);
	int audio_fs = get_hdmi_audio_fs(rate);
	/* get pmu type */
	int pmu_type = get_pmu_type_used();

	if (audio_clk_enable == 0) {
		/* choose function dynamically according to pmu type */
		if (pmu_type == ATC260X_ICTYPE_2603C) {
			atc2603c_audio_hdmia_prepare_and_enable_clk();
		}
		if (pmu_type == ATC260X_ICTYPE_2609A) {
			atc2609a_audio_hdmia_prepare_and_enable_clk();
		}
		if (pmu_type == PMU_NOT_USED) {
			snd_err("ASoC: hdmi-audio-owl audio_clk_enable failed\n");
			return -ENXIO;
		}
		audio_clk_enable = 1;
	}
	/* we set the hdmi audio channels stereo now*/
	set_hdmi_audio_interface(2, audio_fs);
	return 0;
}
static int hdmi_dai_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	/* get pmu type */
	int pmu_type = get_pmu_type_used();

	if (audio_clk_enable == 1) {
		/* choose function dynamically according to pmu type */
		if (pmu_type == ATC260X_ICTYPE_2603C) {
			atc2603c_audio_hdmia_disable_clk();
		}
		if (pmu_type == ATC260X_ICTYPE_2609A) {
			atc2609a_audio_hdmia_disable_clk();
		}
		if (pmu_type == PMU_NOT_USED) {
			snd_err("ASoC: hdmi-audio-owl audio_clk_disable failed\n");
			return -ENXIO;
		}
		audio_clk_enable = 0;
	}

	return 0;
}

static int hdmi_audio_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	/* nothing should to do here now */
	return 0;
}

static int hdmi_audio_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	/* nothing should to do here now */
	return 0;
}

static int hdmi_audio_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	/* nothing should to do here now */
	return 0;
}

#define s900_HDMI_RATES SNDRV_PCM_RATE_8000_192000
#define s900_HDMI_FORMATS (SNDRV_PCM_FMTBIT_S16_LE \
| SNDRV_PCM_FMTBIT_S20_3LE | \
		SNDRV_PCM_FMTBIT_S24_LE)

struct snd_soc_dai_ops hdmi_aif_dai_ops = {
	.hw_params = hdmi_audio_hw_params,
	.prepare = hdmi_audio_prepare,
	.set_fmt = hdmi_audio_set_dai_fmt,
	.set_sysclk = hdmi_audio_set_dai_sysclk,
	.hw_free = hdmi_dai_hw_free,
};

struct snd_soc_dai_driver codec_hdmi_dai[] = {
	{
		.name = "s900-hdmi-dai",
		.id = S900_AIF_HDMI,
		.playback = {
			.stream_name = "s900 hdmi Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = s900_HDMI_RATES,
			.formats = s900_HDMI_FORMATS,
		},
		.ops = &hdmi_aif_dai_ops,
	},
};



static int codec_hdmi_probe(struct snd_soc_codec *codec)
{
	/* nothing should to do here now */
	snd_dbg("codec_hdmi_probe!\n");
	return 0;
}

static int codec_hdmi_remove(struct snd_soc_codec *codec)
{
	/* nothing should to do here now */
	return 0;
}



static struct snd_soc_codec_driver soc_codec_hdmi = {
	.probe = codec_hdmi_probe,
	.remove = codec_hdmi_remove,
};

static int s900_hdmi_probe(struct platform_device *pdev)
{
#if 0
	struct resource *res;
	int i;
	int ret;

	struct device_node *dn;

	dn = of_find_compatible_node(NULL, NULL, "actions,gl5203-audio-hdmi");
	if (!dn) {
		snd_err("Fail to get device_node actions,atm7039c-hdmi\r\n");
		/*goto of_get_failed;*/
	}

	for (i = 0; i < 1; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			snd_err("no memory resource\n");
			return -ENODEV;
		}

		if (!devm_request_mem_region (&pdev->dev, res->start,
					resource_size(res), "gl5203-audio-hdmi")) {
			snd_err("Unable to request register region\n");
			return -EBUSY;
		}

		hdmi_res.base[i] = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (hdmi_res.base[i] == NULL) {
			snd_err("Unable to ioremap register region\n");
			return -ENXIO;
		}
	}

	if (1) {
		for (i = 0; i < ARRAY_SIZE(hdmi_attr); i++) {
			ret = device_create_file(&pdev->dev, &hdmi_attr[i]);
			if (ret) {
				snd_err("Add device file failed");
				/*goto device_create_file_failed;*/
			}
		}
	} else {
		snd_err("Find device failed");
		/*goto err_bus_find_device;*/
	}
#endif

	dev_warn(&pdev->dev, "s900_hdmi_probe!!\n");

	return snd_soc_register_codec(&pdev->dev, &soc_codec_hdmi,
			codec_hdmi_dai, ARRAY_SIZE(codec_hdmi_dai));
}

static int s900_hdmi_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id gl5203_hdmi_of_match[] = {
	{.compatible = "actions,gl5203-audio-hdmi",},
	{}
};

MODULE_DEVICE_TABLE(of, gl5203_hdmi_of_match);


static struct platform_driver s900_hdmi_driver = {
	.driver = {
			.name = "s900-hdmi-audio",
			.owner = THIS_MODULE,
	},

	.probe = s900_hdmi_probe,
	.remove = s900_hdmi_remove,
};

static struct platform_device *s900_hdmi_device;
static int __init s900_hdmi_init(void)
{
	int ret;
	int i = 0;

	s900_hdmi_device = platform_device_alloc("s900-hdmi-audio", -1);
	if (!s900_hdmi_device) {
		snd_err("ASoC: Platform device s900-hdmi-audio allocation failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = platform_device_add(s900_hdmi_device);
	if (ret) {
		snd_err("ASoC: Platform device s900-hdmi-audio add failed\n");
		goto err_device_add;
	}

	ret = platform_driver_register(&s900_hdmi_driver);
	if (ret) {
		snd_err("ASoC: Platform driver s900-hdmi-audio register failed\n");
		goto err_driver_register;
	}

	for (i = 0; i < ARRAY_SIZE(hdmi_attr); i++) {
		ret = device_create_file(
			&s900_hdmi_device->dev, &hdmi_attr[i]);
		if (ret) {
			snd_err("Add device file failed");
			goto device_create_file_failed;
		}
	}

	return 0;

device_create_file_failed:
err_driver_register:
	platform_device_unregister(s900_hdmi_device);

err_device_add:
	platform_device_put(s900_hdmi_device);

err:
	return ret;
}
static void __exit s900_hdmi_exit(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(hdmi_attr); i++) {
		device_remove_file(&s900_hdmi_device->dev, &hdmi_attr[i]);
	}

	platform_driver_unregister(&s900_hdmi_driver);
	platform_device_unregister(s900_hdmi_device);
	s900_hdmi_device = NULL;
}

module_init(s900_hdmi_init);
module_exit(s900_hdmi_exit);

MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_DESCRIPTION("s900 HDMI AUDIO module");
MODULE_LICENSE("GPL");
