#define CALIBFILE	"/data/data/com.actions.sensor.calib/files/gsensor_calib.txt"
#define USE_DTS
#define I2C0 "/i2c@b0170000"
#define I2C1 "/i2c@b0174000"
#define I2C2 "/i2c@b0178000"
#define I2C3 "/i2c@b017c000"
char gsensor_name[32];
static char adaplist[4][64] = {I2C0, I2C1, I2C2, I2C3};
static char *adap_uselist[4];	/*the i2c adapter of used.*/
static int adapdetect_flag;


static int gsensor_read_file(char *path, char *buf, int size)
{
	struct file *filp;
	loff_t len, offset;
	int ret = 0;
	mm_segment_t fs;

	filp = filp_open(path, O_RDWR, 0777);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto out;
	}

	len = vfs_llseek(filp, 0, SEEK_END);
	if (len > size) {
		len = size;
	}

	offset = vfs_llseek(filp, 0, SEEK_SET);

	fs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_read(filp, (char __user *)buf, (size_t)len, &(filp->f_pos));

	set_fs(fs);

	filp_close(filp, NULL);
out:
	return ret;
}

static void detect_i2cadapter(void)
{
	struct device_node *np;
	int id = 0, i;
	printk("Detect I2C Adpater!\n");
	if (adapdetect_flag == 1) {
		printk("Have been detect i2c adapter!\n");
		return;
	}

	for (i = 0; i < 4; i++) {
	np = of_find_node_by_path(adaplist[i]);
		if (of_device_is_available(np)) {
			printk("I2C Adapter of %s is enable!\n", np->full_name);
			adap_uselist[id] = adaplist[i];
			id++;
		}
	}
	adapdetect_flag = 1;
}

static int getadapid(struct i2c_client *client)
{
	return (client->adapter)->nr;
}

static int gsensor_read_calibration(int *a)
{
	char buffer[16];
	int ret = 0;
	int data[3];

	ret = gsensor_read_file(CALIBFILE, buffer, sizeof(buffer));
	if (ret <= 0) {
		printk(KERN_ERR "gsensor calibration file not exist!\n");
		return -1;
	}

	sscanf(buffer, "%d %d %d", &data[0], &data[1], &data[2]);
	printk(KERN_INFO "user cfg_calibration: %d %d %d\n", data[0], data[1], data[2]);
	*(a) = data[0];
	*(a+1) = data[1];
	*(a+2) = data[2];
	return 0;
}

static int gsensor_dt_position(char *sensor_name, struct i2c_client *client)
{
	struct device_node *np;
	const char *buf;
	long position;
	np = of_find_compatible_node(NULL, NULL, sensor_name);
	if (np == NULL) {
		pr_err("gsensor_drv: No %s node found in dts.load position err,default -1\n", sensor_name);
		return -1;
	}
	if (of_property_read_string(np, "position", &buf)) {
		pr_err("gsensor_drv: failed to read position,default -1\n");
		return -1;
	}
	strict_strtol(buf, 10, &position);
	printk("gsensor dt read position is %ld\n", position);
	return position;
}

static int gsensor_dt_calib(char *name, int **ret_calib, struct i2c_client *client)
{
	struct device_node *np;
	int calib[3];
	np = of_find_compatible_node(NULL, NULL, name);
	if (np == NULL) {
		pr_err("gsensor_drv: No %s node found in dts\n", name);
		return -1;
	}

	if (of_property_read_u32_array(np, "calibration", calib, 3)) {
		pr_err("gsensor_drv: failed to read calibration\n");
		return -1;
	}
	*ret_calib = calib[0];
	*(ret_calib + 1) = calib[1];
	*(ret_calib + 2) = calib[2];
	return 0;
}

int gsensor_dt_regulator(char *name, int **ret_regulator, struct i2c_client *client)
{
	struct device_node *np;
	int calib[3];
		char *buf;
		int ret;

	np = of_find_compatible_node(NULL, NULL, name);
	if (np == NULL) {
		pr_err("gsensor_drv: No %s node found in dts\n", name);
		return -1;
	}

	/* load regulator */
	if (of_find_property(np, "regulator", NULL)) {
		ret = of_property_read_string(np, "regulator", ret_regulator);
		if (ret < 0) {
			printk("can not read gsensor power source\n");
						return ret;
		}
				printk("read gsensor power source success!%s\n", *ret_regulator);
	}

	return 0;
}

static int gsensor_dt_adap_id(char *sensor_name)
{
	struct device_node *np;
	char *buf;
	long adap_id;
	np = of_find_compatible_node(NULL, NULL, sensor_name);
	if (np == NULL) {
		pr_err("gsensor_drv: No %s node found in dts.load position err,default -1\n", sensor_name);
		return -1;
	}
	if (of_property_read_string(np, "i2c_adap_id", &buf)) {
		pr_err("gsensor_drv: failed to read i2c_adap_id,default -1\n");
		return -1;
	}
	strict_strtol(buf, 10, &adap_id);
	printk("gsensor dt read i2c_adap_id is %d\n", adap_id);
	return adap_id;
}

/*

static int gsensor_dt_position(char* sensor_name,struct i2c_client* client)
{
	detect_i2cadapter();
	struct device_node *np;
	char* buf;
	unsigned long temp;
	char adapname[64];
	int adapterid;
	adapterid=getadapid(client);
	if (adap_uselist[adapterid]==NULL)
			return -1;
	strcpy(adapname,adap_uselist[adapterid]);
	strcat(adapname,"/gsensor");
	np=of_find_node_by_path(adapname);
	if (np==NULL){
		printk("gsensor can not find the adapter!load position err,default -1\n");
		return -1;
	}
	np = of_find_compatible_node(np->child, NULL, sensor_name);
	if (np==NULL){
		pr_err("gsensor_drv: No %s node found in dts.load position err,default -1\n",sensor_name);
		return -1;
	}
	if (of_property_read_string(np, "position", &buf)) {
		pr_err("gsensor_drv: failed to read position,default -1\n");
		return -1;
	}
	strict_strtol(buf, 10, &temp);
	printk("gsensor dt read position is %d\n",(int)temp);
	return (int)temp;
}

int gsensor_dt_calib(char* name,int* ret_calib,struct i2c_client* client)
{
	detect_i2cadapter();
	struct device_node *np;
	int calib[3],adapterid;
	char adapname[64];
	adapterid=getadapid(client);
	strcpy(adapname,adap_uselist[adapterid]);
	strcat(adapname,"/gsensor");
	np=of_find_node_by_path(adapname);
	if (np==NULL){
		printk("gsensor can not find the adapter!\n");
		return -1;
	}
	np = of_find_compatible_node(np->child, NULL, name);
	if (np==NULL){
		pr_err("gsensor_drv: No %s node found in dts\n",name);
		return -1;
	}

	if (of_property_read_u32_array(np, "calibration", calib,3)) {
		pr_err("gsensor_drv: failed to read calibration\n");
		return -1;
	}
	*ret_calib=calib[0];
	*(ret_calib+1)=calib[1];
	*(ret_calib+2)=calib[2];
	return 0;
}
*/

/*
void setname(char* name)
{
	strcpy(gsensor_name,name);
}
static int gsensor_detect(struct i2c_client *client, struct i2c_board_info *info)
{
  struct i2c_adapter *adapter = client->adapter;
  printk("gsensor detect %s\n",gsensor_name);
  if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
  {
	printk(KERN_INFO "gsensor %s not support I2C_FUNC_SMBUS! \n", gsensor_name);
	return -ENODEV;
  }
  strlcpy(info->type, gsensor_name, I2C_NAME_SIZE);
  return 0;
}

struct i2c_client*	clientinit(char* name,int address)
{
	struct i2c_adapter *adap=i2c_get_adapter(adapterid);
	if (adap==NULL)
	{
		printk("Get adap Error!\n");
		return NULL;
	}
	struct i2c_board_info info={
	.addr=address,};
	strcpy(info.type,name);
	struct i2c_client* client=i2c_new_device(adap,&info);
	if (client==NULL)
		printk("Not Found i2c client!adapterid is %d,client name is %s,client address is %d\n",adapterid,info.type,info.addr);
	else
		printk("Add i2c client!adapterid is %d,client name is %s,client address is %d\n",adapterid,info.type,info.addr);
	return client;
}
*/
/*

static int gsensor_read_positionfile()
{
	char buffer[16];
	int position,ret;
	ret = gsensor_read_file(POSITION_FILE, buffer, sizeof(buffer));
	if (ret <= 0) {
		printk(KERN_ERR "gsensor position file not exist!\n");
		return 1;
	}

	sscanf(buffer, "%d", &position);
	return position;
}

static int gsensor_read_adapterfile()
{
	char buffer[16];
	int adapter,ret;
	ret = gsensor_read_file(ADAPTER_FILE, buffer, sizeof(buffer));
	if (ret <= 0) {
		printk(KERN_ERR "gsensor adapter file not exist!\n");
		return 1;
	}

	sscanf(buffer, "%d", &adapter);
	return adapter;
}

int check_id(struct i2c_client *client)
{
	unsigned char tempvalue;
	tempvalue = i2c_smbus_read_byte_data(client, idreg);
	if (tempvalue == chipid) {
		printk(KERN_INFO "gsensor %s detected!\n",sensor_name);
	} else{
		printk(KERN_INFO "gsensor %s not found! \n", sensor_name);
		return -1;
	}
	return 0;
}

void tran_sensorinfo(unsigned short address,char id,char* name,struct i2c_driver* driver,int en)
{
	printk("gsensor %s module_init!\n",name);
	idreg=address;
	strcpy(gsensor_name,name);
	chipid=id;
	gsensor_driver=driver;
	chipid_en=en;
}*/



