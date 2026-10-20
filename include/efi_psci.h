/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __EFI_PSCI_H
#define __EFI_PSCI_H

#include <dm/ofnode.h>
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
	void *release_slot;
};

extern struct efi_psci_cpu *__efi_runtime_data efi_psci_cpus;
extern u32 __efi_runtime_data efi_psci_cpu_count;

efi_status_t efi_psci_platform_init(void);
efi_status_t efi_psci_platform_cpu_init(ofnode node, struct efi_psci_cpu *cpu);
efi_status_t efi_psci_platform_prepare(struct efi_psci_cpu *cpus, u32 count);
bool __efi_runtime efi_psci_platform_supports(unsigned long fid);
unsigned long __efi_runtime
efi_psci_platform_call(unsigned long fid, unsigned long arg0,
		       unsigned long arg1, unsigned long arg2);
void __efi_runtime efi_psci_platform_secondary_init(u64 mpidr);
void efi_psci_trampoline(void);

#endif
