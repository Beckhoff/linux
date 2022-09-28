// SPDX-License-Identifier: MIT
/**
 * Character Driver for Beckhoff BIOS API
 * Author: Heiko Wilke <h.wilke@beckhoff.com>
 * Author: Patrick Bruenn <p.bruenn@beckhoff.com>
 * Copyright (C) 2013 Beckhoff Automation GmbH & Co. KG
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <generated/utsrelease.h>

#include "bhf-bapi.h"

#define DRV_VERSION "0.3.0"
#define DRV_DESCRIPTION "Beckhoff BIOS API Driver"

#define BBAPI_CMD 0x5001 // BIOS API Command number for IOCTL call

#define BIOSIGRP_GENERAL                                                       \
	0x00000000 // 0x00000000..0x00001FFF General BIOS functions
#define BIOSIOFFS_GENERAL_GETBOARDNAME                                         \
	0x00000001 // Return mainboard plattform, W:0, R:16

#define BIOSIGRP_SUPS 0x00005000 // SUPS functions
#define BIOSIOFFS_SUPS_GPIO_PIN                                                \
	0x000000a0 // Get the Address and the GPIO-Pin from PWR-Fail PIN, W:0, R:4
#define BIOSIOFFS_SUPS_GPIO_PIN_EX                                             \
	0x000000a1 // Get the Address and the GPIO-Pin from PWR-Fail PIN, W:0, R:24

#define BIOSIGRP_CXPWRSUPP 0x00009000 // CX Power Supply functions
#define BIOSIOFFS_CXPWRSUPP_GETTYPE 0x00000010 // Get type, W:0, R:4 (DWORD)
#define BIOSIOFFS_CXPWRSUPP_ENABLEBACKLIGHT                                    \
	0x00000060 // Set display backlight, W:1 (0x00 := OFF, 0xFF := ON), R:0
#define BIOSIOFFS_CXPWRSUPP_DISPLAYLINE1                                       \
	0x00000061 // Set display line 1, W:17(BYTE[17]), R:0
#define BIOSIOFFS_CXPWRSUPP_DISPLAYLINE2                                       \
	0x00000062 // Set display line 2, W:17(BYTE[17]), R:0

// Max. CX power supply display line length
#define CXPWRSUPP_MAX_DISPLAY_CHARS 16 // without null delimiter
#define CXPWRSUPP_MAX_DISPLAY_LINE                                             \
	(CXPWRSUPP_MAX_DISPLAY_CHARS + 1) // 17 => inclusive null delimiter

#define BIOSAPIERR_OFFSET 0x20000000
#define BIOSAPI_SRVNOTSUPP (BIOSAPIERR_OFFSET + 0x701)
#define BIOSAPI_INVALIDSIZE (BIOSAPIERR_OFFSET + 0x705)
#define BIOSAPI_BUSY (BIOSAPIERR_OFFSET + 0x708)
#define BIOSAPI_INVALIDPARM (BIOSAPIERR_OFFSET + 0x70B)

/* Global Variables */
struct bbapi_struct {
	uint32_t nIndexGroup;
	uint32_t nIndexOffset;
	const void __user *pInBuffer;
	uint32_t nInBufferSize;
	void __user *pOutBuffer;
	uint32_t nOutBufferSize;
	uint32_t __user *pBytesReturned;
	void __user *pMode;
};

static struct bbapi_object g_bbapi;

static unsigned long g_bbapi_busy_retry = 10;
module_param_named(busy_retry, g_bbapi_busy_retry, ulong, 0);
MODULE_PARM_DESC(
	busy_retry,
	"Number of attemps to retry BBAPI calls, failed with BIOSAPI_BUSY.");

static unsigned long g_bbapi_search_area = BBIOSAPI_SIGNATURE_SEARCH_AREA;
module_param_named(search_area, g_bbapi_search_area, ulong, 0);
MODULE_PARM_DESC(search_area,
		 "Size in bytes of the area to search for the BBAPI signature.");

static const uint64_t BBIOSAPI_SIGNATURE =
	0x3436584950414242LL; // API-String "BBAPIX64"

/**
 * This function is a wrapper to the Beckhoff BIOS API entry function,
 * which uses MS Windows calling convention.
 */
typedef __attribute__((ms_abi))
uint32_t (*PFN_BBIOSAPI_CALL)(uint32_t group, uint32_t offset, void *in,
			      uint32_t inSize, void *out, uint32_t outSize,
			      uint32_t *bytes);

static unsigned int bbapi_call(void __kernel *const in,
			       void __kernel *const out,
			       PFN_BBIOSAPI_CALL entry,
			       const struct bbapi_struct *const cmd,
			       unsigned int *bytes_written)
{
	return entry(cmd->nIndexGroup, cmd->nIndexOffset, in,
		     cmd->nInBufferSize, out, cmd->nOutBufferSize,
		     bytes_written);
}

static unsigned int bbapi_call_retry(void __kernel *const in,
				     void __kernel *const out,
				     PFN_BBIOSAPI_CALL entry,
				     const struct bbapi_struct *const cmd,
				     unsigned int *bytes_written)
{
	ulong retries = g_bbapi_busy_retry;

	for (;;) {
		const unsigned int status =
			bbapi_call(in, out, entry, cmd, bytes_written);
		if (BIOSAPI_BUSY == (status | BIOSAPIERR_OFFSET)) {
			if (retries--) {
				pr_warn("BBAPI busy, waiting and retrying...\n");
				msleep(100);
				continue;
			} else {
				pr_err("BBAPI was busy for too long, giving up.\n");
			}
		}
		return status;
	}
}

unsigned int bbapi_rw(uint32_t group, uint32_t offset, void __kernel *const in,
		      uint32_t size_in, void __kernel *const out,
		      const uint32_t size_out, uint32_t *bytes_written)
{
	const struct bbapi_struct cmd = { .nIndexGroup = group,
					  .nIndexOffset = offset,
					  .pInBuffer = NULL,
					  .nInBufferSize = size_in,
					  .pOutBuffer = NULL,
					  .nOutBufferSize = size_out };
	unsigned int result = 0;

	if (!g_bbapi.entry)
		return BIOSAPI_SRVNOTSUPP;

	mutex_lock(&g_bbapi.mutex);
	result = bbapi_call_retry(in, out, g_bbapi.entry, &cmd, bytes_written);
	mutex_unlock(&g_bbapi.mutex);
	if (result) {
		pr_debug("%s(0x%x:0x%x) failed with: 0x%x\n", __func__,
			 cmd.nIndexGroup, cmd.nIndexOffset, result);
		return -(result | BIOSAPIERR_OFFSET);
	}
	return result;
}

unsigned int bbapi_read(uint32_t group, uint32_t offset,
			void __kernel *const out, const uint32_t size)
{
	uint32_t bytes_written = 0;

	return bbapi_rw(group, offset, NULL, 0, out, size, &bytes_written);
}
EXPORT_SYMBOL(bbapi_read);

unsigned int bbapi_write(uint32_t group, uint32_t offset,
			 void __kernel *const in, uint32_t size)
{
	uint32_t bytes_written = 0;

	return bbapi_rw(group, offset, in, size, NULL, 0, &bytes_written);
}
EXPORT_SYMBOL(bbapi_write);

int bbapi_board_is(const char *const boardname)
{
	char board[CXPWRSUPP_MAX_DISPLAY_LINE] = { 0 };

	bbapi_read(BIOSIGRP_GENERAL, BIOSIOFFS_GENERAL_GETBOARDNAME, &board,
		   sizeof(board) - 1);
	return !strncmp(board, boardname, sizeof(board));
}
EXPORT_SYMBOL(bbapi_board_is);

/**
 * bbapi_copy_bios() - Copy BIOS from SPI flash into RAM
 * @bbapi: pointer to a not initialized bbapi_object
 * @pos: pointer to the BIOS identifier string in flash
 *
 * We use BIOS shadowing to increase realtime performance.
 * The BIOS identifier string is followed by the 32-Bit BIOS API
 * function offset. This offset is the location of the BIOS API entry
 * function and is located at most 4096 bytes in front of the
 * BIOS memory end. So we calculate the size of the BIOS and copy it
 * from SPI Flash into RAM.
 * Accessing the BIOS in ROM while running realtime applications would
 * otherwise have bad effects on the realtime behaviour.
 *
 * Return: 0 for success, -ENOMEM if the allocation of kernel memory fails
 */
static int __init bbapi_copy_bios(struct bbapi_object *bbapi,
				  uint8_t __iomem *pos)
{
	const uint32_t offset = ioread32(pos + 8);
	const size_t size = offset + 4096;

	bbapi->memory =
		__vmalloc_node_range(size, 1, VMALLOC_START, VMALLOC_END,
				     GFP_KERNEL, PAGE_KERNEL_EXEC,
				     VM_FLUSH_RESET_PERMS, NUMA_NO_NODE,
				     __builtin_return_address(0));

	if (bbapi->memory == NULL) {
		pr_info("__vmalloc for Beckhoff BIOS API failed\n");
		return -ENOMEM;
	}
	memcpy_fromio(bbapi->memory, pos, size);
	bbapi->entry = bbapi->memory + offset;
	return 0;
}

/**
 * bbapi_find_bios() - Find BIOS in SPI flash and copy it into RAM
 * @bbapi: pointer to a not initialized bbapi_object
 *
 * If successful bbapi->memory and bbapi->entry point to the bios in RAM
 *
 * Return: 0 if the bios was successfully copied into RAM
 */
static int __init bbapi_find_bios(struct bbapi_object *bbapi)
{
	static const size_t STEP_SIZE = 0x10;
	uint8_t __iomem *start;
	const uint8_t __iomem *end;
	int result = -EFAULT;
	uint8_t __iomem *pos;
	size_t off;

	if (g_bbapi_search_area > BBIOSAPI_SIGNATURE_SEARCH_AREA) {
		pr_warn("Search area too big\n");
		return -EFAULT;
	}

	// Try to remap IO Memory to search the BIOS API in the memory
	start = ioremap(BBIOSAPI_SIGNATURE_PHYS_START_ADDR,
			g_bbapi_search_area);
	end = start + g_bbapi_search_area;
	if (start == NULL) {
		pr_warn("Mapping memory search area for BIOS API failed\n");
		return -ENOMEM;
	}
	// Search through the remapped memory and look for the BIOS API String
	for (off = 0; off < STEP_SIZE; ++off) {
		for (pos = start + off; pos <= end - STEP_SIZE;
		     pos += STEP_SIZE) {
			const uint32_t low = ioread32(pos);
			const uint32_t high = ioread32(pos + 4);
			const uint64_t lword = ((uint64_t)high << 32 | low);

			if (lword == BBIOSAPI_SIGNATURE) {
				result = bbapi_copy_bios(bbapi, pos);
				pr_info("BIOS found and copied from: %p + 0x%zx | %zu\n",
					start, pos - start, off);
				goto cleanup;
			}
		}
	}
cleanup:
	iounmap(start);
	return result;
}

/**
 * You have to hold the lock on bbapi->mutex when calling this function!!!
 */
static int bbapi_ioctl_mutexed(struct bbapi_object *const bbapi,
			       const struct bbapi_struct *const cmd)
{
	unsigned int written = 0;
	unsigned int ret;

	if (cmd->nInBufferSize > sizeof(bbapi->in)) {
		pr_err("%s(): nInBufferSize invalid\n", __func__);
		return -EINVAL;
	}
	if (cmd->nOutBufferSize > sizeof(bbapi->out)) {
		pr_err("%s(): nOutBufferSize: %d invalid\n", __func__,
		       cmd->nOutBufferSize);
		return -EINVAL;
	}
	// BIOS can operate on kernel space buffers only -> make a temporary copy
	if (copy_from_user(bbapi->in, cmd->pInBuffer, cmd->nInBufferSize)) {
		pr_err("%s(): copy_from_user() failed\n", __func__);
		return -EFAULT;
	}
	// Call the BIOS API
	ret = bbapi_call_retry(bbapi->in, bbapi->out, bbapi->entry, cmd,
			       &written);
	if (ret) {
		pr_debug("%s(0x%x:0x%x) failed with: 0x%x\n", __func__,
			 cmd->nIndexGroup, cmd->nIndexOffset, ret);
		return -(ret | BIOSAPIERR_OFFSET);
	}
	// Copy the BIOS output to the output buffer in user space
	if (copy_to_user(cmd->pOutBuffer, bbapi->out, written)) {
		pr_err("%s(): copy_to_user() failed\n", __func__);
		return -EFAULT;
	}

	if (cmd->pBytesReturned)
		put_user(written, cmd->pBytesReturned);

	return 0;
}

static long bbapi_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct bbapi_struct bbstruct;
	size_t size = sizeof(bbstruct);
	int result = -EINVAL;

	if (!g_bbapi.entry) {
		pr_warn("%s(): not initialized.\n", __func__);
		return -EINVAL;
	}
	// Check if IOCTL CMD matches BBAPI Driver Command
	if (cmd != BBAPI_CMD) {
		pr_info("Wrong Command\n");
		return -EINVAL;
	}
	// Copy data (BBAPI struct) from User Space to Kernel Module - if it fails, return error
	if (copy_from_user(&bbstruct, (const void __user *)arg, size)) {
		pr_err("copy_from_user failed\n");
		return -EINVAL;
	}
	// pMode is reserved for future use
	if (bbstruct.pMode) {
		pr_info("Setting pMode to nullptr is mandatory!\n");
		return -EINVAL;
	}

	if (bbstruct.nIndexOffset >= 0xB0) {
		pr_info("cmd: 0x%x : 0x%x not available from user mode\n",
			bbstruct.nIndexGroup, bbstruct.nIndexOffset);
		return -EACCES;
	}

	mutex_lock(&g_bbapi.mutex);
	result = bbapi_ioctl_mutexed(&g_bbapi, &bbstruct);
	mutex_unlock(&g_bbapi.mutex);
	return result;
}

static int bbapi_release(struct inode *i, struct file *f)
{
	return 0;
}

static const struct file_operations file_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = bbapi_ioctl,
	.release = bbapi_release,
};

static void __init update_display(void)
{
	char line[CXPWRSUPP_MAX_DISPLAY_LINE];
	uint8_t enable = 0xff;

	snprintf(line, sizeof(line), "%s %s", "Linux", UTS_RELEASE);
	bbapi_write(BIOSIGRP_CXPWRSUPP, BIOSIOFFS_CXPWRSUPP_DISPLAYLINE2, line,
		    sizeof(line));

	bbapi_read(BIOSIGRP_GENERAL, BIOSIOFFS_GENERAL_GETBOARDNAME, line,
		   sizeof(line));
	bbapi_write(BIOSIGRP_CXPWRSUPP, BIOSIOFFS_CXPWRSUPP_DISPLAYLINE1, line,
		    sizeof(line));

	bbapi_write(BIOSIGRP_CXPWRSUPP, BIOSIOFFS_CXPWRSUPP_ENABLEBACKLIGHT,
		    &enable, sizeof(enable));
}

static void dev_release_nop(struct device *dev)
{
}

static struct platform_device bbapi_power = {
	.name = "bbapi_power",
	.id = -1,
	.dev = { .release = dev_release_nop },
};

static struct platform_device bbapi_sups = {
	.name = "bbapi_sups",
	.id = -1,
	.dev = { .release = dev_release_nop },
};

static inline bool bbapi_supports(uint32_t group, uint32_t offset)
{
	switch (-bbapi_read(group, offset, NULL, 0)) {
	case BIOSAPI_INVALIDSIZE:
	case BIOSAPI_INVALIDPARM:
		return true;
	default:
		return false;
	}
}

#define bbapi_supports_display()                                               \
	bbapi_supports(BIOSIGRP_CXPWRSUPP, BIOSIOFFS_CXPWRSUPP_ENABLEBACKLIGHT)

#define bbapi_supports_power()                                                 \
	bbapi_supports(BIOSIGRP_CXPWRSUPP, BIOSIOFFS_CXPWRSUPP_GETTYPE)

#define bbapi_supports_sups()                                                  \
	(bbapi_supports(BIOSIGRP_SUPS, BIOSIOFFS_SUPS_GPIO_PIN_EX) ||          \
	 bbapi_supports(BIOSIGRP_SUPS, BIOSIOFFS_SUPS_GPIO_PIN))

static __attribute__((ms_abi)) void __iomem *ExtOsMapPhysAddr(int64_t physAddr,
							      uint32_t memSize)
{
	return ioremap((unsigned long)physAddr, memSize);
}

static __attribute__((ms_abi)) void ExtOsUnMapPhysAddr(void *pLinMem,
						       uint32_t memSize)
{
	iounmap((void __iomem *)pLinMem);
}

struct EXTOS_FUNCTION_ENTRY {
	uint8_t name[8];
	union {
		__attribute__((ms_abi)) void __iomem *(*map)(int64_t, uint32_t);
		__attribute__((ms_abi)) void (*unmap)(void *pLinMem,
						      uint32_t memSize);
		uint64_t placeholder;
	};
};

static struct EXTOS_FUNCTION_ENTRY extOsOps[] = {
	{ "READMSR", { NULL } },
	{ "GETBUSDT", { NULL } },
	{ "MAPMEM", { .map = &ExtOsMapPhysAddr } },
	{ "UNMAPMEM", { .unmap = &ExtOsUnMapPhysAddr } },
	{ "WRITEMSR", { NULL } },
	{ "SETBUSDT", { NULL } },

	/** MARK END OF TABLE */
	{ "\0\0\0\0\0\0\0\0", { NULL } },
};

static int __init bbapi_init_bios(void)
{
	const unsigned int bios_status =
		bbapi_write(0, 0xFE, &extOsOps, sizeof(extOsOps));

	if (bios_status)
		pr_warn("Initializing BIOS failed with: 0x%x\n", bios_status);

	return 0;
}

static void __exit bbapi_exit_bios(void)
{
	const unsigned int bios_status = bbapi_write(0, 0xFF, NULL, 0);

	if (bios_status)
		pr_warn("Unload BIOS failed with: 0x%x\n", bios_status);
}

static const struct dmi_system_id bbapi_unsupported_list[] = {
	{
		.ident = "Hyper-V",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Virtual Machine"),
			DMI_MATCH(DMI_BOARD_NAME, "Virtual Machine"),
		},
	},
	{ }
};

static int __init simple_cdev_init(struct simple_cdev *dev,
				   const char *classname,
				   const char *devicename,
				   const struct file_operations *file_ops)
{
	if (alloc_chrdev_region(&dev->dev, 0, 1, KBUILD_MODNAME) < 0) {
		pr_warn("alloc_chrdev_region() failed!\n");
		return -1;
	}

	cdev_init(&dev->cdev, file_ops);
	dev->cdev.owner = THIS_MODULE;
	kobject_set_name(&dev->cdev.kobj, "%s", devicename);
	if (cdev_add(&dev->cdev, dev->dev, 1) == -1) {
		pr_warn("cdev_add() failed!\n");
		goto rollback_region;
	}

	dev->class = class_create(THIS_MODULE, classname);
	if (dev->class == NULL) {
		pr_warn("class_create() failed!\n");
		goto rollback_cdev;
	}

	if (device_create(dev->class, NULL, dev->dev, NULL, "%s", devicename) ==
	    NULL) {
		pr_warn("device_create() failed!\n");
		goto rollback_class;
	}
	return 0;

rollback_class:
	class_destroy(dev->class);
rollback_cdev:
	cdev_del(&dev->cdev);
rollback_region:
	unregister_chrdev_region(dev->dev, 1);
	return -1;
}

static void __exit simple_cdev_remove(struct simple_cdev *dev)
{
	device_destroy(dev->class, dev->dev);
	class_destroy(dev->class);
	cdev_del(&dev->cdev);
	unregister_chrdev_region(dev->dev, 1);
}

static int __init bbapi_init_module(void)
{
	int result;

	pr_info("%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	mutex_init(&g_bbapi.mutex);

	if (dmi_check_system(bbapi_unsupported_list)) {
		pr_info("BIOS API not supported on this System!\n");
		return -ENODEV;
	}

	result = bbapi_find_bios(&g_bbapi);
	if (result) {
		pr_info("BIOS API not available on this System\n");
		return result;
	}

	if (bbapi_supports_power()) {
		result = platform_device_register(&bbapi_power);
		if (result) {
			pr_info("register %s failed\n", bbapi_power.name);
			goto rollback_memory;
		}
	}

	if (bbapi_supports_sups()) {
		result = platform_device_register(&bbapi_sups);
		if (result) {
			pr_info("register %s failed\n", bbapi_sups.name);
			goto rollback_power;
		}
	}

	result = simple_cdev_init(&g_bbapi.dev, "chardev", KBUILD_MODNAME,
				  &file_ops);
	if (result)
		goto rollback_sups;

	if (bbapi_supports_display())
		update_display();

	return bbapi_init_bios();

rollback_sups:
	if (bbapi_supports_sups())
		platform_device_unregister(&bbapi_sups);

rollback_power:
	if (bbapi_supports_power())
		platform_device_unregister(&bbapi_power);

rollback_memory:
	vfree(g_bbapi.memory);
	return result;
}

static void __exit bbapi_exit(void)
{
	if (!g_bbapi.memory)
		return;

	bbapi_exit_bios();
	simple_cdev_remove(&g_bbapi.dev);

	if (bbapi_supports_sups())
		platform_device_unregister(&bbapi_sups);

	if (bbapi_supports_power())
		platform_device_unregister(&bbapi_power);

	vfree(g_bbapi.memory);
}

module_init(bbapi_init_module);
module_exit(bbapi_exit);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Patrick Bruenn <p.bruenn@beckhoff.com>");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION(DRV_VERSION);
