// SPDX-License-Identifier: GPL-2.0+
/*
 * QEMU ramfb configured through the fw_cfg DMA interface
 */

#define LOG_CATEGORY UCLASS_VIDEO

#include <dm.h>
#include <dm/lists.h>
#include <efi_loader.h>
#include <errno.h>
#include <log.h>
#include <memalign.h>
#include <qfw.h>
#include <video.h>
#include <asm/system.h>
#include <linux/sizes.h>

#define RAMFB_WIDTH	1024
#define RAMFB_HEIGHT	768
#define RAMFB_BPP	4
#define RAMFB_SIZE	(RAMFB_WIDTH * RAMFB_HEIGHT * RAMFB_BPP)
#define RAMFB_FORMAT	0x34325258	/* DRM_FORMAT_XRGB8888 */

struct ramfb_config {
	__be64 addr;
	__be32 format;
	__be32 flags;
	__be32 width;
	__be32 height;
	__be32 stride;
} __packed;

struct qfw_ramfb_plat {
	u16 selector;
};

static int qfw_ramfb_video_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->align = SZ_4K;
	plat->size = ALIGN(RAMFB_SIZE, SZ_4K);

	return 0;
}

static int qfw_ramfb_video_probe(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct video_priv *priv = dev_get_uclass_priv(dev);
	struct qfw_ramfb_plat *ramfb = dev_get_plat(dev);
#if CONFIG_IS_ENABLED(EFI_LOADER)
	efi_status_t status;
#endif
	int ret;

	ALLOC_CACHE_ALIGN_BUFFER(struct ramfb_config, cfg, 1);

	if (!plat->base || plat->size < RAMFB_SIZE)
		return log_msg_ret("framebuffer", -ENOSPC);

	/*
	 * GOP clients can write directly to FrameBufferBase, without calling
	 * video_sync(). DCACHE_WRITETHROUGH selects normal non-cacheable
	 * memory on AArch64, so QEMU sees GOP and console writes immediately.
	 */
	mmu_set_region_dcache_behaviour(plat->base, plat->size,
					DCACHE_WRITETHROUGH);

#if CONFIG_IS_ENABLED(EFI_LOADER)
	status = efi_add_memory_map(plat->base, plat->size,
				    EFI_RESERVED_MEMORY_TYPE);
	if (status != EFI_SUCCESS) {
		log_err("Failed to reserve ramfb in EFI memory map: %lx\n",
			(ulong)status);
		return -EIO;
	}
#endif

	cfg->addr = cpu_to_be64(plat->base);
	cfg->format = cpu_to_be32(RAMFB_FORMAT);
	cfg->flags = 0;
	cfg->width = cpu_to_be32(RAMFB_WIDTH);
	cfg->height = cpu_to_be32(RAMFB_HEIGHT);
	cfg->stride = cpu_to_be32(RAMFB_WIDTH * RAMFB_BPP);

	ret = qfw_write_entry(dev->parent, ramfb->selector,
			      sizeof(*cfg), cfg);
	if (ret)
		return log_msg_ret("fw_cfg write", ret);

	priv->xsize = RAMFB_WIDTH;
	priv->ysize = RAMFB_HEIGHT;
	priv->bpix = VIDEO_BPP32;
	priv->format = VIDEO_X8R8G8B8;

	return 0;
}

U_BOOT_DRIVER(qfw_ramfb) = {
	.name	= "qfw_ramfb",
	.id	= UCLASS_VIDEO,
	.plat_auto	= sizeof(struct qfw_ramfb_plat),
	.bind	= qfw_ramfb_video_bind,
	.probe	= qfw_ramfb_video_probe,
};

int qfw_ramfb_bind(void)
{
	struct udevice *qfw, *video;
	struct qfw_dev *qdev;
	struct fw_file *file;
	int ret;

	ret = qfw_get_dev(&qfw);
	if (ret == -ENODEV)
		return 0;
	if (ret)
		return log_msg_ret("fw_cfg", ret);

	ret = qfw_read_firmware_list(qfw);
	if (ret)
		return log_msg_ret("fw_cfg files", ret);

	file = qfw_find_file(qfw, "etc/ramfb");
	if (!file)
		return 0;
	if (be32_to_cpu(file->cfg.size) != sizeof(struct ramfb_config))
		return log_msg_ret("ramfb size", -EPROTO);
	qdev = dev_get_uclass_priv(qfw);
	if (!qdev->dma_present)
		return log_msg_ret("ramfb needs fw_cfg DMA", -ENOTSUPP);

	ret = device_bind_driver(qfw, "qfw_ramfb", "ramfb", &video);
	if (ret)
		return log_msg_ret("bind ramfb", ret);

	((struct qfw_ramfb_plat *)dev_get_plat(video))->selector =
		be16_to_cpu(file->cfg.select);

	return 0;
}
