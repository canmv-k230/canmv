/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "mphal.h"
#include "mpprint.h"
#include "py/runtime.h"
#include "py/obj.h"

#include "modmachine.h"

#include "machine_touch.h"

// User Utils /////////////////////////////////////////////////////////////////
typedef struct {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint8_t *buf;
} i2c_msg_t;

typedef struct {
    i2c_msg_t *msgs;
    size_t number;
} i2c_priv_data_t;

int machine_touch_user_wr(machine_touch_obj_t *self, uint16_t addr,
    uint8_t *send_buffer, uint32_t send_len, uint8_t *read_buffer, uint32_t read_len) {
#define RT_I2C_DEV_CTRL_RW (0x800 + 0x04)

    int msg_count = 0;

    i2c_msg_t msgs[2];
    i2c_priv_data_t priv;

    int fd = machine_i2c_obj_get_fd(self->user.py_obj.i2c);
    if(0 > fd) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("invalid i2c fd"));
    }

    if(send_buffer && send_len) {
        msgs[msg_count].addr = addr;
        msgs[msg_count].flags = 0x00; // RT_I2C_WR
        msgs[msg_count].buf = send_buffer;
        msgs[msg_count].len = send_len;

        msg_count++;
    }

    if(read_buffer && read_len) {
        msgs[msg_count].addr = addr;
        msgs[msg_count].flags = 0x01; // RT_I2C_RD
        msgs[msg_count].buf = read_buffer;
        msgs[msg_count].len = read_len;

        msg_count++;
    }

    priv.msgs = &msgs[0];
    priv.number = msg_count;

    if(0x00 != ioctl(fd, RT_I2C_DEV_CTRL_RW, &priv)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("i2c transfer failed"));
    }

    return 0;
}

void machine_touch_update_event(int finger_num, struct machine_touch_data *point) {
    static int last_finger_num = 0;
    static struct machine_touch_data last_point[TOUCH_POINT_NUMBER_MAX];
    static mp_uint_t last_timestamp = 0, tmp_timestamp;      // Track the timestamp of the last touch event
    static mp_uint_t last_click_timestamp[TOUCH_POINT_NUMBER_MAX] = {0}; // Track double-click timing

    uint16_t tmpx, tmpy;
    uint8_t new_session = 0;

    // Check if too much time has passed since the last touch event
    if (finger_num > 0 && (point[0].timestamp - last_timestamp > TOUCH_TIMEOUT_MS)) {
        // A new touch event is considered after a timeout
        new_session = 1;
        // printf("New touch session detected due to timeout.\n");

        // If needed, mark all previous touches as lifted
        for (int i = 0; i < last_finger_num; i++) {
            last_point[i].event = RT_TOUCH_EVENT_UP;
            // printf("Finger %d: Timeout - Finger lifted\n", last_point[i].track_id);
        }

        // Reset the last finger number since it's a new session
        last_finger_num = 0;
    }

    // Update the timestamp of the last touch event
    last_timestamp = point[0].timestamp;

    // Process current touch data
    for (int i = 0; i < finger_num; i++) {
        struct machine_touch_data *current_point = &point[i];
        struct machine_touch_data *last_touch = &last_point[i];

        tmp_timestamp = last_click_timestamp[i];
        // Update last click timestamp if it's a valid touch
        last_click_timestamp[i] = current_point->timestamp;

        tmpx = abs(current_point->x_coordinate - last_touch->x_coordinate);
        tmpy = abs(current_point->y_coordinate - last_touch->y_coordinate);
        // Double-click detection: Check if this touch is at the same position as the last one within the threshold
        if (i < last_finger_num &&
            !new_session && (tmpx < 5) && (tmpy < 5) &&
            (current_point->timestamp - tmp_timestamp) < 100) {

            // Double-click detected, ignore this event
            // printf("Double-click detected for finger %d, filtering out.\n", current_point->track_id);
            continue;
        }

        if (i < last_finger_num && !new_session) {
            // Check if the touch point has moved
            if (current_point->x_coordinate != last_touch->x_coordinate ||
                current_point->y_coordinate != last_touch->y_coordinate) {
                // Update event to move
                current_point->event = RT_TOUCH_EVENT_MOVE;
            }
        } else {
            // New finger detected or new session
            current_point->event = RT_TOUCH_EVENT_DOWN;
        }

        // Print touch info for debugging (optional)
        // printf("Finger %d: X = %d, Y = %d, Event = %d, Timestamp = %d\n",
        //        current_point->track_id,
        //        current_point->x_coordinate,
        //        current_point->y_coordinate,
        //        current_point->event,
        //        current_point->timestamp);

        // Update the last point data
        memcpy(last_touch, current_point, sizeof(struct machine_touch_data));
    }

    // Check if fingers have been lifted
    if (finger_num < last_finger_num && !new_session) {
        for (int i = finger_num; i < last_finger_num; i++) {
            struct machine_touch_data *last_touch = &last_point[i];
            // Finger lifted
            last_touch->event = RT_TOUCH_EVENT_UP;
            // printf("Finger %d: Lifted, Event = %d, Timestamp = %d\n",
            //        last_touch->track_id, last_touch->event, last_touch->timestamp);
        }
    }

    // Update last finger number
    last_finger_num = finger_num;
}
// Touch Info /////////////////////////////////////////////////////////////////
typedef struct {
    mp_obj_base_t base;
    struct machine_touch_data info;
} machine_touch_info_obj_t;

STATIC void machine_touch_info_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_touch_info_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "track_id: %d, event: %d, width: %d, x: %d, y: %d, timestamp: %d",
        self->info.track_id, self->info.event, self->info.width, self->info.x_coordinate,
        self->info.y_coordinate, self->info.timestamp);
}

STATIC void machine_touch_info_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    machine_touch_info_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        switch (attr) {
            case MP_QSTR_event:
                dest[0] = mp_obj_new_int(self->info.event);
                break;
            case MP_QSTR_track_id:
                dest[0] = mp_obj_new_int(self->info.track_id);
                break;
            case MP_QSTR_width:
                dest[0] = mp_obj_new_int(self->info.width);
                break;
            case MP_QSTR_x:
                dest[0] = mp_obj_new_int(self->info.x_coordinate);
                break;
            case MP_QSTR_y:
                dest[0] = mp_obj_new_int(self->info.y_coordinate);
                break;
            case MP_QSTR_timestamp:
                dest[0] = mp_obj_new_int(self->info.timestamp);
                break;
        }
    }
}

MP_DEFINE_CONST_OBJ_TYPE(
    machine_touch_info_type,
    MP_QSTR_TOUCH_INFO,
    MP_TYPE_FLAG_NONE,
    print, machine_touch_info_print,
    attr, machine_touch_info_attr
    );

// Touch //////////////////////////////////////////////////////////////////////
// Define argument specs, positional and keyword arguments with defaults
enum { ARG_dev, ARG_type, ARG_rotate, ARG_range_x, ARG_range_y, ARG_i2c, ARG_slave_addr, ARG_rst, ARG_int };
static const mp_arg_t machine_touch_allowed_args[] = {
    // both touch system and user
    { MP_QSTR_dev,      MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },   // required
    { MP_QSTR_type,     MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = TOUCH_TYPE_CST328} },  // optional
    { MP_QSTR_rotate,   MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },  // optional
    // just for touch user
    { MP_QSTR_range_x,  MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },  // optional
    { MP_QSTR_range_y,  MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },  // optional
    { MP_QSTR_i2c,      MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },  // optional
    { MP_QSTR_slave_addr, MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },  // optional
    { MP_QSTR_rst,      MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },  // optional
    { MP_QSTR_int,      MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },  // optional
};

// System /////////////////////////////////////////////////////////////////////
STATIC void machine_touch_system_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_touch_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_print_str(print, "TOUCH(dev=0");

    // Print type value
    mp_printf(print, ", type=%d", self->type);

    // Print rotate value
    mp_printf(print, ", rotate=%d", self->rotate);

    // Print range_x and range_y
    mp_printf(print, ", range_x=%u, range_y=%u", self->range_x, self->range_y);

    // Print fd
    mp_printf(print, ", fd=%d)", self->system.fd);
}

STATIC void machine_touch_system_init(machine_touch_obj_t *self, mp_arg_val_t *args_parsed) {
#define RT_TOUCH_CTRL_GET_INFO          (21 * 0x100 + 1)
#define RT_TOUCH_CTRL_RESET             (21 * 0x100 + 11)
#define RT_TOUCH_CTRL_GET_DFT_ROTATE    (21 * 0x100 + 12)
    struct rt_touch_info {
        uint8_t      type;                       /* The touch type */
        uint8_t      vendor;                     /* Vendor of touchs */
        uint8_t      point_num;                  /* Support point num */
        uint32_t     range_x;                    /* X coordinate range */
        uint32_t     range_y;                    /* Y coordinate range */
    } info;

    int fd = -1;
    const char dev_name[16] = "/dev/touch0";

    self->base.type = &machine_touch_type;
    self->dev = TOUCH_DEV_TYPE_SYSTEM;

    if (0 > (fd = open(dev_name, O_RDWR))) {
        mp_raise_OSError_with_filename(errno, dev_name);
    }

    if(0x00 != ioctl(fd, RT_TOUCH_CTRL_RESET, NULL)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("touch ioctl(reset) error"));
    }
    mp_hal_delay_ms(5);

    if(0x00 != ioctl(fd, RT_TOUCH_CTRL_GET_INFO, &info)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("touch ioctl(get_info) error"));
    }

    int rotate;
    if(0x00 != ioctl(fd, RT_TOUCH_CTRL_GET_DFT_ROTATE, &rotate)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("touch ioctl(get_rotate) error"));
    }

    if(-1 == self->rotate) {
        self->rotate = rotate;
    }
    self->range_x = info.range_x;
    self->range_y = info.range_y;

    self->system.fd = fd;
}

STATIC mp_obj_t machine_touch_system_deinit(mp_obj_t self_in) {
    machine_touch_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if(0 < self->system.fd) {
        close(self->system.fd);
        self->system.fd = -1;
    }

    return mp_const_none;
}

STATIC int machine_touch_system_read(machine_touch_obj_t *self, int *point_number, struct machine_touch_data *touch_data) {
    int result = 0;
    int req_point_number = *point_number;

    result = read(self->system.fd, touch_data, req_point_number * sizeof(struct machine_touch_data));

    if(0 > result) {
        return -1;
    }

    *point_number = result / sizeof(struct machine_touch_data);

    return 0;
}

// User ///////////////////////////////////////////////////////////////////////
STATIC void machine_touch_user_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_touch_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_print_str(print, "TOUCH(dev=1");

    // Print type value
    mp_printf(print, ", type=%d", self->type);

    // Print rotate value
    mp_printf(print, ", rotate=%d", self->rotate);

    // Print range_x and range_y
    mp_printf(print, ", range_x=%u, range_y=%u", self->range_x, self->range_y);

    // Print i2c object
    mp_print_str(print, ", i2c=");
    mp_obj_print_helper(print, self->user.py_obj.i2c, PRINT_REPR);

    // Print slv_addr object
    mp_printf(print, ", slave_addr=%u", self->user.slv_addr);

    // Print pin_rst object
    mp_print_str(print, ", pin_rst=");
    mp_obj_print_helper(print, self->user.py_obj.pin_rst, PRINT_REPR);

    // Print pin_int object
    mp_print_str(print, ", pin_int=");
    mp_obj_print_helper(print, self->user.py_obj.pin_int, PRINT_REPR);

    mp_print_str(print, ")");
}

STATIC void machine_touch_user_init(machine_touch_obj_t *self, mp_arg_val_t *args_parsed) {
    int type = self->type;

    self->dev = TOUCH_DEV_TYPE_USER;
    self->base.type = &machine_touch_user_type;

    if((args_parsed[ARG_i2c].u_obj != mp_const_none) && !mp_obj_is_type(args_parsed[ARG_i2c].u_obj, &machine_i2c_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("i2c must be a I2C object"));
    }
    self->user.py_obj.i2c = args_parsed[ARG_i2c].u_obj;

    if (args_parsed[ARG_rst].u_obj != mp_const_none && !mp_obj_is_type(args_parsed[ARG_rst].u_obj, &machine_pin_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("rst must be a Pin object"));
    }
    self->user.py_obj.pin_rst = args_parsed[ARG_rst].u_obj;

    if (args_parsed[ARG_int].u_obj != mp_const_none && !mp_obj_is_type(args_parsed[ARG_int].u_obj, &machine_pin_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("int must be a Pin object"));
    }
    self->user.py_obj.pin_int = args_parsed[ARG_int].u_obj;
    if(self->user.py_obj.pin_int != mp_const_none) {
        mp_printf(&mp_plat_print, "now, we do't support set int pin.");
    }

    self->user.slv_addr = 0xFF; // invalid slave address
    if(mp_const_none != args_parsed[ARG_slave_addr].u_obj) {
        self->user.slv_addr = mp_obj_get_int(args_parsed[ARG_slave_addr].u_obj);
    }

    if(mp_const_none != self->user.py_obj.pin_rst) {
        machine_pin_value_set(self->user.py_obj.pin_rst, 1);
        mp_hal_delay_ms(30);
        machine_pin_value_set(self->user.py_obj.pin_rst, 0);
        mp_hal_delay_ms(30);
        machine_pin_value_set(self->user.py_obj.pin_rst, 1);
    }

    switch(type) {
#if defined(CONFIG_ENABLE_TOUCH_DRIVER_CST_MUTCAP)
        case TOUCH_TYPE_CST128:
        case TOUCH_TYPE_CST328:
        case TOUCH_TYPE_CST226SE:
            self->ops = &machine_touch_user_cst3xx_ops;
            break;
#endif // CONFIG_ENABLE_TOUCH_DRIVER_CST_MUTCAP
#if defined(CONFIG_ENABLE_TOUCH_DRIVER_GT911)
        case TOUCH_TYPE_GT911:
            self->ops = &machine_touch_user_gt911_ops;
            break;
#endif // CONFIG_ENABLE_TOUCH_DRIVER_GT911
        default:
            mp_raise_TypeError(MP_ERROR_TEXT("Unsupport type"));
    }

    if(!self->ops || !self->ops->init) {
        mp_raise_TypeError(MP_ERROR_TEXT("Unsupport touch driver"));
    }

    self->ops->init(self);
}

STATIC mp_obj_t machine_touch_user_deinit(mp_obj_t self_in) {
    machine_touch_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if(!self->ops || !self->ops->deinit) {
        mp_raise_TypeError(MP_ERROR_TEXT("Unsupport touch driver"));
    }
    self->ops->deinit(self);

    return mp_const_none;
}

STATIC int machine_touch_user_read(machine_touch_obj_t *self, int *point_number, struct machine_touch_data *touch_data) {
    if(!self->ops || !self->ops->read) {
        mp_raise_TypeError(MP_ERROR_TEXT("Unsupport touch driver"));
    }

    return self->ops->read(self, point_number, touch_data);
}

// APIs ///////////////////////////////////////////////////////////////////////
static inline void rotate_touch_point(machine_touch_obj_t *self, struct machine_touch_data *tdata) {
    uint8_t rotate = self->rotate;
    uint32_t range_x = self->range_x, range_y = self->range_y;
    uint16_t point_x = tdata->x_coordinate, point_y = tdata->y_coordinate;

    switch (rotate) {
        default:
        case TOUCH_ROTATE_DEGREE_0:
            // do nothing
        break;
        case TOUCH_ROTATE_DEGREE_90:
            tdata->x_coordinate = range_y - point_y - 1;
            tdata->y_coordinate = point_x;
        break;
        case TOUCH_ROTATE_DEGREE_180:
            tdata->x_coordinate = range_x - point_x - 1;
            tdata->y_coordinate = range_y - point_y - 1;
        break;
        case TOUCH_ROTATE_DEGREE_270:
            tdata->x_coordinate = point_y;
            tdata->y_coordinate = range_x - point_x - 1;
        break;
        case TOUCH_ROTATE_SWAP_XY: {
            tdata->x_coordinate = point_y;
            tdata->y_coordinate = point_x;
        } break;
    }
}

STATIC mp_obj_t machine_touch_read(size_t n_args, const mp_obj_t *args) {
    machine_touch_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int point_number = 1, result = 0;
    struct machine_touch_data touch_data[TOUCH_POINT_NUMBER_MAX];
    machine_touch_info_obj_t *touch_info_obj[TOUCH_POINT_NUMBER_MAX];

    if (n_args > 1) {
        point_number = mp_obj_get_int(args[1]);

        if ((0 >= point_number) || (TOUCH_POINT_NUMBER_MAX < point_number)) {
            mp_raise_ValueError(MP_ERROR_TEXT("point_number invalid"));
        }
    }

    if(TOUCH_DEV_TYPE_SYSTEM == self->dev) {
        result = machine_touch_system_read(self, &point_number, &touch_data[0]);
    } else if(TOUCH_DEV_TYPE_USER == self->dev) {
        result = machine_touch_user_read(self, &point_number, &touch_data[0]);
    } else {
        return mp_obj_new_tuple(0, (mp_obj_t *)touch_info_obj);
    }

    if ((0x00 != result) || (TOUCH_POINT_NUMBER_MAX < point_number)) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("read touch failed, %d"), result);
    }

    for (int i = 0; i < point_number; i++) {
        rotate_touch_point(self, &touch_data[i]);

        touch_info_obj[i] = mp_obj_malloc(machine_touch_info_obj_t, &machine_touch_info_type);
        touch_info_obj[i]->info = touch_data[i];
    }

    return mp_obj_new_tuple(point_number, (mp_obj_t *)touch_info_obj);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_touch_read_obj, 1, 2, machine_touch_read);

STATIC mp_obj_t machine_touch_deinit(mp_obj_t self_in) {
    machine_touch_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if(TOUCH_DEV_TYPE_SYSTEM == self->dev) {
        return machine_touch_system_deinit(self_in);
    } else if(TOUCH_DEV_TYPE_USER == self->dev) {
        return machine_touch_user_deinit(self_in);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_touch_deinit_obj, machine_touch_deinit);

STATIC mp_obj_t machine_touch_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // Parse all arguments
    mp_arg_val_t args_parsed[MP_ARRAY_SIZE(machine_touch_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(machine_touch_allowed_args), machine_touch_allowed_args, args_parsed);

    // Create a new instance of the Touch object
    machine_touch_obj_t *self = m_new_obj(machine_touch_obj_t);

    // Extract the dev parameter
    mp_int_t dev = args_parsed[ARG_dev].u_int;

    self->dev = dev;
    self->type = args_parsed[ARG_type].u_int;
    self->rotate = args_parsed[ARG_rotate].u_int;
    self->range_x = args_parsed[ARG_range_x].u_int;
    self->range_y = args_parsed[ARG_range_y].u_int;

    if(TOUCH_DEV_TYPE_SYSTEM == dev) {
        machine_touch_system_init(self, args_parsed);
    } else if(TOUCH_DEV_TYPE_USER == dev) {
        machine_touch_user_init(self, args_parsed);
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Touch Only support dev=0 or dev=1"));
    }

    return MP_OBJ_FROM_PTR(self);
}

STATIC const mp_rom_map_elem_t machine_touch_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&machine_touch_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_touch_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&machine_touch_read_obj) },

    { MP_ROM_QSTR(MP_QSTR_EVENT_NONE), MP_ROM_INT(RT_TOUCH_EVENT_NONE) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_UP), MP_ROM_INT(RT_TOUCH_EVENT_UP) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_DOWN), MP_ROM_INT(RT_TOUCH_EVENT_DOWN) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_MOVE), MP_ROM_INT(RT_TOUCH_EVENT_MOVE) },

    { MP_ROM_QSTR(MP_QSTR_ROTATE_0), MP_ROM_INT(TOUCH_ROTATE_DEGREE_0) },
    { MP_ROM_QSTR(MP_QSTR_ROTATE_90), MP_ROM_INT(TOUCH_ROTATE_DEGREE_90) },
    { MP_ROM_QSTR(MP_QSTR_ROTATE_180), MP_ROM_INT(TOUCH_ROTATE_DEGREE_180) },
    { MP_ROM_QSTR(MP_QSTR_ROTATE_270), MP_ROM_INT(TOUCH_ROTATE_DEGREE_270) },
    { MP_ROM_QSTR(MP_QSTR_ROTATE_SWAP_XY), MP_ROM_INT(TOUCH_ROTATE_SWAP_XY) },

    // system touch driver type
    // { MP_ROM_QSTR(MP_QSTR_TYPE_CST128), MP_ROM_INT(TOUCH_TYPE_CST128) },
    // { MP_ROM_QSTR(MP_QSTR_TYPE_FT5x16), MP_ROM_INT(TOUCH_TYPE_FT5x16) },

    // user touch driver type
    { MP_ROM_QSTR(MP_QSTR_TYPE_CST226SE), MP_ROM_INT(TOUCH_TYPE_CST226SE) },
    { MP_ROM_QSTR(MP_QSTR_TYPE_CST328), MP_ROM_INT(TOUCH_TYPE_CST328) },

    { MP_ROM_QSTR(MP_QSTR_TYPE_GT911), MP_ROM_INT(TOUCH_TYPE_GT911) },
};
STATIC MP_DEFINE_CONST_DICT(machine_touch_locals_dict, machine_touch_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_touch_type,
    MP_QSTR_TOUCH,
    MP_TYPE_FLAG_NONE,
    make_new, machine_touch_make_new,
    print, machine_touch_system_print,
    locals_dict, &machine_touch_locals_dict
    );

MP_DEFINE_CONST_OBJ_TYPE(
    machine_touch_user_type,
    MP_QSTR_TOUCH_User,
    MP_TYPE_FLAG_NONE,
    make_new, machine_touch_make_new,
    print, machine_touch_user_print,
    locals_dict, &machine_touch_locals_dict
    );
