// Debugging commands.
//
// Copyright (C) 2016-2021  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_MACH_STM32H5
#include "board/io.h" // readl
#include "board/irq.h" // irq_save
#include "command.h" // DECL_COMMAND
#if CONFIG_MACH_STM32H5
#include "board/internal.h" // ICACHE

#define H5_INFO_START 0x08FFF800
#define H5_INFO_END   0x08FFF81f

static uint32_t
debug_read_stm32h5_info(uint8_t order, uint32_t addr)
{
    uint32_t aligned_addr = addr & ~0x3;
    uint32_t cache_cr = ICACHE->CR;
    if (cache_cr & ICACHE_CR_EN) {
        ICACHE->CR = 0;
        while (ICACHE->SR & ICACHE_SR_BUSYF)
            ;
    }
    uint32_t word = *(volatile uint32_t*)aligned_addr;
    if (cache_cr & ICACHE_CR_EN)
        ICACHE->CR = ICACHE_CR_CACHEINV | ICACHE_CR_EN;
    switch (order) {
    default:
    case 0:
        return (word >> ((addr & 0x3) * 8)) & 0xff;
    case 1:
        return (word >> ((addr & 0x2) * 8)) & 0xffff;
    case 2:
        return word;
    }
}
#endif

void
command_debug_read(uint32_t *args)
{
    uint8_t order = args[0];
    uint32_t addr = args[1];
    void *ptr = command_decode_ptr(addr);
    uint32_t v;
    irqstatus_t flag = irq_save();
#if CONFIG_MACH_STM32H5
    if (addr >= H5_INFO_START && addr <= H5_INFO_END) {
        v = debug_read_stm32h5_info(order, addr);
        irq_restore(flag);
        sendf("debug_result val=%u", v);
        return;
    }
#endif
    switch (order) {
    default: case 0: v = readb(ptr); break;
    case 1:          v = readw(ptr); break;
    case 2:          v = readl(ptr); break;
    }
    irq_restore(flag);
    sendf("debug_result val=%u", v);
}
DECL_COMMAND_FLAGS(command_debug_read, HF_IN_SHUTDOWN,
                   "debug_read order=%c addr=%u");

void
command_debug_write(uint32_t *args)
{
    uint8_t order = args[0];
    void *ptr = command_decode_ptr(args[1]);
    uint32_t v = args[2];
    irqstatus_t flag = irq_save();
    switch (order) {
    default: case 0: writeb(ptr, v); break;
    case 1:          writew(ptr, v); break;
    case 2:          writel(ptr, v); break;
    }
    irq_restore(flag);
}
DECL_COMMAND_FLAGS(command_debug_write, HF_IN_SHUTDOWN,
                   "debug_write order=%c addr=%u val=%u");

void
command_debug_ping(uint32_t *args)
{
    uint8_t len = args[0];
    char *data = command_decode_ptr(args[1]);
    sendf("pong data=%*s", len, data);
}
DECL_COMMAND_FLAGS(command_debug_ping, HF_IN_SHUTDOWN, "debug_ping data=%*s");

void
command_debug_nop(uint32_t *args)
{
}
DECL_COMMAND_FLAGS(command_debug_nop, HF_IN_SHUTDOWN, "debug_nop");
