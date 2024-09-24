#ifndef OV5695_2592X1944_REGS_H_
#define OV5695_2592X1944_REGS_H_

#include "ov5695.h"
#include "vvsensor.h"

// clang-format off

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 740(0x2e4)
 * framelength 2024(0x7e8)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static struct vvcam_sccb_data_s __maybe_unused ov5695_2592x1944_regs[] = {
	{OV5695_REG_LONG_EXPO_H, 0x7e},
	{0x366e, 0x18},
	{OV5695_REG_TIMING_X_START, 0x00},
	{0x3801, 0x00},
	{OV5695_REG_TIMING_Y_START, 0x00},
	{0x3803, 0x04},
	{OV5695_REG_TIMING_X_END, 0x0a},
	{0x3805, 0x3f},
	{OV5695_REG_TIMING_Y_END, 0x07},
	{0x3807, 0xab},
	{OV5695_REG_TIMING_X_OUTPUT_SIZE, 0x0a},
	{0x3809, 0x20},
	{OV5695_REG_TIMING_Y_OUTPUT_SIZE, 0x07},
	{0x380b, 0x98},
	{OV5695_REG_TIMING_HTS, 0x02},
	{0x380d, 0xe4},
	{OV5695_REG_TIMING_VTS, 0x07},
	{0x380f, 0xe8},
	{0x3811, 0x06},
	{0x3813, 0x08},
	{OV5695_REG_TIMING_X_ODD_INC, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0x88},
	{0x3821, 0x00},
	{OV5695_REG_SYNC_REG1, 0x00},
	{OV5695_REG_BLC_CTRL08, 0x04},
	{OV5695_REG_BLC_CTRL09, 0x13},
};

// clang-format off

#endif /* OV5695_2592X1944_REGS_H_ */
