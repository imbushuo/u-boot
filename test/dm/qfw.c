// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Asherah Connor <ashe@kivikakk.ee>
 */

#include <qfw.h>
#include <dm.h>
#include <asm/test.h>
#include <dm/test.h>
#include <test/ut.h>

/*
 * Exercise the device enough to be satisfied the initialisation and DMA
 * interfaces work.
 */

static int dm_test_qfw_cpus(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(uclass_first_device_err(UCLASS_QFW, &dev));
	ut_asserteq(5, qfw_online_cpus(dev));

	return 0;
}
DM_TEST(dm_test_qfw_cpus, UTF_SCAN_PDATA);

static int dm_test_qfw_firmware_list(struct unit_test_state *uts)
{
	struct udevice *dev;
	struct fw_file *file;

	ut_assertok(uclass_first_device_err(UCLASS_QFW, &dev));
	ut_assertok(qfw_read_firmware_list(dev));
	ut_assertok_ptr((file = qfw_find_file(dev, "test-one")));

	return 0;
}
DM_TEST(dm_test_qfw_firmware_list, UTF_SCAN_PDATA);

static int dm_test_qfw_dma_write(struct unit_test_state *uts)
{
	const u8 data[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	struct qfw_dev *qdev;
	struct udevice *dev;
	u8 result[sizeof(data)];

	ut_assertok(uclass_first_device_err(UCLASS_QFW, &dev));
	ut_assertok(qfw_write_entry(dev, FW_CFG_FILE_FIRST, sizeof(data), data));
	qfw_read_entry(dev, FW_CFG_FILE_FIRST, sizeof(result), result);
	ut_asserteq_mem(data, result, sizeof(data));

	ut_asserteq(-EIO, qfw_write_entry(dev, FW_CFG_NB_CPUS,
					  sizeof(data), data));

	qdev = dev_get_uclass_priv(dev);
	qdev->dma_present = false;
	ut_asserteq(-ENOTSUPP, qfw_write_entry(dev, FW_CFG_FILE_FIRST,
					       sizeof(data), data));

	return 0;
}
DM_TEST(dm_test_qfw_dma_write, UTF_SCAN_PDATA);
