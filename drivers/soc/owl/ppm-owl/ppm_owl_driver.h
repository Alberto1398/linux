#ifndef __PPM_OWL_IOCTL_H__
#define __PPM_OWL_IOCTL_H__

#ifdef __cplusplus
extern "C" {
#endif

#define PPM_OWL_MAGIC_NUMBER                'p'
/******************************************************************************/
#define PPM_OWL_SET_CPU                        _IOWR(PPM_OWL_MAGIC_NUMBER, 0x10, unsigned long)
#define PPM_OWL_RESET_CPU                        _IOWR(PPM_OWL_MAGIC_NUMBER, 0x30, unsigned long)

#define PPM_OWL_SET_GPU                     _IOWR(PPM_OWL_MAGIC_NUMBER, 0x50, unsigned long)
#define PPM_OWL_RESET_GPU                     _IOWR(PPM_OWL_MAGIC_NUMBER, 0x70, unsigned long)

#define PPM_OWL_SET_SCENE                     _IOWR(PPM_OWL_MAGIC_NUMBER, 0x90, unsigned long)

enum ppm_owl_sub_cmd{
	AUTO_FREQ_AUTO_CORES,/*自动开关核，自动调频*/
	MAX_FREQ_MAX_CORES,/*开所有核，固定最高频*/
	AUTO_FREQ_MAX_CORES,/*开所有核，自动调频*/
	MAX_FREQ_FIXED_CORES,/*固定核，固定最高频*/
	AUTO_FREQ_FIXED_CORES,/*固定核，自动调频*/
	BENCHMARK_FREQ_RANGE,/*最大频率范围，自动调频*/
	APK_FREQ_START,
	CMD_MAX=0xff,
};

typedef struct {
    enum ppm_owl_sub_cmd arg;  
    unsigned int timeout;
} ppm_owl_user_data_t;

#ifdef __cplusplus
}
#endif
#endif
