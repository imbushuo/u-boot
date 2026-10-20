// SPDX-License-Identifier: GPL-2.0+
/*
 * Exercise the EFI PSCI conduit and then reset or shut down the system.
 */

#include <efi_api.h>
#include <asm/psci.h>
#include <linux/libfdt.h>

#define SECONDARY_READY	0x1234abcd5678ef90ULL
#define FDT_MAX_SIZE	(16 * 1024 * 1024)

static const efi_guid_t loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const efi_guid_t psci_guid = LINUX_EFI_ARM_PSCI_HANDLER_TABLE_GUID;
static const efi_guid_t fdt_guid = EFI_FDT_GUID;
static u64 secondary_ready;

static bool is_space(u16 c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool has_option(const struct efi_loaded_image *image, const u16 *option)
{
	const u16 *input = image->load_options;
	size_t count = image->load_options_size / sizeof(*input);
	size_t i = 0, j = 0;

	if (!input || image->load_options_size % sizeof(*input))
		return false;
	while (i < count && is_space(input[i]))
		i++;
	while (option[j]) {
		if (i == count || input[i++] != option[j++])
			return false;
	}
	for (; i < count; i++) {
		if (input[i] && !is_space(input[i]))
			return false;
	}
	return true;
}

static void print_hex(struct efi_simple_text_output_protocol *out, u64 value)
{
	u16 text[19] = u"0x0000000000000000";
	static const char digits[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 16; i++)
		text[i + 2] = digits[(value >> (60 - 4 * i)) & 0xf];
	out->output_string(out, text);
}

static void *find_table(struct efi_system_table *systab,
			const efi_guid_t *guid)
{
	efi_uintn_t i;

	for (i = 0; i < systab->nr_tables; i++)
		if (!memcmp(&systab->tables[i].guid, guid, sizeof(*guid)))
			return systab->tables[i].table;
	return NULL;
}

static bool fdt_name_eq(const char *name, size_t length, const char *expected)
{
	size_t i = 0;

	while (i < length && expected[i] && name[i] == expected[i])
		i++;
	return i < length && !name[i] && !expected[i];
}

static efi_status_t find_offline_cpu(const void *fdt,
				     const struct efi_arm_psci_handler_table *psci,
				     u64 *target)
{
	const struct fdt_header *header = fdt;
	const u8 *bytes = fdt, *pos, *end, *strings;
	const fdt32_t *reg = NULL;
	u32 total, offset, size, strings_off, strings_size;
	u32 addr_cells = 0, reg_size = 0;
	int depth = 0;
	bool cpus = false, cpu = false, enabled = true;

	if (!fdt || fdt32_to_cpu(header->magic) != FDT_MAGIC)
		return EFI_LOAD_ERROR;
	total = fdt32_to_cpu(header->totalsize);
	offset = fdt32_to_cpu(header->off_dt_struct);
	size = fdt32_to_cpu(header->size_dt_struct);
	strings_off = fdt32_to_cpu(header->off_dt_strings);
	strings_size = fdt32_to_cpu(header->size_dt_strings);
	if (total < sizeof(*header) || total > FDT_MAX_SIZE ||
	    (offset & 3) || offset > total || size > total - offset ||
	    strings_off > total || strings_size > total - strings_off)
		return EFI_LOAD_ERROR;

	pos = bytes + offset;
	end = pos + size;
	strings = bytes + strings_off;
	while (pos < end) {
		const u8 *name;
		u32 tag;

		if (end - pos < sizeof(fdt32_t))
			return EFI_LOAD_ERROR;
		tag = fdt32_to_cpu(*(const fdt32_t *)pos);
		pos += sizeof(fdt32_t);

		switch (tag) {
		case FDT_BEGIN_NODE:
			name = pos;
			while (pos < end && *pos)
				pos++;
			if (pos == end)
				return EFI_LOAD_ERROR;
			pos = (const u8 *)(((uintptr_t)pos + 4) & ~3UL);
			if (pos > end)
				return EFI_LOAD_ERROR;
			depth++;
			if (depth == 2 && fdt_name_eq((const char *)name,
						      pos - name, "cpus")) {
				cpus = true;
				addr_cells = 0;
			} else if (cpus && depth == 3) {
				cpu = pos - name >= 4 &&
				      !memcmp(name, "cpu@", 4);
				reg = NULL;
				reg_size = 0;
				enabled = true;
			}
			break;
		case FDT_PROP: {
			const u8 *value;
			const char *property;
			u32 len, nameoff;

			if (end - pos < 2 * sizeof(fdt32_t))
				return EFI_LOAD_ERROR;
			len = fdt32_to_cpu(*(const fdt32_t *)pos);
			nameoff = fdt32_to_cpu(*(const fdt32_t *)(pos + 4));
			pos += 2 * sizeof(fdt32_t);
			if (len > end - pos || nameoff >= strings_size)
				return EFI_LOAD_ERROR;
			property = (const char *)strings + nameoff;
			for (offset = nameoff;
			     offset < strings_size && strings[offset]; offset++)
				;
			if (offset == strings_size)
				return EFI_LOAD_ERROR;
			value = pos;
			pos += (len + 3) & ~3U;
			if (pos > end)
				return EFI_LOAD_ERROR;
			if (cpus && depth == 2 &&
			    fdt_name_eq(property, offset - nameoff + 1,
					"#address-cells") && len == 4)
				addr_cells = fdt32_to_cpu(*(const fdt32_t *)value);
			if (!cpu || depth != 3)
				break;
			if (fdt_name_eq(property, offset - nameoff + 1, "reg")) {
				reg = (const fdt32_t *)value;
				reg_size = len;
			} else if (fdt_name_eq(property, offset - nameoff + 1,
					       "status")) {
				enabled = (len == 5 && !memcmp(value, "okay", 5)) ||
					  (len == 3 && !memcmp(value, "ok", 3));
			}
			break;
		}
		case FDT_END_NODE:
			if (!depth)
				return EFI_LOAD_ERROR;
			if (cpu && depth == 3) {
				u64 mpidr;
				unsigned long state;

				if (enabled && reg && reg_size == addr_cells * 4 &&
				    (addr_cells == 1 || addr_cells == 2)) {
					mpidr = fdt32_to_cpu(reg[0]);
					if (addr_cells == 2)
						mpidr = mpidr << 32 |
							fdt32_to_cpu(reg[1]);
					state = psci->handler(ARM_PSCI_0_2_FN64_AFFINITY_INFO,
							      mpidr, 0, 0);
					if (state == PSCI_AFFINITY_LEVEL_OFF) {
						*target = mpidr;
						return EFI_SUCCESS;
					}
				}
				cpu = false;
			}
			if (cpus && depth == 2)
				cpus = false;
			depth--;
			break;
		case FDT_NOP:
			break;
		case FDT_END:
			return depth ? EFI_LOAD_ERROR : EFI_NOT_FOUND;
		default:
			return EFI_LOAD_ERROR;
		}
	}
	return EFI_LOAD_ERROR;
}

static void EFIAPI secondary_entry(u64 context)
{
	u64 *ready = (void *)(uintptr_t)context;
	u64 value = SECONDARY_READY;

	asm volatile("stlr %x1, [%0]" : : "r"(ready), "r"(value) : "memory");
	for (;;)
		asm volatile("wfe");
}

efi_status_t EFIAPI efi_main(efi_handle_t handle, struct efi_system_table *systab)
{
	struct efi_simple_text_output_protocol *out = systab->con_out;
	struct efi_arm_psci_handler_table *psci;
	struct efi_loaded_image *image;
	struct efi_boot_services *bs = systab->boottime;
	const void *fdt;
	unsigned long fid, result;
	efi_status_t status;
	u64 target, observed;
	u32 i;

	status = bs->handle_protocol(handle, &loaded_image_guid, (void **)&image);
	if (status != EFI_SUCCESS)
		return status;
	if (has_option(image, u"reset")) {
		fid = ARM_PSCI_0_2_FN_SYSTEM_RESET;
	} else if (has_option(image, u"poweroff")) {
		fid = ARM_PSCI_0_2_FN_SYSTEM_OFF;
	} else {
		out->output_string(out, u"Usage: set bootargs to reset or poweroff\r\n");
		return EFI_INVALID_PARAMETER;
	}

	psci = find_table(systab, &psci_guid);
	fdt = find_table(systab, &fdt_guid);
	if (!psci || !psci->handler || psci->version < ARM_PSCI_VER_1_0 ||
	    psci->features[3] != ARM_PSCI_RET_SUCCESS ||
	    psci->features[4] != ARM_PSCI_RET_SUCCESS ||
	    psci->features[fid & (EFI_PSCI_MAX_FN - 1)] != ARM_PSCI_RET_SUCCESS ||
	    !fdt) {
		out->output_string(out, u"PSCI conduit, power action, or DT unavailable\r\n");
		return EFI_UNSUPPORTED;
	}
	status = find_offline_cpu(fdt, psci, &target);
	if (status != EFI_SUCCESS) {
		out->output_string(out, u"No offline secondary CPU in device tree\r\n");
		return status;
	}

	out->output_string(out, u"Starting secondary CPU ");
	print_hex(out, target);
	out->output_string(out, u"\r\n");
	secondary_ready = 0;
	result = psci->handler(ARM_PSCI_0_2_FN64_CPU_ON, target,
			       (uintptr_t)secondary_entry,
			       (uintptr_t)&secondary_ready);
	if (result != ARM_PSCI_RET_SUCCESS) {
		out->output_string(out, u"PSCI CPU_ON failed: ");
		print_hex(out, result);
		out->output_string(out, u"\r\n");
		return EFI_DEVICE_ERROR;
	}

	for (i = 0; i < 500; i++) {
		asm volatile("ldar %x0, [%1]" : "=r"(observed)
			     : "r"(&secondary_ready) : "memory");
		if (observed == SECONDARY_READY)
			break;
		if (bs->stall(10000) != EFI_SUCCESS)
			break;
	}
	if (i == 500)
		out->output_string(out, u"Secondary CPU did not signal within 5s\r\n");
	else if (observed != SECONDARY_READY)
		out->output_string(out, u"Unable to wait for secondary CPU\r\n");
	else
		out->output_string(out, u"Secondary CPU reached its entry point\r\n");

	out->output_string(out, u"Waiting 15 seconds before power action\r\n");
	for (i = 0; i < 15; i++) {
		if (bs->stall(1000000) != EFI_SUCCESS) {
			out->output_string(out, u"Stall failed; keeping image resident\r\n");
			goto park;
		}
	}

	out->output_string(out, fid == ARM_PSCI_0_2_FN_SYSTEM_RESET ?
			   u"Requesting PSCI system reset\r\n" :
			   u"Requesting PSCI system poweroff\r\n");
	result = psci->handler(fid, 0, 0, 0);
	out->output_string(out, u"PSCI power action returned: ");
	print_hex(out, result);
	out->output_string(out, u"\r\n");

park:
	/* The secondary may still be executing this image, so never unload it. */
	for (;;)
		asm volatile("wfe");
}
