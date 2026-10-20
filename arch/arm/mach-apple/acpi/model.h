/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __APPLE_ACPI_MODEL_H
#define __APPLE_ACPI_MODEL_H

#include <acpi/acpi_table.h>
#include <linker_lists.h>

/**
 * struct apple_acpi_model - ACPI table set selected by a root DT compatible
 * @compatible: Exact model compatible, not a SoC-wide fallback
 * @dsdt: Compiled DSDT including its header
 * @table_size: Reserved table space, including roots and alignment
 * @fill_fadt: Fill model-specific FADT fields
 * @fill_madt: Append MADT interrupt controller and CPU entries
 * @fill_csrt: Append CSRT resource groups
 * @write_gtdt: Write the GTDT
 * @write_dbg2: Write the DBG2
 */
struct apple_acpi_model {
	const char *compatible;
	const void *dsdt;
	u32 table_size;
	void (*fill_fadt)(struct acpi_fadt *fadt);
	void (*fill_madt)(struct acpi_ctx *ctx);
	int (*fill_csrt)(struct acpi_ctx *ctx);
	acpi_writer_func write_gtdt;
	acpi_writer_func write_dbg2;
};

#define APPLE_ACPI_MODEL(name) \
	ll_entry_declare(struct apple_acpi_model, name, apple_acpi_model)

const struct apple_acpi_model *apple_acpi_get_model(void);

#endif
