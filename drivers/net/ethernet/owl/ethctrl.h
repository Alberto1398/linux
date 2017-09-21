/******************************************************************************
 ethctrl.h -- ethernet controller header for GL5201
 author : yunchf  @Actions
 date : 2010-04-08
 version 0.1

 date : 2010-08-10
 version 1.0

******************************************************************************/
#ifndef _ETHCTRL_H_
#define _ETHCTRL_H_

/*****************************************************************************/

/*#define DETECT_POWER_SAVE*/

#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>

#ifdef DETECT_POWER_SAVE
#include <linux/timer.h>
#endif
#include <linux/phy.h>

#define ASOC_ETHERNET_PHY_ADDR (0x3)
#define ASOC_ETHERNET_PHY_IRQ  IRQ_ASOC_SIRQ0

/*
 * Should not read and write er_reserved* elements
 */
typedef struct ethregs {
	unsigned int er_busmode;	/* offset 0x0; bus mode reg. */
	unsigned int er_reserved0;
	unsigned int er_txpoll;	/* 0x08; transmit poll demand reg. */
	unsigned int er_reserved1;
	unsigned int er_rxpoll;	/* 0x10; receive poll demand reg. */
	unsigned int er_reserved2;
	unsigned int er_rxbdbase;
	/* 0x18; receive descriptor list base address reg. */
	unsigned int er_reserved3;
	unsigned int er_txbdbase;
	/* 0x20; transmit descriptor list base address reg. */
	unsigned int er_reserved4;
	unsigned int er_status;	/* 0x28; status reg. */
	unsigned int er_reserved5;
	unsigned int er_opmode;	/* 0x30; operation mode reg. */
	unsigned int er_reserved6;
	unsigned int er_ienable;	/* 0x38; interrupt enable reg. */
	unsigned int er_reserved7;
	unsigned int er_mfocnt;
	/* 0x40; missed frames and overflow counter reg. */
	unsigned int er_reserved8;
	unsigned int er_miimng;
	/* 0x48; software mii, don't use it here  */
	unsigned int er_reserved9;
	unsigned int er_miism;	/* 0x50; mii serial management */
	unsigned int er_reserved10;
	unsigned int er_imctrl;
	/* 0x58; general-purpose timer and interrupt mitigation control */
	unsigned int er_reserved11[9];
	unsigned int er_maclow;	/* 0x80; mac address low */
	unsigned int er_reserved12;
	unsigned int er_machigh;	/* 0x88; mac address high */
	unsigned int er_reserved13;
	unsigned int er_cachethr;
	/* 0x90; pause time and cache thresholds */
	unsigned int er_reserved14;
	unsigned int er_fifothr;
	/* 0x98; pause control fifo thresholds */
	unsigned int er_reserved15;
	unsigned int er_flowctrl;
	/* 0xa0; flow control setup and status */
	unsigned int er_reserved16[3];
	unsigned int er_macctrl;
	/* 0xb0; mac control */
	unsigned int er_reserved17[83];

	unsigned int er_rxstats[31];
	/* 0x200~0x278; receive statistics regs. */
	unsigned int er_reserved18[33];
	unsigned int er_txstats[41];
	/* 0x300~0x3A0; transmit statistics regs. */
	unsigned int er_reserved19[23];
} ethregs_t;

/* receive and transmit buffer descriptor */
typedef struct buffer_descriptor {
	unsigned int status;
	unsigned int control;
	unsigned int buf_addr;
	unsigned int reserved;	/* we don't use second buffer address */
} ec_bd_t;

/** ethernet clock enable */
#define ASOC_ETH_CLOCK_EN  (0x1 << 22)
#define ASOC_ETH_CLOCK_RST (0x1 << 20)

/** rx bd status and control information */
#define RXBD_STAT_OWN (0x1 << 31)
#define RXBD_STAT_FF  (0x1 << 30)	/*filtering fail */
#define RXBD_STAT_FL(x) (((x) >> 16) & 0x3FFF)	/* frame leng */
#define RXBD_STAT_ES  (0x1 << 15)	/* error summary */
#define RXBD_STAT_DE  (0x1 << 14)	/* descriptor error */
#define RXBD_STAT_RF  (0x1 << 11)	/* runt frame */
#define RXBD_STAT_MF  (0x1 << 10)	/* multicast frame */
#define RXBD_STAT_FS  (0x1 << 9)	/* first descriptor */
#define RXBD_STAT_LS  (0x1 << 8)	/* last descriptor */
#define RXBD_STAT_TL  (0x1 << 7)	/* frame too long */
#define RXBD_STAT_CS  (0x1 << 6)	/* collision */
#define RXBD_STAT_FT  (0x1 << 5)	/* frame type */
#define RXBD_STAT_RE  (0x1 << 3)	/* mii error */
#define RXBD_STAT_DB  (0x1 << 2)	/* byte not aligned */
#define RXBD_STAT_CE  (0x1 << 1)	/* crc error */
#define RXBD_STAT_ZERO  (0x1)

#define RXBD_CTRL_RER (0x1 << 25)	/* receive end of ring */
#define RXBD_CTRL_RCH (0x1 << 24)
/* using second buffer, not used here */
#define RXBD_CTRL_RBS1(x) ((x) & 0x7FF)	/* buffer1 size */

/** tx bd status and control information */
#define TXBD_STAT_OWN (0x1 << 31)
#define TXBD_STAT_ES  (0x1 << 15)	/* error summary */
#define TXBD_STAT_LO  (0x1 << 11)	/* loss of carrier */
#define TXBD_STAT_NC  (0x1 << 10)	/* no carrier */
#define TXBD_STAT_LC  (0x1 << 9)	/* late collision */
#define TXBD_STAT_EC  (0x1 << 8)	/* excessive collision */
#define TXBD_STAT_CC(x)  (((x) >> 3) & 0xF)	/*  */
#define TXBD_STAT_UF  (0x1 << 1)	/* underflow error */
#define TXBD_STAT_DE  (0x1)	/* deferred */

#define TXBD_CTRL_IC   (0x1 << 31)	/* interrupt on completion */
#define TXBD_CTRL_LS   (0x1 << 30)	/* last descriptor */
#define TXBD_CTRL_FS   (0x1 << 29)	/* first descriptor */
#define TXBD_CTRL_FT1  (0x1 << 28)	/* filtering type  */
#define TXBD_CTRL_SET  (0x1 << 27)	/* setup packet */
#define TXBD_CTRL_AC   (0x1 << 26)	/* add crc disable */
#define TXBD_CTRL_TER  (0x1 << 25)	/* transmit end of ring */
#define TXBD_CTRL_TCH  (0x1 << 24)	/* second address chainded */
#define TXBD_CTRL_DPD  (0x1 << 23)	/* disabled padding */
#define TXBD_CTRL_FT0  (0x1 << 22)
/* filtering type, togethor with 28bit */
#define TXBD_CTRL_TBS2(x)  (((x) & 0x7FF) << 11)
/* buf2 size, no use here */
#define TXBD_CTRL_TBS1(x)  ((x) & 0x7FF)	/* buf1 size */
#define TXBD_CTRL_TBS1M (0x7FF)

/* bus mode register */
#define EC_BMODE_DBO    (0x1 << 20)	/*descriptor byte ordering mode */
#define EC_BMODE_TAP(x) (((x) & 0x7) << 17)	/*transmit auto-polling */
#define EC_BMODE_PBL(x) (((x) & 0x3F) << 8)	/*programmable burst length */
#define EC_BMODE_BLE    (0x1 << 7)
/*big or little endian for data buffers */
#define EC_BMODE_DSL(x) (((x) & 0x1F) << 2)	/*descriptors skip length */
#define EC_BMODE_BAR    (0x1 << 1)	/*bus arbitration mode */
#define EC_BMODE_SWR    (0x1)	/*software reset */

/* transmit and receive poll demand register */
#define EC_TXPOLL_ST  (0x1)
/* leave suspended mode to running mode to start xmit */
#define EC_RXPOLL_SR  (0x1)	/* leave suspended to running mode */

/* status register */
#define EC_STATUS_TSM  (0x7 << 20)	/*transmit process state */
#define EC_TX_run_dsp  (0x3 << 20)
#define EC_STATUS_RSM  (0x7 << 17)	/*receive process state */
#define EC_RX_fetch_dsp (0x1 << 17)
#define EC_RX_close_dsp (0x5 << 17)
#define EC_RX_run_dsp 	(0x7 << 17)
#define EC_STATUS_NIS  (0x1 << 16)	/*normal interrupt summary */
#define EC_STATUS_AIS  (0x1 << 15)	/*abnormal interrupt summary */
#define EC_STATUS_ERI  (0x1 << 14)	/*early receive interrupt */
#define EC_STATUS_GTE  (0x1 << 11)	/*general-purpose timer expiration */
#define EC_STATUS_ETI  (0x1 << 10)	/*early transmit interrupt */
#define EC_STATUS_RPS  (0x1 << 8)	/*receive process stopped */
#define EC_STATUS_RU   (0x1 << 7)	/*receive buffer unavailable */
#define EC_STATUS_RI   (0x1 << 6)	/*receive interrupt */
#define EC_STATUS_UNF  (0x1 << 5)	/*transmit underflow */
#define EC_STATUS_LCIS (0x1 << 4)	/* link change status */
#define EC_STATUS_LCIQ (0x1 << 3)	/* link change interrupt */
#define EC_STATUS_TU   (0x1 << 2)	/*transmit buffer unavailable */
#define EC_STATUS_TPS  (0x1 << 1)	/*transmit process stopped */
#define EC_STATUS_TI   (0x1)	/*transmit interrupt */

/* operation mode register */
#define EC_OPMODE_RA  (0x1 << 30)	/*receive all */
#define EC_OPMODE_TTM (0x1 << 22)	/*transmit threshold mode */
#define EC_OPMODE_SF  (0x1 << 21)	/*store and forward */
#define EC_OPMODE_SPEED(x) (((x) & 0x3) << 16)	/*eth speed selection */
#define EC_OPMODE_10M (0x1 << 17)
/* set when work on 10M, otherwise 100M */
#define EC_OPMODE_TR(x)    (((x) & 0x3) << 14)	/*threshold control bits */
#define EC_OPMODE_ST  (0x1 << 13)	/*start or stop transmit command */
#define EC_OPMODE_LP  (0x1 << 10)	/*loopback mode */
#define EC_OPMODE_FD  (0x1 << 9)	/*full duplex mode */
#define EC_OPMODE_PM  (0x1 << 7)	/*pass all multicast */
#define EC_OPMODE_PR  (0x1 << 6)	/*prmiscuous mode */
#define EC_OPMODE_IF  (0x1 << 4)	/*inverse filtering */
#define EC_OPMODE_PB  (0x1 << 3)	/*pass bad frames */
#define EC_OPMODE_HO  (0x1 << 2)	/*hash only filtering mode */
#define EC_OPMODE_SR  (0x1 << 1)	/*start or stop receive command */
#define EC_OPMODE_HP  (0x1)	/*hash or perfect receive filtering mode */

/* interrupt enable register */
#define EC_IEN_LCIE (0x1 << 17)	/*link change interrupt enable */
#define EC_IEN_NIE (0x1 << 16)	/*normal interrupt summary enable */
#define EC_IEN_AIE (0x1 << 15)	/*abnormal interrupt summary enable */
#define EC_IEN_ERE (0x1 << 14)	/*early receive interrupt enable */
#define EC_IEN_GTE (0x1 << 11)	/*general-purpose timer overflow */
#define EC_IEN_ETE (0x1 << 10)	/*early transmit interrupt enable */
#define EC_IEN_RSE (0x1 << 8)	/*receive stopped enable */
#define EC_IEN_RUE (0x1 << 7)	/*receive buffer unavailable enable */
#define EC_IEN_RIE (0x1 << 6)	/*receive interrupt enable */
#define EC_IEN_UNE (0x1 << 5)	/*underflow interrupt enable */
#define EC_IEN_TUE (0x1 << 2)	/*transmit buffer unavailable enable */
#define EC_IEN_TSE (0x1 << 1)	/*transmit stopped enable */
#define EC_IEN_TIE (0x1)	/*transmit interrupt enable */
#define EC_IEN_ALL (0x1CDE3)	/* TU interrupt disabled */

/* missed frames and overflow counter register */
#define EC_MFOCNT_OCO  (0x1 << 28)	/*overflow flag */
#define EC_MFOCNT_FOCM (0x3FF << 17)	/*fifo overflow counter */
#define EC_MFOCNT_MFO  (0x1 << 16)	/*missed frame flag */
#define EC_MFOCNT_MFCM (0xFFFF)	/*missed frame counter */

/* the mii serial management register */
#define MII_MNG_SB  (0x1 << 31)	/*start transfer or busy */
#define MII_MNG_CLKDIV(x) (((x) & 0x7) << 28)	/*clock divider */
#define MII_MNG_OPCODE(x) (((x) & 0x3) << 26)	/*operation mode */
#define MII_MNG_PHYADD(x) (((x) & 0x1F) << 21)	/*physical layer address */
#define MII_MNG_REGADD(x) (((x) & 0x1F) << 16)	/*register address */
#define MII_MNG_DATAM (0xFFFF)	/*register data mask */
#define MII_MNG_DATA(x)   ((MII_MNG_DATAM) & (x))	/* data to write */
#define MII_OP_WRITE 0x1
#define MII_OP_READ  0x2
#define MII_OP_CDS   0x3

/* general purpose timer and interrupt mitigation control register */
#define EC_IMCTRL_CS     (0x1 << 31)	/*cycle size */
#define EC_IMCTRL_TT(x)  (((x) & 0xF) << 27)	/*transmit timer */
#define EC_IMCTRL_NTP(x) (((x) & 0x7) << 24)	/*number of transmit packets */
#define EC_IMCTRL_RT(x)  (((x) & 0xF) << 20)	/*receive timer */
#define EC_IMCTRL_NRP(x) (((x) & 0x7) << 17)	/*number of receive packets */
#define EC_IMCTRL_CON    (0x1 << 16)	/*continuous mode */
#define EC_IMCTRL_TIMM   (0xFFFF)	/*timer value */
#define EC_IMCTRL_TIM(x) ((x) & 0xFFFF)	/*timer value */

/* pause time and cache thresholds register */
#define EC_CACHETHR_CPTL(x) (((x) & 0xFF) << 24)
/*cache pause threshold level */
#define EC_CACHETHR_CRTL(x) (((x) & 0xFF) << 16)
/*cache restart threshold level */
#define EC_CACHETHR_PQT(x)  ((x) & 0xFFFF)
/*flow control pause quanta time */

/* fifo thresholds register */
#define EC_FIFOTHR_FPTL(x) (((x) & 0xFFFF) << 16)
/*fifo pause threshold level */
#define EC_FIFOTHR_FRTL(x) ((x) & 0xFFFF)
/*fifo restart threshold level */

/* flow control setup and status */
#define EC_FLOWCTRL_FCE (0x1 << 31)	/*flow control enable */
#define EC_FLOWCTRL_TUE (0x1 << 30)	/*transmit un-pause frames enable */
#define EC_FLOWCTRL_TPE (0x1 << 29)	/*transmit pause frames enable */
#define EC_FLOWCTRL_RPE (0x1 << 28)	/*receive pause frames enable */
#define EC_FLOWCTRL_BPE (0x1 << 27)
/*back pressure enable (only half-dup) */
#define EC_FLOWCTRL_ENALL (0x1F << 27)
#define EC_FLOWCTRL_PRS (0x1 << 1)	/*pause request sent */
#define EC_FLOWCTRL_HTP (0x1)	/*host transmission paused */

/** mac control register */
#define EC_MACCTRL_RRSB (0x1 << 8)	/*RMII_REFCLK select bit */
#define EC_MACCTRL_SSDC(x) (((x) & 0xF) << 4)	/*SMII SYNC delay half cycle */
#define EC_MACCTRL_RCPS (0x1 << 1)	/*REF_CLK phase select */
#define EC_MACCTRL_RSIS (0x1 << 0)	/*RMII or SMII interface select bit */

#define MAC_CTRL_SMII (0x41)	/* use smii; bit8: 0 REFCLK output, 1 input */
#define MAC_CTRL_RMII (0x0)	/* use rmii */

/*#define ETH_TEST_MODE*/

#define MULTICAST_LIST_LEN 14
#define ETH_MAC_LEN 6
#define ETH_CRC_LEN 4
#define RX_RING_SIZE 64

#define RX_RING_MOD_MASK  (RX_RING_SIZE - 1)
#ifndef ETH_TEST_MODE
#define TX_RING_SIZE 32
#else
#define TX_RING_SIZE 128
#endif

#define TX_RING_MOD_MASK (TX_RING_SIZE - 1)
#define TATOL_TXRX_BDS (RX_RING_SIZE + TX_RING_SIZE)

#define ETH_SPEED_10M 10
#define ETH_SPEED_100M 100
#define ETH_DUP_HALF 1
#define ETH_DUP_FULL 2
#define ETH_PKG_MIN 64
#define ETH_PKG_MAX 1518
#define PKG_RESERVE 18

#define PKG_MIN_LEN (ETH_PKG_MIN)

/* 0x600, reserve 18 byes for align adjustment */
#define PKG_MAX_LEN (ETH_PKG_MAX + PKG_RESERVE)

#define PHY_ADDR_LIMIT 0x20

/* ANSI Color codes */
#define VT(CODES)  "\033[" CODES "m"
#define VT_NORMAL  VT("")
#define VT_RED	   VT("0;32;31")
#define VT_GREEN   VT("1;32")
#define VT_YELLOW  VT("1;33")
#define VT_BLUE    VT("1;34")
#define VT_PURPLE  VT("0;35")

#define EC_TRACED 0
#define EC_DEBUG
#undef RX_DEBUG
#undef TX_DEBUG
/*#define RX_DEBUG
#define TX_DEBUG

#define ETHENRET_MAC_LOOP_BACK
#define ETHENRET_PHY_LOOP_BACK
#define WORK_IN_10M_MODE
*/

#define _DBG(level, fmt, ...)  \
    printk(level "%s():%d: " fmt, \
		__func__, __LINE__, ## __VA_ARGS__)

#ifdef EC_DEBUG
#define EC_INFO(fmt, args...)  _DBG(KERN_INFO, fmt, ## args)
#else
#define EC_INFO(fmt, args...)  	do {} while (0)
#endif

#define EC_ERR(fmt, args...)   _DBG(KERN_ERR, fmt, ## args)

#define _INFO(color, fmt, ...) \
    printk(color "" fmt ""VT_NORMAL, ## __VA_ARGS__)

/* mainly used in test code */
#define INFO_PURLPLE(fmt, args...) _INFO(VT_PURPLE, fmt, ## args)
#define INFO_RED(fmt, args...)     _INFO(VT_RED, fmt, ## args)
#define INFO_GREEN(fmt, args...)   _INFO(VT_GREEN, fmt, ## args)
#define INFO_BLUE(fmt, args...)    _INFO(VT_BLUE, fmt, ## args)

/*#define EC_NOP __asm__ __volatile__ ("nop; nop; nop; nop" : :)*/
#define EC_NOP

enum eth_phy_model {
	ETH_PHY_MODEL_ATC2605 = 0,	/* same as fdp110 */
	ETH_PHY_MODEL_KSZ8081,	/* Micrel KSZ8041TL */
	ETH_PHY_MODEL_MAX,
};

enum eth_phy_mode {
	ETH_PHY_MODE_RMII = 0,
	ETH_PHY_MODE_SMII,
	ETH_PHY_MODE_MAX,
};

typedef struct mac_list {
	int count;
	char mac_array[MULTICAST_LIST_LEN][ETH_MAC_LEN];
} mac_list_t;

typedef struct phy_info phy_info_t;

struct green_led {
	int gpio_num;
	int gpio_low_active;
};

typedef struct dev_priv {
	struct platform_device *pdev;
	volatile ethregs_t *hwrp;
	struct clk *clk;
	spinlock_t lock;
	struct net_device *netdev;
	struct atc260x_dev *atc260x;
	unsigned int mac_irq;
	unsigned int phy_model;
	unsigned int phy_mode;
	struct regulator *phy_power;
	struct green_led pctl;
	struct green_led gled;
#ifdef ETH_TEST_MODE
	unsigned int test_mode;
#endif

	struct sk_buff *tx_skb[TX_RING_SIZE];
	/* temp. save transmited skb */
	ec_bd_t *tx_bd_base;
	ec_bd_t *cur_tx;	/* the next tx free ring entry */
	ec_bd_t *dirty_tx;	/* the ring entry to be freed */
	ushort skb_dirty;	/* = dirty_tx - tx_bd_base */
	ushort skb_cur;		/* = cur_tx - tx_bd_base */
	dma_addr_t tx_bd_paddr;
	bool tx_full;

	struct sk_buff *rx_skb[RX_RING_SIZE];	/* rx_bd buffers */
	ec_bd_t *rx_bd_base;
	ec_bd_t *cur_rx;	/* the next rx free ring entry */
	dma_addr_t rx_bd_paddr;

#ifdef DETECT_POWER_SAVE
	struct timer_list detect_timer;
	struct work_struct power_save_work;
#endif
	struct work_struct hardware_reset_work;

	int phy_addr;
	int speed;
	int duplex;
	bool pause;
	bool autoneg;
	phy_info_t *phy_ops;
	struct mii_if_info mii_info;

	int msg_enable;
	const char *mac_addr;	/* XXX : careful later added */
	char overrided_mac[ETH_MAC_LEN];
	mac_list_t multicast_list;
	bool mac_overrided;
	bool multicast;
	bool all_multicast;
	bool promiscuous;
	bool opened;
	bool link;

	struct device_node *phy_node;
	phy_interface_t phy_interface;

	struct phy_device *phy;
	int mdio_irqs[PHY_MAX_ADDR];

	struct mii_bus *mdio;
	int phy_irq;
#ifdef DETECT_POWER_SAVE
	bool enable;
#endif
} ec_priv_t;

struct phy_info {
	int id;
	char *name;
	int (*phy_hw_init) (ec_priv_t *ecp);
	int (*phy_init) (ec_priv_t *ecp);
	int (*phy_suspend) (ec_priv_t *ecp, bool power_down);
	int (*phy_setup_advert) (ec_priv_t *ecp, int advertising);
	int (*phy_setup_forced) (ec_priv_t *ecp, int speed, int duplex);
	int (*phy_setup_aneg) (ec_priv_t *ecp, bool autoneg);
	int (*phy_setup_loopback) (ec_priv_t *ecp, bool loopback);
	int (*phy_read_status) (ec_priv_t *ecp);
	int (*phy_get_link) (ec_priv_t *ecp);
};

unsigned short read_phy_reg(ec_priv_t *, unsigned short);
int write_phy_reg(ec_priv_t *, unsigned short, unsigned short);

void ep_set_phy_ops(ec_priv_t *ecp);
void ec_set_ethtool_ops(struct net_device *netdev);

static inline bool need_change_speed(unsigned short new_bmcr,
				     unsigned short old_bmcr)
{
	return (((new_bmcr ^ old_bmcr) & (BMCR_SPEED100 | BMCR_FULLDPLX)) ==
		BMCR_FULLDPLX);
}

/******************************************************************************/

#endif /* _ETHCTRL_H_ */
