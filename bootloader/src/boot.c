#include <stdint.h>

#include "gd32f3x0.h"
#include "crc16.h"
#include "layout.h"

#define META_CRC_LEN  20U

static void boot_fmc_flags_clear(void)
{
    FMC_STAT = FMC_STAT_PGERR | FMC_STAT_WPERR | FMC_STAT_ENDF;
}

static int boot_fmc_wait(void)
{
    while ((FMC_STAT & FMC_STAT_BUSY) != 0U) {
    }

    return ((FMC_STAT & (FMC_STAT_PGERR | FMC_STAT_WPERR)) != 0U) ? -1 : 0;
}

static void boot_fmc_unlock(void)
{
    if ((FMC_CTL & FMC_CTL_LK) != 0U) {
        FMC_KEY = 0x45670123U;
        FMC_KEY = 0xCDEF89ABU;
    }
}

static void boot_fmc_lock(void)
{
    FMC_CTL |= FMC_CTL_LK;
}

static int boot_fmc_erase_page(uint32_t addr)
{
    int ret;

    boot_fmc_flags_clear();
    if (boot_fmc_wait() != 0) {
        return -1;
    }

    FMC_CTL |= FMC_CTL_PER;
    FMC_ADDR = addr;
    FMC_CTL |= FMC_CTL_START;
    __NOP();
    __NOP();
    ret = boot_fmc_wait();
    FMC_CTL &= ~FMC_CTL_PER;

    return ret;
}

static int boot_fmc_program_word(uint32_t addr, uint32_t data)
{
    int ret;

    boot_fmc_flags_clear();
    if (boot_fmc_wait() != 0) {
        return -1;
    }

    FMC_CTL |= FMC_CTL_PG;
    *(volatile uint32_t *)addr = data;
    ret = boot_fmc_wait();
    FMC_CTL &= ~FMC_CTL_PG;

    return ret;
}

static int meta_valid(const update_meta_t *m)
{
    if (m->magic != UPDATE_MAGIC) {
        return 0;
    }
    if (m->size > APP_SIZE) {
        return 0;
    }
    if ((m->state != UPDATE_STATE_REQUEST) &&
        (m->state != UPDATE_STATE_COPYING) &&
        (m->state != UPDATE_STATE_VALID) &&
        (m->state != UPDATE_STATE_FAILED)) {
        return 0;
    }

    return (crc16((const uint8_t *)m, META_CRC_LEN) == m->meta_crc) ? 1 : 0;
}

static const update_meta_t *meta_active(void)
{
    const update_meta_t *a = (const update_meta_t *)META_A_ADDR;
    const update_meta_t *b = (const update_meta_t *)META_B_ADDR;
    int va = meta_valid(a);
    int vb = meta_valid(b);

    if (va && vb) {
        return (a->seq >= b->seq) ? a : b;
    }
    if (va) {
        return a;
    }
    if (vb) {
        return b;
    }

    return 0;
}

static int meta_write(uint32_t state, uint32_t size, uint32_t crc)
{
    const update_meta_t *active = meta_active();
    update_meta_t m;
    uint32_t seq = 1U;
    uint32_t addr = META_A_ADDR;
    uint32_t i;

    if (active != 0) {
        seq = active->seq + 1U;
        addr = (active == (const update_meta_t *)META_A_ADDR) ? META_B_ADDR : META_A_ADDR;
    }

    m.magic = UPDATE_MAGIC;
    m.seq = seq;
    m.state = state;
    m.size = size;
    m.crc = crc;
    m.meta_crc = crc16((const uint8_t *)&m, META_CRC_LEN);

    boot_fmc_unlock();
    if (boot_fmc_erase_page(addr) != 0) {
        boot_fmc_lock();
        return -1;
    }
    for (i = 0U; i < 6U; i++) {
        if (boot_fmc_program_word(addr + 4U * i, ((const uint32_t *)&m)[i]) != 0) {
            boot_fmc_lock();
            return -1;
        }
    }
    boot_fmc_lock();

    return 0;
}

static int app_vector_valid(void)
{
    uint32_t sp = *(const volatile uint32_t *)APP_BASE;
    uint32_t pc = *(const volatile uint32_t *)(APP_BASE + 4U);

    if ((sp < RAM_BASE) || (sp > RAM_END) || ((sp & 7U) != 0U)) {
        return 0;
    }
    if ((pc < APP_BASE) || (pc >= APP_END) || ((pc & 1U) == 0U)) {
        return 0;
    }

    return 1;
}

static void jump_app(void) __attribute__((noreturn));
static void jump_app(void)
{
    uint32_t sp = *(const volatile uint32_t *)APP_BASE;
    uint32_t pc = *(const volatile uint32_t *)(APP_BASE + 4U);
    unsigned i;

    if (!app_vector_valid()) {
        for (;;) {
        }
    }

    __disable_irq();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (i = 0U; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

    SCB->VTOR = APP_BASE;
    __DSB();
    __ISB();

    __set_CONTROL(0U);
    __set_MSP(sp);
    ((void (*)(void))pc)();

    for (;;) {
    }
}

static int copy_app(uint32_t size, uint32_t crc)
{
    uint32_t pages = (size + FLASH_PAGE_SIZE - 1U) / FLASH_PAGE_SIZE;
    uint32_t words = (size + 3U) / 4U;
    uint32_t i;

    boot_fmc_unlock();
    for (i = 0U; i < pages; i++) {
        if (boot_fmc_erase_page(APP_BASE + i * FLASH_PAGE_SIZE) != 0) {
            boot_fmc_lock();
            return -1;
        }
    }
    for (i = 0U; i < words; i++) {
        uint32_t w = *(const volatile uint32_t *)(STAGE_BASE + 4U * i);
        if (boot_fmc_program_word(APP_BASE + 4U * i, w) != 0) {
            boot_fmc_lock();
            return -1;
        }
    }
    boot_fmc_lock();

    return (crc16((const uint8_t *)APP_BASE, size) == crc) ? 0 : -1;
}

int main(void)
{
    const update_meta_t *m;

    SCB->VTOR = BOOT_BASE;
    __DSB();
    __ISB();

    boot_fmc_unlock();
    m = meta_active();

    if (m != 0 && m->state == UPDATE_STATE_REQUEST) {
        if ((m->size == 0U) || (m->size > APP_SIZE) ||
            (crc16((const uint8_t *)STAGE_BASE, m->size) != m->crc)) {
            (void)meta_write(UPDATE_STATE_FAILED, m->size, m->crc);
            if (app_vector_valid()) {
                jump_app();
            }
            for (;;) {
            }
        }

        if (crc16((const uint8_t *)APP_BASE, m->size) == m->crc) {
            (void)meta_write(UPDATE_STATE_VALID, m->size, m->crc);
            NVIC_SystemReset();
        }

        if (meta_write(UPDATE_STATE_COPYING, m->size, m->crc) != 0) {
            if (app_vector_valid()) {
                jump_app();
            }
            for (;;) {
            }
        }
        m = meta_active();
    }

    if (m != 0 && m->state == UPDATE_STATE_COPYING) {
        if ((m->size == 0U) || (m->size > APP_SIZE) ||
            (crc16((const uint8_t *)STAGE_BASE, m->size) != m->crc)) {
            /* stage 不完整：保持 COPYING，下次上电重试；不要跳 partial app */
            for (;;) {
            }
        }

        if (crc16((const uint8_t *)APP_BASE, m->size) == m->crc) {
            (void)meta_write(UPDATE_STATE_VALID, m->size, m->crc);
            NVIC_SystemReset();
        }

        if (copy_app(m->size, m->crc) == 0) {
            (void)meta_write(UPDATE_STATE_VALID, m->size, m->crc);
            NVIC_SystemReset();
        }

        /* 拷贝失败保持 COPYING，下次上电重试；不要跳 partial app */
        for (;;) {
        }
    }

    if (m != 0 && m->state == UPDATE_STATE_VALID) {
        if (crc16((const uint8_t *)APP_BASE, m->size) == m->crc) {
            jump_app();
        }
        if (crc16((const uint8_t *)STAGE_BASE, m->size) == m->crc) {
            (void)meta_write(UPDATE_STATE_COPYING, m->size, m->crc);
            NVIC_SystemReset();
        }
        for (;;) {
        }
    }

    if (app_vector_valid()) {
        jump_app();
    }

    for (;;) {
    }
}
