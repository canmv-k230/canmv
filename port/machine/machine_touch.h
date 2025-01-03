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
#pragma once

#include <stdint.h>

#include "py/obj.h"

#define TOUCH_POINT_NUMBER_MAX 10
#define TOUCH_TIMEOUT_MS        1000

/* Touch event */
#define RT_TOUCH_EVENT_NONE              (0)   /* Touch none */
#define RT_TOUCH_EVENT_UP                (1)   /* Touch up event */
#define RT_TOUCH_EVENT_DOWN              (2)   /* Touch down event */
#define RT_TOUCH_EVENT_MOVE              (3)   /* Touch move event */

#define TOUCH_ROTATE_DEGREE_0           (0)
#define TOUCH_ROTATE_DEGREE_90          (1)
#define TOUCH_ROTATE_DEGREE_180         (2)
#define TOUCH_ROTATE_DEGREE_270         (3)
#define TOUCH_ROTATE_SWAP_XY            (4)

#define TOUCH_DEV_TYPE_SYSTEM           (0x00)
#define TOUCH_DEV_TYPE_USER             (0x01)

#define TOUCH_TYPE_FT5x16               (0x01)
#define TOUCH_TYPE_GT911                (0x02)

#define TOUCH_TYPE_BASE_CST_SEL_CAP     (0x100)
#define TOUCH_TYPE_BASE_CST_MUT_CAP     (0x200)

#define TOUCH_TYPE_CST128               (TOUCH_TYPE_BASE_CST_MUT_CAP | 0x01)
#define TOUCH_TYPE_CST328               (TOUCH_TYPE_BASE_CST_MUT_CAP | 0x02)
#define TOUCH_TYPE_CST226SE             (TOUCH_TYPE_BASE_CST_MUT_CAP | 0x03)

struct machine_touch_data {
    uint8_t event;          /* The touch event of the data */
    uint8_t track_id;       /* Track id of point */
    uint8_t width;          /* Point of width */
    uint16_t x_coordinate;  /* Point of x coordinate */
    uint16_t y_coordinate;  /* Point of y coordinate */
    uint32_t timestamp;     /* The timestamp when the data was received */
};

struct _machine_touch_obj_t;

struct machine_touch_drv_ops {
    void (*init)(struct _machine_touch_obj_t *self);
    void (*deinit)(struct _machine_touch_obj_t *self);
    int (*read)(struct _machine_touch_obj_t *self, int *point_number, struct machine_touch_data *touch_data);
};

typedef struct _machine_touch_obj_t {
    mp_obj_base_t base;

    int type;
    int dev; // 0: system, 1: user
    int rotate;
    uint32_t range_x, range_y;

    union {
        struct {
            int fd;
        } system;

        struct {
            struct {
                mp_obj_t i2c;
                mp_obj_t pin_rst;
                mp_obj_t pin_int; // now, we can't use int in userspace
            } py_obj;

            uint8_t slv_addr;
        } user;
    };

    const struct machine_touch_drv_ops *ops;
} machine_touch_obj_t;

int machine_touch_user_wr(machine_touch_obj_t *self, uint16_t addr,
    uint8_t *send_buffer, uint32_t send_len, uint8_t *read_buffer, uint32_t read_len);

void machine_touch_update_event(int finger_num, struct machine_touch_data *point);

extern struct machine_touch_drv_ops machine_touch_user_cst3xx_ops;
extern struct machine_touch_drv_ops machine_touch_user_gt911_ops;
