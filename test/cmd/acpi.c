// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Tests for acpi command
 */

#include <acpi/acpi_table.h>
#include <asm/global_data.h>
#include <tables_csum.h>
#include <linux/bitops.h>
#include <test/cmd.h>
#include <test/ut.h>

DECLARE_GLOBAL_DATA_PTR;

#define HAVE_RSDP BIT(0)
#define HAVE_XSDT BIT(1)
#define HAVE_FADT BIT(2)
#define HAVE_ALL (HAVE_RSDP | HAVE_XSDT | HAVE_FADT)

/**
 * cmd_test_acpi() - test the acpi command
 */
static int cmd_test_acpi(struct unit_test_state *uts)
{
	unsigned int actual = 0;
	int ret;

	/*
	 * Check that some mandatory ACPI tables exist:
	 * - RSDP
	 * - RSDT or XSDT
	 * - FADT
	 */
	ut_assertok(run_commandf("acpi list"));
	ut_assert_nextline("Name              Base   Size  Detail");
	ut_assert_nextline("----  ----------------  -----  ----------------------------");
	for (;;) {
		ret = console_record_readline(uts->actual_str, sizeof(uts->actual_str));
		if (ret == -ENOENT) {
			ut_asserteq(HAVE_ALL, actual);

			return 0;
		}
		if (ret < 0)
			ut_asserteq(0, ret);

		if (!strncmp("RSDP", uts->actual_str, 4))
			actual |= HAVE_RSDP;
		else if (!strncmp("RSDT", uts->actual_str, 4))
			actual |= HAVE_XSDT;
		else if (!strncmp("XSDT", uts->actual_str, 4))
			actual |= HAVE_XSDT;
		else if (!strncmp("FACP", uts->actual_str, 4))
			actual |= HAVE_FADT;
	}
}
CMD_TEST(cmd_test_acpi, UTF_CONSOLE);

static int cmd_test_acpi_disable_psci(struct unit_test_state *uts)
{
	struct acpi_fadt *fadt = (struct acpi_fadt *)acpi_find_table("FACP");
	ulong saved_start = gd_acpi_start();
	u16 saved_flags;
	u32 saved_length;

	ut_assertnonnull(fadt);
	saved_flags = fadt->arm_boot_arch;
	saved_length = fadt->header.length;
	fadt->arm_boot_arch = BIT(15) | BIT(1) |
		ACPI_ARM_PSCI_COMPLIANT | ACPI_ARM_PSCI_USE_EFI;
	acpi_update_checksum(&fadt->header);

	ut_assertok(run_commandf("acpi disable-psci"));
	ut_assert_nextline("FADT PSCI and EFI conduit flags cleared");
	ut_asserteq(BIT(15) | BIT(1), fadt->arm_boot_arch);
	ut_asserteq(0, table_compute_checksum(fadt, fadt->header.length));
	ut_assertok(run_commandf("acpi disable-psci"));
	ut_assert_nextline("FADT PSCI and EFI conduit flags cleared");
	ut_asserteq(0, table_compute_checksum(fadt, fadt->header.length));

	fadt->header.length = offsetof(struct acpi_fadt, arm_boot_arch);
	ut_asserteq(CMD_RET_FAILURE, run_commandf("acpi disable-psci"));
	ut_assert_nextline("Cannot find a valid FADT with ARM boot flags");
	ut_asserteq(BIT(15) | BIT(1), fadt->arm_boot_arch);
	fadt->header.length = saved_length;
	fadt->arm_boot_arch = saved_flags;
	acpi_update_checksum(&fadt->header);

	gd_set_acpi_start(0);
	ut_asserteq(CMD_RET_FAILURE, run_commandf("acpi disable-psci"));
	ut_assert_nextline("Cannot find a valid FADT with ARM boot flags");
	gd_set_acpi_start(saved_start);
	ut_assert_console_end();

	return 0;
}

CMD_TEST(cmd_test_acpi_disable_psci, UTF_CONSOLE);
