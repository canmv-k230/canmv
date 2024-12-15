#pragma once

#include <stdint.h>
#include <stdlib.h>

#define MISC_DEV_CMD_READ_HEAP          (0x1024 + 0)
#define MISC_DEV_CMD_READ_PAGE          (0x1024 + 1)
#define MISC_DEV_CMD_GET_MEMORY_SIZE    (0x1024 + 2)
#define MISC_DEV_CMD_CPU_USAGE          (0x1024 + 3)
#define MISC_DEV_CMD_CREATE_SOFT_I2C    (0x1024 + 4)
#define MISC_DEV_CMD_DELETE_SOFT_I2C    (0x1024 + 5)
#define MISC_DEV_CMD_GET_FS_STAT        (0x1024 + 6)
#define MISC_DEV_CMD_NTP_SYNC           (0x1024 + 7)
#define MISC_DEV_CMD_GET_UTC_TIMESTAMP  (0x1024 + 8)
#define MISC_DEV_CMD_SET_UTC_TIMESTAMP  (0x1024 + 9)
#define MISC_DEV_CMD_GET_LOCAL_TIME     (0x1024 + 10)
#define MISC_DEV_CMD_SET_TIMEZONE       (0x1024 + 11)
#define MISC_DEV_CMD_GET_TIMEZONE       (0x1024 + 12)

// MISC_DEV_CMD_GET_FS_STAT
#define FS_STAT_PATH_LENGTH 32

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// MISC_DEV_CMD_READ_HEAP
// MISC_DEV_CMD_READ_PAGE
struct canmv_misc_dev_meminfo_t {
  size_t total_size;
  size_t free_size;
  size_t used_size;
};

// MISC_DEV_CMD_CREATE_SOFT_I2C
struct soft_i2c_configure {
  uint32_t bus_num;
  uint32_t pin_scl;
  uint32_t pin_sda;
  uint32_t freq;
  uint32_t timeout_ms;
};

// MISC_DEV_CMD_GET_FS_STAT
struct dfs_statfs {
    size_t f_bsize;   /* block size */
    size_t f_blocks;  /* total data blocks in file system */
    size_t f_bfree;   /* free blocks in file system */
};

// MISC_DEV_CMD_GET_FS_STAT
struct statfs_wrap {
    char path[FS_STAT_PATH_LENGTH];
    struct dfs_statfs sb;
};

int canmv_misc_dev_ioctl(int cmd, void *args);

#ifdef __cplusplus
}
#endif /* __cplusplus */
