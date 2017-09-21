# AT91 Platform Support
snd-soc-atc2609a-objs                            :=      atc2609a-codec/atc2609a-audio-codec.o
snd-soc-atc2603c-objs                            :=      atc2603c-codec/atc2603c-audio-codec.o
snd-soc-dai-owl-objs                        :=      dai-owl.o
snd-soc-hdmi-owl-objs                       :=      hdmi-audio-owl.o
snd-soc-bluetooth-pcm-owl-objs              :=      bluetooth-pcm-owl.o
snd-soc-pcm-owl-objs                        :=      pcm-owl.o dmaengine-pcm-owl.o
snd-soc-link-owl-objs                       :=      link-owl.o

obj-$(CONFIG_SND_SOC_OWL) += snd-soc-pcm-owl.o
obj-$(CONFIG_SND_SOC_ATC2609A) += snd-soc-atc2609a.o
obj-$(CONFIG_SND_SOC_ATC2603C) += snd-soc-atc2603c.o
obj-$(CONFIG_SND_SOC_DAI_OWL) += snd-soc-dai-owl.o
obj-$(CONFIG_SND_SOC_HDMI_OWL) += snd-soc-hdmi-owl.o
obj-$(CONFIG_SND_SOC_BLUETOOTH_PCM_OWL) += snd-soc-bluetooth-pcm-owl.o

# ATC2603A PMU Machine Support
obj-$(CONFIG_SND_SOC_ALL_PMU_OWL) += snd-soc-link-owl.o
