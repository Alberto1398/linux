#ifndef __PPM_OWL_FSM_H__
#define __PPM_OWL_FSM_H__

#ifdef __cplusplus
extern "C" {
#endif

enum ppm_owl_state{
	PPM_OWL_STATE_KERNEL_DEFAULT,
	PPM_OWL_STATE_USER_SCENE,
	PPM_OWL_STATE_KERNEL_THERMAL,
};

extern int ppm_enable_flag;

extern int reset_default_power(ppm_owl_user_data_t *user_date);
extern int user_scene_set_power(ppm_owl_user_data_t *user_date);

extern int ppm_owl_fsm_init(void);
extern void ppm_owl_fsm_exit(void);

#ifdef __cplusplus
}
#endif
#endif