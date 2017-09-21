#
# Copyright (c) 2015 Actions Semi Co., Ltd.
#
# This software is licensed under the terms of the GNU General Public
# License version 2, as published by the Free Software Foundation, and
# may be copied, distributed, and modified under those terms.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# Author: Lipeng<lipeng@actions-semi.com>
#
# Change log:
#	2015/8/20: Created by Lipeng.
obj-$(CONFIG_VIDEO_OWL_HDMIC) += owl_hdmi.o

owl_hdmi-y += core.o packet.o ddc.o edid.o
owl_hdmi-y += ddc.o
owl_hdmi-y += edid.o
owl_hdmi-y += hdcp.o
owl_hdmi-y += hdmi_cec.o
owl_hdmi-y += hdmi_cec_ctrl.o

owl_hdmi-$(CONFIG_VIDEO_OWL_HDMI_IP_SX00) += ip-sx00.o
