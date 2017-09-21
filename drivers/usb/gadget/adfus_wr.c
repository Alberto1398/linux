
#include <linux/module.h>
#include <linux/owl_fs_adfus.h>

/* FIXME: move symbol from adfus to kernel temporarily */

func_t AdfuUpdateMbrFromPhyToUsr;
EXPORT_SYMBOL(AdfuUpdateMbrFromPhyToUsr);

func_t1 adfu_flush_nand_cache;
EXPORT_SYMBOL(adfu_flush_nand_cache);


func_t4 adfus_nand_read;
func_t4 adfus_nand_write;
EXPORT_SYMBOL(adfus_nand_read);
EXPORT_SYMBOL(adfus_nand_write);

