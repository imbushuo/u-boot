// SPDX-License-Identifier: GPL-2.0+

#include <efi_loader.h>
#include <event.h>
#include <log.h>
#include <rtc.h>
#include <linux/errno.h>

#define APPLE_EFI_TIME_EPOCH	1792540800ULL /* 2026-10-21 00:00:00 UTC */
#define EFI_TIME_MAX_SECONDS	253402300799ULL /* 9999-12-31 23:59:59 UTC */

static u64 __efi_runtime_data apple_efi_time_start;
static u32 __efi_runtime_data apple_efi_time_frequency;

static u64 __efi_runtime apple_efi_time_counter(void)
{
	u64 ticks;

	asm volatile("isb\nmrs %0, cntvct_el0" : "=r"(ticks) : : "memory");
	return ticks;
}

static int apple_efi_time_init(void)
{
	u64 frequency;

	asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
	if (!frequency || frequency > U32_MAX) {
		log_err("EFI synthetic clock: invalid counter frequency %llu\n",
			frequency);
		return -EINVAL;
	}

	apple_efi_time_start = apple_efi_time_counter();
	apple_efi_time_frequency = frequency;
	return 0;
}

EVENT_SPY_SIMPLE(EVT_LAST_STAGE_INIT, apple_efi_time_init);

efi_status_t __efi_runtime EFIAPI
efi_get_time(struct efi_time *time, struct efi_time_cap *capabilities)
{
	struct rtc_time tm;
	u64 ticks, seconds, fraction;
	u32 frequency = apple_efi_time_frequency;

	if (!time)
		return EFI_INVALID_PARAMETER;
	if (!frequency)
		return EFI_DEVICE_ERROR;

	ticks = apple_efi_time_counter() - apple_efi_time_start;
	seconds = ticks / frequency;
	fraction = ticks % frequency;
	if (seconds > EFI_TIME_MAX_SECONDS - APPLE_EFI_TIME_EPOCH)
		return EFI_DEVICE_ERROR;

	rtc_to_tm(APPLE_EFI_TIME_EPOCH + seconds, &tm);
	time->year = tm.tm_year;
	time->month = tm.tm_mon;
	time->day = tm.tm_mday;
	time->hour = tm.tm_hour;
	time->minute = tm.tm_min;
	time->second = tm.tm_sec;
	time->pad1 = 0;
	time->nanosecond = fraction * 1000000000ULL / frequency;
	time->timezone = 0;
	time->daylight = 0;
	time->pad2 = 0;

	if (capabilities) {
		capabilities->resolution = min(frequency, 1000000000U);
		/* No calibrated wall-clock accuracy is available. */
		capabilities->accuracy = U32_MAX;
		capabilities->sets_to_zero = false;
	}
	return EFI_SUCCESS;
}
