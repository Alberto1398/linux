/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/12/16: Created by Lipeng.
 */
#define DEBUGX
#define pr_fmt(fmt) "hdmi_hdcp: %s, " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/slab.h>

#include "hdmi.h"

/*
 * READ_HDCP_KEY_FROM_NATIVE,
 * A test definition, if is defined, we will not read real hdcp
 * key from disk, instead of from a static table.
 */
#define READ_HDCP_KEY_FROM_NATIVEX

/* HDCP authen */
#define HDCP_XMT_LINK_H0	0
#define HDCP_XMT_LINK_H1	1
#define HDCP_XMT_AUTH_A0	2
#define HDCP_XMT_AUTH_A1	3
#define HDCP_XMT_AUTH_A2	4
#define HDCP_XMT_AUTH_A3	5
#define HDCP_XMT_AUTH_A4	6
#define HDCP_XMT_AUTH_A5	7
#define HDCP_XMT_AUTH_A6	8
#define HDCP_XMT_AUTH_A7	9
#define HDCP_XMT_AUTH_A8	10
#define HDCP_XMT_AUTH_A9	11
#define HDCP_XMT_AUTH_A9_1	12

#define HDCP_FAIL_TIMES		50

/* sha_1 160bit */
#define HLEN			20

#define SWAPL(x)		((((x) & 0x000000ff) << 24) \
				 + (((x) & 0x0000ff00) << 8) \
				 + (((x) & 0x00ff0000) >> 8) \
				 + (((x) & 0xff000000) >> 24))
/* x leftrotate n */
#define SXOLN(n, x)		((x << n) | (x >> (32 - n)))

struct hdmi_hdcp {
	struct hdmi_ip *ip;

	int hdcp_oper_state;
	int hdcp_oper_retry;
	int repeater;
	int ri;
	int ri_read;
	int retry_times_for_set_up_5_second;

	int need_to_delay;

	unsigned char hdcp_oper_m0[M0_LENGTH];
	unsigned char b_status[BSTATUS_LENGTH];
	unsigned char bksv[KSV_LENGTH];
	unsigned char ksv_list[128 * KSV_LENGTH];
	unsigned char vp[20];

	int i2c_error;
	int hdcp_fail_times;

	struct workqueue_struct *wq;
	struct delayed_work hdcp_check_work;
	struct delayed_work hdcp_ri_update_work;
	struct delayed_work hdcp_read_key_work;
	bool hdcp_authentication_success;
};

static struct hdmi_hdcp		g_hdcp;


static int			hdcp_timer_interval = 50;

static unsigned char		aksv[6] = {
	0x10, 0x1e, 0xc8, 0x42, 0xdd, 0xd9
};

static unsigned char g_hdcp_key[KEY_ROW_LENGTH][KEY_COL_LENGTH * 2 + 1] = {
	"aa6197eb701e4e",
	"780717d2d425d3",
	"7c4f7efe39f44d",
	"1e05f28f0253bd",
	"d614c6ccb090ee",
	"82f7f2803ffefc",
	"4ae2ebe12741d7",
	"e203695a82311c",
	"4537b3e7f9f557",
	"b02d715e2961ed",
	"cd6e0ab9834016",
	"d64fd9edfdd35b",
	"3a13f170726840",
	"4f52fb759d6f92",
	"2afc1d08b4221f",
	"b99db2a29aea40",
	"2020da8eb3282a",
	"0b4c6aee4aa771",
	"edeef811ec6ac0",
	"8f451ad92112f3",
	"705e2fddc47e6e",
	"6efc72aaa3348f",
	"7debb76130fb0e",
	"5fd1a853c40f52",
	"70168fbeaeba3c",
	"a0cb8f3162d198",
	"5ed0a9eef0c1ed",
	"fa18541a02d283",
	"0661135d19feec",
	"da6b02ca363cf3",
	"dd7142f81e832f",
	"cd3490a48be99a",
	"0e08bceaeec81a",
	"9b0d183dbbbbf3",
	"7788f601c07e57",
	"4e7271a716e4aa",
	"f510b293bec62a",
	"16158c6a1f3c84",
	"33656236296f06",
	"a5438e67d72d80",
};

#ifndef READ_HDCP_KEY_FROM_NATIVE
static int check_one_number(unsigned char data)
{
	int num = 0, i;

	for (i = 0; i < 8; i++) {
		if ((data >> i) & 0x1)
			num++;
	}

	return num;
}

unsigned char sha_1(unsigned char *sha_output, unsigned char *m_input, int len)
{
	unsigned int Kt[4] = {
		0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6
	};

	unsigned int h[5] = {
		0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0
	};

	int t, n, n_block;
	unsigned int ft, x, temp;
	unsigned int A, B, C, D, E;
	int i, j;
	unsigned char *sha_input;
	unsigned char *out_temp;
	unsigned int W[80];

	sha_input = kmalloc(700, GFP_KERNEL);
	if (NULL == sha_input)
		return -ENOMEM;
	memset(sha_input, 0, 700);

	for (i = 0; i < len; i++)
		sha_input[i] = m_input[i];

	/* do message padding */
	n_block = (len * 8 + 1 + 64) / 512 + 1;
	sha_input[len] = 0x80;
	pr_debug("n_block=%d\n", n_block);

	/* set len */
	sha_input[(n_block - 1) * 64 + 60]
		= (unsigned char)(((len * 8) & 0xff000000) >> 24);
	sha_input[(n_block - 1) * 64 + 61]
		= (unsigned char)(((len * 8) & 0x00ff0000) >> 16);
	sha_input[(n_block - 1) * 64 + 62]
		= (unsigned char)(((len * 8) & 0x0000ff00) >> 8);
	sha_input[(n_block - 1) * 64 + 63]
		= (unsigned char)((len * 8) & 0x000000ff);

	for (j = 0; j < n_block; j++) {
		pr_debug("\nBlock %d\n", j);
		for (i = 0; i < 64; i++) {
			if ((i % 16 == 0) && (i != 0))
				pr_debug("\n0x%2x,", sha_input[j * 64 + i]);
			else
				pr_debug("0x%2x,", sha_input[j * 64 + i]);
		}
	}
	pr_debug("SHA sha_input end\n");

	for (n = 0; n < n_block; n++) {
		for (t = 0; t < 16; t++) {
			x = *((unsigned int *)&sha_input[n * 64 + t * 4]);
			W[t] = SWAPL(x);
		}
		for (t = 16; t < 80; t++) {
			x = W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16];
			W[t] = SXOLN(1, x);
		}

		A = h[0];
		B = h[1];
		C = h[2];
		D = h[3];
		E = h[4];

		for (t = 0; t < 80; t++) {
			if (t >= 0 && t <= 19)
				ft = (B & C) | ((~B) & D);
			else if (t >= 20 && t <= 39)
				ft = B ^ C ^ D;
			else if (t >= 40 && t <= 59)
				ft = (B & C) | (B & D) | (C & D);
			else
				ft = (B ^ C ^ D);

			/* temp = S^5(A) + f(t;B,C,D) + E + W(t) + K(t) */
			temp = SXOLN(5, A) + ft + E + W[t] + Kt[t / 20];

			E = D;
			D = C;
			C = SXOLN(30, B);	/* C = S^30(B) */
			B = A;
			A = temp;
		}

		h[0] += A;			/* H0 = H0 + A */
		h[1] += B;			/* H1 = H1 + B */
		h[2] += C;			/* H2 = H2 + C */
		h[3] += D;			/* H3 = H3 + D */
		h[4] += E;			/* H4 = H4 + E */
	}

	pr_debug("\noutput original sha_input:\n");
	for (i = 0; i < HLEN / 4; i++)
		pr_debug("0x%x\t", h[i]);

	pr_debug("\nconvert to little endien\n");
	for (i = 0; i < HLEN / 4; i++) {
		h[i] = SWAPL(h[i]);
		pr_debug("0x%x\t", h[i]);
	}
	pr_debug("\n");

	/* copy to output pointer */
	out_temp = (unsigned char *) h;
	pr_debug("out_temp:\n");
	for (i = 0; i < HLEN; i++) {
		sha_output[i] = out_temp[i];
		pr_debug("0x%x\t", out_temp[i]);
	}
	pr_debug("\n");
	kfree(sha_input);
	return 0;
}

extern int read_mi_item(char *name, void *buf, unsigned int count);

static int hdcp_read_key(void)
{
	int array, col, index;
	int num = 0;
	int i = 0;
	int ret;
	unsigned char key[308];
	unsigned char sha1_verify[20];
	unsigned char sha1_result[20];

	ret = read_mi_item("HDCP", key, sizeof(key));
	if (ret < 0) {
		pr_info("failed to read hdcp key from secure storage(%d)\n",
			ret);
		return ret;
	}
	pr_debug("hdcp key cnt %d\n", ret);

	for (i = 1; i < sizeof(aksv); i++)
		aksv[i] = key[i - 1];

	for (i = 0; i < KSV_LENGTH; i++)
		num += check_one_number(key[i]);

	/* key */
	if (num == 20) {
		pr_debug("aksv is valid\n");
		pr_debug("hdcp key is as follows:\n");
		for (array = 0; array < KEY_ROW_LENGTH; array++) {
			for (col = 0; col < KEY_COL_LENGTH; col++) {
				index = (KSV_LENGTH + KSV_RESERVED
					+ (array + 1) * KEY_COL_LENGTH)
					- col - 1;
				sprintf(&g_hdcp_key[array][2 * col], "%x",
					((key[index] & 0xf0) >> 4) & 0x0f);
				sprintf(&g_hdcp_key[array][2 * col + 1], "%x",
					key[index] & 0x0f);

				pr_debug("key_tmp[%d][%d]:%c\n", array,
					 2 * col, g_hdcp_key[array][2 * col]);
				pr_debug("key_tmp[%d][%d]:%c\n", array,
					 2 * col + 1,
					 g_hdcp_key[array][2 * col + 1]);
			}
			pr_debug("%s\n", g_hdcp_key[array]);
		}
		pr_debug("\n\nhdcp key parse finished\n\n");

		pr_debug("verify code is as follows:\n");
		for (i = 0; i < sizeof(sha1_verify); i++) {
			index = i + KSV_LENGTH + KSV_RESERVED + KEY_LENGTH;
			sha1_verify[i] = key[index];
			pr_debug("sha1_verify[%d]:%x\n", i, sha1_verify[i]);
		}
		pr_debug("verify code parse finished\n\n");

		if (sha_1(sha1_result, key,
			  KSV_LENGTH + KSV_RESERVED + KEY_LENGTH)) {
			pr_debug("aksv kmalloc mem is failed\n");
			return 1;
		}

	} else {
		pr_debug("aksv is invalid\n");
		return 1;
	}

	/* verify */
	for (i = 0; i < VERIFY_LENGTH; i++) {
		if (sha1_verify[i] != sha1_result[i]) {
			pr_err("sha1 verify error!\n");
			return 1;
		}
	}
	pr_debug("[%s]sha1 verify success !\n", __func__);

	return 0;
}
#else
static int hdcp_read_key(void)
{
	return 0;
}
#endif

/*===========================================================================
 *		hdcp_check_handle, handle HDCP checking
 *=========================================================================*/

/*
 * check if Bksv is valid, need have 20 "1"  and 20 "0"
 */
static int hdcp_check_bksv_invalid(void)
{
	int i, j;
	unsigned char counter = 0;
	unsigned char invalid_bksv[4][5] = {
		{0x0b, 0x37, 0x21, 0xb4, 0x7d},
		{0xf4, 0xc8, 0xde, 0x4b, 0x82},
		{0x23, 0xde, 0x5c, 0x43, 0x93},
		{0x4e, 0x4d, 0xc7, 0x12, 0x7c},
	};

	for (i = 0; i < 5; i++) {
		for (j = 0; j < 8; j++) {
			if (((g_hdcp.bksv[i] >> j) & 0x1) != 0)
				counter++;
		}
	}

	if (counter != 20) {
		pr_err("[%s]fail  0x%x 0x%x 0x%x 0x%x 0x%x\n", __func__,
		       g_hdcp.bksv[0], g_hdcp.bksv[1], g_hdcp.bksv[2],
		       g_hdcp.bksv[3], g_hdcp.bksv[4]);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 5; j++) {
			if (invalid_bksv[i][j] != g_hdcp.bksv[j])
				break;

			if (j == 4)
				return 0;
		}
	}

	pr_debug("successful!\n");
	return 1;
}

static int hdcp_read_ksv_list(unsigned char *b_status, unsigned char *ksvlist)
{
	int cnt;

	/* get device count in Bstatus [6:0] */
	if (hdmi_ddc_hdcp_read(b_status, 0x41, 2) < 0)
		if (hdmi_ddc_hdcp_read(b_status, 0x41, 2) < 0)
			return 0;

	/* if Max_devs_exceeded then quit */
	if (b_status[0] & 0x80)
		return 0;

	/* if Max_cascade_exceeded then quit */
	if (b_status[1] & 0x8)
		return 0;

	cnt = b_status[0] & 0x7f;

	if (!cnt)
		return 1;

	if (hdmi_ddc_hdcp_read(ksvlist, 0x43, 5 * cnt) < 0)
		if (hdmi_ddc_hdcp_read(ksvlist, 0x43, 5 * cnt) < 0)
			return 0;
	return 1;
}

static int hdcp_read_vprime(unsigned char *vp)
{
	/* read Vp */
	if (hdmi_ddc_hdcp_read(vp, 0x20, 20) != 0) {
		if (hdmi_ddc_hdcp_read(vp, 0x20, 20) != 0)
			return 0;
	}
	return 1;
}

static void hdcp_check_handle(struct work_struct *work)
{
	unsigned char an[9] = { 0 };
	unsigned char b_caps = 0, ri_temp[8] = { 0 };

	struct hdmi_ip *ip = g_hdcp.ip;

	if (!ip->ops->cable_status(ip)) {
		pr_debug("hdmi plug out in hdcp\n");
		goto end;
	}

#if 0
	if (hdcp_readl(HDMI_GCPCR) & 0x01) {
		pr_debug("hdcp_readl(HDCP_GCPCR)&0x01 is true 0x%x\n",
			 hdcp_readl(HDMI_GCPCR));
		goto end;
	}
#endif

	if (aksv[0] == 0x00 && aksv[1] == 0x00 && aksv[2] == 0x00 &&
	    aksv[3] == 0x00 && aksv[4] == 0x00) {
		pr_debug("aksv == 0\n");
		goto end;
	}

	pr_debug("\n**********hdcp start************\n");
	if (g_hdcp.hdcp_fail_times > HDCP_FAIL_TIMES) {
		/* stop play */
		pr_debug("\n**********hdcp fail many times************\n");

		g_hdcp.hdcp_authentication_success = false;
		goto end;
	}

	/* delay */
	if (g_hdcp.need_to_delay > 0) {
		g_hdcp.need_to_delay -= hdcp_timer_interval;

		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_check_work,
				   msecs_to_jiffies(hdcp_timer_interval));
		return;
	}

	/* state machine */
	switch (g_hdcp.hdcp_oper_state) {
	case HDCP_XMT_LINK_H0:
		pr_debug("************HDCP_XMT_LINK_H0*************\n");
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H1;

	case HDCP_XMT_LINK_H1:
		pr_debug("************HDCP_XMT_LINK_H1*************\n");
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;

	/*
	 * Authentication phase 1: A0,A1,A2,A3
	 * A0.generate An, get Aksv;
	 * A1.write An and Aksv, read Bksv;
	 * A2.get R0;
	 * A3.computes Km, Ks, M0 and R0;
	 *
	 * Authentication phase 2:  A6  or  A6,A8,A9,A91
	 * A6.check if sink support repeater,
	 * if not support repeater then authentication finish;
	 * A8. check if  KSV is ready;
	 * A9:get KSV list;
	 * A91:compare V value;
	 *
	 * Authentication phase 3: A4,A5
	 * A4.set hdcp to Authenticated state;
	 * A5.authentication successful
	 */
	case HDCP_XMT_AUTH_A0:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A0*************\n");

		/* genrate An, get Aksv */
		g_hdcp.hdcp_oper_retry = 0;

		/* get An value */
		ip->ops->hdcp_an_generate(ip, an);
		msleep(20);

		pr_debug("************send An*************\n");
		if (hdmi_ddc_hdcp_write(&an[0], 0x18, 9) < 0)
			pr_debug("\nWrite An error\n");

		pr_debug("************send aksv*************\n");
		if (hdmi_ddc_hdcp_write(aksv, 0x10, 6) < 0)
			pr_debug("\nWrite aksv error\n");

		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A1;
		g_hdcp.need_to_delay = 100;
		break;

	case HDCP_XMT_AUTH_A1:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A1*************\n");
		/*
		 * write An and Aksv, read Bksv
		 * if get Bksv successful mean receiver/repeater support HDCP
		 * check valid Bksv 20 ones and 20 zero
		 */

		pr_debug("************read Bksv*************\n");
		while (hdmi_ddc_hdcp_read(g_hdcp.bksv, 0x00, 5) < 0) {
			/* if read successful, means support HDCP */
			g_hdcp.i2c_error++;
			if (g_hdcp.i2c_error > 3) {
				g_hdcp.i2c_error = 0;
				pr_debug("[631]Do not support HDCP\n");
				break;
			}
		}
		msleep(110);

		pr_debug("************Check_Bksv*************\n");
		if (hdcp_check_bksv_invalid() == 0) {
			pr_debug("\ncheck_bksv_invalid\n");

			ip->ops->hdcp_reset(ip);

			g_hdcp.hdcp_fail_times++;
			g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
			g_hdcp.need_to_delay = 100;
			break;
		}

		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A2;
		g_hdcp.need_to_delay = 100;
		break;

	case HDCP_XMT_AUTH_A2:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A2*************\n");

		/* Computations */

		/* computes Km, Ks, M0 and R0 */
		pr_debug("************read Bcaps*************\n");
		if (hdmi_ddc_hdcp_read(&b_caps, 0x40, 1) < 0)
			pr_debug("Read Bcaps error\n"); /* error_handle(); */

		if ((b_caps & (1 << 6)) != 0) {
			/* set to support repeater */
			pr_debug("***********support repeater************\n");
			g_hdcp.repeater = 1;
			ip->ops->hdcp_repeater_enable(ip, true);
		} else {
			/* set to support NO repeater */
			pr_debug("*********dont  support repeater********\n");
			g_hdcp.repeater = 0;
			ip->ops->hdcp_repeater_enable(ip, false);
		}

		pr_debug("************generate Ri*************\n");

		ip->ops->hdcp_ks_m0_r0_generate(ip, g_hdcp.bksv, g_hdcp_key);

		g_hdcp.ri = ip->ops->hdcp_ri_get(ip);
		pr_debug("Ri: 0x%x\n", g_hdcp.ri);

		g_hdcp.hdcp_oper_retry = 3;

		/* if computed results are available */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A3;

		/* wait for 100 msec to read R0p. */
		mdelay(130); /* 建议不改为可调度 */

		break;

	case HDCP_XMT_AUTH_A3:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A3*************\n");

		/*
		 * Validate Receiver
		 * computes Km, Ks, M0 and R0
		 * if computed results are available
		 */
		pr_debug("************read R0*************\n");

		if (hdmi_ddc_hdcp_read(ri_temp, 0x08, 2) < 0)
			memset(ri_temp, 0, sizeof(ri_temp));

		g_hdcp.ri_read = (int)((unsigned int)ri_temp[1] << 8)
				| ri_temp[0];

		pr_debug("****Ri_Read:0x%x\n****", g_hdcp.ri_read);
		if (g_hdcp.ri != g_hdcp.ri_read) {
			pr_debug("\nR0 != Ri_Read\n");
			if (g_hdcp.hdcp_oper_retry != 0) {
				g_hdcp.hdcp_oper_retry--;
				g_hdcp.need_to_delay = 100;
			} else {
				ip->ops->hdcp_reset(ip);

				/* authentication part I failed */
				g_hdcp.hdcp_fail_times++;
				g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;
				g_hdcp.need_to_delay = 200;
			}
			break;
		}

		g_hdcp.hdcp_oper_retry = 0;
		ip->ops->hdcp_m0_get(ip, g_hdcp.hdcp_oper_m0);

		/* authentication part I successful */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A6;

	case HDCP_XMT_AUTH_A6:	/* Authentication phase 2 */
		pr_debug("************HDCP_XMT_AUTH_A6*************\n");
		/* Test for Repeater */

		/* get REPEATER */
		if (g_hdcp.repeater != 0) {
			/* change to Authentication part II */

			g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A8;

			/* wait 100msec */
			g_hdcp.need_to_delay = 100;
			g_hdcp.retry_times_for_set_up_5_second = 0;
			break;
		}

		/* NO repeater */

		/* change to Authentication part III */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A4;

	case HDCP_XMT_AUTH_A4:	/* Authentication phase 3 */
		pr_debug("************HDCP_XMT_AUTH_A4*************\n");

		/*
		 *  Authenticated
		 * set HDCP module to authenticated state
		 * start encryption
		 */
		ip->ops->hdcp_auth_start(ip);

		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A5;
		g_hdcp.hdcp_oper_retry = 0;

		break;

	case HDCP_XMT_AUTH_A5:	/* Authentication phase 3 */
		pr_debug("************HDCP_XMT_AUTH_A5*************\n");

		/* Link Integrity Check */
		/* Interrupt and BH will do this job */
		pr_info("********hdcp Authentication suceesful********\n");
		g_hdcp.hdcp_authentication_success = true;

		/* enable Ri update check */
		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_ri_update_work,
				   msecs_to_jiffies(3000));
		return;

	case HDCP_XMT_AUTH_A8:	/* Authentication phase 2 */
		pr_debug("************HDCP_XMT_AUTH_A8*************\n");

		/* 2nd part authentication */
		/* Wait for Ready */
		/* set up 5 second timer poll for KSV list ready */
		if ((g_hdcp.retry_times_for_set_up_5_second % 5) == 0)
			if (hdmi_ddc_hdcp_read(&b_caps, 0x40, 1) < 0)
				pr_debug("\nRead Bcaps err\n");

		if (!((b_caps >> 5) & 0x1)) {
			/* if KSVlist not ready! */
			if (g_hdcp.retry_times_for_set_up_5_second <= 50) {
				/* 100 msec * 50 = 5 sec */

				g_hdcp.retry_times_for_set_up_5_second++;
				g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A8;
				/* wait 100msec */
				g_hdcp.need_to_delay = 100;
				break;
			} else {
				/* restart */
				pr_debug("\nretry times for setup > 50\n");

				ip->ops->hdcp_reset(ip);

				g_hdcp.hdcp_fail_times++;
				g_hdcp.retry_times_for_set_up_5_second = 0;
				g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;
				g_hdcp.need_to_delay = 100; /* wait 100msec */
				break;
			}

		}

		g_hdcp.retry_times_for_set_up_5_second = 0;
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A9;

	case HDCP_XMT_AUTH_A9:	/* Authentication phase 2 */
		 pr_debug("************HDCP_XMT_AUTH_A9*************\n");

		/* Read KSV List and Bstatus */
		if (!hdcp_read_ksv_list(g_hdcp.b_status, g_hdcp.ksv_list)) {
			pr_debug("\nhdcp_read_ksv_list\n");

			ip->ops->hdcp_reset(ip);

			g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;
			g_hdcp.hdcp_fail_times++;
			g_hdcp.need_to_delay = 100;
			break;
		}

		g_hdcp.need_to_delay = 100;
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A9_1;
		break;

	case HDCP_XMT_AUTH_A9_1:	/* Authentication phase 2 */
		pr_debug("************HDCP_XMT_AUTH_A9_1*************\n");

		hdcp_read_vprime(g_hdcp.vp);

		if (!ip->ops->hdcp_vprime_verify(ip, g_hdcp.vp, g_hdcp.ksv_list,
						 g_hdcp.b_status,
						 g_hdcp.hdcp_oper_m0)) {
			/* compare with V' */
			/* authentication part II failed */
			pr_debug("\nhdcp_do_vmatch\n");

			ip->ops->hdcp_reset(ip);

			g_hdcp.hdcp_fail_times++;
			g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;
			g_hdcp.need_to_delay = 100;
			break;
		}

		/* KSV list correct , transit to Authentication Part III */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A4;
		g_hdcp.need_to_delay = 100;
		break;

	default:
		break;
	}

	/* restart */
	queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_check_work,
			   msecs_to_jiffies(hdcp_timer_interval));
	return;


end:
	ip->ops->hdcp_reset(ip);
	g_hdcp.need_to_delay = 0;
	g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
}

/*===========================================================================
 *		hdcp_ri_update_handle, handle Ri updating event
 *=========================================================================*/

static int hdcp_check_ri(void)
{
	unsigned char ri_temp[8] = {0};
	int ri, ri_read;

	struct hdmi_ip *ip = g_hdcp.ip;

	pr_debug("start\n");

	/* hdcp Ri */
	ri = ip->ops->hdcp_ri_get(ip);;

	if (hdmi_ddc_hdcp_read(ri_temp, 0x08, 2) == 0)
		memset(ri_temp, 0, sizeof(ri_temp));

	ri_read = (ri_temp[1] << 8) | ri_temp[0];

	pr_debug("Ri %x, Riread %x\n", ri, ri_read);
	if (ri != ri_read)
		return 1;

	return 0;
}

static void hdcp_ri_update_handle(struct work_struct *work)
{
	struct hdmi_ip *ip = g_hdcp.ip;

	if (hdcp_check_ri()) {
		ip->ops->hdcp_reset(ip);

		g_hdcp.need_to_delay = 0;
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;

		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_check_work,
				   msecs_to_jiffies(50));
	} else {
		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_ri_update_work,
				   msecs_to_jiffies(3000));
	}
}

static void hdcp_read_key_handle(struct work_struct *work)
{
	if (hdcp_read_key() == -EAGAIN)
		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_read_key_work,
				   msecs_to_jiffies(2000));
}

/*===========================================================================
 *				APIs
 *=========================================================================*/

int hdmi_hdcp_init(struct hdmi_ip *ip)
{
	pr_debug("enter\n");

	if (g_hdcp.ip != NULL) {
		pr_err("already inited\n");
		return -EBUSY;
	}

	g_hdcp.ip = ip;

	g_hdcp.need_to_delay = 0;
	g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
	g_hdcp.hdcp_fail_times = 0;

	g_hdcp.wq = create_workqueue("hdmi-hdcp");

	INIT_DELAYED_WORK(&g_hdcp.hdcp_check_work, hdcp_check_handle);
	INIT_DELAYED_WORK(&g_hdcp.hdcp_ri_update_work, hdcp_ri_update_handle);
	INIT_DELAYED_WORK(&g_hdcp.hdcp_read_key_work, hdcp_read_key_handle);

	/* try to read HDCP key */
	queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_read_key_work,
			   msecs_to_jiffies(50));

	return 0;
}

void hdmi_hdcp_exit(struct hdmi_ip *ip)
{
	cancel_delayed_work_sync(&g_hdcp.hdcp_read_key_work);
	flush_delayed_work(&g_hdcp.hdcp_read_key_work);

	cancel_delayed_work_sync(&g_hdcp.hdcp_ri_update_work);
	flush_delayed_work(&g_hdcp.hdcp_ri_update_work);

	cancel_delayed_work_sync(&g_hdcp.hdcp_check_work);
	flush_delayed_work(&g_hdcp.hdcp_check_work);

	destroy_workqueue(g_hdcp.wq);

	g_hdcp.ip = NULL;
}

int hdmi_hdcp_enable(struct hdmi_ip *ip, bool enable)
{
	pr_debug("enable? %d\n", enable);

	/* make sure Ri check work is canceled! */
	cancel_delayed_work_sync(&g_hdcp.hdcp_ri_update_work);
	flush_delayed_work(&g_hdcp.hdcp_ri_update_work);

	if (enable) {
		g_hdcp.need_to_delay = 0;
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;

		ip->ops->hdcp_init(ip);
		ip->ops->hdcp_reset(ip);

		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_check_work,
				   msecs_to_jiffies(50));
	} else {
		cancel_delayed_work_sync(&g_hdcp.hdcp_check_work);
		flush_delayed_work(&g_hdcp.hdcp_check_work);
	}

	return 0;
}
