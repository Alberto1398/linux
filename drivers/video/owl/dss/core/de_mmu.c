/*
 * Copyright (C) 2014 Actions Corporation
 * Author: lipeng<lipeng@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define DEBUGX
#define pr_fmt(fmt) "owl_de_mmu: %s, " fmt, __func__

#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/module.h>

#include <asm/page.h>

#include <video/owl_dss.h>

#define MMU_TABLE_SIZE		(4 * 1024 * 1024)

/*
 * MMU entry used to convert *TODO*
 * to DE device address.
 *
 * --da, device address used by DE MMU
 * --dsize, device memory size
 *
 * --addr, virtual address in MMU table, which stores physical
 *	address used by DE(DA-->PA)
 */
struct owl_mmu_entry {
	u32			da;
	u32			dsize;

	__u64			stamp;

	void __iomem		*addr;

	ktime_t			time_stamp;
	struct list_head	list;
};


/*
 * DE MMU table.
 * --base, base virtual address
 * --base_phys, base physical address
 * --remain_size, remain size in byte
 * --entries, a double list to hold all allocated MMU entries
 */
struct mmu_table {
	void __iomem		*base;
	dma_addr_t		base_phys;

	void __iomem		*addr;
	u32			remain_size;

	struct list_head	entries;
};

static struct mmu_table		mmu;

static int			test_force_flush;
static int			test_no_stamp;

static inline void mmu_flush_entry(void);

/*
 * MMU table memory operations
 */

/* allocate memory from MMU table */
static int mmu_alloc_memory(void __iomem **addr, int size)
{
	pr_debug("size %d, remain size %d\n", size, mmu.remain_size);

	if (mmu.remain_size < size || test_force_flush == 1) {
		pr_info("MMU table no space, flush it\n");

		test_force_flush = 0;
		mmu_flush_entry();
	}

	*addr = mmu.addr;
	mmu.addr += size;
	mmu.remain_size -= size;

	pr_debug("addr 0x%p, mmu.addr 0x%p, mmu.remain_size %d\n",
		 *addr, mmu.addr, mmu.remain_size);

	return 0;
}


/*
 * struct owl_mmu_entry operations
 */

#define mmu_alloc_entry() kzalloc(sizeof(*entry), GFP_KERNEL)
#define mmu_free_entry(entry) kfree(entry)

static inline void mmu_add_entry(struct owl_mmu_entry *entry)
{
	entry->time_stamp = ktime_get();
	list_add_tail(&entry->list, &mmu.entries);
}

#define mmu_delete_entry(entry) list_del(&entry->list)

/*
 * touch a recently used entry,
 * and re-order it to MMU entry list's tail
 */
static inline void mmu_touch_entry(struct owl_mmu_entry *entry)
{
	entry->time_stamp = ktime_get();
	list_move_tail(&entry->list, &mmu.entries);
}

static inline void mmu_flush_entry(void)
{
	struct owl_mmu_entry *entry, *tmp;

	mmu.addr = mmu.base;
	mmu.remain_size = MMU_TABLE_SIZE;

	/* recyle all the mmu entry */
	list_for_each_entry_safe(entry, tmp, &mmu.entries, list) {
		mmu_delete_entry(entry);
		mmu_free_entry(entry);
	}
}

static inline bool mmu_find_entry_by_stamp(struct owl_mmu_entry **found,
					   __u64 stamp)
{
	bool is_found = false;
	struct owl_mmu_entry *entry;

	/*
	 * the recently entries is in list tail,
	 * which will be the one we are searching,
	 * so search it from tail to head
	 */
	list_for_each_entry_reverse(entry, &mmu.entries, list) {
		if (entry->stamp == stamp) {
			is_found = true;
			*found = entry;
			break;
		}
	}

	return is_found;
}


/*=======================================================================
 *			APIs to others
 *=====================================================================*/
int owl_de_mmu_init(struct device *dev)
{
	void __iomem *vaddr;

	/* mmu.base_phys must 64bytes align */
	vaddr = dma_alloc_coherent(dev, MMU_TABLE_SIZE,
				   &mmu.base_phys, GFP_KERNEL);
	if (!vaddr) {
		pr_err("fail to allocate mmu table mem (size: %dK))\n",
		       MMU_TABLE_SIZE / 1024);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&mmu.entries);

	mmu.base = vaddr;
	mmu.addr = mmu.base;
	mmu.remain_size = MMU_TABLE_SIZE;
	memset(vaddr, 0, MMU_TABLE_SIZE);

	pr_info("base 0x%p base_phys 0x%llx mmu.addr 0x%p\n",
		mmu.base, mmu.base_phys, mmu.addr);

	owl_de_mmu_config((uint32_t)mmu.base_phys);

	return 0;
}

int owl_de_mmu_sg_table_to_da(struct sg_table *sg, __u64 stamp, u32 *da)
{
	int i, j;
	int ret = 0;
	u32 page_count = 0;
	u32 *p;
	void __iomem *mmu_addr;

	struct owl_mmu_entry *entry = NULL;

	struct scatterlist *p_sc_temp, *p_scatterlist;

	pr_debug("stamp = %lld\n", stamp);

	if (test_no_stamp == 0 && mmu_find_entry_by_stamp(&entry, stamp)) {
		pr_debug("entry(stamp = %lld) is found\n", stamp);

		mmu_touch_entry(entry);
		goto ok_exit;
	} else {
		entry = NULL;
	}

	p_scatterlist = sg->sgl;
	if (p_scatterlist == NULL) {
		pr_err("Can not get p_scatterlist\n");
		return -EINVAL;
	}

	for (p_sc_temp = p_scatterlist; p_sc_temp;
		p_sc_temp = sg_next(p_sc_temp)) {
		for (i = 0; i < p_sc_temp->length; i += PAGE_SIZE)
			page_count++;
	}
	pr_debug("page_count = %d, size = %ld\n",
		 page_count, page_count * PAGE_SIZE);

	pr_debug("alloc ps info for mem record\n");
	entry = mmu_alloc_entry();
	if (entry == NULL) {
		pr_err("Failed to alloc MMU entry\n");
		goto error_exit1;
	}

	entry->dsize = page_count * sizeof(u32);

	ret = mmu_alloc_memory(&mmu_addr, entry->dsize);
	if (ret) {
		pr_err("Failed to alloc memory for devices\n");
		ret = -2;
		goto error_exit2;
	}
	pr_debug("mmu_addr = %p\n", mmu_addr);

	entry->da = (u32)(mmu_addr - mmu.base);
	entry->addr = mmu_addr;
	memset(entry->addr, 0, entry->dsize);

	pr_debug("entry da %d, dsize %d, add %p\n",
		 entry->da, entry->dsize, entry->addr);

	/* Build list of physical page addresses */
	i = 0;
	for (p_sc_temp = p_scatterlist; p_sc_temp;
		p_sc_temp = sg_next(p_sc_temp)) {
		p = (u32 *)entry->addr;
		for (j = 0; j < p_sc_temp->length; j += PAGE_SIZE) {
			p[i] = sg_phys(p_sc_temp) + j;
			pr_debug("entry->addr[%d] %p, 0x%x\n", i, &p[i], p[i]);
			i++;
		}
	}

	entry->stamp = stamp;

	pr_debug("mmu_add_entry\n");
	mmu_add_entry(entry);

	goto ok_exit;

error_exit2:
	kfree(entry);
error_exit1:
ok_exit:
	if (entry != NULL) {
		*da = ((entry->da / 4) << PAGE_SHIFT);
		pr_debug("da = 0x%d\n", *da);
	}

	return ret;
}

module_param(test_force_flush, int, 0644);
module_param(test_no_stamp, int, 0644);
