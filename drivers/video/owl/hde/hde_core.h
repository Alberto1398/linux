#ifndef _HDE_CORE_H_
#define _HDE_CORE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HDE_REG_NO {
	HDE_REG0 = 0,
	HDE_REG1 , HDE_REG2,  HDE_REG3,  HDE_REG4,  HDE_REG5,  HDE_REG6,
	HDE_REG7,  HDE_REG8,  HDE_REG9 , HDE_REG10, HDE_REG11, HDE_REG12,
	HDE_REG13, HDE_REG14, HDE_REG15, HDE_REG16, HDE_REG17, HDE_REG18,
	HDE_REG19, HDE_REG20, HDE_REG21, HDE_REG22, HDE_REG23, HDE_REG24,
	HDE_REG25, HDE_REG26, HDE_REG27, HDE_REG28, HDE_REG29, HDE_REG30,
	HDE_REG31, HDE_REG32, HDE_REG33, HDE_REG34, HDE_REG35, HDE_REG36,
	HDE_REG37, HDE_REG38, HDE_REG39, HDE_REG40, HDE_REG41, HDE_REG42,
	HDE_REG43, HDE_REG44, HDE_REG45, HDE_REG46, HDE_REG47, HDE_REG48,
	HDE_REG49, HDE_REG50, HDE_REG51, HDE_REG52, HDE_REG53, HDE_REG54,
	HDE_REG55, HDE_REG56, HDE_REG57, HDE_REG58, HDE_REG59, HDE_REG60,
	HDE_REG61, HDE_REG62, HDE_REG63, HDE_REG64, HDE_REG65, HDE_REG66,
	HDE_REG67, HDE_REG68, HDE_REG69, HDE_REG70, HDE_REG71, HDE_REG72,
	HDE_REG73, HDE_REG74, HDE_REG75, HDE_REG76, HDE_REG77, HDE_REG78,
	HDE_REG79, HDE_REG80, HDE_REG81, HDE_REG82, HDE_REG83, HDE_REG84,
	HDE_REG85, HDE_REG86, HDE_REG87, HDE_REG88, HDE_REG89, HDE_REG90,
	HDE_REG91, HDE_REG92, HDE_REG93, HDE_REG94, HDE_REG_MAX
} HDE_RegNO_t;

#define MAX_HDE_REG_NUM         (HDE_REG_MAX+1)

#define CODEC_CUSTOMIZE_ADDR            (HDE_REG_MAX)
#define CODEC_CUSTOMIZE_VALUE_PERFORMANCE  0x00000001
#define CODEC_CUSTOMIZE_VALUE_LOWPOWER     0x00000002
#define CODEC_CUSTOMIZE_VALUE_DROPFRAME    0x00000004
#define CODEC_CUSTOMIZE_VALUE_MAX          0xffffffff

typedef enum HDE_STATUS {
	HDE_STATUS_IDLE                 = 0x1,
	HDE_STATUS_READY_TO_RUN,
	HDE_STATUS_RUNING,
	HDE_STATUS_GOTFRAME,
	HDE_STATUS_JPEG_SLICE_READY     = 0x100,
	HDE_STATUS_DIRECTMV_FULL,
	HDE_STATUS_STREAM_EMPTY,
	HDE_STATUS_ASO_DETECTED,
	HDE_STATUS_TIMEOUT              = -1,
	HDE_STATUS_STREAM_ERROR         = -2,
	HDE_STATUS_BUS_ERROR            = -3,
	HDE_STATUS_DEAD                 = -4,
	HDE_STATUS_UNKNOWN_ERROR        = -0x100
} HDE_Status_t;

typedef struct hde_handle {
	/* read Reg*/
	unsigned int (*readReg)(struct hde_handle*, HDE_RegNO_t);

	/* write Reg, except reg1*/
	int (*writeReg)(struct hde_handle*, HDE_RegNO_t, const unsigned int);

	/* if return -1, status err*/
	int (*run)(struct hde_handle *);

	/*HDE status query.No block.Return HDE_STATUS_RUNING if run*/
	int (*query)(struct hde_handle*, HDE_Status_t*);

	/*HDE status query.Block.Return status until
		HDE_STATUS_DEAD or interrupted */
	int (*query_timeout)(struct hde_handle*, HDE_Status_t*);

	/*turn to idle*/
	int (*reset)(struct hde_handle *);

} hde_handle_t;


typedef struct {
	unsigned int w;
	unsigned int h;
} COMMON;

typedef struct {
	int fd_mmu;
	int fd_buf;
	unsigned int phys_addr;
} PRIVATE;

typedef struct {
	unsigned int n_bufs;
	COMMON common;
	PRIVATE priavte[17];
} MMU_FILL_PACKAGE;

/*Return NULL if param err or maximum instances*/
hde_handle_t *hde_getHandle(void);

void hde_freeHandle(struct hde_handle *);

void hde_enable_log(void);

void hde_disable_log(void);

/*Get the instance's info and regs's info */
void hde_dump_info(void);

int hde_setBuffnum(void *mmu_package, int frame_len);
void hde_setMulti(unsigned int value);

void mmu_freeHandle(void *mmu_handle);

#define bool char
#define ture 1
#define false 0

#if 1
void ion_init(void);
void ion_deinit(int iIonCtlFd);
int ion_alloc_buf(int iIonCtlFd, int size, bool bPhysContiguous, bool bCPUCached);
void ion_free_buf(int fd);
int get_ionfdpmem(int size);
int get_ionfdsystem(int size);
#endif

/**********************************************************
limited condition instances should be in the same process.
**********************************************************/
#ifdef __cplusplus
}
#endif

#endif

