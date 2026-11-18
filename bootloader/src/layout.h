#ifndef BOOT_LAYOUT_H
#define BOOT_LAYOUT_H

#include <stdint.h>

/*
 * GD32F330CB 128K flash:
 *   0x08000000 + 8K   bootloader（前 6K 代码，后 2K metadata A/B）
 *   0x08002000 + 60K  app
 *   0x08011000 + 60K  stage
 */
#define BOOT_BASE        0x08000000U
#define BOOT_SIZE        (8U * 1024U)
#define BOOT_CODE_SIZE   (6U * 1024U)

#define META_A_ADDR      0x08001800U
#define META_B_ADDR      0x08001C00U
#define META_PAGE_SIZE   1024U

#define APP_BASE         0x08002000U
#define APP_SIZE         (60U * 1024U)
#define APP_END          (APP_BASE + APP_SIZE)

#define STAGE_BASE       0x08011000U
#define STAGE_SIZE       (60U * 1024U)
#define STAGE_END        (STAGE_BASE + STAGE_SIZE)

#define FLASH_PAGE_SIZE  1024U
#define RAM_BASE         0x20000000U
#define RAM_SIZE         (16U * 1024U)
#define RAM_END          (RAM_BASE + RAM_SIZE)

#define UPDATE_MAGIC        0x4C435547U  /* "LCUG" */
#define UPDATE_STATE_REQUEST 0x52455131U /* "REQ1" */
#define UPDATE_STATE_COPYING 0x434F5059U /* "COPY" */
#define UPDATE_STATE_VALID   0x56414C49U /* "VALI" */
#define UPDATE_STATE_FAILED  0x4641494CU /* "FAIL" */

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t state;
    uint32_t size;
    uint32_t crc;
    uint32_t meta_crc;
} update_meta_t;

#endif /* BOOT_LAYOUT_H */
