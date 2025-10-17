// SPDX-License-Identifier: MIT
/**
 * 1-second UPS driver using the Beckhoff BIOS API
 * Author: Stefan Raufhake <s.raufhake@beckhoff.com>
 * Copyright (C) 2015 - 2025 Beckhoff Automation GmbH & Co. KG
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/power/gpio-charger.h>

#include <linux/sysfs.h>
#include <linux/device.h>

#include "bbapi.h"

#define DRV_VERSION "0.2"
#define DRV_DESCRIPTION "Beckhoff BIOS API 1-second UPS driver"

#define BIOSIGRP_SUPS 0x00005000 // SUPS functions

#define BIOSIOFFS_SUPS_GPIO_PIN \
	0x000000a0 // Get the Address and the GPIO-Pin from PWR-Fail PIN, W:0, R:4
#define BIOSIOFFS_SUPS_GPIO_PIN_EX \
	0x000000a1 // Get the Address and the GPIO-Pin from PWR-Fail PIN, W:0, R:24

#define BBAPI_SUPS_TYPE_MMIO 2 // type: MemoryMappedIo for Bapi_GpioInfoEx
#define BBAPI_SUPS_FLAGS_RO 0x01 // flags: read only data for Bapi_GpioInfoEx
#define BBAPI_SUPS_FLAGS_RW 0x03 // flags: read/write data for Bapi_GpioInfoEx
#define BBAPI_SUPS_FLAGS_LOW_ACT 0x00 // flags: signal low active
#define BBAPI_SUPS_FLAGS_HIGH_ACT 0x04 // flags: signal high active
#define BBAPI_SUPS_FLAGS_EDGE_BITS 0x0C // flags: toggle signal

typedef struct Bapi_GpioInfoEx {
	uint16_t type; //0=Port access, 2=MemoryMappedIo,...
	uint16_t length;
	uint16_t flags; //Bit0-1: 00 Not valid, 01=RO, 10=WO, 11=RW;Bit2-3: 00=lowActive, 01=highActive,11=Toggle;Bit4-15:TBD
	uint16_t reserved;
	uint64_t address;
	uint64_t bitmask;
} __attribute__((packed)) BAPI_GPIO_INFO_EX;

// SUPS or watchdog GPIO pin info
typedef struct TSUps_GpioInfo {
	uint16_t ioAddr;
	uint8_t offset;
	uint8_t params;
	const uint32_t reserved;
} __attribute__((packed)) SUPS_GPIO_INFO;

struct bbapi_sups_info {
	struct gpio_chip gpio_chip;
	struct Bapi_GpioInfoEx gpio_info;
	void __iomem *mapped_base;
};

#define sups_read(offset, buffer) \
	bbapi_read(BIOSIGRP_SUPS, offset, &buffer, sizeof(buffer))

static int sups_gpio_get(struct gpio_chip *chip, unsigned int nr)
{
	struct bbapi_sups_info *pbi =
		container_of(chip, struct bbapi_sups_info, gpio_chip);

	u32 val = ioread32(pbi->mapped_base);

	return (!(((val & pbi->gpio_info.bitmask) == pbi->gpio_info.bitmask) ^
		  ((pbi->gpio_info.flags & BBAPI_SUPS_FLAGS_EDGE_BITS) >> 2)));
}

static const char *sups_gpio_names[] = { "sups_pwrfail" };

static const struct gpio_chip sups_gpio_chip = {
	.label = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.get = sups_gpio_get,
	.base = -1,
	.ngpio = 1,
	.names = sups_gpio_names,
};

static struct gpiod_lookup_table sups_power_gpiod_table = {
	.dev_id = "gpio-charger",
	.table = {
		GPIO_LOOKUP(KBUILD_MODNAME, 0,
			    "charge-status", GPIO_ACTIVE_LOW),
		{ },
	},
};

static char *sups_supplied_to[] = {
	"second_ups",
};

static struct gpio_charger_platform_data sups_power_data = {
	.name = "ups",
	.type = POWER_SUPPLY_TYPE_UPS,
	.supplied_to = sups_supplied_to,
	.num_supplicants = ARRAY_SIZE(sups_supplied_to),
};

static struct platform_device sups_power_device = {
	.name = "gpio-charger",
	.id = -1,
	.dev.platform_data = &sups_power_data,
};

static int init_sups(struct bbapi_sups_info *pbi)
{
	int status;

	memset(&pbi->gpio_info, 0, sizeof(pbi->gpio_info));

	if (sups_read(BIOSIOFFS_SUPS_GPIO_PIN_EX, pbi->gpio_info)) {
		struct TSUps_GpioInfo legacy;

		if (sups_read(BIOSIOFFS_SUPS_GPIO_PIN, legacy)) {
			pr_err("BIOSIOFFS_SUPS_GPIO_PIN not supported\n");
			return -ENODEV;
		}

		pbi->gpio_info.address = legacy.ioAddr + legacy.offset;
		pbi->gpio_info.bitmask = legacy.params;
		pbi->gpio_info.length = 4;
		pbi->gpio_info.flags = BBAPI_SUPS_FLAGS_RO |
				       BBAPI_SUPS_FLAGS_HIGH_ACT;
	} else {
		if (pbi->gpio_info.type != BBAPI_SUPS_TYPE_MMIO) {
			pr_err("type MemoryMappedIo not suppoorted by this device\n");
			return -ENODEV;
		}

		if (((pbi->gpio_info.flags & 0x3) != BBAPI_SUPS_FLAGS_RO) &&
		    ((pbi->gpio_info.flags & 0x3) != BBAPI_SUPS_FLAGS_RW)) {
			pr_err("not allowed to read the register\n");
			return -ENODEV;
		}

		if (((pbi->gpio_info.flags & BBAPI_SUPS_FLAGS_EDGE_BITS) !=
		     BBAPI_SUPS_FLAGS_HIGH_ACT) &&
		    ((pbi->gpio_info.flags & BBAPI_SUPS_FLAGS_EDGE_BITS) !=
		     BBAPI_SUPS_FLAGS_LOW_ACT)) {
			pr_err("signal high and low only supported\n");
			return -ENODEV;
		}
	}

	pbi->mapped_base = (void *)pbi->gpio_info.address;

	memcpy(&pbi->gpio_chip, &sups_gpio_chip, sizeof(pbi->gpio_chip));
	status = gpiochip_add_data(&pbi->gpio_chip, NULL);
	if (status) {
		pr_err("register gpio-charger failed\n");
		return status;
	}

	pr_info("registered %s as gpiochip%d with #%d GPIOs.\n",
		pbi->gpio_chip.label, pbi->gpio_chip.base,
		pbi->gpio_chip.ngpio);

	gpiod_add_lookup_table(&sups_power_gpiod_table);
	status = platform_device_register(&sups_power_device);

	if (status) {
		pr_err("failed to register the platform device\n");
		gpiod_remove_lookup_table(&sups_power_gpiod_table);
		return status;
	}

	return 0;
}

static int bbapi_sups_probe(struct platform_device *pdev)
{
	struct bbapi_sups_info *pbi;

	pbi = devm_kzalloc(&pdev->dev, sizeof(*pbi), GFP_KERNEL);
	if (!pbi)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, pbi);

	return init_sups(pbi);
}

static void bbapi_sups_remove(struct platform_device *pdev)
{
	platform_device_unregister(&sups_power_device);
	gpiod_remove_lookup_table(&sups_power_gpiod_table);

	struct bbapi_sups_info *pbi = platform_get_drvdata(pdev);

	if (pbi->gpio_info.address)
		gpiochip_remove(&pbi->gpio_chip);
}

static struct platform_driver bbapi_sups_driver = {
	.driver = {
		   .name = KBUILD_MODNAME,
		   },
	.probe = bbapi_sups_probe,
	.remove = bbapi_sups_remove,
};

module_platform_driver(bbapi_sups_driver);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Stefan Raufhake <s.raufhake@beckhoff.com>");
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(DRV_VERSION);
