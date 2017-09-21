#ifndef _OWL_IRKEY_H_
#define _OWL_IRKEY_H_

#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/of.h>
#include <linux/io.h>

#define IRC_STAT_UCMP       (0x1 << 6)
#define IRC_STAT_KDCM       (0x1 << 5)
#define IRC_STAT_RCD        (0x1 << 4)
#define IRC_STAT_IIP        (0x1 << 2)
#define IRC_STAT_IREP       (0x1 << 0)

enum {
	IR_PROTOCOL_9012 = 0x0,
	IR_PROTOCOL_NEC8 = 0x1,
	IR_PROTOCOL_RC5 = 0x2
};

enum {
	IR_MODE_NORMAL,
	IR_MODE_WAKEUPSRC,
	IR_MODE_DISABLED
};

struct irconfig {
	unsigned int size;
	unsigned int protocol;
	unsigned int user_code;
	unsigned int wk_code;
	unsigned int period;
	unsigned int *ir_code;
	unsigned int *key_code;

	struct list_head list;
};

static inline unsigned int ir_convert(unsigned int protocol, unsigned int *val)
{
	switch (protocol) {
	case IR_PROTOCOL_9012:
		*val &= 0x00ff;
		break;
	case IR_PROTOCOL_NEC8:
		*val &= 0x00ff;
		break;
	case IR_PROTOCOL_RC5:
		*val &= 0x003f;
		break;
	default:
		*val = 0;
		break;
	}

	return *val;
}

/*
 * Ir module in 5211 can not wake up cpu at sleep time.
 * So use ir module in atc260x instead.
 */
bool ir_register_wk_notifier(void (*cb1)(struct irconfig *, void *), int (*cb2)(void *), void *data);

#endif /*_OWL_IRKEY_H_*/
