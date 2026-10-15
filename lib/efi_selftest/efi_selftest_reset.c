// SPDX-License-Identifier: GPL-2.0+
/*
 * efi_selftest_reset
 *
 * Copyright (c) 2020 Heinrich Schuchardt <xypron.glpk@gmx.de>
 *
 * This test checks the following service at boot time or runtime:
 * ResetSystem()
 */

#include <efi_selftest.h>

/*
 * Execute unit test.
 *
 * Return:	EFI_ST_SUCCESS for success
 */
static int execute_reset(enum efi_reset_type type)
{
	u16 reset_data[] = u"Reset by selftest";

	st_runtime->reset_system(type, EFI_SUCCESS,
			      sizeof(reset_data), reset_data);
	efi_st_error("Reset failed.\n");
	return EFI_ST_FAILURE;
}

static int execute(void)
{
	return execute_reset(EFI_RESET_COLD);
}

static int execute_shutdown(void)
{
	return execute_reset(EFI_RESET_SHUTDOWN);
}

EFI_UNIT_TEST(reset) = {
	.name = "reset system",
	.phase = EFI_EXECUTE_BEFORE_BOOTTIME_EXIT,
	.execute = execute,
	.on_request = true,
};

EFI_UNIT_TEST(resetrt) = {
	.name = "reset system runtime",
	.phase = EFI_SETUP_BEFORE_BOOTTIME_EXIT,
	.execute = execute,
	.on_request = true,
};

EFI_UNIT_TEST(shutdown) = {
	.name = "shutdown system",
	.phase = EFI_EXECUTE_BEFORE_BOOTTIME_EXIT,
	.execute = execute_shutdown,
	.on_request = true,
};

EFI_UNIT_TEST(shutdownrt) = {
	.name = "shutdown system runtime",
	.phase = EFI_SETUP_BEFORE_BOOTTIME_EXIT,
	.execute = execute_shutdown,
	.on_request = true,
};
