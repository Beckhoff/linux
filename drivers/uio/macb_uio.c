// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 - 2025 Phytium Technology Co., Ltd. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/acpi.h>


#define DRIVER_NAME "macb_uio"
#define DRIVER_VERSION "5.0"
#define DRIVER_AUTHOR "Phytium"
#define DRIVER_DESC "UIO driver for platform device"

#define UIO_POLL_INTERVAL 100 /* Unit: ms */

/**
 * A structure describing the private information for a uio device.
 */
struct rte_uio_platform_dev {
	struct uio_info info;
	struct platform_device *pdev;
	atomic_t refcnt;
	struct task_struct *poll_task;
};

struct macb_platform_data {
	struct clk *pclk;
	struct clk *hclk;
};

struct fixed_phy_status {
	int speed;
	int duplex;
};


static ssize_t dev_type_show(struct device *dev, struct device_attribute *attr,
							 char *buf)
{
	struct device_node *np = dev->of_node;
	const char *pm;
	int err;

	if (np) {
		err = device_property_read_string(dev, "compatible", &pm);
		if (err < 0)
			return snprintf(buf, 64, "Unknown");

		return snprintf(buf, 64, "%s", pm);
	} else if (has_acpi_companion(dev)) {
		pm = acpi_device_hid(ACPI_COMPANION(dev));
		return snprintf(buf, 64, "%s", pm);
	}

	return snprintf(buf, 64, "Unknown");
}

static DEVICE_ATTR_RO(dev_type);

/* sriov sysfs */
static ssize_t pclk_hz_show(struct device *dev, struct device_attribute *attr,
							char *buf)
{
	struct macb_platform_data *pdata;
	struct clk *pclk;
	unsigned long pclk_hz;

	pdata = dev_get_platdata(dev);
	if (pdata) {
		pclk = pdata->pclk;
	} else {
		pclk = devm_clk_get(dev, "pclk");
		if (IS_ERR(pclk)) {
			dev_info(dev, "can't get pclk value.\n");
			pclk = NULL;
		}
	}

	pclk_hz = clk_get_rate(pclk);
	if (!pclk_hz)
		pclk_hz = 250000000;
	return snprintf(buf, 12, "%lu", pclk_hz);
}

static DEVICE_ATTR_RO(pclk_hz);

static ssize_t phy_mode_show(struct device *dev, struct device_attribute *attr,
							 char *buf)
{
	const char *pm;
	int err;

	err = device_property_read_string(dev, "phy-mode", &pm);
	if (err < 0)
		return snprintf(buf, 64, "Unknown");

	return snprintf(buf, 64, "%s", pm);
}

static DEVICE_ATTR_RO(phy_mode);

static ssize_t physical_addr_show(struct device *dev, struct device_attribute *attr,
							char *buf)
{
	phys_addr_t physical_addr = 0;
	struct resource *res;
	int i;
	struct platform_device *pdev = to_platform_device(dev);

	for (i = 0; i < MAX_UIO_MAPS; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			continue;

		physical_addr = res->start;
		break;
	}

	return snprintf(buf, 16, "0x%llx", physical_addr);
}

static DEVICE_ATTR_RO(physical_addr);

static ssize_t speed_info_show(struct device *dev, struct device_attribute *attr,
						char *buf)
{
	struct fixed_phy_status status = {};
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct fwnode_handle *fixed_node;

	if (fwnode) {
		fixed_node = fwnode_get_named_child_node(fwnode, "fixed-link");
		if (fixed_node) {
			fwnode_property_read_u32(fixed_node, "speed", &status.speed);

			status.duplex = DUPLEX_HALF;
			if (fwnode_property_read_bool(fixed_node, "full-duplex"))
				status.duplex = DUPLEX_FULL;

			fwnode_handle_put(fixed_node);
			if (status.duplex == DUPLEX_FULL)
				return snprintf(buf, 64, "fixed-link:%d full-duplex\n",
						status.speed);
			else
				return snprintf(buf, 64, "fixed-link:%d half-duplex\n",
						status.speed);
		} else {
			return snprintf(buf, 64, "unknown");
		}
	} else {
		return snprintf(buf, 64, "unknown");
	}
}

static DEVICE_ATTR_RO(speed_info);

static struct attribute *dev_attrs[] = {
	&dev_attr_pclk_hz.attr,
	&dev_attr_phy_mode.attr,
	&dev_attr_physical_addr.attr,
	&dev_attr_dev_type.attr,
	&dev_attr_speed_info.attr,
	NULL,
};

static const struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

static int macb_uio_poll(void *args)
{
	struct uio_info *info = (struct uio_info *)args;

	while (!kthread_should_stop()) {
		uio_event_notify(info);
		msleep(UIO_POLL_INTERVAL);
	}

	return 0;
}

static int macb_uio_open(struct uio_info *info, struct inode *inode)
{
	struct rte_uio_platform_dev *udev = info->priv;

	if (atomic_inc_return(&udev->refcnt) != 1)
		return 0;

	udev->poll_task =
		kthread_create(macb_uio_poll, info, "poll_macb_uio%d", info->uio_dev->minor);
	kthread_bind(udev->poll_task, 0);
	wake_up_process(udev->poll_task);

	return 0;
}

static int macb_uio_release(struct uio_info *info, struct inode *inode)
{
	struct rte_uio_platform_dev *udev = info->priv;

	if (atomic_dec_and_test(&udev->refcnt))
		kthread_stop(udev->poll_task);

	return 0;
}

/* Unmap previously ioremap'd resources */
static void macb_uio_release_iomem(struct uio_info *info)
{
	int i;

	for (i = 0; i < MAX_UIO_MAPS; i++) {
		if (info->mem[i].internal_addr)
			iounmap(info->mem[i].internal_addr);
	}
}

/* Remap platform device's resources */
static int macb_uio_setup_iomem(struct platform_device *dev,
								struct uio_info *info)
{
	int i, iom = 0;
	struct resource *res;

	for (i = 0; i < MAX_UIO_MAPS; i++) {
		res = platform_get_resource(dev, IORESOURCE_MEM, i);
		if (!res)
			continue;

		info->mem[iom].memtype = UIO_MEM_PHYS;
		info->mem[iom].addr = res->start & PAGE_MASK;
		info->mem[iom].size = PAGE_ALIGN(resource_size(res));
		info->mem[iom].name = "macb_regs";
		info->mem[iom].internal_addr =
			ioremap(info->mem[iom].addr, info->mem[iom].size);
		iom++;
	}

	return (iom != 0) ? 0 : -ENOENT;
}

/*
 * macb_uio_probe() - platform uio driver probe routine
 * - register uio devices filled with memory maps retrieved from device tree
 */
static int macb_uio_probe(struct platform_device *dev)
{
	struct rte_uio_platform_dev *udev;
	dma_addr_t map_dma_addr;
	void *map_addr;
	int err;

	udev = kzalloc(sizeof(struct rte_uio_platform_dev), GFP_KERNEL);
	if (!udev)
		return -ENOMEM;

	/* remap IO memory */
	err = macb_uio_setup_iomem(dev, &udev->info);
	if (err) {
		dev_err(&dev->dev, "There is no resource for register uio device.\n");
		goto fail_release_iomem;
	}

	/* fill uio infos */
	udev->info.name = DRIVER_NAME;
	udev->info.version = DRIVER_VERSION;
	udev->info.open = macb_uio_open;
	udev->info.release = macb_uio_release;
	udev->info.irq = UIO_IRQ_CUSTOM;
	udev->info.priv = udev;
	udev->pdev = dev;
	atomic_set(&udev->refcnt, 0);

	err = sysfs_create_group(&dev->dev.kobj, &dev_attr_grp);
	if (err != 0)
		goto fail_release_iomem;

	/* register uio driver */
	err = uio_register_device(&dev->dev, &udev->info);
	if (err) {
		dev_err(&dev->dev, "Failed to register uio device.\n");
		goto fail_remove_group;
	}

	platform_set_drvdata(dev, udev);

	/*
	 * Doing a harmless dma mapping for attaching the device to
	 * the iommu identity mapping if kernel boots with iommu=pt.
	 * Note this is not a problem if no IOMMU at all.
	 */
	map_addr = dma_alloc_coherent(&dev->dev, 1024, &map_dma_addr, GFP_KERNEL);
	if (map_addr)
		memset(map_addr, 0, 1024);

	if (!map_addr)
		dev_info(&dev->dev, "dma mapping failed\n");
	else {
		dev_info(&dev->dev, "mapping 1K dma=%#llx host=%p\n",
				 (unsigned long long)map_dma_addr, map_addr);

		dma_free_coherent(&dev->dev, 1024, map_addr, map_dma_addr);
		dev_info(&dev->dev, "unmapping 1K dma=%#llx host=%p\n",
				 (unsigned long long)map_dma_addr, map_addr);
	}

	return 0;

fail_remove_group:
	sysfs_remove_group(&dev->dev.kobj, &dev_attr_grp);
fail_release_iomem:
	macb_uio_release_iomem(&udev->info);

	kfree(udev);

	return err;
}

static int macb_uio_remove(struct platform_device *dev)
{
	struct rte_uio_platform_dev *udev = platform_get_drvdata(dev);

	if (!udev)
		return -EINVAL;

	macb_uio_release(&udev->info, NULL);

	sysfs_remove_group(&dev->dev.kobj, &dev_attr_grp);
	uio_unregister_device(&udev->info);
	macb_uio_release_iomem(&udev->info);
	platform_set_drvdata(dev, NULL);
	kfree(udev);

	return 0;
}

static struct platform_driver macb_uio_driver = {
	.driver = {
			.owner = THIS_MODULE,
			.name = DRIVER_NAME,
			.of_match_table = NULL,
			.acpi_match_table = NULL,
		},
	.probe = macb_uio_probe,
	.remove = macb_uio_remove,
};

module_platform_driver(macb_uio_driver);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:" DRIVER_NAME);
