#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/compat.h>
#include <linux/bootafinfo.h>

#if 1
#define DVFS_PRINT(fmt, args...)\
	printk(fmt, ##args)
#else
#define DVFS_PRINT(fmt, args...)
#endif

#define DEVDRV_NAME_OWL_CHIPID	"dvfs"
#define TRANS_INPUT_KEY_MAX 40

#define OWL_CHIPID 0xc3000000
#define OWL_TRANS  0xc3000001
#define OWL_SENSOR 0xc3000002
#define OWL_USB_HSDP 0xc3000003
#define OWL_COREPLL_RECALC_RATE 0xc3000004
#define OWL_COREPLL_SET_RATE 0xc3000005
#define OWL_UPDATE_TABLE_VOLT 0xc3000006

struct dvfs_trans_t {
	unsigned char *keybox;
	int keybox_len;
	unsigned char *cert;
	int cert_len;
};

struct dvfs_trans_ret {
	struct dvfs_trans_t *dvfs;
	int ret;
};

struct dvfs_trans_t32 {
	unsigned int keybox;
	int keybox_len;
	unsigned int cert;
	int cert_len;
};

#define TRANS_INPUT_KEY_MAX 40
#define TRANS_SCHEME_INFO_MAX 8
#define TRANS_DVFS_INFO_MAX 16

#define __asmeq(x, y)  ".ifnc " x "," y " ; .err ; .endif\n\t"

static	unsigned char keybox[TRANS_INPUT_KEY_MAX];
static unsigned char cert[32+4];


noinline u64 _invoke_fn_smc(u64 function_id, u64 arg0, u64 arg1,
				u64 arg2)
{
	__asm__ __volatile__(
		__asmeq("%0", "x0")
		__asmeq("%1", "x1")
		__asmeq("%2", "x2")
		__asmeq("%3", "x3")
		"smc    #0\n"
		: "+r" (function_id)
		: "r" (arg0), "r" (arg1), "r" (arg2));

	return function_id;
}

static void __read_data(unsigned int *data, int instruction)
{
	*data = (int)_invoke_fn_smc(instruction, 0, 0, 0);
	pr_info("__read_chipid sensor = 0x%x\n", *data);
}

static void __read_chipid(unsigned int *dvfs_val)
{
	__read_data(dvfs_val, OWL_CHIPID);
}

static void __read_sensor(unsigned int *sensor)
{
	__read_data(sensor, OWL_SENSOR);
}
static void __read_usb_hsdp(unsigned int *usb_hsdp)
{
	__read_data(usb_hsdp, OWL_USB_HSDP);
}

static void trans(struct dvfs_trans_ret *arg)
{
	unsigned long ptr;

	ptr = __pa(arg->dvfs);

	arg->ret = _invoke_fn_smc(OWL_TRANS, ptr, 0, 0);
}

static int cpu0_read_data(unsigned int *data, smp_call_func_t func)
{
	int ret;

	ret = smp_call_function_single(0, func, (void *)data, 1);
	if (ret < 0) {
		pr_err("smp_call_function_single error %d\n ", ret);
		return ret;
	}
	pr_info("cpu0_read_chipid dvfs_val = 0x%x\n", *data);

	return 0;
}

static int cpu0_read_chipid(unsigned int *dvfs_val)
{
	return cpu0_read_data(dvfs_val, (smp_call_func_t)__read_chipid);
}

static int cpu0_read_sensor(unsigned int *sensor)
{
	return cpu0_read_data(sensor, (smp_call_func_t)__read_sensor);
}


static int cpu0_read_usb_hsdp(unsigned int *usb_hsdp)
{
	return cpu0_read_data(usb_hsdp, (smp_call_func_t)__read_usb_hsdp);
}


int owl_read_sensor_data(unsigned int *data)
{
	return cpu0_read_sensor(data);
}
EXPORT_SYMBOL_GPL(owl_read_sensor_data);


int owl_afi_get_sensor(struct chipid_data *data)
{
	int sensor;
	int ret;
	ret = cpu0_read_sensor(&sensor);
	if (ret < 0) {
		pr_err("read sensor data error\n");
		memset(data, 0, sizeof(struct chipid_data));
		return ret;
	}

	memcpy(data->cpu_sensor, (char *)&sensor, 2);
	memcpy(data->vdd_sensor, (char *)&sensor + 2, 2);
	return 0;
}
EXPORT_SYMBOL_GPL(owl_afi_get_sensor);


int owl_get_usb_hsdp(unsigned int *usb_hsdp)
{
	int ret;

	ret = cpu0_read_usb_hsdp(usb_hsdp);
	if (ret < 0) {
		pr_err("read usb_hsdp data error\n");
		*usb_hsdp = 0;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(owl_get_usb_hsdp);


static void __owl_corepll_recalc_rate_smc(unsigned long *rate)
{
	*rate = (unsigned long)_invoke_fn_smc(OWL_COREPLL_RECALC_RATE, 0, 0, 0);
}

unsigned long _owl_corepll_recalc_rate(void)
{
	unsigned long rate;
	smp_call_function_single(0,
				(smp_call_func_t)__owl_corepll_recalc_rate_smc,
				(void *)&rate, 1);
	return rate;
}
EXPORT_SYMBOL_GPL(_owl_corepll_recalc_rate);

int _owl_corepll_set_rate_smc(void *rate)
{
	int ret;
	ret = (int)_invoke_fn_smc(OWL_COREPLL_SET_RATE, __pa(rate), 0, 0);
	return ret;
}

int _owl_corepll_set_rate(unsigned int rate)
{
	return smp_call_function_single(0,
				(smp_call_func_t)_owl_corepll_set_rate_smc,
				(void *)&rate, 1);
}
EXPORT_SYMBOL_GPL(_owl_corepll_set_rate);


static int __owl_update_table_volt_smc(unsigned long *tables)
{
	int ret;
	ret = (int)_invoke_fn_smc(OWL_UPDATE_TABLE_VOLT, __pa(tables), 0, 0);
	if (ret < 0) {
		pr_err("%s error %d\n ", __func__, ret);
		return ret;
	}

	return 0;
}

int owl_update_table_volt(struct cpu0_opp_table *table, int table_size)
{
	int i;
	struct cpu0_opp_table_arry tables;

	tables.table_size = table_size;
	for (i = 0; i < table_size; i++)
		tables.table[i] = table[i];

	smp_call_function_single(0,
				(smp_call_func_t)__owl_update_table_volt_smc,
				(void *)&tables, 1);

	for (i = 0; i < table_size; i++)
		table[i] = tables.table[i];
	return 0;
}
EXPORT_SYMBOL_GPL(owl_update_table_volt);

static int cpu0_trans(struct dvfs_trans_t *arg)
{
	int ret;
	struct dvfs_trans_ret dvfs_ret;

	dvfs_ret.dvfs = arg;
	ret = smp_call_function_single(0,
						(smp_call_func_t)trans,
						(void *)&dvfs_ret, 1);
	if (ret < 0) {
		pr_err("smp_call_function_single error %d\n ", ret);
		return ret;
	}

	return dvfs_ret.ret;
}

static long owl_chipid_ioctl(
		struct file *file,
		unsigned int cmd,
		unsigned long arg)
{
	unsigned int val = 0;
	int ret = 0;
	struct dvfs_trans_t temp;
	int keybox_len;

	switch (cmd) {
	case 0xddff0001:
		ret = cpu0_read_chipid(&val);
		if (ret < 0) {
			pr_err("read chipid error\n");
			return -EFAULT;
		}
		ret = copy_to_user((void __user *)arg, &val, sizeof(int));
		if (ret) {
			DVFS_PRINT("copy_ret:%d\n", ret);
			return -EFAULT;
		}
		break;
	case 0xddff0002:
		ret = copy_from_user(&temp,
					(void *)arg,
					sizeof(struct dvfs_trans_t));
		if (ret) {
			DVFS_PRINT("---err,copy_ret:0x%x---\n", ret);
			return -EFAULT;
		}

		if (temp.keybox_len > TRANS_INPUT_KEY_MAX) {
			DVFS_PRINT("---keybox_len too long---\n");
			return -EINVAL;
		}

		keybox_len = temp.keybox_len;
		pr_info("key box len = 0x%x\n", keybox_len);
		ret = copy_from_user(keybox,
					(void __user *)temp.keybox,
					keybox_len);
		if (ret) {
			DVFS_PRINT("---err,copy_ret:0x%x---\n", ret);
			return -EFAULT;
		}

		temp.keybox = (unsigned char *)((unsigned long)__pa(keybox));
		temp.cert = (unsigned char *)((unsigned long)__pa(cert));
		ret = cpu0_trans(&temp);
		if (ret < 0) {
			pr_err("cert error\n");
			return ret;
		}
		temp.cert = cert;

		((struct dvfs_trans_t *)arg)->cert_len = temp.cert_len;
		ret = copy_to_user(
				((struct dvfs_trans_t *)arg)->cert,
				temp.cert,
				temp.cert_len);
		if (ret) {
			DVFS_PRINT("---copy_to_user err---\n");
			return -EFAULT;
		}
		break;

	default:
		return -EIO;
	}

	return 0;
}


static int owl_chipid_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int owl_chipid_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int compat_get_dvfs_trans(
		struct dvfs_trans_t32 __user *dvfs32,
		struct dvfs_trans_t __user *dvfs)
{
	compat_int_t i;
	compat_ulong_t ul;
	int err;

	err = get_user(ul, &dvfs32->keybox);
	err |= put_user(ul, &dvfs->keybox);
	err |= get_user(i, &dvfs32->keybox_len);
	err |= put_user(i, &dvfs->keybox_len);
	err |= get_user(ul, &dvfs32->cert);
	err |= put_user(ul, &dvfs->cert);
	err |= get_user(i, &dvfs32->cert_len);
	err |= put_user(i, &dvfs->cert_len);

	return err;
}

static int compat_put_dvfs_trans(
		struct dvfs_trans_t32 __user *dvfs32,
		struct dvfs_trans_t __user *dvfs)
{
	compat_int_t i;
	compat_ulong_t ul;
	int err;

	err = get_user(ul, &dvfs->keybox);
	err |= put_user(ul, &dvfs32->keybox);
	err |= get_user(i, &dvfs->keybox_len);
	err |= put_user(i, &dvfs32->keybox_len);
	err |= get_user(ul, &dvfs->cert);
	err |= put_user(ul, &dvfs32->cert);
	err |= get_user(i, &dvfs->cert_len);
	err |= put_user(i, &dvfs32->cert_len);

	return err;
}

static long owl_chipid_ioctl_compat(
		struct file *file,
		unsigned int cmd,
		unsigned long arg)
{
	int ret;
	struct dvfs_trans_t32 __user *dvfs32;
	struct dvfs_trans_t *dvfs;
	int err;

	switch (cmd) {
	case 0xddff0001:
		ret = owl_chipid_ioctl(file, cmd,
		(unsigned long) compat_ptr(arg));
		return ret;
	case 0xddff0002:
		dvfs32 = compat_ptr(arg);
		dvfs = compat_alloc_user_space(sizeof(*dvfs));
		if (dvfs == NULL)
			return -EFAULT;

		err = compat_get_dvfs_trans(dvfs32, dvfs);
		if (err)
			return err;

		ret = owl_chipid_ioctl(file, cmd, (unsigned long)dvfs);

		err = compat_put_dvfs_trans(dvfs32, dvfs);

		return ret ? ret : err;
	default:
		return -ENOIOCTLCMD;
	}
}


static const struct file_operations owl_chipid_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = owl_chipid_ioctl,
	.compat_ioctl = owl_chipid_ioctl_compat,
	.open = owl_chipid_open,
	.release = owl_chipid_release,
};

static struct miscdevice owl_chipid_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVDRV_NAME_OWL_CHIPID,
	.fops = &owl_chipid_fops,
};


static int owl_chipid_init(void)
{
	int ret;

	DVFS_PRINT("%s\n", __func__);

	ret = misc_register(&owl_chipid_miscdevice);
	if (ret) {
		DVFS_PRINT("register owl_chipid misc device failed!\n");
		goto err;
	}
	return 0;
err:
	return ret;
}

static void owl_chipid_exit(void)
{
	DVFS_PRINT("%s\n", __func__);
	misc_deregister(&owl_chipid_miscdevice);
}

module_init(owl_chipid_init);
module_exit(owl_chipid_exit);

MODULE_AUTHOR("Actions Semi Inc");
MODULE_DESCRIPTION("owl_chipid kernel module");
MODULE_LICENSE("GPL");
