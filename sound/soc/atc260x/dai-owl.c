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
#include <sound/initval.h>

#include <linux/clk.h>			/* clk_enable */
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include "sndrv-owl.h"
#include "common-regs-owl.h"

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>

#include <linux/mfd/atc260x/atc260x.h>

static int dai_clk_i2s_count;
static int dai_clk_hdmi_count;
static int dai_clk_spdif_count;
static int dai_mode_i2s_count;
static int dai_mode_hdmi_count;
static int dai_clk_pcm1_count;
static int dai_mode_pcm1_count;

static int dai_clk_enabled;
static int dai_pcm1_clk_enabled;
/* define IC type to use */
static int ic_type_used = IC_NOT_USED;

struct asoc_dai_resource {
    void __iomem    *base[MAX_RES_NUM];/*virtual base for every resource*/
    void __iomem    *baseptr; /*pointer to every virtual base*/
    struct clk      *i2stx_clk;
    struct clk	    *i2srx_clk;
    struct clk	    *hdmia_clk;
    struct clk      *spdif_clk;
	struct clk	    *pcm1_clk;
    int             irq;
    unsigned int    setting;
    struct dma_chan *tx_chan;
    struct dma_chan *rx_chan;
    struct dma_chan *hdmia_chan;
	struct dma_chan *pcm1tx_chan;
	struct dma_chan *pcm1rx_chan;
};

/* ic information of platform */
struct ic_data {
	int ic_type;
};

/* ic information of s700 */
static const struct ic_data  ic_s700 = {
	.ic_type = IC_PLATFM_S700,
};

/* ic information of s900 */
static const struct ic_data  ic_s900 = {
	.ic_type = IC_PLATFM_S900,
};

/* store base register address of i2s and hdmi, pcm */
struct audio_reg_addr {
	phys_addr_t i2s_reg_base;
	phys_addr_t hdmi_reg_base;
	phys_addr_t pcm_reg_base;
};

/* dai resources */
static struct asoc_dai_resource dai_res;
/* data of i2s/hdmi register address */
static struct audio_reg_addr dai_regs;

extern int atc2603c_audio_prepare_and_set_clk(int rate);
extern int atc2603c_audio_disable_clk(void);
extern int atc2603c_audio_hdmia_set_clk(int rate);
/* choose audio clk of atc2603c/atc2609a */
extern int atc2609a_audio_prepare_and_set_clk(int rate);
extern int atc2609a_audio_disable_clk(void);
extern int atc2609a_audio_hdmia_set_clk(int rate);

/* get pmu type to use */
extern int get_pmu_type_used(void);

void __iomem *get_dai_reg_base(int num)
{
	return dai_res.base[num];
}
EXPORT_SYMBOL_GPL(get_dai_reg_base);

void set_dai_reg_base(int num)
{
	dai_res.baseptr = dai_res.base[num];
}

EXPORT_SYMBOL_GPL(set_dai_reg_base);

u32 snd_dai_readl(u32 reg)
{
	u32 res;
	res = readl(dai_res.baseptr + reg);
	return res;
}

EXPORT_SYMBOL_GPL(snd_dai_readl);

void snd_dai_writel(u32 val, u32 reg)
{
	u32 reg_val;
	writel(val, dai_res.baseptr + reg);

	reg_val = readl(dai_res.baseptr + reg);

}
EXPORT_SYMBOL_GPL(snd_dai_writel);

void snd_dai_i2s_clk_disable(void)
{
	if (dai_clk_enabled == 1) {
		clk_disable(dai_res.i2srx_clk);
		clk_disable(dai_res.i2stx_clk);
		dai_clk_enabled = 0;
	}
}
EXPORT_SYMBOL_GPL(snd_dai_i2s_clk_disable);

void snd_dai_i2s_clk_enable(void)
{
	clk_prepare_enable(dai_res.i2srx_clk);
	clk_prepare_enable(dai_res.i2stx_clk);
	dai_clk_enabled = 1;
}
EXPORT_SYMBOL_GPL(snd_dai_i2s_clk_enable);

/* get phys_addr from struct audio_reg_addr */
phys_addr_t get_dai_i2s_reg_base(void)
{
	return dai_regs.i2s_reg_base;
}
EXPORT_SYMBOL_GPL(get_dai_i2s_reg_base);

phys_addr_t get_dai_pcm_reg_base(void)
{
	return dai_regs.pcm_reg_base;
}
EXPORT_SYMBOL_GPL(get_dai_pcm_reg_base);

void snd_dai_pcm1_clk_disable(void)
{
	if(dai_pcm1_clk_enabled == 1)
	{
		clk_disable(dai_res.pcm1_clk);	
		dai_pcm1_clk_enabled = 0;
	}
}
EXPORT_SYMBOL_GPL(snd_dai_pcm1_clk_disable);

void snd_dai_pcm1_clk_enable(void)
{
	clk_prepare_enable(dai_res.pcm1_clk);	
	dai_pcm1_clk_enabled = 1;
}
EXPORT_SYMBOL_GPL(snd_dai_pcm1_clk_enable);

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

static struct device_attribute dai_attr[] = {
	__ATTR(error, S_IRUSR | S_IWUSR, error_show, error_store),
	__ATTR(debug, S_IRUSR | S_IWUSR, debug_show, debug_store),
};

/******************************************************************************/
/*!
 * \par  Description:
 *    将采样率转换成矊 *¬件寄存器设置所需的索引值
 * \param[in]    sample_rate  采样率
 * \param[in]    mode 输入输出模
 *式 （如果是spdif或者hdmi，index不同）
 * \return       索引值
 * \retval           -1 failed
 * \ingroup      sndrv
 *****************************************/
static int get_sf_index(int mode, int sample_rate)
{
	int i = 0;
	char fs = sample_rate / 1000;
	/* 44k 在基础上寄存器bit位基础上加16 */
	static fs_t fs_list[] = {
		{ 384, { -1, 0} },
		{ 352, { -1, 16} },
		{ 192, { 0,  1} },
		{ 176, { 16, 17} },
		{ 96,  { 1,  3} },
		{ 88,  { 17, 19} },
		{ 64,  { 2, -1} },
		{ 48,  { 3,  5} },
		{ 44,  { 19, 21} },
		{ 32,  { 4,  6} },
		{ 24,  { 5, -1} },
		{ 22,  {21, -1} },
		{ 16,  { 6, -1} },
		{ 12,  { 7, -1} },
		{ 11,  {23, -1} },
		{ 8,   { 8, -1} },
		{ -1,  {-1, -1} }
	};

	while ((fs_list[i].sample_rate > 0) && (fs_list[i].sample_rate != fs))
		i++;

	if ((mode == O_MODE_HDMI) || (mode == O_MODE_SPDIF))
		return fs_list[i].index[1];
	else
		return fs_list[i].index[0];
}

/* use sample rate 44.1k and i2s tx/rx clock 11.2896MHz just for antipop */
int snd_antipop_i2s_clk_set(void)
{
	unsigned long reg_val;
	int rate, ret = -1;
	int pmu_type = get_pmu_type_used();

	rate = 44100;
	reg_val = 45158400;

	/* choose function dynamically according to pmu type */
	if (pmu_type == ATC260X_ICTYPE_2603C) {
		ret = atc2603c_audio_prepare_and_set_clk(reg_val);
	}
	if (pmu_type == ATC260X_ICTYPE_2609A) {
		ret = atc2609a_audio_prepare_and_set_clk(reg_val);
	}
	if (pmu_type == PMU_NOT_USED) {
		snd_err("ASoC: dai-owl failed to prepare_and_set_clk\n");
		return -ENXIO;
	}
	if (ret < 0) {
		snd_err("audiopll set error!\n");
		return ret;
	}

	ret = clk_set_rate(dai_res.i2stx_clk, rate << 8);
	if (ret) {
		snd_err("i2stx clk rate set error!!\n");
		return ret;
	}
	ret = clk_set_rate(dai_res.i2srx_clk, rate << 8);
	if (ret) {
		snd_err("i2srx clk rate set error!!\n");
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_antipop_i2s_clk_set);

static int s900_dai_record_clk_set(int mode, int rate)
{
	unsigned long reg_val;
	int sf_index, ret = -1;
	/* get pmu type */
	int pmu_type = get_pmu_type_used();

	if (mode == O_MODE_PCM) {
		if (dai_clk_pcm1_count > 0) {
			dai_clk_pcm1_count++;
			return 0;
		}

		clk_prepare_enable(dai_res.pcm1_clk);
		dai_pcm1_clk_enabled = 1;

		reg_val = 49152000;
		if (pmu_type == ATC260X_ICTYPE_2603C) {
			ret = atc2603c_audio_prepare_and_set_clk(reg_val);
		}
		if (pmu_type == ATC260X_ICTYPE_2609A) {
			ret = atc2609a_audio_prepare_and_set_clk(reg_val);
		}
		if (pmu_type == PMU_NOT_USED) {
			snd_err("ASoC: dai-owl failed to set pcm capture clk\n");
			return -ENXIO;
		}
		if (ret < 0) {
			snd_err("audiopll set error!\n");
			return ret;
		}

		dai_clk_pcm1_count++;
		return 0;
	} 		

	if (dai_clk_i2s_count > 0) {
		dai_clk_i2s_count++;
		return 0;
	}
	if (dai_clk_i2s_count > 0) {
		dai_clk_i2s_count++;
		return 0;
	}
	clk_prepare_enable(dai_res.i2srx_clk);
	clk_prepare_enable(dai_res.i2stx_clk);
	dai_clk_enabled = 1;


	sf_index = get_sf_index(mode, rate);
	if (sf_index & 0x10)
		reg_val = 45158400;
	else
		reg_val = 49152000;

	/* choose function dynamically according to pmu type */
	if (pmu_type == ATC260X_ICTYPE_2603C) {
		ret = atc2603c_audio_prepare_and_set_clk(reg_val);
	}
	if (pmu_type == ATC260X_ICTYPE_2609A) {
		ret = atc2609a_audio_prepare_and_set_clk(reg_val);
	}
	if (pmu_type == PMU_NOT_USED) {
		snd_err("ASoC: dai-owl failed to prepare_and_set_clk\n");
		return -ENXIO;
	}
	if (ret < 0) {
		snd_err("audiopll set error!\n");
		return ret;
	}

	ret = clk_set_rate(dai_res.i2stx_clk, rate << 8);
	if (ret) {
		snd_err("i2stx clk rate set error!!\n");
		return ret;
	}
	ret = clk_set_rate(dai_res.i2srx_clk, rate << 8);
	if (ret) {
		snd_err("i2srx clk rate set error!!\n");
		return ret;
	}
	dai_clk_i2s_count++;

	return 0;
}

static int s900_dai_clk_set(int mode, int rate)
{
	unsigned long reg_val;
	int sf_index, ret = -1;
	/* get pmu type */
	int pmu_type = get_pmu_type_used();

	if (mode == O_MODE_PCM) {
		clk_prepare_enable(dai_res.pcm1_clk);
		dai_pcm1_clk_enabled = 1;

		reg_val = 49152000;
		if (pmu_type == ATC260X_ICTYPE_2603C) {
			ret = atc2603c_audio_prepare_and_set_clk(reg_val);
		}
		if (pmu_type == ATC260X_ICTYPE_2609A) {
			ret = atc2609a_audio_prepare_and_set_clk(reg_val);
		}
		if (pmu_type == PMU_NOT_USED) {
			snd_err("ASoC: dai-owl failed to set pcm playback clk\n");
			return -ENXIO;
		}
		if (ret < 0) {
			snd_err("audiopll set error!\n");
			return ret;
		}

		dai_clk_pcm1_count++;
		return 0;
	}

	clk_prepare_enable(dai_res.i2srx_clk);
	clk_prepare_enable(dai_res.i2stx_clk);
	dai_clk_enabled = 1;

	sf_index = get_sf_index(mode, rate);
	if (sf_index & 0x10)
		reg_val = 45158400;
	else
		reg_val = 49152000;

	/* choose function dynamically according to pmu type */
	if (pmu_type == ATC260X_ICTYPE_2603C) {
		ret = atc2603c_audio_prepare_and_set_clk(reg_val);
	}
	if (pmu_type == ATC260X_ICTYPE_2609A) {
		ret = atc2609a_audio_prepare_and_set_clk(reg_val);
	}
	if (pmu_type == PMU_NOT_USED) {
		snd_err("ASoC: dai-owl failed to prepare_and_set_clk \n");
		return -ENXIO;
	}
	if (ret < 0) {
		snd_err("audiopll set error!\n");
		return ret;
	}

	switch (mode) {
	case O_MODE_I2S:
	if (dai_clk_i2s_count == 0) {
		ret = clk_set_rate(dai_res.i2stx_clk, rate << 8);
		if (ret) {
			snd_dbg("i2stx clk rate set error!!\n");
			return ret;
		}
		ret = clk_set_rate(dai_res.i2srx_clk, rate << 8);
		if (ret) {
			snd_dbg("i2srx clk rate set error!!\n");
			return ret;
		}
	}
	dai_clk_i2s_count++;
	break;

	case O_MODE_HDMI:
	if (dai_clk_hdmi_count == 0) {
		/* choose function dynamically according to pmu type */
		if (pmu_type == ATC260X_ICTYPE_2603C) {
			ret = atc2603c_audio_hdmia_set_clk(rate << 7);
		}
		if (pmu_type == ATC260X_ICTYPE_2609A) {
			ret = atc2609a_audio_hdmia_set_clk(rate << 7);
		}
		if (pmu_type == PMU_NOT_USED) {
			snd_err("ASoC: dai-owl failed to hdmia_set_clk \n");
			return -ENXIO;
		}
		if (ret) {
			snd_dbg("hdmi clk rate set error!!\n");
			return ret;
		}
	}
	dai_clk_hdmi_count++;
	break;

	case O_MODE_SPDIF:
	if (dai_clk_spdif_count == 0) {
		ret = clk_set_rate(dai_res.spdif_clk, rate << 7);
		if (ret) {
			snd_dbg("spdif clk rate set error!!\n");
			return ret;
		}
	}
	dai_clk_spdif_count++;
	break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s900_dai_record_clk_disable(int mode)
{
	if (mode == O_MODE_PCM) {
		dai_clk_pcm1_count--;
		if (dai_clk_pcm1_count == 0) {
			snd_dai_pcm1_clk_disable();
		}
		return 0;
	}

	dai_clk_i2s_count--;

	return 0;
}

static int s900_dai_clk_disable(int mode)
{
	switch (mode) {
	case O_MODE_I2S:
	/* we disable the i2s_clk in another place */
	dai_clk_i2s_count--;
	break;

	case O_MODE_HDMI:
	dai_clk_hdmi_count--;
	break;

	case O_MODE_SPDIF:
	dai_clk_spdif_count--;
	break;

	case O_MODE_PCM:
	dai_clk_pcm1_count--;
	if (dai_clk_pcm1_count == 0) {
		snd_dai_pcm1_clk_disable();
	}
	break;

	default:
		return -EINVAL;
	}
	return 0;
}

#if 0
static int s900_i2s_4wire_config(struct s900_pcm_priv *pcm_priv,
		struct snd_soc_dai *dai)
{
	int ret;

	pcm_priv->pc = pinctrl_get(dai->dev);
	if (IS_ERR(pcm_priv->pc) || (pcm_priv->pc == NULL)) {
		snd_dbg("i2s pin control failed!\n");
		return -EAGAIN;
	}

	pcm_priv->ps = pinctrl_lookup_state(pcm_priv->pc, "default");
	if (IS_ERR(pcm_priv->ps) || (pcm_priv->ps == NULL)) {
		snd_dbg("i2s pin state get failed!\n");
		return -EAGAIN;
	}

	ret = pinctrl_select_state(pcm_priv->pc, pcm_priv->ps);
	if (ret) {
		snd_dbg("i2s pin state set failed!\n");
		return -EAGAIN;
	}

	/*
	 * set 4wire mode
	snd_dai_writel(snd_dai_readl(PAD_CTL) | (0x1 << 1), PAD_CTL);
	snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x1 << 2), MFP_CTL0);
	snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x1 << 5), MFP_CTL0);
	 */

	/* disable i2s tx&rx */
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 0), I2S_CTL);

	/* reset i2s rx&&tx fifo, avoid left & right channel wrong */
	snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~(0x3 << 9) & ~0x3, I2S_FIFOCTL);
	snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) | (0x3 << 9) | 0x3, I2S_FIFOCTL);

	/* this should before enable rx/tx,
	or after suspend, data may be corrupt */
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 11), I2S_CTL);
	snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 11), I2S_CTL);
	/* set i2s mode I2S_RX_ClkSel==1 */
	snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 10), I2S_CTL);

	/* enable i2s rx/tx at the same time */
	snd_dai_writel(snd_dai_readl(I2S_CTL) | 0x3, I2S_CTL);

	/* i2s rx 00: 2.0-Channel Mode */
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 8), I2S_CTL);
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x7 << 4), I2S_CTL);

	return 0;
}
#endif

static int s900_dai_record_mode_set(struct s900_pcm_priv *pcm_priv,
		struct snd_soc_dai *dai)
{
	if (pcm_priv->input_mode == O_MODE_PCM) {
		if (dai_mode_pcm1_count == 0) {

			/* congfig pcm register */
			set_dai_reg_base(PCM_NUM);
			/* disable pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) & ~(0x1 << 19), PCM1_CTL);
			/* clear PCM1_STAT */
			snd_dai_writel(0xffff, PCM1_STAT);
			/* enable pcm1 tx/rx drq */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) | (0x3 << 4), PCM1_CTL);
			/* choose linear pcm(16 bits) for pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) & ~(0x7) | 0x5, PCM1_CTL);
			/* enable pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) | (0x1 << 19), PCM1_CTL);

		}
		dai_mode_pcm1_count++;
		return 0;
	}

	if (dai_mode_i2s_count == 0) {
		/* disable i2s tx&rx */
		set_dai_reg_base(I2S_SPDIF_NUM);
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 0), I2S_CTL);

		/* reset i2s rx&&tx fifo, avoid left & right channel wrong */
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
		& ~(0x3 << 9) & ~0x3, I2S_FIFOCTL);
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
			| (0x3 << 9) | 0x3, I2S_FIFOCTL);

		/* this should before enable rx/tx,
		or after suspend, data may be corrupt */
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 11), I2S_CTL);
		snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 11), I2S_CTL);
		/* set i2s mode I2S_RX_ClkSel==1 */
		snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 10), I2S_CTL);

		/* enable i2s rx/tx at the same time */
		snd_dai_writel(snd_dai_readl(I2S_CTL) | 0x3, I2S_CTL);

		/* i2s rx 00: 2.0-Channel Mode */
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 8), I2S_CTL);
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x7 << 4), I2S_CTL);
	}
	dai_mode_i2s_count++;

	return 0;
}


static int s900_dai_mode_set(struct s900_pcm_priv *pcm_priv,
		struct snd_soc_dai *dai)
{
	int ret;

	switch (pcm_priv->output_mode) {
	case O_MODE_I2S:
		if (dai_mode_i2s_count == 0) {
			/* disable i2s tx&rx */
			set_dai_reg_base(I2S_SPDIF_NUM);
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 0), I2S_CTL);

			/* reset i2s rx&&tx fifo, avoid left & right channel wrong */
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
				& ~(0x3 << 9) & ~0x3, I2S_FIFOCTL);
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
				| (0x3 << 9) | 0x3, I2S_FIFOCTL);

			/* this should before enable rx/tx,
			or after suspend, data may be corrupt */
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 11), I2S_CTL);
			snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 11), I2S_CTL);
			/* set i2s mode I2S_RX_ClkSel==1 */
			snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 10), I2S_CTL);

			/* enable i2s rx/tx at the same time */
			snd_dai_writel(snd_dai_readl(I2S_CTL) | 0x3, I2S_CTL);

			/* i2s rx 00: 2.0-Channel Mode */
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 8), I2S_CTL);
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x7 << 4), I2S_CTL);

			snd_dai_writel(0x0, I2STX_DAT);
			snd_dai_writel(0x0, I2STX_DAT);

		}
		dai_mode_i2s_count++;
		break;

	case O_MODE_HDMI:
		/* HDMI&SPDIF fifo reset */
		set_dai_reg_base(I2S_SPDIF_NUM);
		if (dai_mode_hdmi_count == 0) {
			snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) & ~0x3,
				SPDIF_HDMI_CTL);
			/* HDMI fifo enable,DRQ enable */
			snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) |
				0x102, SPDIF_HDMI_CTL);
		}
		dai_mode_hdmi_count++;
		break;

	case O_MODE_SPDIF:
		break;

	case O_MODE_PCM:
		if (dai_mode_pcm1_count == 0) {

			/* congfig pcm register */
			set_dai_reg_base(PCM_NUM);
			/* disable pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) & ~(0x1 << 19), PCM1_CTL);
			/* clear PCM1_STAT */
			snd_dai_writel(0xffff, PCM1_STAT);
			/* enable pcm1 tx/rx drq */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) | (0x3 << 4), PCM1_CTL);
			/* choose linear pcm(16 bits) for pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) & ~(0x7) | 0x5, PCM1_CTL);
			/* enable pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) | (0x1 << 19), PCM1_CTL);

		}
		dai_mode_pcm1_count++;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int s900_dai_record_mode_unset(struct s900_pcm_priv *pcm_priv)
{
	if (pcm_priv->input_mode == O_MODE_PCM) {
		dai_mode_pcm1_count--;
		if (dai_mode_pcm1_count == 0) {
			/* congfig pcm register */
			set_dai_reg_base(PCM_NUM);
			/* disable pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) & ~(0x1 << 19), PCM1_CTL);
		}
		return 0;
	}

	dai_mode_i2s_count--;
	if (dai_mode_i2s_count == 0) {
		set_dai_reg_base(I2S_SPDIF_NUM);
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~0x3, I2S_CTL);
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~0x3, I2S_FIFOCTL);
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~(0x3 << 9), I2S_FIFOCTL);
		/*pinctrl_put(pcm_priv->pc);*/
	}
	return 0;
}

static int s900_dai_mode_unset(struct s900_pcm_priv *pcm_priv)
{
	switch (pcm_priv->output_mode) {
	case O_MODE_I2S:
		dai_mode_i2s_count--;
		if (dai_mode_i2s_count == 0) {
			set_dai_reg_base(I2S_SPDIF_NUM);
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~0x3, I2S_CTL);
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~0x3, I2S_FIFOCTL);
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~(0x3 << 9), I2S_FIFOCTL);
			/*pinctrl_put(pcm_priv->pc);*/
		}
		break;
	case O_MODE_HDMI:
		/* HDMI fifo disable */
		dai_mode_hdmi_count--;
		if (dai_mode_hdmi_count == 0) {
			set_dai_reg_base(I2S_SPDIF_NUM);
			snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) & ~0x2, SPDIF_HDMI_CTL);
		}
		break;
	case O_MODE_SPDIF:
		break;
	case O_MODE_PCM:
		dai_mode_pcm1_count--;
		if (dai_mode_pcm1_count == 0) {
			/* congfig pcm register */
			set_dai_reg_base(PCM_NUM);
			/* disable pcm1 */
			snd_dai_writel(snd_dai_readl(PCM1_CTL) & ~(0x1 << 19), PCM1_CTL);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s900_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv
		= snd_soc_platform_get_drvdata(platform);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		break;
	default:
		return -EINVAL;
	}
	if (SNDRV_PCM_STREAM_CAPTURE == substream->stream) {
		s900_dai_record_clk_set(pcm_priv->input_mode, params_rate(params));
		s900_dai_record_mode_set(pcm_priv, dai);
	}

	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream) {
		s900_dai_clk_set(pcm_priv->output_mode, params_rate(params));
		s900_dai_mode_set(pcm_priv, dai);
	}
	return 0;
}

static int s900_dai_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);

	if (SNDRV_PCM_STREAM_CAPTURE == substream->stream) {
		s900_dai_record_clk_disable(pcm_priv->input_mode);
		s900_dai_record_mode_unset(pcm_priv);
	}

	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream) {
		s900_dai_clk_disable(pcm_priv->output_mode);
		s900_dai_mode_unset(pcm_priv);
	}
	return 0;
}

struct snd_soc_dai_ops s900_dai_dai_ops = {
	.hw_params = s900_dai_hw_params,
	.hw_free = s900_dai_hw_free,
};

#define S900_STEREO_CAPTURE_RATES SNDRV_PCM_RATE_8000_96000
#define S900_STEREO_PLAYBACK_RATES SNDRV_PCM_RATE_8000_192000
#define S900_FORMATS SNDRV_PCM_FMTBIT_S16_LE

struct snd_soc_dai_driver s900_dai = {
	.name = "owl-audio-i2s",
	.id = S900_AIF_I2S,
	.playback = {
		.stream_name = "s900 dai Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = S900_STEREO_PLAYBACK_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "s900 dai Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = S900_STEREO_CAPTURE_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &s900_dai_dai_ops,
};

static const struct snd_soc_component_driver s900_component = {
	.name		= "s900ac97c",
};

/* modify compatible to match diff IC */
static const struct of_device_id owl_i2s_of_match[] = {
	{.compatible = "actions,s700-audio-i2s", .data = &ic_s700,},
	{.compatible = "actions,s900-audio-i2s", .data = &ic_s900,},
	{},
};

MODULE_DEVICE_TABLE(of, owl_i2s_of_match);


struct dma_chan *dai_dma_slave_tx_chan(void)
{
	return dai_res.tx_chan;
}
EXPORT_SYMBOL_GPL(dai_dma_slave_tx_chan);

struct dma_chan *dai_dma_slave_rx_chan(void)
{
	return dai_res.rx_chan;
}
EXPORT_SYMBOL_GPL(dai_dma_slave_rx_chan);

struct dma_chan *dai_dma_slave_hdmia_chan(void)
{
	return dai_res.hdmia_chan;
}
EXPORT_SYMBOL_GPL(dai_dma_slave_hdmia_chan);

struct dma_chan *dai_dma_slave_pcm1tx_chan(void)
{
	return dai_res.pcm1tx_chan;
}
EXPORT_SYMBOL_GPL(dai_dma_slave_pcm1tx_chan);

struct dma_chan *dai_dma_slave_pcm1rx_chan(void)
{
	return dai_res.pcm1rx_chan;
}
EXPORT_SYMBOL_GPL(dai_dma_slave_pcm1rx_chan);

static int s900_dai_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct resource *res;
	int i;
	int ret = 0;

	struct ic_data *ic;

	/* dn = of_find_compatible_node(NULL, NULL, "actions,owl-audio-i2s"); */
	/* get IC type by match device */
	id = of_match_device(of_match_ptr(owl_i2s_of_match), &pdev->dev);
	if (!id) {
		printk(KERN_ERR"Fail to match device_node actions,owl-audio-i2s!\n");
		return -ENODEV;
	}
	ic = id->data;
	ic_type_used = ic->ic_type;

	if (ic_type_used == IC_PLATFM_S700) {
		printk(KERN_INFO"audio: IC type is S700!\n");
	}
	if (ic_type_used == IC_PLATFM_S900) {
		printk(KERN_INFO"audio: IC type is S900!\n");
	}
	if (ic_type_used == IC_NOT_USED) {
		printk(KERN_INFO"audio: IC type is not used!\n");
		return -ENODEV;
	}

	/* get resource of i2s and hdmi from dts file */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		snd_err("no memory resource of i2s!\n");
		return -ENODEV;
	}
	/* get virtual base for i2s */
	dai_res.base[I2S_SPDIF_NUM] = devm_ioremap_resource(&pdev->dev, res);
	if (dai_res.base[I2S_SPDIF_NUM] == NULL) {
		snd_err("Unable to ioremap register region of i2s\n");
		return -ENXIO;
	}
	/* get phys_addr of i2s register from dts file */
	dai_regs.i2s_reg_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdmi_base");
	if (!res) {
		snd_err("no memory resource of hdmi!\n");
		return -ENODEV;
	}
	/* get virtual base for hdmi */
	dai_res.base[HDMI_NUM] = ioremap(res->start, resource_size(res));
	if (dai_res.base[HDMI_NUM] == NULL) {
		snd_err("Unable to ioremap register region of hdmi\n");
		return -ENXIO;
	}
	/* get phys_addr of hdmi register from dts file */
	dai_regs.hdmi_reg_base = res->start;

	/* get resource of  pcm from dts file */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcm_base");
	if (!res) {
		snd_err("no memory resource of pcm_base!\n");
		return -ENODEV;
	}
	/* get virtual base for pcm */
	dai_res.base[PCM_NUM] = devm_ioremap_resource(&pdev->dev, res);
	if (dai_res.base[PCM_NUM] == NULL) {
		snd_err("Unable to ioremap register region of pcm_base\n");
		return -ENXIO;
	}
	/* get phys_addr of pcm register from dts file */
	dai_regs.pcm_reg_base = res->start;

	printk(KERN_INFO"it's ok to get resource of owl-audio-i2s !\n");

	if (1) {
		for (i = 0; i < ARRAY_SIZE(dai_attr); i++) {
			ret = device_create_file(&pdev->dev, &dai_attr[i]);
			if (ret) {
				snd_err("Add device file failed");
				/*goto device_create_file_failed;*/
			}
		}
	} else {
		snd_err("Find device failed");
		/*goto err_bus_find_device;	*/
	}

	dai_res.i2stx_clk = devm_clk_get(&pdev->dev, "i2stx");
	if (IS_ERR(dai_res.i2stx_clk)) {
		snd_err("no i2stx clock defined\n");
		ret = PTR_ERR(dai_res.i2stx_clk);
		return ret;
	}


	dai_res.i2srx_clk = devm_clk_get(&pdev->dev, "i2srx");
	if (IS_ERR(dai_res.i2srx_clk)) {
		snd_err("no i2srx clock defined\n");
		ret = PTR_ERR(dai_res.i2srx_clk);

		devm_clk_put(&pdev->dev, dai_res.i2stx_clk);
		dai_res.i2stx_clk = NULL;
		dai_res.i2srx_clk = NULL;
		return ret;
	}

	/*FIXME: we STILL need spdif and hdmia clk!!!!!!!*/
	dai_res.pcm1_clk = devm_clk_get(&pdev->dev, "pcm1");
	if (IS_ERR(dai_res.pcm1_clk)) {
		snd_err("no pcm1 clock defined\n");
		ret = PTR_ERR(dai_res.pcm1_clk);
		return ret;
	}

	dai_res.tx_chan = dma_request_slave_channel(&pdev->dev, "tx");
	if (!dai_res.tx_chan) {
		dev_warn(&pdev->dev, "request tx chan failed\n");
		return ret;
	}

	dai_res.rx_chan = dma_request_slave_channel(&pdev->dev, "rx");
	if (!dai_res.rx_chan) {
		dev_warn(&pdev->dev, "request rx chan failed\n");
		return ret;
	}

	dai_res.hdmia_chan = dma_request_slave_channel(&pdev->dev, "hdmia");
	if (!dai_res.hdmia_chan) {
		dev_warn(&pdev->dev, "request hdmia chan failed\n");
		return ret;
	}

	dai_res.pcm1tx_chan = dma_request_slave_channel(&pdev->dev, "pcm1tx");
	if(!dai_res.pcm1tx_chan)
	{
		dev_warn(&pdev->dev, "request pcm1tx chan failed\n");
		return ret;
	}
	
	dai_res.pcm1rx_chan = dma_request_slave_channel(&pdev->dev, "pcm1rx");
	if(!dai_res.pcm1rx_chan)
	{
		dev_warn(&pdev->dev, "request pcm1rx chan failed\n");
		return ret;	
	}

	dev_warn(&pdev->dev, "s900_dai_probe\n");

	pdev->dev.init_name = "owl-audio-i2s";

	return snd_soc_register_component(&pdev->dev, &s900_component,
					 &s900_dai, 1);

}

static int s900_dai_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	/* release virtual dma channels */
	if (dai_res.tx_chan) {
		dma_release_channel(dai_res.tx_chan);
	}

	if (dai_res.rx_chan) {
		dma_release_channel(dai_res.rx_chan);
	}

	if (dai_res.hdmia_chan) {
		dma_release_channel(dai_res.hdmia_chan);
	}

	if(dai_res.pcm1tx_chan) {
		dma_release_channel(dai_res.pcm1tx_chan);
	}

	if(dai_res.pcm1rx_chan){
		dma_release_channel(dai_res.pcm1rx_chan);
	}

	if (dai_res.i2stx_clk) {
		devm_clk_put(&pdev->dev, dai_res.i2stx_clk);
		dai_res.i2stx_clk = NULL;
	}

	if (dai_res.i2srx_clk) {
		devm_clk_put(&pdev->dev, dai_res.i2srx_clk);
		dai_res.i2srx_clk = NULL;
	}

	if (dai_res.spdif_clk) {
		devm_clk_put(&pdev->dev, dai_res.spdif_clk);
		dai_res.spdif_clk = NULL;
	}

	if(dai_res.pcm1_clk) {
		devm_clk_put(&pdev->dev, dai_res.pcm1_clk);
		dai_res.pcm1_clk = NULL;
	}
	return 0;
}



static struct platform_driver s900_dai_driver = {
	.driver = {
		.name = "owl-audio-i2s",
		.owner = THIS_MODULE,
		.of_match_table = owl_i2s_of_match,
	},

	.probe = s900_dai_probe,
	.remove = s900_dai_remove,
};

/*static struct platform_device *s900_dai_device;*/

static int __init s900_dai_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&s900_dai_driver);
	if (ret) {
		snd_err("ASoC: Platform driver s900-dai register failed\n");
	}

	return ret;
}

static void __exit s900_dai_exit(void)
{
	int i = 0;
	struct device *dev = NULL;

    dev = bus_find_device_by_name(&platform_bus_type, NULL, "owl-audio-i2s");

	if (dev) {
		for (i = 0; i < ARRAY_SIZE(dai_attr); i++) {
			device_remove_file(dev, &dai_attr[i]);
		}
	}

	platform_driver_unregister(&s900_dai_driver);
}

module_init(s900_dai_init);
module_exit(s900_dai_exit);


/* Module information */
MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_DESCRIPTION("S900 I2S Interface");
MODULE_LICENSE("GPL");
