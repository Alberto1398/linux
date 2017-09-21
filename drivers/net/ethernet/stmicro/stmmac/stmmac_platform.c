/*******************************************************************************
  This contains the functions to handle the platform driver.

  Copyright (C) 2007-2011  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/random.h>

#include "stmmac.h"

#define ETH_MAC_ADDR_RANDDOM_PATH "/etc/mac_address_random"
#define ETH_MAC_LEN 6


extern void __iomem *ppaddr;
extern const struct plat_stmmacenet_data owl_gmac_data;

static char g_default_mac_addr[ETH_MAC_LEN] = {
	0x00, 0x18, 0xFE, 0x61, 0xD5, 0xD6};

static void print_mac_address(const char *mac)
{
	int i;
	for (i = 0; i < ETH_MAC_LEN - 1; i++)
		printk(KERN_DEBUG"%02x-", (unsigned int)mac[i] & 0xFF);
	printk(KERN_DEBUG"%02x\n", (unsigned int)mac[i] & 0xFF);
	return;
}

static int ctox(int c)
{
	int tmp;

	if (!isxdigit(c)) {
		printk(KERN_DEBUG"'%c' is not hex digit\n", (char)c);
		return -1;
	}

	if ((c >= '0') && (c <= '9'))
		tmp = c - '0';
	else
		tmp = (c | 0x20) - 'a' + 10;

	return tmp;
}

static int parse_mac_address(const char *mac, int len)
{
	int tmp, tmp2;
	int i = 0;
	int j = 0;
	char s[16] = "";
	int c;

	printk(KERN_DEBUG"ethernet mac address string: %s, len: %d\n", mac, strlen(mac));
	if (17 == len) {
		if (strlen(mac) > 17) {
			printk(KERN_ERR"ethernet mac address string too long\n");
			return -1;
		}
		while ((c = mac[i++]) != '\0') {
			if (c == ':')
				continue;
			s[j++] = c;
		}
		s[j] = '\0';
		printk(KERN_DEBUG"mac address string stripped colon: %s\n", s);
	} else if (12 == len) {
		if (strlen(mac) > 12) {
			printk(KERN_ERR"ethernet mac address string too long\n");
			return -1;
		}
		memcpy(s, mac, 12);
		s[12] = '\0';
	} else {
		printk(KERN_ERR"length of ethernet mac address is not 12 or 17\n");
		return -1;
	}

	for (i = 0; i < ETH_MAC_LEN; i++) {
		tmp = ctox(s[i * 2]);
		tmp2 = ctox(s[i * 2 + 1]);
		tmp = (tmp * 16) + tmp2;
		*(char *)(g_default_mac_addr + i) = tmp & 0xFF;
	}

	return 0;
}



#ifdef CONFIG_OF
static int stmmac_probe_config_dt(struct platform_device *pdev,
				  struct plat_stmmacenet_data *plat,
				  const char **mac)
{
	struct device_node *np = pdev->dev.of_node;
	char def_mac[20] = "";
	const char *str;
	mm_segment_t fs;
	struct file *filp;
	loff_t offset = 0;
	int ret;

	if (!np)
		return -ENODEV;

	plat->interface = of_get_phy_mode(np);
	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(struct stmmac_mdio_bus_data),
					   GFP_KERNEL);

	/*
	 * Currently only the properties needed on SPEAr600
	 * are provided. All other properties should be added
	 * once needed on other platforms.
	 */
	if (of_device_is_compatible(np, "st,spear600-gmac") ||
		of_device_is_compatible(np, "snps,dwmac-3.70a") ||
		of_device_is_compatible(np, "snps,dwmac")) {
		plat->has_gmac = 1;
		plat->pmt = 1;
	}

#ifdef MISCINFO_HAS_ETHMAC
	ret = read_mi_item("ETHMAC", def_mac, 20);
	if (ret > 0) {
		printk(KERN_DEBUG"Using the mac address in miscinfo.\n");
		parse_mac_address(def_mac, ret);
		*mac = g_default_mac_addr;
		return 0;
	}
#endif

	ret = of_property_read_string(np, "random-mac-address", &str);
	if (ret) {
		printk(KERN_ERR"no random-mac-address in dts\n");
	} else {
		printk(KERN_INFO"random-mac-address: %s\n", str);
		if (!strcmp("okay", str))
			goto random_mac;
	}

	str = of_get_property(np, "local-mac-address", NULL);
	if (str == NULL)
		printk(KERN_ERR"no local-mac-address in dts\n");
	else
		*mac = of_get_mac_address(np);
	return 0;

random_mac:
	get_random_bytes(def_mac, ETH_MAC_LEN);
	memcpy(g_default_mac_addr + 3, def_mac + 3, ETH_MAC_LEN - 3);

	fs = get_fs();
	set_fs(get_ds());

	filp = filp_open(ETH_MAC_ADDR_RANDDOM_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp)) {
		filp = filp_open(ETH_MAC_ADDR_RANDDOM_PATH,
					 O_WRONLY | O_CREAT, 0640);
		if (!IS_ERR_OR_NULL(filp)) {
			printk(KERN_INFO"use random mac generated: ");
			print_mac_address(g_default_mac_addr);
			vfs_write(filp, (char __user *)g_default_mac_addr,
						ETH_MAC_LEN, &offset);
			filp_close(filp, current->files);
		}
	} else {
		if (vfs_read(filp, (char __user *)def_mac,
				ETH_MAC_LEN, &offset) == ETH_MAC_LEN) {
			memcpy(g_default_mac_addr, def_mac, ETH_MAC_LEN);
			printk(KERN_INFO"use random mac stored: ");
			print_mac_address(g_default_mac_addr);
		}
		filp_close(filp, current->files);
	}
	set_fs(fs);
	*mac = g_default_mac_addr;

	return 0;
}
#else
static int stmmac_probe_config_dt(struct platform_device *pdev,
				  struct plat_stmmacenet_data *plat,
				  const char **mac)
{
	return -ENOSYS;
}
#endif /* CONFIG_OF */

/**
 * stmmac_pltfr_probe
 * @pdev: platform device pointer
 * Description: platform_device probe function. It allocates
 * the necessary resources and invokes the main to init
 * the net device, register the mdio bus etc.
 */
int stmmac_pltfr_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct device *dev = &pdev->dev;
	void __iomem *addr = NULL;
	struct stmmac_priv *priv = NULL;
	struct plat_stmmacenet_data *plat_dat = NULL;
	const char *mac = NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	ppaddr = addr;
	if (pdev->dev.of_node) {
		plat_dat = &owl_gmac_data;
		if (!plat_dat) {
			pr_err("%s: ERROR: no memory", __func__);
			return  -ENOMEM;
		}

		ret = stmmac_probe_config_dt(pdev, plat_dat, &mac);
		if (ret) {
			pr_err("%s: main dt probe failed", __func__);
			return ret;
		}
	} else {
		plat_dat = pdev->dev.platform_data;
	}

	/* Custom initialisation (if needed)*/
	if (plat_dat->init) {
		ret = plat_dat->init(pdev);
		if (unlikely(ret))
			return ret;
	}

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	priv = stmmac_dvr_probe(&(pdev->dev), plat_dat, addr);
	if (!priv) {
		pr_err("%s: main driver probe failed", __func__);
		return -ENODEV;
	}

	/* Get MAC address if available (DT) */
	if (mac)
		memcpy(priv->dev->dev_addr, mac, ETH_ALEN);

	/* Get the MAC information */
	priv->dev->irq = platform_get_irq_byname(pdev, "macirq");
	if (priv->dev->irq == -ENXIO) {
		pr_err("%s: ERROR: MAC IRQ configuration "
		       "information not found\n", __func__);
		return -ENXIO;
	}

	/*
	 * On some platforms e.g. SPEAr the wake up irq differs from the mac irq
	 * The external wake up irq can be passed through the platform code
	 * named as "eth_wake_irq"
	 *
	 * In case the wake up interrupt is not passed from the platform
	 * so the driver will continue to use the mac irq (ndev->irq)
	 */
	priv->wol_irq = platform_get_irq_byname(pdev, "eth_wake_irq");
	if (priv->wol_irq == -ENXIO)
		priv->wol_irq = priv->dev->irq;

	priv->lpi_irq = platform_get_irq_byname(pdev, "eth_lpi");

	platform_set_drvdata(pdev, priv->dev);



	pr_debug("STMMAC platform driver registration completed");

	return 0;
}

/**
 * stmmac_pltfr_remove
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and calls the platforms hook and release the resources (e.g. mem).
 */
int stmmac_pltfr_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret = stmmac_dvr_remove(ndev);

	if (priv->plat->exit)
		priv->plat->exit(pdev);

	platform_set_drvdata(pdev, NULL);

	return ret;
}
EXPORT_SYMBOL_GPL(stmmac_pltfr_remove);

#ifdef CONFIG_PM
static int stmmac_pltfr_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_suspend(ndev);
}

static int stmmac_pltfr_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_resume(ndev);
}

int stmmac_pltfr_freeze(struct device *dev)
{
	int ret;
	struct plat_stmmacenet_data *plat_dat = dev_get_platdata(dev);
	struct net_device *ndev = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);

	ret = stmmac_freeze(ndev);
	if (plat_dat->exit)
		plat_dat->exit(pdev);

	return ret;
}

int stmmac_pltfr_restore(struct device *dev)
{
	struct plat_stmmacenet_data *plat_dat = dev_get_platdata(dev);
	struct net_device *ndev = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);

	if (plat_dat->init)
		plat_dat->init(pdev);

	return stmmac_restore(ndev);
}

const struct dev_pm_ops stmmac_pltfr_pm_ops = {
	.suspend = stmmac_pltfr_suspend,
	.resume = stmmac_pltfr_resume,
	.freeze = stmmac_pltfr_freeze,
	.thaw = stmmac_pltfr_restore,
	.restore = stmmac_pltfr_restore,
};
EXPORT_SYMBOL_GPL(stmmac_pltfr_pm_ops);
#else
static const struct dev_pm_ops stmmac_pltfr_pm_ops;
#endif /* CONFIG_PM */

static const struct of_device_id stmmac_dt_ids[] = {
	{ .compatible = "st,spear600-gmac"},
	{ .compatible = "snps,dwmac-3.70a"},
	{ .compatible = "snps,dwmac"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, stmmac_dt_ids);

struct platform_driver stmmac_pltfr_driver = {
	.probe = stmmac_pltfr_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		   .name = STMMAC_RESOURCE_NAME,
		   .owner = THIS_MODULE,
		   .pm = &stmmac_pltfr_pm_ops,
		   .of_match_table = of_match_ptr(stmmac_dt_ids),
		   },
};

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet PLATFORM driver");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
