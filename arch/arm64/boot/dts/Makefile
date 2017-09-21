dtb-$(CONFIG_ARCH_OWL) += s900_evb.dtb s900_evb_cts.dtb \
			 s900_ces.dtb s900_ksd.dtb s900_tpe.dtb s900_upgrade_tpe.dtb \
			 s900_evb_216ball.dtb s900_vr.dtb s900_qcb.dtb s900_96board.dtb \
			 s900_evb_216ball_mmc_boot.dtb s900_evb_mmc_boot.dtb \
			 s900_RY_VR.dtb  s900_evb2.dtb s900vr_v3s_cp.dtb s900vr_ys_2k.dtb \
			 gt9_ebox.dtb s900_upgrade.dtb s700_upgrade.dtb  gt7_ebox.dtb \
                         gt7_ebox_m8s.dtb gt7_upgrade.dtb gt7_qcb.dtb gt7_ebox_m8s_cts.dtb \
		         gt7_ebox_cts.dtb v700_gb7_wxga.dtb v700_gb7_nibiru.dtb v700_cxvr.dtb \
			 v700_cxvr_sd.dtb v700_qcb_mipi.dtb v700_qcb_lvds.dtb  s700_cb7.dtb \
			 s700_cb7_sd.dtb
			 

dtb-$(CONFIG_ARCH_VEXPRESS) += rtsm_ve-aemv8a.dtb foundation-v8.dtb \
				fvp-base-gicv2-psci.dtb
dtb-$(CONFIG_ARCH_VEXPRESS) += juno.dtb
dtb-$(CONFIG_ARCH_XGENE) += apm-mustang.dtb

targets += dtbs
DTC_FLAGS ?= -R -4 -p 0x1000
DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
targets += $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := dts/*.dtb *.dtb
