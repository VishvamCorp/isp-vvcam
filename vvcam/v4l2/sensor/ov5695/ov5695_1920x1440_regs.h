#ifndef OV5695_REGS_1920X1440_H
#define OV5695_REGS_1920X1440_H
#include "ov5695.h"
#include "vvsensor.h"

// clang-format off

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 672(0x2a0)
 * framelength 2232(0x8b8)
 * grabwindow_width 1920
 * grabwindow_height 1440
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static struct vvcam_sccb_data_s ov5695_1920x1440_regs[] = {
	{OV5695_REG_LONG_EXPO_H, 0x45}, // 1104.0
	{OV5695_REG_LONG_EXPO_L, 0x00},
	{0x366e, 0x18},
	{OV5695_REG_TIMING_X_START, 0x01}, // 336
	{0x3801, 0x50},
	{OV5695_REG_TIMING_Y_START, 0x01}, // 256
	{0x3803, 0x00},
	{OV5695_REG_TIMING_X_END, 0x08}, // 2287
	{0x3805, 0xef},
	{OV5695_REG_TIMING_Y_END, 0x07}, // 1703
	{0x3807, 0x33},
	{OV5695_REG_TIMING_X_OUTPUT_SIZE, 0x07}, // 1920
	{0x3809, 0x80},
	{OV5695_REG_TIMING_Y_OUTPUT_SIZE, 0x05}, // 1440
	{0x380b, 0xa0},
	{OV5695_REG_TIMING_HTS, 0x02}, // 672
	{0x380d, 0xa0},
	{OV5695_REG_TIMING_VTS, 0x08}, // 2232
	{0x380f, 0xb8},
	{OV5695_REG_TIMING_ISP_X_WIN, 0x00}, // 6
	{0x3811, 0x06},
	{OV5695_REG_TIMING_ISP_Y_WIN, 0x00}, // 4
	{0x3813, 0x04},
	{OV5695_REG_TIMING_X_ODD_INC, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0x88},
	{0x3821, 0x00},
	{OV5695_REG_SYNC_REG1, 0x00},
	{OV5695_REG_BLC_CTRL08, 0x04}, // Why we change this setting??
	{OV5695_REG_BLC_CTRL09, 0x13}, // Why we change this setting??
};

// clang-format on

#endif /* OV5695_REGS_1920X1440_H */
