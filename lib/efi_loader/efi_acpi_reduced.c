/*
*  EFI ACPI Reduced Hardware table support
*
*  Copyright (c) 2018 Bingxing Wang
*
*  SPDX-License-Identifier:     GPL-2.0+
*/

#include <common.h>
#include <efi_loader.h>
#include <inttypes.h>

/* For now, all ACPI tables are hard-coded, which have additional 100KB overhead. */
#include <acpi/db410c_tables.h>

// static const efi_guid_t acpi_guid = EFI_ACPI_TABLE_GUID;

void efi_acpi_register(void)
{
    #if CONFIG_EFI_TRACING
        printf("EFI Tracing: efi_acpi_register entered \n");
    #endif

    #if CONFIG_EFI_TRACING
        printf("EFI Tracing: efi_acpi_register exit \n");
    #endif
}
