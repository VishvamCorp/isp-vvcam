#ifndef OV5695_1280X720_REGS_H
#define OV5695_1280X720_REGS_H

#include "vvsensor.h"

// clang-format off

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 672(0x2a0)
 * framelength 2232(0x8b8)
 * grabwindow_width 1280
 * grabwindow_height 720
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static struct vvcam_sccb_data_s ov5695_1280x720_regs[] = {
	{0x3501, 0x45},
	{0x366e, 0x0c},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x01},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x06},
	{0x3807, 0xaf},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x02},
	{0x380b, 0xd0},
	{0x380c, 0x02},
	{0x380d, 0xa0},
	{0x380e, 0x08},
	{0x380f, 0xb8},
	{0x3811, 0x06},
	{0x3813, 0x02},
	{0x3814, 0x03},
	{0x3816, 0x03},
	{0x3817, 0x01},
	{0x3820, 0x8b},
	{0x3821, 0x01},
	{0x4501, 0x00},
	{0x4008, 0x02},
	{0x4009, 0x09},
};

// clang-format on

#endif // OV5695_1280X720_REGS_H
