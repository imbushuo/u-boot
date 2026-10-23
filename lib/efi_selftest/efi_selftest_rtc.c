// SPDX-License-Identifier: GPL-2.0+
/*
 * efi_selftest_rtc
 *
 * Copyright (c) 2018 Heinrich Schuchardt <xypron.glpk@gmx.de>
 *
 * Test the real time clock runtime services.
 */

#include <efi_selftest.h>

#define EFI_ST_NO_RTC "Could not read real time clock\n"
#define EFI_ST_NO_RTC_SET "Could not set real time clock\n"

/*
 * Execute unit test.
 *
 * Read and display current time.
 * Set a new value and read it back.
 * Set the real time clock back the current time.
 *
 * Return:	EFI_ST_SUCCESS for success
 */
static int execute(void)
{
	efi_status_t ret;
	struct efi_time tm_old;
#ifdef CONFIG_EFI_SET_TIME
	struct efi_time tm, tm_new = {
		.year = 2017,
		.month = 5,
		.day = 19,
		.hour = 13,
		.minute = 47,
		.second = 53,
	};
#endif

	/* Display current time */
	ret = st_runtime->get_time(&tm_old, NULL);
	if (ret != EFI_SUCCESS) {
		efi_st_error(EFI_ST_NO_RTC);
		return EFI_ST_FAILURE;
	}
	efi_st_printf("Time according to real time clock: "
		      "%.4u-%.2u-%.2u %.2u:%.2u:%.2u\n",
		      tm_old.year, tm_old.month, tm_old.day,
		      tm_old.hour, tm_old.minute, tm_old.second);
#ifdef CONFIG_EFI_SET_TIME
	ret = st_runtime->set_time(&tm_new);
	if (ret != EFI_SUCCESS) {
		efi_st_error(EFI_ST_NO_RTC_SET);
		return EFI_ST_FAILURE;
	}
	ret = st_runtime->get_time(&tm, NULL);
	if (ret != EFI_SUCCESS) {
		efi_st_error(EFI_ST_NO_RTC);
		return EFI_ST_FAILURE;
	}
	if (tm.year != tm_new.year ||
	    tm.month != tm_new.month ||
	    tm.day != tm_new.day ||
	    tm.hour !=  tm_new.hour ||
	    tm.minute != tm_new.minute ||
	    tm.second < tm_new.second ||
	    tm.second > tm_new.second + 2) {
		efi_st_error(EFI_ST_NO_RTC_SET);
		return EFI_ST_FAILURE;
	}
	/* Set time back to old value */
	ret = st_runtime->set_time(&tm_old);
	if (ret != EFI_SUCCESS) {
		efi_st_error(EFI_ST_NO_RTC_SET);
		return EFI_ST_FAILURE;
	}
#endif

	return EFI_ST_SUCCESS;
}

EFI_UNIT_TEST(rtc) = {
	.name = "real time clock",
	.phase = EFI_EXECUTE_BEFORE_BOOTTIME_EXIT,
	.execute = execute,
};

#ifdef CONFIG_EFI_HAVE_RUNTIME_GET_TIME
static struct efi_time time_before_exit;

static u64 time_order(const struct efi_time *time)
{
	return (((((u64)time->year * 12 + time->month) * 31 + time->day) *
		 24 + time->hour) * 60 + time->minute) * 60 + time->second;
}

static int setup_runtime(const efi_handle_t handle,
			 const struct efi_system_table *systable)
{
	const efi_guid_t guid = EFI_RT_PROPERTIES_TABLE_GUID;
	const struct efi_rt_properties_table *properties = NULL;
	efi_uintn_t i;

	for (i = 0; i < systable->nr_tables; i++) {
		if (!memcmp(&systable->tables[i].guid, &guid, sizeof(guid)))
			properties = systable->tables[i].table;
	}
	if (!properties ||
	    !(properties->runtime_services_supported & EFI_RT_SUPPORTED_GET_TIME)) {
		efi_st_error("Runtime GetTime is not advertised\n");
		return EFI_ST_FAILURE;
	}
	if (st_runtime->get_time(&time_before_exit, NULL) != EFI_SUCCESS) {
		efi_st_error("GetTime failed before ExitBootServices\n");
		return EFI_ST_FAILURE;
	}
	st_boottime->stall(1000000);
	return EFI_ST_SUCCESS;
}

static int execute_runtime(void)
{
	struct efi_time time;
	struct efi_time_cap capabilities;
	efi_status_t ret;
	u64 before, after;

	if (st_runtime->get_time(NULL, NULL) != EFI_INVALID_PARAMETER) {
		efi_st_error("GetTime accepted a NULL time pointer\n");
		return EFI_ST_FAILURE;
	}
	ret = st_runtime->get_time(&time, &capabilities);
	if (ret != EFI_SUCCESS || !capabilities.resolution) {
		efi_st_error("GetTime failed after ExitBootServices\n");
		return EFI_ST_FAILURE;
	}
	before = time_order(&time_before_exit);
	after = time_order(&time);
	if (time.nanosecond > 999999999 || after <= before) {
		efi_st_error("Runtime GetTime returned an invalid or earlier date\n");
		return EFI_ST_FAILURE;
	}

	return EFI_ST_SUCCESS;
}

EFI_UNIT_TEST(rtc_runtime) = {
	.name = "real time clock runtime",
	.phase = EFI_SETUP_BEFORE_BOOTTIME_EXIT,
	.setup = setup_runtime,
	.execute = execute_runtime,
	.on_request = true,
};
#endif
