/*
 * file_storage.c -- File-backed USB Storage Gadget, for USB development
 *
 * Copyright (C) 2003-2007 Alan Stern
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The File-backed Storage Gadget acts as a USB Mass Storage device,
 * appearing to the host as a disk drive.  In addition to providing an
 * example of a genuinely useful gadget driver for a USB device, it also
 * illustrates a technique of double-buffering for increased throughput.
 * Last but not least, it gives an easy way to probe the behavior of the
 * Mass Storage drivers in a USB host.
 *
 * Backing storage is provided by a regular file or a block device, specified
 * by the "file" module parameter.  Access can be limited to read-only by
 * setting the optional "ro" module parameter.  The gadget will indicate that
 * it has removable media if the optional "removable" module parameter is set.
 *
 * The gadget supports the Control-Bulk (CB), Control-Bulk-Interrupt (CBI),
 * and Bulk-Only (also known as Bulk-Bulk-Bulk or BBB) transports, selected
 * by the optional "transport" module parameter.  It also supports the
 * following protocols: RBC (0x01), ATAPI or SFF-8020i (0x02), QIC-157 (0c03),
 * UFI (0x04), SFF-8070i (0x05), and transparent SCSI (0x06), selected by
 * the optional "protocol" module parameter.  In addition, the default
 * Vendor ID, Product ID, and release number can be overridden.
 *
 * There is support for multiple logical units (LUNs), each of which has
 * its own backing file.  The number of LUNs can be set using the optional
 * "luns" module parameter (anywhere from 1 to 8), and the corresponding
 * files are specified using comma-separated lists for "file" and "ro".
 * The default number of LUNs is taken from the number of "file" elements;
 * it is 1 if "file" is not given.  If "removable" is not set then a backing
 * file must be specified for each LUN.  If it is set, then an unspecified
 * or empty backing filename means the LUN's medium is not loaded.
 *
 * Requirements are modest; only a bulk-in and a bulk-out endpoint are
 * needed (an interrupt-out endpoint is also needed for CBI).  The memory
 * requirement amounts to two 16K buffers, size configurable by a parameter.
 * Support is included for both full-speed and high-speed operation.
 *
 * Note that the driver is slightly non-portable in that it assumes a
 * single memory/DMA buffer will be useable for bulk-in, bulk-out, and
 * interrupt-in endpoints.  With most device controllers this isn't an
 * issue, but there may be some with hardware restrictions that prevent
 * a buffer from being used by more than one endpoint.
 *
 * Module options:
 *
 *	file=filename[,filename...]
 *				Required if "removable" is not set, names of
 *					the files or block devices used for
 *					backing storage
 *	ro=b[,b...]		Default false, booleans for read-only access
 *	removable		Default false, boolean for removable media
 *	luns=N			Default N = number of filenames, number of
 *					LUNs to support
 *	stall			Default determined according to the type of
 *					USB device controller (usually true),
 *					boolean to permit the driver to halt
 *					bulk endpoints
 *	transport=XXX		Default BBB, transport name (CB, CBI, or BBB)
 *	protocol=YYY		Default SCSI, protocol name (RBC, 8020 or
 *					ATAPI, QIC, UFI, 8070, or SCSI;
 *					also 1 - 6)
 *	vendor=0xVVVV		Default 0x0525 (NetChip), USB Vendor ID
 *	product=0xPPPP		Default 0xa4a5 (FSG), USB Product ID
 *	release=0xRRRR		Override the USB release number (bcdDevice)
 *	buflen=N		Default N=16384, buffer size used (will be
 *					rounded down to a multiple of
 *					PAGE_CACHE_SIZE)
 *
 * If CONFIG_USB_FILE_STORAGE_TEST is not set, only the "file", "ro",
 * "removable", "luns", and "stall" options are available; default values
 * are used for everything else.
 *
 * The pathnames of the backing files and the ro settings are available in
 * the attribute files "file" and "ro" in the lun<n> subdirectory of the
 * gadget's sysfs directory.  If the "removable" option is set, writing to
 * these files will simulate ejecting/loading the medium (writing an empty
 * line means eject) and adjusting a write-enable tab.  Changes to the ro
 * setting are not allowed when the medium is loaded.
 *
 * This gadget driver is heavily based on "Gadget Zero" by David Brownell.
 * The driver's SCSI command interface was based on the "Information
 * technology - Small Computer System Interface - 2" document from
 * X3T9.2 Project 375D, Revision 10L, 7-SEP-93, available at
 * <http://www.t10.org/ftp/t10/drafts/s2/s2-r10l.pdf>.  The single exception
 * is opcode 0x23 (READ FORMAT CAPACITIES), which was based on the
 * "Universal Serial Bus Mass Storage Class UFI Command Specification"
 * document, Revision 1.0, December 14, 1998, available at
 * <http://www.usb.org/developers/devclass_docs/usbmass-ufi10.pdf>.
 */

/*
 *				Driver Design
 *
 * The FSG driver is fairly straightforward.  There is a main kernel
 * thread that handles most of the work.  Interrupt routines field
 * callbacks from the controller driver: bulk- and interrupt-request
 * completion notifications, endpoint-0 events, and disconnect events.
 * Completion events are passed to the main thread by wakeup calls.  Many
 * ep0 requests are handled at interrupt time, but SetInterface,
 * SetConfiguration, and device reset requests are forwarded to the
 * thread in the form of "exceptions" using SIGUSR1 signals (since they
 * should interrupt any ongoing file I/O operations).
 *
 * The thread's main routine implements the standard command/data/status
 * parts of a SCSI interaction.  It and its subroutines are full of tests
 * for pending signals/exceptions -- all this polling is necessary since
 * the kernel has no setjmp/longjmp equivalents.  (Maybe this is an
 * indication that the driver really wants to be running in userspace.)
 * An important point is that so long as the thread is alive it keeps an
 * open reference to the backing file.  This will prevent unmounting
 * the backing file's underlying filesystem and could cause problems
 * during system shutdown, for example.  To prevent such problems, the
 * thread catches INT, TERM, and KILL signals and converts them into
 * an EXIT exception.
 *
 * In normal operation the main thread is started during the gadget's
 * fsg_bind() callback and stopped during fsg_unbind().  But it can also
 * exit when it receives a signal, and there's no point leaving the
 * gadget running when the thread is dead.  So just before the thread
 * exits, it deregisters the gadget driver.  This makes things a little
 * tricky: The driver is deregistered at two places, and the exiting
 * thread can indirectly call fsg_unbind() which in turn can tell the
 * thread to exit.  The first problem is resolved through the use of the
 * REGISTERED atomic bitflag; the driver will only be deregistered once.
 * The second problem is resolved by having fsg_unbind() check
 * fsg->state; it won't try to stop the thread if the state is already
 * FSG_STATE_TERMINATED.
 *
 * To provide maximum throughput, the driver uses a circular pipeline of
 * buffer heads (struct fsg_buffhd).  In principle the pipeline can be
 * arbitrarily long; in practice the benefits don't justify having more
 * than 2 stages (i.e., double buffering).  But it helps to think of the
 * pipeline as being a long one.  Each buffer head contains a bulk-in and
 * a bulk-out request pointer (since the buffer can be used for both
 * output and input -- directions always are given from the host's
 * point of view) as well as a pointer to the buffer and various state
 * variables.
 *
 * Use of the pipeline follows a simple protocol.  There is a variable
 * (fsg->next_buffhd_to_fill) that points to the next buffer head to use.
 * At any time that buffer head may still be in use from an earlier
 * request, so each buffer head has a state variable indicating whether
 * it is EMPTY, FULL, or BUSY.  Typical use involves waiting for the
 * buffer head to be EMPTY, filling the buffer either by file I/O or by
 * USB I/O (during which the buffer head is BUSY), and marking the buffer
 * head FULL when the I/O is complete.  Then the buffer will be emptied
 * (again possibly by USB I/O, during which it is marked BUSY) and
 * finally marked EMPTY again (possibly by a completion routine).
 *
 * A module parameter tells the driver to avoid stalling the bulk
 * endpoints wherever the transport specification allows.  This is
 * necessary for some UDCs like the SuperH, which cannot reliably clear a
 * halt on a bulk endpoint.  However, under certain circumstances the
 * Bulk-only specification requires a stall.  In such cases the driver
 * will halt the endpoint and set a flag indicating that it should clear
 * the halt in software during the next device reset.  Hopefully this
 * will permit everything to work correctly.  Furthermore, although the
 * specification allows the bulk-out endpoint to halt when the host sends
 * too much data, implementing this would cause an unavoidable race.
 * The driver will always use the "no-stall" approach for OUT transfers.
 *
 * One subtle point concerns sending status-stage responses for ep0
 * requests.  Some of these requests, such as device reset, can involve
 * interrupting an ongoing file I/O operation, which might take an
 * arbitrarily long time.  During that delay the host might give up on
 * the original ep0 request and issue a new one.  When that happens the
 * driver should not notify the host about completion of the original
 * request, as the host will no longer be waiting for it.  So the driver
 * assigns to each ep0 request a unique tag, and it keeps track of the
 * tag value of the request associated with a long-running exception
 * (device-reset, interface-change, or configuration-change).  When the
 * exception handler is finished, the status-stage response is submitted
 * only if the current ep0 request tag is equal to the exception request
 * tag.  Thus only the most recently received ep0 request will get a
 * status-stage response.
 *
 * Warning: This driver source file is too long.  It ought to be split up
 * into a header file plus about 3 separate .c files, to handle the details
 * of the Gadget, USB Mass Storage, and SCSI protocols.
 */
#define FPGA_VERIFY_MODE

#include <linux/kallsyms.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/utsname.h>
#include <linux/kdev_t.h>
#include <linux/proc_fs.h>
#include <linux/owl_fs_adfus.h>
#include <linux/reboot.h>
#include <asm/unaligned.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

extern void set_dwc3_gadget_plugin_flag(int flag);

#ifdef FPGA_VERIFY_MODE
#include "mbr_info.h"

#if 0
struct uparam {
	unsigned int flash_partition;
	unsigned int devnum_in_phypart;
};
#endif

#else
#include <mach/hardware.h>
#include <mach/atc260x/atc260x.h>

#include "../../../include/mbr_info.h"
#include "../../../include/boot/afinfo.h"
#endif
/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#ifdef FPGA_VERIFY_MODE
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"
#else
#include "../gadget/usbstring.c"
#include "../gadget/config.c"
#include "../gadget/epautoconf.c"
#endif
/*-------------------------------------------------------------------------*/

#define DRIVER_DESC		"File-backed Storage Gadget"
#define DRIVER_NAME		"g_file_storage"
#define DRIVER_VERSION		"7 August 2007"

static const char longname[] = DRIVER_DESC;
static const char shortname[] = DRIVER_NAME;

#if 0
struct uparam {
	unsigned int flash_partition;
	unsigned int devnum_in_phypart;
};
#endif


#define ADFUS_PROC
#ifdef ADFUS_PROC

enum probatch_status {
	PROBATCH_START = 0,
	PROBATCH_INSTAL_FLASH,
	PROBATCH_FINISH_INSTALL_FLASH,
	PROBATCH_WRITE_PHY,
	PROBATCH_FINISH_WRITE_PHY,
	PROBATCH_FORMAT,
	PROBATCH_FINISH_FORMAT,
	PROBATCH_FINISH,	
	PROBATCH_FINISH_OK,
};

#define ADFUS_PROC_FILE_LEN 64
char probatch_phase[ADFUS_PROC_FILE_LEN];
char all_probatch_phase[][ADFUS_PROC_FILE_LEN]=
{
	"null",
	"install_flash",
	"finish_install_flash",
	"write_phy",
	"finish_write_phy",
	"format",
	"finish_format",
	"finish",
	"finish_ok",
};

int set_probatch_phase(int id)
{
	strcpy(probatch_phase, all_probatch_phase[id]);
	printk("%s %d: %s\n", __FUNCTION__, __LINE__, probatch_phase);

	return 0;
}	
//EXPORT_SYMBOL_GPL(set_probatch_phase);

int is_probatch_phase(int id)
{
	int ret;
	
	ret = memcmp(probatch_phase, all_probatch_phase[id], strlen(all_probatch_phase[id]));
	return ret;
}
//EXPORT_SYMBOL_GPL(is_probatch_phase);

static ssize_t adfus_proc_read(struct file *file, char __user *buf, 
			       size_t count, loff_t *ppos)
{
	size_t len;

	if (*ppos != 0)
		return 0;

	len = strlen(probatch_phase) + 1;
	if (len > count)
		len = count;

	if (copy_to_user(buf, probatch_phase, len))
		return -EFAULT;

	*ppos += len;

	return len;
}

static ssize_t adfus_proc_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (*ppos != 0)
		return 0;
    
	if (count > (ADFUS_PROC_FILE_LEN - 1))
		count = ADFUS_PROC_FILE_LEN - 1;

	if (copy_from_user(probatch_phase, buf, count))
		return -EFAULT;

	probatch_phase[count] = 0;

	*ppos += count;

	return count;
}

static int adfus_proc_open(struct inode *inode, struct file *filp)
{
	nonseekable_open(inode, filp);
	return 0;
}

static const struct file_operations proc_adfu_operations = {
	.owner		= THIS_MODULE,
	.open       = adfus_proc_open,
	.read		= adfus_proc_read,
	.write		= adfus_proc_write,
	.llseek		= no_llseek,
};


char adfus_proc_path[] = "adfus_proc";
static struct proc_dir_entry *adfus_proc_entry;

#endif

/* Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with any other driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures. */
#define DRIVER_VENDOR_ID	0x10D6	/* NetChip */
#define DRIVER_PRODUCT_ID	0x10D6
/* Linux-USB File-backed Storage Gadget */

/*
 * This driver assumes self-powered hardware and has no way for users to
 * trigger remote wakeup.  It uses autoconfiguration to select endpoints
 * and endpoint addresses.
 */

#define MAJOR_OF_MMS	254
#define MAJOR_OF_CARD	179
#define MAJOR_OF_NAND	93

#define CARD_MEDIUM	10
#define NAND_MEDIUM	20
#define MMS_MEDIUM	30

#define WHEN_FOR_ADFU	1

/* NAND directly ADFU is not ready yet, defined for debug */

#define SUPPORT_DIRECT_UDISK
unsigned char no_finish_reply;

static int first_start;

static int adfu_success_flag;
static int need_format = 0;
static char need_restart = 1;

char format_fs_name[8];
char format_disk_name[32];
char disk_label[][8]={
	"data",
	"cache"
};
char *format_disk_label;

static int first_start;

static unsigned int uniSerial;

/* add by hmwei */
extern char probatch_phase[];
int cmnd_sequence_first_flag=1;
struct file *misc_disk_filp;
char last_cmnd=-1;

//extern int set_probatch_phase(int id);
//extern int is_probatch_phase(int id);
#ifdef FPGA_VERIFY_MODE
#else
extern int aotg_ep_switchbuf(struct usb_ep *_ep);

/* add by hmwei */

extern void asoc_restart(void);
#endif

/*-------------------------------------------------------------------------*/

#define LDBG(lun, fmt, args...)			\
	dev_dbg(&(lun)->dev , fmt , ## args)
#define MDBG(fmt, args...)				\
	pr_debug(DRIVER_NAME ": " fmt , ## args)

//	#define VERBOSE_DEBUG

#ifndef DEBUG
#undef VERBOSE_DEBUG
#undef DUMP_MSGS
#endif /* !DEBUG */

#ifdef VERBOSE_DEBUG
#define VLDBG	LDBG
#else
#define VLDBG(lun, fmt, args...)		\
	do { } while (0)
#endif /* VERBOSE_DEBUG */

#define LERROR(lun, fmt, args...)		\
	dev_err(&(lun)->dev, fmt, ## args)
#define LWARN(lun, fmt,args...)			\
	dev_warn(&(lun)->dev, fmt, ## args)
#define LINFO(lun, fmt, args...)		\
	dev_info(&(lun)->dev, fmt, ## args)

#define MINFO(fmt, args...)			\
	pr_info(DRIVER_NAME ": " fmt , ## args)

#define DBG(d, fmt, args...)				\
	dev_dbg(&(d)->gadget->dev , fmt , ## args)
#define VDBG(d, fmt, args...)				\
	dev_vdbg(&(d)->gadget->dev , fmt , ## args)
#define ERROR(d, fmt, args...)				\
	dev_err(&(d)->gadget->dev , fmt , ## args)
#define WARNING(d, fmt, args...)			\
	dev_warn(&(d)->gadget->dev , fmt , ## args)
#define INFO(d, fmt, args...)				\
	dev_info(&(d)->gadget->dev , fmt , ## args)

/*-------------------------------------------------------------------------*/

/* Encapsulate the module parameter settings */

#define MAX_LUNS	8

/* USB protocol value = the transport method */
#define USB_PR_CBI	0x00	/* Control/Bulk/Interrupt */
#define USB_PR_CB	0x01	/* Control/Bulk w/o interrupt */
#define USB_PR_BULK	0x50	/* Bulk-only */

/* USB subclass value = the protocol encapsulation */
#define USB_SC_RBC	0x01	/* Reduced Block Commands (flash) */
#define USB_SC_8020	0x02	/* SFF-8020i, MMC-2, ATAPI (CD-ROM) */
#define USB_SC_QIC	0x03	/* QIC-157 (tape) */
#define USB_SC_UFI	0x04	/* UFI (floppy) */
#define USB_SC_8070	0x05	/* SFF-8070i (removable) */
#define USB_SC_SCSI	0x06	/* Transparent SCSI */

static struct {
	char *file[MAX_LUNS];
	int ro[MAX_LUNS];
	unsigned int num_filenames;
	unsigned int num_ros;
	unsigned int nluns;

	int removable;
	int can_stall;

	char *transport_parm;
	char *protocol_parm;
	unsigned short vendor;
	unsigned short product;
	unsigned short release;
	unsigned int buflen;

	int transport_type;
	char *transport_name;
	int protocol_type;
	char *protocol_name;

} mod_data = {			/* Default values */
	/* .file[0] = "/dev/actd", */
	.transport_parm = "BBB",
	//.protocol_parm = "SCSI",
	.protocol_parm = "8070i",


	.transport_type = USB_PR_BULK,      /*! transport type signed by usb-if*/
	.transport_name = "Bulk-only",     /*! transport name*/
	.protocol_type = USB_SC_8070,      /*! protocol type*/
	.protocol_name = "8070i",     /*! protocol name*/

	.nluns = 2,	/* tmp used by wlt 20091130 */
	.removable = 1,
	.can_stall = 0,
	.vendor = DRIVER_VENDOR_ID,
	.product = DRIVER_PRODUCT_ID,
	.release = 0xffff,	/* Use controller chip type */
	.buflen = 65536,    /* 2014-3-7 14:10  emmc, larger buffer faster*/
};

module_param_array_named(file, mod_data.file, charp, &mod_data.num_filenames,
			 S_IRUGO);
MODULE_PARM_DESC(file, "names of backing files or devices");


module_param_named(uniSerial, uniSerial, uint, S_IRUGO);
MODULE_PARM_DESC(uniSerial,
		 "1 to indicate we should report iSerialNumber as unicode mod");

//MODULE_PARM_DESC(file, "names of backing files or devices");

module_param_named(vendor, mod_data.vendor, ushort, S_IRUGO);
MODULE_PARM_DESC(vendor, "USB Vendor ID");

module_param_named(product, mod_data.product, ushort, S_IRUGO);
MODULE_PARM_DESC(product, "USB Product ID");

module_param_named(release, mod_data.release, ushort, S_IRUGO);
MODULE_PARM_DESC(release, "USB release number");

/*-------------------------------------------------------------------------*/


/* Bulk-only data structures */

/* Command Block Wrapper */
struct bulk_cb_wrap {
	__le32 Signature;	/* Contains 'USBC' */
	u32 Tag;		/* Unique per command id */
	__le32 DataTransferLength;	/* Size of the data */
	u8 Flags;		/* Direction in bit 7 */
	u8 Lun;			/* LUN (normally 0) */
	u8 Length;		/* Of the CDB, <= MAX_COMMAND_SIZE */
	u8 CDB[16];		/* Command Data Block */
};

#define USB_BULK_CB_WRAP_LEN	31
#define USB_BULK_CB_SIG		0x43425355	/* Spells out USBC */
#define USB_BULK_IN_FLAG	0x80

/* Command Status Wrapper */
struct bulk_cs_wrap {
	__le32 Signature;	/* Should = 'USBS' */
	u32 Tag;		/* Same as original command */
	__le32 Residue;		/* Amount not transferred */
	u8 Status;		/* See below */
};

#define USB_BULK_CS_WRAP_LEN	13
#define USB_BULK_CS_SIG		0x53425355	/* Spells out 'USBS' */
#define USB_STATUS_PASS		0
#define USB_STATUS_FAIL		1
#define USB_STATUS_PHASE_ERROR	2

/* Bulk-only class specific requests */
#define USB_BULK_RESET_REQUEST		0xff
#define USB_BULK_GET_MAX_LUN_REQUEST	0xfe

/* CBI Interrupt data structure */
struct interrupt_data {
	u8 bType;
	u8 bValue;
};

#define CBI_INTERRUPT_DATA_LEN		2

/* CBI Accept Device-Specific Command request */
#define USB_CBI_ADSC_REQUEST		0x00

#define MAX_COMMAND_SIZE	16
/* Length of a SCSI Command Data Block */

/* SCSI commands that we recognize */
#define SC_FORMAT_UNIT			0x04
#define SC_INQUIRY			0x12
#define SC_MODE_SELECT_6		0x15
#define SC_MODE_SELECT_10		0x55
#define SC_MODE_SENSE_6			0x1a
#define SC_MODE_SENSE_10		0x5a
#define SC_PREVENT_ALLOW_MEDIUM_REMOVAL	0x1e
#define SC_READ_6			0x08
#define SC_READ_10			0x28
#define SC_READ_12			0xa8
#define SC_READ_CAPACITY		0x25
#define SC_READ_FORMAT_CAPACITIES	0x23
#define SC_RELEASE			0x17
#define SC_REQUEST_SENSE		0x03
#define SC_RESERVE			0x16
#define SC_SEND_DIAGNOSTIC		0x1d
#define SC_START_STOP_UNIT		0x1b
#define SC_SYNCHRONIZE_CACHE		0x35
#define SC_TEST_UNIT_READY		0x00
#define SC_VERIFY			0x2f
#define SC_WRITE_6			0x0a
#define SC_WRITE_10			0x2a
#define SC_WRITE_12			0xaa

/* make sure the U-device is ACTIONS */
#define	 SC_ACTIONS_INQUIRY		0xcc
#define  SC_ADFU_UPGRADE		0xcd
/* sub-code used for up-tool */
#define	 SC_ADFU_ACCESS_MBR		0x90
#define	 SC_ADFU_WRITE_ROOTFS		0x11
#define	 SC_ADFU_READ_ROOTFS		0x91

#define  SC_ADFU_ACCESS_INTERNAL_RAM	0x13

#define  SC_ADFU_FORMAT_FLASH		0x16
#define	 SC_ADFU_TEST_FLASHRDY		0x17

/* sub-code used for u-disk upgrade */
#define	 SC_ADFU_WRITE_FILES		0x12

/* FILE TYPE PARAM */
#define  FP_MBREC		0x01
#define  FP_MBRINFO		0x02
#define  FP_VMLINUX		0x03

#define  SC_ADFU_TEST_READY		0x13

/* TEST ACTION TYPE PARAM */
#define  SC_ADFU_FILE_READY	0x01
#define	 SC_ADFU_APP_READY	0x02

#define	 SC_ADFU_START_APP		0x14
#define  SC_ADFU_CREATE_FILES		0x15

#define SC_ADFU_DOWNLOAD_IMG	0x60

#define SC_ADFU_INFO	0xc0
#define SC_ADFU_TFEROVER		0x30

/* upgrade success-ful */
#define  SC_ADFU_SUCCESSFUL		0xb0

/* SCSI Sense Key/Additional Sense Code/ASC Qualifier values */
#define SS_NO_SENSE				0
#define SS_COMMUNICATION_FAILURE		0x040800
#define SS_INVALID_COMMAND			0x052000
#define SS_INVALID_FIELD_IN_CDB			0x052400
#define SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE	0x052100
#define SS_LOGICAL_UNIT_NOT_SUPPORTED		0x052500
#define SS_MEDIUM_NOT_PRESENT			0x023a00
#define SS_MEDIUM_REMOVAL_PREVENTED		0x055302
#define SS_NOT_READY_TO_READY_TRANSITION	0x062800
#define SS_RESET_OCCURRED			0x062900
#define SS_SAVING_PARAMETERS_NOT_SUPPORTED	0x053900
#define SS_UNRECOVERED_READ_ERROR		0x031100
#define SS_WRITE_ERROR				0x030c02
#define SS_WRITE_PROTECTED			0x072700

#define SK(x)		((u8) ((x) >> 16))	/* Sense Key byte, etc. */
#define ASC(x)		((u8) ((x) >> 8))
#define ASCQ(x)		((u8) (x))

/*-------------------------------------------------------------------------*/

#define transport_is_bbb()	1
#define transport_is_cbi()	0

struct lun {
	struct file *filp;
	loff_t file_length;
	loff_t num_sectors;
	u8 sector_size_mask;
	u8 disk_type;		/* add by wlt 20091130 */
	dev_t devnum;		/* add by wlt 20091223 */

	unsigned int ro:1;
	unsigned int prevent_medium_removal:1;
	unsigned int registered:1;
	unsigned int info_valid:1;

	u32 sense_data;
	u32 sense_data_info;
	u32 unit_attention_data;

	struct device dev;
};

#define backing_file_is_open(curlun)	((curlun)->filp != NULL)

static struct lun *dev_to_lun(struct device *dev)
{
	return container_of(dev, struct lun, dev);
}

/* Big enough to hold our biggest descriptor */
#define EP0_BUFSIZE	256
#define DELAYED_STATUS	(EP0_BUFSIZE + 999)	/* An impossibly large value */

/* Number of buffers we will use.  2 is enough for double-buffering */
#define NUM_BUFFERS	2


enum fsg_buffer_state {
	BUF_STATE_EMPTY = 0,
	BUF_STATE_FULL,
	BUF_STATE_BUSY
};

struct fsg_buffhd {
	void *buf;
	enum fsg_buffer_state state;
	struct fsg_buffhd *next;

	/* The NetChip 2280 is faster, and handles some protocol faults
	 * better, if we don't submit any short bulk-out read requests.
	 * So we will record the intended request length here. */
	unsigned int bulk_out_intended_length;

	struct usb_request *inreq;
	int inreq_busy;
	struct usb_request *outreq;
	int outreq_busy;
};

enum fsg_state {
	FSG_STATE_COMMAND_PHASE = -10,	/* This one isn't used anywhere */
	FSG_STATE_DATA_PHASE,
	FSG_STATE_STATUS_PHASE,

	FSG_STATE_IDLE = 0,
	FSG_STATE_ABORT_BULK_OUT,
	FSG_STATE_RESET,
	FSG_STATE_INTERFACE_CHANGE,
	FSG_STATE_CONFIG_CHANGE,
	FSG_STATE_DISCONNECT,
	FSG_STATE_EXIT,
	FSG_STATE_TERMINATED
};

enum data_direction {
	DATA_DIR_UNKNOWN = 0,
	DATA_DIR_FROM_HOST,
	DATA_DIR_TO_HOST,
	DATA_DIR_NONE
};

struct fsg_dev {
	/* lock protects: state, all the req_busy's, and cbbuf_cmnd */
	spinlock_t lock;
	struct usb_gadget *gadget;

	/* filesem protects: backing files in use */
	struct rw_semaphore filesem;

	/* reference counting: wait until all LUNs are released */
	struct kref ref;

	struct usb_ep *ep0;	/* Handy copy of gadget->ep0 */
	struct usb_request *ep0req;	/* For control responses */
	unsigned int ep0_req_tag;
	const char *ep0req_name;

	struct usb_request *intreq;	/* For interrupt responses */
	int intreq_busy;
	struct fsg_buffhd *intr_buffhd;

	unsigned int bulk_out_maxpacket;
	enum fsg_state state;	/* For exception handling */
	unsigned int exception_req_tag;

	u8 config, new_config;

	unsigned int running:1;
	unsigned int bulk_in_enabled:1;
	unsigned int bulk_out_enabled:1;
	unsigned int intr_in_enabled:1;
	unsigned int phase_error:1;
	unsigned int short_packet_received:1;
	unsigned int bad_lun_okay:1;

	unsigned long atomic_bitflags;
#define REGISTERED		0
#define IGNORE_BULK_OUT		1
/* #define CLEAR_BULK_HALTS	1 */
#define SUSPENDED		2

	struct usb_ep *bulk_in;
	struct usb_ep *bulk_out;
	struct usb_ep *intr_in;

	struct fsg_buffhd *next_buffhd_to_fill;
	struct fsg_buffhd *next_buffhd_to_drain;
	struct fsg_buffhd buffhds[NUM_BUFFERS];

	int thread_wakeup_needed;
	struct completion thread_notifier;
	struct task_struct *thread_task;

	int cmnd_size;
	u8 cmnd[MAX_COMMAND_SIZE];
	enum data_direction data_dir;
	u32 data_size;
	u32 data_size_from_cmnd;
	u32 tag;
	unsigned int lun;
	u32 residue;
	u32 usb_amount_left;

	/* The CB protocol offers no way for a host to know when a command
	 * has completed.  As a result the next command may arrive early,
	 * and we will still have to handle it.  For that reason we need
	 * a buffer to store new commands when using CB (or CBI, which
	 * does not oblige a host to wait for command completion either). */
	int cbbuf_cmnd_size;
	u8 cbbuf_cmnd[MAX_COMMAND_SIZE];

	unsigned int nluns;
	struct lun *luns;
	struct lun *curlun;
};

/****************************************************/
/**** for the app to know current state of udisk ****/

#define TIMEOUT (HZ * 1)
struct status_ops {
	unsigned long timeout;
	wait_queue_head_t wait;
	struct completion thread_exit;
	unsigned char quit;
} status_arg;

#define STATE_IDLE	0
#define STATE_READING	1
#define STATE_WRITING	2

static unsigned int card_dirty_flag;
static unsigned int nand_dirty_flag;

int check_partition_flag=0, chk_HdcpFlag = 0;//partiton update
int update_phy_boot=0;//
	
/*****************************************************/
static int do_ADFU_acmbr(struct fsg_dev *);
static int do_ADFU_wtrootfs(struct fsg_dev *);
static void handle_exception(struct fsg_dev *fsg);
static int open_backing_file(struct lun *curlun, const char *filename);

extern void machine_restart(char *cmd);
extern void kernel_power_off(void);
extern void asoc_pm_halt_upgrade(void);

typedef void (*fsg_routine_t)(struct fsg_dev *);

static int exception_in_progress(struct fsg_dev *fsg)
{
	return fsg->state > FSG_STATE_IDLE;
}

/* Make bulk-out requests be divisible by the maxpacket size */
static void set_bulk_out_req_length(struct fsg_dev *fsg,
				    struct fsg_buffhd *bh, unsigned int length)
{
	unsigned int	rem; 

	bh->bulk_out_intended_length = length;
	rem = length % fsg->bulk_out_maxpacket;
	if (rem > 0)
		length += fsg->bulk_out_maxpacket - rem; 
	bh->outreq->length = length;
}

static struct fsg_dev *the_fsg;
static struct usb_gadget_driver fsg_driver;

static void close_backing_file(struct lun *curlun);
static void close_all_backing_files(struct fsg_dev *fsg);

#define CONFIG_USB_GADGET_DUALSPEED
/*-------------------------------------------------------------------------*/
static int act_gadget_is_dualspeed(struct usb_gadget *g)
{
#ifdef CONFIG_USB_GADGET_DUALSPEED
	return 1;
#else
	return 0;
#endif
}

#ifdef DUMP_MSGS

static void dump_msg(struct fsg_dev *fsg, const char *label,
		     const u8 *buf, unsigned int length)
{
	if (length < 512) {
		DBG(fsg, "%s, length %u:\n", label, length);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET,
			       16, 1, buf, length, 0);
	}
}

static void dump_cdb(struct fsg_dev *fsg)
{
}

#else

static void dump_msg(struct fsg_dev *fsg, const char *label,
		     const u8 *buf, unsigned int length)
{
}

#ifdef VERBOSE_DEBUG

static void dump_cdb(struct fsg_dev *fsg)
{
	print_hex_dump(KERN_DEBUG, "SCSI CDB: ", DUMP_PREFIX_NONE,
		       16, 1, fsg->cmnd, fsg->cmnd_size, 0);
}

#else

static void dump_cdb(struct fsg_dev *fsg)
{
}

#endif /* VERBOSE_DEBUG */
#endif /* DUMP_MSGS */

#define EP_STALL (1<<6)
static int fsg_set_halt(struct fsg_dev *fsg, struct usb_ep *ep)
{
	const char *name;

	if (ep == fsg->bulk_in)
		name = "bulk-in";
	else if (ep == fsg->bulk_out)
		name = "bulk-out";
	else
		name = ep->name;
	DBG(fsg, "%s set halt\n", name);
	return usb_ep_set_halt(ep);
}

/*-------------------------------------------------------------------------*/

/* Routines for unaligned data access */

static u32 get_le32(u8 *buf)
{
	return ((u32) buf[3] << 24) | ((u32) buf[2] << 16) |
		((u32) buf[1] << 8) | ((u32) buf[0]);
}

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.  Also the (static) config and interface
 * descriptors are adjusted during fsg_bind().
 */
#define STRING_MANUFACTURER	1
#define STRING_PRODUCT		2
#define STRING_SERIAL		3
#define STRING_CONFIG		4
#define STRING_INTERFACE	5

/* There is only one configuration. */
#define	CONFIG_VALUE		1

static struct usb_device_descriptor device_desc = {
	.bLength = sizeof device_desc,
	.bDescriptorType = USB_DT_DEVICE,

	.bcdUSB = __constant_cpu_to_le16(0x0200),
	.bDeviceClass = USB_CLASS_PER_INTERFACE,

	/* The next three values can be overridden by module parameters */
	.idVendor = __constant_cpu_to_le16(DRIVER_VENDOR_ID),
	.idProduct = __constant_cpu_to_le16(DRIVER_PRODUCT_ID),
	.bcdDevice = __constant_cpu_to_le16(0x0100),

	.iManufacturer = 0,
	.iProduct = 0,
	.iSerialNumber = 0,
	.bNumConfigurations = 1,
};

static struct usb_config_descriptor config_desc = {
	.bLength = sizeof config_desc,
	.bDescriptorType = USB_DT_CONFIG,

	/* wTotalLength computed by usb_gadget_config_buf() */
	.bNumInterfaces = 1,
	.bConfigurationValue = CONFIG_VALUE,
	.iConfiguration = STRING_CONFIG,
	/*.bmAttributes = USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER, */
	.bmAttributes = USB_CONFIG_ATT_ONE,
	//.bmAttributes = USB_CONFIG_ATT_ONE |USB_CONFIG_ATT_SELFPOWER,
	/*.bMaxPower = CONFIG_USB_GADGET_VBUS_DRAW / 2, */
	.bMaxPower = CONFIG_USB_GADGET_VBUS_DRAW,
	//.bMaxPower = 0x3F,
};

static struct usb_otg_descriptor otg_desc = {
	.bLength = sizeof(otg_desc),
	.bDescriptorType = USB_DT_OTG,

	.bmAttributes = USB_OTG_SRP,
};

/* There is only one interface. */

static struct usb_interface_descriptor intf_desc = {
	.bLength = sizeof intf_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bNumEndpoints = 2,	/* Adjusted during fsg_bind() */
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0xff,	/* Adjusted during fsg_bind() */
	.bInterfaceProtocol = 0xff,	/* Adjusted during fsg_bind() */
	.iInterface = STRING_INTERFACE,
};

/* Three full-speed endpoint descriptors: bulk-in, bulk-out,
 * and interrupt-in. */

static struct usb_endpoint_descriptor fs_bulk_in_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

static struct usb_endpoint_descriptor fs_bulk_out_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

static struct usb_endpoint_descriptor fs_intr_in_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = __constant_cpu_to_le16(2),
	.bInterval = 32,	/* frames -> 32 ms */
};

static const struct usb_descriptor_header *fs_function[] = {
	(struct usb_descriptor_header *)&otg_desc,
	(struct usb_descriptor_header *)&intf_desc,
	(struct usb_descriptor_header *)&fs_bulk_in_desc,
	(struct usb_descriptor_header *)&fs_bulk_out_desc,
	(struct usb_descriptor_header *)&fs_intr_in_desc,
	NULL,
};

#define FS_FUNCTION_PRE_EP_ENTRIES	2

/*
 * USB 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 *
 * That means alternate endpoint descriptors (bigger packets)
 * and a "device qualifier" ... plus more construction options
 * for the config descriptor.
 */
static struct usb_qualifier_descriptor dev_qualifier = {
	.bLength = sizeof dev_qualifier,
	.bDescriptorType = USB_DT_DEVICE_QUALIFIER,

	.bcdUSB = __constant_cpu_to_le16(0x0200),
	.bDeviceClass = USB_CLASS_PER_INTERFACE,

	.bNumConfigurations = 1,
};

static struct usb_endpoint_descriptor hs_bulk_in_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_in_desc during fsg_bind() */
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_bulk_out_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_out_desc during fsg_bind() */
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = __constant_cpu_to_le16(512),
	.bInterval = 1,		/* NAK every 1 uframe */
};

static struct usb_endpoint_descriptor hs_intr_in_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_intr_in_desc during fsg_bind() */
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = __constant_cpu_to_le16(2),
	.bInterval = 9,		/* 2**(9-1) = 256 uframes -> 32 ms */
};

static const struct usb_descriptor_header *hs_function[] = {
	(struct usb_descriptor_header *)&otg_desc,
	(struct usb_descriptor_header *)&intf_desc,
	(struct usb_descriptor_header *)&hs_bulk_in_desc,
	(struct usb_descriptor_header *)&hs_bulk_out_desc,
	(struct usb_descriptor_header *)&hs_intr_in_desc,
	NULL,
};


static struct usb_endpoint_descriptor
fsg_ss_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_in_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor fsg_ss_bulk_in_comp_desc = {
	.bLength =		sizeof(fsg_ss_bulk_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/*.bMaxBurst =		DYNAMIC, */
};

static struct usb_endpoint_descriptor
fsg_ss_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_out_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor fsg_ss_bulk_out_comp_desc = {
	.bLength =		sizeof(fsg_ss_bulk_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/*.bMaxBurst =		DYNAMIC, */
};

static struct usb_descriptor_header *fsg_ss_function[] = {
	(struct usb_descriptor_header *)&otg_desc,
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_in_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_in_comp_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_out_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_out_comp_desc,
	NULL,
};

#define HS_FUNCTION_PRE_EP_ENTRIES	2

/* Maxpacket and other transfer characteristics vary by speed. */
static struct usb_endpoint_descriptor
*ep_desc(struct usb_gadget *g, struct usb_endpoint_descriptor
	 *fs, struct usb_endpoint_descriptor
	 *hs, struct usb_endpoint_descriptor
	 *ss)
{
	/* printk(KERN_INFO "act_gadget_is_dualspeed:0x%x.\n",
	   act_gadget_is_dualspeed(g));
	   printk(KERN_INFO "%d",
	   g->speed == USB_SPEED_HIGH); */
	if (gadget_is_superspeed(g)&& g->speed == USB_SPEED_SUPER)
	{
		return ss;
	}
	
	if (act_gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
	{
		return hs;
	}
	printk("reutrn fs, 0x%x, 0x%x,0x%x\n",act_gadget_is_dualspeed(g),g->speed,USB_SPEED_HIGH );
	return fs;
}

/* The CBI specification limits the serial string to 12 uppercase hexadecimal
 * characters. */
static char manufacturer[64];
static char serial[13];
module_param_string(iSerialNumber, serial, 13, S_IRUGO);

/* Static strings, in UTF-8 (for simplicity we use only ASCII characters) */
static struct usb_string strings[] = {
	{STRING_MANUFACTURER, manufacturer},
	{STRING_PRODUCT, longname},
	{STRING_SERIAL, serial},
	{STRING_CONFIG, "Bus-powered"},
	{STRING_INTERFACE, "Mass Storage"},
	{}
};

static struct usb_gadget_strings stringtab = {
	.language = 0x0409,	/* en-us */
	.strings = strings,
};

int wait_adfus_proc(int probatch_phase)
{
	int ret;
	while(1)
	{
		ret = is_probatch_phase(probatch_phase);
		if(ret == 0)
		{
			break;
		}
		VLDBG("schedule_timeout 1000,ret:%d,probatch_phase:%s\n",ret, probatch_phase);
		schedule_timeout_interruptible(msecs_to_jiffies(1000));	
	}
	
	return ret;
}

#ifdef FPGA_VERIFY_MODE



unsigned int nand_part[MAX_PARTITION];
mbr_info_t *mbr_info_buf;

#endif
/*
 * Config descriptors must agree with the code that sets configurations
 * and with code managing interfaces and their altsettings.  They must
 * also handle different speeds and other-speed requests.
 */
static int populate_config_buf(struct usb_gadget *gadget,
			       u8 *buf, u8 type, unsigned index)
{
	enum usb_device_speed speed = gadget->speed;
	int len;
	const struct usb_descriptor_header **function;

	if (index > 0)
		return -EINVAL;

	if (act_gadget_is_dualspeed(gadget) && type == USB_DT_OTHER_SPEED_CONFIG)
		speed = (USB_SPEED_FULL + USB_SPEED_HIGH) - speed;
	if (gadget_is_superspeed(gadget) && speed == USB_SPEED_SUPER)
		function = fsg_ss_function;
	else if (act_gadget_is_dualspeed(gadget) && speed == USB_SPEED_HIGH)
		function = hs_function;
	else
		function = fs_function;

	/* for now, don't advertise srp-only devices */
	if (!gadget_is_otg(gadget))
		function++;

	len = usb_gadget_config_buf(&config_desc, buf, EP0_BUFSIZE, function);
	((struct usb_config_descriptor *)buf)->bDescriptorType = type;
	return len;
}

/*-------------------------------------------------------------------------*/

/* These routines may be called in process context or in_irq */

/* Caller must hold fsg->lock */
static void wakeup_thread(struct fsg_dev *fsg)
{
	/* Tell the main thread that something has happened */
	fsg->thread_wakeup_needed = 1;
	if (fsg->thread_task)
		wake_up_process(fsg->thread_task);
}

static void raise_exception(struct fsg_dev *fsg, enum fsg_state new_state)
{
	unsigned long flags;

	/* Do nothing if a higher-priority exception is already in progress.
	 * If a lower-or-equal priority exception is in progress, preempt it
	 * and notify the main thread by sending it a signal. */
	spin_lock_irqsave(&fsg->lock, flags);
	if (fsg->state <= new_state) {
		fsg->exception_req_tag = fsg->ep0_req_tag;
		fsg->state = new_state;
		if (fsg->thread_task) {
			send_sig_info(SIGUSR1, SEND_SIG_FORCED,
				      fsg->thread_task);
		}
	}
	spin_unlock_irqrestore(&fsg->lock, flags);
}

/*-------------------------------------------------------------------------*/

/* The disconnect callback and ep0 routines.  These always run in_irq,
 * except that ep0_queue() is called in the main thread to acknowledge
 * completion of various requests: set config, set interface, and
 * Bulk-only device reset. */

static void fsg_disconnect(struct usb_gadget *gadget)
{
	struct fsg_dev *fsg = get_gadget_data(gadget);

	DBG(fsg, "disconnect or port reset\n");
	raise_exception(fsg, FSG_STATE_DISCONNECT);
}

static int ep0_queue(struct fsg_dev *fsg)
{
	int rc;

	rc = usb_ep_queue(fsg->ep0, fsg->ep0req, GFP_ATOMIC);
	if (rc != 0 && rc != -ESHUTDOWN) {

		/* We can't do much more than wait for a reset */
		WARNING(fsg, "error in submission: %s --> %d\n",
			fsg->ep0->name, rc);
	}
	return rc;
}

static void ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev *fsg = ep->driver_data;

	if (req->actual > 0)
		dump_msg(fsg, fsg->ep0req_name, req->buf, req->actual);
	if (req->status || req->actual != req->length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
		    req->status, req->actual, req->length);
	if (req->status == -ECONNRESET)	/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	if (req->status == 0 && req->context)
		((fsg_routine_t) (req->context)) (fsg);
}

/*-------------------------------------------------------------------------*/

/* Bulk and interrupt endpoint completion handlers.
 * These always run in_irq. */

static void bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev *fsg = ep->driver_data;
	struct fsg_buffhd *bh = req->context;

	if (req->status || req->actual != req->length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
		    req->status, req->actual, req->length);
	if (req->status == -ECONNRESET)	/* Request was cancelled */
		usb_ep_fifo_flush(ep);
	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock(&fsg->lock);
	bh->inreq_busy = 0;
	bh->state = BUF_STATE_EMPTY;
	wakeup_thread(fsg);
	spin_unlock(&fsg->lock);

	/* when adfu upgrade successful, disconnect and sync */
	if (unlikely(adfu_success_flag == 1)) {
#ifdef FPGA_VERIFY_MODE
		if(adfu_flush_nand_cache)
			adfu_flush_nand_cache();
#endif
		raise_exception(fsg, FSG_STATE_DISCONNECT);
		printk(KERN_INFO "UPGRADE SUCCESSFULLY\n");
//			if(need_restart == 0)
//			{
//				machine_restart("reboot");
//			}
	}

}

static void bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev *fsg = ep->driver_data;
	struct fsg_buffhd *bh = req->context;

	dump_msg(fsg, "bulk-out", req->buf, req->actual);
	if (req->status || req->actual != bh->bulk_out_intended_length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
		    req->status, req->actual, bh->bulk_out_intended_length);
	if (req->status == -ECONNRESET)	/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock(&fsg->lock);
	bh->outreq_busy = 0;
	bh->state = BUF_STATE_FULL;
	wakeup_thread(fsg);
	spin_unlock(&fsg->lock);
}

static void intr_in_complete(struct usb_ep *ep, struct usb_request *req)
{
}

/*-------------------------------------------------------------------------*/

/* Ep0 class-specific handlers.  These always run in_irq. */

static void received_cbi_adsc(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
}

static int class_setup_req(struct fsg_dev *fsg,
			   const struct usb_ctrlrequest *ctrl)
{
	struct usb_request *req = fsg->ep0req;
	int value = -EOPNOTSUPP;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);

	if (!fsg->config)
		return value;

	/* Handle Bulk-only class-specific requests */
	if (transport_is_bbb()) {
		switch (ctrl->bRequest) {

		case USB_BULK_RESET_REQUEST:
			/* printk(KERN_INFO "****USB_BULK_RESET_REQUEST****\n"); */
			if (ctrl->bRequestType != (USB_DIR_OUT |
						   USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE))
				break;
			if ((w_index != 0) || (w_value != 0)
			    || (w_length !=0)) {
				value = -EDOM;
				break;
			}

			/* Raise an exception to stop the current operation
			 * and reinitialize our state. */
			DBG(fsg, "bulk reset request\n");
			raise_exception(fsg, FSG_STATE_RESET);
			value = DELAYED_STATUS;
			break;

		case USB_BULK_GET_MAX_LUN_REQUEST:
			/* printk(KERN_INFO
			   "****USB_BULK_GET_MAX_LUN_REQUEST****\n"); */
			if (ctrl->bRequestType != (USB_DIR_IN |
						   USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE))
				break;
			if ((w_index != 0) || (w_value != 0)
			    || (w_length !=1)) {
				value = -EDOM;
				break;
			}
			VDBG(fsg, "get max LUN\n");
			*(u8 *) req->buf = fsg->nluns - 1;
			value = 1;
			break;
		default:
			break;
		}
	}

	/* Handle CBI class-specific requests */
	else {
		switch (ctrl->bRequest) {

		case USB_CBI_ADSC_REQUEST:
			if (ctrl->bRequestType != (USB_DIR_OUT |
						   USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE))
				break;
			if (w_index != 0 || w_value != 0) {
				value = -EDOM;
				break;
			}
			if (w_length > MAX_COMMAND_SIZE) {
				value = -EOVERFLOW;
				break;
			}
			value = w_length;
			fsg->ep0req->context = received_cbi_adsc;
			break;
		}
	}

	if (value == -EOPNOTSUPP)
		VDBG(fsg,
		     "unknown class-specific control req "
		     "%02x.%02x v%04x i%04x l%u\n",
		     ctrl->bRequestType, ctrl->bRequest,
		     le16_to_cpu(ctrl->wValue), w_index, w_length);
	return value;
}

/*-------------------------------------------------------------------------*/
static int bos_desc(struct fsg_dev *fsg)
{
	struct usb_ext_cap_descriptor	*usb_ext;
	struct usb_ss_cap_descriptor	*ss_cap;
	struct usb_dcd_config_params	dcd_config_params;
	struct usb_bos_descriptor	*bos = fsg->ep0req->buf;

	bos->bLength = USB_DT_BOS_SIZE;
	bos->bDescriptorType = USB_DT_BOS;

	bos->wTotalLength = cpu_to_le16(USB_DT_BOS_SIZE);
	bos->bNumDeviceCaps = 0;

	/*
	 * A SuperSpeed device shall include the USB2.0 extension descriptor
	 * and shall support LPM when operating in USB2.0 HS mode.
	 */
	usb_ext = fsg->ep0req->buf + le16_to_cpu(bos->wTotalLength);
	bos->bNumDeviceCaps++;
	le16_add_cpu(&bos->wTotalLength, USB_DT_USB_EXT_CAP_SIZE);
	usb_ext->bLength = USB_DT_USB_EXT_CAP_SIZE;
	usb_ext->bDescriptorType = USB_DT_DEVICE_CAPABILITY;
	usb_ext->bDevCapabilityType = USB_CAP_TYPE_EXT;
	usb_ext->bmAttributes = cpu_to_le32(USB_LPM_SUPPORT);

	/*
	 * The Superspeed USB Capability descriptor shall be implemented by all
	 * SuperSpeed devices.
	 */
	ss_cap = fsg->ep0req->buf + le16_to_cpu(bos->wTotalLength);
	bos->bNumDeviceCaps++;
	le16_add_cpu(&bos->wTotalLength, USB_DT_USB_SS_CAP_SIZE);
	ss_cap->bLength = USB_DT_USB_SS_CAP_SIZE;
	ss_cap->bDescriptorType = USB_DT_DEVICE_CAPABILITY;
	ss_cap->bDevCapabilityType = USB_SS_CAP_TYPE;
	ss_cap->bmAttributes = 0; /* LTM is not supported yet */
	ss_cap->wSpeedSupported = cpu_to_le16(USB_LOW_SPEED_OPERATION |
				USB_FULL_SPEED_OPERATION |
				USB_HIGH_SPEED_OPERATION |
				USB_5GBPS_OPERATION);
	ss_cap->bFunctionalitySupport = USB_LOW_SPEED_OPERATION;

	/* Get Controller configuration */
	if (fsg->gadget->ops->get_config_params)
		fsg->gadget->ops->get_config_params(&dcd_config_params);
	else {
		dcd_config_params.bU1devExitLat = USB_DEFAULT_U1_DEV_EXIT_LAT;
		dcd_config_params.bU2DevExitLat =
			cpu_to_le16(USB_DEFAULT_U2_DEV_EXIT_LAT);
	}
	ss_cap->bU1devExitLat = dcd_config_params.bU1devExitLat;
	ss_cap->bU2DevExitLat = dcd_config_params.bU2DevExitLat;

	return le16_to_cpu(bos->wTotalLength);
}
/* Ep0 standard request handlers.  These always run in_irq. */

static int standard_setup_req(struct fsg_dev *fsg,
			      const struct usb_ctrlrequest *ctrl)
{
	struct usb_request *req = fsg->ep0req;
	int value = -EOPNOTSUPP;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);
	/* Usually this just stores reply data in the pre-allocated ep0 buffer,
	 * but config change events will also reconfigure hardware. */
	switch (ctrl->bRequest) {

	case USB_REQ_GET_DESCRIPTOR:
		/* printk(KERN_INFO "****USB_REQ_GET_DESCRIPTOR_\n");*/
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_STANDARD |
					   USB_RECIP_DEVICE)) {
			printk("DING!!\n");
			break;
		}
		switch (w_value >> 8) {

		case USB_DT_DEVICE:
			/* printk(KERN_INFO "**** GET_DEVICE_DESC ****\n"); */
			VDBG(fsg, "get device descriptor\n");
			device_desc.bMaxPacketSize0 = fsg->ep0->maxpacket;

			if (gadget_is_superspeed(fsg->gadget)) {
				if (fsg->gadget->speed >= USB_SPEED_SUPER) {
					device_desc.bcdUSB = cpu_to_le16(0x0300);
					device_desc.bMaxPacketSize0 = 9;
				} else {
					//device_desc.bcdUSB = cpu_to_le16(0x0210);
				}
			}
			
			value = sizeof device_desc;
			memcpy(req->buf, &device_desc, value);
			break;
		case USB_DT_DEVICE_QUALIFIER:
			/* printk(KERN_INFO "QUALIFIER****\n"); */
			VDBG(fsg, "get device qualifier\n");
			if (!act_gadget_is_dualspeed(fsg->gadget) ||
			   fsg->gadget->speed >= USB_SPEED_SUPER)
				break;
			dev_qualifier.bMaxPacketSize0 = fsg->ep0->maxpacket;
			value = sizeof dev_qualifier;
			memcpy(req->buf, &dev_qualifier, value);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
			/* printk(KERN_INFO "OTHER_SPEED_CONFIG****\n"); */
			VDBG(fsg, "get other-speed config descriptor\n");
			if (!act_gadget_is_dualspeed(fsg->gadget) ||
			    fsg->gadget->speed >= USB_SPEED_SUPER)
				break;
			goto get_config;
		case USB_DT_CONFIG:
			/* printk(KERN_INFO "DT_CONFIG****\n"); */
			VDBG(fsg, "get configuration descriptor\n");
		get_config:
			value = populate_config_buf(fsg->gadget,
						    req->buf,
						    w_value >> 8,
						    w_value & 0xff);
			break;

		case USB_DT_STRING:
			/* printk(KERN_INFO "DT_STRING****\n"); */
			VDBG(fsg, "get string descriptor\n");

			/* wIndex == language code */
			value = usb_gadget_get_string(&stringtab,
						      w_value & 0xff, req->buf);
			break;
			
		case USB_DT_BOS:
			if (gadget_is_superspeed(fsg->gadget)) {
				value = bos_desc(fsg);
				value = min(w_length, (u16) value);
			}
			break;
			
		default:
			break;
		}
		break;

		/* One config, two speeds */
	case USB_REQ_SET_CONFIGURATION:
		/* printk(KERN_INFO "****USB_REQ_SET_CONFIGURATION****\n"); */
		if (ctrl->bRequestType != (USB_DIR_OUT | USB_TYPE_STANDARD |
					   USB_RECIP_DEVICE))
			break;
		VDBG(fsg, "set configuration\n");
		if (w_value == CONFIG_VALUE || w_value == 0) {
			/* printk(KERN_INFO
			   "The w_value is 0x%x.\n",w_value); */
			fsg->new_config = w_value;

			/* Raise an exception to wipe out previous transaction
			 * state (queued bufs, etc) and set the new config. */
			raise_exception(fsg, FSG_STATE_CONFIG_CHANGE);
			/* wakeup_thread(fsg);
			   wakeup_thread(fsg);
			   handle_exception(fsg); */
			/* wakeup_thread(fsg); */
			value = USB_GADGET_DELAYED_STATUS;
		}
		break;
	case USB_REQ_GET_CONFIGURATION:
		/* printk(KERN_INFO "****USB_REQ_GET_CONFIGUATION****\n"); */
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_STANDARD |
					   USB_RECIP_DEVICE))
			break;
		VDBG(fsg, "get configuration\n");
		*(u8 *) req->buf = fsg->config;
		value = 1;
		break;

	case USB_REQ_SET_INTERFACE:
		/* printk(KERN_INFO "****USB_REQ_SET_INTERFACE****\n"); */
		if (ctrl->bRequestType != (USB_DIR_OUT | USB_TYPE_STANDARD |
					   USB_RECIP_INTERFACE))
			break;
		if (fsg->config && w_index == 0) {

			/* Raise an exception to wipe out previous transaction
			 * state (queued bufs, etc) and install the new
			 * interface altsetting. */
			raise_exception(fsg, FSG_STATE_INTERFACE_CHANGE);
			value = DELAYED_STATUS;
		}
		break;
	case USB_REQ_GET_INTERFACE:
		/* printk(KERN_INFO "****USB_REQ_GET_INTERFACE****\n"); */
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_STANDARD |
					   USB_RECIP_INTERFACE))
			break;
		if (!fsg->config)
			break;
		if (w_index != 0) {
			value = -EDOM;
			break;
		}
		VDBG(fsg, "get interface\n");
		*(u8 *) req->buf = 0;
		value = 1;
		break;
	/*
	 * USB 3.0 additions:
	 * Function driver should handle get_status request. If such cb
	 * wasn't supplied we respond with default value = 0
	 * Note: function driver should supply such cb only for the first
	 * interface of the function
	 */
	case USB_REQ_GET_STATUS:
		if (!gadget_is_superspeed(fsg->gadget))
			break;
		if (ctrl->bRequestType != (USB_DIR_IN | USB_RECIP_INTERFACE))
			break;
		value = 2;	/* This is the length of the get_status reply */
		put_unaligned_le16(0, req->buf);
		break;
		
	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		if (!gadget_is_superspeed(fsg->gadget))
			break;
		if (ctrl->bRequestType != (USB_DIR_OUT | USB_RECIP_INTERFACE))
			break;
		switch (w_value) {
		case USB_INTRF_FUNC_SUSPEND:
			value = 0;
			break;
		}
		break;
	default:
		printk("unknown control req %02x.%02x v%04x i%04x l%u\n",
		       ctrl->bRequestType, ctrl->bRequest,
		       w_value, w_index, le16_to_cpu(ctrl->wLength));
		/* printk(KERN_INFO "*****UNKNOWN_CONTROL_REQ*****.\n"); */
	}
	return value;
}

static int fsg_setup(struct usb_gadget *gadget,
		     const struct usb_ctrlrequest *ctrl)
{
	struct fsg_dev *fsg = get_gadget_data(gadget);
	int rc;
	int w_length = le16_to_cpu(ctrl->wLength);

	++fsg->ep0_req_tag;	/* Record arrival of a new request */
	fsg->ep0req->context = NULL;
	fsg->ep0req->length = 0;
	dump_msg(fsg, "ep0-setup", (u8 *) ctrl, sizeof(*ctrl));

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS)
		rc = class_setup_req(fsg, ctrl);
	else
		rc = standard_setup_req(fsg, ctrl);

	/* Respond with data/status or defer until later? */
	if (rc >= 0 && rc != DELAYED_STATUS && rc != USB_GADGET_DELAYED_STATUS) {
		rc = min(rc, w_length);
		fsg->ep0req->length = rc;
		fsg->ep0req->zero = rc < w_length;
		fsg->ep0req_name = (ctrl->bRequestType & USB_DIR_IN ?
				    "ep0-in" : "ep0-out");
		rc = ep0_queue(fsg);
	}

	/* Device either stalls (rc < 0) or reports success */
	return rc;
}

/*-------------------------------------------------------------------------*/

/* All the following routines run in process context */

/* Use this for bulk or interrupt transfers, not ep0 */
static void start_transfer(struct fsg_dev *fsg, struct usb_ep *ep,
			   struct usb_request *req, int *pbusy,
			   enum fsg_buffer_state *state)
{
	int rc;
	unsigned long flag;

	if (ep == fsg->bulk_in)
		dump_msg(fsg, "bulk-in", req->buf, req->length);
	else if (ep == fsg->intr_in)
		dump_msg(fsg, "intr-in", req->buf, req->length);
	spin_lock_irqsave(&fsg->lock, flag);
	*pbusy = 1;
	*state = BUF_STATE_BUSY;
	spin_unlock_irqrestore(&fsg->lock, flag);
	rc = usb_ep_queue(ep, req, GFP_KERNEL);
	if (rc != 0) {
		printk(KERN_INFO "usb_ep_queue error !!!\n");
		*pbusy = 0;
		*state = BUF_STATE_EMPTY;

		/* We can't do much more than wait for a reset */

		/* Note: currently the net2280 driver fails zero-length
		 * submissions if DMA is enabled. */
		if (rc != -ESHUTDOWN && !(rc == -EOPNOTSUPP &&
					  req->length == 0))
			WARNING(fsg, "error in submission: %s --> %d\n",
				ep->name, rc);
	}

	return;
}

static int sleep_thread(struct fsg_dev *fsg)
{
	int rc = 0;

	/* Wait until a signal arrives or we are woken up */
	for (;;) {
		try_to_freeze();
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current)) {
			rc = -EINTR;
			break;
		}
		if (fsg->thread_wakeup_needed)
			break;
		schedule();
	}
	__set_current_state(TASK_RUNNING);
	fsg->thread_wakeup_needed = 0;
	return rc;
}

/*-------------------------------------------------------------------------*/
#ifdef FPGA_VERIFY_MODE
static int do_ADFU_rdrootfs(struct fsg_dev *fsg)
{
	return 0;
}
#else
static int do_ADFU_rdrootfs(struct fsg_dev *fsg)
{
	struct lun *curlun = fsg->curlun;
	dev_t media_dev_t;
	struct block_device *bdev_back;
	struct gendisk *disk;

	u32 lba;
	struct fsg_buffhd *bh;
	int rc;
	u32 amount_left;
	loff_t file_offset, file_offset_tmp;
	struct uparam rd_param;
	unsigned int amount;
	ssize_t nread;

	/* Get the starting Logical Block Address and check that it's
	 * not too big */

	lba = get_le32(&fsg->cmnd[5]);
#if 0
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}
#endif
	rd_param.flash_partition = fsg->cmnd[3];
	rd_param.devnum_in_phypart = fsg->cmnd[4];

	/* Carry out the file reads */
	file_offset = lba;
	amount_left = fsg->data_size_from_cmnd;

	if (unlikely(amount_left == 0))
	{
		//printk("return:%d\n",__LINE__);
		return -EIO;	/* No default reply */
	}

	for (;;) {

		/* Figure out how much we need to read:
		 * Try to read the remaining amount.
		 * But don't read more than the buffer size.
		 * And don't try to read past the end of the file.
		 * Finally, if we're not at a page boundary, don't read past
		 *      the next page.
		 * If this means reading 0 then we were asked to read past
		 *      the end of file. */
		amount = min((unsigned int)amount_left, mod_data.buflen);
		/*
		  amount = min((loff_t) amount,
		  curlun->file_length - file_offset);
		  partial_page = file_offset & (PAGE_CACHE_SIZE - 1);
		  if (partial_page > 0)
		  amount = min(amount, (unsigned int)PAGE_CACHE_SIZE -
		  partial_page);
		*/
		/* Wait for the next buffer to become available */
		bh = fsg->next_buffhd_to_fill;
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
			{
				//printk("return:%d\n",__LINE__);
				return rc;
			}
		}

		/* If we were asked to read past the end of file,
		 * end with an empty buffer. */
		/*
		  if (amount == 0) {
		  curlun->sense_data =
		  SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		  curlun->sense_data_info = file_offset;
		  curlun->info_valid = 1;
		  bh->inreq->length = 0;
		  bh->state = BUF_STATE_FULL;
		  break;
		  }
		*/
		/* Perform the read */
		amount = amount << 9; /* unit by sector */
		file_offset_tmp = file_offset;
		media_dev_t = curlun->devnum;
		bdev_back = bdget(media_dev_t);
		disk = bdev_back->bd_disk;

		//nread = disk->fops->adfu_read(file_offset, amount,
		//		bh->buf, &rd_param);
		nread = vfs_read(curlun->filp,
				 (char __user *)bh->buf,
				 amount, &file_offset_tmp);

#if 0
		nread = vfs_read(curlun->filp,
				 (char __user *)bh->buf,
				 amount, &file_offset_tmp);
#endif
		VLDBG(curlun, "file read %u @ %llu -> %d\n", amount,
		      (unsigned long long)file_offset, (int)nread);
		if (signal_pending(current)) {
			bdput(bdev_back);
			//printk("return:%d\n",__LINE__);
			return -EINTR;
		}
		if (nread < 0) {
			LDBG(curlun, "error in file read: %d\n", (int)nread);
			nread = 0;
		} else if (nread < amount) {
			LDBG(curlun, "partial file read: %d/%u\n",
			     (int)nread, amount);
		}
		file_offset += nread;
		amount_left -= (((unsigned int)nread) << 9);
		fsg->residue -= (((unsigned int)nread) << 9);
		bh->inreq->length = (((unsigned int)nread) << 9);
		bh->state = BUF_STATE_FULL;

		/* If an error occurred, report it and its position */
		if (nread < amount) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info = file_offset;
			curlun->info_valid = 1;
			break;
		}

		if (amount_left == 0)
			break;	/* No more left to read */

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
			       &bh->inreq_busy, &bh->state);
		fsg->next_buffhd_to_fill = bh->next;
	}
	no_finish_reply = 0;	/* the last buf has not been transfered,
				   move it into finish_reply() */
	bdput(bdev_back);
	//printk("return:%d\n",__LINE__);
	return -EIO;		/* No default reply */
}
#endif
static int get_udisk_size(struct lun *curlun, const char *filename)
{
	int ro;
	struct file *filp = NULL;
	int rc = -EINVAL;
	struct inode *inode = NULL;
	loff_t size;
	loff_t num_sectors;

	/* R/W if we can, R/O if we must */
	ro = curlun->ro;
	if (!ro) {
		filp = filp_open(filename, O_RDWR | O_LARGEFILE, 0);
		if (-EROFS == PTR_ERR(filp))
			ro = 1;
	}
	if (ro)
		filp = filp_open(filename, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(filp)) {
		LINFO(curlun, "unable to open backing file: %s\n", filename);
		return PTR_ERR(filp);
	}

	if (!(filp->f_mode & FMODE_WRITE))
		ro = 1;

	if (filp->f_path.dentry)
		inode = filp->f_path.dentry->d_inode;
	if (inode && S_ISBLK(inode->i_mode)) {
		if (bdev_read_only(inode->i_bdev))
			ro = 1;
	} else if (!inode || !S_ISREG(inode->i_mode)) {
		LINFO(curlun, "invalid file type: %s\n", filename);
		goto out;
	}

	/* If we can't read the file, it's no good.
	 * If we can't write the file, use it read-only. */
	if (!filp->f_op || !(filp->f_op->read || filp->f_op->aio_read)) {
		LINFO(curlun, "file not readable: %s\n", filename);
		goto out;
	}
	if (!(filp->f_op->write || filp->f_op->aio_write))
		ro = 1;

	size = i_size_read(inode->i_mapping->host);
	if (size < 0) {
		LINFO(curlun, "unable to find file size: %s\n", filename);
		rc = (int)size;
		goto out;
	}
	num_sectors = size >> 9;	/* File size in 512-byte sectors */
	if (num_sectors == 0) {
		LINFO(curlun, "file too small: %s\n", filename);
		rc = -ETOOSMALL;
		goto out;
	}

	get_file(filp);

	curlun->devnum = filp->f_path.dentry->d_inode->i_rdev;

	curlun->ro = ro;
	curlun->filp = filp;
	curlun->file_length = size;
	curlun->num_sectors = num_sectors;
	LDBG(curlun, "open backing file: %s\n", filename);
	rc = 0;

out:
	filp_close(filp, current->files);
	return rc;
}
static int do_ADFU_check_partition(struct fsg_dev *fsg)
{
	struct fsg_buffhd *bh;
	int rc;
	u32 amount_left;
	unsigned int amount;
	ssize_t nread;
	unsigned long tmp;
       struct lun *curlun = fsg->curlun;
	int rc2 = -EINVAL;
	int size = 1;

	if(cmnd_sequence_first_flag)	//wait insmod flash
	{
		set_probatch_phase(PROBATCH_INSTAL_FLASH);
		wait_adfus_proc(PROBATCH_FINISH_INSTALL_FLASH);//sync with upgrade_app
		cmnd_sequence_first_flag = 0;

	    rc2=get_udisk_size(curlun, "/dev/actk");
		if(rc2 != 0)
		{
			VLDBG("open /dev/actk error:%d\n",__LINE__);
			/*return rc2;*/
		}
		size = (int)curlun->num_sectors;
		printk("##udisk size:%d    0x%x\n",size,size);

	}	
		
	amount_left = fsg->data_size_from_cmnd;

	if (unlikely(amount_left == 0))
	{
		return -EIO;	/* No default reply */
	}

	for (;;) {
		amount = (unsigned int)amount_left;
		/* Wait for the next buffer to become available */
		bh = fsg->next_buffhd_to_fill;
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
			{
				return rc;
			}
		}
		printk("%s %d: bh %p, bh->buf %p\n", __FUNCTION__, __LINE__,
		       bh, bh->buf);

		/* Perform the read */
		memset((char *)bh->buf, 0, amount);

		//printk("%s %d\n", __FUNCTION__, __LINE__);

		/*
		 * return results, there may be
		 * partition error, or hdcp key error
		 */
		if(check_partition_flag < 0)
		{
			tmp = (unsigned long)bh->buf;
			writel(-check_partition_flag, tmp);
		}

		//printk("%s %d\n", __FUNCTION__, __LINE__);


		//add partition cap info
#ifdef FPGA_VERIFY_MODE
		memcpy((unsigned long)bh->buf + 0x10, (unsigned long *)&size, sizeof(unsigned int));
#endif
		nread = amount;

		amount_left -= (unsigned int)nread;
		fsg->residue -= (unsigned int)nread;
		bh->inreq->length = (unsigned int)nread;
		bh->state = BUF_STATE_FULL;

		if (amount_left == 0)
			break;	/* No more left to read */

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
			       &bh->inreq_busy, &bh->state);
		fsg->next_buffhd_to_fill = bh->next;
	}
	no_finish_reply = 0;	/* the last buf has not been transfered,
				   move it into finish_reply() */	   

	return -EIO;		/* No default reply */
}

#define FLASH_READ_BUF_SECTORS (32)
#define FLASH_READ_BUF_SIZE (FLASH_READ_BUF_SECTORS*512)
unsigned int image_sectors, check_image_sectors;
struct uparam read_param;
#ifdef FPGA_VERIFY_MODE
static int do_ADFU_check_image_checksum(struct fsg_dev *fsg)
{
	return 0;
}
#else


static int do_ADFU_check_image_checksum(struct fsg_dev *fsg)
{
	struct fsg_buffhd *bh;
	int rc;
	u32 amount_left;
	unsigned int amount;
	ssize_t nread;
	
	int read_err=0;
	unsigned int tmp;
	unsigned int file_offset=0,read_sectors=0;

	//Don't check phy_boot_partition 			
	if(read_param.flash_partition == 0)
	{
		goto finish_check;	
	}
	
	read_param.flash_partition -= 1;
	read_param.devnum_in_phypart -= 1;
	
	void *buf = kmalloc(FLASH_READ_BUF_SIZE, GFP_KERNEL);
	
	for(;;)
	{
		if(check_image_sectors == 0)
		{
			break;
		}		
		if(check_image_sectors>FLASH_READ_BUF_SECTORS)
		{
			read_sectors = FLASH_READ_BUF_SECTORS;
		}
		else
		{
			read_sectors = check_image_sectors;
		}

//		printk("nand_read:0x%x,0x%x,%d,%d\n", file_offset, read_sectors, read_param.flash_partition, read_param.devnum_in_phypart);
#ifdef FPGA_VERIFY_MODE
#else
		read_err = adfus_nand_read(file_offset, read_sectors,
					   buf, &read_param);
#endif
		if(read_err < 0)
		{
			break;
		}					
		file_offset += read_sectors;					
		check_image_sectors -= read_sectors;		
	}
	
	kfree(buf);
	buf=NULL;

finish_check:
						
	amount_left = fsg->data_size_from_cmnd;

	if (unlikely(amount_left == 0))
	{
		return -EIO;	/* No default reply */
	}

	for (;;) {
		amount = (unsigned int)amount_left;
		/* Wait for the next buffer to become available */
		bh = fsg->next_buffhd_to_fill;
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
			{
				return rc;
			}
		}
		/* Perform the read */
		memset((char *)bh->buf, 0, amount);
		if(read_err < 0)
		{
			tmp = (unsigned int)bh->buf;
			writel(1, tmp);			
		}
		nread = amount;

		amount_left -= (unsigned int)nread;
		fsg->residue -= (unsigned int)nread;
		bh->inreq->length = (unsigned int)nread;
		bh->state = BUF_STATE_FULL;

		if (amount_left == 0)
			break;	/* No more left to read */

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
			       &bh->inreq_busy, &bh->state);
		fsg->next_buffhd_to_fill = bh->next;
	}
	no_finish_reply = 0;	/* the last buf has not been transfered,
				   move it into finish_reply() */
	return -EIO;		/* No default reply */
}
#endif
int do_ADFU_INFO(struct fsg_dev *fsg)
{
	int ret=0;
	struct timeval tv1,tv2;
	
	switch(fsg->cmnd[2])
	{
	case 0x20:
		printk("do_ADFU_check_partition begin\n");
		ret = do_ADFU_check_partition(fsg);
		printk("do_ADFU_check_partition end\n");
		break;
	case 0x21:
		do_gettimeofday(&tv1);
		ret = do_ADFU_check_image_checksum(fsg);
		do_gettimeofday(&tv2);
		printk("do_ADFU_check_image_checksum,timing:%ds\n", tv2.tv_sec-tv1.tv_sec);
		break;
	default:
		break;
	}
	return ret;
}

#ifdef FPGA_VERIFY_MODE

#else
extern int asoc_get_board_opt(void);
static int modify_afi(afinfo_t *p_afinfo)
{
	unsigned char *afinfo_buf = p_afinfo;
	board_opt_t *board_opt;
	int i, cur_board_opt;
	unsigned char offset, value;

	cur_board_opt = asoc_get_board_opt();
	printk("%s(): cur_board_opt %d\n", __FUNCTION__, cur_board_opt);

	if (!has_board_opt(p_afinfo) || cur_board_opt == 0)
	{
		printk("%s(): use default board opt 0\n", __FUNCTION__);
		return 0;
	}

	if (cur_board_opt < 0 || cur_board_opt >= BOARD_OPT_MAX_CNT)
	{
		printk("%s(): invalid board opt %d\n", __FUNCTION__, cur_board_opt);
		return -1;
	}

	board_opt = &p_afinfo->board_opt;
	memcpy(&p_afinfo->ddr_param,
	       &board_opt->ddr_param[cur_board_opt - 1],
	       sizeof(ddr_param_t));

	/* update the board option in afinfo */
	p_afinfo->board_opt.cur_opt = cur_board_opt;

	return 0;
}
#endif


int write_ram_bin(u32 addr, char *buf, u32 len)
{
	int ret=0;
	char val;
	char ram_bin_name[64];
	static struct file *ram_bin_filp;
	loff_t file_offset_byte;
	
	memset(ram_bin_name, 0, 64);
	strcpy(ram_bin_name, "/usr/");
	
	if(addr == 0x21340000)
	{
		strcat(ram_bin_name, "oem.bin");
	}
	else if(addr == 0x21340001)
	{
		strcat(ram_bin_name, "oem.ko");
	}
	else if(addr == 0x32140000)
	{
		strcat(ram_bin_name, "mbr_info.bin");
#ifdef FPGA_VERIFY_MODE
		mbr_info_buf = kmalloc(sizeof(mbr_info_t), GFP_KERNEL);
		memcpy(mbr_info_buf, buf, len);
	//	memcpy(mbr_info_buf, buf, len);
		need_format = readb((volatile void *)buf + 0x04);

		printk("need_format:%d\n", need_format);
		printk("need_restart:%d\n", need_restart);
#endif		
	}
	else if(addr == 0x32140001)
	{
		strcat(ram_bin_name, "startup.bin");

	}	
	else if(addr == 0x32140002)
	{
		strcat(ram_bin_name, "shutoff.bin");
	}				
	else if(addr == 0x1e000000)
	{
		strcat(ram_bin_name, "adfudec.bin");
	}		
	else
	{
#ifdef FPGA_VERIFY_MODE
		strcat(ram_bin_name, "afinfo.bin");
		need_format = readb(buf + 0x10);
		need_restart = readb(buf + 0x11);
		printk("need_restart:%d\n", need_restart);

		printk("need_format:%d\n", need_format);
#else
		static const unsigned int ddr_cap[] =
			{32, 64, 128, 256, 512, 1024, 2048, 0, 0, 0, 0, 0, 0, 0, 0, 0};
     /*
		if (asoc_get_board_opt() != 0)
		{
			modify_afi(buf);
		}
    */
		strcat(ram_bin_name, "afinfo.bin");
		need_format = readb((unsigned int)buf + 0x10);
		need_restart = readb((unsigned int)buf + 0x11);
	
		printk("need_format:%d\n", need_format);			
		printk("need_restart:%d\n", need_restart);
				
		
#endif		
	}
	printk("addr:0x%x, ram_bin_name:%s\n", addr, ram_bin_name);
			
	if(!ram_bin_filp)
	{
		ram_bin_filp = filp_open(ram_bin_name, O_WRONLY|O_CREAT, 0700);
		if(!ram_bin_filp)
		{
			printk("fail to creat %s,errno:%p\n", ram_bin_name, ram_bin_filp);
			ret = -1;
			goto out;
		}
		else
		{
			printk("creat %s,filp:%p\n", ram_bin_name, ram_bin_filp);
		}
	}

	file_offset_byte = 0;
	ret = vfs_write(ram_bin_filp, buf, len, &file_offset_byte);
	filp_close(ram_bin_filp, current->files);
	ram_bin_filp = NULL;
	
out:	
	return ret;	
}


static int do_ADFU_access_iram(struct fsg_dev *fsg)
{
	struct lun *curlun = fsg->curlun;
	struct fsg_buffhd *bh;
	int get_some_more;
	u32 amount_left_to_req, amount_left_to_write,download_addr;

	unsigned int amount;
	ssize_t nwritten;
	int rc;

	if (curlun->ro) {
		curlun->sense_data = SS_WRITE_PROTECTED;
		printk("return:%d\n",__LINE__);
		return -EINVAL;
	}

	download_addr = get_le32(&fsg->cmnd[9]);
	/* Carry out the file writes */
	get_some_more = 1;
	amount_left_to_req = amount_left_to_write = fsg->data_size_from_cmnd;

	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = fsg->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && get_some_more) {

			/* Figure out how much we want to get:
			 * Try to get the remaining amount.
			 * But don't get more than the buffer size.
			 * And don't try to go past the end of the file.
			 * If we're not at a page boundary,
			 *      don't go past the next page.
			 * If this means getting 0, then we were asked
			 *      to write past the end of file.
			 * Finally, round down to a block boundary. */
			amount = min(amount_left_to_req, mod_data.buflen);
			

			/* Get the next buffer */
			fsg->usb_amount_left -= amount;
			amount_left_to_req -= amount;
			if (amount_left_to_req == 0)
				get_some_more = 0;

			/* Except at the end of the transfer, amount will be
			 * equal to the buffer size, which is divisible by
			 * the bulk-out maxpacket size.
			 */
			set_bulk_out_req_length(fsg, bh, amount);
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
				       &bh->outreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = fsg->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
		{
			VLDBG("break:%d,bh->state:%d, BUF_STATE_EMPTY:%d\n",__LINE__, bh->state, BUF_STATE_EMPTY);
			break;	/* We stopped early */
		}

		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			fsg->next_buffhd_to_drain = bh->next;
			bh->state = BUF_STATE_EMPTY;

			/* Did something go wrong with the transfer? */
			if (bh->outreq->status != 0) {
				curlun->sense_data = SS_COMMUNICATION_FAILURE;
				curlun->info_valid = 1;
				VLDBG("break:%d,bh->outreq->status:%d\n",__LINE__, bh->outreq->status);
				break;
			}

			amount = bh->outreq->actual; /* unit by sector */
			
			/* Don't accept excess data.  The spec doesn't say
			 * what to do in this case.  We'll ignore the error.
			 */
			amount = min(amount, bh->bulk_out_intended_length);
			//amount = round_down(amount, curlun->blksize);
			/* Perform the write */		
			nwritten = write_ram_bin(download_addr, bh->buf, amount);
			if(nwritten >= 0)
			{
				nwritten = amount;
			}

			if (signal_pending(current)) {
				printk("return:%d\n",__LINE__);
				return -EINTR;	/* Interrupted! */
			}
			if (nwritten < 0) {
				LDBG(curlun, "error in file write: %d\n",
				     (int)nwritten);
				nwritten = 0;
			} else if (nwritten < amount) {
				LDBG(curlun, "partial file write: %d/%u\n",
				     (int)nwritten, amount);
				/* Round down to a block */
			}
			amount_left_to_write -= nwritten;
			fsg->residue -= nwritten;

			/* If an error occurred, report it and its position */
			if (nwritten < amount) {
				curlun->sense_data = SS_WRITE_ERROR;
				curlun->info_valid = 1;
				VLDBG("break:%d\n",__LINE__);
				break;
			}

			/* Did the host decide to stop early? */
			if (bh->outreq->actual != bh->outreq->length) {
				fsg->short_packet_received = 1;
				VLDBG("break:%d\n",__LINE__);
				break;
			}
			continue;
		}
		else
		{
			VLDBG("break:%d,bh->state:%d\n",__LINE__, bh->state);
		}

		/* Wait for something to happen */
		rc = sleep_thread(fsg);
		if (rc) {
			printk("return:%d\n",__LINE__);
			return rc;
		}
	}

	no_finish_reply = 1;
	printk("return:%d\n",__LINE__);
	return -EIO;		/* No default reply */
}

char write_file_name[32];
struct file *write_file_fp = NULL;
loff_t pos = 0;
#if 0
static int do_ADFU_wtrootfs(struct fsg_dev *fsg)
{}
#else
static int do_ADFU_wtrootfs(struct fsg_dev *fsg)
{
	struct lun *curlun = fsg->curlun;
	u32 lba;
	struct fsg_buffhd *bh;
	int get_some_more;
	u32 amount_left_to_req, amount_left_to_write;
	loff_t usb_offset, file_offset, file_offset_tmp;
	struct uparam wt_param;
	unsigned int amount;
	ssize_t nwritten;
	int rc;
	char op_type;
	char fs_len;
	int i,error;

	//printk("%s %d\n", __FUNCTION__, __LINE__);

	if (curlun->ro) {
		curlun->sense_data = SS_WRITE_PROTECTED;
		printk("return:%d\n",__LINE__);
		return -EINVAL;
	}

	//printk("%s %d\n", __FUNCTION__, __LINE__);

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	lba = get_le32(&fsg->cmnd[9]);
	wt_param.flash_partition = fsg->cmnd[2] & ~0x80;
	wt_param.devnum_in_phypart = fsg->cmnd[2] & ~0x80;
	
	//printk("%s %d: flash_partition %d, devnum_in_phypart %d\n", __FUNCTION__, __LINE__,
	//       wt_param.flash_partition, wt_param.devnum_in_phypart);

#if 0
	if(wt_param.flash_partition == 0x0) {
		printk("%s %d\n", __FUNCTION__, __LINE__);

		if (!write_file_fp) {
			printk("%s %d\n", __FUNCTION__, __LINE__);

			/*unit of probatch tool download img is 10MB*/
			printk("write boot.bin\n");	
			strcpy(write_file_name, "/tmp/mbrc");			
			write_file_fp = filp_open(write_file_name, O_RDWR | O_CREAT, 0644);
			printk("%s:0x%08x\n", write_file_name, write_file_fp);
			update_phy_boot = 1;			
		}
	}

	if(wt_param.flash_partition != UDISK_ACCESS) {
		wt_param.flash_partition -= 1;
		wt_param.devnum_in_phypart -= 1;
	}
#endif
	
	if(wt_param.flash_partition == 0x0 || wt_param.flash_partition == 0x1)
	{
		if (!write_file_fp)
		{
			//unit of probatch tool download img is 10MB
			if (wt_param.flash_partition == 0x0 ) {
				printk("write mbrec.bin\n");	
				strcpy(write_file_name, "/tmp/mbrec.bin");
			} else {
				printk("write uboot.bin\n");	
				strcpy(write_file_name, "/tmp/uboot.bin");			
			}
			write_file_fp = filp_open(write_file_name, O_RDWR | O_CREAT, 0644);
			printk("boot:%s\n", write_file_name);
			update_phy_boot = 1;
		}

	} else  {
		//if(wt_param.flash_partition != UDISK_ACCESS)
		//{
		   wt_param.flash_partition -= 2;
		   wt_param.devnum_in_phypart -= 2;
		//}
		printk("flash_partition:%d, devnum_in_phypart:%d\n", wt_param.flash_partition, wt_param.devnum_in_phypart);
		sprintf(format_disk_name, "/dev/act%c", 'a'+wt_param.flash_partition);
		printk("format_disk_name = %s\n", format_disk_name);
	}


#if 0
	printk("flash_partition:%d, devnum_in_phypart:%d\n", wt_param.flash_partition, wt_param.devnum_in_phypart);

	sprintf(format_disk_name, "/dev/act%c", 'a'+wt_param.flash_partition);
	printk("format_disk_name = %s\n", format_disk_name);
#endif

	op_type = fsg->cmnd[3];
	if(op_type == 1) {
		printk("%s %d\n", __FUNCTION__, __LINE__);

		fs_len = fsg->cmnd[4];
		memcpy(format_fs_name, &(fsg->cmnd[5]), fs_len);
		format_fs_name[fs_len]=0;
		for(i=0; i<fs_len; i++) {
			format_fs_name[i] = tolower(format_fs_name[i]);
		}
// FIXME: where is the label?
//		format_disk_label = disk_label[wt_param.flash_partition-3];
		format_disk_label = "label";
		printk("format_fs_name:%s, format_disk_name:%s, format_disk_label:%s\n", format_fs_name, format_disk_name, format_disk_label);
		
		set_probatch_phase(PROBATCH_FORMAT);
		wait_adfus_proc(PROBATCH_FINISH_FORMAT);//sync with upgrade_app		
		goto out;
	}
	
	/* Carry out the file writes */
	get_some_more = 1;
	usb_offset = ((loff_t) lba) << 9;
	file_offset = lba;  /* unit by sector */
	amount_left_to_req = amount_left_to_write = fsg->data_size_from_cmnd;
	
	/*changed by liyong, to add a file operation to write flash*/
	if(!write_file_fp) { 
		write_file_fp = filp_open(format_disk_name, O_RDWR, 0644);
		error = PTR_ERR(write_file_fp);
		if(IS_ERR(write_file_fp)) {
				printk("open %s error ,error = %d\n", format_disk_name, error);	
				return -1;	
		}
	}
	
	if(amount_left_to_write == 0) {
		read_param.flash_partition = fsg->cmnd[2] & ~0x80;
		read_param.devnum_in_phypart = fsg->cmnd[2] & ~0x80;
		printk("image_sectors:0x%x,%d,%d\n", image_sectors, read_param.flash_partition, read_param.devnum_in_phypart);	
		check_image_sectors = image_sectors;
		image_sectors = 0;
		if(write_file_fp) {
			filp_close(write_file_fp, current->files);
			write_file_fp = NULL;
			pos = 0;
			write_file_name[0] = 0;
		}		
		goto out;
	}
	image_sectors += amount_left_to_write>>9;	
	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = fsg->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && get_some_more) {

			/* Figure out how much we want to get:
			 * Try to get the remaining amount.
			 * But don't get more than the buffer size.
			 * And don't try to go past the end of the file.
			 * If we're not at a page boundary,
			 *      don't go past the next page.
			 * If this means getting 0, then we were asked
			 *      to write past the end of file.
			 * Finally, round down to a block boundary. */
			amount = min(amount_left_to_req, mod_data.buflen);
			/*
			 * amount = min((loff_t) amount, curlun->file_length -
			 * usb_offset);
			*/

			/* move the overflow judge into flash driver? */

			/*
			 * partial_page = usb_offset & (PAGE_CACHE_SIZE - 1);
			 * if (partial_page > 0)
			 * amount = min(amount,
			 * (unsigned int)PAGE_CACHE_SIZE -
			 * partial_page);
			 * if (amount == 0) {
			 * get_some_more = 0;
			 * curlun->sense_data =
			 * SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			 * curlun->sense_data_info = usb_offset >> 9;
			 * curlun->info_valid = 1;
			 * continue;
			 * }
			*/


			/* Get the next buffer */
			usb_offset += amount;  /* for future use(test the boundary) */
			fsg->usb_amount_left -= amount;
			amount_left_to_req -= amount;
			if (amount_left_to_req == 0)
				get_some_more = 0;

			/* Except at the end of the transfer, amount will be
			 * equal to the buffer size, which is divisible by
			 * the bulk-out maxpacket size.
			 */
			set_bulk_out_req_length(fsg, bh, amount);
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
				       &bh->outreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;

			continue;
		}

		/* Write the received data to the backing file */
		bh = fsg->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more) {
			VLDBG("break:%d,bh->state:%d, BUF_STATE_EMPTY:%d\n",__LINE__, bh->state, BUF_STATE_EMPTY);
			break;	/* We stopped early */
		}

		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			fsg->next_buffhd_to_drain = bh->next;
			bh->state = BUF_STATE_EMPTY;

			/* Did something go wrong with the transfer? */
			if (bh->outreq->status != 0) {
				curlun->sense_data = SS_COMMUNICATION_FAILURE;
				curlun->sense_data_info = file_offset;
				curlun->info_valid = 1;
				VLDBG("break:%d,bh->outreq->status:%d\n",__LINE__, bh->outreq->status);
				break;
			}

			amount = bh->outreq->actual; /* unit by sector */
			
			/* Don't accept excess data.  The spec doesn't say
			 * what to do in this case.  We'll ignore the error.
			 */
			amount = min(amount, bh->bulk_out_intended_length);
			//amount = round_down(amount, curlun->blksize);

			/* Perform the write */
			file_offset_tmp = file_offset;

			VLDBG("Perform the write,wt_param.devnum_in_phypart:0x%x\n", wt_param.devnum_in_phypart);		
//			if(wt_param.devnum_in_phypart == 0x1)
			{
				if(!write_file_fp) {
//#ifdef FPGA_VERIFY_MODE
//nwritten = amount >> 9;
//#else

					nwritten = adfus_nand_write(file_offset, amount >> 9,
								    bh->buf, &wt_param);
//#endif
					nwritten = nwritten << 9;	
					VLDBG("amount:0x%x,nwritten:0x%x,file_offset:0x%x\n", amount, nwritten, (unsigned long)file_offset);					
				}
				else {
					//printk("amount:0x%x,nwritten:0x%x,pos:0x%x\n", amount, nwritten, (unsigned long)pos);
					/* reinitialize offset for random write */
					pos = file_offset << 9;
					nwritten = write_file_fp->f_op->write(write_file_fp, bh->buf, amount, &pos);
					VLDBG("write file: amount:0x%x,nwritten:0x%x,file_offset:0x%x\n", amount, nwritten, file_offset << 9);
				}	
			}
			
			if (signal_pending(current)) {
				printk("return:%d\n",__LINE__);
				return -EINTR;	/* Interrupted! */
			}
			if (nwritten < 0) {
				printk("adfus.error in file write: %d.file_offset:%d, amount:%d\n",
				       (int)nwritten, file_offset, amount);
				nwritten = 0;
			} else if (nwritten < amount) {
				LDBG(curlun, "partial file write: %d/%u\n",
				     (int)nwritten, amount);
				/* Round down to a block */
			}
			file_offset += nwritten >> 9;
			amount_left_to_write -= nwritten;
			fsg->residue -= nwritten;

			/* If an error occurred, report it and its position */
			if (nwritten < amount) {
				curlun->sense_data = SS_WRITE_ERROR;
				curlun->sense_data_info = file_offset;
				curlun->info_valid = 1;
				VLDBG("break:%d\n",__LINE__);
				break;
			}

			/* Did the host decide to stop early? */
			if (bh->outreq->actual != bh->outreq->length) {
				fsg->short_packet_received = 1;
				VLDBG("break:%d\n",__LINE__);
				break;
			}
			continue;
		}
		else
		{
			VLDBG("break:%d,bh->state:%d\n",__LINE__, bh->state);
		}

		/* Wait for something to happen */
		rc = sleep_thread(fsg);
		if (rc) {
			VLDBG("return:%d\n",__LINE__);
			return rc;
		}
	}

out:
	no_finish_reply = 1;
	VLDBG("return:%d\n",__LINE__);
	return -EIO;		/* No default reply */
}
#endif

/* Sync the file data, don't bother with the metadata.
 * This code was copied from fs/buffer.c:sys_fdatasync(). */
static int fsync_sub(struct lun *curlun)
{
#if 0
	if (curlun->disk_type == NAND_MEDIUM)
	{
		dev_t media_dev_t;
		struct gendisk *disk;
		struct block_device *bdev_back;
		media_dev_t = curlun->devnum;
		bdev_back = bdget(media_dev_t);
		disk = bdev_back->bd_disk;
			
		if (disk == NULL)
		{ 
			bdput(bdev_back);
			return 0;
		}
		if (disk->fops->flush_disk_cache != NULL) {
			disk->fops->flush_disk_cache();
		}
		bdput(bdev_back);
	}
	return 0;	//when we dont use VFS, we neednt sync
#else
	struct file	*filp = curlun->filp;

	if (curlun->ro || !filp)
		return 0;
	return vfs_fsync(filp, 1);

#endif
}

static void fsync_all(struct fsg_dev *fsg)
{
	int i;

	for (i = 0; i < fsg->nluns; ++i)
		fsync_sub(&fsg->luns[i]);
}

/*-------------------------------------------------------------------------*/

static int halt_bulk_in_endpoint(struct fsg_dev *fsg)
{
	int rc;
	rc = fsg_set_halt(fsg, fsg->bulk_in);

	if (rc == -EAGAIN)
		VDBG(fsg, "delayed bulk-in endpoint halt\n");
	while (rc != 0) {
		if (rc != -EAGAIN) {
			WARNING(fsg, "usb_ep_set_halt -> %d\n", rc);
			rc = 0;
			break;
		}

		/* Wait for a short time and then try again */
		if (msleep_interruptible(100) != 0)
			return -EINTR;
		rc = usb_ep_set_halt(fsg->bulk_in);
	}
	return rc;
}

static int halt_bulk_out_endpoint(struct fsg_dev *fsg)
{
	int rc;
	rc = fsg_set_halt(fsg, fsg->bulk_out);

	if (rc == -EAGAIN)
		VDBG(fsg, "delayed bulk-out endpoint halt\n");
	while (rc != 0) {
		if (rc != -EAGAIN) {
			WARNING(fsg, "usb_ep_set_halt -> %d\n", rc);
			rc = 0;
			break;
		}

		/* Wait for a short time and then try again */
		if (msleep_interruptible(100) != 0)
			return -EINTR;
		rc = usb_ep_set_halt(fsg->bulk_out);
	}
	return rc;
}

static int wedge_bulk_in_endpoint(struct fsg_dev *fsg)
{
	int rc;

	DBG(fsg, "bulk-in set wedge\n");
	rc = usb_ep_set_wedge(fsg->bulk_in);
	if (rc == -EAGAIN)
		VDBG(fsg, "delayed bulk-in endpoint wedge\n");
	while (rc != 0) {
		if (rc != -EAGAIN) {
			WARNING(fsg, "usb_ep_set_wedge -> %d\n", rc);
			rc = 0;
			break;
		}

		/* Wait for a short time and then try again */
		if (msleep_interruptible(100) != 0)
			return -EINTR;
		rc = usb_ep_set_wedge(fsg->bulk_in);
	}
	return rc;
}

static int pad_with_zeros(struct fsg_dev *fsg)
{
	struct fsg_buffhd *bh = fsg->next_buffhd_to_fill;
	u32 nkeep = bh->inreq->length;
	u32 nsend;
	int rc;
	bh->state = BUF_STATE_EMPTY;	/* For the first iteration */
	fsg->usb_amount_left = nkeep + fsg->residue;
	while (fsg->usb_amount_left > 0) {

		/* Wait for the next buffer to be free */
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;

		}

		nsend = min(fsg->usb_amount_left, (u32) mod_data.buflen);
		memset(bh->buf + nkeep, 0, nsend - nkeep);
		bh->inreq->length = nsend;
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
			       &bh->inreq_busy, &bh->state);
		bh = fsg->next_buffhd_to_fill = bh->next;
		fsg->usb_amount_left -= nsend;
		nkeep = 0;
	}
	return 0;
}

static int throw_away_data(struct fsg_dev *fsg)
{
	struct fsg_buffhd *bh;
	u32 amount;
	int rc;

	while ((bh = fsg->next_buffhd_to_drain)->state != BUF_STATE_EMPTY ||
	       fsg->usb_amount_left > 0) {

		/* Throw away the data in a filled buffer */
		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			bh->state = BUF_STATE_EMPTY;
			fsg->next_buffhd_to_drain = bh->next;

			/* A short packet or an error ends everything */
			if (bh->outreq->actual < bh->bulk_out_intended_length ||
			    bh->outreq->status != 0) {
				raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
				return -EINTR;
			}
			continue;
		}

		/* Try to submit another request if we need one */
		bh = fsg->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && fsg->usb_amount_left > 0) {
			amount = min(fsg->usb_amount_left,
				     (u32) mod_data.buflen);

			/* amount is always divisible by 512, hence by
			 * the bulk-out maxpacket size */

			set_bulk_out_req_length(fsg, bh, amount);
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
				       &bh->outreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
			fsg->usb_amount_left -= amount;
			continue;
		}

		/* Otherwise wait for something to happen */
		rc = sleep_thread(fsg);
		if (rc)
			return rc;

	}
	return 0;
}

extern int finish_reply(struct fsg_dev *fsg);
int finish_reply(struct fsg_dev *fsg)
{
	struct fsg_buffhd *bh = fsg->next_buffhd_to_fill;
	int rc = 0;

	switch (fsg->data_dir) {
	case DATA_DIR_NONE:
		break;		/* Nothing to send */

		/* If we don't know whether the host wants to read or write,
		 * this must be CB or CBI with an unknown command.  We mustn't
		 * try to send or receive any data.  So stall both bulk pipes
		 * if we can and wait for a reset. */
	case DATA_DIR_UNKNOWN:
		if (mod_data.can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			rc = halt_bulk_in_endpoint(fsg);
		}
		break;

		/* All but the last buffer of data must have already been sent */
	case DATA_DIR_TO_HOST:
		if (fsg->data_size == 0)
			;	/* Nothing to send */

		/* If there's no residue, simply send the last buffer */
		else if (fsg->residue == 0) {
			if (!no_finish_reply) {
				bh->inreq->zero = 0;
				/* bh->inreq->medium = 0; */
				start_transfer(fsg, fsg->bulk_in,
					       bh->inreq,
					       &bh->inreq_busy,
					       &bh->state);
				fsg->next_buffhd_to_fill = bh->next;
			} else {
				/* */
				no_finish_reply = 0;
			}
		} else {
#if 0
			if (mod_data.can_stall) {
				bh->inreq->zero = 1;
				start_transfer(fsg, fsg->bulk_in, bh->inreq,
					       &bh->inreq_busy, &bh->state);
				fsg->next_buffhd_to_fill = bh->next;
				rc = halt_bulk_in_endpoint(fsg);
			} else
#endif
				rc = pad_with_zeros(fsg);
		}

		break;

		/* We have processed all we want
		 * from the data the host has sent.
		 * There may still be outstanding bulk-out requests. */
	case DATA_DIR_FROM_HOST:
		if ((fsg->residue == 0) || (no_finish_reply))
			no_finish_reply = 0;	/* Nothing to receive */

		/* Did the host stop sending unexpectedly early? */
		else if (fsg->short_packet_received) {
			raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;
		}

		/* We haven't processed all the incoming data.  Even though
		 * we may be allowed to stall, doing so would cause a race.
		 * The controller may already have ACK'ed all the remaining
		 * bulk-out packets, in which case the host wouldn't see a
		 * STALL.  Not realizing the endpoint was halted, it wouldn't
		 * clear the halt -- leading to problems later on. */
#if 0
		else if (mod_data.can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;
		}
#endif
		else
			rc = throw_away_data(fsg);

		/* We can't stall.  Read in the excess data and throw it
		 * all away. */
		break;
	}
	return rc;
}

#ifdef FPGA_VERIFY_MODE
u32 get_phBaseAddr(void)
{
	return 0;
}

void init_WD_timer(unsigned int load_value, unsigned int auto_reload)
{
}

static void WD_is_running(void)
{
	return 1;
}

static void clear_is_running(void)
{
	return 1;
}
#else
u32 get_phBaseAddr(void)
{
	u32 tmp;
	
	__asm__ __volatile__ ("mrc p15, 4, %0, c15, c0, 0":"=r"(tmp));
	return tmp;
}

// void init_WD_timer(unsigned int load_value, unsigned int auto_reload)
// Sets up the WD timer
// r0: initial load value
// r1:  IF 0 (AutoReload) ELSE (SingleShot)
void init_WD_timer(unsigned int load_value, unsigned int auto_reload)
{
	u32 phBaseAddr, wdCountAddr, wdModeAddr, tmp=0;
   	
   	phBaseAddr = get_phBaseAddr();
   	wdCountAddr = phBaseAddr+0x620;
   	wdModeAddr = phBaseAddr+0x628;
   	
   	act_writel(load_value, wdCountAddr);
   	if(auto_reload == 0)
   	{
   		tmp = 0x2;	
   	}
   	act_writel(tmp, wdModeAddr);
}

static void WD_is_running(void)
{
	u32 phBaseAddr, wdModeAddr;

	phBaseAddr = get_phBaseAddr();
   	wdModeAddr = phBaseAddr+0x628;

	if (act_readl(wdModeAddr) & 0x8)
		return 1;

	return 0;
}

static void clear_is_running(void)
{
	u32 phBaseAddr, wdModeAddr;

	phBaseAddr = get_phBaseAddr();
   	wdModeAddr = phBaseAddr+0x628;

	if (act_readl(wdModeAddr) & 0x8)
		return 1;

	return 0;
}
#endif


#ifdef FPGA_VERIFY_MODE
static void clear_enteradfu_flag(void)
{}

static int need_enteradfu(void)
{
	return 1;
}

#else
extern struct atc260x_dev *atc260x_dev_handle;

static void clear_enteradfu_flag(void)
{
	atc260x_set_bits(atc260x_dev_handle, atc2603_PMU_UV_INT_EN, 0x2 , 0x0);
	mdelay(1);
	printk(KERN_INFO "atc2603_PMU_UV_INT_EN: %x\n", atc260x_reg_read(atc260x_dev_handle, atc2603_PMU_UV_INT_EN));
}

static int need_enteradfu(void)
{
	return (atc260x_reg_read(atc260x_dev_handle, atc2603_PMU_UV_INT_EN) & 0x2) ? 1 : 0;
}
#endif
// void set_WD_mode(unsigned int mode)
// Sets up the WD timer  
// r0:  IF 0 (timer mode) ELSE (watchdog mode)
//	void set_WD_mode(unsigned int mode)
//	{
//		u32 phBaseAddr, wdModeAddr;
//		
//		phBaseAddr = get_phBaseAddr();
//	   	wdModeAddr = phBaseAddr+0x628;
//	   	
//	   	if(mode == 0)
//	   	{
//	   		act_writel((act_readl(wdModeAddr) & 0xf7) | 0x4, wdModeAddr);
//	   	}
//	   	else
//	   	{
//	   		act_writel(act_readl(wdModeAddr) | 0x8, wdModeAddr);
//	   	}
//	}
//	
//	void start_WD_timer(void)
//	{
//		u32 phBaseAddr, wdModeAddr;
//		
//		phBaseAddr = get_phBaseAddr();
//	   	wdModeAddr = phBaseAddr+0x628;
//	   	
//		act_writel(act_readl(wdModeAddr) | 0x1, wdModeAddr);
//	}
//	
//	int wd_restart(void)
//	{
//	    printk(KERN_INFO "wd_restart\n");
//	
//	    if (need_enteradfu())
//	    {
//	        printk(KERN_INFO "clear enter adfu flag\n");
//	        clear_enteradfu_flag();
//	    }
//	
//	    msleep(10);
//	
//		//bit26=1,使WD0复位所有cpu及整个系统(除了该bit和WDRESET0)，其它寄存器全部复位
//	    act_writel(act_readl(SPS_PG_CTL)|0x04000000, SPS_PG_CTL);
//	    init_WD_timer(0x98, 0x01);
//	    set_WD_mode(1);
//	    start_WD_timer();
//	    while(1)
//	    {
//	    };
//	}

extern unsigned long (*boot_info_read)(unsigned long);
extern int (*boot_info_write)(unsigned long, unsigned long);
static int send_status(struct fsg_dev *fsg)
{
	struct lun *curlun = fsg->curlun;
	struct fsg_buffhd *bh;
	int rc;
	u8 status = USB_STATUS_PASS;
	u32 sd, sdinfo = 0;
	/* Wait for the next buffer to become available */
	bh = fsg->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;

	}
	if (curlun) {
		sd = curlun->sense_data;
		sdinfo = curlun->sense_data_info;
	} else if (fsg->bad_lun_okay)
		sd = SS_NO_SENSE;
	else
		sd = SS_LOGICAL_UNIT_NOT_SUPPORTED;

	if (fsg->phase_error) {
		DBG(fsg, "sending phase-error status\n");
		status = USB_STATUS_PHASE_ERROR;
		sd = SS_INVALID_COMMAND;
	} else if (sd != SS_NO_SENSE) {
		DBG(fsg, "sending command-failure status\n");
		status = USB_STATUS_FAIL;
		VDBG(fsg, "  sense data: SK x%02x, ASC x%02x, ASCQ x%02x;"
		     "  info x%x\n", SK(sd), ASC(sd), ASCQ(sd), sdinfo);
	}
	if (transport_is_bbb()) {
		struct bulk_cs_wrap *csw = bh->buf;

		/* Store and send the Bulk-only CSW */
		csw->Signature = __constant_cpu_to_le32(USB_BULK_CS_SIG);
		csw->Tag = fsg->tag;
		csw->Residue = cpu_to_le32(fsg->residue);
		csw->Status = status;
#if 1
		printk(
			"csw-> SIG: %x Tag:%x Residue:%x Status:%x\n",
			csw->Signature, csw->Tag, csw->Residue, csw->Status);
#endif
		bh->inreq->length = USB_BULK_CS_WRAP_LEN;
		bh->inreq->zero = 0;
		//printk("  send_status sss\n");
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
			       &bh->inreq_busy, &bh->state);

	} else if (mod_data.transport_type == USB_PR_CB) {

		/* Control-Bulk transport has no status phase! */
		return 0;

	} else {		/* USB_PR_CBI */
		struct interrupt_data *buf = bh->buf;

		/* Store and send the Interrupt data.  UFI sends the ASC
		 * and ASCQ bytes.  Everything else sends a Type (which
		 * is always 0) and the status Value. */
		if (mod_data.protocol_type == USB_SC_UFI) {
			buf->bType = ASC(sd);
			buf->bValue = ASCQ(sd);
		} else {
			buf->bType = 0;
			buf->bValue = status;
		}
		fsg->intreq->length = CBI_INTERRUPT_DATA_LEN;

		fsg->intr_buffhd = bh;	/* Point to the right buffhd */
		fsg->intreq->buf = bh->inreq->buf;
		fsg->intreq->context = bh;
		start_transfer(fsg, fsg->intr_in, fsg->intreq,
			       &fsg->intreq_busy, &bh->state);
	}

	fsg->next_buffhd_to_fill = bh->next;

//		/* when adfu upgrade successful, disconnect and sync */
//		if (unlikely(adfu_success_flag == 1)) {
//			raise_exception(fsg, FSG_STATE_DISCONNECT);
//			adfu_flush_nand_cache();
//			
//			if (boot_info_write && boot_info_read) {
//				boot_info_write(atc2603_PMU_SYS_CTL8, 0x0);
//				boot_info_write(atc2603_PMU_SYS_CTL9, 0x0);
//				printk("\n%s, %d, ctl8: %x, ctl9: %x\n", __func__, __LINE__, boot_info_read(atc2603_PMU_SYS_CTL8), boot_info_read(atc2603_PMU_SYS_CTL9));
//			}
//			
//			printk(KERN_INFO "UPGRADE SUCCESSFULLY\n");
//			if(need_restart == 0)
//			{
//				machine_restart("reboot");
//			}
//		}

	return 0;
}

/*-------------------------------------------------------------------------*/

/* Check whether the command is properly formed and whether its data size
 * and direction agree with the values we already have. */
static int check_command(struct fsg_dev *fsg, int cmnd_size,
			 enum data_direction data_dir, unsigned int mask,
			 int needs_medium, const char *name)
{
	//int i;
	int lun = fsg->cmnd[1] >> 5;
	static const char dirletter[4] = { 'u', 'o', 'i', 'n' };
	char hdlen[20];
	struct lun *curlun;


	if (0 != lun){
		printk("[adfus]we donot support no-lun0 now\n");
		return -EINVAL;
	}
	/* There's some disagreement as to whether RBC pads commands or not.
	 * We'll play it safe and accept either form. */
	if (mod_data.protocol_type == USB_SC_RBC) {
		if (fsg->cmnd_size == 12)
			cmnd_size = 12;

		/* All the other protocols pad to 12 bytes */
	} else
		cmnd_size = 12;

	hdlen[0] = 0;
	if (fsg->data_dir != DATA_DIR_UNKNOWN)
		sprintf(hdlen, ", H%c=%u", dirletter[(int)fsg->data_dir],
			fsg->data_size);

	VDBG(fsg, "SCSI command: %s;  Dc=%d, D%c=%u;  Hc=%d%s\n",
	     name, cmnd_size, dirletter[(int)data_dir],
	     fsg->data_size_from_cmnd, fsg->cmnd_size, hdlen);

	/* We can't reply at all until we know the correct data direction
	 * and size. */
	if (fsg->data_size_from_cmnd == 0)
		data_dir = DATA_DIR_NONE;
	if (fsg->data_dir == DATA_DIR_UNKNOWN) {	/* CB or CBI */
		fsg->data_dir = data_dir;
		fsg->data_size = fsg->data_size_from_cmnd;

	} else {		/* Bulk-only */
		if (fsg->data_size < fsg->data_size_from_cmnd) {

			/* Host data size < Device data size is a phase error.
			 * Carry out the command, but only transfer as much
			 * as we are allowed. */
			fsg->data_size_from_cmnd = fsg->data_size;
			fsg->phase_error = 1;
		}
	}
	fsg->residue = fsg->usb_amount_left = fsg->data_size;

	/* Conflicting data directions is a phase error */
	if (fsg->data_dir != data_dir && fsg->data_size_from_cmnd > 0) {
		fsg->phase_error = 1;
		return -EINVAL;
	}
#if 0
	/* Verify the length of the command itself */
	if (cmnd_size != fsg->cmnd_size) {

		/* Special case workaround: There are plenty of buggy SCSI
		 * implementations. Many have issues with cbw->Length
		 * field passing a wrong command size. For those cases we
		 * always try to work around the problem by using the length
		 * sent by the host side provided it is at least as large
		 * as the correct command length.
		 * Examples of such cases would be MS-Windows, which issues
		 * REQUEST SENSE with cbw->Length == 12 where it should
		 * be 6, and xbox360 issuing INQUIRY, TEST UNIT READY and
		 * REQUEST SENSE with cbw->Length == 10 where it should
		 * be 6 as well.
		 */
		if (cmnd_size <= fsg->cmnd_size) {
			DBG(fsg, "%s is buggy! Expected length %d "
			    "but we got %d\n", name, cmnd_size, fsg->cmnd_size);
			cmnd_size = fsg->cmnd_size;
		} else {
			fsg->phase_error = 1;
			return -EINVAL;
		}
	}
#endif
	/* Check that the LUN values are consistent */
	if (transport_is_bbb()) {
		if (fsg->lun != lun)
			DBG(fsg, "using LUN %d from CBW, "
			    "not LUN %d from CDB\n", fsg->lun, lun);
	} else
		fsg->lun = lun;	/* Use LUN from the command */

	/* Check the LUN */
	if (fsg->lun >= 0 && fsg->lun < fsg->nluns) {
		fsg->curlun = curlun = &fsg->luns[fsg->lun];
		if (fsg->cmnd[0] != SC_REQUEST_SENSE) {
			curlun->sense_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
	} else {
		fsg->curlun = curlun = NULL;
		fsg->bad_lun_okay = 0;

		/* INQUIRY and REQUEST SENSE commands are explicitly allowed
		 * to use unsupported LUNs; all others may not. */
		if (fsg->cmnd[0] != SC_INQUIRY &&
		    fsg->cmnd[0] != SC_REQUEST_SENSE) {
			DBG(fsg, "unsupported LUN %d\n", fsg->lun);
			return -EINVAL;
		}
	}

	/* If a unit attention condition exists, only INQUIRY and
	 * REQUEST SENSE commands are allowed; anything else must fail. */
	if (curlun && curlun->unit_attention_data != SS_NO_SENSE &&
	    fsg->cmnd[0] != SC_INQUIRY && fsg->cmnd[0] != SC_REQUEST_SENSE) {
		curlun->sense_data = curlun->unit_attention_data;
		curlun->unit_attention_data = SS_NO_SENSE;
		return -EINVAL;
	}
#if 0
	/* Check that only command bytes listed in the mask are non-zero */
	fsg->cmnd[1] &= 0x1f;	/* Mask away the LUN */
	for (i = 1; i < cmnd_size; ++i) {
		if (fsg->cmnd[i] && !(mask & (1 << i))) {
			if (curlun)
				curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
	}
#endif
	/* If the medium isn't mounted and the command needs to access
	 * it, return an error. */
	if (curlun && !backing_file_is_open(curlun) && needs_medium) {
		curlun->sense_data = SS_MEDIUM_NOT_PRESENT;
		return -EINVAL;
	}

	return 0;
}

extern int do_scsi_command(struct fsg_dev *fsg);
int do_scsi_command(struct fsg_dev *fsg)
{
	struct fsg_buffhd *bh;
	int rc;
	int reply = -EINVAL;
	unsigned char sub_code;
	static char unknown[16];
	struct lun *curlun;

	dump_cdb(fsg);

	/* Wait for the next buffer to become available for data or status */
	bh = fsg->next_buffhd_to_drain = fsg->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}
	fsg->phase_error = 0;
	fsg->short_packet_received = 0;

	down_read(&fsg->filesem);	/* We're using the backing file */
	printk("fsg->cmnd[0-2] = 0x%x 0x%x 0x%x\n", fsg->cmnd[0], fsg->cmnd[1], fsg->cmnd[2]);
	switch (fsg->cmnd[0]) {

	case SC_ADFU_UPGRADE:
		/* printk(KERN_INFO "*******ADFU_UPGRADE_COMMAND:*******\n"); */

		fsg->lun = 0; /* cheat the OS */
		fsg->curlun = curlun = &fsg->luns[fsg->lun];
		curlun->sense_data = SS_NO_SENSE;
		curlun->sense_data_info = 0;
		curlun->info_valid = 0;

		sub_code = fsg->cmnd[1];
		switch (sub_code) {

//		case SC_ADFU_FORMAT_FLASH:
//		printk(KERN_INFO "\n****** ERASE THE FLASH ******\n");
//			need_format = 1;
//			fsg->data_dir = DATA_DIR_NONE;
//
//			break;

//		case SC_ADFU_TEST_FLASHRDY:
//		/* printk(KERN_INFO "\n****** TEST FLASH ERASE READY ******\n"); */
//			fsg->data_dir = DATA_DIR_NONE;
//			if (!backing_file_is_open(curlun)) {
//				/* printk(KERN_INFO "\n****** NOT READY ******\n"); */
//				curlun->sense_data = SS_MEDIUM_NOT_PRESENT;
//			} else {
//				/* */
//				printk(KERN_INFO "\n****** READY ******\n");
//			}
//
//			break;

//		case SC_ADFU_ACCESS_MBR:
//		/* printk(KERN_INFO "*******ACCESS THE MBR !!*******\n"); */
//			fsg->data_size = 0x400;
//			fsg->residue = fsg->usb_amount_left = fsg->data_size;
//			fsg->data_dir = DATA_DIR_TO_HOST;
//			reply = do_ADFU_acmbr(fsg);
//			break;
		case SC_ADFU_ACCESS_INTERNAL_RAM:
			fsg->data_dir = DATA_DIR_FROM_HOST;
			fsg->data_size_from_cmnd = get_le32(&fsg->cmnd[5]);
			fsg->residue = fsg->usb_amount_left = fsg->data_size;
			reply = do_ADFU_access_iram(fsg);
			break;
		case SC_ADFU_DOWNLOAD_IMG:
//			printk("*******WRITE THE ROOTFS !!*******\n");
			/* prepare */
			/* fsg->data_size = */
			fsg->data_dir = DATA_DIR_FROM_HOST;
			fsg->data_size_from_cmnd = (get_le32(&fsg->cmnd[5]))<<9;
			fsg->residue = fsg->usb_amount_left = fsg->data_size;
			reply = do_ADFU_wtrootfs(fsg);
//			printk("handle SC_ADFU_WRITE_ROOTFS end\n");
			break;
		case SC_ADFU_INFO:
			fsg->data_dir = DATA_DIR_TO_HOST;
			fsg->data_size_from_cmnd = get_le32(&fsg->cmnd[5]);
			fsg->residue = fsg->usb_amount_left = fsg->data_size;
			reply = do_ADFU_INFO(fsg);
			break;
//		case SC_ADFU_READ_ROOTFS:
//		/* printk(KERN_INFO "*******READ THE ROOTFS !!*******\n"); */
//			/* prepare */
//			/* fsg->data_size = */
//			fsg->data_dir = DATA_DIR_TO_HOST;
//			fsg->data_size_from_cmnd = get_le32(&fsg->cmnd[9]);
//			fsg->residue = fsg->usb_amount_left = fsg->data_size;
//			reply = do_ADFU_rdrootfs(fsg);
//			break;
		case SC_ADFU_TFEROVER:
			printk("SC_ADFU_TFEROVER\n");
			//update phy if downloading misc.img or updating mbr_info.bin
			if((update_phy_boot == 1) || (check_partition_flag > 0))
			{
				set_probatch_phase(PROBATCH_WRITE_PHY);
				wait_adfus_proc(PROBATCH_FINISH_WRITE_PHY);	
			}
	
			//check udisk formatted ?					
			set_probatch_phase(PROBATCH_FINISH);
			wait_adfus_proc(PROBATCH_FINISH_OK);			
			break;
		}
		break;

	case SC_ADFU_SUCCESSFUL:
		fsg->curlun = curlun = &fsg->luns[0];
		curlun->sense_data = SS_NO_SENSE;
		curlun->sense_data_info = 0;
		curlun->info_valid = 0;
		adfu_success_flag = 1;
		break;

		/* Some mandatory commands that we recognize but don't implement.
		 * They don't mean much in this setting.  It's left as an exercise
		 * for anyone interested to implement RESERVE and RELEASE in terms
		 * of Posix locks. */
	case SC_FORMAT_UNIT:
	case SC_RELEASE:
	case SC_RESERVE:
	case SC_SEND_DIAGNOSTIC:
		/* printk(KERN_INFO "SC_ENTENSIVE.\n"); */

	default:
		/* printk(KERN_INFO "SC_UNKNOW.\n"); */

		fsg->data_size_from_cmnd = 0;
		sprintf(unknown, "Unknown x%02x", fsg->cmnd[0]);
		reply = check_command(fsg, fsg->cmnd_size,
				      DATA_DIR_UNKNOWN, 0xff, 0,
				      unknown);
		if (reply == 0) {
			fsg->curlun->sense_data = SS_INVALID_COMMAND;
			reply = -EINVAL;
		}
		break;
	}
	up_read(&fsg->filesem);

	if (reply == -EINTR || signal_pending(current)) {
		/* */
		return -EINTR;
	}
	/* Set up the single reply buffer for finish_reply() */
	if (reply == -EINVAL)
		reply = 0;	/* Error reply length */
	if (reply >= 0 && fsg->data_dir == DATA_DIR_TO_HOST) {
		reply = min((u32) reply, fsg->data_size_from_cmnd);
		bh->inreq->length = reply;
		bh->state = BUF_STATE_FULL;
		fsg->residue -= reply;
	}			/* Otherwise it's already set */
	return 0;
}

/*-------------------------------------------------------------------------*/
static int received_cbw(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct usb_request *req = bh->outreq;
	struct bulk_cb_wrap *cbw = req->buf;
	/* Was this a real packet?  Should it be ignored? */
	if (req->status || test_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags))
		return -EINVAL;
#if 0
	int k;
	printk(KERN_INFO "CDB:");
	for (k = 0; k < MAX_COMMAND_SIZE; k++) {
		/**/
		printk(KERN_INFO "%x ", cbw->CDB[k]);
	}
	printk(KERN_INFO "\n");
	printk(KERN_INFO "Signature:%x Tag:%x Flags: \
		%x DataTransferLength:%x\n",
	       cbw->Signature, cbw->Tag, cbw->Flags,
	       le32_to_cpu(cbw->DataTransferLength));
#endif

#if 0
	/* Is the CBW valid? */
	if (req->actual != USB_BULK_CB_WRAP_LEN ||
	    cbw->Signature != __constant_cpu_to_le32(USB_BULK_CB_SIG)) {
		DBG(fsg, "invalid CBW: len %u sig 0x%x\n",
		    req->actual, le32_to_cpu(cbw->Signature));

		/* The Bulk-only spec says we MUST stall the IN endpoint
		 * (6.6.1), so it's unavoidable.  It also says we must
		 * retain this state until the next reset, but there's
		 * no way to tell the controller driver it should ignore
		 * Clear-Feature(HALT) requests.
		 *
		 * We aren't required to halt the OUT endpoint; instead
		 * we can simply accept and discard any data received
		 * until the next reset. */
		wedge_bulk_in_endpoint(fsg);
		set_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags);
		return -EINVAL;
	}

	/* Is the CBW meaningful? */
	if (cbw->Lun >= MAX_LUNS || cbw->Flags & ~USB_BULK_IN_FLAG ||
	    cbw->Length <= 0 || cbw->Length > MAX_COMMAND_SIZE) {
		DBG(fsg, "non-meaningful CBW: lun = %u, flags = 0x%x, "
		    "cmdlen %u\n", cbw->Lun, cbw->Flags, cbw->Length);

		/* We can do anything we want here, so let's stall the
		 * bulk pipes if we are allowed to. */
		if (mod_data.can_stall) {
			//fsg_set_halt(fsg, fsg->bulk_out);
			halt_bulk_in_endpoint(fsg);
		}
		return -EINVAL;
	}
common_use:
#endif	
	/* Save the command for later */
	fsg->cmnd_size = cbw->Length;
	memcpy(fsg->cmnd, cbw->CDB, fsg->cmnd_size);
	if (cbw->Flags & USB_BULK_IN_FLAG)
		fsg->data_dir = DATA_DIR_TO_HOST;
	else
		fsg->data_dir = DATA_DIR_FROM_HOST;
	fsg->data_size = le32_to_cpu(cbw->DataTransferLength);
	if (fsg->data_size == 0)
		fsg->data_dir = DATA_DIR_NONE;
	fsg->lun = cbw->Lun;
	fsg->tag = cbw->Tag;
	return 0;
}

static int is_adfu_cmd(char *buf)
{
	if( (buf[0] == 0x55)&&(buf[1] == 0x53)&&(buf[2] == 0x42)&&(buf[3] == 0x43) )
		return 1;
	
	return 0;
}

static void printk_adfu_cmd(struct usb_request *req)
{
	int i;
	char *buf=req->buf;

	printk("usb_request:length=%d,actual=%d\n", req->length, req->actual);

	for(i=0; i<req->actual; i++)
	{
		printk("0x%x ", *buf);
		buf++;
	}

	printk("\n%x\n", __FUNCTION__);
	
}

static int get_next_command(struct fsg_dev *fsg)
{
	struct fsg_buffhd *bh;
	int rc = 0;
	if (transport_is_bbb()) {

		/* Wait for the next buffer to become available */
		bh = fsg->next_buffhd_to_fill;
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;

		}

		/* Queue a request to read a Bulk-only CBW */
		set_bulk_out_req_length(fsg, bh, USB_BULK_CB_WRAP_LEN);
		bh->outreq->short_not_ok = 1;
		/* printk(KERN_INFO
		   "get command length: %d\n",bh->outreq->length); */
		start_transfer(fsg, fsg->bulk_out, bh->outreq,
			       &bh->outreq_busy, &bh->state);

		/* We will drain the buffer in software, which means we
		 * can reuse it for the next filling.  No need to advance
		 * next_buffhd_to_fill. */

		/* Wait for the CBW to arrive */
		while (bh->state != BUF_STATE_FULL) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;

		}
		smp_rmb();

		if(0==is_adfu_cmd(bh->outreq->buf))
			printk_adfu_cmd(bh->outreq);
		
		rc = received_cbw(fsg, bh);
		bh->state = BUF_STATE_EMPTY;

	} else {		/* USB_PR_CB or USB_PR_CBI */

		unsigned long flag;
		/* Wait for the next command to arrive */
		while (fsg->cbbuf_cmnd_size == 0) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;
		}

		/* Is the previous status interrupt request still busy?
		 * The host is allowed to skip reading the status,
		 * so we must cancel it. */

		if (fsg->intreq_busy)
			usb_ep_dequeue(fsg->intr_in, fsg->intreq);

		/* Copy the command and mark the buffer empty */
		fsg->data_dir = DATA_DIR_UNKNOWN;
		spin_lock_irqsave(&fsg->lock, flag);
		fsg->cmnd_size = fsg->cbbuf_cmnd_size;
		memcpy(fsg->cmnd, fsg->cbbuf_cmnd, fsg->cmnd_size);
		fsg->cbbuf_cmnd_size = 0;
		spin_unlock_irqrestore(&fsg->lock, flag);
	}
	return rc;
}

/*-------------------------------------------------------------------------*/

static int enable_endpoint(struct fsg_dev *fsg, struct usb_ep *ep,
			   const struct usb_endpoint_descriptor *d)
{
	int rc;

	ep->driver_data = fsg;
	ep->desc = d;
	rc = usb_ep_enable(ep);
	if (rc)
		ERROR(fsg, "------can't enable %s, result %d\n", ep->name, rc);
	return rc;
}

static int alloc_request(struct fsg_dev *fsg, struct usb_ep *ep,
			 struct usb_request **preq)
{
	*preq = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (*preq)
		return 0;
	ERROR(fsg, "can't allocate request for %s\n", ep->name);
	return -ENOMEM;
}

static int act_config_ep_by_speed(struct usb_gadget *g,
			struct usb_ss_ep_comp_descriptor *comp_desc,
			struct usb_ep *_ep)
{
	if (g->speed == USB_SPEED_SUPER && gadget_is_superspeed(g)) {
		if (!comp_desc ||(comp_desc->bDescriptorType != USB_DT_SS_ENDPOINT_COMP))
			return -EIO;
		
		_ep->comp_desc = comp_desc;
		_ep->maxburst = comp_desc->bMaxBurst;
	}
	return 0;
}

/*
 * Reset interface setting and re-init endpoint state (toggle etc).
 * Call with altsetting < 0 to disable the interface.  The only other
 * available altsetting is 0, which enables the interface.
 */
static int do_set_interface(struct fsg_dev *fsg, int altsetting)
{
	int rc = 0;
	int i;
	const struct usb_endpoint_descriptor *d;

	if (fsg->running)
		DBG(fsg, "reset interface\n");

reset:
	/* Deallocate the requests */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		struct fsg_buffhd *bh = &fsg->buffhds[i];

		if (bh->inreq) {
			usb_ep_free_request(fsg->bulk_in, bh->inreq);
			bh->inreq = NULL;
		}
		if (bh->outreq) {
			usb_ep_free_request(fsg->bulk_out, bh->outreq);
			bh->outreq = NULL;
		}
	}
	if (fsg->intreq) {
		usb_ep_free_request(fsg->intr_in, fsg->intreq);
		fsg->intreq = NULL;
	}

	/* Disable the endpoints */
	if (fsg->bulk_in_enabled) {
		usb_ep_disable(fsg->bulk_in);
		fsg->bulk_in_enabled = 0;
	}
	if (fsg->bulk_out_enabled) {
		usb_ep_disable(fsg->bulk_out);
		fsg->bulk_out_enabled = 0;
	}
	if (fsg->intr_in_enabled) {
		usb_ep_disable(fsg->intr_in);
		fsg->intr_in_enabled = 0;
	}

	fsg->running = 0;
	if (altsetting < 0 || rc != 0)
		return rc;

	DBG(fsg, "set interface %d\n", altsetting);

	/* Enable the endpoints */
	d = ep_desc(fsg->gadget, &fs_bulk_in_desc, &hs_bulk_in_desc, &fsg_ss_bulk_in_desc);
	if (fsg->gadget->speed == USB_SPEED_SUPER && gadget_is_superspeed(fsg->gadget))
		act_config_ep_by_speed(fsg->gadget, &fsg_ss_bulk_in_comp_desc,fsg->bulk_in);
	rc = enable_endpoint(fsg, fsg->bulk_in, d);
	if (rc != 0)
		goto reset;
	fsg->bulk_in_enabled = 1;

	d = ep_desc(fsg->gadget, &fs_bulk_out_desc, &hs_bulk_out_desc, &fsg_ss_bulk_out_desc);
	if (fsg->gadget->speed == USB_SPEED_SUPER && gadget_is_superspeed(fsg->gadget))
		act_config_ep_by_speed(fsg->gadget, &fsg_ss_bulk_out_comp_desc,fsg->bulk_out);
	rc = enable_endpoint(fsg, fsg->bulk_out, d);
	if (rc != 0)
		goto reset;
	fsg->bulk_out_enabled = 1;
	fsg->bulk_out_maxpacket = le16_to_cpu(d->wMaxPacketSize);
	clear_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags);

	if (transport_is_cbi()) {
		d = ep_desc(fsg->gadget, &fs_intr_in_desc, &hs_intr_in_desc,NULL);
		rc = enable_endpoint(fsg, fsg->intr_in, d);
		if (rc != 0)
			goto reset;
		fsg->intr_in_enabled = 1;
	}

	/* Allocate the requests */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		struct fsg_buffhd *bh = &fsg->buffhds[i];
		rc = alloc_request(fsg, fsg->bulk_in, &bh->inreq);
		if (rc != 0)
			goto reset;
		rc = alloc_request(fsg, fsg->bulk_out, &bh->outreq);
		if (rc != 0)
			goto reset;
		bh->inreq->buf = bh->outreq->buf = bh->buf;
		bh->inreq->context = bh->outreq->context = bh;
		bh->inreq->complete = bulk_in_complete;
		bh->outreq->complete = bulk_out_complete;
	}
	if (transport_is_cbi()) {
		rc = alloc_request(fsg, fsg->intr_in, &fsg->intreq);
		if (rc != 0)
			goto reset;
		fsg->intreq->complete = intr_in_complete;
	}

	fsg->running = 1;
	for (i = 0; i < fsg->nluns; ++i)
		fsg->luns[i].unit_attention_data = SS_RESET_OCCURRED;
	return rc;
}

/*
 * Change our operational configuration.  This code must agree with the code
 * that returns config descriptors, and with interface altsetting code.
 *
 * It's also responsible for power management interactions.  Some
 * configurations might not work with our current power sources.
 * For now we just assume the gadget is always self-powered.
 */
static int do_set_config(struct fsg_dev *fsg, u8 new_config)
{
	int rc = 0;

	/* Disable the single interface */
	if (fsg->config != 0) {
		DBG(fsg, "reset config\n");
		fsg->config = 0;
		rc = do_set_interface(fsg, -1);
	}

	/* Enable the interface */
	if (new_config != 0) {
		fsg->config = new_config;
		rc = do_set_interface(fsg, 0);
		if (rc != 0)
			fsg->config = 0;	/* Reset on errors */
		else {
			char *speed;

			switch (fsg->gadget->speed) {
			case USB_SPEED_LOW:
				speed = "low";
				break;
			case USB_SPEED_FULL:
				speed = "full";
				break;
			case USB_SPEED_HIGH:
				speed = "high";
				break;
			case USB_SPEED_SUPER:
				speed = "super";
				break;	
			default:
				speed = "?";
				break;
			}
			INFO(fsg, "%s speed config #%d\n", speed, fsg->config);
		}
	}
	return rc;
}

/*-------------------------------------------------------------------------*/

static void handle_exception(struct fsg_dev *fsg)
{
	siginfo_t info;
	int sig;
	int i;
	int num_active;
	struct fsg_buffhd *bh;
	enum fsg_state old_state;
	u8 new_config;
	struct lun *curlun;
	unsigned int exception_req_tag;
	unsigned long flag;
	int rc;

	/* Clear the existing signals.  Anything but SIGUSR1 is converted
	 * into a high-priority EXIT exception. */
	for (;;) {
		sig = dequeue_signal_lock(current, &current->blocked, &info);
		if (!sig)
			break;
		if (sig != SIGUSR1) {
			if (fsg->state < FSG_STATE_EXIT)
				DBG(fsg, "Main thread exiting on signal\n");
			raise_exception(fsg, FSG_STATE_EXIT);
		}
	}
	
	if (unlikely(first_start != 0)) {
		first_start = 0;
		return;
	}

	/* Cancel all the pending transfers */
	if (fsg->intreq_busy)
		usb_ep_dequeue(fsg->intr_in, fsg->intreq);
	for (i = 0; i < NUM_BUFFERS; ++i) {
		bh = &fsg->buffhds[i];
		if (bh->inreq_busy) {
			printk("bulk in dequeue\n");
			usb_ep_dequeue(fsg->bulk_in, bh->inreq);
		}
		if (bh->outreq_busy) {
			printk("bulk out dequeue\n");
			usb_ep_dequeue(fsg->bulk_out, bh->outreq);
		}
	}

	/* Wait until everything is idle */
	for (;;) {
		num_active = fsg->intreq_busy;
		for (i = 0; i < NUM_BUFFERS; ++i) {
			bh = &fsg->buffhds[i];
			num_active += bh->inreq_busy + bh->outreq_busy;
		}
		if (num_active == 0)
			break;
		if (sleep_thread(fsg))
			return;
	}

	/* Clear out the controller's fifos */
	if (fsg->bulk_in_enabled)
		usb_ep_fifo_flush(fsg->bulk_in);
	if (fsg->bulk_out_enabled)
		usb_ep_fifo_flush(fsg->bulk_out);
	if (fsg->intr_in_enabled)
		usb_ep_fifo_flush(fsg->intr_in);

	/* Reset the I/O buffer states and pointers, the SCSI
	 * state, and the exception.  Then invoke the handler. */
	spin_lock_irqsave(&fsg->lock, flag);

	for (i = 0; i < NUM_BUFFERS; ++i) {
		bh = &fsg->buffhds[i];
		bh->state = BUF_STATE_EMPTY;
	}
	fsg->next_buffhd_to_fill = fsg->next_buffhd_to_drain = &fsg->buffhds[0];

	exception_req_tag = fsg->exception_req_tag;
	new_config = fsg->new_config;
	old_state = fsg->state;

	if (old_state == FSG_STATE_ABORT_BULK_OUT)
		fsg->state = FSG_STATE_STATUS_PHASE;
	else {
		for (i = 0; i < fsg->nluns; ++i) {
			curlun = &fsg->luns[i];
			curlun->prevent_medium_removal = 0;
			curlun->sense_data = curlun->unit_attention_data =
				SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
		fsg->state = FSG_STATE_IDLE;
	}
	spin_unlock_irqrestore(&fsg->lock, flag);

	/* Carry out any extra actions required for the exception */
	switch (old_state) {
	default:
		break;

	case FSG_STATE_ABORT_BULK_OUT:
		send_status(fsg);
		spin_lock_irqsave(&fsg->lock, flag);
		if (fsg->state == FSG_STATE_STATUS_PHASE)
			fsg->state = FSG_STATE_IDLE;
		spin_unlock_irqrestore(&fsg->lock, flag);
		break;
	case FSG_STATE_RESET:
		/* In case we were forced against our will to halt a
		 * bulk endpoint, clear the halt now.  (The SuperH UDC
		 * requires this.) */
		if (test_and_clear_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags))
			usb_ep_clear_halt(fsg->bulk_in);

		if (transport_is_bbb()) {
			if (fsg->ep0_req_tag == exception_req_tag)
				ep0_queue(fsg);	/* Complete the status stage */

		} else if (transport_is_cbi())
			send_status(fsg);	/* Status by interrupt pipe */

		/* Technically this should go here, but it would only be
		 * a waste of time.  Ditto for the INTERFACE_CHANGE and
		 * CONFIG_CHANGE cases. */
		/* for (i = 0; i < fsg->nluns; ++i)
		   fsg->luns[i].unit_attention_data = SS_RESET_OCCURRED; */
		break;
	case FSG_STATE_INTERFACE_CHANGE:
		rc = do_set_interface(fsg, 0);
		if (fsg->ep0_req_tag != exception_req_tag)
			break;
		if (rc != 0) {	/* STALL on errors */
			fsg_set_halt(fsg, fsg->ep0);
		} else		/* Complete the status stage */
			ep0_queue(fsg);
		break;

	case FSG_STATE_CONFIG_CHANGE:
		rc = do_set_config(fsg, new_config);
		if (fsg->ep0_req_tag != exception_req_tag)
			break;
		if (rc != 0) {	/* STALL on errors */
			fsg_set_halt(fsg, fsg->ep0);
		} else		/* Complete the status stage */
			ep0_queue(fsg);
		break;

	case FSG_STATE_DISCONNECT:
		fsync_all(fsg);
		do_set_config(fsg, 0);	/* Unconfigured state */
        if (unlikely(adfu_success_flag == 1)) {
            usb_gadget_disconnect(fsg->gadget);
            
            if(need_restart == 1)   //shutdown
            {
                kernel_restart("upgrade_halt");
            }
            else if(need_restart == 2)  //reboot
            {
                kernel_restart("upgrade_reboot");
            }
            else
            {     
                //do nothing           
            }
    	}
		break;

	case FSG_STATE_EXIT:
	case FSG_STATE_TERMINATED:
		do_set_config(fsg, 0);	/* Free resources */
		spin_lock_irqsave(&fsg->lock, flag);
		fsg->state = FSG_STATE_TERMINATED;	/* Stop the thread */
		spin_unlock_irqrestore(&fsg->lock, flag);
		break;
	}
}

/*-------------------------------------------------------------------------*/

static int fsg_main_thread(void *fsg_)
{
	struct fsg_dev *fsg = fsg_;
	unsigned long flag;
	/* Allow the thread to be killed by a signal, but set the signal mask
	 * to block everything but INT, TERM, KILL, and USR1. */
	allow_signal(SIGINT);
	allow_signal(SIGTERM);
	allow_signal(SIGKILL);
	allow_signal(SIGUSR1);
	/* Allow the thread to be frozen */
	set_freezable();

	/* Arrange for userspace references to be interpreted as kernel
	 * pointers.  That way we can pass a kernel pointer to a routine
	 * that expects a __user pointer and it will work okay. */
	set_fs(get_ds());

	/* The main loop */
	while (fsg->state != FSG_STATE_TERMINATED) {

		if (exception_in_progress(fsg) || signal_pending(current)) {
			printk("stat= %d\n", fsg->state);
			handle_exception(fsg);
			continue;
		}

		if (!fsg->running) {
			sleep_thread(fsg);
			continue;
		}

		if (get_next_command(fsg))
			continue;

		spin_lock_irqsave(&fsg->lock, flag);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_DATA_PHASE;
		spin_unlock_irqrestore(&fsg->lock, flag);

		//printk("  do_scsi_command yyyyyy\n");
		if (do_scsi_command(fsg) || finish_reply(fsg))
			continue;
		//printk("  do_scsi_command sss\n");
		
		spin_lock_irqsave(&fsg->lock, flag);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_STATUS_PHASE;
		spin_unlock_irqrestore(&fsg->lock, flag);

		if (send_status(fsg))
			continue;

		spin_lock_irqsave(&fsg->lock, flag);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_IDLE;
		spin_unlock_irqrestore(&fsg->lock, flag);
	}

	spin_lock_irqsave(&fsg->lock, flag);
	fsg->thread_task = NULL;
	spin_unlock_irqrestore(&fsg->lock, flag);

	/* In case we are exiting because of a signal, unregister the
	 * gadget driver and close the backing file. */
	if (test_and_clear_bit(REGISTERED, &fsg->atomic_bitflags)) {
		usb_gadget_unregister_driver(&fsg_driver);
		close_all_backing_files(fsg);
	}

	/* Let the unbind and cleanup routines know the thread has exited */
	complete_and_exit(&fsg->thread_notifier, 0);
	return 0;
}

/*-------------------------------------------------------------------------*/

/* If the next two routines are called while the gadget is registered,
 * the caller must own fsg->filesem for writing. */

static int open_backing_file(struct lun *curlun, const char *filename)
{
	int ro;
	struct file *filp = NULL;
	int rc = -EINVAL;
	struct inode *inode = NULL;
	loff_t size;
	loff_t num_sectors;

	/* R/W if we can, R/O if we must */
	ro = curlun->ro;
	if (!ro) {
		filp = filp_open(filename, O_RDWR | O_LARGEFILE, 0);
		if (-EROFS == PTR_ERR(filp))
			ro = 1;
	}
	if (ro)
		filp = filp_open(filename, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(filp)) {
		LINFO(curlun, "unable to open backing file: %s\n", filename);
		return PTR_ERR(filp);
	}

	if (!(filp->f_mode & FMODE_WRITE))
		ro = 1;

	if (filp->f_path.dentry)
		inode = filp->f_path.dentry->d_inode;
	if (inode && S_ISBLK(inode->i_mode)) {
		if (bdev_read_only(inode->i_bdev))
			ro = 1;
	} else if (!inode || !S_ISREG(inode->i_mode)) {
		LINFO(curlun, "invalid file type: %s\n", filename);
		goto out;
	}

	/* If we can't read the file, it's no good.
	 * If we can't write the file, use it read-only. */
	if (!filp->f_op || !(filp->f_op->read || filp->f_op->aio_read)) {
		LINFO(curlun, "file not readable: %s\n", filename);
		goto out;
	}
	if (!(filp->f_op->write || filp->f_op->aio_write))
		ro = 1;

	size = i_size_read(inode->i_mapping->host);
	if (size < 0) {
		LINFO(curlun, "unable to find file size: %s\n", filename);
		rc = (int)size;
		goto out;
	}
	num_sectors = size >> 9;	/* File size in 512-byte sectors */
	if (num_sectors == 0) {
		LINFO(curlun, "file too small: %s\n", filename);
		rc = -ETOOSMALL;
		goto out;
	}

	get_file(filp);

	curlun->devnum = filp->f_path.dentry->d_inode->i_rdev;
	/* add by wlt, disk type decided */
	if (MAJOR(filp->f_path.dentry->d_inode->i_rdev) == MAJOR_OF_CARD) {
		printk("device num:MAJOR: %u MINOR: %u\n",
		       MAJOR(filp->f_path.dentry->d_inode->i_rdev),
		       MINOR(filp->f_path.dentry->d_inode->i_rdev));
		curlun->disk_type = CARD_MEDIUM;
		card_dirty_flag = 0;
	}
	else if (MAJOR(filp->f_path.dentry->d_inode->i_rdev) == MAJOR_OF_NAND) {
		printk("device num:MAJOR: %u MINOR: %u\n",
		       MAJOR(filp->f_path.dentry->d_inode->i_rdev),
		       MINOR(filp->f_path.dentry->d_inode->i_rdev));
		curlun->disk_type = NAND_MEDIUM;
		nand_dirty_flag = 0;
	}
	else if (MAJOR(filp->f_path.dentry->d_inode->i_rdev) == MAJOR_OF_MMS) {
		printk("device num:MAJOR: %u MINOR: %u\n",
		       MAJOR(filp->f_path.dentry->d_inode->i_rdev),
		       MINOR(filp->f_path.dentry->d_inode->i_rdev));
		curlun->disk_type = MMS_MEDIUM;
		card_dirty_flag = 0;
	}
	curlun->ro = ro;
	curlun->filp = filp;
	curlun->file_length = size;
	curlun->num_sectors = num_sectors;
	LDBG(curlun, "open backing file: %s\n", filename);
	rc = 0;

out:
	filp_close(filp, current->files);
	return rc;
}

static void close_backing_file(struct lun *curlun)
{
	if (curlun->filp) {
		LDBG(curlun, "close backing file\n");
		fput(curlun->filp);
		curlun->filp = NULL;
	}
}

static void close_all_backing_files(struct fsg_dev *fsg)
{
	int i;

	for (i = 0; i < fsg->nluns; ++i)
		close_backing_file(&fsg->luns[i]);
}

static ssize_t show_ro(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct lun *curlun = dev_to_lun(dev);

	return sprintf(buf, "%d\n", curlun->ro);
}

static ssize_t show_file(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lun *curlun = dev_to_lun(dev);
	struct fsg_dev *fsg = dev_get_drvdata(dev);
	char *p;
	ssize_t rc;

	down_read(&fsg->filesem);
	if (backing_file_is_open(curlun)) {	/* Get the complete pathname */
		p = d_path(&curlun->filp->f_path, buf, PAGE_SIZE - 1);
		if (IS_ERR(p))
			rc = PTR_ERR(p);
		else {
			rc = strlen(p);
			memmove(buf, p, rc);
			buf[rc] = '\n';	/* Add a newline */
			buf[++rc] = 0;
		}
	} else {		/* No file, return 0 bytes */
		*buf = 0;
		rc = 0;
	}
	up_read(&fsg->filesem);
	return rc;
}

static ssize_t store_ro(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	ssize_t rc = count;
	struct lun *curlun = dev_to_lun(dev);
	struct fsg_dev *fsg = dev_get_drvdata(dev);
	int i;

	if (sscanf(buf, "%d", &i) != 1)
		return -EINVAL;

	/* Allow the write-enable status to change only while the backing file
	 * is closed. */
	down_read(&fsg->filesem);
	if (backing_file_is_open(curlun)) {
		LDBG(curlun, "read-only status change prevented\n");
		rc = -EBUSY;
	} else {
		curlun->ro = !!i;
		LDBG(curlun, "read-only status set to %d\n", curlun->ro);
	}
	up_read(&fsg->filesem);
	return rc;
}

static ssize_t store_file(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	return count;
#if 0
	struct lun *curlun = dev_to_lun(dev);
	struct fsg_dev *fsg = dev_get_drvdata(dev);
	int rc = 0;
#if 0
	if (curlun->prevent_medium_removal
	    && backing_file_is_open(curlun)) {
		LDBG(curlun, "eject attempt prevented\n");
		return -EBUSY;
		/* "Door is locked" */
	}
#endif
	/* Remove a trailing newline */
	if (count > 0 && buf[count - 1] == '\n')
		((char *)buf)[count - 1] = 0;
	/* Ugh! */

	/* Eject current medium */
	down_write(&fsg->filesem);
	if (backing_file_is_open(curlun)) {
		close_backing_file(curlun);
		curlun->unit_attention_data = SS_MEDIUM_NOT_PRESENT;
	}

	/* Load new medium */
	if (count > 0 && buf[0]) {
		rc = open_backing_file(curlun, buf);
		if (rc == 0)
			curlun->unit_attention_data =
				SS_NOT_READY_TO_READY_TRANSITION;
	}
	up_write(&fsg->filesem);
	return rc < 0 ? rc : count;
#endif
}

/* The write permissions and store_xxx pointers are set in fsg_bind() */
static DEVICE_ATTR(ro, 0444, show_ro, NULL);
static DEVICE_ATTR(file, 0444, show_file, NULL);


static ssize_t show_need_format(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d", need_format);
}

static ssize_t store_need_format(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	/* for future use */
	return 0;
}

static DEVICE_ATTR(need_format, 0444, show_need_format, NULL);

static ssize_t show_fs_name(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%s", format_fs_name);
}

static ssize_t show_disk_name(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%s", format_disk_name);
}

static ssize_t show_disk_label(struct device *dev, struct device_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%s", format_disk_label);
}

static DEVICE_ATTR(format_fs_name, 0444, show_fs_name, NULL);
static DEVICE_ATTR(format_disk_name, 0444, show_disk_name, NULL);
static DEVICE_ATTR(format_disk_label, 0444, show_disk_label, NULL);
/*-------------------------------------------------------------------------*/

static void fsg_release(struct kref *ref)
{
	struct fsg_dev *fsg = container_of(ref, struct fsg_dev, ref);

	kfree(fsg->luns);
	kfree(fsg);
}

static void lun_release(struct device *dev)
{
	struct fsg_dev *fsg = dev_get_drvdata(dev);

	kref_put(&fsg->ref, fsg_release);
}

static void /* __init_or_exit */ fsg_unbind(struct usb_gadget *gadget)
{
	struct fsg_dev *fsg = get_gadget_data(gadget);
	int i;
	struct lun *curlun;
	struct usb_request *req = fsg->ep0req;

	DBG(fsg, "unbind\n");
	clear_bit(REGISTERED, &fsg->atomic_bitflags);

	/* Unregister the sysfs attribute files and the LUNs */
	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		if (curlun->registered) {
			device_remove_file(&curlun->dev, &dev_attr_ro);
			device_remove_file(&curlun->dev, &dev_attr_file);
			device_unregister(&curlun->dev);
			curlun->registered = 0;
		}
	}

	/* If the thread isn't already dead, tell it to exit now */
	if (fsg->state != FSG_STATE_TERMINATED) {
		raise_exception(fsg, FSG_STATE_EXIT);
		wait_for_completion(&fsg->thread_notifier);

		/* The cleanup routine waits for this completion also */
		complete(&fsg->thread_notifier);
	}

	/* Free the data buffers */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		/* kfree(fsg->buffhds[i].buf); */
		struct fsg_buffhd *bh = &fsg->buffhds[i];
		if (bh->buf)
			kfree(bh->buf);
	}
	/* Free the request and buffer for endpoint 0 */
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(fsg->ep0, req);
	}

	set_gadget_data(gadget, NULL);
}

static int __init check_parameters(struct fsg_dev *fsg)
{
	int prot;
	int gcnum;

	/* Store the default values */
	mod_data.transport_type = USB_PR_BULK;
	mod_data.transport_name = "Bulk-only";
	mod_data.protocol_type = USB_SC_8070;
	mod_data.protocol_name = "8070i";

	if (gadget_is_at91(fsg->gadget))
		mod_data.can_stall = 0;

	if (mod_data.release == 0xffff) {	/* Parameter wasn't set */
#ifdef FPGA_VERIFY_MODE
		gcnum = 0x32;
#else
		gcnum = usb_gadget_controller_number(fsg->gadget);
#endif
		if (gcnum >= 0)
			mod_data.release = 0x0300 + gcnum;
		else {
			WARNING(fsg, "controller '%s' not recognized\n",
				fsg->gadget->name);
			mod_data.release = 0x0399;
		}
	}

	prot = simple_strtol(mod_data.protocol_parm, NULL, 0);
	return 0;
}

/* just for test */

static int do_ADFU_acmbr(struct fsg_dev *fsg)
{
	struct lun *curlun = fsg->curlun;
	u32 lba;
	struct fsg_buffhd *bh;
	int rc;
	u32 amount_left;
	loff_t file_offset, file_offset_tmp;
	unsigned int amount;
	unsigned int partial_page;
	ssize_t nread;

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	lba = 0;
	file_offset = ((loff_t) lba) << 9;

	/* Carry out the file reads */
	amount_left = fsg->data_size_from_cmnd = 0x400;
	if (unlikely(amount_left == 0))
		return -EIO;	/* No default reply */

	for (;;) {

		/* Figure out how much we need to read:
		 * Try to read the remaining amount.
		 * But don't read more than the buffer size.
		 * And don't try to read past the end of the file.
		 * Finally, if we're not at a page boundary, don't read past
		 *      the next page.
		 * If this means reading 0 then we were asked to read past
		 *      the end of file. */
		amount = min((unsigned int)amount_left, mod_data.buflen);
		amount = min((loff_t) amount,
			     curlun->file_length - file_offset);
		partial_page = file_offset & (PAGE_CACHE_SIZE - 1);
		if (partial_page > 0)
			amount = min(amount, (unsigned int)PAGE_CACHE_SIZE -
				     partial_page);

		/* Wait for the next buffer to become available */
		bh = fsg->next_buffhd_to_fill;
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;
		}

		/* If we were asked to read past the end of file,
		 * end with an empty buffer. */
		if (amount == 0) {
			curlun->sense_data =
				SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			curlun->sense_data_info = file_offset >> 9;
			curlun->info_valid = 1;
			bh->inreq->length = 0;
			bh->state = BUF_STATE_FULL;
			break;
		}

		/* Perform the read */
		file_offset_tmp = file_offset;
		nread = vfs_read(curlun->filp,
				 (char __user *)bh->buf,
				 amount, &file_offset_tmp);
		VLDBG(curlun, "file read %u @ %llu -> %d\n", amount,
		      (unsigned long long)file_offset, (int)nread);
		if (signal_pending(current))
			return -EINTR;

		if (nread < 0) {
			LDBG(curlun, "error in file read: %d\n", (int)nread);
			nread = 0;
		} else if (nread < amount) {
			LDBG(curlun, "partial file read: %d/%u\n",
			     (int)nread, amount);
			nread -= (nread & 511);	/* Round down to a block */
		}
		file_offset += nread;
		amount_left -= nread;
		fsg->residue -= nread;
		bh->inreq->length = nread;
		bh->state = BUF_STATE_FULL;

		/* If an error occurred, report it and its position */
		if (nread < amount) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info = file_offset >> 9;
			curlun->info_valid = 1;
			break;
		}

		if (amount_left == 0)
			break;	/* No more left to read */

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
			       &bh->inreq_busy, &bh->state);
		fsg->next_buffhd_to_fill = bh->next;
	}

	return -EIO;		/* No default reply */
}


static void start_adfu_transfer(struct fsg_dev *fsg)
{
	int i;
	struct fsg_buffhd *bh;
	enum fsg_state old_state;
	u8 new_config;
	struct lun *curlun;
	unsigned int exception_req_tag;
	unsigned long flag;
	struct usb_ep *ep;
	
	ep = fsg->bulk_out;

	fsg->config = 0;
	
/* 	is_high_speed = aotg_test_speed_mod();
	if (is_high_speed == 0) {
	fsg->gadget->speed = USB_SPEED_FULL;
	} */

	//do_set_config(fsg, 1);

	//printk("BEGIN TO SWITCH BUF & ACK PC WITH ACK\n");
//	aotg_ep_switchbuf(ep);

	//mdelay(50);
	//printk("AFTER SWITCH BUF & BEGIN TO REAISE EXCEPTION\n");

	first_start = 1;
	spin_lock_irqsave(&fsg->lock, flag);

	for (i = 0; i < NUM_BUFFERS; ++i) {
		bh = &fsg->buffhds[i];
		bh->state = BUF_STATE_EMPTY;
	}
	fsg->next_buffhd_to_fill = fsg->next_buffhd_to_drain = &fsg->buffhds[0];

	exception_req_tag = fsg->exception_req_tag;
	new_config = fsg->new_config;
	old_state = fsg->state;

	if (old_state == FSG_STATE_ABORT_BULK_OUT)
		fsg->state = FSG_STATE_STATUS_PHASE;
	else {
		for (i = 0; i < fsg->nluns; ++i) {
			curlun = &fsg->luns[i];
			curlun->prevent_medium_removal = 0;
			curlun->sense_data = curlun->unit_attention_data =
				SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
		fsg->state = FSG_STATE_IDLE;
	}
	
	do_set_config(fsg, 1);
	wakeup_thread(fsg);

	spin_unlock_irqrestore(&fsg->lock, flag);

	//raise_exception(fsg, 0);
}

static int __init fsg_bind(struct usb_gadget *gadget)
{
	struct fsg_dev *fsg = the_fsg;
	int rc;
	int i;
	struct lun *curlun;
	struct usb_ep *ep;
	struct usb_request *req;
	char *pathbuf, *p;
	
	
	first_start =0;

	fsg->gadget = gadget;
	set_gadget_data(gadget, fsg);
	fsg->ep0 = gadget->ep0;
	fsg->ep0->driver_data = fsg;
	rc = check_parameters(fsg);
	if (rc != 0)
		goto out;

	if (mod_data.removable) {	/* Enable the store_xxx attributes */
		dev_attr_ro.attr.mode = dev_attr_file.attr.mode = 0644;
		dev_attr_ro.store = store_ro;
		dev_attr_file.store = store_file;
	}

	dev_attr_need_format.store = store_need_format;

	/* Find out how many LUNs there should be */
	i = mod_data.nluns;
	if (i == 0)
		i = max(mod_data.num_filenames, 1u);
	if (i > MAX_LUNS) {
		ERROR(fsg, "invalid number of LUNs: %d\n", i);
		rc = -EINVAL;
		goto out;
	}

	/* Create the LUNs, open their backing files, and register the
	 * LUN devices in sysfs. */
	fsg->luns = kzalloc(i * sizeof(struct lun), GFP_KERNEL);
	if (!fsg->luns) {
		rc = -ENOMEM;
		goto out;
	}
	fsg->nluns = i;

	mod_data.file[0] = "/dev/actb";
	mod_data.file[1] = "/dev/acta";

	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		curlun->ro = mod_data.ro[i];
		curlun->dev.release = lun_release;
		curlun->dev.parent = &gadget->dev;
		curlun->dev.driver = &fsg_driver.driver;
		dev_set_drvdata(&curlun->dev, fsg);
		dev_set_name(&curlun->dev, "%s-lun%d",
			     dev_name(&gadget->dev), i);
		rc = device_register(&curlun->dev);
		if (rc != 0) {
			INFO(fsg, "failed to register LUN%d: %d\n", i, rc);
			goto out;
		}
		if ((rc = device_create_file(&curlun->dev, &dev_attr_ro)) != 0 ||
		    (rc = device_create_file(&curlun->dev, &dev_attr_file)) != 0) {
			device_unregister(&curlun->dev);
			goto out;
		}

		/* when used for adfu, we should create a sysfile to support format nand */
		rc = device_create_file(&curlun->dev, &dev_attr_need_format);
		rc = device_create_file(&curlun->dev, &dev_attr_format_fs_name);
		rc = device_create_file(&curlun->dev, &dev_attr_format_disk_name);
		rc = device_create_file(&curlun->dev, &dev_attr_format_disk_label);

		curlun->registered = 1;
		kref_get(&fsg->ref);

//		if (mod_data.file[i] && *mod_data.file[i]) {
//			rc = open_backing_file(curlun, mod_data.file[i]);
//			if (rc != 0)
//				goto out;
//		} else if (!mod_data.removable) {
//			ERROR(fsg, "no file given for LUN%d\n", i);
//			rc = -EINVAL;
//			goto out;
//		}
	}

	/* Find all the endpoints we will use */
	usb_ep_autoconfig_reset(gadget);
	ep = usb_ep_autoconfig(gadget, &fs_bulk_in_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;	/* claim the endpoint */
	fsg->bulk_in = ep;

	ep = usb_ep_autoconfig(gadget, &fs_bulk_out_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;	/* claim the endpoint */
	fsg->bulk_out = ep;

	if (transport_is_cbi()) {
		ep = usb_ep_autoconfig(gadget, &fs_intr_in_desc);
		if (!ep)
			goto autoconf_fail;
		ep->driver_data = fsg;	/* claim the endpoint */
		fsg->intr_in = ep;
	}

	/* Fix up the descriptors */
	device_desc.idVendor = cpu_to_le16(mod_data.vendor);
	device_desc.idProduct = cpu_to_le16(mod_data.product);
	device_desc.bcdDevice = cpu_to_le16(mod_data.release);

	i = (transport_is_cbi() ? 3 : 2);	/* Number of endpoints */
	intf_desc.bNumEndpoints = i;
	intf_desc.bInterfaceSubClass = mod_data.protocol_type;
	intf_desc.bInterfaceProtocol = mod_data.transport_type;
	//intf_desc.bInterfaceClass = 0x08;
	//intf_desc.bInterfaceSubClass = 0x06;
	//intf_desc.bInterfaceProtocol = 0x50;
	fs_function[i + FS_FUNCTION_PRE_EP_ENTRIES] = NULL;
	if (act_gadget_is_dualspeed(gadget)) {
		hs_function[i + HS_FUNCTION_PRE_EP_ENTRIES] = NULL;

		/* Assume endpoint addresses are the same for both speeds */
		hs_bulk_in_desc.bEndpointAddress =
			fs_bulk_in_desc.bEndpointAddress;
		hs_bulk_out_desc.bEndpointAddress =
			fs_bulk_out_desc.bEndpointAddress;
		hs_intr_in_desc.bEndpointAddress =
			fs_intr_in_desc.bEndpointAddress;
	}

	if (gadget_is_superspeed(gadget)) {
		unsigned		max_burst;
		/* Calculate bMaxBurst, we know packet size is 1024 */
		max_burst = min_t(unsigned, mod_data.buflen / 1024, 15);

		fsg_ss_bulk_in_desc.bEndpointAddress =
			fs_bulk_in_desc.bEndpointAddress;
		fsg_ss_bulk_in_comp_desc.bMaxBurst = max_burst;

		fsg_ss_bulk_out_desc.bEndpointAddress =
			fs_bulk_out_desc.bEndpointAddress;
		fsg_ss_bulk_out_comp_desc.bMaxBurst = max_burst;
	}
	if (gadget_is_otg(gadget))
		otg_desc.bmAttributes |= USB_OTG_HNP;

	rc = -ENOMEM;

	/* Allocate the request and buffer for endpoint 0 */
	fsg->ep0req = req = usb_ep_alloc_request(fsg->ep0, GFP_KERNEL);
	if (!req)
		goto out;
	req->buf = kmalloc(EP0_BUFSIZE, GFP_KERNEL);
	if (!req->buf)
		goto out;
	req->complete = ep0_complete;

	/* Allocate the data buffers */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		struct fsg_buffhd *bh = &fsg->buffhds[i];

		/* Allocate for the bulk-in endpoint.  We assume that
		 * the buffer will also work with the bulk-out (and
		 * interrupt-in) endpoint. */
		bh->buf = kmalloc(mod_data.buflen, GFP_KERNEL);
		if (!bh->buf)
			goto out;
		bh->next = bh + 1;
	}
	fsg->buffhds[NUM_BUFFERS - 1].next = &fsg->buffhds[0];

	/* This should reflect the actual gadget power source */
	usb_gadget_set_selfpowered(gadget);

	snprintf(manufacturer, sizeof manufacturer, "Actions Disk");
#if 0
	snprintf(manufacturer, sizeof manufacturer, "%s %s with %s",
		 init_utsname()->sysname, init_utsname()->release,
		 gadget->name);
#endif
	/* On a real device, serial[] would be loaded from permanent
	 * storage.  We just encode it from the driver version string. */
	if (uniSerial == 0)
	{
		for (i = 0; i < sizeof(serial) - 2; i += 2) {
			sprintf(&serial[i], "%02X", '0');
		}
		serial[0] = 0x2C;	//invalid serial
	}
	/* printk("Now The Serial is %s\n", serial); */
	fsg->thread_task = kthread_create(fsg_main_thread, fsg,
					  "file-storage-gadget");
	if (IS_ERR(fsg->thread_task)) {
		rc = PTR_ERR(fsg->thread_task);
		goto out;
	}
#if 0
	INFO(fsg, DRIVER_DESC ", version: " DRIVER_VERSION "\n");
	INFO(fsg, "Number of LUNs=%d\n", fsg->nluns);
#endif
	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		if (backing_file_is_open(curlun)) {
			p = NULL;
			if (pathbuf) {
				p = d_path(&curlun->filp->f_path,
					   pathbuf, PATH_MAX);
				if (IS_ERR(p))
					p = NULL;
			}
			LINFO(curlun, "ro=%d, file: %s\n",
			      curlun->ro, (p ? p : "(error)"));
		}
	}
	kfree(pathbuf);

	DBG(fsg, "transport=%s (x%02x)\n",
	    mod_data.transport_name, mod_data.transport_type);
	DBG(fsg, "protocol=%s (x%02x)\n",
	    mod_data.protocol_name, mod_data.protocol_type);
	DBG(fsg, "VendorID=x%04x, ProductID=x%04x, Release=x%04x\n",
	    mod_data.vendor, mod_data.product, mod_data.release);
	DBG(fsg, "removable=%d, stall=%d, buflen=%u\n",
	    mod_data.removable, mod_data.can_stall, mod_data.buflen);
	DBG(fsg, "I/O thread pid: %d\n", task_pid_nr(fsg->thread_task));

	set_bit(REGISTERED, &fsg->atomic_bitflags);

	/* Tell the thread to start working */
	wake_up_process(fsg->thread_task);
	start_adfu_transfer(fsg);

	return 0;

autoconf_fail:
	ERROR(fsg, "unable to autoconfigure all endpoints\n");
	rc = -ENOTSUPP;

out:
	fsg->state = FSG_STATE_TERMINATED;	/* The thread is dead */
	fsg_unbind(gadget);
	close_all_backing_files(fsg);
	return rc;
}

/*-------------------------------------------------------------------------*/

static void fsg_suspend(struct usb_gadget *gadget)
{
	struct fsg_dev *fsg = get_gadget_data(gadget);

	DBG(fsg, "suspend\n");
	set_bit(SUSPENDED, &fsg->atomic_bitflags);
}

static void fsg_resume(struct usb_gadget *gadget)
{
	struct fsg_dev *fsg = get_gadget_data(gadget);

	DBG(fsg, "resume\n");
	clear_bit(SUSPENDED, &fsg->atomic_bitflags);
}

/*-------------------------------------------------------------------------*/

static struct usb_gadget_driver fsg_driver = {
	.max_speed = USB_SPEED_SUPER,
	.function = (char *)longname,
	.bind = fsg_bind,
	.unbind = fsg_unbind,
	.disconnect = fsg_disconnect,
	.setup = fsg_setup,
	.suspend = fsg_suspend,
	.resume = fsg_resume,

	.driver = {
		.name = (char *)shortname,
		.owner = THIS_MODULE,
		/* .release = ... */
		/* .suspend = ... */
		/* .resume = ...  */
	},
};

static int __init fsg_alloc(void)
{
	struct fsg_dev *fsg;

	fsg = kzalloc(sizeof *fsg, GFP_KERNEL);
	if (!fsg)
		return -ENOMEM;
	spin_lock_init(&fsg->lock);
	init_rwsem(&fsg->filesem);
	kref_init(&fsg->ref);
	init_completion(&fsg->thread_notifier);

	the_fsg = fsg;
	return 0;
}

typedef void (*FUNC)(void);
static FUNC dwc3_mask_supper_speed = 0;


int __init fsg_init(void)
{
	int rc;
	struct fsg_dev *fsg;

	printk("adfus: %s %d %s %s\n", __func__, __LINE__, __DATE__, __TIME__);

	dwc3_mask_supper_speed = (FUNC)kallsyms_lookup_name("dwc3_mask_supper_speed_for_adfus");
	if(dwc3_mask_supper_speed)
		dwc3_mask_supper_speed();

#ifdef ADFUS_PROC
	adfus_proc_entry = proc_create(adfus_proc_path, 0, NULL, &proc_adfu_operations);
	if (adfus_proc_entry) {
		strcpy(probatch_phase, all_probatch_phase[0]);		
	} else {
		printk("adfus :can not create proc file\n");
	}
#endif

	adfu_success_flag = 0;

	no_finish_reply = 0;

	/* adfu need to set flag by itself, normally it is ruled by usb monitor */
	set_dwc3_gadget_plugin_flag(1);

	rc = fsg_alloc();
	if (rc != 0)
		return rc;
	fsg = the_fsg;
	rc = usb_gadget_probe_driver(&fsg_driver);
	if (rc != 0) {
		kref_put(&fsg->ref, fsg_release);
		return rc;
	}

	usb_gadget_connect(fsg->gadget);

	return rc;
}
module_init(fsg_init);

void __exit fsg_cleanup(void)
{
	struct fsg_dev *fsg = the_fsg;
	printk("file-storage unload ... \n");
	/* Unregister the driver iff the thread hasn't already done so */
	if (test_and_clear_bit(REGISTERED, &fsg->atomic_bitflags))
		usb_gadget_unregister_driver(&fsg_driver);

	/* Wait for the thread to finish up */
	wait_for_completion(&fsg->thread_notifier);



	close_all_backing_files(fsg);
	kref_put(&fsg->ref, fsg_release);

#ifdef ADFUS_PROC
	if (adfus_proc_entry)
		remove_proc_entry(adfus_proc_path, NULL);
#endif

}
module_exit(fsg_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Alan Stern");
MODULE_LICENSE("GPL");
