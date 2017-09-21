/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/11/10: Created by Lipeng.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM owl_adf

#if !defined(_ADF_OWL_ADF_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _ADF_OWL_ADF_TRACE_H_

#include <linux/tracepoint.h>
#include <linux/ktime.h>
#include "owl-adf.h"

TRACE_EVENT(owl_adf_vsync,
	TP_PROTO(struct owl_adf_interface *owlintf,
		 int duration, int deviation),
	TP_ARGS(owlintf, duration, deviation),

	TP_STRUCT__entry(
		__field(int, intf_id)
		__field(int, duration)
		__field(int, deviation)
	),
	TP_fast_assign(
		__entry->intf_id = owlintf->id;
		__entry->duration = duration;
		__entry->deviation = deviation;
	),
	TP_printk("intf %d, duration %dus, deviation %dus", __entry->intf_id,
		  __entry->duration, __entry->deviation)
);

TRACE_EVENT(owl_adf_vsync_enable,
	TP_PROTO(struct owl_adf_interface *owlintf),
	TP_ARGS(owlintf),

	TP_STRUCT__entry(
		__field(int, intf_id)
	),
	TP_fast_assign(
		__entry->intf_id = owlintf->id;
	),
	TP_printk("intf = %d", __entry->intf_id)
);

TRACE_EVENT(owl_adf_vsync_disable,
	TP_PROTO(struct owl_adf_interface *owlintf),
	TP_ARGS(owlintf),

	TP_STRUCT__entry(
		__field(int, intf_id)
	),
	TP_fast_assign(
		__entry->intf_id = owlintf->id;
	),
	TP_printk("intf = %d", __entry->intf_id)
);

#endif	/* _ADF_OWL_ADF_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE owl-adf-trace
#include <trace/define_trace.h>
