#include <stdint.h>

#include "gd32f3x0.h"
#include "upgrade.h"

/*
 * 常驻升级器放在 flash 中间的 8K 里，拷贝前后 60K 时它本身不会被擦。
 * 因此这里的函数不能调用前 60K 里的任何代码（库函数、newlib 等），
 * FMC 操作全部在本文件内用寄存器实现。
 */
#define UPG_SEC   __attribute__((section(".upgrade")))
#define PAGE_WORDS (UPGRADE_PAGE_SIZE / 4U)

static UPG_SEC void upg_flags_clear(void)
{
    FMC_STAT = FMC_STAT_PGERR | FMC_STAT_WPERR | FMC_STAT_ENDF;
}

static UPG_SEC void upg_wait(void)
{
    while ((FMC_STAT & FMC_STAT_BUSY) != 0U) {
    }
}

static UPG_SEC void upg_unlock(void)
{
    if ((FMC_CTL & FMC_CTL_LK) != 0U) {
        FMC_KEY = 0x45670123U;
        FMC_KEY = 0xCDEF89ABU;
    }
}

static UPG_SEC void upg_erase(uint32_t addr)
{
    upg_flags_clear();
    upg_wait();

    FMC_CTL |= FMC_CTL_PER;
    FMC_ADDR = addr;
    FMC_CTL |= FMC_CTL_START;
    __NOP();
    __NOP();
    upg_wait();

    FMC_CTL &= ~FMC_CTL_PER;
}

static UPG_SEC void upg_program(uint32_t addr, uint32_t data)
{
    upg_flags_clear();
    upg_wait();

    FMC_CTL |= FMC_CTL_PG;
    *(volatile uint32_t *)addr = data;
    upg_wait();

    FMC_CTL &= ~FMC_CTL_PG;
}

/* 逐页把 stage 的 60K 拷到 front；不保留旧前区 */
static UPG_SEC void upg_copy(uint32_t front, uint32_t stage)
{
    uint32_t off;
    uint32_t i;

    for (off = 0U; off < UPGRADE_IMAGE_SIZE; off += UPGRADE_PAGE_SIZE) {
        uint32_t fa = front + off;
        uint32_t sa = stage + off;

        upg_erase(fa);
        for (i = 0U; i < PAGE_WORDS; i++) {
            upg_program(fa + 4U * i,
                        *(const volatile uint32_t *)(sa + 4U * i));
        }
    }
}

/* 直接写 SCB->AIRCR，不能调用 NVIC_SystemReset：它可能被放在前 60K */
static UPG_SEC __attribute__((noreturn)) void upg_reset(void)
{
    __DSB();
    *(volatile uint32_t *)0xE000ED0CU = (0x5FAU << 16) | (1U << 2);
    __DSB();
    for (;;) {
        __NOP();
    }
}

UPG_SEC __attribute__((noinline, noreturn))
void upgrade_apply(uint32_t front, uint32_t stage)
{
    __disable_irq();

    upg_unlock();
    upg_copy(front, stage);
    upg_reset();
}
