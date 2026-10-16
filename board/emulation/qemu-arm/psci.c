// SPDX-License-Identifier: GPL-2.0+
/*
 * QEMU virt's native PSCI firmware is the platform power backend.
 */

#include <efi_loader.h>
#include <dm.h>
#include <dm/uclass.h>
#include <linux/psci.h>
#include <log.h>

extern U_BOOT_DRIVER(psci);

efi_status_t efi_psci_platform_init(void)
{
	const struct driver *driver = DM_DRIVER_GET(psci);
	struct udevice *dev;
	unsigned long version;

	if (uclass_get_device_by_driver(UCLASS_FIRMWARE, driver, &dev))
		return EFI_DEVICE_ERROR;
	version = invoke_psci_fn(PSCI_0_2_FN_PSCI_VERSION, 0, 0, 0);
	if ((long)version < 0 || PSCI_VERSION_MAJOR(version) < 1) {
		log_warning("EFI PSCI: native PSCI 1.0 is unavailable\n");
		return EFI_UNSUPPORTED;
	}

	return EFI_SUCCESS;
}

unsigned long __efi_runtime
efi_psci_platform_call(unsigned long fid, unsigned long arg0,
		       unsigned long arg1, unsigned long arg2)
{
	return invoke_psci_fn(fid, arg0, arg1, arg2);
}
