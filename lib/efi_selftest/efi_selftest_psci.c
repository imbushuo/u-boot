// SPDX-License-Identifier: GPL-2.0+
/*
 * Check the EFI PSCI configuration-table ABI and secondary CPU trampoline.
 */

#include <efi_selftest.h>

#define PSCI_FN_VERSION		0x84000000
#define PSCI_FN_CPU_OFF		0x84000002
#define PSCI_FN_CPU_ON		0xc4000003
#define PSCI_FN_AFFINITY_INFO	0xc4000004
#define PSCI_FN_FEATURES	0x8400000a
#define PSCI_FN_SYSTEM_OFF	0x84000008
#define PSCI_FN_SYSTEM_RESET	0x84000009

static const efi_guid_t psci_guid = LINUX_EFI_ARM_PSCI_HANDLER_TABLE_GUID;
static struct efi_arm_psci_handler_table *psci_table;
static u64 secondary_seen;
static struct efi_mem_desc *virtual_map;
static efi_uintn_t virtual_map_size;
static efi_uintn_t virtual_desc_size;
static u32 virtual_desc_version;

static int check_runtime_memory(const struct efi_system_table *table)
{
	efi_uintn_t size = 0, key, desc_size, offset;
	struct efi_mem_desc *map;
	efi_status_t ret;
	bool table_found = false, code_found = false;
	const u64 table_addr = (uintptr_t)psci_table;
	const u64 handler_addr = (uintptr_t)psci_table->handler;

	ret = table->boottime->get_memory_map(&size, NULL, &key,
					      &desc_size, NULL);
	if (ret != EFI_BUFFER_TOO_SMALL)
		return EFI_ST_FAILURE;
	size += 32 * sizeof(*map);
	ret = table->boottime->allocate_pool(EFI_BOOT_SERVICES_DATA, size,
					     (void **)&map);
	if (ret != EFI_SUCCESS)
		return EFI_ST_FAILURE;
	ret = table->boottime->get_memory_map(&size, map, &key,
					      &desc_size, NULL);
	if (ret != EFI_SUCCESS) {
		table->boottime->free_pool(map);
		return EFI_ST_FAILURE;
	}

	for (offset = 0; offset < size; offset += desc_size) {
		const struct efi_mem_desc *entry = (void *)map + offset;
		u64 base = entry->physical_start;
		u64 bytes = entry->num_pages << EFI_PAGE_SHIFT;

		if (table_addr >= base && table_addr < base + bytes) {
			table_found = entry->type == EFI_RUNTIME_SERVICES_DATA &&
				      (entry->attribute &
				       (EFI_MEMORY_WB | EFI_MEMORY_RUNTIME)) ==
				      (EFI_MEMORY_WB | EFI_MEMORY_RUNTIME) &&
				      bytes >= 129 * 0x10000 &&
				      !(base & 0xffff);
		}
		if (handler_addr >= base && handler_addr < base + bytes) {
			code_found = entry->type == EFI_RUNTIME_SERVICES_CODE &&
				     (entry->attribute &
				      (EFI_MEMORY_WB | EFI_MEMORY_RUNTIME)) ==
				     (EFI_MEMORY_WB | EFI_MEMORY_RUNTIME) &&
				     !(base & 0xffff);
		}
	}
	ret = table->boottime->free_pool(map);
	if (ret != EFI_SUCCESS || !table_found || !code_found) {
		efi_st_error("EFI PSCI runtime code/data memory map is invalid\n");
		return EFI_ST_FAILURE;
	}

	return EFI_ST_SUCCESS;
}

static void EFIAPI efi_psci_test_secondary(u64 context)
{
	u64 *seen = (void *)(uintptr_t)context;
	u64 value = 0x1234abcd;

	asm volatile("stlr %x1, [%0]" : : "r"(seen), "r"(value) : "memory");
	psci_table->handler(PSCI_FN_CPU_OFF, 0, 0, 0);
	for (;;)
		asm volatile("wfe");
}

static int setup(const efi_handle_t handle,
		 const struct efi_system_table *table)
{
	efi_uintn_t i;

	for (i = 0; i < table->nr_tables; i++) {
		if (!memcmp(&table->tables[i].guid, &psci_guid,
			    sizeof(psci_guid))) {
			psci_table = table->tables[i].table;
		}
	}
	if (!psci_table || !psci_table->handler) {
		efi_st_error("EFI PSCI configuration table not found\n");
		return EFI_ST_FAILURE;
	}

	return check_runtime_memory(table);
}

static int execute(void)
{
	unsigned long ret;
	u64 target = 0x70f;
	u64 observed;
	u32 i;

	if (psci_table->version != 0x10000 ||
	    psci_table->features[3] != 0 ||
	    psci_table->features[5] != -1 ||
	    psci_table->handler(PSCI_FN_VERSION, 0, 0, 0) != 0x10000 ||
	    psci_table->handler(PSCI_FN_FEATURES, PSCI_FN_CPU_ON, 0, 0) != 0 ||
	    (long)psci_table->handler(0xc3000001, 0, 0, 0) != -1) {
		efi_st_error("EFI PSCI version/features mismatch\n");
		return EFI_ST_FAILURE;
	}
	/* Exercise the last of 128 CPUs if present, otherwise CPU 1. */
	ret = psci_table->handler(PSCI_FN_AFFINITY_INFO, target, 0, 0);
	if ((int)ret == -2) {
		target = 1;
		ret = psci_table->handler(PSCI_FN_AFFINITY_INFO, target, 0, 0);
	}
	if (ret != 1) {
		efi_st_error("CPU %u is not offline (%d)\n",
			     (u32)target, (int)ret);
		return EFI_ST_FAILURE;
	}

	secondary_seen = 0;
	ret = psci_table->handler(PSCI_FN_CPU_ON, target,
				  (uintptr_t)efi_psci_test_secondary,
				  (uintptr_t)&secondary_seen);
	if (ret) {
		efi_st_error("EFI PSCI CPU_ON failed: %d\n", (int)ret);
		return EFI_ST_FAILURE;
	}
	for (i = 0; i < 100000000; i++) {
		asm volatile("ldar %x0, [%1]" : "=r"(observed)
			     : "r"(&secondary_seen) : "memory");
		if (observed == 0x1234abcd)
			break;
	}
	if (i == 100000000) {
		efi_st_error("EFI PSCI secondary did not reach its OS entry\n");
		return EFI_ST_FAILURE;
	}
	efi_st_printf("EFI PSCI CPU_ON reached CPU %u with its context\n",
		      (u32)target);

	return EFI_ST_SUCCESS;
}

EFI_UNIT_TEST(psci_conduit) = {
	.name = "psci conduit",
	.phase = EFI_EXECUTE_BEFORE_BOOTTIME_EXIT,
	.setup = setup,
	.execute = execute,
	.on_request = true,
};

EFI_UNIT_TEST(psci_conduit_runtime) = {
	.name = "psci conduit runtime",
	.phase = EFI_SETUP_BEFORE_BOOTTIME_EXIT,
	.setup = setup,
	.execute = execute,
	.on_request = true,
};

static int setup_virtual(const efi_handle_t handle,
			 const struct efi_system_table *table)
{
	efi_uintn_t key, required = 0;
	efi_status_t ret;
	int i;

	if (setup(handle, table))
		return EFI_ST_FAILURE;

	ret = st_boottime->get_memory_map(&required, NULL, &key,
					  &virtual_desc_size,
					  &virtual_desc_version);
	if (ret != EFI_BUFFER_TOO_SMALL)
		return EFI_ST_FAILURE;
	virtual_map_size = required + 32 * sizeof(struct efi_mem_desc);
	ret = st_boottime->allocate_pool(EFI_BOOT_SERVICES_DATA,
					 virtual_map_size, (void **)&virtual_map);
	if (ret != EFI_SUCCESS)
		return EFI_ST_FAILURE;
	ret = st_boottime->get_memory_map(&virtual_map_size, virtual_map,
					  &key, &virtual_desc_size,
					  &virtual_desc_version);
	if (ret != EFI_SUCCESS)
		return EFI_ST_FAILURE;
	for (i = 0; i < virtual_map_size / virtual_desc_size; i++) {
		struct efi_mem_desc *desc = (void *)virtual_map +
					   i * virtual_desc_size;

		if (desc->attribute & EFI_MEMORY_RUNTIME)
			desc->virtual_start = desc->physical_start;
	}

	return EFI_ST_SUCCESS;
}

static int execute_virtual(void)
{
	efi_status_t ret;

	ret = st_runtime->set_virtual_address_map(virtual_map_size,
						   virtual_desc_size,
						   virtual_desc_version,
						   virtual_map);
	if (ret != EFI_SUCCESS) {
		efi_st_error("EFI PSCI SetVirtualAddressMap failed\n");
		return EFI_ST_FAILURE;
	}

	return execute();
}

EFI_UNIT_TEST(psci_conduit_virtual) = {
	.name = "psci conduit virtual",
	.phase = EFI_SETTING_VIRTUAL_ADDRESS_MAP,
	.setup = setup_virtual,
	.execute = execute_virtual,
	.on_request = true,
};

static int execute_poweroff(void)
{
	psci_table->handler(PSCI_FN_SYSTEM_OFF, 0, 0, 0);
	efi_st_error("EFI PSCI SYSTEM_OFF returned\n");
	return EFI_ST_FAILURE;
}

static int execute_reset(void)
{
	psci_table->handler(PSCI_FN_SYSTEM_RESET, 0, 0, 0);
	efi_st_error("EFI PSCI SYSTEM_RESET returned\n");
	return EFI_ST_FAILURE;
}

EFI_UNIT_TEST(psci_conduit_poweroff) = {
	.name = "psci conduit poweroff",
	.phase = EFI_SETUP_BEFORE_BOOTTIME_EXIT,
	.setup = setup,
	.execute = execute_poweroff,
	.on_request = true,
};

EFI_UNIT_TEST(psci_conduit_reset) = {
	.name = "psci conduit reset",
	.phase = EFI_SETUP_BEFORE_BOOTTIME_EXIT,
	.setup = setup,
	.execute = execute_reset,
	.on_request = true,
};
