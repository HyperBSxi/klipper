// Code to setup clocks and peripherals on STM32H5
//
// Copyright (C) 2026  Klipper contributors
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_REF_FREQ
#include "board/armcm_boot.h" // VectorTable
#include "board/armcm_reset.h" // try_request_canboot
#include "board/misc.h" // bootloader_request
#include "command.h" // DECL_CONSTANT_STR
#include "internal.h" // enable_pclock
#include "sched.h" // sched_main

#define FREQ_PERIPH CONFIG_CLOCK_FREQ
#define FREQ_FDCAN 50000000

// Map a peripheral address to its RCC enable and reset bits.  Most H5
// peripheral positions follow the address / 0x400 convention.
struct cline
lookup_clock_line(uint32_t periph_base)
{
    if (periph_base == USB_DRD_BASE)
        return (struct cline){.en=&RCC->APB2ENR, .rst=&RCC->APB2RSTR,
                              .bit=RCC_APB2ENR_USBEN};
    if (periph_base == FDCAN1_BASE)
        return (struct cline){.en=&RCC->APB1HENR, .rst=&RCC->APB1HRSTR,
                              .bit=RCC_APB1HENR_FDCANEN};
    if (periph_base == ADC12_COMMON_BASE || periph_base == ADC1_BASE
        || periph_base == ADC2_BASE)
        return (struct cline){.en=&RCC->AHB2ENR, .rst=&RCC->AHB2RSTR,
                              .bit=RCC_AHB2ENR_ADCEN};
    if (periph_base >= AHB4PERIPH_BASE) {
        uint32_t bit = 1 << ((periph_base - AHB4PERIPH_BASE) / 0x400);
        return (struct cline){.en=&RCC->AHB4ENR, .rst=&RCC->AHB4RSTR,
                              .bit=bit};
    }
    if (periph_base >= APB3PERIPH_BASE) {
        uint32_t pos = (periph_base - APB3PERIPH_BASE) / 0x400;
        // SPI5 starts at address slot 8 and RCC enable bit 5.
        uint32_t bit = pos >= 8 ? 1 << (pos - 3) : 0;
        return (struct cline){.en=&RCC->APB3ENR, .rst=&RCC->APB3RSTR,
                              .bit=bit};
    }
    if (periph_base >= AHB2PERIPH_BASE) {
        uint32_t bit = 1 << ((periph_base - AHB2PERIPH_BASE) / 0x400);
        return (struct cline){.en=&RCC->AHB2ENR, .rst=&RCC->AHB2RSTR,
                              .bit=bit};
    }
    if (periph_base >= AHB1PERIPH_BASE) {
        uint32_t bit = 1 << ((periph_base - AHB1PERIPH_BASE) / 0x400);
        return (struct cline){.en=&RCC->AHB1ENR, .rst=&RCC->AHB1RSTR,
                              .bit=bit};
    }
    if (periph_base >= APB2PERIPH_BASE) {
        uint32_t bit = 1 << ((periph_base - APB2PERIPH_BASE) / 0x400);
        return (struct cline){.en=&RCC->APB2ENR, .rst=&RCC->APB2RSTR,
                              .bit=bit};
    }
    uint32_t offset = (periph_base - APB1PERIPH_BASE) / 0x400;
    if (offset < 32)
        return (struct cline){.en=&RCC->APB1LENR, .rst=&RCC->APB1LRSTR,
                              .bit=1 << offset};
    return (struct cline){.en=&RCC->APB1HENR, .rst=&RCC->APB1HRSTR,
                          .bit=1 << (offset - 32)};
}

uint32_t
get_pclock_frequency(uint32_t periph_base)
{
    if (periph_base == FDCAN1_BASE)
        return FREQ_FDCAN;
    return FREQ_PERIPH;
}

void
gpio_clock_enable(GPIO_TypeDef *regs)
{
    uint32_t pos = ((uint32_t)regs - GPIOA_BASE) / 0x400;
    RCC->AHB2ENR |= 1 << pos;
    RCC->AHB2ENR;
}

#if !CONFIG_STM32_CLOCK_REF_INTERNAL
DECL_CONSTANT_STR("RESERVE_PINS_crystal", "PH0,PH1");
#endif

static void
clock_setup(void)
{
    // Voltage scale 0 is required for 250MHz operation.
    PWR->VOSCR = PWR_VOSCR_VOS;
    while (!(PWR->VOSSR & PWR_VOSSR_VOSRDY))
        ;

    // Configure PLL1 for a 500MHz VCO, 250MHz SYSCLK, and 50MHz Q clock.
    uint32_t pll_input, pll_source, rcc_cr = RCC_CR_HSION;
    if (CONFIG_STM32_CLOCK_REF_INTERNAL) {
        pll_input = 64000000;
        pll_source = RCC_PLL1CFGR_PLL1SRC_0; // HSI64
        RCC->CR |= RCC_CR_HSION;
        while (!(RCC->CR & RCC_CR_HSIRDY))
            ;
    } else {
        pll_input = CONFIG_CLOCK_REF_FREQ;
        pll_source = RCC_PLL1CFGR_PLL1SRC; // HSE
        RCC->CR |= RCC_CR_HSEON;
        while (!(RCC->CR & RCC_CR_HSERDY))
            ;
        rcc_cr |= RCC_CR_HSEON;
    }
    uint32_t pllm = pll_input / 4000000;
    RCC->PLL1CFGR = (pll_source | RCC_PLL1CFGR_PLL1RGE_1
                     | (pllm << RCC_PLL1CFGR_PLL1M_Pos)
                     | RCC_PLL1CFGR_PLL1PEN | RCC_PLL1CFGR_PLL1QEN);
    RCC->PLL1DIVR = ((125 - 1) << RCC_PLL1DIVR_PLL1N_Pos)
        | ((2 - 1) << RCC_PLL1DIVR_PLL1P_Pos)
        | ((10 - 1) << RCC_PLL1DIVR_PLL1Q_Pos);

    // Configure flash before increasing the core clock.
    FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_5WS)
        ;

    RCC->CFGR2 = 0; // HCLK and all PCLKs run at SYSCLK
    RCC->CR = rcc_cr | RCC_CR_PLL1ON;
    while (!(RCC->CR & RCC_CR_PLL1RDY))
        ;
    RCC->CFGR1 = RCC_CFGR1_SW; // PLL1
    while ((RCC->CFGR1 & RCC_CFGR1_SWS) != RCC_CFGR1_SWS)
        ;

    // The H5 has a peripheral instruction cache rather than an M7 SCB cache.
    ICACHE->CR = ICACHE_CR_EN;

    // FDCAN uses the 50MHz PLL1Q output.
    RCC->CCIPR5 = (RCC->CCIPR5 & ~RCC_CCIPR5_FDCANSEL)
        | RCC_CCIPR5_FDCANSEL_0;

    if (CONFIG_USB) {
        // Enable the HSI48 oscillator, USB supply, and clock recovery.
        RCC->CR |= RCC_CR_HSI48ON;
        while (!(RCC->CR & RCC_CR_HSI48RDY))
            ;
        PWR->USBSCR |= PWR_USBSCR_USB33DEN;
        while (!(PWR->VMSR & PWR_VMSR_USB33RDY))
            ;
        PWR->USBSCR |= PWR_USBSCR_USB33SV;
        enable_pclock(CRS_BASE);
        CRS->CR = (CRS->CR & CRS_CR_TRIM) | CRS_CR_CEN | CRS_CR_AUTOTRIMEN;
        RCC->CCIPR4 |= RCC_CCIPR4_USBSEL; // HSI48
    }
}

void
bootloader_request(void)
{
    try_request_canboot();
    dfu_reboot();
}

void
armcm_main(void)
{
    SystemInit();
    SCB->VTOR = (uint32_t)VectorTable;
    dfu_reboot_check();
    clock_setup();
    __enable_irq();
    sched_main();
}
