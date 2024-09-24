// SPDX-License-Identifier: GPL-2.0
/*
 * ov5695 VVcam + ISP driver implementation
 *
 *
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 * Copyright 2018 NXP
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * VVCam port is made:
 * @author Nikita Bulaev agency@grovety.com
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/v4l2-mediabus.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "ov5695.h"
#include "ov5695_1280x720_regs.h"
#include "ov5695_1296x972_regs.h"
#include "ov5695_1472x1840_regs.h"
#include "ov5695_1920x1440_regs.h"
#include "ov5695_2048x1856_regs.h"
#include "ov5695_2176x1824_regs.h"
#include "ov5695_640x480_regs.h"
#include "ov5695_regs_1080p.h"
#include "ov5695_regs_init.h"
#include "vvsensor.h"

#ifndef V4L2_CID_DIGITAL_GAIN
    #define V4L2_CID_DIGITAL_GAIN V4L2_CID_GAIN
#endif

#define VERSION_MAJOR 1
#define VERSION_MINOR 5
#define VERSION_PATCH 6

/* 45Mhz * 4 Binning */
#define OV5695_PIXEL_RATE (45 * 1000 * 1000 * 4)
#define OV5695_XVCLK_FREQ 24000000

#define REG_NULL 0xFFFF

#define OV5695_REG_VALUE_08BIT 1
#define OV5695_REG_VALUE_16BIT 2
#define OV5695_REG_VALUE_24BIT 3

#define OV5695_LANES           2
#define OV5695_BITS_PER_SAMPLE 10

static const char* const ov5695_supply_names[] = {
    "avdd",  /* Analog power */
    "dovdd", /* Digital I/O power */
    "dvdd",  /* Digital core power */
};

#define OV5695_NUM_SUPPLIES ARRAY_SIZE(ov5695_supply_names)

/*
Use USER_TO_KERNEL/KERNEL_TO_USER to fix "uaccess" exception on run time.
Also, use "copy_ret" to fix the build issue as below.
error: ignoring return value of function declared with 'warn_unused_result' attribute.
*/

#ifdef CONFIG_HARDENED_USERCOPY
    #define USER_TO_KERNEL(TYPE)                                    \
        do {                                                        \
            TYPE tmp;                                               \
            unsigned long copy_ret;                                 \
            arg      = (void*)(&tmp);                               \
            copy_ret = copy_from_user(arg, arg_user, sizeof(TYPE)); \
        } while (0)

    #define KERNEL_TO_USER(TYPE)                                  \
        do {                                                      \
            unsigned long copy_ret;                               \
            copy_ret = copy_to_user(arg_user, arg, sizeof(TYPE)); \
        } while (0)
#else
    #define USER_TO_KERNEL(TYPE)
    #define KERNEL_TO_USER(TYPE)
#endif

struct ov5695_capture_properties {
    __u64 max_lane_frequency;
    __u64 max_pixel_frequency;
    __u64 max_data_rate;
};

struct ov5695_mode {
    u32 vvcam_idx; /*!< VVCAM mode index. Backward compability with legacy driver. */
    u32 width;
    u32 height;
    u32 max_fps;
    u32 hts_def;
    u32 vts_def;
    u32 exp_def;
    const struct vvcam_sccb_data_s* reg_list;
};

struct ov5695 {
    struct i2c_client* i2c_client;
    struct clk* xvclk;
    struct gpio_desc* reset_gpio;
    u32 csi_id;
    struct ov5695_capture_properties ocp;
    struct regulator_bulk_data supplies[OV5695_NUM_SUPPLIES];
    int num_lanes;

    struct v4l2_subdev subdev;
    struct media_pad pad;
    struct v4l2_ctrl_handler ctrl_handler;
    struct v4l2_ctrl* exposure;
    struct v4l2_ctrl* anal_gain;
    struct v4l2_ctrl* digi_gain;
    struct v4l2_ctrl* hblank;
    struct v4l2_ctrl* vblank;
    struct v4l2_ctrl* test_pattern;
    struct mutex mutex;

    /* Flags */
    int power_count; /*!< Power on counter */
    bool streaming;  /*!< Streaming on/off */

    vvcam_mode_info_t cur_mode; /*!< Current mode (VVCam) */
    struct sensor_white_balance_s wb;

    const struct ov5695_mode*
        legacy_mode; /*!< Old driver mode. TODO: Move it to vvcam mode. */
};

static struct vvcam_mode_info_s ov5695_mode_info[] = {
    {
        .index = 0,
        .size =
            {
                .bounds_width  = 2176,
                .bounds_height = 1824,
                .top           = 0,
                .left          = 0,
                .width         = 2176,
                .height        = 1824,
            },
        .hdr_mode  = SENSOR_MODE_LINEAR,
        .bit_width = 10,
        .data_compress =
            {
                .enable = 0,
            },
        .bayer_pattern = BAYER_BGGR,
        .ae_info =
            {
                .def_frm_len_lines     = 0x7ec,
                .curr_frm_len_lines    = 0x7ec,
                .one_line_exp_time_ns  = 30240,
                .max_integration_line  = 0x7ec - 4,
                .min_integration_line  = 4,
                .max_again             = OV5695_GAIN_A_MAX_WITH_FRACT,
                .min_again             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .max_dgain             = OV5695_GAIN_D_MAX_WITH_FRACT,
                .min_dgain             = 2 * (1 << SENSOR_FIX_FRACBITS),
                .start_exposure        = 8 * 100 * (1 << SENSOR_FIX_FRACBITS),
                .cur_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .max_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .min_fps               = 5 * (1 << SENSOR_FIX_FRACBITS),
                .min_afps              = 5 * (1 << SENSOR_FIX_FRACBITS),
                .int_update_delay_frm  = 1,
                .gain_update_delay_frm = 2,
            },
        .mipi_info =
            {
                .mipi_lane = 2,
            },
        .preg_data      = ov5695_2176x1824_regs,
        .reg_data_count = ARRAY_SIZE(ov5695_2176x1824_regs),
    },
    {
        .index = 1,
        .size =
            {
                .bounds_width  = 2048,
                .bounds_height = 1856,
                .top           = 0,
                .left          = 0,
                .width         = 2048,
                .height        = 1856,
            },
        .hdr_mode  = SENSOR_MODE_LINEAR,
        .bit_width = 10,
        .data_compress =
            {
                .enable = 0,
            },
        .bayer_pattern = BAYER_BGGR,
        .ae_info =
            {
                .def_frm_len_lines     = 0x7ec,
                .curr_frm_len_lines    = 0x7ec,
                .one_line_exp_time_ns  = 30240,
                .max_integration_line  = 0x7ec - 4,
                .min_integration_line  = 4,
                .max_again             = OV5695_GAIN_A_MAX_WITH_FRACT,
                .min_again             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .max_dgain             = OV5695_GAIN_D_MAX_WITH_FRACT,
                .min_dgain             = 2 * (1 << SENSOR_FIX_FRACBITS),
                .start_exposure        = 8 * 100 * (1 << SENSOR_FIX_FRACBITS),
                .cur_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .max_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .min_fps               = 5 * (1 << SENSOR_FIX_FRACBITS),
                .min_afps              = 5 * (1 << SENSOR_FIX_FRACBITS),
                .int_update_delay_frm  = 1,
                .gain_update_delay_frm = 2,
            },
        .mipi_info =
            {
                .mipi_lane = 2,
            },
        .preg_data      = ov5695_2048x1856_regs,
        .reg_data_count = ARRAY_SIZE(ov5695_2048x1856_regs),
    },
    {
        .index = 2,
        .size =
            {
                .bounds_width  = 1472,
                .bounds_height = 1840,
                .top           = 0,
                .left          = 0,
                .width         = 1472,
                .height        = 1840,
            },
        .hdr_mode  = SENSOR_MODE_LINEAR,
        .bit_width = 10,
        .data_compress =
            {
                .enable = 0,
            },
        .bayer_pattern = BAYER_BGGR,
        .ae_info =
            {
                .def_frm_len_lines     = 0x7ec,
                .curr_frm_len_lines    = 0x7ec,
                .one_line_exp_time_ns  = 30240,
                .max_integration_line  = 0x7ec - 4,
                .min_integration_line  = 4,
                .max_again             = OV5695_GAIN_A_MAX_WITH_FRACT,
                .min_again             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .max_dgain             = OV5695_GAIN_D_MAX_WITH_FRACT,
                .min_dgain             = 2 * (1 << SENSOR_FIX_FRACBITS),
                .start_exposure        = 8 * 100 * (1 << SENSOR_FIX_FRACBITS),
                .cur_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .max_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .min_fps               = 5 * (1 << SENSOR_FIX_FRACBITS),
                .min_afps              = 5 * (1 << SENSOR_FIX_FRACBITS),
                .int_update_delay_frm  = 1,
                .gain_update_delay_frm = 2,
            },
        .mipi_info =
            {
                .mipi_lane = 2,
            },
        .preg_data      = ov5695_1472x1840_regs,
        .reg_data_count = ARRAY_SIZE(ov5695_1472x1840_regs),
    },
    {
        .index = 3,
        .size =
            {
                .bounds_width  = 1920,
                .bounds_height = 1440,
                .top           = 0,
                .left          = 0,
                .width         = 1920,
                .height        = 1440,
            },
        .hdr_mode  = SENSOR_MODE_LINEAR,
        .bit_width = 10,
        .data_compress =
            {
                .enable = 0,
            },
        .bayer_pattern = BAYER_BGGR,
        .ae_info =
            {
                .def_frm_len_lines     = 0x8b8,
                .curr_frm_len_lines    = 0x8b8,
                .one_line_exp_time_ns  = 30240,
                .max_integration_line  = 0x8ff - 8,
                .min_integration_line  = 8,
                .max_again             = OV5695_GAIN_A_MAX_WITH_FRACT,
                .min_again             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .max_dgain             = OV5695_GAIN_D_MAX_WITH_FRACT,
                .min_dgain             = 2 * (1 << SENSOR_FIX_FRACBITS),
                .start_exposure        = 8 * 100 * (1 << SENSOR_FIX_FRACBITS),
                .cur_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .max_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .min_fps               = 5 * (1 << SENSOR_FIX_FRACBITS),
                .min_afps              = 5 * (1 << SENSOR_FIX_FRACBITS),
                .int_update_delay_frm  = 1,
                .gain_update_delay_frm = 2,
            },
        .mipi_info =
            {
                .mipi_lane = 2,
            },
        .preg_data      = ov5695_1920x1440_regs,
        .reg_data_count = ARRAY_SIZE(ov5695_1920x1440_regs),
    },
    {
        .index = 4,
        .size =
            {
                .bounds_width  = 1920,
                .bounds_height = 1080,
                .top           = 0,
                .left          = 0,
                .width         = 1920,
                .height        = 1080,
            },
        .hdr_mode  = SENSOR_MODE_LINEAR,
        .bit_width = 10,
        .data_compress =
            {
                .enable = 0,
            },
        .bayer_pattern = BAYER_BGGR,
        .ae_info =
            {
                .def_frm_len_lines     = 0x8b8,
                .curr_frm_len_lines    = 0x8b8,
                .one_line_exp_time_ns  = 30240,
                .max_integration_line  = 0x8ff - 8,
                .min_integration_line  = 8,
                .max_again             = OV5695_GAIN_A_MAX_WITH_FRACT,
                .min_again             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .max_dgain             = OV5695_GAIN_D_MAX_WITH_FRACT,
                .min_dgain             = 2 * (1 << SENSOR_FIX_FRACBITS),
                .start_exposure        = 8 * 100 * (1 << SENSOR_FIX_FRACBITS),
                .cur_fps               = 30 * (1 << SENSOR_FIX_FRACBITS),
                .max_fps               = 60 * (1 << SENSOR_FIX_FRACBITS),
                .min_fps               = 5 * (1 << SENSOR_FIX_FRACBITS),
                .min_afps              = 5 * (1 << SENSOR_FIX_FRACBITS),
                .int_update_delay_frm  = 1,
                .gain_update_delay_frm = 2,
            },
        .mipi_info =
            {
                .mipi_lane = 2,
            },
        .preg_data      = ov5695_1920x1080_regs,
        .reg_data_count = ARRAY_SIZE(ov5695_1920x1080_regs),
    },
};

#define to_ov5695(sd) container_of(sd, struct ov5695, subdev)

// clang-format off

static const char* const ov5695_test_pattern_menu[] = {
    "Disabled",
    "Vertical Color Bar Type 1",
    "Vertical Color Bar Type 2",
    "Vertical Color Bar Type 3",
    "Vertical Color Bar Type 4"
};

// clang-format on

static const struct ov5695_mode supported_modes[] = {
    {
        .vvcam_idx = 0,
        .width     = 2176,
        .height    = 1824,
        .max_fps   = 30,
        .exp_def   = 0x0450,
        .hts_def   = 0x02e4 * 4,
        .vts_def   = 0x07ec,
        .reg_list  = ov5695_2176x1824_regs,
    },
    {
        .vvcam_idx = 1,
        .width     = 2048,
        .height    = 1856,
        .max_fps   = 30,
        .exp_def   = 0x0450,
        .hts_def   = 0x02e4 * 4,
        .vts_def   = 0x07ec,
        .reg_list  = ov5695_2048x1856_regs,
    },
    {
        .vvcam_idx = 2,
        .width     = 1472,
        .height    = 1840,
        .max_fps   = 30,
        .exp_def   = 0x0450,
        .hts_def   = 0x02e4 * 4,
        .vts_def   = 0x07ec,
        .reg_list  = ov5695_1472x1840_regs,
    },
    {
        .vvcam_idx = 3,
        .width     = 1920,
        .height    = 1440,
        .max_fps   = 30,
        .exp_def   = 0x0450,
        .hts_def   = 0x02a0 * 4,
        .vts_def   = 0x08b8,
        .reg_list  = ov5695_1920x1440_regs,
    },
    {
        .vvcam_idx = 4,
        .width     = 1920,
        .height    = 1080,
        .max_fps   = 30,
        .exp_def   = 0x07b0,
        .hts_def   = 0x02a0 * 4,
        .vts_def   = 0x07e8,
        .reg_list  = ov5695_1920x1080_regs,
    },
    {
        .vvcam_idx = 0xFFFFFFFD,   // TODO: Not implemented yet in vvcam modes
        .width     = 1296,
        .height    = 972,
        .max_fps   = 60,
        .exp_def   = 0x03e0,
        .hts_def   = 0x02e4 * 4,
        .vts_def   = 0x03f4,
        .reg_list  = ov5695_1296x972_regs,
    },
    {
        .vvcam_idx = 0xFFFFFFFE,   // TODO: Not implemented yet in vvcam modes
        .width     = 1280,
        .height    = 720,
        .max_fps   = 30,
        .exp_def   = 0x0450,
        .hts_def   = 0x02a0 * 4,
        .vts_def   = 0x08b8,
        .reg_list  = ov5695_1280x720_regs,
    },
    {
        .vvcam_idx = 0xFFFFFFFF,   // TODO: Not implemented yet in vvcam modes
        .width     = 640,
        .height    = 480,
        .max_fps   = 120,
        .exp_def   = 0x0450,
        .hts_def   = 0x02a0 * 4,
        .vts_def   = 0x022e,
        .reg_list  = ov5695_640x480_regs,
    },
};

#define OV5695_LINK_FREQ_420MHZ 420000000
static const s64 link_freq_menu_items[] = {OV5695_LINK_FREQ_420MHZ};

/* Write registers up to 4 at a time */
static int ov5695_write_reg(struct i2c_client* client, u16 reg, u32 len,
                            u32 val)
{
    u32 buf_i, val_i;
    u8 buf[6];
    u8* val_p;
    __be32 val_be;

    if (len > 4)
        return -EINVAL;

    buf[0] = reg >> 8;
    buf[1] = reg & 0xff;

    val_be = cpu_to_be32(val);
    val_p  = (u8*)&val_be;
    buf_i  = 2;
    val_i  = 4 - len;

    while (val_i < 4)
        buf[buf_i++] = val_p[val_i++];

    if (i2c_master_send(client, buf, len + 2) != len + 2)
        return -EIO;

    return 0;
}

static int ov5695_write_array(struct i2c_client* client,
                              const struct vvcam_sccb_data_s* regs, u32 size)
{
    u32 i;
    int ret = 0;

    for (i = 0; i < size; i++)
        ret = ov5695_write_reg(client, regs[i].addr, OV5695_REG_VALUE_08BIT,
                               regs[i].data);

    return ret;
}

/* Read registers up to 4 at a time */
static int ov5695_read_reg(struct i2c_client* client, u16 reg, unsigned int len,
                           u32* val)
{
    struct i2c_msg msgs[2];
    u8* data_be_p;
    __be32 data_be     = 0;
    __be16 reg_addr_be = cpu_to_be16(reg);
    int ret;

    if (len > 4)
        return -EINVAL;

    data_be_p = (u8*)&data_be;
    /* Write register address */
    msgs[0].addr  = client->addr;
    msgs[0].flags = 0;
    msgs[0].len   = 2;
    msgs[0].buf   = (u8*)&reg_addr_be;

    /* Read data from register */
    msgs[1].addr  = client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = len;
    msgs[1].buf   = &data_be_p[4 - len];

    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs))
        return -EIO;

    *val = be32_to_cpu(data_be);

    return 0;
}

static int ov5695_set_fmt(struct v4l2_subdev* sd,
                          struct v4l2_subdev_state* sd_state,
                          struct v4l2_subdev_format* fmt)
{
    int ret = 0;
    u8 reg  = 0;

    struct ov5695* ov5695 = to_ov5695(sd);

    dev_info(&ov5695->i2c_client->dev, "%s enter", __func__);

    mutex_lock(&ov5695->mutex);

    if ((fmt->format.width != ov5695->cur_mode.size.bounds_width) ||
        (fmt->format.height != ov5695->cur_mode.size.bounds_height)) {
        dev_err(&ov5695->i2c_client->dev,
                "%s:set sensor format %dx%d error. Out of bounds width/height "
                "%dx%d\n",
                __func__, fmt->format.width, fmt->format.height,
                ov5695->cur_mode.size.bounds_width,
                ov5695->cur_mode.size.bounds_height);
        mutex_unlock(&ov5695->mutex);
        return -EINVAL;
    }

    ret = ov5695_write_array(ov5695->i2c_client, ov5695_regs_init,
                             ARRAY_SIZE(ov5695_regs_init));
    if (ret)
        return ret;

    // Set number of lanes
    reg = OV5695_MIPI_SC_CTRL_PHY_RTS | OV5695_MIPI_SC_CTRL_MIPI_EN;

    if (ov5695->num_lanes == 2) {
        reg |= OV5695_MIPI_SC_CTRL_TWOLANE;
    }

    ret = ov5695_write_reg(ov5695->i2c_client, OV5695_REG_SC_MIPI_SC_CTRL,
                           OV5695_REG_VALUE_08BIT, reg);

    // Set default mode
    ret = ov5695_write_array(
        ov5695->i2c_client,
        (struct vvcam_sccb_data_s*)ov5695->cur_mode.preg_data,
        ov5695->cur_mode.reg_data_count);

    fmt->format.code   = MEDIA_BUS_FMT_SBGGR10_1X10;
    fmt->format.field  = V4L2_FIELD_NONE;
    fmt->format.width  = ov5695->cur_mode.size.width;
    fmt->format.height = ov5695->cur_mode.size.height;

    mutex_unlock(&ov5695->mutex);

    return ret;
}

static int ov5695_get_fmt(struct v4l2_subdev* sd,
                          struct v4l2_subdev_state* sd_state,
                          struct v4l2_subdev_format* fmt)
{
    struct ov5695* ov5695 = to_ov5695(sd);

    dev_info(&ov5695->i2c_client->dev, "%s enter", __func__);

    mutex_lock(&ov5695->mutex);

    fmt->format.width  = ov5695->cur_mode.size.width;
    fmt->format.height = ov5695->cur_mode.size.height;
    fmt->format.code   = MEDIA_BUS_FMT_SBGGR10_1X10;
    fmt->format.field  = V4L2_FIELD_NONE;

    mutex_unlock(&ov5695->mutex);

    return 0;
}

static int ov5695_set_fps(struct ov5695* sensor, u32 fps)
{
    // Not implemented yet, and possibly no need to implement.
    return 0;
}

static int ov5695_get_fps(struct ov5695* sensor, u32* pfps)
{
    *pfps = sensor->cur_mode.ae_info.cur_fps;
    return 0;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 12, 0)
static int ov5695_enum_mbus_code(struct v4l2_subdev* sd,
                                 struct v4l2_subdev_state* state,
                                 struct v4l2_subdev_mbus_code_enum* code)
#else
static int ov5695_enum_mbus_code(struct v4l2_subdev* sd,
                                 struct v4l2_subdev_pad_config* cfg,
                                 struct v4l2_subdev_mbus_code_enum* code)
#endif
{
    struct ov5695* ov5695 = to_ov5695(sd);

    dev_dbg(&ov5695->i2c_client->dev, "%s enter", __func__);

    if (code->index != 0) {
        dev_err(&ov5695->i2c_client->dev, "index out of bounds\n");
        return -EINVAL;
    }

    code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

    return 0;
}

static int ov5695_enum_frame_sizes(struct v4l2_subdev* sd,
                                   struct v4l2_subdev_state* sd_state,
                                   struct v4l2_subdev_frame_size_enum* fse)
{
    struct ov5695* ov5695 = to_ov5695(sd);

    dev_dbg(&ov5695->i2c_client->dev, "%s enter", __func__);

    if (fse->index >= ARRAY_SIZE(supported_modes)) {
        dev_err(&ov5695->i2c_client->dev, "Frame size index out of bounds\n");
        return -EINVAL;
    }

    if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10) {
        dev_err(&ov5695->i2c_client->dev,
                "Only Bayer RAW10 format supported\n");
        return -EINVAL;
    }

    fse->min_width  = supported_modes[fse->index].width;
    fse->max_width  = supported_modes[fse->index].width;
    fse->max_height = supported_modes[fse->index].height;
    fse->min_height = supported_modes[fse->index].height;

    return 0;
}

/** Legacy V4L2 controls from old driver */
static int __maybe_unused ov5695_enable_test_pattern_v4l2(struct ov5695* ov5695,
                                                          u32 pattern)
{
    u32 val;

    if (pattern)
        val = (pattern - 1) | OV5695_TEST_PATTERN_ENABLE;
    else
        val = OV5695_TEST_PATTERN_DISABLE;

    return ov5695_write_reg(ov5695->i2c_client, OV5695_REG_TEST_PATTERN,
                            OV5695_REG_VALUE_08BIT, val);
}

static int __maybe_unused ov5695_set_test_pattern_vvcam(struct ov5695* ov5695,
                                                        void* arg)
{
    int ret;
    struct sensor_test_pattern_s test_pattern;
    u32 val;

    ret = copy_from_user(&test_pattern, arg, sizeof(test_pattern));

    if (ret != 0)
        return -ENOMEM;

    if (test_pattern.enable) {
        if (test_pattern.pattern >= ARRAY_SIZE(ov5695_test_pattern_menu) - 1) {
            return -EINVAL;
        }

        dev_info(&ov5695->i2c_client->dev, "%s: %s (%u)", __func__,
                 ov5695_test_pattern_menu[test_pattern.pattern + 1],
                 test_pattern.pattern);

        val = (test_pattern.pattern & 0x03) | OV5695_TEST_PATTERN_ENABLE;
    } else {
        dev_info(&ov5695->i2c_client->dev, "Test pattern: %s",
                 ov5695_test_pattern_menu[0]);

        val = OV5695_TEST_PATTERN_DISABLE;
    }

    return ov5695_write_reg(ov5695->i2c_client, OV5695_REG_TEST_PATTERN,
                            OV5695_REG_VALUE_08BIT, val);
}

static int __ov5695_start_stream(struct ov5695* ov5695)
{
    int ret;

    /* In case these controls are set before streaming */
    ret = __v4l2_ctrl_handler_setup(&ov5695->ctrl_handler);

    ret |= ov5695_write_reg(ov5695->i2c_client, OV5695_REG_CTRL_MODE,
                            OV5695_REG_VALUE_08BIT, OV5695_MODE_STREAMING);

    return ret;
}

static int __ov5695_stop_stream(struct ov5695* ov5695)
{
    return ov5695_write_reg(ov5695->i2c_client, OV5695_REG_CTRL_MODE,
                            OV5695_REG_VALUE_08BIT, OV5695_MODE_SW_STANDBY);
}

static int ov5695_s_stream(struct v4l2_subdev* sd, int on)
{
    struct ov5695* ov5695     = to_ov5695(sd);
    struct i2c_client* client = ov5695->i2c_client;
    int ret                   = 0;

    dev_dbg(&client->dev, "Set stream %s\n", on ? "ON" : "OFF");

    mutex_lock(&ov5695->mutex);
    on = !!on;
    if (on == ov5695->streaming)
        goto unlock_and_return;

    if (on) {
        ret = pm_runtime_resume_and_get(&client->dev);
        if (ret < 0)
            goto unlock_and_return;

        ret = __ov5695_start_stream(ov5695);
        if (ret) {
            v4l2_err(sd, "start stream failed while write regs\n");
            pm_runtime_put(&client->dev);
            goto unlock_and_return;
        }
    } else {
        __ov5695_stop_stream(ov5695);
        pm_runtime_put(&client->dev);
    }

    ov5695->streaming = on;

unlock_and_return:
    mutex_unlock(&ov5695->mutex);

    return ret;
}

static int __ov5695_power_on(struct ov5695* ov5695)
{
    int i, ret;
    struct device* dev = &ov5695->i2c_client->dev;

    if (ov5695->power_count++) {
        return 0;
    }

    ret = clk_prepare_enable(ov5695->xvclk);

    if (ret < 0) {
        dev_err(dev, "Failed to enable xvclk\n");
        return ret;
    }

    if (ov5695->reset_gpio) {
        gpiod_set_value_cansleep(ov5695->reset_gpio, 1);
    }

    /*
	 * The hardware requires the regulators to be powered on in order,
	 * so enable them one by one.
	 */
    for (i = 0; i < OV5695_NUM_SUPPLIES; i++) {
        ret = regulator_enable(ov5695->supplies[i].consumer);
        if (ret) {
            dev_err(dev, "Failed to enable %s: %d\n",
                    ov5695->supplies[i].supply, ret);
            goto disable_reg_clk;
        }
    }

    if (ov5695->reset_gpio) {
        gpiod_set_value_cansleep(ov5695->reset_gpio, 0);
    }

    usleep_range(1000, 1200);

    return 0;

disable_reg_clk:
    ov5695->power_count--;

    for (--i; i >= 0; i--)
        regulator_disable(ov5695->supplies[i].consumer);

    clk_disable_unprepare(ov5695->xvclk);

    return ret;
}

static void __ov5695_power_off(struct ov5695* ov5695)
{
    struct device* dev = &ov5695->i2c_client->dev;
    int i, ret;

    if (ov5695->power_count > 0) {
        ov5695->power_count--;
    } else {
        dev_warn(dev, "Power is already OFF\n");
    }

    if (ov5695->power_count) {
        return;
    }

    clk_disable_unprepare(ov5695->xvclk);

    if (ov5695->reset_gpio) {
        gpiod_set_value_cansleep(ov5695->reset_gpio, 1);
    }

    /*
	 * The hardware requires the regulators to be powered off in order,
	 * so disable them one by one.
	 */
    for (i = OV5695_NUM_SUPPLIES - 1; i >= 0; i--) {
        ret = regulator_disable(ov5695->supplies[i].consumer);
        if (ret)
            dev_err(dev, "Failed to disable %s: %d\n",
                    ov5695->supplies[i].supply, ret);
    }
}

static int ov5695_s_power(struct ov5695* sensor, int on)
{
    int ret = 0;

    struct i2c_client* client = sensor->i2c_client;

    dev_info(&client->dev, "Power %s\n", on ? "ON" : "OFF");

    if (on)
        ret = __ov5695_power_on(sensor);
    else
        __ov5695_power_off(sensor);

    return ret;
}

static int ov5695_s_power_v4l2(struct v4l2_subdev* sd, int on)
{
    struct ov5695* ov5695 = to_ov5695(sd);

    return ov5695_s_power(ov5695, on);
}

static int ov5695_get_clk(struct ov5695* sensor, void* clk)
{
    struct vvcam_clk_s vvcam_clk;
    int ret = 0;

    vvcam_clk.sensor_mclk       = clk_get_rate(sensor->xvclk);
    vvcam_clk.csi_max_pixel_clk = sensor->ocp.max_pixel_frequency;
    ret = copy_to_user(clk, &vvcam_clk, sizeof(struct vvcam_clk_s));

    if (ret != 0)
        ret = -EINVAL;

    return ret;
}

static int __maybe_unused ov5695_runtime_resume(struct device* dev)
{
    struct v4l2_subdev* sd = dev_get_drvdata(dev);
    struct ov5695* ov5695  = to_ov5695(sd);

    return __ov5695_power_on(ov5695);
}

static int __maybe_unused ov5695_runtime_suspend(struct device* dev)
{
    struct v4l2_subdev* sd = dev_get_drvdata(dev);
    struct ov5695* ov5695  = to_ov5695(sd);

    __ov5695_power_off(ov5695);

    return 0;
}

// IOCTL function declaration
static long ov5695_priv_ioctl(struct v4l2_subdev* sd, unsigned int cmd,
                              void* arg);

static int ov5695_open(struct v4l2_subdev* sd, struct v4l2_subdev_fh* fh)
{
    struct ov5695* ov5695 = to_ov5695(sd);
    struct v4l2_mbus_framefmt* try_fmt =
        v4l2_subdev_get_try_format(sd, fh->state, 0);
    const struct ov5695_mode* def_mode = &supported_modes[0];

    dev_dbg(&ov5695->i2c_client->dev, "%s enter", __func__);

    mutex_lock(&ov5695->mutex);
    /* Initialize try_fmt */
    try_fmt->width  = def_mode->width;
    try_fmt->height = def_mode->height;
    try_fmt->code   = MEDIA_BUS_FMT_SBGGR10_1X10;
    try_fmt->field  = V4L2_FIELD_NONE;

    mutex_unlock(&ov5695->mutex);
    /* No crop or compose */

    return 0;
}

static const struct dev_pm_ops ov5695_pm_ops = {
    SET_RUNTIME_PM_OPS(ov5695_runtime_suspend, ov5695_runtime_resume, NULL)};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov5695_internal_ops = {
    .open = ov5695_open,
};
#endif

static const struct v4l2_subdev_video_ops ov5695_video_ops = {
    .s_stream = ov5695_s_stream,
};

static const struct v4l2_subdev_pad_ops ov5695_pad_ops = {
    .enum_mbus_code  = ov5695_enum_mbus_code,
    .enum_frame_size = ov5695_enum_frame_sizes,
    .get_fmt         = ov5695_get_fmt,
    .set_fmt         = ov5695_set_fmt,
};

static struct v4l2_subdev_core_ops ov5695_subdev_core_ops = {
    .s_power = ov5695_s_power_v4l2,
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
    .ioctl = ov5695_priv_ioctl,
#endif
};

static const struct v4l2_subdev_ops ov5695_subdev_ops = {
    .core  = &ov5695_subdev_core_ops,
    .video = &ov5695_video_ops,
    .pad   = &ov5695_pad_ops,
};

#if defined(CONFIG_MEDIA_CONTROLLER)
static int ov5695_link_setup(struct media_entity* entity,
                             struct media_pad const* local,
                             struct media_pad const* remote, u32 flags)
{
    return 0;
}

static const struct media_entity_operations ov5695_entity_ops = {
    .link_setup = ov5695_link_setup,
};
#endif

static int ov5695_set_wb(struct ov5695* sensor, void* pwb_cfg)
{
    int ret    = 0;
    u16 g_gain = 0;

    struct sensor_white_balance_s* wb = (struct sensor_white_balance_s*)pwb_cfg;

    if (wb == NULL)
        return -ENOMEM;

    g_gain = wb->gb_gain + wb->gr_gain;

    // Currently, set max wb digital gain
    ret |= ov5695_write_reg(sensor->i2c_client, OV5695_REG_ISP_D_GAIN_LONG,
                            OV5695_REG_VALUE_16BIT, 0x0fff);

    ret |= ov5695_write_reg(sensor->i2c_client, OV5695_REG_ISP_D_GAIN_SHORT,
                            OV5695_REG_VALUE_16BIT, 0x0fff);

    ret |= ov5695_write_reg(sensor->i2c_client,
                            OV5695_REG_ISP_RED_GAIN_LONG,   // 0xffe
                            OV5695_REG_VALUE_16BIT, wb->r_gain);

    ret |= ov5695_write_reg(sensor->i2c_client,
                            OV5695_REG_ISP_GREEN_GAIN_LONG,   // 0xbff
                            OV5695_REG_VALUE_16BIT, g_gain);

    ret |= ov5695_write_reg(sensor->i2c_client,
                            OV5695_REG_ISP_BLUE_GAIN_LONG,   // 0xfff
                            OV5695_REG_VALUE_16BIT, wb->b_gain);

    ret |= ov5695_write_reg(sensor->i2c_client,
                            OV5695_REG_ISP_RED_GAIN_SHORT,   // 0xffe
                            OV5695_REG_VALUE_16BIT, wb->r_gain);

    ret |= ov5695_write_reg(sensor->i2c_client,
                            OV5695_REG_ISP_GREEN_GAIN_SHORT,   // 0xbff
                            OV5695_REG_VALUE_16BIT, g_gain);

    ret |= ov5695_write_reg(sensor->i2c_client,
                            OV5695_REG_ISP_BLUE_GAIN_SHORT,   // 0xfff
                            OV5695_REG_VALUE_16BIT, wb->b_gain);

    return ret;
}

static int ov5695_set_exp(struct ov5695* sensor, u32 exp)
{
    int ret     = 0;
    u32 val_exp = ((exp & 0x00FFFFFF) << 4) | 0x4;

    dev_dbg(&sensor->i2c_client->dev, "Set exp: %d, val_exp: %d", exp, val_exp);

    /* 4 least significant bits of expsoure are fractional part */
    ret = ov5695_write_reg(sensor->i2c_client, OV5695_REG_EXPOSURE,
                           OV5695_REG_VALUE_24BIT, val_exp);

    return ret;
}

/**
 * @brief Setup gain
 * @note  Gain = (Analog gain) * (Digital gain)
 *        First count and set analog gain, then count and set digital gain
 *
 * @param[in] sensor     Pointer to sensor device
 * @param[in] total_gain Total gain to set with SENSOR_FIX_FRACBITS fractional part
 *
 * @return int 0 if success, otherwise error code
 */
static int ov5695_set_gain(struct ov5695* sensor, u32 total_gain)
{
    int ret   = 0;
    u32 again = 0x80;
    u32 dgain = 0x400;

    if (total_gain >= (1 << SENSOR_FIX_FRACBITS)) {
        if (total_gain <= OV5695_GAIN_A_MAX_WITH_FRACT) {
            again = total_gain >> 6;
            dgain = 0x400;
        } else {
            again = OV5695_GAIN_A_MAX_WITH_FRACT >> 6;
            dgain = (total_gain << SENSOR_FIX_FRACBITS) /
                    OV5695_GAIN_A_MAX_WITH_FRACT;
        }
    }

    dev_dbg(&sensor->i2c_client->dev,
            "Set total gain: %d, again: %d, dgain: %d", total_gain, again >> 4,
            dgain >> 10);

    // OV5695 Bit[7:5]: Not used
    ret |= ov5695_write_reg(sensor->i2c_client, OV5695_REG_ANALOG_GAIN_HI,
                            OV5695_REG_VALUE_08BIT,
                            (again >> 8) & OV5695_REG_ANALOG_GAIN_HI_MASK);

    ret |= ov5695_write_reg(sensor->i2c_client, OV5695_REG_ANALOG_GAIN_LOW,
                            OV5695_REG_VALUE_08BIT,
                            (again)&OV5695_REG_ANALOG_GAIN_LOW_COARSE_MASK);

    // Bit[7:0]: Long digital gain[13:6]
    ret |= ov5695_write_reg(sensor->i2c_client, OV5695_REG_DIGI_GAIN_H,
                            OV5695_REG_VALUE_08BIT, (dgain >> 6) & 0xFF);

    // Bit[5:0]: Long digital gain[5:0]
    ret |= ov5695_write_reg(sensor->i2c_client, OV5695_REG_DIGI_GAIN_L,
                            OV5695_REG_VALUE_08BIT, dgain & 0x3F);

    return ret;
}

static int ov5695_set_ctrl(struct v4l2_ctrl* ctrl)
{
    struct ov5695* ov5695 =
        container_of(ctrl->handler, struct ov5695, ctrl_handler);
    struct i2c_client* client = ov5695->i2c_client;
    s64 max;
    int ret = 0;

    /* Propagate change of current control to all related controls */
    switch (ctrl->id) {
        case V4L2_CID_VBLANK:
            /* Update max exposure while meeting expected vblanking */
            max = ov5695->cur_mode.size.height + ctrl->val - 4;
            __v4l2_ctrl_modify_range(
                ov5695->exposure, ov5695->exposure->minimum, max,
                ov5695->exposure->step, ov5695->exposure->default_value);
            break;
    }

    if (!pm_runtime_get_if_in_use(&client->dev))
        return 0;

    switch (ctrl->id) {
        case V4L2_CID_EXPOSURE:
            /* 4 least significant bits of expsoure are fractional part */
            ret = ov5695_set_exp(ov5695, ctrl->val);

            break;
        case V4L2_CID_ANALOGUE_GAIN:
            ret =
                ov5695_write_reg(ov5695->i2c_client, OV5695_REG_ANALOG_GAIN_LOW,
                                 OV5695_REG_VALUE_08BIT, ctrl->val);
            break;
        case V4L2_CID_DIGITAL_GAIN:
            ret = ov5695_write_reg(ov5695->i2c_client, OV5695_REG_DIGI_GAIN_L,
                                   OV5695_REG_VALUE_08BIT,
                                   ctrl->val & OV5695_DIGI_GAIN_L_MASK);
            ret = ov5695_write_reg(ov5695->i2c_client, OV5695_REG_DIGI_GAIN_H,
                                   OV5695_REG_VALUE_08BIT,
                                   ctrl->val >> OV5695_DIGI_GAIN_H_SHIFT);
            break;
        case V4L2_CID_VBLANK:
            ret = ov5695_write_reg(ov5695->i2c_client, OV5695_REG_VTS,
                                   OV5695_REG_VALUE_16BIT,
                                   ctrl->val + ov5695->cur_mode.size.height);
            break;
        case V4L2_CID_TEST_PATTERN:
            ret = ov5695_enable_test_pattern_v4l2(ov5695, ctrl->val);
            break;
        default:
            dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n", __func__,
                     ctrl->id, ctrl->val);
            break;
    }

    pm_runtime_put(&client->dev);

    return ret;
}

static const struct v4l2_ctrl_ops ov5695_ctrl_ops = {
    .s_ctrl = ov5695_set_ctrl,
};

static int ov5695_query_capability(struct ov5695* sensor, void* arg)
{
    struct v4l2_capability* pcap = (struct v4l2_capability*)arg;

    strcpy((char*)pcap->driver, "ov5695");
    sprintf((char*)pcap->bus_info, "csi%d", sensor->csi_id);
    strcpy(pcap->card, "OV5695");
    // pcap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

    if (sensor->i2c_client->adapter) {
        pcap->bus_info[VVCAM_CAP_BUS_INFO_I2C_ADAPTER_NR_POS] =
            (__u8)sensor->i2c_client->adapter->nr;
    } else {
        pcap->bus_info[VVCAM_CAP_BUS_INFO_I2C_ADAPTER_NR_POS] = 0xFF;
    }

    return 0;
}

static int ov5695_query_supports(struct ov5695* sensor, void* parry)
{
    struct vvcam_mode_info_array_s* psensor_mode_arry = parry;

    psensor_mode_arry->count = ARRAY_SIZE(ov5695_mode_info);
    memcpy((void*)&psensor_mode_arry->modes, (void*)ov5695_mode_info,
           sizeof(ov5695_mode_info));

    return 0;
}

static int ov5695_get_sensor_mode(struct ov5695* sensor, void* pmode)
{
    int ret = 0;

    ret = copy_to_user(pmode, &sensor->cur_mode,
                       sizeof(struct vvcam_mode_info_s));
    if (ret != 0)
        ret = -ENOMEM;

    return ret;
}

static int ov5695_set_sensor_mode(struct ov5695* sensor, void* pmode)
{
    int ret = 0;
    int i = 0, j = 0;
    struct vvcam_mode_info_s sensor_mode;
    ret = copy_from_user(&sensor_mode, pmode, sizeof(struct vvcam_mode_info_s));

    if (ret != 0)
        return -ENOMEM;

    for (i = 0; i < ARRAY_SIZE(ov5695_mode_info); i++) {
        if (ov5695_mode_info[i].index == sensor_mode.index) {
            memcpy(&sensor->cur_mode, &ov5695_mode_info[i],
                   sizeof(struct vvcam_mode_info_s));

            dev_info(&sensor->i2c_client->dev, "Set sensor mode: %d, %dx%d\n",
                     sensor->cur_mode.index, sensor->cur_mode.size.width,
                     sensor->cur_mode.size.height);

            // Legacy mode, TODO: Remove after VVCAM migration
            for (j = 0; j < ARRAY_SIZE(supported_modes); j++) {
                if (supported_modes[j].vvcam_idx == sensor_mode.index) {
                    sensor->legacy_mode = &supported_modes[j];
                    break;
                }
            }

            return 0;
        }
    }

    return -ENXIO;
}

static long ov5695_priv_ioctl(struct v4l2_subdev* sd, unsigned int cmd,
                              void* arg)
{
    struct ov5695* ov5695 = to_ov5695(sd);
    int ret               = 0;

    if (ov5695 == NULL) {
        printk("ov5695_priv_ioctl: ov5695 is NULL\n");
        return -ENODEV;
    }

    switch (cmd) {
        case VVSENSORIOC_S_POWER:
            USER_TO_KERNEL(int);
            ret = ov5695_s_power(ov5695, *(int*)arg);
            break;

        case VVSENSORIOC_S_CLK:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_S_CLK\n",
                    __func__);

            break;

        case VVSENSORIOC_G_CLK:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_G_CLK\n",
                    __func__);

            ret = ov5695_get_clk(ov5695, arg);
            break;

        case VVSENSORIOC_RESET:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_RESET\n",
                    __func__);

            break;

        case VIDIOC_QUERYCAP:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VIDIOC_QUERYCAP\n",
                    __func__);
            ret = ov5695_query_capability(ov5695, arg);
            break;

        case VVSENSORIOC_QUERY:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_QUERY\n",
                    __func__);

            USER_TO_KERNEL(struct vvcam_mode_info_array_s);
            ret = ov5695_query_supports(ov5695, arg);
            KERNEL_TO_USER(struct vvcam_mode_info_array_s);
            break;

        case VVSENSORIOC_G_SENSOR_MODE:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_G_SENSOR_MODE\n",
                    __func__);

            ret = ov5695_get_sensor_mode(ov5695, arg);
            break;

        case VVSENSORIOC_S_SENSOR_MODE:
            ret = ov5695_set_sensor_mode(ov5695, arg);
            break;

        case VVSENSORIOC_S_STREAM:
            USER_TO_KERNEL(int);
            ret = ov5695_s_stream(&ov5695->subdev, *(int*)arg);
            break;

        case VVSENSORIOC_S_EXP:
            USER_TO_KERNEL(u32);
            ret = ov5695_set_exp(ov5695, *(u32*)arg);
            break;

        case VVSENSORIOC_S_GAIN:
            USER_TO_KERNEL(u32);
            ret = ov5695_set_gain(ov5695, *(u32*)arg);
            break;

        case VVSENSORIOC_S_WB:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_S_WB\n",
                    __func__);

            USER_TO_KERNEL(struct sensor_white_balance_s);
            ret = ov5695_set_wb(ov5695, arg);

            break;

        case VVSENSORIOC_S_FPS:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_S_FPS\n",
                    __func__);

            USER_TO_KERNEL(u32);
            ret = ov5695_set_fps(ov5695, *(u32*)arg);
            break;

        case VVSENSORIOC_G_FPS:
            dev_dbg(&ov5695->i2c_client->dev, "%s: VVSENSORIOC_G_FPS\n",
                    __func__);

            USER_TO_KERNEL(u32);
            ret = ov5695_get_fps(ov5695, (u32*)arg);
            KERNEL_TO_USER(u32);
            break;

        case VVSENSORIOC_S_TEST_PATTERN:
            ret = ov5695_set_test_pattern_vvcam(ov5695, arg);
            break;

        default:
            dev_err(&ov5695->i2c_client->dev, "%s: Unknown cmd=%d\n", __func__,
                    cmd);
            ret = -EINVAL;
            break;
    }

    return 0;
}

static int ov5695_initialize_controls(struct ov5695* ov5695)
{
    const struct ov5695_mode* mode;
    struct v4l2_ctrl_handler* handler;
    struct v4l2_ctrl* ctrl;
    s64 exposure_max, vblank_def;
    u32 h_blank;
    int ret;

    handler = &ov5695->ctrl_handler;
    mode    = ov5695->legacy_mode;
    ret     = v4l2_ctrl_handler_init(handler, 8);
    if (ret)
        return ret;
    handler->lock = &ov5695->mutex;

    ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ, 0, 0,
                                  link_freq_menu_items);
    if (ctrl)
        ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

    v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE, 0, OV5695_PIXEL_RATE,
                      1, OV5695_PIXEL_RATE);

    if (handler->error) {
        ret = handler->error;
        dev_err(&ov5695->i2c_client->dev, "Failed to init pixel rate(%d)\n",
                ret);
        goto err_free_handler;
    }

    h_blank        = mode->hts_def - ov5695->cur_mode.size.width;
    ov5695->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK, h_blank,
                                       h_blank, 1, h_blank);
    if (ov5695->hblank)
        ov5695->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

    vblank_def = ov5695->cur_mode.ae_info.def_frm_len_lines -
                 ov5695->cur_mode.size.height;
    ov5695->vblank = v4l2_ctrl_new_std(
        handler, &ov5695_ctrl_ops, V4L2_CID_VBLANK, vblank_def,
        OV5695_VTS_MAX - ov5695->cur_mode.size.height, 1, vblank_def);

    if (handler->error) {
        ret = handler->error;
        dev_err(&ov5695->i2c_client->dev, "Failed to init vblank(%d)\n", ret);

        goto err_free_handler;
    }

    exposure_max     = ov5695->cur_mode.ae_info.max_integration_line;
    ov5695->exposure = v4l2_ctrl_new_std(
        handler, &ov5695_ctrl_ops, V4L2_CID_EXPOSURE, OV5695_EXPOSURE_MIN,
        exposure_max, OV5695_EXPOSURE_STEP, mode->exp_def);

    if (handler->error) {
        ret = handler->error;
        dev_err(&ov5695->i2c_client->dev, "Failed to init exposure(%d)\n", ret);
        goto err_free_handler;
    }

    ov5695->anal_gain = v4l2_ctrl_new_std(
        handler, &ov5695_ctrl_ops, V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
        ANALOG_GAIN_MAX, ANALOG_GAIN_STEP, ANALOG_GAIN_DEFAULT);

    if (handler->error) {
        ret = handler->error;
        dev_err(&ov5695->i2c_client->dev, "Failed to init analog gain(%d)\n",
                ret);
        goto err_free_handler;
    }

    /* Digital gain */
    ov5695->digi_gain = v4l2_ctrl_new_std(
        handler, &ov5695_ctrl_ops, V4L2_CID_DIGITAL_GAIN, OV5695_DIGI_GAIN_MIN,
        OV5695_DIGI_GAIN_MAX, OV5695_DIGI_GAIN_STEP, OV5695_DIGI_GAIN_DEFAULT);

    if (handler->error) {
        ret = handler->error;
        dev_err(&ov5695->i2c_client->dev, "Failed to init digi gain(%d)\n",
                ret);
        goto err_free_handler;
    }

    ov5695->test_pattern = v4l2_ctrl_new_std_menu_items(
        handler, &ov5695_ctrl_ops, V4L2_CID_TEST_PATTERN,
        ARRAY_SIZE(ov5695_test_pattern_menu) - 1, 0, 0,
        ov5695_test_pattern_menu);

    if (handler->error) {
        ret = handler->error;
        dev_err(&ov5695->i2c_client->dev, "Failed to init menu(%d)\n", ret);
        goto err_free_handler;
    }

    ov5695->subdev.ctrl_handler = handler;

    return 0;

err_free_handler:
    v4l2_ctrl_handler_free(handler);

    return ret;
}

static void print_version(struct ov5695* ov5695)
{
    struct device* dev = &ov5695->i2c_client->dev;

    dev_info(dev, "Module version %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR,
             VERSION_PATCH);
}

static int ov5695_check_sensor_id(struct ov5695* ov5695,
                                  struct i2c_client* client)
{
    struct device* dev = &ov5695->i2c_client->dev;
    u32 id             = 0;
    int ret;

    ret = ov5695_read_reg(client, OV5695_REG_CHIP_ID, OV5695_REG_VALUE_24BIT,
                          &id);
    if (id != CHIP_ID) {
        dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
        return ret;
    }

    dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

    return 0;
}

static int ov5695_configure_regulators(struct ov5695* ov5695)
{
    int i;

    for (i = 0; i < OV5695_NUM_SUPPLIES; i++)
        ov5695->supplies[i].supply = ov5695_supply_names[i];

    return regulator_bulk_get(&ov5695->i2c_client->dev, OV5695_NUM_SUPPLIES,
                              ov5695->supplies);
}

static int ov5695_retrieve_capture_properties(
    struct ov5695* sensor, struct ov5695_capture_properties* ocp)
{
    struct device* dev = &sensor->i2c_client->dev;
    __u64 mlf          = 0;
    __u64 mpf          = 0;
    __u64 mdr          = 0;

    struct device_node* ep;
    u32 data_lanes[4];
    int ret;

    memset(data_lanes, 0, sizeof(data_lanes));

    /*
    * Collecting the information about limits of capture path
	* has been centralized to the sensor
	* also into the sensor endpoint itself.
	*/

    ep = of_graph_get_next_endpoint(dev->of_node, NULL);
    if (!ep) {
        dev_err(dev, "missing endpoint node\n");
        return -ENODEV;
    }

    ret = fwnode_property_read_u64(of_fwnode_handle(ep), "max-lane-frequency",
                                   &mlf);

    if (ret || mlf == 0) {
        dev_dbg(dev, "no limit for max-lane-frequency\n");
    }

    ret = fwnode_property_read_u64(of_fwnode_handle(ep), "max-pixel-frequency",
                                   &mpf);

    if (ret || mpf == 0) {
        dev_dbg(dev, "no limit for max-pixel-frequency\n");
    }

    sensor->num_lanes =
        fwnode_property_count_u32(of_fwnode_handle(ep), "data-lanes");

    if (sensor->num_lanes <= 0) {
        dev_warn(dev, "Incorrect data-lanes set, setting to default 2");
        sensor->num_lanes = 2;
    }

    dev_info(dev, "num_lanes: %d\n", sensor->num_lanes);

    ocp->max_lane_frequency  = mlf;
    ocp->max_pixel_frequency = mpf;
    ocp->max_data_rate       = mdr;

    return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
static int ov5695_probe(struct i2c_client* client,
                        const struct i2c_device_id* id)
#else
static int ov5695_probe(struct i2c_client* client)
#endif
{
    struct device* dev = &client->dev;
    struct ov5695* ov5695;
    struct v4l2_subdev* sd;
    int ret, i;

    dev_info(dev, "Probing OV5695 sensor\n");

    ov5695 = devm_kzalloc(dev, sizeof(*ov5695), GFP_KERNEL);
    if (!ov5695)
        return -ENOMEM;

    ov5695->i2c_client = client;

    // Legacy mode info. TODO: Move it to  sensor->cur_mode
    ov5695->legacy_mode = &supported_modes[0];

    memcpy(&ov5695->cur_mode, &ov5695_mode_info[0],
           sizeof(struct vvcam_mode_info_s));

    ov5695->xvclk = devm_clk_get(dev, "xvclk");

    if (IS_ERR(ov5695->xvclk)) {
        dev_err(dev, "Failed to get xvclk\n");
        return -EINVAL;
    }

    ret = of_property_read_u32(dev->of_node, "csi_id", &ov5695->csi_id);

    if (ret < 0) {
        dev_warn(dev, "Failed to get csi_id from device tree\n");
        ov5695->csi_id = 0;
    }

    ret = ov5695_retrieve_capture_properties(ov5695, &ov5695->ocp);

    if (ret) {
        dev_warn(dev, "retrive capture properties error\n");
    }

    for (i = 0; i < ARRAY_SIZE(ov5695_mode_info); i++) {
        ov5695_mode_info[i].mipi_info.mipi_lane = ov5695->num_lanes;
    }

    ret = clk_set_rate(ov5695->xvclk, OV5695_XVCLK_FREQ);

    if (ret < 0) {
        dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
        return ret;
    }

    if (clk_get_rate(ov5695->xvclk) != OV5695_XVCLK_FREQ) {
        dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
    }

    ov5695->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);

    if (IS_ERR(ov5695->reset_gpio)) {
        dev_warn(dev, "Failed to get reset-gpios, wont be able to "
                      "RESET/POWER_OFF sensor.\n");
        ov5695->reset_gpio = NULL;
    }

    ret = ov5695_configure_regulators(ov5695);
    if (ret) {
        dev_err(dev, "Failed to get power regulators\n");
        return ret;
    }

    mutex_init(&ov5695->mutex);

    sd = &ov5695->subdev;
    v4l2_i2c_subdev_init(sd, client, &ov5695_subdev_ops);

    ret = ov5695_initialize_controls(ov5695);
    if (ret)
        goto err_destroy_mutex;

    ret = __ov5695_power_on(ov5695);

    if (ret)
        goto err_free_handler;

    ret = ov5695_check_sensor_id(ov5695, client);

    if (ret)
        goto err_power_off;

    print_version(ov5695);

    // sd->internal_ops = &ov5695_internal_ops;
    sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

    ov5695->pad.flags   = MEDIA_PAD_FL_SOURCE;
    sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
    sd->entity.ops      = &ov5695_entity_ops;
    ret                 = media_entity_pads_init(&sd->entity, 1, &ov5695->pad);

    if (ret < 0)
        goto err_power_off;

    ret = v4l2_async_register_subdev_sensor(sd);

    if (ret) {
        dev_err(dev, "v4l2 async register subdev failed\n");
        goto err_clean_entity;
    }

    ov5695->wb.r_gain  = 0x0ff8;
    ov5695->wb.gb_gain = ov5695->wb.gr_gain = 0x067f;
    ov5695->wb.b_gain                       = 0x0fff;

    ret = ov5695_set_wb(ov5695, &ov5695->wb);

    if (ret) {
        dev_err(dev, "ov5695_set_wb failed\n");
        goto err_clean_entity;
    }

    pm_runtime_set_active(dev);
    pm_runtime_enable(dev);
    pm_runtime_idle(dev);

    dev_info(dev, "Probe: Success\n");

    return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
err_power_off:
    __ov5695_power_off(ov5695);
    // regulator_bulk_disable(OV5695_NUM_SUPPLIES, ov5695->supplies);
    // regulator_bulk_free(OV5695_NUM_SUPPLIES, ov5695->supplies);
err_free_handler:
    v4l2_ctrl_handler_free(&ov5695->ctrl_handler);
err_destroy_mutex:
    dev_err(dev, "ov5695_probe: Error exit with code %d\n", ret);
    mutex_destroy(&ov5695->mutex);

    return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
static int ov5695_remove(struct i2c_client* client)
#else
static void ov5695_remove(struct i2c_client* client)
#endif
{
    struct v4l2_subdev* sd = i2c_get_clientdata(client);
    struct ov5695* ov5695  = to_ov5695(sd);

    dev_info(&ov5695->i2c_client->dev, "ov5695 module removed\n");

    v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
    v4l2_ctrl_handler_free(&ov5695->ctrl_handler);
    mutex_destroy(&ov5695->mutex);

    pm_runtime_disable(&client->dev);
    if (!pm_runtime_status_suspended(&client->dev))
        __ov5695_power_off(ov5695);
    pm_runtime_set_suspended(&client->dev);

    regulator_bulk_disable(OV5695_NUM_SUPPLIES, ov5695->supplies);
    regulator_bulk_free(OV5695_NUM_SUPPLIES, ov5695->supplies);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
    return 0;
#endif
}

static const struct i2c_device_id ov5695_id[] = {
    {"ov5695", 0},
    {},
};
MODULE_DEVICE_TABLE(i2c, ov5695_id);

static const struct of_device_id ov5695_of_match[] = {
    {.compatible = "ovti,ov5695"},
    {},
};
MODULE_DEVICE_TABLE(of, ov5695_of_match);

static struct i2c_driver ov5695_i2c_driver = {
    .driver =
        {
            .owner          = THIS_MODULE,
            .name           = "ov5695",
            .pm             = &ov5695_pm_ops,
            .of_match_table = of_match_ptr(ov5695_of_match),
        },
    .probe    = &ov5695_probe,
    .remove   = &ov5695_remove,
    .id_table = ov5695_id,
};

module_i2c_driver(ov5695_i2c_driver);

MODULE_DESCRIPTION("OmniVision ov5695 vvcam sensor driver");
MODULE_LICENSE("GPL v2");
