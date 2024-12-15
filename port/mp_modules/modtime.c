/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2017 Paul Sokolovsky
 * Copyright (c) 2014-2023 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "mpprint.h"
#include "obj.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py_clock.h"

#include "shared/timeutils/timeutils.h"

#include "canmv_drivers.h"

#include "generated/autoconf.h"

///////////////////////////////////////////////////////////////////////////////
// mp standard ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// Return the localtime as an 8-tuple.
STATIC mp_obj_t mp_time_localtime_get(void) {
    struct tm _tm;

    if(0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_LOCAL_TIME, &_tm)) {
        mp_printf(&mp_plat_print, "time get localtime failed.\n");
    }

    mp_obj_t tuple[8] = {
        tuple[0] = mp_obj_new_int(_tm.tm_year + 1900),
        tuple[1] = mp_obj_new_int(_tm.tm_mon + 1),
        tuple[2] = mp_obj_new_int(_tm.tm_mday),
        tuple[3] = mp_obj_new_int(_tm.tm_hour),
        tuple[4] = mp_obj_new_int(_tm.tm_min),
        tuple[5] = mp_obj_new_int(_tm.tm_sec),
        tuple[6] = mp_obj_new_int(_tm.tm_wday),
        tuple[7] = mp_obj_new_int(_tm.tm_yday),
    };
    return mp_obj_new_tuple(8, tuple);
}

// Return the number of seconds since the Epoch.
STATIC mp_obj_t mp_time_time_get(void) {
    time_t tm;

    if(0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_UTC_TIMESTAMP, &tm)) {
        tm = 0;
        mp_printf(&mp_plat_print, "rtc get timestamp failed 1.\n");
    }

    return mp_obj_new_int(tm);
}
///////////////////////////////////////////////////////////////////////////////
// k230 add ///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
STATIC mp_obj_t mod_time_ntp_sync(void) {
    int result = -1;

    mp_printf(&mp_plat_print, "please use machine.RTC.ntp_sync() instead time.ntp_sync()\n");

    if(0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_NTP_SYNC, &result)) {
        result = -1;

        mp_printf(&mp_plat_print, "time ntp sync failed 1.\n");
    }

    return mp_obj_new_bool(0x00 < result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_time_ntp_sync_obj, mod_time_ntp_sync);

#define MICROPY_PY_TIME_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_clock), MP_ROM_PTR(&py_clock_type) }, \
    { MP_ROM_QSTR(MP_QSTR_ntp_sync), MP_ROM_PTR(&mod_time_ntp_sync_obj) },
