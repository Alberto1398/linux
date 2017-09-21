/*
* Automatically generated register definition: don't edit
* GL5209 Spec Version_V2.00
* Sat 03-13-2014  10:22:15
*/
#ifndef __SI_REG_H___
#define __SI_REG_H___

/*--------------Bits Location------------------------------------------*/
/*--------------ISP-------------------------------------------*/
/*--------------Register Address---------------------------------------*/
#define    MODULE_BASE                          0xE0268000
#define    MODULE_ENABLE                        (MODULE_BASE+0x00)
#define    SI_BUS_PRIORITY_CTL	                (MODULE_BASE+0x04)
#define    SI_SCALE_SET			        (MODULE_BASE+0x10)
#define    CH_PRELINE_SET			(MODULE_BASE+0x14)
#define    CH1_ROW_RANGE			(MODULE_BASE+0x18)
#define    CH1_COL_RANGE			(MODULE_BASE+0x1c)
#define    CH2_ROW_RANGE			(MODULE_BASE+0x20)
#define    CH2_COL_RANGE			(MODULE_BASE+0x24)
#define    SI_CH1_VTD_CTL			(MODULE_BASE+0x30)
#define    SI_CH2_VTD_CTL			(MODULE_BASE+0x34)
#define    SI_SCALE_OUT_SIZE			(MODULE_BASE+0x38)
#define    SI_CH1_DS0_OUT_FMT			(MODULE_BASE+0x40)
#define    SI_CH1_DS0_OUT_ADDRY		        (MODULE_BASE+0x44)
#define    SI_CH1_DS0_OUT_ADDRU			(MODULE_BASE+0x48)
#define    SI_CH1_DS0_OUT_ADDRV			(MODULE_BASE+0x4c)
#define    SI_CH1_DS1_OUT_FMT			(MODULE_BASE+0x50)
#define    SI_CH1_DS1_OUT_ADDRY			(MODULE_BASE+0x54)
#define    SI_CH1_DS1_OUT_ADDRU			(MODULE_BASE+0x58)
#define    SI_CH1_DS1_OUT_ADDRV			(MODULE_BASE+0x5c)
#define    SI_CH2_DS_OUT_FMT			(MODULE_BASE+0x60)
#define    SI_CH2_DS_OUT_ADDRY			(MODULE_BASE+0x64)
#define    SI_CH2_DS_OUT_ADDRU			(MODULE_BASE+0x68)
#define    SI_CH2_DS_OUT_ADDRV		        (MODULE_BASE+0x6c)
#define    SI_DR_OUT_ADDR			(MODULE_BASE+0x70)
#define    MODULE_STAT				(MODULE_BASE+0x80)
#define    SI_DEBUG			        (MODULE_BASE+0x90)

/*--------------Bits Location------------------------------------------*/
/*--------------CSI0-------------------------------------------*/
/*--------------Register Address---------------------------------------*/
#define     CSI_BASE                            0xE0240000
#define     CSI_CTRL                            (CSI_BASE+0x00)
#define     CSI_SHORT_PACKET                    (CSI_BASE+0x04)
#define     CSI_ERROR_PENDING                   (CSI_BASE+0x08)
#define     CSI_STATUS_PENDING                  (CSI_BASE+0x0c)
#define     CSI_LANE_STATUS                     (CSI_BASE+0x10)
#define     CSI_PHY_T0                          (CSI_BASE+0x14)
#define     CSI_PHY_T1                          (CSI_BASE+0x18)
#define     CSI_PHY_T2                          (CSI_BASE+0x1c)
#define     CSI_ANALOG_PHY                      (CSI_BASE+0x20)
#define     CSI_PH                              (CSI_BASE+0x24)
#define     CSI_PIN_MAP                         (CSI_BASE+0x28)
#define     CSI_CONTEXT0_CFG                    (CSI_BASE+0x100)
#define     CSI_CONTEXT0_STATUS                 (CSI_BASE+0x104)
#define     CSI_CONTEXT1_CFG                    (CSI_BASE+0x120)
#define     CSI_CONTEXT1_STATUS                 (CSI_BASE+0x124)
#define     CSI_TEST_CONTROL                    (CSI_BASE+0x130)
#define     CSI_TEST_DATA                       (CSI_BASE+0x134)

/*--------------Bits Location------------------------------------------*/
/*--------------CMU-------------------------------------------*/
/*--------------Register Address---------------------------------------*/
#define     CMU_BASE                            0xE0168000
#define     CMU_COREPLL                         (CMU_BASE+0x0000)
#define     CMU_DEVPLL                          (CMU_BASE+0x0004)
#define     CMU_DDRPLL                          (CMU_BASE+0x0008)
#define     CMU_NANDPLL                         (CMU_BASE+0x000C)
#define     CMU_DISPLAYPLL                      (CMU_BASE+0x0010)
#define     CMU_AUDIOPLL                        (CMU_BASE+0x0014)
#define     CMU_TVOUTPLL                        (CMU_BASE+0x0018)
#define     CMU_BUSCLK                          (CMU_BASE+0x001C)
#define     CMU_SENSORCLK                       (CMU_BASE+0x0020)
#define     CMU_LCDCLK                          (CMU_BASE+0x0024)
#define     CMU_DSICLK                          (CMU_BASE+0x0028)
#define     CMU_CSICLK                          (CMU_BASE+0x002C)
#define     CMU_DECLK                           (CMU_BASE+0x0030)
#define     CMU_SICLK                           (CMU_BASE+0x0034)
#define     CMU_IMXCLK                          (CMU_BASE+0x0038)
#define     CMU_HDECLK                          (CMU_BASE+0x003C)
#define     CMU_VDECLK                          (CMU_BASE+0x0040)
#define     CMU_VCECLK                          (CMU_BASE+0x0044)
#define     CMU_NANDCCLK                        (CMU_BASE+0x004C)
#define     CMU_SD0CLK                          (CMU_BASE+0x0050)
#define     CMU_SD1CLK                          (CMU_BASE+0x0054)
#define     CMU_SD2CLK                          (CMU_BASE+0x0058)
#define     CMU_UART0CLK                        (CMU_BASE+0x005C)
#define     CMU_UART1CLK                        (CMU_BASE+0x0060)
#define     CMU_UART2CLK                        (CMU_BASE+0x0064)
#define     CMU_PWM0CLK                         (CMU_BASE+0x0070)
#define     CMU_PWM1CLK                         (CMU_BASE+0x0074)
#define     CMU_PWM2CLK                         (CMU_BASE+0x0078)
#define     CMU_PWM3CLK                         (CMU_BASE+0x007C)
#define     CMU_USBPLL                          (CMU_BASE+0x0080)
#define     CMU_ASSISTPLL                       (CMU_BASE+0x0084)
#define     CMU_EDPCLK                          (CMU_BASE+0x0088)
#define     CMU_GPU3DCLK                        (CMU_BASE+0x0090)
#define     CMU_CORECTL                         (CMU_BASE+0x009C)
#define     CMU_DEVCLKEN0                       (CMU_BASE+0x00A0)
#define     CMU_DEVCLKEN1                       (CMU_BASE+0x00A4)
#define     CMU_DEVRST0                         (CMU_BASE+0x00A8)
#define     CMU_DEVRST1                         (CMU_BASE+0x00AC)
#define     CMU_UART3CLK                        (CMU_BASE+0x00B0)
#define     CMU_UART4CLK                        (CMU_BASE+0x00B4)
#define     CMU_UART5CLK                        (CMU_BASE+0x00B8)
#define     CMU_UART6CLK                        (CMU_BASE+0x00BC)
#define     CMU_TLSCLK                          (CMU_BASE+0x00C0)
#define     CMU_SD3CLK                          (CMU_BASE+0x00C4)
#define     CMU_PWM4CLK                         (CMU_BASE+0x00C8)
#define     CMU_PWM5CLK                         (CMU_BASE+0x00CC)
#define     CMU_DIGITALDEBUG                    (CMU_BASE+0x00D0)
#define     CMU_ANALOGDEBUG                     (CMU_BASE+0x00D4)
#define     CMU_COREPLLDEBUG                    (CMU_BASE+0x00D8)
#define     CMU_DEVPLLDEBUG                     (CMU_BASE+0x00DC)
#define     CMU_DDRPLLDEBUG                     (CMU_BASE+0x00E0)
#define     CMU_NANDPLLDEBUG                    (CMU_BASE+0x00E4)
#define     CMU_DISPLAYPLLDEBUG                 (CMU_BASE+0x00E8)
#define     CMU_TVOUTPLLDEBUG0                  (CMU_BASE+0x00EC)
#define     CMU_DSIPLLDEBUG                     (CMU_BASE+0x00F0)
#define     CMU_DPPLLDEBUG                      (CMU_BASE+0x00F4)
#define     CMU_AUDIOPLL_ASSISTPLLDEBUG         (CMU_BASE+0x00F8)
#define     CMU_TVOUTPLLDEBUG1                  (CMU_BASE+0x00FC)

/*--------------Bits Location------------------------------------------*/
/*--------------GPIO_MFP_PWM-------------------------------------------*/
/*--------------Register Address---------------------------------------*/
#define     GPIO_MFP_PWM_BASE                   0xE01B0000
#define     GPIO_AOUTEN                         (GPIO_MFP_PWM_BASE+0x0000)
#define     GPIO_AINEN                          (GPIO_MFP_PWM_BASE+0x0004)
#define     GPIO_ADAT                           (GPIO_MFP_PWM_BASE+0x0008)
#define     GPIO_BOUTEN                         (GPIO_MFP_PWM_BASE+0x000C)
#define     GPIO_BINEN                          (GPIO_MFP_PWM_BASE+0x0010)
#define     GPIO_BDAT                           (GPIO_MFP_PWM_BASE+0x0014)
#define     GPIO_COUTEN                         (GPIO_MFP_PWM_BASE+0x0018)
#define     GPIO_CINEN                          (GPIO_MFP_PWM_BASE+0x001C)
#define     GPIO_CDAT                           (GPIO_MFP_PWM_BASE+0x0020)
#define     GPIO_DOUTEN                         (GPIO_MFP_PWM_BASE+0x0024)
#define     GPIO_DINEN                          (GPIO_MFP_PWM_BASE+0x0028)
#define     GPIO_DDAT                           (GPIO_MFP_PWM_BASE+0x002C)
#define     GPIO_EOUTEN                         (GPIO_MFP_PWM_BASE+0x0030)
#define     GPIO_EINEN                          (GPIO_MFP_PWM_BASE+0x0034)
#define     GPIO_EDAT                           (GPIO_MFP_PWM_BASE+0x0038)
#define     GPIO_FOUTEN                         (GPIO_MFP_PWM_BASE+0x00F0)
#define     GPIO_FINEN                          (GPIO_MFP_PWM_BASE+0x00F4)
#define     GPIO_FDAT                           (GPIO_MFP_PWM_BASE+0x00F8)
#define     MFP_CTL0                            (GPIO_MFP_PWM_BASE+0x0040)
#define     MFP_CTL1                            (GPIO_MFP_PWM_BASE+0x0044)
#define     MFP_CTL2                            (GPIO_MFP_PWM_BASE+0x0048)
#define     MFP_CTL3                            (GPIO_MFP_PWM_BASE+0x004C)
#define     PWM_CTL0                            (GPIO_MFP_PWM_BASE+0X50)
#define     PWM_CTL1                            (GPIO_MFP_PWM_BASE+0X54)
#define     PWM_CTL2                            (GPIO_MFP_PWM_BASE+0X58)
#define     PWM_CTL3                            (GPIO_MFP_PWM_BASE+0X5C)
#define     PAD_CTL                             (GPIO_MFP_PWM_BASE+0X74)
/*--------------Bits Location------------------------------------------*/
/*---------------------------------------------------------------------*/
#endif	/*  */
