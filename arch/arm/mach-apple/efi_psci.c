// SPDX-License-Identifier: GPL-2.0+
/*
 * Apple silicon EFI PSCI backend: m1n1 spin tables, watchdog and SMC.
 */

#include <dm.h>
#include <dm/ofnode.h>
#include <efi_psci.h>
#include <log.h>
#include <mailbox.h>
#include <asm/io.h>
#include <asm/arch/rtkit.h>
#include <asm/psci.h>
#include <asm/system.h>
#include <linux/bitfield.h>
#include <linux/psci.h>
#include <linux/sizes.h>

#define APPLE_WDT_CUR_TIME	0x10
#define APPLE_WDT_BITE_TIME	0x14
#define APPLE_WDT_CTRL		0x1c
#define APPLE_WDT_CTRL_RESET_EN	BIT(2)
#define APPLE_MPIDR_AFFINITY	0xff00ffffffULL

#define APPLE_MBOX_A2I_STAT	0x110
#define APPLE_MBOX_A2I_FULL	BIT(16)
#define APPLE_MBOX_I2A_STAT	0x114
#define APPLE_MBOX_I2A_EMPTY	BIT(17)
#define APPLE_MBOX_A2I_MSG0	0x800
#define APPLE_MBOX_A2I_MSG1	0x808
#define APPLE_MBOX_I2A_MSG0	0x830
#define APPLE_MBOX_I2A_MSG1	0x838
#define APPLE_SMC_ENDPOINT	0x20
#define APPLE_SMC_MBSE		0x4d425345
#define APPLE_SMC_OFFW		0x6f666677
#define APPLE_SMC_OFF1		0x6f666631

#define APPLE_RTKIT_MGMT_TYPE	GENMASK_ULL(59, 52)
#define APPLE_RTKIT_HELLO		1
#define APPLE_RTKIT_HELLO_REPLY	2
#define APPLE_RTKIT_START_EP	5
#define APPLE_RTKIT_IOP_POWER	6
#define APPLE_RTKIT_IOP_POWER_ACK	7
#define APPLE_RTKIT_EPMAP		8
#define APPLE_RTKIT_AP_POWER	11
#define APPLE_RTKIT_EPMAP_LAST	BIT_ULL(51)
#define APPLE_RTKIT_EPMAP_BASE	GENMASK_ULL(34, 32)
#define APPLE_RTKIT_POWER_INIT	0x220

static void __efi_runtime_data *apple_wdt_base;
static void __efi_runtime_data *apple_smc_asc;
static void __efi_runtime_data *apple_smc_mbox;
static void __efi_runtime_data *apple_smc_sram;
static u64 __efi_runtime_data apple_smc_sram_phys;
static u64 __efi_runtime_data apple_smc_sram_size;

static efi_status_t apple_psci_smc_setup(void)
{
	struct ofnode_phandle_args args;
	ofnode smc = ofnode_by_compatible(ofnode_null(), "apple,smc");
	fdt_addr_t smc_addr, mbox_addr, sram_addr;
	fdt_size_t smc_size, mbox_size, sram_size;
	efi_status_t ret;

	if (!ofnode_valid(smc) || !ofnode_is_enabled(smc) ||
	    ofnode_parse_phandle_with_args(smc, "mboxes", "#mbox-cells",
					   0, 0, &args))
		return EFI_UNSUPPORTED;
	smc_addr = ofnode_get_addr_size_index(smc, 0, &smc_size);
	sram_addr = ofnode_get_addr_size_index(smc, 1, &sram_size);
	mbox_addr = ofnode_get_addr_size(args.node, "reg", &mbox_size);
	if (smc_addr == FDT_ADDR_T_NONE || smc_size < 0x48 ||
	    sram_addr == FDT_ADDR_T_NONE || sram_size < SZ_4K ||
	    mbox_addr == FDT_ADDR_T_NONE ||
	    mbox_size < APPLE_MBOX_I2A_MSG1 + sizeof(u64))
		return EFI_UNSUPPORTED;

	ret = efi_add_memory_map(ALIGN_DOWN(smc_addr, SZ_64K),
				 ALIGN(smc_addr + smc_size, SZ_64K) -
				 ALIGN_DOWN(smc_addr, SZ_64K), EFI_MMAP_IO);
	if (ret != EFI_SUCCESS)
		return ret;
	ret = efi_add_memory_map(ALIGN_DOWN(mbox_addr, SZ_64K),
				 ALIGN(mbox_addr + mbox_size, SZ_64K) -
				 ALIGN_DOWN(mbox_addr, SZ_64K), EFI_MMAP_IO);
	if (ret != EFI_SUCCESS)
		return ret;
	ret = efi_add_memory_map(ALIGN_DOWN(sram_addr, SZ_64K),
				 ALIGN(sram_addr + sram_size, SZ_64K) -
				 ALIGN_DOWN(sram_addr, SZ_64K), EFI_MMAP_IO);
	if (ret == EFI_SUCCESS) {
		apple_smc_asc = (void *)(uintptr_t)smc_addr;
		apple_smc_mbox = (void *)(uintptr_t)mbox_addr;
		apple_smc_sram = (void *)(uintptr_t)sram_addr;
		apple_smc_sram_phys = sram_addr;
		apple_smc_sram_size = sram_size;
	}
	return ret;
}

static void EFIAPI apple_psci_virtual_address_map(struct efi_event *event,
						  void *context)
{
	efi_status_t ret;

	ret = efi_convert_pointer(0, &apple_wdt_base);
	if (ret != EFI_SUCCESS)
		log_err("Apple watchdog runtime pointer conversion failed: %lx\n",
			(ulong)ret);
	if (!apple_smc_mbox)
		return;
	ret = efi_convert_pointer(0, &apple_smc_asc);
	if (ret != EFI_SUCCESS)
		log_err("Apple SMC ASC pointer conversion failed: %lx\n",
			(ulong)ret);
	ret = efi_convert_pointer(0, &apple_smc_mbox);
	if (ret != EFI_SUCCESS)
		log_err("Apple SMC mailbox pointer conversion failed: %lx\n",
			(ulong)ret);
	ret = efi_convert_pointer(0, &apple_smc_sram);
	if (ret != EFI_SUCCESS)
		log_err("Apple SMC SRAM pointer conversion failed: %lx\n",
			(ulong)ret);
}

efi_status_t efi_psci_platform_init(void)
{
	ofnode chosen = ofnode_path("/chosen");
	ofnode wdt = ofnode_by_compatible(ofnode_null(), "apple,wdt");
	struct efi_event *event;
	fdt_addr_t addr;
	fdt_size_t size;
	efi_status_t ret;

	if (!ofnode_read_string(chosen, "asahi,m1n1-stage2-version")) {
		log_warning("EFI PSCI: m1n1 spin-table handoff unavailable\n");
		return EFI_UNSUPPORTED;
	}
	if (!ofnode_valid(wdt) || !ofnode_is_enabled(wdt)) {
		log_warning("EFI PSCI: Apple watchdog unavailable\n");
		return EFI_UNSUPPORTED;
	}

	addr = ofnode_get_addr_size(wdt, "reg", &size);
	if (addr == FDT_ADDR_T_NONE || size < APPLE_WDT_CTRL + sizeof(u32))
		return EFI_UNSUPPORTED;
	ret = efi_add_memory_map(ALIGN_DOWN(addr, SZ_64K),
				 ALIGN(addr + size, SZ_64K) -
				 ALIGN_DOWN(addr, SZ_64K), EFI_MMAP_IO);
	if (ret != EFI_SUCCESS)
		return ret;

	apple_wdt_base = (void *)(uintptr_t)addr;
	ret = apple_psci_smc_setup();
	if (ret != EFI_SUCCESS)
		log_warning("EFI PSCI: Apple SMC shutdown unavailable: %lx\n",
			    (ulong)ret);
	ret = efi_create_event(EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE, TPL_CALLBACK,
			       apple_psci_virtual_address_map, NULL, NULL, &event);
	if (ret != EFI_SUCCESS) {
		apple_wdt_base = NULL;
		apple_smc_asc = NULL;
		apple_smc_mbox = NULL;
		apple_smc_sram = NULL;
	}
	return ret;
}

efi_status_t efi_psci_platform_cpu_init(ofnode node, struct efi_psci_cpu *cpu)
{
	const char *method = ofnode_read_string(node, "enable-method");
	u64 release_addr;

	if (!method || strcmp(method, "spin-table")) {
		log_warning("EFI PSCI: CPU %llx has no spin-table handoff\n",
			    cpu->mpidr);
		return EFI_UNSUPPORTED;
	}
	if (cpu->mpidr == (read_mpidr() & APPLE_MPIDR_AFFINITY))
		return EFI_SUCCESS;

	if (ofnode_read_u64(node, "cpu-release-addr", &release_addr) ||
	    !release_addr || (release_addr & 7)) {
		log_warning("EFI PSCI: missing CPU release slot for %llx\n",
			    cpu->mpidr);
		return EFI_UNSUPPORTED;
	}
	cpu->release_slot = (void *)(uintptr_t)release_addr;
	return EFI_SUCCESS;
}

efi_status_t efi_psci_platform_prepare(struct efi_psci_cpu *cpus, u32 count)
{
	u64 base;
	efi_status_t ret;
	u32 i, j;
	bool have_secondary = false;

	for (i = 0; i < count; i++) {
		if (!cpus[i].release_slot)
			continue;
		have_secondary = true;
		base = ALIGN_DOWN((uintptr_t)cpus[i].release_slot, SZ_64K);
		for (j = 0; j < i; j++) {
			if (cpus[j].release_slot &&
			    ALIGN_DOWN((uintptr_t)cpus[j].release_slot, SZ_64K) ==
			    base)
				break;
		}
		if (j != i)
			continue;

		mmu_map_region(base, SZ_64K, false);
		ret = efi_add_memory_map(base, SZ_64K, EFI_RUNTIME_SERVICES_DATA);
		if (ret != EFI_SUCCESS)
			return ret;
	}

	return have_secondary ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

bool __efi_runtime efi_psci_platform_supports(unsigned long fid)
{
	return fid == ARM_PSCI_0_2_FN64_CPU_ON ||
	       fid == ARM_PSCI_0_2_FN64_AFFINITY_INFO ||
	       (fid == ARM_PSCI_0_2_FN_SYSTEM_OFF && apple_smc_mbox) ||
	       fid == ARM_PSCI_0_2_FN_SYSTEM_RESET;
}

static u64 __efi_runtime apple_psci_deadline(u32 millis)
{
	u64 now, freq;

	asm volatile("mrs %0, cntvct_el0" : "=r"(now));
	asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
	return now + (freq / 1000) * millis;
}

static bool __efi_runtime apple_psci_timed_out(u64 deadline)
{
	u64 now;

	asm volatile("mrs %0, cntvct_el0" : "=r"(now));
	return now >= deadline;
}

static bool __efi_runtime apple_psci_mbox_send(u64 message, u8 endpoint)
{
	void *mbox = apple_smc_mbox;
	u64 deadline = apple_psci_deadline(500);

	while (readl(mbox + APPLE_MBOX_A2I_STAT) & APPLE_MBOX_A2I_FULL) {
		if (apple_psci_timed_out(deadline))
			return false;
	}
	writeq(message, mbox + APPLE_MBOX_A2I_MSG0);
	writeq(endpoint, mbox + APPLE_MBOX_A2I_MSG1);
	return true;
}

static bool __efi_runtime apple_psci_smc_buffer_valid(u64 addr, u64 size)
{
	return size && size <= apple_smc_sram_size &&
	       addr >= apple_smc_sram_phys &&
	       addr - apple_smc_sram_phys <= apple_smc_sram_size - size;
}

/* Runtime shutdown cannot use driver model or allocate RTKit buffers. */
static bool __efi_runtime
apple_psci_rtkit_recv(u64 *message, u8 *endpoint, u64 deadline)
{
	void *mbox = apple_smc_mbox;
	u64 type, addr, size;

	while (!apple_psci_timed_out(deadline)) {
		if (readl(mbox + APPLE_MBOX_I2A_STAT) & APPLE_MBOX_I2A_EMPTY)
			continue;
		*message = readq(mbox + APPLE_MBOX_I2A_MSG0);
		*endpoint = readq(mbox + APPLE_MBOX_I2A_MSG1);
		if (!*endpoint || *endpoint == APPLE_SMC_ENDPOINT)
			return true;

		type = FIELD_GET(APPLE_RTKIT_MGMT_TYPE, *message);
		if (*endpoint == 4 && (type == 8 || type == 0xc)) {
			if (!apple_psci_mbox_send(*message, *endpoint))
				return false;
		} else if ((*endpoint == 1 || *endpoint == 4) && type == 1) {
			size = FIELD_GET(GENMASK_ULL(51, 44), *message) << 12;
			addr = FIELD_GET(GENMASK_ULL(43, 0), *message);
			if (!apple_psci_smc_buffer_valid(addr, size))
				return false;
		} else if (*endpoint == 8 && (*message >> 56) == 1) {
			size = FIELD_GET(GENMASK_ULL(55, 36), *message);
			addr = FIELD_GET(GENMASK_ULL(35, 0), *message) << 12;
			if (!apple_psci_smc_buffer_valid(addr, size))
				return false;
		}
	}
	return false;
}

static bool __efi_runtime
apple_psci_rtkit_wait(u8 endpoint, u64 mask, u64 expected, u64 *message)
{
	u64 deadline = apple_psci_deadline(1000);
	u8 ep;

	while (apple_psci_rtkit_recv(message, &ep, deadline)) {
		if (ep == endpoint && (*message & mask) == expected)
			return true;
	}
	return false;
}

static bool __efi_runtime
apple_psci_rtkit_set_power(u8 request, u16 state)
{
	u8 response = request == APPLE_RTKIT_IOP_POWER ? APPLE_RTKIT_IOP_POWER_ACK : request;
	u64 reply;
	u64 message = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, request) | state;
	u64 expected = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, response) | state;

	return apple_psci_mbox_send(message, 0) &&
	       apple_psci_rtkit_wait(0, APPLE_RTKIT_MGMT_TYPE | 0xffff, expected, &reply);
}

static bool __efi_runtime apple_psci_smc_start(void **shmem)
{
	u64 message, reply, deadline;
	u32 min_ver, max_ver, version, endpoints = 0;
	u8 ep;
	int i;

	writel(readl(apple_smc_asc + 0x44) | BIT(4), apple_smc_asc + 0x44);
	message = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, APPLE_RTKIT_IOP_POWER) |
		  APPLE_RTKIT_POWER_INIT;
	if (!apple_psci_mbox_send(message, 0) ||
	    !apple_psci_rtkit_wait(0, APPLE_RTKIT_MGMT_TYPE,
				  FIELD_PREP(APPLE_RTKIT_MGMT_TYPE,
					     APPLE_RTKIT_HELLO), &reply))
		return false;

	min_ver = reply & 0xffff;
	max_ver = (reply >> 16) & 0xffff;
	version = min(max_ver, 12U);
	if (min_ver > version || version < 11)
		return false;
	message = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, APPLE_RTKIT_HELLO_REPLY) |
		  (version << 16) | version;
	if (!apple_psci_mbox_send(message, 0))
		return false;

	deadline = apple_psci_deadline(1000);
	do {
		if (!apple_psci_rtkit_recv(&reply, &ep, deadline) || ep ||
		    FIELD_GET(APPLE_RTKIT_MGMT_TYPE, reply) != APPLE_RTKIT_EPMAP)
			return false;
		if (!FIELD_GET(APPLE_RTKIT_EPMAP_BASE, reply))
			endpoints = reply;
		message = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, APPLE_RTKIT_EPMAP) |
			  (reply & APPLE_RTKIT_EPMAP_BASE);
		message |= reply & APPLE_RTKIT_EPMAP_LAST ?
			   APPLE_RTKIT_EPMAP_LAST : BIT(0);
		if (!apple_psci_mbox_send(message, 0))
			return false;
	} while (!(reply & APPLE_RTKIT_EPMAP_LAST));

	/* Leave noisy logging endpoints and unrelated applications stopped. */
	endpoints &= BIT(1) | BIT(4) | BIT(8);
	for (i = 0; i < 32; i++) {
		if (!(endpoints & BIT(i)))
			continue;
		message = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, APPLE_RTKIT_START_EP) |
			  ((u64)i << 32) | BIT(1);
		if (!apple_psci_mbox_send(message, 0))
			return false;
	}
	message = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, APPLE_RTKIT_IOP_POWER_ACK) |
		  APPLE_RTKIT_PWR_STATE_ON;
	if (!apple_psci_rtkit_wait(0, APPLE_RTKIT_MGMT_TYPE | 0xffff, message, &reply) ||
	    !apple_psci_rtkit_set_power(APPLE_RTKIT_AP_POWER,
				       APPLE_RTKIT_PWR_STATE_ON))
		return false;

	message = FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, APPLE_RTKIT_START_EP) |
		  ((u64)APPLE_SMC_ENDPOINT << 32) | BIT(1);
	if (!apple_psci_mbox_send(message, 0) ||
	    !apple_psci_mbox_send(0x17, APPLE_SMC_ENDPOINT) ||
	    !apple_psci_rtkit_wait(APPLE_SMC_ENDPOINT, 0, 0, &reply) ||
	    !apple_psci_smc_buffer_valid(reply, SZ_4K))
		return false;
	*shmem = apple_smc_sram + (reply - apple_smc_sram_phys);
	return true;
}

static unsigned long __efi_runtime
apple_psci_smc_command(void *shmem, u32 command, u32 id)
{
	u64 deadline, response;
	u8 endpoint;

	writel(command, shmem);
	asm volatile("dsb sy" : : : "memory");
	if (!apple_psci_mbox_send(((u64)APPLE_SMC_MBSE << 32) | (4 << 16) |
				  (id << 12) | 0x11, APPLE_SMC_ENDPOINT))
		return PSCI_RET_INTERNAL_FAILURE;

	deadline = apple_psci_deadline(500);
	while (apple_psci_rtkit_recv(&response, &endpoint, deadline)) {
		if (endpoint != APPLE_SMC_ENDPOINT ||
		    (response & 0xff) == 0x18 ||
		    (response & 0xf000) != (id << 12))
			continue;
		return (response & 0xff) ? PSCI_RET_INTERNAL_FAILURE :
					  PSCI_RET_SUCCESS;
	}
	return PSCI_RET_INTERNAL_FAILURE;
}

static unsigned long __efi_runtime apple_psci_poweroff(void)
{
	void *shmem;
	u64 deadline;

	if (!apple_smc_mbox)
		return PSCI_RET_NOT_SUPPORTED;
	/* Do not take ownership of the SMC until shutdown is requested. */
	if (!apple_psci_smc_start(&shmem))
		goto out;
	if (apple_psci_smc_command(shmem, APPLE_SMC_OFFW, 0))
		goto out;
	if (apple_psci_smc_command(shmem, APPLE_SMC_OFF1, 1))
		goto out;

	deadline = apple_psci_deadline(200);
	while (!apple_psci_timed_out(deadline))
		asm volatile("yield");
out:
	/* A failed shutdown may return to a loader which boots another OS. */
	if (!apple_psci_rtkit_set_power(APPLE_RTKIT_AP_POWER,
					APPLE_RTKIT_PWR_STATE_QUIESCED) ||
	    !apple_psci_rtkit_set_power(APPLE_RTKIT_IOP_POWER,
					APPLE_RTKIT_PWR_STATE_QUIESCED))
		return PSCI_RET_INTERNAL_FAILURE;
	return PSCI_RET_INTERNAL_FAILURE;
}

static unsigned long __efi_runtime apple_psci_affinity_info(u64 target,
							    unsigned long level)
{
	bool found = false, pending = false;
	u64 mask;
	u32 i;

	if (level > 3)
		return PSCI_RET_INVALID_PARAMS;
	mask = APPLE_MPIDR_AFFINITY & (APPLE_MPIDR_AFFINITY << (8 * level));

	for (i = 0; i < efi_psci_cpu_count; i++) {
		struct efi_psci_cpu *cpu = &efi_psci_cpus[i];

		if ((cpu->mpidr & mask) != (target & mask))
			continue;
		found = true;
		if (!cpu->release_slot || READ_ONCE(*(u64 *)cpu->release_slot))
			return PSCI_AFFINITY_LEVEL_ON;
		if (READ_ONCE(cpu->busy))
			pending = true;
	}
	if (!found)
		return PSCI_RET_INVALID_PARAMS;
	return pending ? PSCI_AFFINITY_LEVEL_ON_PENDING :
			 PSCI_AFFINITY_LEVEL_OFF;
}

static unsigned long __efi_runtime apple_psci_reset(void)
{
	void *base = apple_wdt_base;
	u64 deadline;

	if (!base)
		return PSCI_RET_NOT_SUPPORTED;

	writel(APPLE_WDT_CTRL_RESET_EN, base + APPLE_WDT_CTRL);
	writel(0, base + APPLE_WDT_BITE_TIME);
	writel(0, base + APPLE_WDT_CUR_TIME);
	readl(base + APPLE_WDT_CUR_TIME);
	deadline = apple_psci_deadline(200);
	while (!apple_psci_timed_out(deadline))
		asm volatile("yield");

	return PSCI_RET_INTERNAL_FAILURE;
}

unsigned long __efi_runtime
efi_psci_platform_call(unsigned long fid, unsigned long arg0,
		       unsigned long arg1, unsigned long arg2)
{
	struct efi_psci_cpu *cpu;
	u64 *slot;

	switch (fid) {
	case ARM_PSCI_0_2_FN64_CPU_ON:
		if (arg2 >= efi_psci_cpu_count)
			return PSCI_RET_INVALID_PARAMS;
		cpu = &efi_psci_cpus[arg2];
		if (cpu->mpidr != arg0)
			return PSCI_RET_INVALID_PARAMS;
		slot = cpu->release_slot;
		if (!slot || READ_ONCE(*slot))
			return PSCI_RET_ALREADY_ON;
		WRITE_ONCE(*slot, arg1);
		asm volatile("dsb sy\nsev" : : : "memory");
		return PSCI_RET_SUCCESS;
	case ARM_PSCI_0_2_FN64_AFFINITY_INFO:
		return apple_psci_affinity_info(arg0, arg1);
	case ARM_PSCI_0_2_FN_SYSTEM_OFF:
		return apple_psci_poweroff();
	case ARM_PSCI_0_2_FN_SYSTEM_RESET:
		return apple_psci_reset();
	default:
		return PSCI_RET_NOT_SUPPORTED;
	}
}

void __efi_runtime EFIAPI efi_reset_system(enum efi_reset_type reset_type,
					   efi_status_t reset_status,
					   unsigned long data_size,
					   void *reset_data)
{
	if (reset_type == EFI_RESET_COLD || reset_type == EFI_RESET_WARM ||
	    reset_type == EFI_RESET_PLATFORM_SPECIFIC)
		apple_psci_reset();
	else if (reset_type == EFI_RESET_SHUTDOWN)
		apple_psci_poweroff();
}
