/*
 * (C) Copyright 2018
 * Bingxing Wang
 *
 * reference linux/drivers/rtc/rtc-pm8xxx.c 
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <rtc.h>
#include <dm.h>
#include <power/pmic.h>
#include <spmi/spmi.h>
#include <linux/bitops.h>

#if defined(CONFIG_CMD_DATE)

static int  pm8916_rtc_initiated = 0;
bool        pm8916_rtc_offset_is_valid;
long long   pm8916_rtc_offset;

/* RTC Register offsets from RTC CTRL REG */
#define PM8916_RTC_CONTROL_ADDR	        0x6046
#define PM8916_RTC_READ_ADDR            0x6048
#define NUM_8_BIT_RTC_REGS              0x04

#define PM8xxx_RTC_ENABLE		        (1UL << 7)

/* Enable RTC Start in Control register */
static void pm8916_rtc_init(const struct udevice *dev)
{
    debug("PM8916 RTC: pm8916_rtc_init\n");

    /* Check if the RTC is on, else turn it on */
    unsigned int ctrl_reg;
    int rc;
    
    ctrl_reg = pmic_reg_read(dev->parent, PM8916_RTC_CONTROL_ADDR);

    if (!(ctrl_reg & PM8xxx_RTC_ENABLE)) {
        debug("PM8916 RTC: Enable RTC\n");
        ctrl_reg |= PM8xxx_RTC_ENABLE;
        rc = pmic_reg_write(dev->parent, PM8916_RTC_CONTROL_ADDR, ctrl_reg);
        if (rc) debug("PM8916 RTC: Something happened\n");
    }

    debug("PM8916 RTC: Initialized\n");
    pm8916_rtc_initiated = 1;

    pm8916_rtc_offset_is_valid = false;
    pm8916_rtc_offset = 0;
}

static int pm8916_get_epoch(const struct udevice *dev)
{
    debug("PM8916 RTC: pm8916_get_epoch\n");

    unsigned long secs;
    u8 value[NUM_8_BIT_RTC_REGS];

    if (!pm8916_rtc_initiated) {
        pm8916_rtc_init(dev);
    }

    value[0] = pmic_reg_read(dev->parent, PM8916_RTC_READ_ADDR);
    value[1] = pmic_reg_read(dev->parent, PM8916_RTC_READ_ADDR + 0x01);
    value[2] = pmic_reg_read(dev->parent, PM8916_RTC_READ_ADDR + 0x02);
    value[3] = pmic_reg_read(dev->parent, PM8916_RTC_READ_ADDR + 0x03);

    if (value[0] < 0) {
        puts("PM8916 RTC: Failed\n");
        return -1;
    }

    secs = value[0] | (value[1] << 8) | (value[2] << 16) | (value[3] << 24);
    return secs;
}

/*
 * Reset the RTC. We set the date back to 1970-01-01.
 */
static int pm8916_rtc_reset(struct udevice *dev)
{
    debug("PM8916 RTC: pm8916_rtc_reset\n");

    // No way can do in PM8916
    // Simply set new offset for further references
    unsigned long secs = pm8916_get_epoch(dev);

    pm8916_rtc_offset = secs;
    pm8916_rtc_offset_is_valid = true;

    return 0;
}

/*
 * Get the current time from the RTC
 */
static int pm8916_rtc_get(struct udevice *dev, struct rtc_time *tmp)
{
    debug("PM8916 RTC: pm8916_rtc_get\n");

    if (tmp == NULL) {
        debug("PM8916 RTC: Access violation\n");
        return -1;
    }

    // Handle resets
    unsigned long secs = pm8916_get_epoch(dev);
    if (pm8916_rtc_offset_is_valid) {
        secs = secs - pm8916_rtc_offset;
    }

    if (secs < 0) {
        debug("PM8916 RTC: secs < 0\n");
        return -1;
    }

    // Report time
    rtc_to_tm(secs, tmp);
	debug("PM8916 RTC Get DATE: %4d-%02d-%02d (wday=%d)  TIME: %2d:%02d:%02d\n",
		tmp->tm_year, tmp->tm_mon, tmp->tm_mday, tmp->tm_wday,
		tmp->tm_hour, tmp->tm_min, tmp->tm_sec);

	return 0;
}

/*
 * Set the RTC
*/
static int pm8916_rtc_set(struct udevice *dev, const struct rtc_time *tmp)
{
    debug("PM8916 RTC: pm8916_rtc_set\n");

    unsigned long tim, curr;

    if (!pm8916_rtc_initiated) {
        pm8916_rtc_init(dev);
    }

    if (tmp == NULL) {
        debug("PM8916 RTC: Access violation\n");
        return -1;
    }

    /* Calculate number of seconds this incoming time represents */
	tim = rtc_mktime(tmp);

    /* Update current offset */
    curr = pm8916_get_epoch(dev);
    pm8916_rtc_offset = curr - tim;
    pm8916_rtc_offset_is_valid = true;

    return 0;
}

static int pm8916_rtc_probe(struct udevice *dev) 
{
    /* Assume everything is good */
    debug("PM8916 RTC: pm8916_rtc_probe\n");
    return 0;
}

static const struct rtc_ops pm8916_rtc_ops = {
	.get = pm8916_rtc_get,
	.set = pm8916_rtc_set,
	.reset = pm8916_rtc_reset,
};

static const struct udevice_id pm8916_rtc_ids[] = {
	{ .compatible = "qcom,pm8941-rtc" },
	{ }
};

U_BOOT_DRIVER(pm8916_rtc) = {
    .name = "pm8916_rtc",
    .id = UCLASS_RTC,
    .of_match = pm8916_rtc_ids,
    .probe = pm8916_rtc_probe,
    .ops = &pm8916_rtc_ops
};

#endif