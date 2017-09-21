#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/io.h>

#include "./../host_comm/owl_device.h"

#define CAMERA_COMMON_NAME      "sensor_common"
#define SI_FDT_COMPATIBLE       "actions,s700-isp"
#define ISP_FDT_COMPATIBLE      "actions,s900-isp"

#define CAMERA_MODULE_CLOCK     24000000
#define SI_MODULE_CLOCK         24000000
#define CAMERA_REAR_NAME        "rear_camera"
#define CAMERA_FRONT_NAME       "front_camera"

static int camera_front_offset = -1;
static int camera_rear_offset = -1;

static struct clk *g_isp_clk;

static char camera_front_name[32];
static char camera_rear_name[32];

struct sensor_pwd_info g__spinfo;
struct module_regulators g_isp_ir;
static int g_sens_channel;	/*sens0 or sens1 */
static struct i2c_adapter *g_adap;
int board_type_num;		/*0 : evb  1: ces */
int mf;				/* mirror flip */
bool gpio_flash_cfg_exist;

static int get_parent_node_id(struct device_node *node,
			      const char *property, const char *stem)
{
	struct device_node *pnode;
	unsigned int value = -ENODEV;

	pnode = of_parse_phandle(node, property, 0);
	if (NULL == pnode) {
		DBG_INFO("fail to get node[%s]", property);
		return value;
	}
	value = of_alias_get_id(pnode, stem);

	return value;
}

static int init_common(void)
{
	struct device_node *fdt_node;
	int i2c_adap_id = 0;
	struct dts_regulator *dr = &g_isp_ir.avdd.regul;

	/*get i2c adapter */
	fdt_node = of_find_compatible_node(NULL, NULL, CAMERA_COMMON_NAME);
	if (NULL == fdt_node) {
		DBG_ERR("err: no [" CAMERA_COMMON_NAME "] in dts");
		return -1;
	}
	DBG_INFO("the name : %s\n", fdt_node->name);
	i2c_adap_id = get_parent_node_id(fdt_node, "i2c_adapter", "i2c");
    if (i2c_adap_id < 0) {
#if USE_AS_FRONT
	    i2c_adap_id = get_parent_node_id(fdt_node, "front_i2c_adapter", "i2c");
#endif

#if USE_AS_REAR
	    i2c_adap_id = get_parent_node_id(fdt_node, "rear_i2c_adapter", "i2c");
#endif
	    if (i2c_adap_id < 0) {
		    DBG_ERR("fail to get i2c adapter id");
            return -1;
        }
    }

	g_adap = i2c_get_adapter(i2c_adap_id);

	/*get senors channel */
	if (of_property_read_u32(fdt_node, "channel", &g_sens_channel))
		g_sens_channel = 0;

	g__spinfo.flag = 0;
	g__spinfo.gpio_rear.num = -1;
	g__spinfo.gpio_front.num = -1;
	g__spinfo.gpio_front_reset.num = -1;
	g__spinfo.gpio_rear_reset.num = -1;
	g__spinfo.ch_clk[HOST_MODULE_CHANNEL_0] = NULL;
	g__spinfo.ch_clk[HOST_MODULE_CHANNEL_1] = NULL;

	dr->regul = NULL;
	g_isp_ir.avdd_use_gpio = 0;
	g_isp_ir.dvdd_use_gpio = 0;
	g_isp_ir.dvdd.regul = NULL;

	return 0;
}

inline void set_gpio_level(struct dts_gpio *gpio, bool value)
{
	if (value)
		gpio_direction_output(gpio->num, gpio->active_level);
	else
		gpio_direction_output(gpio->num, !gpio->active_level);
}

static int gpio_init(struct device_node *fdt_node,
		     const char *gpio_name, struct dts_gpio *gpio, bool value)
{
	enum of_gpio_flags flags;

	if (!of_find_property(fdt_node, gpio_name, NULL)) {
		DBG_INFO("no config gpios:%s", gpio_name);
		goto fail;
	}
	gpio->num = of_get_named_gpio_flags(fdt_node, gpio_name, 0, &flags);
	gpio->active_level = !(!(flags & OF_GPIO_ACTIVE_LOW));
	DBG_INFO("%s: num-%d, active-%s", gpio_name, gpio->num,
		 gpio->active_level ? "high" : "low");
	if (gpio_request(gpio->num, gpio_name)) {
		DBG_ERR("fail to request gpio [%d]", gpio->num);
		gpio->num = -1;
		goto fail;
	}

	set_gpio_level(gpio, value);
	DBG_INFO("gpio value: 0x%x", gpio_get_value(gpio->num));

	return 0;
 fail:
	return -1;
}

static void gpio_exit(struct dts_gpio *gpio, bool value)
{
	if (gpio->num >= 0) {
		set_gpio_level(gpio, value);
		gpio_free(gpio->num);
	}
}

static int regulator_init(struct device_node *fdt_node,
			  const char *regul_name, const char *scope_name,
			  struct dts_regulator *dts_regul)
{
	unsigned int scope[2];
	const char *regul = NULL;

	if (of_property_read_string(fdt_node, regul_name, &regul)) {
		DBG_INFO("don't config %s", regul_name);
		goto fail;
	}
	DBG_INFO("%s", regul ? regul : "NULL");

	if (of_property_read_u32_array(fdt_node, scope_name, scope, 2)) {
		DBG_ERR("fail to get %s", scope_name);
		goto fail;
	}
	DBG_INFO("min-%d, max-%d", scope[0], scope[1]);
	dts_regul->min = scope[0];
	dts_regul->max = scope[1];

	dts_regul->regul = regulator_get(NULL, regul);
	if (IS_ERR(dts_regul->regul)) {
		dts_regul->regul = NULL;
		DBG_ERR("get regulator failed");
		goto fail;
	}

	regulator_set_voltage(dts_regul->regul, dts_regul->min, dts_regul->max);
	return 0;

 fail:
	return -1;

}

static inline void regulator_exit(struct dts_regulator *dr)
{
	regulator_put(dr->regul);
	dr->regul = NULL;
}

static int isp_regulator_init(struct device_node *fdt_node,
			      struct module_regulators *ir)
{
	const char *avdd_src = NULL;
    const char *dvdd_src = NULL;

	/*DVDD*/
	struct dts_gpio *dvdd_gpio = &ir->dvdd_gpio;
	/* poweroff */
	if (!gpio_init(fdt_node, "dvdd-gpios", dvdd_gpio, GPIO_LOW))
		ir->dvdd_use_gpio = 1;
	else
		ir->dvdd_use_gpio = 0;

	/*AVDD*/
	if (of_property_read_string(fdt_node, "avdd-src", &avdd_src)) {
		DBG_INFO("get avdd-src faild");
#if USE_AS_REAR
	    if (of_property_read_string(fdt_node, "rear-avdd-src", &avdd_src)) {
		    DBG_INFO("get rear-avdd-src faild");
        }
#endif
#if USE_AS_FRONT
	    if (of_property_read_string(fdt_node, "front-avdd-src", &avdd_src)) {
		    DBG_INFO("get front-avdd-src faild");
        }
#endif
        if (avdd_src == NULL) {
		    DBG_ERR("get all avdd-src faild");
		    goto fail;
        }
	}

	if (!strcmp(avdd_src, "regulator")) {
		DBG_INFO("avdd using regulator");
		ir->avdd_use_gpio = 0;

#if USE_AS_REAR
		if (regulator_init(fdt_node, "avdd-regulator",
				   "avdd-regulator-scope", &ir->avdd.regul) && \
            regulator_init(fdt_node, "rear-avdd-regulator",
				   "rear_avdd-regulator-scope", &ir->avdd.regul))
			goto free_dvdd;
#endif
#if USE_AS_FRONT
		if (regulator_init(fdt_node, "avdd-regulator",
				   "avdd-regulator-scope", &ir->avdd.regul) && \
            regulator_init(fdt_node, "front-avdd-regulator",
				   "rear_avdd-regulator-scope", &ir->avdd.regul))
			goto free_dvdd;
#endif
	} else if (!strcmp(avdd_src, "gpio")) {
		struct dts_gpio *gpio = &ir->avdd.gpio;
		ir->avdd_use_gpio = 1;
#if USE_AS_REAR
		/* poweroff */
		if (gpio_init(fdt_node, "avdd-gpios", gpio, GPIO_HIGH) && \
            gpio_init(fdt_node, "rear-avdd-gpios", gpio, GPIO_HIGH))
			goto fail;
#endif
#if USE_AS_FRONT
		/* poweroff */
		if (gpio_init(fdt_node, "avdd-gpios", gpio, GPIO_HIGH) && \
            gpio_init(fdt_node, "front-avdd-gpios", gpio, GPIO_HIGH))
			goto fail;
#endif

		g__spinfo.gpio_power.num = gpio->num;
		g__spinfo.gpio_power.active_level = gpio->active_level;
	} else {
		DBG_ERR("needn't operate avdd manually");
	}

	if (of_property_read_string(fdt_node, "dvdd-src", &dvdd_src)) {
		DBG_INFO("get dvdd-src faild");
#if USE_AS_REAR
	    if (of_property_read_string(fdt_node, "rear-dvdd-src", &dvdd_src)) {
		    DBG_INFO("get rear-dvdd-src faild");
        }
#endif
#if USE_AS_FRONT
	    if (of_property_read_string(fdt_node, "front-dvdd-src", &dvdd_src)) {
		    DBG_INFO("get front-dvdd-src faild");
        }
#endif
        if (dvdd_src == NULL) {
		    DBG_INFO("get all dvdd-src faild");
        }
    }

    if (dvdd_src != NULL) {
        if (!strcmp(dvdd_src, "regulator")) {
            ir->dvdd_use_gpio = 0;
#if USE_AS_REAR
            if (regulator_init(fdt_node, "dvdd-regulator",
                        "dvdd-regulator-scope", &ir->dvdd) && \
                regulator_init(fdt_node, "rear-dvdd-regulator",
                        "rear-dvdd-regulator-scope", &ir->dvdd))
                goto free_dvdd;
#endif
#if USE_AS_FRONT
            if (regulator_init(fdt_node, "dvdd-regulator",
                        "dvdd-regulator-scope", &ir->dvdd) && \
                regulator_init(fdt_node, "front-dvdd-regulator",
                        "front-dvdd-regulator-scope", &ir->dvdd))
                goto free_dvdd;
#endif
        }
    }

	return 0;

 free_dvdd:
	regulator_exit(&(ir->dvdd));
 fail:
	return -1;
}

static void isp_regulator_exit_power_off(struct module_regulators *ir)
{
	if (ir->dvdd_use_gpio)
		gpio_exit(&ir->dvdd_gpio, GPIO_LOW);

	if (ir->dvdd.regul)
		regulator_exit(&ir->dvdd);

	if (ir->avdd_use_gpio) {
		gpio_exit(&ir->avdd.gpio, GPIO_LOW);
	} else {
		struct dts_regulator *dr = &ir->avdd.regul;

		if (dr->regul)
			regulator_exit(dr);
	}
}

static void isp_regulator_exit_power_hold(struct module_regulators *ir)
{
	if (ir->dvdd_use_gpio)
		gpio_exit(&ir->dvdd_gpio, GPIO_HIGH);

	if (ir->dvdd.regul)
		regulator_exit(&ir->dvdd);

	if (ir->avdd_use_gpio) {
		gpio_exit(&ir->avdd.gpio, GPIO_HIGH);
	} else {
		struct dts_regulator *dr = &ir->avdd.regul;

		if (dr->regul)
			regulator_exit(dr);
	}
}

static void isp_regulator_enable(struct module_regulators *ir)
{
	int ret = 0;
	if (ir->dvdd.regul) {
		ret = regulator_enable(ir->dvdd.regul);
		mdelay(5);
	}

	if (ir->dvdd_use_gpio)
		set_gpio_level(&ir->dvdd_gpio, GPIO_HIGH);
	/* avdd enbale */
	if (ir->avdd_use_gpio) {
		set_gpio_level(&ir->avdd.gpio, GPIO_HIGH);
	} else {
		struct dts_regulator *dr = &ir->avdd.regul;
		if (dr->regul) {
			ret = regulator_enable(dr->regul);
			mdelay(5);
		}
	}
}

static void isp_regulator_disable(struct module_regulators *ir)
{
	if (ir->dvdd_use_gpio)
		set_gpio_level(&ir->dvdd_gpio, GPIO_LOW);

	if (ir->dvdd.regul)
		regulator_disable(ir->dvdd.regul);

	if (ir->avdd_use_gpio) {
		set_gpio_level(&ir->avdd.gpio, GPIO_LOW);
	} else {
		struct dts_regulator *dr = &ir->avdd.regul;
		if (dr->regul)
			regulator_disable(dr->regul);
	}
}

static int isp_gpio_init(struct device_node *fdt_node,
			 struct sensor_pwd_info *spinfo)
{
	const char *sensors = NULL;
	const char *board_type = NULL;

	if (of_property_read_string(fdt_node, "board_type", &board_type)) {
		DBG_ERR("get board_type faild");
		goto free_reset;
	}
	if (!strcmp(board_type, "ces")) {
		if (gpio_init(fdt_node, "front-reset-gpios",
			      &spinfo->gpio_front_reset, GPIO_LOW)) {
			goto fail;
		}
		if (gpio_init(fdt_node, "rear-reset-gpios",
			      &spinfo->gpio_rear_reset, GPIO_LOW)) {
			goto fail;
		}
		board_type_num = 1;
	} else if (!strcmp(board_type, "evb")) {
		if (gpio_init(fdt_node, "reset-gpios",
			      &spinfo->gpio_front_reset, GPIO_LOW)) {
			goto fail;
		}
		spinfo->gpio_rear_reset.num = spinfo->gpio_front_reset.num;
		spinfo->gpio_rear_reset.active_level =
		    spinfo->gpio_front_reset.active_level;
		board_type_num = 0;
	} else {
		DBG_ERR("get board type faild");
		return -1;
	}

	if (of_property_read_string(fdt_node, "sensors", &sensors)) {
		DBG_ERR("get sensors faild");
		goto free_reset;
	}

	if (!strcmp(sensors, "front")) {
		/* default is power-down */
		if (gpio_init(fdt_node, "pwdn-front-gpios",
			      &spinfo->gpio_front, GPIO_LOW)) {
			goto free_reset;
		}
		spinfo->flag = SENSOR_FRONT;
	} else if (!strcmp(sensors, "rear")) {
		if (gpio_init(fdt_node, "pwdn-rear-gpios",
			      &spinfo->gpio_rear, GPIO_LOW)) {
			goto free_reset;
		}
		spinfo->flag = SENSOR_REAR;
	} else if (!strcmp(sensors, "dual")) {
		if (gpio_init(fdt_node, "pwdn-front-gpios",
			      &spinfo->gpio_front, GPIO_LOW)) {
			goto free_reset;
		}
		if (gpio_init(fdt_node, "pwdn-rear-gpios",
			      &spinfo->gpio_rear, GPIO_LOW)) {
			goto free_reset;
		}
		spinfo->flag = SENSOR_DUAL;
	} else {
		DBG_ERR("sensors of dts is wrong");
		goto free_reset;
	}

	return 0;

 free_reset:
	DBG_ERR("isp_gpio_init error!!!");
	gpio_exit(&spinfo->gpio_front_reset, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
 fail:
	return -1;
}

static void isp_gpio_exit_power_off(struct sensor_pwd_info *spinfo)
{
	/*only free valid gpio, so no need to check its existence. */
	gpio_exit(&spinfo->gpio_front, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear, GPIO_LOW);
	if (board_type_num) {
		gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
		gpio_exit(&spinfo->gpio_front_reset, GPIO_LOW);
	} else {
		gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
	}
}

static void isp_gpio_exit_power_hold(struct sensor_pwd_info *spinfo)
{
	/*only free valid gpio, so no need to check its existence. */
	gpio_exit(&spinfo->gpio_front, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear, GPIO_LOW);
	if (board_type_num) {
		gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
		gpio_exit(&spinfo->gpio_front_reset, GPIO_LOW);
	} else {
		gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
	}
}

static int isp_clk_init(void)
{
	struct clk *tmp = NULL;
	int ret = 0;

	/*get isp clock first, if not exist, get si clock. */
	tmp = clk_get(NULL, "bisp");
	if (IS_ERR(tmp)) {
		tmp = clk_get(NULL, "si");
		if (IS_ERR(tmp)) {
			ret = PTR_ERR(tmp);
			g_isp_clk = NULL;
			DBG_ERR("get clock error (%d)", ret);
			return ret;
		}
	}
	g_isp_clk = tmp;
	mdelay(1);

	return ret;
}

static int isp_clk_enable(void)
{
	int ret = 0;

	if (g_isp_clk != NULL) {
		clk_prepare(g_isp_clk);
		ret = clk_enable(g_isp_clk);	/*enable clk */
		if (ret)
			DBG_ERR("si clock enable error (%d)", ret);
		/*set isp work freq */
		ret = clk_set_rate(g_isp_clk, SI_MODULE_CLOCK);
	}
	return ret;
}

static void isp_clk_disable(void)
{
	if (g_isp_clk != NULL) {
		clk_disable(g_isp_clk);
		clk_unprepare(g_isp_clk);
		clk_put(g_isp_clk);
		g_isp_clk = NULL;
	}
}

int detect_init(void)
{
	struct device_node *fdt_node = NULL;
	int ret = 0;

	fdt_node = of_find_compatible_node(NULL, NULL, "flashlight");
	if (NULL == fdt_node) {
		DBG_INFO("no [flashlight] in dts\n");
		gpio_flash_cfg_exist = false;
	} else {
		gpio_flash_cfg_exist = true;
	}
	if (init_common())
		return -1;
	/*init isp clock */
	ret = isp_clk_init();
	if (ret) {
		DBG_ERR("init isp clock error");
		goto exit;
	}
	/*find isp node first,if not exist,find si node,if not exist,return; */
	fdt_node = of_find_compatible_node(NULL, NULL, ISP_FDT_COMPATIBLE);
	if (NULL == fdt_node) {
		DBG_INFO("no [" ISP_FDT_COMPATIBLE "] in dts");
		fdt_node = of_find_compatible_node(NULL, NULL,
						   SI_FDT_COMPATIBLE);
		if (NULL == fdt_node) {
			DBG_INFO("no [" SI_FDT_COMPATIBLE "] in dts");
			return -1;
		}
	}

	ret = of_property_read_u32(fdt_node, "mirror_flip", &mf);
	if (ret) {
		DBG_INFO("failed to get mirror_flip,set default:3\n");
		mf = 3;
	}

	ret = isp_gpio_init(fdt_node, &g__spinfo);
	if (ret) {
		DBG_ERR("pwdn init error!");
		goto exit;
	}

	ret = isp_regulator_init(fdt_node, &g_isp_ir);
	if (ret) {
		DBG_ERR("avdd init error!");
		goto exit;
	}

	ret = isp_clk_enable();
	if (ret) {
		DBG_ERR("enable isp clock error");
		goto exit;
	}

	isp_regulator_enable(&g_isp_ir);

	return ret;
 exit:
	isp_regulator_exit_power_off(&g_isp_ir);
	isp_gpio_exit_power_off(&g__spinfo);

	return ret;
}

void detect_deinit_power_off(void)
{
	isp_clk_disable();
	isp_regulator_disable(&g_isp_ir);
	isp_regulator_exit_power_off(&g_isp_ir);
	isp_gpio_exit_power_off(&g__spinfo);
}

void detect_deinit_power_hold(void)
{
	isp_clk_disable();
	isp_regulator_exit_power_hold(&g_isp_ir);
	isp_gpio_exit_power_hold(&g__spinfo);
}

static int detect_process(void)
{
	int ret = -1;
	ret = module_verify_pid(g_adap, NULL);
	if (ret == 0)
		DBG_INFO("detect success!!!!");
	else
		DBG_ERR("detect failed.");

	return ret;
}

#if 1
static ssize_t front_name_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return strlcpy(buf, camera_front_name, sizeof(camera_front_name));
}

static ssize_t rear_name_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return strlcpy(buf, camera_rear_name, sizeof(camera_rear_name));
}

static ssize_t front_offset_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", camera_front_offset);
}

static ssize_t rear_offset_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", camera_rear_offset);
}

static DEVICE_ATTR(front_name, 0444, front_name_show, NULL);
static DEVICE_ATTR(rear_name, 0444, rear_name_show, NULL);
static DEVICE_ATTR(front_offset, 0444, front_offset_show, NULL);
static DEVICE_ATTR(rear_offset, 0444, rear_offset_show, NULL);

#endif
static int detect_work(void)
{
	int ret = 0;
	if (!fornt_sensor_detected) {
		DBG_INFO("--------detect front sensor------");
#if USE_AS_FRONT
		sensor_power_on(false, &g__spinfo, true);
		ret = detect_process();
		sensor_power_off(false, &g__spinfo, false);
		if (0 == ret) {
#if 1
			struct kobject *front_kobj;
			front_kobj = kobject_create_and_add(CAMERA_FRONT_NAME,
							    NULL);
			if (front_kobj == NULL) {
				DBG_ERR("kobject_create_and_add failed.");
				ret = -ENOMEM;
				return ret;
			}
			camera_front_offset = 1;
			sprintf(camera_front_name, "%s.ko", CAMERA_MODULE_NAME);
			ret = sysfs_create_file(front_kobj,
						&dev_attr_front_offset.attr);
			ret = sysfs_create_file(front_kobj,
						&dev_attr_front_name.attr);
#endif
			fornt_sensor_detected = true;
			return SENSOR_FRONT;
		}
#endif
	}
	if (!rear_sensor_detected) {
		DBG_INFO("-------detect rear sensor-------");
#if USE_AS_REAR
		sensor_power_on(true, &g__spinfo, true);
		ret = detect_process();
		sensor_power_off(true, &g__spinfo, false);
		if (ret == 0) {
#if 1
			struct kobject *rear_kobj;
			rear_kobj = kobject_create_and_add(CAMERA_REAR_NAME,
							   NULL);
			if (rear_kobj == NULL) {
				DBG_ERR("kobject_create_and_add failed.");
				ret = -ENOMEM;
				return ret;
			}
			camera_rear_offset = 0;
			ret = sysfs_create_file(rear_kobj,
						&dev_attr_rear_offset.attr);
			sprintf(camera_rear_name, "%s.ko", CAMERA_MODULE_NAME);
			ret = sysfs_create_file(rear_kobj,
						&dev_attr_rear_name.attr);
#endif
			rear_sensor_detected = true;
			return SENSOR_REAR;
		}
#endif
	}
	return ret;
}
