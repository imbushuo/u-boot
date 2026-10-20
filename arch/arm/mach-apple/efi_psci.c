// SPDX-License-Identifier: GPL-2.0+
/*
 * Apple silicon EFI PSCI backend: m1n1 spin tables, watchdog and SMC.
 */

#include <dm.h>
#include <dm/ofnode.h>
#include <dm/uclass.h>
#include <efi_psci.h>
#include <log.h>
#include <mailbox.h>
#include <malloc.h>
#include <time.h>
#include <asm/io.h>
#include <asm/arch/rtkit.h>
#include <asm/psci.h>
#include <asm/system.h>
#include <linux/apple-mailbox.h>
#include <linux/delay.h>
#include <linux/errno.h>
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

static void __efi_runtime_data *apple_wdt_base;
static void __efi_runtime_data *apple_smc_mbox;
static void __efi_runtime_data *apple_smc_shmem;

static int apple_psci_rtkit_shmem(void *cookie, struct apple_rtkit_buffer *buf)
{
	if (buf->is_mapped)
		return 0;

	buf->buffer = efi_alloc_aligned_pages(ALIGN(buf->size, SZ_16K),
					      EFI_RESERVED_MEMORY_TYPE,
					      SZ_16K);
	if (!buf->buffer)
		return -ENOMEM;
	buf->dva = (uintptr_t)buf->buffer;
	return 0;
}

static efi_status_t apple_psci_smc_init(void)
{
	struct ofnode_phandle_args args;
	ofnode smc = ofnode_by_compatible(ofnode_null(), "apple,smc");
	struct apple_rtkit *rtk;
	struct apple_mbox_msg msg = { .msg0 = 0x17, .msg1 = APPLE_SMC_ENDPOINT };
	struct mbox_chan chan = {};
	struct udevice *mbox;
	fdt_addr_t smc_addr, mbox_addr, sram_addr;
	fdt_size_t smc_size, mbox_size, sram_size;
	ulong start;
	efi_status_t ret;
	int err;

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

	err = uclass_get_device_by_ofnode(UCLASS_MAILBOX, args.node, &mbox);
	if (err)
		return EFI_UNSUPPORTED;
	chan.dev = mbox;
	chan.id = 0;

	writel(readl((void *)(uintptr_t)smc_addr + 0x44) | BIT(4),
	       (void *)(uintptr_t)smc_addr + 0x44);
	rtk = apple_rtkit_init(&chan, NULL, apple_psci_rtkit_shmem, NULL);
	if (!rtk)
		return EFI_OUT_OF_RESOURCES;
	err = apple_rtkit_boot(rtk);
	if (!err)
		err = apple_rtkit_set_ap_power(rtk, APPLE_RTKIT_PWR_STATE_ON);
	if (!err)
		err = mbox_send(&chan, &msg);
	if (err)
		goto out;

	start = get_timer(0);
	do {
		err = mbox_recv(&chan, &msg, 0);
		if (err == -ENODATA) {
			udelay(1);
			continue;
		}
		if (err)
			break;
		if (msg.msg1 == APPLE_SMC_ENDPOINT)
			break;
	} while (get_timer(start) < 1000);
	if (err || msg.msg1 != APPLE_SMC_ENDPOINT ||
	    msg.msg0 < sram_addr ||
	    msg.msg0 - sram_addr > sram_size - sizeof(u32)) {
		err = -ENODEV;
		goto out;
	}

	ret = efi_add_memory_map(ALIGN_DOWN(mbox_addr, SZ_64K),
				 ALIGN(mbox_addr + mbox_size, SZ_64K) -
				 ALIGN_DOWN(mbox_addr, SZ_64K), EFI_MMAP_IO);
	if (ret != EFI_SUCCESS)
		goto out;
	ret = efi_add_memory_map(ALIGN_DOWN(sram_addr, SZ_64K),
				 ALIGN(sram_addr + sram_size, SZ_64K) -
				 ALIGN_DOWN(sram_addr, SZ_64K), EFI_MMAP_IO);
	if (ret == EFI_SUCCESS) {
		apple_smc_mbox = (void *)(uintptr_t)mbox_addr;
		apple_smc_shmem = (void *)(uintptr_t)msg.msg0;
	}
out:
	free(rtk);
	if (err) {
		log_warning("EFI PSCI: Apple SMC initialization failed: %d\n", err);
		return EFI_UNSUPPORTED;
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
	ret = efi_convert_pointer(0, &apple_smc_mbox);
	if (ret != EFI_SUCCESS)
		log_err("Apple SMC mailbox pointer conversion failed: %lx\n",
			(ulong)ret);
	ret = efi_convert_pointer(0, &apple_smc_shmem);
	if (ret != EFI_SUCCESS)
		log_err("Apple SMC shared memory pointer conversion failed: %lx\n",
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
	ret = apple_psci_smc_init();
	if (ret != EFI_SUCCESS)
		log_warning("EFI PSCI: Apple SMC shutdown unavailable: %lx\n",
			    (ulong)ret);
	ret = efi_create_event(EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE, TPL_CALLBACK,
			       apple_psci_virtual_address_map, NULL, NULL, &event);
	if (ret != EFI_SUCCESS) {
		apple_wdt_base = NULL;
		apple_smc_mbox = NULL;
		apple_smc_shmem = NULL;
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

static unsigned long __efi_runtime apple_psci_smc_command(u32 command, u32 id)
{
	void *mbox = apple_smc_mbox;
	u64 deadline, response, endpoint;

	if (!mbox || !apple_smc_shmem)
		return PSCI_RET_NOT_SUPPORTED;

	writel(command, apple_smc_shmem);
	asm volatile("dsb sy" : : : "memory");
	writeq(((u64)APPLE_SMC_MBSE << 32) | (4 << 16) | (id << 12) | 0x11,
	       mbox + APPLE_MBOX_A2I_MSG0);
	writeq(APPLE_SMC_ENDPOINT, mbox + APPLE_MBOX_A2I_MSG1);

	deadline = apple_psci_deadline(500);
	while (readl(mbox + APPLE_MBOX_A2I_STAT) & APPLE_MBOX_A2I_FULL) {
		if (apple_psci_timed_out(deadline))
			return PSCI_RET_INTERNAL_FAILURE;
	}
	while (!apple_psci_timed_out(deadline)) {
		if (readl(mbox + APPLE_MBOX_I2A_STAT) & APPLE_MBOX_I2A_EMPTY)
			continue;
		response = readq(mbox + APPLE_MBOX_I2A_MSG0);
		endpoint = readq(mbox + APPLE_MBOX_I2A_MSG1);
		if (endpoint != APPLE_SMC_ENDPOINT ||
		    (response & 0xf000) != (id << 12))
			continue;
		return (response & 0xff) ? PSCI_RET_INTERNAL_FAILURE :
					  PSCI_RET_SUCCESS;
	}
	return PSCI_RET_INTERNAL_FAILURE;
}

static unsigned long __efi_runtime apple_psci_poweroff(void)
{
	u64 deadline;

	if (apple_psci_smc_command(APPLE_SMC_OFFW, 0))
		return PSCI_RET_INTERNAL_FAILURE;
	if (apple_psci_smc_command(APPLE_SMC_OFF1, 1))
		return PSCI_RET_INTERNAL_FAILURE;

	deadline = apple_psci_deadline(200);
	while (!apple_psci_timed_out(deadline))
		asm volatile("yield");
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
