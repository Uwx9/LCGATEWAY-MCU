#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdint.h>

/* 供 boot.c 和下载器共用的 FMC / metadata 操作 */

void boot_fmc_unlock(void);
void boot_fmc_lock(void);
int boot_fmc_erase_page(uint32_t addr);
int boot_fmc_program_word(uint32_t addr, uint32_t data);
int boot_meta_write(uint32_t state, uint32_t size, uint32_t crc);

#endif /* BOOT_FLASH_H */
