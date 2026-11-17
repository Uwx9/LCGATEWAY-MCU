/*
 * 测试6:
 * 测试方法: 读取 tests/fw0.2.elf，把它落在前 60K 的 PT_LOAD 段拼成完整 app 镜像，
 *           通过 IAP 协议真实下载、擦写并触发交换重启。
 * 测试目标: 旧固件被新固件替换；重启后版本号变为 ELF 里的版本。
 * 测试目的: 端到端验证 0x01~0x08 全流程和 upgrade 的 60K 拷贝。
 *
 * 编译:
 *   cc -std=gnu11 -Wall -Wextra -O2 test6.c frame_io.c iap_flow.c -o test6
 *
 * 运行（在 tests 目录下）:
 *   ./test6 [/dev/ttyUSB0] [fw0.2.elf]
 *
 * 注意: 固件 ELF 只取 p_paddr 在 0x08000000~0x0800EFFF 的段，中间的
 *       upgrade 代码段不会被下载。更新前后可以用 RTT 的 ps 看版本和存储布局。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frame_io.h"
#include "iap_flow.h"

#define ELF_PT_LOAD     1U
#define APP_BASE        0x08000000U
#define APP_LIMIT       0x0800F000U          /* 中间 8K upgrade 区起点 */
#define APP_MAX         (APP_LIMIT - APP_BASE)

#define VER_STR_OFF     0x150U
#define VER_NUM_OFF     0x170U

#define PACKET_TRIES    3
#define SWAP_WAIT_S     6U

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int read_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *f;
    long len;
    uint8_t *buf;

    f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        return -1;
    }
    if ((fseek(f, 0, SEEK_END) != 0) || ((len = ftell(f)) <= 0)) {
        fprintf(stderr, "%s: bad file size\n", path);
        fclose(f);
        return -1;
    }
    rewind(f);

    buf = malloc((size_t)len);
    if (buf == NULL) {
        perror("malloc");
        fclose(f);
        return -1;
    }
    if (fread(buf, 1U, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "%s: read failed\n", path);
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    *out = buf;
    *out_len = (size_t)len;
    return 0;
}

/*
 * 把 ELF 里落在前 60K 的可加载段按 p_paddr 拼成镜像。
 * 段之间没覆盖到的字节保持 0xFF，和擦除后的 flash 一致。
 */
static int build_image(const char *path, uint8_t *image, size_t cap, size_t *out_len)
{
    uint8_t *elf;
    size_t elf_len;
    uint32_t phoff;
    uint32_t phentsize;
    uint32_t phnum;
    size_t image_end = 0U;
    uint32_t i;

    if (read_file(path, &elf, &elf_len) != 0) {
        return -1;
    }
    if ((elf_len < 0x34U) || (memcmp(elf, "\x7f" "ELF", 4U) != 0) ||
        (elf[4] != 1U) || (elf[5] != 1U)) {
        fprintf(stderr, "%s: not a little-endian ELF32\n", path);
        free(elf);
        return -1;
    }

    phoff     = rd32(elf + 0x1CU);
    phentsize = rd16(elf + 0x2AU);
    phnum     = rd16(elf + 0x2CU);

    if ((phentsize < 32U) ||
        ((size_t)phoff + (size_t)phnum * (size_t)phentsize > elf_len)) {
        fprintf(stderr, "%s: bad program header table\n", path);
        free(elf);
        return -1;
    }

    memset(image, 0xFF, cap);

    for (i = 0U; i < phnum; i++) {
        const uint8_t *ph = elf + (size_t)phoff + (size_t)i * (size_t)phentsize;
        uint32_t type    = rd32(ph);
        uint32_t offset  = rd32(ph + 4U);
        uint32_t paddr   = rd32(ph + 12U);
        uint32_t filesz  = rd32(ph + 16U);
        size_t img_off;
        size_t img_end;

        if ((type != ELF_PT_LOAD) || (filesz == 0U)) {
            continue;
        }
        if ((paddr < APP_BASE) || (paddr >= APP_LIMIT)) {
            continue;
        }

        img_off = (size_t)(paddr - APP_BASE);
        img_end = img_off + (size_t)filesz;
        if (img_end > cap) {
            fprintf(stderr, "%s: app image exceeds %u bytes\n",
                    path, (unsigned)APP_MAX);
            free(elf);
            return -1;
        }
        if (((size_t)offset + (size_t)filesz > elf_len)) {
            fprintf(stderr, "%s: program segment out of file\n", path);
            free(elf);
            return -1;
        }

        memcpy(image + img_off, elf + offset, filesz);
        if (img_end > image_end) {
            image_end = img_end;
        }
    }

    free(elf);

    if (image_end == 0U) {
        fprintf(stderr, "%s: no loadable segment in app region\n", path);
        return -1;
    }

    *out_len = image_end;
    return 0;
}

static int report(const char *name, int err, const uint8_t *data, uint8_t data_len)
{
    if (err != ACK_OK) {
        printf("FAIL %-28s: %s\n", name, ack_str(err));
        return -1;
    }

    printf("PASS %-28s", name);
    if (data_len != 0U) {
        printf(" data=");
        dump_hex(data, data_len);
    }
    printf("\n");
    return 0;
}

/* 超时重发同一包：MCU 对重复包只回 ACK，不会重复写 flash */
static int send_packet(uint16_t id, const uint8_t *data, uint8_t len)
{
    int tries;
    int e = ACK_ERR_TIMEOUT;

    for (tries = 0; tries < PACKET_TRIES; tries++) {
        e = iap_packet(id, data, len);
        if (e == ACK_OK) {
            return ACK_OK;
        }
        if (e != ACK_ERR_TIMEOUT) {
            return e;
        }
        printf("retry packet id=%u\n", id);
    }
    return e;
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    const char *elf = (argc > 2) ? argv[2] : "fw0.2.elf";
    uint8_t *image;
    size_t image_size = 0U;
    size_t off;
    uint16_t total_packets;
    uint16_t crc;
    uint16_t id;
    uint8_t ver[2];
    uint8_t got[2];
    int keepalive = 0;
    int ret = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    image = malloc(APP_MAX);
    if (image == NULL) {
        perror("malloc");
        return 1;
    }
    if (build_image(elf, image, APP_MAX, &image_size) != 0) {
        free(image);
        return 1;
    }

    ver[0] = image[VER_NUM_OFF];
    ver[1] = image[VER_NUM_OFF + 1U];
    crc = crc16(image, image_size);
    total_packets = (uint16_t)((image_size + IAP_DATA_MAX - 1U) / IAP_DATA_MAX);

    printf("IAP test6 on %s\n", dev);
    printf("image: %s\n", elf);
    printf("  size=%u bytes, packets=%u, crc=0x%04X\n",
           (unsigned)image_size, (unsigned)total_packets, crc);
    printf("  version string: %.32s\n", (const char *)(image + VER_STR_OFF));
    printf("  target version: %u.%u\n", (unsigned)ver[0], (unsigned)ver[1]);
    printf("  sp=0x%08X reset=0x%08X\n", rd32(image), rd32(image + 4U));

    if ((rd32(image + 4U) < APP_BASE) || (rd32(image + 4U) >= APP_LIMIT)) {
        fprintf(stderr, "bad reset vector in image\n");
        free(image);
        return 1;
    }

    if (serial_open(dev) != 0) {
        free(image);
        return 1;
    }

    if (report("start upgrade", iap_start(), NULL, 0U) != 0) {
        goto out;
    }
    if (report("get current version", iap_version(ver, got), got, 2U) != 0) {
        goto out;
    }
    printf("current version: %u.%u\n", (unsigned)got[0], (unsigned)got[1]);
    if ((got[0] == ver[0]) && (got[1] == ver[1])) {
        printf("NOTE: target version equals current version\n");
    }

    if (report("file size", iap_size((uint32_t)image_size), NULL, 0U) != 0) {
        goto out;
    }

    {
        int e = iap_erase(&keepalive);

        if (e != ACK_OK) {
            printf("FAIL %-28s: %s\n", "erase", ack_str(e));
            goto out;
        }
        printf("PASS %-28s keepalive=%d\n", "erase", keepalive);
    }

    off = 0U;
    for (id = 0U; id < total_packets; id++) {
        size_t left = image_size - off;
        uint8_t plen = (uint8_t)((left > IAP_DATA_MAX) ? IAP_DATA_MAX : left);
        int e = send_packet(id, image + off, plen);

        if (e != ACK_OK) {
            printf("FAIL packet id=%u: %s\n", (unsigned)id, ack_str(e));
            goto out;
        }
        off += plen;

        if (((id + 1U) % 16U == 0U) || ((id + 1U) == total_packets)) {
            printf("packet %u/%u\n",
                   (unsigned)(id + 1U), (unsigned)total_packets);
        }
    }

    if (report("packet count", iap_packet_count(total_packets), NULL, 0U) != 0) {
        goto out;
    }
    if (report("total crc", iap_crc(crc), NULL, 0U) != 0) {
        goto out;
    }
    if (report("finish/reboot", iap_finish(), NULL, 0U) != 0) {
        goto out;
    }

    printf("0x09 received, swapping 60K and resetting, wait %u s...\n",
           (unsigned)SWAP_WAIT_S);
    sleep(SWAP_WAIT_S);

    if (report("version after reset", iap_version(ver, got), got, 2U) != 0) {
        goto out;
    }
    printf("running version: %u.%u\n", (unsigned)got[0], (unsigned)got[1]);

    if ((got[0] != ver[0]) || (got[1] != ver[1])) {
        printf("FAIL %-28s: expect %u.%u, got %u.%u\n", "version check",
               (unsigned)ver[0], (unsigned)ver[1],
               (unsigned)got[0], (unsigned)got[1]);
        goto out;
    }

    printf("TEST6 PASS\n");
    ret = 0;

out:
    if (ret != 0) {
        printf("TEST6 FAIL\n");
    }
    serial_close();
    free(image);
    return ret;
}
