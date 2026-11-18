/* fw_version.c */
#include "version.h"

_Static_assert(sizeof(FW_VERSION_STR) <= FW_VER_STR_LEN,
               "version string too long");
_Static_assert(FW_VER_MAJOR <= 255 && FW_VER_MINOR <= 255,
               "version number exceeds 1 byte");

__attribute__((section(".fw_version_str"), used, aligned(4)))
const char g_fw_version_str[FW_VER_STR_LEN] = FW_VERSION_STR;

__attribute__((section(".fw_version_num"), used, aligned(2)))
const fw_ver_num_t g_fw_version_num = {
    .major = FW_VER_MAJOR,
    .minor = FW_VER_MINOR,
};

