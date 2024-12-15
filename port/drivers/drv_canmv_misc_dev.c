#include "drv_canmv_misc_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "mpprint.h"
#include "py/obj.h"
#include "py/runtime.h"

int canmv_misc_dev_ioctl(int cmd, void *args) {
  int misc_dev_fd = -1;

  if (0 > (misc_dev_fd = open("/dev/canmv_misc", O_RDWR))) {
    mp_raise_msg_varg(&mp_type_RuntimeError,
                      MP_ERROR_TEXT("can not misc device"));
  }

  if (0x00 != ioctl(misc_dev_fd, cmd, args)) {
    close(misc_dev_fd);
    mp_printf(&mp_plat_print, "ioctl misc device failed, cmd %x\n", cmd);
    return -1;
  }

  if (0 <= misc_dev_fd) {
    close(misc_dev_fd);
  }
  return 0;
}
