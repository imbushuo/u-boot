// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2015 Miao Yan <yanmiaobest@gmail.com>
 * (C) Copyright 2021 Asherah Connor <ashe@kivikakk.ee>
 */

#define LOG_CATEGORY UCLASS_QFW

#include <acpi/acpi_table.h>
#include <bootdev.h>
#include <bootflow.h>
#include <bootmeth.h>
#include <command.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <qfw.h>
#include <dm.h>
#include <memalign.h>
#include <misc.h>
#include <cpu_func.h>
#include <tables_csum.h>
#include <asm/acpi_table.h>

static void qfw_read_entry_io(struct qfw_dev *qdev, u16 entry, u32 size,
			      void *address)
{
	struct dm_qfw_ops *ops = dm_qfw_get_ops(qdev->dev);

	debug("%s: entry 0x%x, size %u address %p\n", __func__, entry, size,
	      address);

	ops->read_entry_io(qdev->dev, entry, size, address);
}

static int qfw_dma_transfer(struct qfw_dev *qdev, u16 entry, u32 size,
			    const void *address, u32 control)
{
	struct dm_qfw_ops *ops = dm_qfw_get_ops(qdev->dev);
	ALLOC_CACHE_ALIGN_BUFFER(struct qfw_dma, dma, 1);
	ulong start = ALIGN_DOWN((ulong)address, ARCH_DMA_MINALIGN);
	ulong end = ALIGN((ulong)address + size, ARCH_DMA_MINALIGN);
	int ret;

	/*
	 * FW_CFG_INVALID continues the current transfer at its previous offset;
	 * any other entry selects a file and starts at offset 0.
	 */
	if (entry != FW_CFG_INVALID)
		control |= FW_CFG_DMA_SELECT | (entry << 16);

	dma->length = cpu_to_be32(size);
	dma->address = cpu_to_be64((uintptr_t)address);
	dma->control = cpu_to_be32(control);

	debug("%s: entry 0x%x, size %u address %p, control 0x%x\n", __func__,
	      entry, size, address, control);

	flush_dcache_range(start, end);
	flush_dcache_range((ulong)dma,
			   (ulong)dma + ALIGN(sizeof(*dma), ARCH_DMA_MINALIGN));
	barrier();

	ret = ops->dma_transfer(qdev->dev, dma);
	if (control & FW_CFG_DMA_READ)
		invalidate_dcache_range(start, end);

	return ret;
}

void qfw_read_entry(struct udevice *dev, u16 entry, u32 size, void *address)
{
	struct qfw_dev *qdev = dev_get_uclass_priv(dev);

	if (qdev->dma_present) {
		if (qfw_dma_transfer(qdev, entry, size, address, FW_CFG_DMA_READ))
			log_err("QEMU fw_cfg DMA read failed (entry %x)\n", entry);
	} else {
		qfw_read_entry_io(qdev, entry, size, address);
	}
}

int qfw_write_entry(struct udevice *dev, u16 entry, u32 size,
		    const void *address)
{
	struct qfw_dev *qdev = dev_get_uclass_priv(dev);

	if (!qdev->dma_present)
		return -ENOTSUPP;

	return qfw_dma_transfer(qdev, entry, size, address, FW_CFG_DMA_WRITE);
}

int qfw_register(struct udevice *dev)
{
	struct qfw_dev *qdev = dev_get_uclass_priv(dev);
	u32 qemu, dma_enabled;

	qdev->dev = dev;
	INIT_LIST_HEAD(&qdev->fw_list);

	qfw_read_entry_io(qdev, FW_CFG_SIGNATURE, 4, &qemu);
	if (be32_to_cpu(qemu) != QEMU_FW_CFG_SIGNATURE)
		return -ENODEV;

	qfw_read_entry_io(qdev, FW_CFG_ID, 1, &dma_enabled);
	if (dma_enabled & FW_CFG_DMA_ENABLED)
		qdev->dma_present = true;

	return 0;
}

static int qfw_post_bind(struct udevice *dev)
{
	int ret;

	ret = bootdev_setup_for_dev(dev, "qfw_bootdev");
	if (ret)
		return log_msg_ret("dev", ret);

	return 0;
}

static int qfw_get_bootflow(struct udevice *dev, struct bootflow_iter *iter,
			    struct bootflow *bflow)
{
	const struct udevice *media = dev_get_parent(dev);
	int ret;

	if (!CONFIG_IS_ENABLED(BOOTSTD))
		return -ENOSYS;

	log_debug("media=%s\n", media->name);
	ret = bootmeth_check(bflow->method, iter);
	if (ret)
		return log_msg_ret("check", ret);

	log_debug("iter->part=%d\n", iter->part);

	/* We only support the whole device, not partitions */
	if (iter->part)
		return log_msg_ret("max", -ESHUTDOWN);

	log_debug("reading bootflow with method: %s\n", bflow->method->name);
	ret = bootmeth_read_bootflow(bflow->method, bflow);
	if (ret)
		return log_msg_ret("method", ret);

	return 0;
}

static int qfw_bootdev_bind(struct udevice *dev)
{
	struct bootdev_uc_plat *ucp = dev_get_uclass_plat(dev);

	ucp->prio = BOOTDEVP_4_SCAN_FAST;

	return 0;
}

static int qfw_bootdev_hunt(struct bootdev_hunter *info, bool show)
{
	int ret;

	ret = uclass_probe_all(UCLASS_QFW);
	if (ret && ret != -ENOENT)
		return log_msg_ret("vir", ret);

	return 0;
}

UCLASS_DRIVER(qfw) = {
	.id		= UCLASS_QFW,
	.name		= "qfw",
	.post_bind	= qfw_post_bind,
	.per_device_auto	= sizeof(struct qfw_dev),
};

static const struct bootdev_ops qfw_bootdev_ops = {
	.get_bootflow	= qfw_get_bootflow,
};

static const struct udevice_id qfw_bootdev_ids[] = {
	{ .compatible = "u-boot,bootdev-qfw" },
	{ }
};

U_BOOT_DRIVER(qfw_bootdev) = {
	.name		= "qfw_bootdev",
	.id		= UCLASS_BOOTDEV,
	.ops		= &qfw_bootdev_ops,
	.bind		= qfw_bootdev_bind,
	.of_match	= qfw_bootdev_ids,
};

BOOTDEV_HUNTER(qfw_bootdev_hunter) = {
	.prio		= BOOTDEVP_4_SCAN_FAST,
	.uclass		= UCLASS_QFW,
	.hunt		= qfw_bootdev_hunt,
	.drv		= DM_DRIVER_REF(qfw_bootdev),
};
