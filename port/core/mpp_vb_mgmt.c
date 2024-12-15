#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "mpp_vb_mgmt.h"

#include "mpi_vicap_api.h"
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#define VB_MGMT_RECORD_MAX_CNT (128)
#define VB_MGMT_RECORD_MAGIC_IN_USE (0x1234ABCD)

struct vb_record_t
{
    void *virt_addr; // self.virt_addr = virt_addr
    k_u32 magic;
    k_u32 size;             // self.size = size, user set
    k_vb_blk_handle handle; // self.handle = handle
};

static struct vb_record_t vb_records[VB_MGMT_RECORD_MAX_CNT];
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
/* vicap mgmt */
static k_u8 vicap_dev_stat[VICAP_MAX_DEV_NUMS];

static k_s32 vb_mgmt_deinit_vicap(k_u32 id)
{
    k_s32 ret = 0;

    if (0x00 == vicap_dev_stat[id])
    {
        return 0;
    }

    if (0x00 != kd_mpi_vicap_stop_stream(id))
    {
        ret += 1;
        printf("vb_mgmt stop vicap %d stream failed.\n", id);
    }

    if (0x00 != kd_mpi_vicap_deinit(id))
    {
        ret += 1;
        printf("vb_mgmt stop vicap %d failed.\n", id);
    }

    vicap_dev_stat[id] = 0;

    return ret;
}

k_s32 vb_mgmt_vicap_dev_inited(k_u32 id)
{
    if (id > VICAP_MAX_DEV_NUMS)
    {
        return 1;
    }

    vicap_dev_stat[id] = 1;

    return 0;
}

k_s32 vb_mgmt_vicap_dev_deinited(k_u32 id)
{
    if (id > VICAP_MAX_DEV_NUMS)
    {
        return 1;
    }

    vicap_dev_stat[id] = 0;

    return 0;
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
/* vb mgmt */
static struct vb_record_t *vb_mgmt_get_block_record_by_info(vb_block_info *info)
{
    struct vb_record_t *record = (void *)0;

    for (int i = 0; i < VB_MGMT_RECORD_MAX_CNT; i++)
    {
        if (VB_MGMT_RECORD_MAGIC_IN_USE == vb_records[i].magic)
        {
            record = &vb_records[i];

            if (record->handle == info->handle)
            {
                if (record->size == info->size)
                {
                    if (record->virt_addr == info->virt_addr)
                    {
                        return record;
                    }
                }
            }
        }
    }

    return (void *)0;
}

static k_s32 vb_mgmt_push_block_to_record(k_u32 index, vb_block_info *info)
{
    if (VB_MGMT_RECORD_MAX_CNT <= index)
    {
        printf("invaild index\n");
        return -1;
    }

    struct vb_record_t *record = &vb_records[index];

    if (VB_MGMT_RECORD_MAGIC_IN_USE == record->magic)
    {
        printf("this index %d is in use\n", index);
        return -2;
    }

    record->handle = info->handle;
    record->size = info->size;
    record->virt_addr = info->virt_addr;

    record->magic = VB_MGMT_RECORD_MAGIC_IN_USE;

    return 0;
}

static k_s32 vb_mgmt_pop_block_from_record(struct vb_record_t *record)
{
    k_s32 ret = 0;

    if (VB_MGMT_RECORD_MAGIC_IN_USE != record->magic)
    {
        return -1;
    }

    if (0x00 != (ret += kd_mpi_vb_release_block(record->handle)))
    {
        printf("vb_mgmt release block failed at pop record, %d\n", record->handle);
    }

    if (0x00 != (ret += kd_mpi_sys_munmap(record->virt_addr, record->size)))
    {
        printf("vb_mgmt umap block failed at pop record, %p, %d\n", record->virt_addr, record->size);
    }

    record->magic = 0;

    return ret;
}

k_s32 vb_mgmt_get_block(vb_block_info *info)
{
    k_u32 record_index = 0xFFFFFFFF;

    if ((void *)0 == info)
    {
        return 1;
    }

    for (int i = 0; i < VB_MGMT_RECORD_MAX_CNT; i++)
    {
        if (VB_MGMT_RECORD_MAGIC_IN_USE != vb_records[i].magic)
        {
            record_index = i;
            break;
        }
    }

    if (0xFFFFFFFF == record_index)
    {
        printf("there is no free record\n");
        return 6;
    }

    info->handle = kd_mpi_vb_get_block(info->pool_id, info->size, (void *)0);
    if (VB_INVALID_HANDLE == info->handle)
    {
        return 2;
    }

    info->pool_id = kd_mpi_vb_handle_to_pool_id(info->handle);
    if (VB_INVALID_POOLID == info->pool_id)
    {
        kd_mpi_vb_release_block(info->handle);
        return 3;
    }

    info->phys_addr = kd_mpi_vb_handle_to_phyaddr(info->handle);
    if (0x00 == info->phys_addr)
    {
        kd_mpi_vb_release_block(info->handle);
        return 4;
    }

    info->virt_addr = kd_mpi_sys_mmap(info->phys_addr, info->size);
    if (0x00 == info->virt_addr)
    {
        kd_mpi_vb_release_block(info->handle);
        return 5;
    }

    if (0x00 != vb_mgmt_push_block_to_record(record_index, info))
    {
        printf("push vb_block_info to record failed\n");

        kd_mpi_vb_release_block(info->handle);

        return 6;
    }

    return 0;
}

k_s32 vb_mgmt_put_block(vb_block_info *info)
{
    struct vb_record_t *record = (void *)0;

    if ((void *)0 == info)
    {
        return 1;
    }

    if ((void *)0 == (record = vb_mgmt_get_block_record_by_info(info)))
    {
        return 2;
    }

    if (0x00 != vb_mgmt_pop_block_from_record(record))
    {
        printf("vb_mgmt pop record failed");
        return 3;
    }

    return 0;
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
/* mpp link mgmt */
#define VB_MGMT_MPP_LINK_MAX_CNT (64)
#define VB_MGMT_MPP_LINK_MAGIC_IN_USE (0x5678DEF0)

struct mpp_link_info_t
{
    k_u32 magic;

    k_mpp_chn src;
    k_mpp_chn dst;
};

static struct mpp_link_info_t vb_mpp_links_records[VB_MGMT_MPP_LINK_MAX_CNT];

static k_s32 vb_mgmt_push_mpp_link_to_record(k_u32 index, k_mpp_chn *src, k_mpp_chn *dst)
{
    struct mpp_link_info_t *info = (void *)0;

    if (VB_MGMT_MPP_LINK_MAX_CNT <= index)
    {
        printf("invaild index\n");
        return -1;
    }

    info = &vb_mpp_links_records[index];
    if (VB_MGMT_MPP_LINK_MAGIC_IN_USE == info->magic)
    {
        printf("this record is in use %d\n", index);
        return -2;
    }

    memcpy(&info->src, src, sizeof(k_mpp_chn));
    memcpy(&info->dst, dst, sizeof(k_mpp_chn));

    info->magic = VB_MGMT_MPP_LINK_MAGIC_IN_USE;

    return 0;
}

static k_bool is_mpp_chn_eq(k_mpp_chn *a, k_mpp_chn *b)
{
    if (a->mod_id != b->mod_id)
    {
        return K_FALSE;
    }
    if (a->dev_id != b->dev_id)
    {
        return K_FALSE;
    }
    if (a->chn_id != b->chn_id)
    {
        return K_FALSE;
    }

    return K_TRUE;
}

static k_s32 vb_mgmt_pop_mpp_link_from_record(k_mpp_chn *src, k_mpp_chn *dst)
{
    struct mpp_link_info_t *info = (void *)0;

    for (int i = 0; i < VB_MGMT_MPP_LINK_MAX_CNT; i++)
    {
        info = &vb_mpp_links_records[i];

        if (VB_MGMT_MPP_LINK_MAGIC_IN_USE == info->magic)
        {
            if (K_FALSE == is_mpp_chn_eq(&info->src, src))
            {
                continue;
            }

            if (K_FALSE == is_mpp_chn_eq(&info->dst, dst))
            {
                continue;
            }

            if (0x0 != kd_mpi_sys_unbind(src, dst))
            {
                printf("unbind link failed.\n");
            }

            info->magic = 0;
        }
    }

    return 0;
}

k_s32 vb_mgmt_push_link_info(k_mpp_chn *src, k_mpp_chn *dst)
{
    k_u32 index = 0xFFFFFFFF;

    for (int i = 0; i < VB_MGMT_MPP_LINK_MAX_CNT; i++)
    {
        if (VB_MGMT_MPP_LINK_MAGIC_IN_USE != vb_mpp_links_records[i].magic)
        {
            index = i;
            break;
        }
    }

    if (0xFFFFFFFF == index)
    {
        printf("no vaild record for mpp link\n");
        return 1;
    }

    return vb_mgmt_push_mpp_link_to_record(index, src, dst);
}

k_s32 vb_mgmt_pop_link_info(k_mpp_chn *src, k_mpp_chn *dst)
{
    return vb_mgmt_pop_mpp_link_from_record(src, dst);
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// #define VB_MGMT_PY_AT_EXIT_RECORD_MAX_CNT (64)
// #define VB_MGMT_PY_AT_EXIT_RECORD_IN_USE (0x5693AEBD)

// struct vb_py_at_exit_record_t
// {
//     mp_obj_t func;
//     mp_obj_t arg;
//     int magic;
//     int prio;
// };

// static struct vb_py_at_exit_record_t vb_py_at_exit_records[VB_MGMT_PY_AT_EXIT_RECORD_MAX_CNT];

// mp_obj_t vb_mgmt_py_reg_exit(mp_obj_t func, mp_obj_t arg, mp_obj_t prio)
// {
//     struct vb_py_at_exit_record_t *record = NULL;

//     for (int i = 0; i < VB_MGMT_PY_AT_EXIT_RECORD_MAX_CNT; i++)
//     {
//         record = &vb_py_at_exit_records[i];

//         if (VB_MGMT_PY_AT_EXIT_RECORD_IN_USE != record->magic)
//         {
//             record->func = func;
//             record->arg = arg;
//             record->magic = VB_MGMT_PY_AT_EXIT_RECORD_IN_USE;
//             record->prio = mp_obj_get_int(prio);

//             return mp_const_true;
//         }
//     }

//     return mp_const_false;
// }

// mp_obj_t vb_mgmt_py_unreg_exit(mp_obj_t func)
// {
//     struct vb_py_at_exit_record_t *record = NULL;

//     for (int i = 0; i < VB_MGMT_PY_AT_EXIT_RECORD_MAX_CNT; i++)
//     {
//         record = &vb_py_at_exit_records[i];

//         if ((VB_MGMT_PY_AT_EXIT_RECORD_IN_USE == record->magic) && (mp_obj_equal(record->func, func)))
//         {
//             record->magic = 0;
//             record->func = mp_const_none;
//             record->arg = mp_const_none;
//             record->prio = 1024;

//             return mp_const_true;
//         }
//     }

//     return mp_const_false;
// }

// static int compare_prio(const void *a, const void *b)
// {
//     const struct vb_py_at_exit_record_t *record_a = (const struct vb_py_at_exit_record_t *)a;
//     const struct vb_py_at_exit_record_t *record_b = (const struct vb_py_at_exit_record_t *)b;

//     return record_a->prio - record_b->prio;
// }

// void vb_mgmt_py_at_exit(void)
// {
//     struct vb_py_at_exit_record_t *record = NULL;

//     qsort(vb_py_at_exit_records, VB_MGMT_PY_AT_EXIT_RECORD_MAX_CNT, sizeof(struct vb_py_at_exit_record_t), compare_prio);

//     for (int i = 0; i < VB_MGMT_PY_AT_EXIT_RECORD_MAX_CNT; i++)
//     {
//         record = &vb_py_at_exit_records[i];

//         if (VB_MGMT_PY_AT_EXIT_RECORD_IN_USE == record->magic)
//         {
//             if (mp_obj_is_callable(record->func))
//             {
//                 if (mp_obj_equal(record->arg, mp_const_none))
//                 {
//                     mp_call_function_0(record->func);
//                 }
//                 else
//                 {
//                     mp_call_function_1(record->func, record->arg);
//                 }
//             }
//             else
//             {
//                 printf("Warning, not callable.\n");
//             }
//         }

//         record->magic = 0;
//         record->func = mp_const_none;
//         record->arg = mp_const_none;
//         record->prio = 1024;
//     }
// }
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#include "mpi_vo_api.h"

static int vo_video_layer_used[K_MAX_VO_LAYER_NUM] = {0};
static int vo_osd_layer_used[K_MAX_VO_OSD_NUM] = {0};

k_s32 vb_mgmt_enable_video_layer(k_u32 layer)
{
    if(K_MAX_VO_LAYER_NUM <= layer) {
        printf("%s invalid layer.\n", __func__);

        return -1;
    }
    vo_video_layer_used[layer] = 1;

    return kd_mpi_vo_enable_video_layer(layer);
}

k_s32 vb_mgmt_disable_video_layer(k_u32 layer)
{
    if(K_MAX_VO_LAYER_NUM <= layer) {
        printf("%s invalid layer.\n", __func__);

        return -1;
    }
    vo_video_layer_used[layer] = 0;

    return kd_mpi_vo_disable_video_layer(layer);
}

k_s32 vb_mgmt_enable_osd_layer(k_u32 layer)
{
    if(K_MAX_VO_OSD_NUM <= layer) {
        printf("%s invalid layer.\n", __func__);

        return -1;
    }
    vo_osd_layer_used[layer] = 1;

    return kd_mpi_vo_osd_enable(layer);
}

k_s32 vb_mgmt_disable_osd_layer(k_u32 layer)
{
    if(K_MAX_VO_OSD_NUM <= layer) {
        printf("%s invalid layer.\n", __func__);

        return -1;
    }
    vo_osd_layer_used[layer] = 0;

    return kd_mpi_vo_osd_disable(layer);
}

static void vb_mgmt_disable_vo_layers(void)
{
    for(size_t i = 0; i < (sizeof(vo_video_layer_used) / sizeof(vo_video_layer_used[0])); i++)
    {
        if(0x00 != vo_video_layer_used[i])
        {
            kd_mpi_vo_disable_video_layer(i);
        }
    }

    for(size_t i = 0; i < (sizeof(vo_osd_layer_used) / sizeof(vo_osd_layer_used[0])); i++)
    {
        if(0x00 != vo_osd_layer_used[i])
        {
            kd_mpi_vo_osd_disable(i);
        }
    }
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
/* mpp link mgmt */
#define VB_MGMT_VICAP_IMAGE_MAX_CNT (32)
#define VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE (0x1234DEAD)

static vb_mgmt_vicap_image vicap_images[VB_MGMT_VICAP_IMAGE_MAX_CNT];

k_s32 vb_mgmt_dump_vicap_frame(vb_mgmt_dump_vicap_config *cfg, vb_mgmt_vicap_image *image)
{
    vb_mgmt_vicap_image *_image = NULL;

    if((NULL == cfg) || (NULL == image)) {
        return 1;
    }

    for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++)
    {
        if (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE != vicap_images[i].magic)
        {
            _image = &vicap_images[i];

            _image->magic = VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE;

            break;
        }
    }

    if(NULL == _image) {
        printf("no space to record vicap_image\n");

        return 2;
    }

    memcpy(&_image->cfg, cfg, sizeof(*cfg));

    if(0x00 != kd_mpi_vicap_dump_frame(cfg->dev_num, cfg->chn_num, cfg->foramt, &_image->vf_info, cfg->milli_sec)) {
        printf("vicap dump dev %u chn %u failed.\n", cfg->dev_num, cfg->chn_num);

        _image->magic = 0x00;

        return 3;
    }

    k_u32 img_width = _image->vf_info.v_frame.width;
    k_u32 img_height = _image->vf_info.v_frame.height;

    switch (_image->vf_info.v_frame.pixel_format)
    {
        case PIXEL_FORMAT_YUV_SEMIPLANAR_420:
        case PIXEL_FORMAT_YVU_SEMIPLANAR_420:
        case PIXEL_FORMAT_YVU_PLANAR_420:
            _image->image_size = img_width * img_height * 3 / 2;
            break;

        case PIXEL_FORMAT_RGB_888:
        case PIXEL_FORMAT_BGR_888:
        case PIXEL_FORMAT_YUV_PACKAGE_444:
        case PIXEL_FORMAT_BGR_888_PLANAR:
        case PIXEL_FORMAT_RGB_888_PLANAR:
        case PIXEL_FORMAT_YVU_PLANAR_444:
            _image->image_size = img_width * img_height * 3;
            break;

        case PIXEL_FORMAT_RGB_565:
        case PIXEL_FORMAT_BGR_565:
        case PIXEL_FORMAT_RGB_565_LE:
        case PIXEL_FORMAT_BGR_565_LE:
        case PIXEL_FORMAT_ARGB_1555:
        case PIXEL_FORMAT_ARGB_4444:
        case PIXEL_FORMAT_ABGR_1555:
        case PIXEL_FORMAT_ABGR_4444:
            _image->image_size = img_width * img_height * 2;
            break;

        case PIXEL_FORMAT_ABGR_8888:
        case PIXEL_FORMAT_BGRA_8888:
            _image->image_size = img_width * img_height * 4;
            break;

        default:
        {
            printf("unsupport image format %u\n", _image->vf_info.v_frame.pixel_format);

            kd_mpi_vicap_dump_release(_image->cfg.dev_num, _image->cfg.chn_num, &_image->vf_info);
            _image->magic = 0x00;

            return 4;
        }
    }

    _image->vf_info.v_frame.virt_addr[0] = (k_u64)kd_mpi_sys_mmap_cached(_image->vf_info.v_frame.phys_addr[0], _image->image_size);

    if(0x00 == _image->vf_info.v_frame.virt_addr[0])
    {
        printf("mmap failed.\n");

        kd_mpi_vicap_dump_release(_image->cfg.dev_num, _image->cfg.chn_num, &_image->vf_info);
        _image->magic = 0x00;

        return 5;
    }

    memcpy(image, _image, sizeof(*_image));

    return 0;
}

k_s32 vb_mgmt_release_vicap_frame(vb_mgmt_vicap_image *image)
{
    k_s32 ret = 0;

    if((NULL == image) || (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE != image->magic))
    {
        return 1;
    }

    if(0x00 != (ret += kd_mpi_sys_munmap((void *)image->vf_info.v_frame.virt_addr[0], image->image_size)))
    {
        printf("release image failed(1).\n");
    }

    if(0x00 != (ret += kd_mpi_vicap_dump_release(image->cfg.dev_num, image->cfg.chn_num, &image->vf_info)))
    {
        printf("release image failed(2).\n");
    }

    if(0x00 == ret)
    {
        for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++)
        {
            if ((VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE == vicap_images[i].magic) && \
                (image->vf_info.v_frame.phys_addr[0] == vicap_images[i].vf_info.v_frame.phys_addr[0]) && \
                (image->vf_info.v_frame.virt_addr[0] == vicap_images[i].vf_info.v_frame.virt_addr[0]))
            {
                vicap_images[i].magic = 0x00;
            }
        }
    }

    return ret;
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
k_s32 vb_mgmt_init(void)
{
    for (int i = 0; i < VB_MGMT_RECORD_MAX_CNT; i++)
    {
        if (VB_MGMT_RECORD_MAGIC_IN_USE == vb_records[i].magic)
        {
            printf("maybe not call vb_mgmt_deinit, the block record %d is in use\n", i);
            vb_mgmt_pop_block_from_record(&vb_records[i]);
        }

        vb_records[i].magic = 0;
    }

    for (int i = 0; i < VB_MGMT_MPP_LINK_MAX_CNT; i++)
    {
        if (VB_MGMT_MPP_LINK_MAGIC_IN_USE == vb_mpp_links_records[i].magic)
        {
            printf("maybe not call vb_mgmt_deinit, the mpp link record %d is in use\n", i);

            vb_mgmt_pop_mpp_link_from_record(&vb_mpp_links_records[i].src, &vb_mpp_links_records[i].dst);
        }

        vb_mpp_links_records[i].magic = 0;
    }

    // for (int i = 0; i < VB_MGMT_PY_AT_EXIT_RECORD_MAX_CNT; i++)
    // {
    //     if (VB_MGMT_PY_AT_EXIT_RECORD_IN_USE == vb_py_at_exit_records[i].magic)
    //     {
    //         printf("maybe not call `vb_mgmt_py_at_exit`\n");
    //     }
    //     vb_py_at_exit_records[i].magic = 0;
    // }

    for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++)
    {
        if (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE == vicap_images[i].magic)
        {
            printf("maybe not call vb_mgmt_deinit, the vicap images record %d is in use\n", i);

            vb_mgmt_release_vicap_frame(&vicap_images[i]);
        }

        vicap_images[i].magic = 0;
    }

    return 0;
}

k_s32 vb_mgmt_deinit(void)
{
    for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++)
    {
        if (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE == vicap_images[i].magic)
        {
            vb_mgmt_release_vicap_frame(&vicap_images[i]);
        }
    }

    for (int i = 0; i < VICAP_MAX_DEV_NUMS; i++)
    {
        vb_mgmt_deinit_vicap(i);
    }
    usleep(1000 * 100);

    vb_mgmt_disable_vo_layers();

    extern void ide_dbg_vo_wbc_deinit(void);
    extern int ide_dbg_set_vo_wbc(int quality, int width, int height);

    ide_dbg_set_vo_wbc(0, 0, 0);
    ide_dbg_vo_wbc_deinit();

    kd_display_reset();

    for (int i = 0; i < VB_MGMT_RECORD_MAX_CNT; i++)
    {
        if (VB_MGMT_RECORD_MAGIC_IN_USE == vb_records[i].magic)
        {
            vb_mgmt_pop_block_from_record(&vb_records[i]);
        }
    }

    for (int i = 0; i < VB_MGMT_MPP_LINK_MAX_CNT; i++)
    {
        if (VB_MGMT_MPP_LINK_MAGIC_IN_USE == vb_mpp_links_records[i].magic)
        {
            vb_mgmt_pop_mpp_link_from_record(&vb_mpp_links_records[i].src, &vb_mpp_links_records[i].dst);
        }
    }

    /* not deinit py_at_exit */

    return 0;
}
