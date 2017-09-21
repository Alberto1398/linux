#ifndef _VDE_CORE_H_
#define _VDE_CORE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VDE_REG_NO {
	VDE_REG0 = 0,
	VDE_REG1,  VDE_REG2,  VDE_REG3,  VDE_REG4,  VDE_REG5,  VDE_REG6,
	VDE_REG7,  VDE_REG8,  VDE_REG9,  VDE_REG10, VDE_REG11, VDE_REG12,
	VDE_REG13, VDE_REG14, VDE_REG15, VDE_REG16, VDE_REG17, VDE_REG18,
	VDE_REG19, VDE_REG20, VDE_REG21, VDE_REG22, VDE_REG23, VDE_REG24,
	VDE_REG25, VDE_REG26, VDE_REG27, VDE_REG28, VDE_REG29, VDE_REG30,
	VDE_REG31, VDE_REG32, VDE_REG33, VDE_REG34, VDE_REG35, VDE_REG36,
	VDE_REG37, VDE_REG38, VDE_REG39, VDE_REG40, VDE_REG41, VDE_REG42,
	VDE_REG43, VDE_REG44, VDE_REG45, VDE_REG46, VDE_REG47, VDE_REG48,
	VDE_REG49, VDE_REG50, VDE_REG51, VDE_REG52, VDE_REG53, VDE_REG54,
	VDE_REG55, VDE_REG56, VDE_REG57, VDE_REG58, VDE_REG59, VDE_REG60,
	VDE_REG61, VDE_REG62, VDE_REG63, VDE_REG64, VDE_REG65, VDE_REG66,
	VDE_REG67, VDE_REG68, VDE_REG69, VDE_REG70, VDE_REG71, VDE_REG72,
	VDE_REG73, VDE_REG74, VDE_REG75, VDE_REG76, VDE_REG77, VDE_REG78,
	VDE_REG79, VDE_REG80, VDE_REG81, VDE_REG82, VDE_REG83, VDE_REG84,
	VDE_REG85, VDE_REG86, VDE_REG87, VDE_REG88, VDE_REG89, VDE_REG90,
	VDE_REG91, VDE_REG92, VDE_REG93, VDE_REG94, VDE_REG_MAX
} VDE_RegNO_t;


#define MAX_VDE_REG_NUM         (VDE_REG_MAX+1)
#define CODEC_CUSTOMIZE_ADDR            (VDE_REG_MAX)
#define CODEC_CUSTOMIZE_VALUE_PERFORMANCE  0x00000001
#define CODEC_CUSTOMIZE_VALUE_LOWPOWER     0x00000002
#define CODEC_CUSTOMIZE_VALUE_DROPFRAME    0x00000004
#define CODEC_CUSTOMIZE_VALUE_MAX          0xffffffff

typedef enum VDE_STATUS {
	VDE_STATUS_IDLE                = 0x1,
	VDE_STATUS_READY_TO_RUN,
	VDE_STATUS_RUNING,
	VDE_STATUS_GOTFRAME,

	VDE_STATUS_JPEG_SLICE_READY    = 0x100,
	VDE_STATUS_DIRECTMV_FULL,
	VDE_STATUS_STREAM_EMPTY,

	VDE_STATUS_ASO_DETECTED,
	VDE_STATUS_TIMEOUT             = -1,
	VDE_STATUS_STREAM_ERROR        = -2,
	VDE_STATUS_BUS_ERROR           = -3,
	VDE_STATUS_DEAD                = -4,
	VDE_STATUS_UNKNOWN_ERROR       = -0x100
} VDE_Status_t;

typedef struct vde_handle {
	/* read Reg */
	unsigned int (*readReg) (struct vde_handle *, VDE_RegNO_t);

	/* write Reg, except reg1 */
	int (*writeReg) (struct vde_handle *, VDE_RegNO_t, const unsigned int);

	/* if return -1, status err */
	int (*run) (struct vde_handle *);

	/*VDE status query.No block.Return VDE_STATUS_RUNING if run */
	int (*query) (struct vde_handle *, VDE_Status_t *);

	/*VDE status query.Block.Return status until
	VDE_STATUS_DEAD or interrupted */
	int (*query_timeout) (struct vde_handle *, VDE_Status_t *);

	/*turn to idle */
	int (*reset) (struct vde_handle *);

} vde_handle_t;

/*Return NULL if param err or maximum instances*/
vde_handle_t *vde_getHandle(void);

void vde_freeHandle(struct vde_handle *);

void vde_enable_log(void);

void vde_disable_log(void);

/*Get the instance's info and regs's info */
void vde_dump_info(void);


/**********************************************************
limited condition£º instances should be in the same process.
**********************************************************/
#ifdef __cplusplus
}
#endif

#endif

