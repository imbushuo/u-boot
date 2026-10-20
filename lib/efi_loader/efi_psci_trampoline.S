/* SPDX-License-Identifier: GPL-2.0+ */

#include <linux/linkage.h>

	.section .text.efi_runtime, "ax"
	.align 4

/*
 * Native PSCI passes our slot index in x0. Apple's m1n1 spin table does not,
 * so match MPIDR_EL1 instead. All addresses must be physical and PC-relative.
 */
ENTRY(efi_psci_trampoline)
	adrp	x2, efi_psci_cpus_phys
	add	x2, x2, :lo12:efi_psci_cpus_phys
	ldr	x2, [x2]
#ifdef CONFIG_ARCH_APPLE
	adrp	x5, efi_psci_cpu_count
	add	x5, x5, :lo12:efi_psci_cpu_count
	ldr	w5, [x5]
	mrs	x4, mpidr_el1
	ubfx	x6, x4, #32, #8
	and	x4, x4, #0xffffff
	orr	x4, x4, x6, lsl #32
1:	cbz	w5, 2f
	ldr	x6, [x2]
	cmp	x6, x4
	b.eq	3f
	add	x2, x2, #48
	sub	w5, w5, #1
	b	1b
2:	wfe
	b	2b
3:
#else
	mov	x3, #48
	madd	x2, x0, x3, x2
#endif
	ldr	x3, [x2, #24]
	mov	sp, x3
	mov	x0, x2
	bl	efi_psci_secondary_start
	mov	x2, x0
	mov	x0, x1
	br	x2
ENDPROC(efi_psci_trampoline)
