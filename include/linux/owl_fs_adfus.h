
#ifndef _LINUX_OWL_FS_ADFUS_H
#define _LINUX_OWL_FS_ADFUS_H


struct uparam {
	unsigned int flash_partition;
	unsigned int devnum_in_phypart;
};

typedef unsigned int (*func_t)(unsigned int *, void *);
extern func_t AdfuUpdateMbrFromPhyToUsr;

typedef void (*func_t1)(void);
extern func_t1 adfu_flush_nand_cache;


typedef int (*func_t4)(unsigned long, unsigned long , void *, void *);

extern func_t4 adfus_nand_read;
extern func_t4 adfus_nand_write;


#endif /* _LINUX_OWL_FS_ADFUS_H */
