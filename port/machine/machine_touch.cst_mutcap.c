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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mphal.h"
#include "mpprint.h"
#include "py/runtime.h"

#include "machine_touch.h"

struct cst3xx_point {
    uint8_t id_stat;
    uint8_t xh;
    uint8_t yh;
    uint8_t xl_yl;
    uint8_t z;
};

struct cst3xx_reg {
    struct cst3xx_point point1;

    uint8_t point_num;
    uint8_t const_0xab;

    struct cst3xx_point point2_5[4];
};

STATIC void machine_touch_user_init_cst3xx(machine_touch_obj_t *self) {
    uint8_t cmd[4];
    uint8_t data[24];
    uint8_t slv_addr;

    int type = self->type;

    if(0xFF == self->user.slv_addr) {
        if(TOUCH_TYPE_CST328 == type) {
            self->user.slv_addr = 0x1A;
        } else if(TOUCH_TYPE_CST226SE == type) {
            self->user.slv_addr = 0x5A;
        } else {
            mp_raise_TypeError(MP_ERROR_TEXT("Unsupport type"));
        }
    }
    slv_addr = self->user.slv_addr;

    if(-1 == self->rotate) {
        self->rotate = TOUCH_ROTATE_DEGREE_0;

        if(TOUCH_TYPE_CST328 == type) {
            self->rotate = TOUCH_ROTATE_SWAP_XY;
        } else if(TOUCH_TYPE_CST226SE == type) {

        }
    }

    // HYN_REG_MUT_DEBUG_INFO_MODE
    cmd[0] = 0xd1;
    cmd[1] = 0x01;
    cmd[2] = 0x01;
    if(0x00 != machine_touch_user_wr(self, slv_addr, cmd, 3, NULL, 0)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("init cst3xx failed 1"));
    }
    mp_hal_delay_ms(1);

    // HYN_REG_MUT_DEBUG_INFO_FW_VERSION
    cmd[0] = 0xd2;
    cmd[1] = 0x04;
    if(0x00 != machine_touch_user_wr(self, slv_addr, cmd, 2, data, 4)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("init cst3xx failed 2"));
    }
    uint16_t chip, proj;
    proj = (data[0] << 8) | data[1];
    chip = (data[2] << 8) | data[3];
    mp_printf(&mp_plat_print, "CST3xx, ChipID: 0x%02X, ProjectID: 0x%02X\n", chip, proj);

    // HYN_REG_MUT_NORMAL_MODE 
    cmd[0] = 0xd1;
    cmd[1] = 0x09;
    if(0x00 != machine_touch_user_wr(self, slv_addr, cmd, 2, NULL, 0)) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("init cst3xx failed 3"));
    }
}

STATIC void machine_touch_user_deinit_cst3xx(machine_touch_obj_t *self) {
    // do nothings.
    (void)self;

    return;
}

STATIC int machine_touch_user_read_cst3xx(machine_touch_obj_t *self, int *point_number, struct machine_touch_data *touch_data) {
    struct cst3xx_reg reg;

    uint8_t temp, cmd[4];

    int pt_num = 0;
    mp_uint_t tick = 0;

    uint8_t slv_addr = self->user.slv_addr;

    cmd[0] = 0xD0;
    cmd[1] = 0x05;
    machine_touch_user_wr(self, slv_addr, (uint8_t *)&cmd[0], 2, &temp, 1);
    pt_num = (temp & 0x0F);

    if((0x00 == pt_num) || (5 < pt_num)) {
        cmd[0] = 0xD0;
        cmd[1] = 0x05;
        cmd[2] = 0x00;
        machine_touch_user_wr(self, slv_addr, (uint8_t *)&cmd[0], 3, NULL, 0);

        *point_number = 0;
        return 0;
    }

    cmd[0] = 0xD0;
    cmd[1] = 0x00;
    machine_touch_user_wr(self, slv_addr, (uint8_t *)&cmd[0], 2, (uint8_t *)&reg, pt_num * sizeof(struct cst3xx_point) + 2);

    cmd[0] = 0xD0;
    cmd[1] = 0x05;
    cmd[2] = 0x00;
    machine_touch_user_wr(self, slv_addr, (uint8_t *)&cmd[0], 3, NULL, 0);

    if ((0xAB != reg.const_0xab) || (0x80 == (reg.point_num & 0x80)/* report key */)) {
        *point_number = 0;
        return 0;
    }

    if(0x00 != (pt_num = reg.point_num & 0x7F)) {
        if(pt_num > *point_number) {
            pt_num = *point_number;
        }
        *point_number = pt_num;

        tick = mp_hal_ticks_ms();

        temp = reg.point1.id_stat;
        touch_data[0].event = (temp & 0x0F) == 0x06 ? RT_TOUCH_EVENT_DOWN : RT_TOUCH_EVENT_NONE;
        touch_data[0].track_id = (temp & 0xF0) >> 4;
        touch_data[0].width = reg.point1.z;
        touch_data[0].x_coordinate = (reg.point1.xh << 4) | (reg.point1.xl_yl & 0xF0) >> 4;
        touch_data[0].y_coordinate = (reg.point1.yh << 4) | (reg.point1.xl_yl & 0x0F);
        touch_data[0].timestamp = tick;

        if(pt_num > 1) {
            for(int i = 1; i < pt_num; i++) {
                struct cst3xx_point *point = &reg.point2_5[i - 1];

                temp = point->id_stat;
                touch_data[i].event = (temp & 0x0F) == 0x06 ? RT_TOUCH_EVENT_DOWN : RT_TOUCH_EVENT_NONE;
                touch_data[i].track_id = (temp & 0xF0) >> 4;
                touch_data[i].width = point->z;
                touch_data[i].x_coordinate = (point->xh << 4) | (point->xl_yl & 0xF0) >> 4;
                touch_data[i].y_coordinate = (point->yh << 4) | (point->xl_yl & 0x0F);
                touch_data[i].timestamp = tick;
            }
        }
    }

    return 0;
}

struct machine_touch_drv_ops machine_touch_user_cst3xx_ops = {
    .init = machine_touch_user_init_cst3xx,
    .deinit = machine_touch_user_deinit_cst3xx,
    .read = machine_touch_user_read_cst3xx,
};
