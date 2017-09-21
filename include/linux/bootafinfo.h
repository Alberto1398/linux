/*
 * arch/arm/mach-leopard/include/mach/bootafinfo.h
 *
 * Boot AFInfo interface
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_ARCH_BOOTAFINFO_H
#define __ASM_ARCH_BOOTAFINFO_H

#define OWL_NORMAL   0
#define OWL_PRODUCE    1
#define OWL_MNICHARGE 2
#define OWL_RECOVERY  3
/*
 * get boot afinfo
 */
struct chipid_data {
	char vdd_sensor[2];
	char cpu_sensor[2];
};

struct cpu0_opp_table {
	unsigned long clk; /*khz*/
	unsigned long volt; /*uv*/
};

#define CPU0_VOLT_TABLE_MAX	8
struct cpu0_opp_table_arry {
	int table_size;
	struct cpu0_opp_table table[CPU0_VOLT_TABLE_MAX];
};

extern int owl_get_boot_mode(void);
extern int owl_get_boardinfo(char *buf);
extern int owl_afi_get_sensor(struct chipid_data *data);
extern int owl_get_usb_hsdp(unsigned int *usb_hsdp);
extern int owl_get_cpu_level(unsigned int *cpu_level);
extern unsigned char owl_get_wifi_vendor_id(void);
extern int owl_update_table_volt(struct cpu0_opp_table *table, int table_size);

#endif /* __ASM_ARCH_BOOTAFINFO_H ss*/
