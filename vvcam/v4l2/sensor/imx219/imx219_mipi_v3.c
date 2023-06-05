/*
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2018 NXP
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_graph.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "vvsensor.h"

#include <asm/unaligned.h>
#include <linux/pm_runtime.h>

#include "imx219_regs_1080p.h"
#include "imx219_regs_4k.h"

#define IMX219_REG_VALUE_08BIT		1
#define IMX219_REG_VALUE_16BIT		2

#define IMX219_REG_MODE_SELECT		0x0100
#define IMX219_MODE_STANDBY		0x00
#define IMX219_MODE_STREAMING		0x01

/* Chip ID */
#define IMX219_REG_CHIP_ID		0x0000
#define IMX219_CHIP_ID			0x0219

/* External clock frequency is 24.0M */
#define IMX219_XCLK_FREQ		24000000

/* Pixel rate is fixed at 182.4M for all the modes */
#define IMX219_PIXEL_RATE		182400000

#define IMX219_DEFAULT_LINK_FREQ	456000000

/* V_TIMING internal */
#define IMX219_REG_VTS			0x0160
#define IMX219_VTS_15FPS		0x0dc6
#define IMX219_VTS_30FPS_1080P		0x06e3
#define IMX219_VTS_30FPS_BINNED		0x06e3
#define IMX219_VTS_30FPS_640x480	0x06e3
#define IMX219_VTS_MAX			0xffff

#define IMX219_VBLANK_MIN		4

/*Frame Length Line*/
#define IMX219_FLL_MIN			0x08a6
#define IMX219_FLL_MAX			0xffff
#define IMX219_FLL_STEP			1
#define IMX219_FLL_DEFAULT		0x0c98

/* HBLANK control - read only */
#define IMX219_PPL_DEFAULT		3448

/* Exposure control */
#define IMX219_REG_EXPOSURE		0x015a
#define IMX219_EXPOSURE_MIN		4
#define IMX219_EXPOSURE_STEP		1
#define IMX219_EXPOSURE_DEFAULT		0x640
#define IMX219_EXPOSURE_MAX		65535

/* Analog gain control */
#define IMX219_REG_ANALOG_GAIN		0x0157
#define IMX219_ANA_GAIN_MIN		0
#define IMX219_ANA_GAIN_MAX		232
#define IMX219_ANA_GAIN_STEP		1
#define IMX219_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define IMX219_REG_DIGITAL_GAIN		0x0158
#define IMX219_DGTL_GAIN_MIN		0x0100
#define IMX219_DGTL_GAIN_MAX		0x0fff
#define IMX219_DGTL_GAIN_DEFAULT	0x0100
#define IMX219_DGTL_GAIN_STEP		1

#define IMX219_REG_ORIENTATION		0x0172

/* Test Pattern Control */
#define IMX219_REG_TEST_PATTERN		0x0600
#define IMX219_TEST_PATTERN_DISABLE	0
#define IMX219_TEST_PATTERN_SOLID_COLOR	1
#define IMX219_TEST_PATTERN_COLOR_BARS	2
#define IMX219_TEST_PATTERN_GREY_COLOR	3
#define IMX219_TEST_PATTERN_PN9		4

/* Test pattern colour components */
#define IMX219_REG_TESTP_RED		0x0602
#define IMX219_REG_TESTP_GREENR		0x0604
#define IMX219_REG_TESTP_BLUE		0x0606
#define IMX219_REG_TESTP_GREENB		0x0608
#define IMX219_TESTP_COLOUR_MIN		0
#define IMX219_TESTP_COLOUR_MAX		0x03ff
#define IMX219_TESTP_COLOUR_STEP	1
#define IMX219_TESTP_RED_DEFAULT	IMX219_TESTP_COLOUR_MAX
#define IMX219_TESTP_GREENR_DEFAULT	0
#define IMX219_TESTP_BLUE_DEFAULT	0
#define IMX219_TESTP_GREENB_DEFAULT	0

/* IMX219 native and active pixel array size. */
#define IMX219_NATIVE_WIDTH		3296U
#define IMX219_NATIVE_HEIGHT		2480U
#define IMX219_PIXEL_ARRAY_LEFT		8U
#define IMX219_PIXEL_ARRAY_TOP		8U
#define IMX219_PIXEL_ARRAY_WIDTH	3280U
#define IMX219_PIXEL_ARRAY_HEIGHT	2464U

#define IMX219_MIN_FPS 15
#define IMX219_MAX_FPS 30
#define IMX219_DEFAULT_FPS 15

#define IMX219_XCLR_MIN_DELAY_US	6200
#define IMX219_XCLR_DELAY_RANGE_US	1000



#define IMX219_XCLK_MIN 6000000
#define IMX219_XCLK_MAX 24000000

#define IMX219_SENS_PAD_SOURCE	0
#define IMX219_SENS_PADS_NUM	1

//#define IMX219_RESERVE_ID 0X2770
#define DCG_CONVERSION_GAIN 11


// gain reg 
#define ANA_GAIN_GLOBAL_A		0x157
#define DIG_GAIN_GLOBAL_A_UP   0x0158 
#define DIG_GAIN_GLOBAL_A_LOW  0x0159
// exp reg
#define COARSE_INTEGRATION_TIME_A_UP  0x15A
#define COARSE_INTEGRATION_TIME_A_LOW  0x15B


static const int imx219_test_pattern_val[] = {
	IMX219_TEST_PATTERN_DISABLE,
	IMX219_TEST_PATTERN_COLOR_BARS,
	IMX219_TEST_PATTERN_SOLID_COLOR,
	IMX219_TEST_PATTERN_GREY_COLOR,
	IMX219_TEST_PATTERN_PN9,
};

static const struct vvcam_sccb_data_s raw8_framefmt_regs[] = {
	{0x018c, 0x08},
	{0x018d, 0x08},
	{0x0309, 0x08},
};

static const struct vvcam_sccb_data_s raw10_framefmt_regs[] = {
	{0x018c, 0x0a},
	{0x018d, 0x0a},
	{0x0309, 0x0a},
	// vts
	{0x0160, 0x06},
	{0x0161, 0xe4},
};



#define client_to_imx219(client)\
	container_of(i2c_get_clientdata(client), struct imx219, subdev)

struct imx219_capture_properties {
	__u64 max_lane_frequency;
	__u64 max_pixel_frequency;
	__u64 max_data_rate;
};

/* Mode : resolution and related config&values */
struct imx219_mode_crop {
	/* Frame width */
	unsigned int width;
	/* Frame height */
	unsigned int height;

	/* Analog crop rectangle. */
	struct v4l2_rect crop;

	/* V-timing */
	unsigned int vts_def;
};



struct imx219 {
	struct i2c_client *i2c_client;
	struct regulator *io_regulator;
	struct regulator *core_regulator;
	struct regulator *analog_regulator;
	unsigned int pwn_gpio;
	unsigned int rst_gpio;
	unsigned int mclk;
	unsigned int mclk_source;
	struct clk *xclk;
	u32 xclk_freq;
	
	unsigned int csi_id;
	struct imx219_capture_properties ocp;

	struct v4l2_subdev subdev;
	struct media_pad pads[IMX219_SENS_PADS_NUM];

	struct v4l2_mbus_framefmt format;
	vvcam_mode_info_t cur_mode;
	sensor_blc_t blc;
	sensor_white_balance_t wb;
	struct mutex lock;
	u32 stream_status;
	u32 resume_status;

	/* V4L2 Controls */
	
	const struct imx219_mode_crop *pmode_crop;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	*/
	struct mutex mutex;
	
};



static const struct imx219_mode_crop supported_modes_crop[] = {
	{
		/* 1080P 30fps cropped */
		.width = 1920,
		.height = 1080,
		.crop = {
			.left = 688,
			.top = 700,
			.width = 1920,
			.height = 1080
			},
		.vts_def = IMX219_VTS_30FPS_1080P,
	},
	{
		/* 8MPix 15fps mode */
		.width = 3280,
		.height = 2464,
		.crop = {
			.left = IMX219_PIXEL_ARRAY_LEFT,
			.top = IMX219_PIXEL_ARRAY_TOP,
			.width = 3280,
			.height = 2464
		},
		.vts_def = IMX219_VTS_15FPS,
	},

};

static struct vvcam_mode_info_s pimx219_mode_info[] = {
	{
		.index          = 0,
		.size           = {
			.bounds_width  = 1920,
			.bounds_height = 1080,
			.top           = 0,
			.left          = 0,
			.width         = 1920,
			.height        = 1080,
		},
		.hdr_mode       = SENSOR_MODE_LINEAR,
		.bit_width      = 10,
		.data_compress  = {
			.enable = 0,
		},
		.bayer_pattern = BAYER_RGGB,
		.ae_info = {
			.def_frm_len_lines     = IMX219_VTS_30FPS_1080P,
			.curr_frm_len_lines    = IMX219_VTS_30FPS_1080P,
/*
	{0x0301, 0x05},  // VTPXCK_DIV
	{0x0303, 0x01},
	{0x0304, 0x03},  // Pre-Div1
	{0x0305, 0x03},

	{0x0306, 0x00},  // PLL1 
	{0x0307, 0x39},

P81
PIX_CLK = 24M * Pre-Div1 * PLL1 * VTPXCK_DIV
        = 24/3 * 0x39 /5
        = 91.2M 

Time Per Line [sec] = 0xd78 / (2* 91.2 * 1000 *1000 )
                    = 1.8903508771929824e-05
*/
			.one_line_exp_time_ns  = 18903,

/*
(Frame Length)  = 1/ Time per Line[sec] * Frame rate [frame/s] 
                =  1/ (1.8903508771929824e-05 * 30)
                = 0x6e4   // IMX219_VTS_30FPS_1080P
*/
			.max_integration_line  = /*0x466*/0x6e4 - 4,
			.min_integration_line  = 1,

			.max_again			   = 10.66 * (1 << SENSOR_FIX_FRACBITS),
			.min_again			   = 1 * (1 << SENSOR_FIX_FRACBITS),
			.max_dgain			   = 15.85 * (1 << SENSOR_FIX_FRACBITS),
			.min_dgain			   = 1 * (1 << SENSOR_FIX_FRACBITS),
			.gain_step			   = 1,
			
			.start_exposure        = 3 * 400 * 1024,
			.cur_fps               = 30 * 1024,
			.max_fps               = 30 * 1024,
			.min_fps               = 5 * 1024,
			.min_afps              = 5 * 1024,
			.int_update_delay_frm  = 1,
			.gain_update_delay_frm = 1,
		},
		.mipi_info = {
			.mipi_lane = 2,
		},
		.preg_data      = imx219_init_setting_1080p,
		.reg_data_count = ARRAY_SIZE(imx219_init_setting_1080p),
	},
	{
		.index			= 1,
		.size           = {
			.bounds_width  = 3280,
			.bounds_height = 2464,
			.top           = 0,
			.left          = 0,
			.width         = 3280,
			.height        = 2464,
		},
		.hdr_mode		= SENSOR_MODE_LINEAR,
		.bit_width		= 10,
		.data_compress	= {
			.enable = 0,
		},
		.bayer_pattern = BAYER_RGGB,
		.ae_info = {
			.def_frm_len_lines	   = IMX219_VTS_15FPS,
			.curr_frm_len_lines    = IMX219_VTS_15FPS,
			.one_line_exp_time_ns  = 18903,

			.max_integration_line  = 0x466 - 4,
			.min_integration_line  = 1,

			.max_again			   = 10.6 * (1 << SENSOR_FIX_FRACBITS),
			.min_again			   = 1 * (1 << SENSOR_FIX_FRACBITS),
			.max_dgain			   = 15.9 * (1 << SENSOR_FIX_FRACBITS),
			.min_dgain			   = 1 * (1 << SENSOR_FIX_FRACBITS),
			.gain_step			   = 1,
			
			.start_exposure 	   = 3 * 400 * 1024,
			.cur_fps			   = 30 * 1024,
			.max_fps			   = 30 * 1024,
			.min_fps			   = 5 * 1024,
			.min_afps			   = 5 * 1024,
			.int_update_delay_frm  = 1,
			.gain_update_delay_frm = 1,
		},
		.mipi_info = {
			.mipi_lane = 2,
		},
		.preg_data		= imx219_init_setting_3280x2464,
		.reg_data_count = ARRAY_SIZE(imx219_init_setting_3280x2464),
	}
};

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 support_codes[] = {
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,

	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SBGGR8_1X8,
};


int imx219_get_clk(struct imx219 *sensor, void *clk)
{
	struct vvcam_clk_s vvcam_clk;
	int ret = 0;
	vvcam_clk.sensor_mclk = IMX219_XCLK_MAX;//clk_get_rate(sensor->sensor_clk);
	vvcam_clk.csi_max_pixel_clk = IMX219_XCLK_MAX;//sensor->ocp.max_pixel_frequency;
	ret = copy_to_user(clk, &vvcam_clk, sizeof(struct vvcam_clk_s));
	if (ret != 0)
		ret = -EINVAL;
	return ret;
}

static int imx219_power_on(struct imx219 *sensor)
{
	int ret = 0;


	pr_info("enter %s \n", __func__);

	clk_prepare_enable(sensor->xclk);

	return ret;
}

static int imx219_power_off(struct imx219 *sensor)
{
	pr_info("enter %s \n", __func__);
	clk_disable_unprepare(sensor->xclk);
	return 0;
}

static int imx219_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *sensor = client_to_imx219(client);

	pr_debug("enter %s\n", __func__);
	if (on)
		imx219_power_on(sensor);
	else
		imx219_power_off(sensor);

	return 0;
}



static int imx219_read_reg(struct imx219 *sensor, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	int ret;

	if (len > 4)
		return -EINVAL;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

#if 0
static int imx219_write_reg(struct imx219 *sensor, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = sensor->i2c_client;
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;
/*static int os08a20_write_reg(struct os08a20 *sensor, u16 reg, u8 val)
{
	struct device *dev = &sensor->i2c_client->dev;
	u8 au8Buf[3] = { 0 };

	au8Buf[0] = reg >> 8;
	au8Buf[1] = reg & 0xff;
	au8Buf[2] = val;

	if (i2c_master_send(sensor->i2c_client, au8Buf, 3) < 0) {
		dev_err(dev, "Write reg error: reg=%x, val=%x\n", reg, val);
		return -1;
	}

	return 0;
}
*/

	return 0;
}
#else
static int imx219_write_reg(struct imx219 *sensor, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = sensor->i2c_client;
	u8 au8Buf[3] = { 0 };

	au8Buf[0] = reg >> 8;
	au8Buf[1] = reg & 0xff;
	au8Buf[2] = val;

	if (i2c_master_send(client, au8Buf, 3) < 0) {
		// dev_err(dev, "Write reg error: reg=%x, val=%x\n", reg, val);
		return -1;
	}
	return 0;
}
#endif

/* Write a list of registers */
static int imx219_write_regs(struct imx219 *sensor,
				const struct vvcam_sccb_data_s *sensor_reg_cfg , u32 len)
{
	struct i2c_client *client = sensor->i2c_client;
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx219_write_reg(sensor, sensor_reg_cfg[i].addr, 1, sensor_reg_cfg[i].data);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    sensor_reg_cfg[i].addr, ret);

			return ret;
		}
	}

	return 0;
}

static int imx219_query_capability(struct imx219 *sensor, void *arg)
{
	struct v4l2_capability *pcap = (struct v4l2_capability *)arg;

	strcpy((char *)pcap->driver, "imx219");
	sprintf((char *)pcap->bus_info, "csi%d",sensor->csi_id);

	if(sensor->i2c_client->adapter) {
		pcap->bus_info[VVCAM_CAP_BUS_INFO_I2C_ADAPTER_NR_POS] =
			(__u8)sensor->i2c_client->adapter->nr;
	} else {
		pcap->bus_info[VVCAM_CAP_BUS_INFO_I2C_ADAPTER_NR_POS] = 0xFF;
	}
	return 0;
}

static int imx219_query_supports(struct imx219 *sensor, void* parry)
{
	int ret = 0;
	struct vvcam_mode_info_array_s *psensor_mode_arry = parry;
	uint32_t support_counts = ARRAY_SIZE(pimx219_mode_info);

	ret = copy_to_user(&psensor_mode_arry->count, &support_counts, sizeof(support_counts));
	ret |= copy_to_user(&psensor_mode_arry->modes, pimx219_mode_info,
			   sizeof(pimx219_mode_info));
	if (ret != 0)
		ret = -ENOMEM;
	return ret;

}


static int imx219_get_sensor_id(struct imx219 *sensor, void* pchip_id)
{
	int ret = 0;
	u16 chip_id;
	u32 val;
	ret = imx219_read_reg(sensor, IMX219_REG_CHIP_ID,
			      IMX219_REG_VALUE_16BIT, &val);

	if (ret != 0){
		return ret;
	}
	
	chip_id = val;
	ret = copy_to_user(pchip_id, &chip_id, sizeof(u16));
	if (ret != 0)
		ret = -ENOMEM;
	return ret;
}

static int imx219_get_reserve_id(struct imx219 *sensor, void* preserve_id)
{
	int ret = 0;
	u16 reserve_id = IMX219_CHIP_ID;
	ret = copy_to_user(preserve_id, &reserve_id, sizeof(u16));
	if (ret != 0)
		ret = -ENOMEM;
	return ret;
}

static int imx219_get_sensor_mode(struct imx219 *sensor, void* pmode)
{
	int ret = 0;
	ret = copy_to_user(pmode, &sensor->cur_mode,
		sizeof(struct vvcam_mode_info_s));
	if (ret != 0)
		ret = -ENOMEM;
	return ret;
}

static int imx219_set_sensor_mode(struct imx219 *sensor, void* pmode)
{
	int ret = 0;
	int i = 0;
	struct vvcam_mode_info_s sensor_mode;
	ret = copy_from_user(&sensor_mode, pmode,
		sizeof(struct vvcam_mode_info_s));
	if (ret != 0)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(pimx219_mode_info); i++) {
		if (pimx219_mode_info[i].index == sensor_mode.index) {
			memcpy(&sensor->cur_mode, &pimx219_mode_info[i],
				sizeof(struct vvcam_mode_info_s));
			return 0;
		}
	}

	return -ENXIO;
}

static int imx219_set_lexp(struct imx219 *sensor, u32 exp)
{
	//pr_info("%s exp=0x%x\n",__func__,exp);
	return 0;
}

static int imx219_set_exp(struct imx219 *sensor, u32 exp)
{
/*
#define COARSE_INTEGRATION_TIME_A_UP  0x15A
#define COARSE_INTEGRATION_TIME_A_LOW  0x15B
*/
	int ret = 0;
#if 1
	//pr_info("%s exp=0x%x\n",__func__,exp);
	ret |= imx219_write_reg(sensor, COARSE_INTEGRATION_TIME_A_UP, 1, (exp >> 8) & 0xff);
	ret |= imx219_write_reg(sensor, COARSE_INTEGRATION_TIME_A_LOW,1, exp & 0xff);
#endif
	return ret;
}


static int imx219_set_vsexp(struct imx219 *sensor, u32 exp)
{
	int ret = 0;
	//pr_info("%s exp=0x%x\n",__func__,exp);
	return ret;
}

static int imx219_set_lgain(struct imx219 *sensor, u32 gain)
{
	int ret = 0;
	//pr_info("%s gain=0x%x\n",__func__,gain);
	return ret;
}

static int imx219_set_gain(struct imx219 *sensor, u32 gain)
{
/*
#define ANA_GAIN_GLOBAL_A		0x157
#define DIG_GAIN_GLOBAL_A_UP   0x0158 
#define DIG_GAIN_GLOBAL_A_LOW 0x0159
*/
	int ret = 0;

#if 1
	u32 again, dgain; 

	//pr_info("%s gain=0x%x\n",__func__,gain);
	if (gain > (10 * (1 << SENSOR_FIX_FRACBITS))) {
		again = 10 * (1 << SENSOR_FIX_FRACBITS);
		dgain = gain / 10;
	}
	else {
		again = gain;
		dgain = 1 << SENSOR_FIX_FRACBITS;
	}

	// set dgain
	if (dgain == (1 << SENSOR_FIX_FRACBITS)) {
		imx219_write_reg(sensor, DIG_GAIN_GLOBAL_A_UP, 1, 1);
		imx219_write_reg(sensor, DIG_GAIN_GLOBAL_A_LOW, 1, 0);
	}
	else {
		/*gain * 256 = (up + low/256) * 256
		gain * (1 << SENSOR_FIX_FRACBITS) /4 = 256 * up + low 
		up = dgain // 4 // 256;
		low = dgain / 4 - (256 * up)   */
		// assert(SENSOR_FIX_FRACBITS == 1024);
		imx219_write_reg(sensor, DIG_GAIN_GLOBAL_A_UP, 1, dgain / /*4/256*/ 1024);
		imx219_write_reg(sensor, DIG_GAIN_GLOBAL_A_LOW, 1, dgain / 4 - (dgain / /*4 /256*/1024 * 256 ));
	}

	// set again
	/* gain =  256/(256-x)
		gain * 	(1 << SENSOR_FIX_FRACBITS) = 256 /(256-x) * (1 << SENSOR_FIX_FRACBITS)
		x = 256 -( 256 / (again / (1 << SENSOR_FIX_FRACBITS)))
	*/
	imx219_write_reg(sensor, ANA_GAIN_GLOBAL_A, 1, 256 -( 256 / (again / (1 << SENSOR_FIX_FRACBITS))));
#endif
	
	return ret;
}

static int imx219_set_vsgain(struct imx219 *sensor, u32 gain)
{
	int ret = 0;
	//pr_info("%s gain=0x%x\n",__func__,gain);
	return ret;
}
#if 0
static int imx219_set_fps(struct imx219 *sensor, u32 fps)
{
	u32 vts;
	int ret = 0;

	if (fps > sensor->cur_mode.ae_info.max_fps) {
		fps = sensor->cur_mode.ae_info.max_fps;
	}
	else if (fps < sensor->cur_mode.ae_info.min_fps) {
		fps = sensor->cur_mode.ae_info.min_fps;
	}
	vts = sensor->cur_mode.ae_info.max_fps *
	      sensor->cur_mode.ae_info.def_frm_len_lines / fps;

//	ret = imx219_write_reg(sensor, 0x30B2, (u8)(vts >> 8) & 0xff);
//	ret |= imx219_write_reg(sensor, 0x30B3, (u8)(vts & 0xff));

	sensor->cur_mode.ae_info.cur_fps = fps;

	if (sensor->cur_mode.hdr_mode == SENSOR_MODE_LINEAR) {
		sensor->cur_mode.ae_info.max_integration_line = vts - 4;
	} else {
		if (sensor->cur_mode.stitching_mode ==
		    SENSOR_STITCHING_DUAL_DCG){
			sensor->cur_mode.ae_info.max_vsintegration_line = 44;
			sensor->cur_mode.ae_info.max_integration_line = vts -
				4 - sensor->cur_mode.ae_info.max_vsintegration_line;
		} else {
			sensor->cur_mode.ae_info.max_integration_line = vts - 4;
		}
	}
	sensor->cur_mode.ae_info.curr_frm_len_lines = vts;
	return ret;
}
#endif
static int imx219_get_fps(struct imx219 *sensor, u32 *pfps)
{
	*pfps = sensor->cur_mode.ae_info.cur_fps;
	return 0;
}

static int imx219_set_test_pattern(struct imx219 *sensor, void * arg)
{
	int ret;
	struct sensor_test_pattern_s test_pattern;

	ret = copy_from_user(&test_pattern, arg, sizeof(test_pattern));
	if (ret != 0)
		return -ENOMEM;
	if (test_pattern.enable) {
		if(test_pattern.pattern >= IMX219_TEST_PATTERN_PN9 ){
			ret = imx219_write_reg(sensor, IMX219_REG_TEST_PATTERN,
				       IMX219_REG_VALUE_16BIT,
				       imx219_test_pattern_val[test_pattern.pattern + 1]);
		}
		else 
			ret = -1;
	} else {
		ret = imx219_write_reg(sensor, IMX219_REG_TEST_PATTERN,
				       IMX219_REG_VALUE_16BIT,
				       imx219_test_pattern_val[IMX219_TEST_PATTERN_DISABLE]);
	}
	return ret;
}
#if 0

static int imx219_set_ratio(struct imx219 *sensor, void* pratio)
{
	int ret = 0;
	struct sensor_hdr_artio_s hdr_ratio;
	struct vvcam_ae_info_s *pae_info = &sensor->cur_mode.ae_info;

	ret = copy_from_user(&hdr_ratio, pratio, sizeof(hdr_ratio));

	if ((hdr_ratio.ratio_l_s != pae_info->hdr_ratio.ratio_l_s) ||
	    (hdr_ratio.ratio_s_vs != pae_info->hdr_ratio.ratio_s_vs) ||
	    (hdr_ratio.accuracy != pae_info->hdr_ratio.accuracy)) {
		pae_info->hdr_ratio.ratio_l_s = hdr_ratio.ratio_l_s;
		pae_info->hdr_ratio.ratio_s_vs = hdr_ratio.ratio_s_vs;
		pae_info->hdr_ratio.accuracy = hdr_ratio.accuracy;
		/*imx219 vs exp is limited for isp,so no need update max exp*/
	}

	return 0;
}

static int imx219_set_blc(struct imx219 *sensor, sensor_blc_t *pblc)
{
	int ret = 0;

	return ret;
}

static int imx219_set_wb(struct imx219 *sensor, void *pwb_cfg)
{
	int ret = 0;

	return ret;
}
#endif
static int imx219_get_expand_curve(struct imx219 *sensor,
				   sensor_expand_curve_t* pexpand_curve)
{
	int i;
	if ((pexpand_curve->x_bit == 12) && (pexpand_curve->y_bit == 16)) {
		uint8_t expand_px[64] = {6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
					6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
					6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
					6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6};

		memcpy(pexpand_curve->expand_px,expand_px,sizeof(expand_px));

		pexpand_curve->expand_x_data[0] = 0;
		pexpand_curve->expand_y_data[0] = 0;
		for(i = 1; i < 65; i++) {
			pexpand_curve->expand_x_data[i] =
				(1 << pexpand_curve->expand_px[i-1]) +
				pexpand_curve->expand_x_data[i-1];

			if (pexpand_curve->expand_x_data[i] < 512) {
				pexpand_curve->expand_y_data[i] =
					pexpand_curve->expand_x_data[i] << 1;

			} else if (pexpand_curve->expand_x_data[i] < 768)
			{
				pexpand_curve->expand_y_data[i] =
					(pexpand_curve->expand_x_data[i] - 256) << 2 ;

			} else if (pexpand_curve->expand_x_data[i] < 2560) {
				pexpand_curve->expand_y_data[i] =
					(pexpand_curve->expand_x_data[i] - 512) << 3 ;

			} else {
				pexpand_curve->expand_y_data[i] =
					(pexpand_curve->expand_x_data[i] - 2048) << 5;
			}
		}
		return 0;
	}
	return -1;
}

static int imx219_set_framefmt(struct imx219 *sensor)
{
   switch (sensor->format.code) {
   case MEDIA_BUS_FMT_SRGGB8_1X8:
   case MEDIA_BUS_FMT_SGRBG8_1X8:
   case MEDIA_BUS_FMT_SGBRG8_1X8:
   case MEDIA_BUS_FMT_SBGGR8_1X8:
	   return imx219_write_regs(sensor, raw8_framefmt_regs,
				   ARRAY_SIZE(raw8_framefmt_regs));

   case MEDIA_BUS_FMT_SRGGB10_1X10:
   case MEDIA_BUS_FMT_SGRBG10_1X10:
   case MEDIA_BUS_FMT_SGBRG10_1X10:
   case MEDIA_BUS_FMT_SBGGR10_1X10:
	   return imx219_write_regs(sensor, raw10_framefmt_regs,
				   ARRAY_SIZE(raw10_framefmt_regs));
   }

   return -EINVAL;
}
static int imx219_start_streaming(struct imx219 *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret;
	
	struct vvcam_sccb_data_s *sensor_reg_cfg;
	u32 sensor_reg_size = 0;
	//pr_info("enter %s ===\n", __func__);

	sensor_reg_cfg =
		(struct vvcam_sccb_data_s *)sensor->cur_mode.preg_data;
	sensor_reg_size = sensor->cur_mode.reg_data_count;
	/* Apply default values of current mode */
	ret = imx219_write_regs(sensor, sensor_reg_cfg, sensor_reg_size);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	ret = imx219_set_framefmt(sensor);
	if (ret) {
		dev_err(&client->dev, "%s failed to set frame format: %d\n",
			__func__, ret);
		return ret;
	}

	/* Apply customized values from user */
	//ret =  __v4l2_ctrl_handler_setup(sensor->sd.ctrl_handler);
	//if (ret)
	//	return ret;

	/* set stream on register */
	return imx219_write_reg(sensor, IMX219_REG_MODE_SELECT,
				IMX219_REG_VALUE_08BIT, IMX219_MODE_STREAMING);
}

static void imx219_stop_streaming(struct imx219 *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret;

	/* set stream off register */
	ret = imx219_write_reg(sensor, IMX219_REG_MODE_SELECT,
			       IMX219_REG_VALUE_08BIT, IMX219_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int imx219_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *sensor = client_to_imx219(client);
	int ret = 0;

	mutex_lock(&sensor->mutex);
	if (sensor->stream_status == enable) {
		mutex_unlock(&sensor->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imx219_start_streaming(sensor);
		if (ret)
			goto err_rpm_put;
	} else {
		imx219_stop_streaming(sensor);
		pm_runtime_put(&client->dev);
	}

	sensor->stream_status = enable;

	/* vflip and hflip cannot change during streaming */
	//__v4l2_ctrl_grab(sensor->vflip, enable); // add by imx219
	//__v4l2_ctrl_grab(sensor->hflip, enable);

	mutex_unlock(&sensor->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&sensor->mutex);

	return ret;
}

/* Get bayer order based on flip setting. */
static u32 imx219_get_format_code(struct imx219 *sensor, u32 code)
{
	unsigned int i;

	lockdep_assert_held(&sensor->mutex);

	for (i = 0; i < ARRAY_SIZE(support_codes); i++)
		if (support_codes[i] == code)
			break;

	if (i >= ARRAY_SIZE(support_codes))
		i = 0;

//	i = (i & ~3) | (imx219->vflip->val ? 2 : 0) |
//s	    (imx219->hflip->val ? 1 : 0);

	return support_codes[i];
}



#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 12, 0)
static int imx219_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
#else
static int imx219_enum_mbus_code(struct v4l2_subdev *sd,
			         struct v4l2_subdev_pad_config *cfg,
			         struct v4l2_subdev_mbus_code_enum *code)
#endif
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *sensor = client_to_imx219(client);
	//pr_info("enter %s ===\n", __func__);

	if (code->index >= (ARRAY_SIZE(support_codes) / 4))
		return -EINVAL;

	code->code = imx219_get_format_code(sensor, support_codes[code->index * 4]);

	return 0;
}
static void imx219_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx219_update_pad_format(struct imx219 *imx219,
				     const struct imx219_mode_crop *pmode_crop,
				     struct v4l2_subdev_format *fmt)
{
	fmt->format.width = pmode_crop->width;
	fmt->format.height = pmode_crop->height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx219_reset_colorspace(&fmt->format);
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 12, 0)
static int imx219_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
#else
static int imx219_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
#endif
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *sensor = client_to_imx219(client);
	const struct imx219_mode_crop *pmode_crop;
	//struct v4l2_mbus_framefmt *framefmt;
	//int exposure_max, exposure_def, hblank;
	unsigned int i;
	pr_info("enter %s ===\n", __func__);

	mutex_lock(&sensor->mutex);

	if ((fmt->format.width != sensor->cur_mode.size.bounds_width) ||
	    (fmt->format.height != sensor->cur_mode.size.bounds_height)) {
		pr_err("%s:set sensor format %dx%d error\n",
			__func__,fmt->format.width,fmt->format.height);
		mutex_unlock(&sensor->lock);
		return -EINVAL;
	}

	/* Bayer order varies with flips */
	fmt->format.code = imx219_get_format_code(sensor, support_codes[i]);

	pmode_crop = v4l2_find_nearest_size(supported_modes_crop,
				      ARRAY_SIZE(supported_modes_crop),
				      width, height,
				      fmt->format.width, fmt->format.height);
	
	imx219_update_pad_format(sensor, pmode_crop, fmt);

	 if (sensor->pmode_crop != pmode_crop ||
		   sensor->format.code != fmt->format.code) {
		sensor->format = fmt->format;
		sensor->pmode_crop = pmode_crop;
	}

	sensor->format = fmt->format;
	mutex_unlock(&sensor->mutex);

	return 0;
}


#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 12, 0)
static int imx219_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
#else
static int imx219_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
#endif
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *sensor = client_to_imx219(client);
	
	pr_info("enter %s ===\n", __func__);
	mutex_lock(&sensor->mutex);
	fmt->format = sensor->format;
	mutex_unlock(&sensor->mutex);

	return 0;
}


static long imx219_priv_ioctl(struct v4l2_subdev *sd,
                              unsigned int cmd,
                              void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *sensor = client_to_imx219(client);
	long ret = 0;
	struct vvcam_sccb_data_s sensor_reg;
	uint32_t value = 0;
	sensor_blc_t blc;
	sensor_expand_curve_t expand_curve;
	//pr_info("== enter %s cmd=0x%x VIDIOC_QUERYCAP=0x%lx\n", __func__,cmd, VIDIOC_QUERYCAP); 

	mutex_lock(&sensor->lock);
	switch (cmd){
	case VVSENSORIOC_S_POWER:
		ret = 0;
		break;
	case VVSENSORIOC_S_CLK:
		ret = 0;
		break;
	case VVSENSORIOC_G_CLK: // need review
		ret = imx219_get_clk(sensor,arg);
		break;
	case VVSENSORIOC_RESET:
		ret = 0;
		break;
	case VIDIOC_QUERYCAP:
		ret = imx219_query_capability(sensor, arg);
		break;
	case VVSENSORIOC_QUERY:
		ret = imx219_query_supports(sensor, arg);
		break;
	case VVSENSORIOC_G_CHIP_ID:
		ret = imx219_get_sensor_id(sensor, arg);
		break;
	case VVSENSORIOC_G_RESERVE_ID:
		ret = imx219_get_reserve_id(sensor, arg);
		break;
	case VVSENSORIOC_G_SENSOR_MODE: // need review
		ret = imx219_get_sensor_mode(sensor, arg);
		break;
	case VVSENSORIOC_S_SENSOR_MODE: // need review
		ret = imx219_set_sensor_mode(sensor, arg);
		break;
	case VVSENSORIOC_S_STREAM:
		ret = copy_from_user(&value, arg, sizeof(value));
		ret |= imx219_s_stream(&sensor->subdev, value);
		break;
	case VVSENSORIOC_WRITE_REG:
		ret = copy_from_user(&sensor_reg, arg,
			sizeof(struct vvcam_sccb_data_s));
		ret |= imx219_write_reg(sensor, (u16)sensor_reg.addr,IMX219_REG_VALUE_16BIT, sensor_reg.data);
		break;
	case VVSENSORIOC_READ_REG:
		ret = copy_from_user(&sensor_reg, arg,
			sizeof(struct vvcam_sccb_data_s));
		
		ret |= imx219_read_reg(sensor, (u16) sensor_reg.addr, IMX219_REG_VALUE_16BIT,&sensor_reg.data) ;
		
		ret |= copy_to_user(arg, &sensor_reg,
			sizeof(struct vvcam_sccb_data_s));
		break;
	case VVSENSORIOC_S_LONG_EXP:
		ret = copy_from_user(&value, arg, sizeof(value));
		ret |= imx219_set_lexp(sensor, value);
		break;
	case VVSENSORIOC_S_EXP:
		ret = copy_from_user(&value, arg, sizeof(value));
		ret |= imx219_set_exp(sensor, value);
		break;
	case VVSENSORIOC_S_VSEXP:
		ret = copy_from_user(&value, arg, sizeof(value));
		ret |= imx219_set_vsexp(sensor, value);
		break;
	case VVSENSORIOC_S_LONG_GAIN:
		ret = copy_from_user(&value, arg, sizeof(value));
		ret |= imx219_set_lgain(sensor, value);
		break;
	case VVSENSORIOC_S_GAIN:
		ret = copy_from_user(&value, arg, sizeof(value));
		ret |= imx219_set_gain(sensor, value);
		break;
	case VVSENSORIOC_S_VSGAIN:
		ret = copy_from_user(&value, arg, sizeof(value));
		ret |= imx219_set_vsgain(sensor, value);
		break;
	case VVSENSORIOC_S_FPS:
		ret = copy_from_user(&value, arg, sizeof(value));
		//ret |= imx219_set_fps(sensor, value); //imx219 not support
		break;
	case VVSENSORIOC_G_FPS:
		ret = imx219_get_fps(sensor, &value);
		ret |= copy_to_user(arg, &value, sizeof(value));
		break;
	case VVSENSORIOC_S_HDR_RADIO:
		//ret = imx219_set_ratio(sensor, arg); //imx219 no support
		break;
	case VVSENSORIOC_S_BLC:
		ret = copy_from_user(&blc, arg, sizeof(blc));
		//ret |= imx219_set_blc(sensor, &blc);//imx219 no support
		break;
	case VVSENSORIOC_S_WB:
		//ret = imx219_set_wb(sensor, arg);//imx219 no support
		break;
	case VVSENSORIOC_G_EXPAND_CURVE:
		ret = copy_from_user(&expand_curve, arg, sizeof(expand_curve));
		ret |= imx219_get_expand_curve(sensor, &expand_curve);
		ret |= copy_to_user(arg, &expand_curve, sizeof(expand_curve));
		break;
	case VVSENSORIOC_S_TEST_PATTERN:
		ret= imx219_set_test_pattern(sensor, arg);
		break;
	default:
		break;
	}

	mutex_unlock(&sensor->lock);
	return ret;
}

static struct v4l2_subdev_video_ops imx219_subdev_video_ops = {
	.s_stream = imx219_s_stream,
};

static const struct v4l2_subdev_pad_ops imx219_subdev_pad_ops = {
	.enum_mbus_code = imx219_enum_mbus_code,
	.set_fmt = imx219_set_fmt,
	.get_fmt = imx219_get_fmt,
};

static struct v4l2_subdev_core_ops imx219_subdev_core_ops = {
	.s_power = imx219_s_power,
	.ioctl = imx219_priv_ioctl,
};

static struct v4l2_subdev_ops imx219_subdev_ops = {
	.core  = &imx219_subdev_core_ops,
	.video = &imx219_subdev_video_ops,
	.pad   = &imx219_subdev_pad_ops,
};

static int imx219_link_setup(struct media_entity *entity,
			     const struct media_pad *local,
			     const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations imx219_sd_media_ops = {
	.link_setup = imx219_link_setup,
};


#if 0
static int imx219_set_clk_rate(struct imx219 *sensor)
{
	int ret;
	unsigned int clk;

	clk = sensor->mclk;
	clk = min_t(u32, clk, (u32)IMX219_XCLK_MAX);
	clk = max_t(u32, clk, (u32)IMX219_XCLK_MIN);
	sensor->mclk = clk;

	pr_debug("   Setting mclk to %d MHz\n",sensor->mclk / 1000000);
	ret = clk_set_rate(sensor->sensor_clk, sensor->mclk);
	if (ret < 0)
		pr_debug("set rate filed, rate=%d\n", sensor->mclk);
	return ret;
}
#endif
static void imx219_reset(struct imx219 *sensor)
{
	pr_debug("enter %s\n", __func__);
	if (!gpio_is_valid(sensor->rst_gpio))
		return;

	gpio_set_value_cansleep(sensor->rst_gpio, 0);
	msleep(20);

	gpio_set_value_cansleep(sensor->rst_gpio, 1);
	msleep(20);

	return;
}

static int imx219_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	/* Check the number of MIPI CSI2 data lanes */
	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "only 2 data lanes are currently supported\n");
		goto error_out;
	}

	/* Check the link frequency set in device tree */
	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	if (ep_cfg.nr_of_link_frequencies != 1 ||
	    ep_cfg.link_frequencies[0] != IMX219_DEFAULT_LINK_FREQ) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
		goto error_out;
	}

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}


/* Verify chip ID */
static int imx219_identify_module(struct imx219 *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret;
	u32 val;

	ret = imx219_read_reg(sensor, IMX219_REG_CHIP_ID,
			      IMX219_REG_VALUE_16BIT, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x\n",
			IMX219_CHIP_ID);
		return ret;
	}

	if (val != IMX219_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			IMX219_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static void imx219_set_default_format(struct imx219 *sensor)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = &sensor->format;
	fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width =  sensor->cur_mode.size.bounds_width;
	fmt->height = sensor->cur_mode.size.bounds_height;
	fmt->field = V4L2_FIELD_NONE;
}

static int imx219_probe(struct i2c_client *client)
{
	int retval;
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd;
	struct imx219 *sensor;
//	u32 chip_id = 0;
//	u8 reg_val = 0;

	pr_info("enter %s\n", __func__);
	
	/* Check the hardware configuration in device tree */
	if (imx219_check_hwcfg(dev))
		return -EINVAL;

	sensor = devm_kmalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	memset(sensor, 0, sizeof(*sensor));

	sensor->i2c_client = client;

	sensor->pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(sensor->pwn_gpio))
		dev_warn(dev, "No sensor pwdn pin available");
	else {
		retval = devm_gpio_request_one(dev, sensor->pwn_gpio,
						GPIOF_OUT_INIT_LOW,
						"imx219_mipi_pwdn");
		if (retval < 0) {
			dev_warn(dev, "Failed to set power pin\n");
			dev_warn(dev, "retval=%d\n", retval);
			return retval;
		}
	}

	sensor->rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(sensor->rst_gpio))
		dev_warn(dev, "No sensor reset pin available");
	else {
		retval = devm_gpio_request_one(dev, sensor->rst_gpio,
						GPIOF_OUT_INIT_HIGH,
						"imx219_mipi_reset");
		if (retval < 0) {
			dev_warn(dev, "Failed to set reset pin\n");
			return retval;
		}
	}

	retval = of_property_read_u32(dev->of_node, "csi_id", &(sensor->csi_id));
	if (retval) {
		dev_err(dev, "csi id missing or invalid\n");
		return retval;
	}
	
	/* Get system clock (xclk) */
	sensor->xclk =  devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(sensor->xclk);
	}
	sensor->xclk_freq = clk_get_rate(sensor->xclk);
	if (sensor->xclk_freq != IMX219_XCLK_FREQ) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			sensor->xclk_freq);
		return -EINVAL;
	}
	
	retval = imx219_power_on(sensor);
	if (retval < 0) {
		dev_err(dev, "%s: sensor power on fail\n", __func__);
		return retval;
	}

	imx219_reset(sensor);

	retval = imx219_identify_module(sensor);
	if (retval)
		goto probe_err_power_off;
	/* sensor doesn't enter LP-11 state upon power up until and unless
	 * streaming is started, so upon power up switch the modes to:
	 * streaming -> standby
	 */
	retval = imx219_write_reg(sensor, IMX219_REG_MODE_SELECT,
			       IMX219_REG_VALUE_08BIT, IMX219_MODE_STREAMING);
	if (retval < 0)
		goto probe_err_power_off;
	usleep_range(100, 110);

	/* put sensor back to standby mode */
	retval= imx219_write_reg(sensor, IMX219_REG_MODE_SELECT,
			       IMX219_REG_VALUE_08BIT, IMX219_MODE_STANDBY);
	if (retval < 0)
		goto probe_err_power_off;
	usleep_range(100, 110);

	memcpy(&sensor->cur_mode, &pimx219_mode_info[0],
			sizeof(struct vvcam_mode_info_s));

	imx219_set_default_format(sensor);

	sd = &sensor->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx219_subdev_ops);

	//sd->sd.internal_ops = &imx219_internal_ops; //imx219 special
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->dev = &client->dev;
	sd->entity.ops = &imx219_sd_media_ops;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->pads[IMX219_SENS_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	retval = media_entity_pads_init(&sd->entity,
				IMX219_SENS_PADS_NUM,
				sensor->pads);
	if (retval < 0)
		goto probe_err_power_off;

	
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 12, 0)
	retval = v4l2_async_register_subdev_sensor(sd);
#else
	retval = v4l2_async_register_subdev_sensor_common(sd);
#endif
	if (retval < 0) {
		dev_err(&client->dev,"%s--Async register failed, ret=%d\n",
			__func__,retval);
		goto probe_err_free_entiny;
	}



	mutex_init(&sensor->lock);
	
	mutex_init(&sensor->mutex);
	pr_info("%s camera mipi imx219, is found\n", __func__);
	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

probe_err_free_entiny:
	media_entity_cleanup(&sd->entity);

probe_err_power_off:
	imx219_power_off(sensor);

	return retval;
}

static void imx219_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx219 *sensor = client_to_imx219(client);

	pr_info("enter %s\n", __func__);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx219_power_off(sensor);
	pm_runtime_set_suspended(&client->dev);
	
	mutex_destroy(&sensor->lock);
	mutex_destroy(&sensor->mutex);

	return;
}

static int __maybe_unused imx219_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct imx219 *sensor = client_to_imx219(client);

	sensor->resume_status = sensor->stream_status;
	if (sensor->resume_status) {
		imx219_s_stream(&sensor->subdev,0);
	}

	return 0;
}

static int __maybe_unused imx219_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct imx219 *sensor = client_to_imx219(client);

	if (sensor->resume_status) {
		imx219_s_stream(&sensor->subdev,1);
	}

	return 0;
}

static const struct dev_pm_ops imx219_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx219_suspend, imx219_resume)
};


static const struct of_device_id imx219_of_match[] = {
	{ .compatible = "sony,imx219" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx219_of_match);

static struct i2c_driver imx219_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name  = "imx219",
		.pm = &imx219_pm_ops,
		.of_match_table	= imx219_of_match,
	},
	.probe  = imx219_probe,
	.remove = imx219_remove,
};


module_i2c_driver(imx219_i2c_driver);
MODULE_DESCRIPTION("IMX219 MIPI Camera Subdev Driver");
MODULE_LICENSE("GPL");
