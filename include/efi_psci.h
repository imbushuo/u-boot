/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __EFI_PSCI_H
#define __EFI_PSCI_H

#include <efi_loader.h>

#define EFI_PSCI_MAX_CPUS	128
#define EFI_PSCI_STACK_SIZE	0x10000

struct efi_psci_cpu {
	u64 mpidr;
	u64 entry;
	u64 context;
	u64 stack_top;
	u32 busy;
	u32 reserved;
};

efi_status_t efi_psci_platform_init(void);
unsigned long __efi_runtime
efi_psci_platform_call(unsigned long fid, unsigned long arg0,
		       unsigned long arg1, unsigned long arg2);
void __efi_runtime efi_psci_platform_secondary_init(u64 mpidr);
void efi_psci_trampoline(void);

#endif
