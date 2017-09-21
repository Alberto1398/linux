#ifndef __PPM_OWL_SYSFS_H__
#define __PPM_OWL_SYSFS_H__

#ifdef __cplusplus
extern "C" {
#endif

extern struct kobject *ppm_owl_global_kobject;
extern int ppm_owl_sysfs_init(void);
extern void ppm_owl_sysfs_exit(void);

#ifdef __cplusplus
}
#endif
#endif
