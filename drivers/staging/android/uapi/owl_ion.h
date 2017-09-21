/*
 * ION define for Actions OWL SoC family
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#if !defined(__KERNEL__)
#define __user
#endif

#ifndef _UAPI_LINUX_OWL_ION_H
#define _UAPI_LINUX_OWL_ION_H

#include <linux/types.h>

struct owl_ion_phys_data {
	ion_user_handle_t handle;
	unsigned long phys_addr;
	size_t size;
};

#if defined(__KERNEL__)
struct owl_ion_phys_data_compat {
	/* as owl_ion_phys_data, the compat (32bit) vetsion
	 * see compat_ion_allocation_data, compat_ion_custom_data for reference. */
	compat_int_t handle;
	compat_ulong_t phys_addr;
	compat_size_t size;
};
#endif


/* Custom Ioctl's. */
enum {
	OWL_ION_GET_PHY = 0,
};

/**
 * These are the only ids that should be used for Ion heap ids.
 * The ids listed are the order in which allocation will be attempted
 * if specified. Don't swap the order of heap ids unless you know what
 * you are doing!
 * Id's are spaced by purpose to allow new Id's to be inserted in-between (for
 * possible fallbacks)
 */

enum ion_heap_ids {
	ION_HEAP_ID_INVALID = -1,
	ION_HEAP_ID_PMEM = 0,
	ION_HEAP_ID_FB = 8,
	ION_HEAP_ID_SYSTEM = 12,
	ION_HEAP_ID_RESERVED = 31 /** Bit reserved for ION_SECURE flag */
};

#endif /* _UAPI_LINUX_OWL_ION_H */
