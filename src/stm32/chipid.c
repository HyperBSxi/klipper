// Support for extracting the hardware chip id on stm32
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "generic/canserial.h" // canserial_set_uuid
#include "generic/usb_cdc.h" // usb_fill_serial
#include "generic/usbstd.h" // usb_string_descriptor
#include "internal.h" // UID_BASE
#include "sched.h" // DECL_INIT
#include <string.h> // memcpy

#define CHIP_UID_LEN 12

static struct {
    struct usb_string_descriptor desc;
    uint16_t data[CHIP_UID_LEN * 2];
} cdc_chipid;
static uint8_t chip_uid[CHIP_UID_LEN];

struct usb_string_descriptor *
usbserial_get_serialid(void)
{
   return &cdc_chipid.desc;
}

static void
chipid_read(void)
{
#if CONFIG_MACH_STM32H5
    // H5 places UID in the flash read-only area.  Read it with
    // word accesses from RAM and temporarily disable ICACHE.
    uint32_t cache_cr = ICACHE->CR;
    if (cache_cr & ICACHE_CR_EN) {
        ICACHE->CR = 0;
        while (ICACHE->SR & ICACHE_SR_BUSYF)
            ;
    }
    uint32_t *src = (void*)UID_BASE;
    uint32_t *dst = (void*)chip_uid;
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    if (cache_cr & ICACHE_CR_EN)
        ICACHE->CR = ICACHE_CR_CACHEINV | ICACHE_CR_EN;
#else
    memcpy(chip_uid, (void*)UID_BASE, CHIP_UID_LEN);
#endif
}

void
chipid_init(void)
{
    if (!(CONFIG_USB_SERIAL_NUMBER_CHIPID || CONFIG_CANBUS))
        return;
    chipid_read();
    if (CONFIG_USB_SERIAL_NUMBER_CHIPID)
        usb_fill_serial(&cdc_chipid.desc, ARRAY_SIZE(cdc_chipid.data)
                        , chip_uid);
    if (CONFIG_CANBUS)
        canserial_set_uuid(chip_uid, CHIP_UID_LEN);
}
DECL_INIT(chipid_init);
