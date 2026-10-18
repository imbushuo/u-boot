/* SPDX-License-Identifier: GPL-2.0+ */

#include <linux/linkage.h>

	.section .text.efi_runtime, "ax"
	.align 4

/*
 * Native PSCI passes our slot index in x0. The secondary has no stack or
 * page tables yet; all addresses used here must be physical and PC-relative.
 */
ENTRY(efi_psci_trampoline)
	adrp	x2, efi_psci_cpus_phys
	add	x2, x2, :lo12:efi_psci_cpus_phys
	ldr	x2, [x2]
	mov	x3, #40
	madd	x2, x0, x3, x2
	ldr	x3, [x2, #24]
	mov	sp, x3
	mov	x0, x2
	bl	efi_psci_secondary_start
	mov	x2, x0
	mov	x0, x1
	br	x2
ENDPROC(efi_psci_trampoline)
