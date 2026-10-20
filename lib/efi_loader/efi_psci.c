// SPDX-License-Identifier: GPL-2.0+
/*
 * Experimental PSCI conduit through an EFI configuration table.
 *
 * The table's ABI follows AsahiLinux/m1n1#618. The power backend and the
 * secondary setup hook can be supplied by the board.
 */

#define LOG_CATEGORY LOGC_EFI

#include <dm/ofnode.h>
#include <efi_psci.h>
#include <linux/psci.h>
#include <log.h>
#include <asm/psci.h>
#include <linux/sizes.h>

static const efi_guid_t psci_guid = LINUX_EFI_ARM_PSCI_HANDLER_TABLE_GUID;

struct efi_psci_state {
	struct efi_arm_psci_handler_table table;
	u32 cpu_count;
	struct efi_psci_cpu cpus[EFI_PSCI_MAX_CPUS];
};

#define EFI_PSCI_RUNTIME_SIZE	(SZ_64K + EFI_PSCI_MAX_CPUS * EFI_PSCI_STACK_SIZE)

struct efi_psci_cpu __efi_runtime_data *efi_psci_cpus_phys;
struct efi_psci_cpu *__efi_runtime_data efi_psci_cpus;
u32 __efi_runtime_data efi_psci_cpu_count;
static struct efi_psci_state __efi_runtime_data *efi_psci_state;
static struct efi_psci_state *efi_psci_state_phys;
static __efi_runtime_data u64 efi_psci_trampoline_phys;

static_assert(sizeof(struct efi_psci_cpu) == 48);
static_assert(offsetof(struct efi_psci_cpu, stack_top) == 24);
static_assert(offsetof(struct efi_arm_psci_handler_table, handler) == 8);
static_assert(sizeof(struct efi_arm_psci_handler_table) == 144);
static_assert(sizeof(struct efi_psci_state) <= SZ_64K);

void __weak __efi_runtime efi_psci_platform_secondary_init(u64 mpidr)
{
}

efi_status_t __weak efi_psci_platform_cpu_init(ofnode node,
					       struct efi_psci_cpu *cpu)
{
	return EFI_SUCCESS;
}

efi_status_t __weak efi_psci_platform_prepare(struct efi_psci_cpu *cpus,
					      u32 count)
{
	return EFI_SUCCESS;
}

bool __weak __efi_runtime efi_psci_platform_supports(unsigned long fid)
{
	return true;
}

static bool __efi_runtime efi_psci_claim_cpu(struct efi_psci_cpu *cpu)
{
	u32 old, failed;
	u32 claimed = 1;

	do {
		asm volatile("ldaxr %w0, [%2]\n"
			     "cbnz %w0, 1f\n"
			     "stxr %w1, %w3, [%2]\n"
			     "b 2f\n"
			     "1: clrex\n"
			     "2:"
			     : "=&r"(old), "=&r"(failed)
			     : "r"(&cpu->busy), "r"(claimed)
			     : "memory");
	} while (!old && failed);

	return !old;
}

static unsigned long __efi_runtime
efi_psci_cpu_on(unsigned long target, unsigned long entry,
		unsigned long context)
{
	struct efi_psci_cpu *cpu = NULL;
	unsigned long ret;
	u32 i;

	if (!entry || (entry & 3))
		return PSCI_RET_INVALID_ADDRESS;

	for (i = 0; i < efi_psci_state->cpu_count; i++) {
		if (efi_psci_state->cpus[i].mpidr == target) {
			cpu = &efi_psci_state->cpus[i];
			break;
		}
	}
	if (!cpu)
		return PSCI_RET_INVALID_PARAMS;
	if (!efi_psci_claim_cpu(cpu))
		return PSCI_RET_ON_PENDING;

	cpu->entry = entry;
	cpu->context = context;
	asm volatile("dsb ishst" : : : "memory");
	ret = efi_psci_platform_call(ARM_PSCI_0_2_FN64_CPU_ON, target,
				     efi_psci_trampoline_phys, i);
	if (ret)
		cpu->busy = 0;

	return ret;
}

struct efi_psci_entry {
	u64 entry;
	u64 context;
};

struct efi_psci_entry __efi_runtime
efi_psci_secondary_start(struct efi_psci_cpu *cpu)
{
	struct efi_psci_entry start = { cpu->entry, cpu->context };

	efi_psci_platform_secondary_init(cpu->mpidr);
	asm volatile("stlr %w1, [%0]" : : "r"(&cpu->busy), "r"(0) : "memory");

	return start;
}

static unsigned long __efi_runtime EFIAPI
efi_psci_handler(unsigned long fid, unsigned long arg0, unsigned long arg1,
		 unsigned long arg2)
{
	if (fid == ARM_PSCI_0_2_FN_PSCI_VERSION)
		return ARM_PSCI_VER_1_0;
	if (fid == ARM_PSCI_1_0_FN_PSCI_FEATURES) {
		if (arg0 == ARM_PSCI_0_2_FN_PSCI_VERSION ||
		    arg0 == ARM_PSCI_1_0_FN_PSCI_FEATURES)
			return PSCI_RET_SUCCESS;
		if ((arg0 == ARM_PSCI_0_2_FN64_CPU_SUSPEND ||
		     arg0 == ARM_PSCI_0_2_FN_CPU_OFF ||
		     arg0 == ARM_PSCI_0_2_FN64_CPU_ON ||
		     arg0 == ARM_PSCI_0_2_FN64_AFFINITY_INFO ||
		     arg0 == ARM_PSCI_0_2_FN_SYSTEM_OFF ||
		     arg0 == ARM_PSCI_0_2_FN_SYSTEM_RESET) &&
		    efi_psci_platform_supports(arg0))
			return PSCI_RET_SUCCESS;
		return PSCI_RET_NOT_SUPPORTED;
	}
	if (!efi_psci_platform_supports(fid))
		return PSCI_RET_NOT_SUPPORTED;
	if (fid == ARM_PSCI_0_2_FN64_CPU_ON)
		return efi_psci_cpu_on(arg0, arg1, arg2);
	if (fid == ARM_PSCI_0_2_FN64_CPU_SUSPEND ||
	    fid == ARM_PSCI_0_2_FN_CPU_OFF ||
	    fid == ARM_PSCI_0_2_FN64_AFFINITY_INFO ||
	    fid == ARM_PSCI_0_2_FN_SYSTEM_OFF ||
	    fid == ARM_PSCI_0_2_FN_SYSTEM_RESET)
		return efi_psci_platform_call(fid, arg0, arg1, arg2);

	return PSCI_RET_NOT_SUPPORTED;
}

static const struct efi_arm_psci_handler_table psci_table_template = {
	.version = ARM_PSCI_VER_1_0,
	.features = {
		[0 ... EFI_PSCI_MAX_FN - 1] = PSCI_RET_NOT_SUPPORTED,
		[0] = PSCI_RET_SUCCESS,  /* PSCI_VERSION */
		[1] = PSCI_RET_SUCCESS,  /* CPU_SUSPEND */
		[2] = PSCI_RET_SUCCESS,  /* CPU_OFF */
		[3] = PSCI_RET_SUCCESS,  /* CPU_ON */
		[4] = PSCI_RET_SUCCESS,  /* AFFINITY_INFO */
		[8] = PSCI_RET_SUCCESS,  /* SYSTEM_OFF */
		[9] = PSCI_RET_SUCCESS,  /* SYSTEM_RESET */
		[10] = PSCI_RET_SUCCESS, /* PSCI_FEATURES */
	},
};

static void EFIAPI efi_psci_virtual_address_map(struct efi_event *event,
						void *context)
{
	efi_status_t ret;
	int i;

	ret = efi_convert_pointer(0, (void **)&efi_psci_state_phys->table.handler);
	if (ret != EFI_SUCCESS)
		goto err;

	for (i = 0; i < efi_psci_state_phys->cpu_count; i++) {
		if (!efi_psci_state_phys->cpus[i].release_slot)
			continue;
		ret = efi_convert_pointer(0,
					  &efi_psci_state_phys->cpus[i].release_slot);
		if (ret != EFI_SUCCESS)
			goto err;
	}

	for (i = 0; i < systab.nr_tables; i++) {
		if (!guidcmp(&systab.tables[i].guid, &psci_guid)) {
			void **ptr = (void **)&systab.tables[i].table;

			ret = efi_convert_pointer(0, ptr);
			if (ret != EFI_SUCCESS)
				goto err;
			break;
		}
	}
	ret = efi_convert_pointer(0, (void **)&efi_psci_cpus);
	if (ret != EFI_SUCCESS)
		goto err;
	ret = efi_convert_pointer(0, (void **)&efi_psci_state);
	if (ret == EFI_SUCCESS)
		return;
err:
	log_err("EFI PSCI runtime pointer conversion failed: %lx\n",
		(ulong)ret);
}

efi_status_t efi_psci_register(void)
{
	ofnode cpus = ofnode_path("/cpus");
	ofnode node;
	u64 stack_base, memory;
	struct efi_psci_state *state;
	struct efi_event *event;
	u32 addr_cells, count = 0;
	u32 i;
	efi_status_t ret;

	ret = efi_psci_platform_init();
	if (ret != EFI_SUCCESS)
		return ret;

	if (!ofnode_valid(cpus) ||
	    ofnode_read_u32(cpus, "#address-cells", &addr_cells) ||
	    (addr_cells != 1 && addr_cells != 2))
		return EFI_NOT_FOUND;

	ofnode_for_each_subnode(node, cpus) {
		if (!ofnode_name_eq(node, "cpu") || !ofnode_is_enabled(node))
			continue;
		if (count == EFI_PSCI_MAX_CPUS) {
			log_warning("EFI PSCI: more than %u CPUs; conduit disabled\n",
				    EFI_PSCI_MAX_CPUS);
			return EFI_UNSUPPORTED;
		}
		count++;
	}
	if (!count)
		return EFI_NOT_FOUND;

	memory = (uintptr_t)efi_alloc_aligned_pages(EFI_PSCI_RUNTIME_SIZE,
						    EFI_RUNTIME_SERVICES_DATA,
						    SZ_64K);
	if (!memory)
		return EFI_OUT_OF_RESOURCES;
	memset((void *)(uintptr_t)memory, 0, EFI_PSCI_RUNTIME_SIZE);
	state = (void *)(uintptr_t)memory;
	state->table = psci_table_template;
	state->table.handler = efi_psci_handler;
	state->cpu_count = count;
	stack_base = memory + SZ_64K;
	i = 0;
	ofnode_for_each_subnode(node, cpus) {
		u64 mpidr;
		u32 reg;

		if (!ofnode_name_eq(node, "cpu") || !ofnode_is_enabled(node))
			continue;
		if (addr_cells == 1) {
			if (ofnode_read_u32(node, "reg", &reg)) {
				ret = EFI_DEVICE_ERROR;
				goto err_free;
			}
			mpidr = reg;
		} else if (ofnode_read_u64(node, "reg", &mpidr)) {
			ret = EFI_DEVICE_ERROR;
			goto err_free;
		}
		state->cpus[i].mpidr = mpidr;
		state->cpus[i].stack_top = stack_base +
					   (i + 1) * EFI_PSCI_STACK_SIZE;
		ret = efi_psci_platform_cpu_init(node, &state->cpus[i]);
		if (ret != EFI_SUCCESS)
			goto err_free;
		i++;
	}
	ret = efi_psci_platform_prepare(state->cpus, count);
	if (ret != EFI_SUCCESS)
		goto err_free;
	if (!efi_psci_platform_supports(ARM_PSCI_0_2_FN64_CPU_SUSPEND))
		state->table.features[1] = PSCI_RET_NOT_SUPPORTED;
	if (!efi_psci_platform_supports(ARM_PSCI_0_2_FN_CPU_OFF))
		state->table.features[2] = PSCI_RET_NOT_SUPPORTED;
	if (!efi_psci_platform_supports(ARM_PSCI_0_2_FN64_CPU_ON))
		state->table.features[3] = PSCI_RET_NOT_SUPPORTED;
	if (!efi_psci_platform_supports(ARM_PSCI_0_2_FN64_AFFINITY_INFO))
		state->table.features[4] = PSCI_RET_NOT_SUPPORTED;
	if (!efi_psci_platform_supports(ARM_PSCI_0_2_FN_SYSTEM_OFF))
		state->table.features[8] = PSCI_RET_NOT_SUPPORTED;
	if (!efi_psci_platform_supports(ARM_PSCI_0_2_FN_SYSTEM_RESET))
		state->table.features[9] = PSCI_RET_NOT_SUPPORTED;

	efi_psci_state = state;
	efi_psci_state_phys = state;
	efi_psci_cpus = state->cpus;
	efi_psci_cpu_count = count;
	efi_psci_cpus_phys = state->cpus;
	efi_psci_trampoline_phys = (uintptr_t)&efi_psci_trampoline;
	ret = efi_install_configuration_table(&psci_guid, &state->table);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to install EFI PSCI table\n");
		goto err_free;
	}

	ret = efi_create_event(EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE, TPL_CALLBACK,
			       efi_psci_virtual_address_map, NULL, NULL, &event);
	if (ret == EFI_SUCCESS)
		return EFI_SUCCESS;

	log_err("Failed to register EFI PSCI address change event\n");
	efi_install_configuration_table(&psci_guid, NULL);
err_free:
	efi_psci_cpus_phys = NULL;
	efi_psci_cpus = NULL;
	efi_psci_cpu_count = 0;
	efi_psci_state = NULL;
	efi_psci_state_phys = NULL;
	efi_free_pages(memory, EFI_PSCI_RUNTIME_SIZE / EFI_PAGE_SIZE);
	return ret;
}
