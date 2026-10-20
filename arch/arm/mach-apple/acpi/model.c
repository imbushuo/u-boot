// SPDX-License-Identifier: GPL-2.0+

#include <dm/ofnode.h>
#include <linux/errno.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include "model.h"

const struct apple_acpi_model *apple_acpi_get_model(void)
{
	const struct apple_acpi_model *models =
		ll_entry_start(struct apple_acpi_model, apple_acpi_model);
	const int count = ll_entry_count(struct apple_acpi_model, apple_acpi_model);
	const char *compatible;
	ofnode root = ofnode_path("/");
	int i, j;

	for (i = 0; !ofnode_read_string_index(root, "compatible", i, &compatible); i++) {
		for (j = 0; j < count; j++) {
			if (!strcmp(compatible, models[j].compatible))
				return &models[j];
		}
	}

	return NULL;
}

u32 acpi_get_table_size(void)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	return model ? ALIGN(model->table_size, SZ_4K) : 0;
}

const void *acpi_get_dsdt(void)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	return model ? model->dsdt : NULL;
}

bool acpi_table_enabled(const struct acpi_writer *entry)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	if (!model)
		return false;

	/* Hardware-reduced Apple platforms need neither FACS nor legacy SPCR. */
	if (!strcmp(entry->name, "0base") || !strcmp(entry->table ?: "", "DSDT") ||
	    !strcmp(entry->table ?: "", "FADT") || !strcmp(entry->table ?: "", "MADT"))
		return true;
	if (!strcmp(entry->table ?: "", "CSRT"))
		return !!model->fill_csrt;
	if (!strcmp(entry->table ?: "", "GTDT"))
		return !!model->write_gtdt;
	if (!strcmp(entry->table ?: "", "DBG2"))
		return !!model->write_dbg2;

	return false;
}

void acpi_fill_fadt(struct acpi_fadt *fadt)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	model->fill_fadt(fadt);
}

void *acpi_fill_madt(struct acpi_madt *madt, struct acpi_ctx *ctx)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	model->fill_madt(ctx);
	return ctx->current;
}

int acpi_fill_csrt(struct acpi_ctx *ctx)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	if (!model || !model->fill_csrt)
		return -ENOENT;
	return model->fill_csrt(ctx);
}

static int apple_write_gtdt(struct acpi_ctx *ctx, const struct acpi_writer *entry)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	if (!model || !model->write_gtdt)
		return -ENOENT;
	return model->write_gtdt(ctx, entry);
}

ACPI_WRITER(5gtdt, "GTDT", apple_write_gtdt, 0);

static int apple_write_dbg2(struct acpi_ctx *ctx, const struct acpi_writer *entry)
{
	const struct apple_acpi_model *model = apple_acpi_get_model();

	if (!model || !model->write_dbg2)
		return -ENOENT;
	return model->write_dbg2(ctx, entry);
}

ACPI_WRITER(5dbg2, "DBG2", apple_write_dbg2, 0);
