/*
 * hdmi_cecc
 *
 * HDMI OWL IP driver Library
 *
 * Copyright (C) 2014 Actions Corporation
 * Author: HaiYu Huang  <huanghaiyu@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define DEBUGX
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/compat.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>

#define CEC_DEBUG
#include "cec.h"

#define CEC_IOC_MAGIC        'c'
#define CEC_IOC_SETLADDR	_IOW(CEC_IOC_MAGIC, 2, unsigned int)
#define CEC_SET_LADDR32		_IOW(CEC_IOC_MAGIC, 4, unsigned int)

#define VERSION   "1.0" /* Driver version number */
#define CEC_MINOR 243	/* Major 10, Minor 242, /dev/cec */


#define CEC_STATUS_TX_BYTES         (1<<0)
#define CEC_STATUS_TX_ERROR         (1<<5)
#define CEC_STATUS_TX_DONE          (1<<6)
#define CEC_STATUS_TX_TRANSFERRING  (1<<7)

#define CEC_STATUS_RX_BYTES         (1<<8)
#define CEC_STATUS_RX_ERROR         (1<<13)
#define CEC_STATUS_RX_DONE          (1<<14)
#define CEC_STATUS_RX_TRANSFERRING  (1<<15)

static atomic_t hdmi_on = ATOMIC_INIT(0);
static DEFINE_MUTEX(cec_lock);

static int hdmi_cec_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	mutex_lock(&cec_lock);

	if (atomic_read(&hdmi_on)) {
		hdmi_cec_dbg("do not allow multiple open for tvout cec\n");
		ret = -EBUSY;
		goto err_multi_open;
	} else
		atomic_inc(&hdmi_on);

	hdmi_cec_hw_init(cec);

	hdmi_cec_reset(cec);

	hdmi_cec_set_rx_state(cec, STATE_RX);

	hdmi_cec_unmask_rx_interrupts(cec);

	hdmi_cec_enable_rx(cec);

err_multi_open:
	mutex_unlock(&cec_lock);

	return ret;
}

static int hdmi_cec_release(struct inode *inode, struct file *file)
{
	atomic_dec(&hdmi_on);

	hdmi_cec_mask_tx_interrupts(cec);
	hdmi_cec_mask_rx_interrupts(cec);

	return 0;
}

static ssize_t hdmi_cec_read(struct file *file, char __user *buffer,
			size_t count, loff_t *ppos)
{
	ssize_t retval;
	int i = 0;
	unsigned long spin_flags;

	if (wait_event_interruptible(cec->cec_rx_struct.waitq,
			atomic_read(&cec->cec_rx_struct.state) == STATE_DONE)) {
		return -ERESTARTSYS;
	}
	spin_lock_irqsave(&cec->cec_rx_struct.lock, spin_flags);

	if (cec->cec_rx_struct.size > count) {
		spin_unlock_irqrestore(&cec->cec_rx_struct.lock, spin_flags);

		return -1;
	}

#if 0
	hdmi_cec_dbg("hdmi_cec_read size 0x%x: ", cec_rx_struct.size);
	for (i = 0; i < cec_rx_struct.size; i++)
		hdmi_cec_dbg("0x%x ", cec_rx_struct.buffer[i]);
#endif

	if (copy_to_user(buffer, cec->cec_rx_struct.buffer,
				cec->cec_rx_struct.size)) {
		spin_unlock_irqrestore(&cec->cec_rx_struct.lock, spin_flags);
		pr_err("copy_to_user() failed!\n");
		return -EFAULT;
	}

	retval = cec->cec_rx_struct.size;

	hdmi_cec_set_rx_state(cec, STATE_RX);

	spin_unlock_irqrestore(&cec->cec_rx_struct.lock, spin_flags);

	return retval;
}

static ssize_t hdmi_cec_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *ppos)
{
	int i = 0;
	char *data;

	/* check data size */
	if (count > CEC_TX_BUFF_SIZE || count == 0) {
		pr_err("hdmi_cec_write count %d error\n", count);
		return -1;
	}

	data = kmalloc(count, GFP_KERNEL);
	if (!data) {
		pr_err(" kmalloc() failed!\n");
		return -1;
	}

	if (copy_from_user(data, buffer, count)) {
		pr_err(" copy_from_user() failed!\n");
		kfree(data);
		return -EFAULT;
	}

	/*disable rx  switch to tx mode*/
	hdmi_cec_disable_rx(cec);

	hdmi_cec_copy_packet(cec, data, count);

	kfree(data);

	/* wait for interrupt */
	if (wait_event_interruptible(cec->cec_tx_struct.waitq,
		atomic_read(&cec->cec_tx_struct.state) != STATE_TX)) {
		return -ERESTARTSYS;
	}

	hdmi_cec_disable_tx(cec);

	/*cec_reset*/
	hdmi_cec_reset(cec);

	/*switch to rx mode*/
	hdmi_cec_unmask_rx_interrupts(cec);

	hdmi_cec_enable_rx(cec);

	if (atomic_read(&cec->cec_tx_struct.state) == STATE_ERROR) {
		pr_err("TX STATE_ERROR\n");
		return -1;
	}

	return count;
}
static long hdmi_cec_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)

{
	u32 laddr;
	switch (cmd) {
	case CEC_IOC_SETLADDR:
		if (get_user(laddr, (u32 __user *) arg))
			return -EFAULT;
		hdmi_cec_dbg(" hdmi_cec_ioctl set ladr 0x%x\n", laddr);
		hdmi_cec_set_addr(cec, laddr);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

long cec_compat_ioctl_32(struct file *file, unsigned long arg)
{
	int ret;
	hdmi_cec_dbg("%s\n", __func__);

	ret = hdmi_cec_ioctl(file, CEC_IOC_SETLADDR, arg);

	return 0;
}

static u32 hdmi_cec_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &cec->cec_rx_struct.waitq, wait);
	if (atomic_read(&cec->cec_rx_struct.state) == STATE_DONE) {
			hdmi_cec_dbg("owl_hdmi_cec_poll ok\n");
			return POLLIN | POLLRDNORM;
		}
	return 0;
}

long hdmi_cec_compat_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	pr_info("%s\n", __func__);
	switch (cmd) {
	case CEC_SET_LADDR32:
		hdmi_cec_dbg("cec ioctl compat 32\n");
		return cec_compat_ioctl_32(file, compat_ptr(arg));

	default:
		return hdmi_cec_ioctl(file, cmd, arg);
	}
}
static const struct file_operations cec_fops = {
	.owner		=	THIS_MODULE,
	.open		=	hdmi_cec_open,
	.release	=	hdmi_cec_release,
	.read		=	hdmi_cec_read,
	.write		=	hdmi_cec_write,
	.unlocked_ioctl =	hdmi_cec_ioctl,
	.compat_ioctl	=	hdmi_cec_compat_ioctl,
	.poll		=	hdmi_cec_poll,
};

static struct miscdevice cec_misc_device = {
	.minor = CEC_MINOR,
	.name  = "CEC",
	.fops  = &cec_fops,
};

void hdmi_cec_irq_handler(void)
{
	u32 status = 0;
	status = hdmi_cec_get_status(cec);
	if (status & CEC_STATUS_TX_DONE) {
		if (status & CEC_STATUS_TX_ERROR) {
			hdmi_cec_dbg(" CEC_STATUS_TX_ERROR!\n");
			hdmi_cec_set_tx_state(cec, STATE_ERROR);
		} else {
			hdmi_cec_dbg(" CEC_STATUS_TX_DONE!\n");
			hdmi_cec_set_tx_state(cec, STATE_DONE);
		}

		hdmi_cec_clr_pending_tx(cec);

		wake_up_interruptible(&cec->cec_tx_struct.waitq);
	}

	if (status & CEC_STATUS_RX_DONE) {
		if (status & CEC_STATUS_RX_ERROR) {
			hdmi_cec_dbg(" CEC_STATUS_RX_ERROR!\n");
			hdmi_cec_rx_reset(cec);
			hdmi_cec_unmask_rx_interrupts(cec);
			hdmi_cec_enable_rx(cec);
		} else {
			u32 size;
			u8 header;

			hdmi_cec_dbg(" CEC_STATUS_RX_DONE!\n");

			/* copy data from internal buffer */
			size = (status >> 8) & 0x0f;

			header = hdmi_cec_get_rx_header(cec);
			cec->cec_rx_struct.buffer[0] = header;

			spin_lock(&cec->cec_rx_struct.lock);

			hdmi_cec_get_rx_buf(cec, size,
						cec->cec_rx_struct.buffer);
			/*includ 1 byte header*/
			cec->cec_rx_struct.size = size + 1;

			hdmi_cec_set_rx_state(cec, STATE_DONE);

			spin_unlock(&cec->cec_rx_struct.lock);

			/*after receive data reset rx*/
			hdmi_cec_rx_reset(cec);
			hdmi_cec_unmask_rx_interrupts(cec);
			hdmi_cec_enable_rx(cec);
		}

		/* clear interrupt pending bit */
		hdmi_cec_clr_pending_rx(cec);

		wake_up_interruptible(&cec->cec_rx_struct.waitq);
	}
}
EXPORT_SYMBOL(hdmi_cec_irq_handler);
int hdmi_cec_init(struct hdmi_ip *ip)
{
	int ret;
	pr_info("hdmi cec init start\n");

	hdmi_cec_ctrl_init(ip);

	if (misc_register(&cec_misc_device)) {
		pr_err("%s: Couldn't register device 10, %d.\n",
			CEC_MINOR);
		misc_deregister(&cec_misc_device);
		return -EBUSY;
	}

	return 0;
}


