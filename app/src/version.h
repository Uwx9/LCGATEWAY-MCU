/* fw_version.h */
#ifndef FW_VERSION_H
#define FW_VERSION_H

#include <stdint.h>

#define FW_VER_STR_LEN 32

#ifndef FW_VER_MAJOR
#define FW_VER_MAJOR 0
#endif
#ifndef FW_VER_MINOR
#define FW_VER_MINOR 0
#endif
#ifndef FW_VERSION_STR
#define FW_VERSION_STR "version0.0"
#endif

typedef struct {
    uint8_t major;
    uint8_t minor;
} fw_ver_num_t;

extern const char g_fw_version_str[FW_VER_STR_LEN];
extern const fw_ver_num_t g_fw_version_num;

#endif

