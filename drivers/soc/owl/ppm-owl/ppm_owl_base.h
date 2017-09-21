#ifndef __PPM_OWL_BASE_H__
#define __PPM_OWL_BASE_H__

#ifdef __cplusplus
extern "C" {
#endif

extern int lock_cpu(int cpu_nr);
extern int unlock_cpu(void);

extern int set_max_freq(void);
extern int set_max_freq_range(void);
extern int reset_freq_range(void);

extern int ppm_owl_base_init(void);
extern void ppm_owl_base_exit(void);

#ifdef __cplusplus
}
#endif
#endif
