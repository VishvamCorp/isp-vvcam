#ifndef OV5695_2048X1856_REGS_H
#define OV5695_2048X1856_REGS_H

#include "ov5695.h"
#include "vvsensor.h"

// clang-format off

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 740(0x2e4)
 * framelength 2028(0x7ec)
 *
 * grabwindow format 10:9
 * grabwindow_width 2048
 * grabwindow_height 1856
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static struct vvcam_sccb_data_s ov5695_2048x1856_regs[] = {
	{OV5695_REG_LONG_EXPO_H, 0x45}, // 1104.0
	{OV5695_REG_LONG_EXPO_L, 0x00},
	{0x366e, 0x18},
	{OV5695_REG_TIMING_X_START, 0x01}, // 368
	{0x3801, 0x70},
	{OV5695_REG_TIMING_Y_START, 0x00}, // 64
	{0x3803, 0x40},
	{OV5695_REG_TIMING_X_END, 0x09}, // 2423 (2048 + 368 + 8 - 1) (2048 plus 8 bytes align)
	{0x3805, 0x77},
	{OV5695_REG_TIMING_Y_END, 0x07}, // 1927 (1856 + 64 + 8 - 1)
	{0x3807, 0x87},
	{OV5695_REG_TIMING_X_OUTPUT_SIZE, 0x08}, // 2048
	{0x3809, 0x00},
	{OV5695_REG_TIMING_Y_OUTPUT_SIZE, 0x07}, // 1856
	{0x380b, 0x40},
	{OV5695_REG_TIMING_HTS, 0x02}, // 740
	{0x380d, 0xe4},
	{OV5695_REG_TIMING_VTS, 0x07}, // 2028
	{0x380f, 0xec},
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

#endif   // OV5695_2048X1856_REGS_H
