/*
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define OV5640_VOLTAGE_ANALOG               2800000
#define OV5640_VOLTAGE_DIGITAL_CORE         1500000
#define OV5640_VOLTAGE_DIGITAL_IO           1800000

#define MIN_FPS 15
#define MAX_FPS 30
#define DEFAULT_FPS 30

#define OV5640_XCLK_MIN 6000000
#define OV5640_XCLK_MAX 24000000

#define OV5640_CHIP_ID_HIGH_BYTE        0x300A
#define OV5640_CHIP_ID_LOW_BYTE         0x300B

#define OV5640_REG_SYSCLK_PLL_CTRL0     0x3034
#define OV5640_REG_SYSCLK_PLL_CTRL1     0x3035
#define OV5640_REG_SYSCLK_PLL_MULT      0x3036
#define OV5640_REG_SYSCLK_PLL_PREDIV    0x3037
#define OV5640_REG_PAD_OUTPUT00         0x302c
#define OV5640_REG_FREX_CTRL            0x3022
#define OV5640_REG_SYS_ROOT_DIVIDER     0x3108
#define OV5640_REG_TIMING_HTS_H         0x380c
#define OV5640_REG_TIMING_VTS_H         0x380e
#define OV5640_REG_AEC_PK_EXPOSURE_H    0x3500
#define OV5640_REG_AEC_PK_EXPOSURE_M    0x3501
#define OV5640_REG_AEC_PK_EXPOSURE_L    0x3502
#define OV5640_REG_AEC_PK_MANUAL        0x3503
#define OV5640_REG_AEC_PK_REAL_GAIN_H   0x350a
#define OV5640_REG_AEC_CTRL00           0x3a00
#define OV5640_REG_AEC_STABLE_HIGH      0x3a0f
#define OV5640_REG_AEC_STABLE_LOW       0x3a10
#define OV5640_REG_AEC_FAST_HIGH        0x3a11
#define OV5640_REG_AEC_CTRL1B           0x3a1b
#define OV5640_REG_AEC_CTRL1E           0x3a1e
#define OV5640_REG_AEC_FAST_LOW         0x3a1f
#define OV5640_REG_BANDING_50_STEP_H    0x3a08
#define OV5640_REG_BANDING_50_MAX       0x3a0e
#define OV5640_REG_BANDING_60_STEP_H    0x3a0a
#define OV5640_REG_BANDING_60_MAX       0x3a0d
#define OV5640_REG_BANDING_FILTER_MAN   0x3c00
#define OV5640_REG_BANDING_FILTER_CTRL  0x3c01
#define OV5640_REG_BANDING_FILTER_AUTO  0x3c0c
#define OV5640_REG_TIMING_TC_REG20      0x3820
#define OV5640_REG_TIMING_TC_REG21      0x3821
#define OV5640_REG_STREAM_CTRL          0x4202
#define OV5640_REG_FORMAT_CONTROL00     0x4300
#define OV5640_REG_FORMAT_MUX_CONTROL   0x501f
#define OV5640_FORMAT_MUX_RGB           0x01
#define OV5640_FORMAT_CTRL_RGB565       0x6f
#define OV5640_REG_AVG_READOUT          0x56a1

#define OV5640_TIMING_FLIP_MASK         0x06
#define OV5640_BANDING_MANUAL_ENABLE    0x80
#define OV5640_BANDING_MANUAL_50HZ      0x04

#define OV5640_STREAM_ON                0x00
#define OV5640_STREAM_OFF               0x0f
#define OV5640_AUTOSUSPEND_DELAY_MS     1000

enum ov5640_mode {
	ov5640_mode_MIN = 0,
	ov5640_mode_VGA_640_480 = 0,
	ov5640_mode_QVGA_320_240 = 1,
	ov5640_mode_NTSC_720_480 = 2,
	ov5640_mode_PAL_720_576 = 3,
	ov5640_mode_720P_1280_720 = 4,
	ov5640_mode_1080P_1920_1080 = 5,
	ov5640_mode_QSXGA_2592_1944 = 6,
	ov5640_mode_QCIF_176_144 = 7,
	ov5640_mode_XGA_1024_768 = 8,
	ov5640_mode_800_480 = 9,
	ov5640_mode_MAX = 9
};

enum ov5640_frame_rate {
	ov5640_15_fps,
	ov5640_30_fps
};

static int ov5640_framerates[] = {
	[ov5640_15_fps] = 15,
	[ov5640_30_fps] = 30,
};

struct ov5640_datafmt {
	u32	code;
	enum v4l2_colorspace		colorspace;
};

struct reg_value {
	u16 u16RegAddr;
	u8 u8Val;
	u8 u8Mask;
	u32 u32Delay_ms;
};

enum ov5640_downsize_mode {
	OV5640_DOWNSIZE_SUBSAMPLING,
	OV5640_DOWNSIZE_SCALING,
};

struct ov5640_rect {
	u32 left;
	u32 top;
	u32 width;
	u32 height;
};

struct ov5640_mode_fps_info {
	enum ov5640_frame_rate frame_rate;
	const struct reg_value *regs;
	u32 num_regs;
};

struct ov5640_mode_info {
	enum ov5640_mode mode;
	u32 width;
	u32 height;
	enum ov5640_downsize_mode downsize;
	struct ov5640_rect analog_crop;
	struct ov5640_rect crop;
	u32 htot;
	u32 vts_def;
	u32 pixel_rate;
	enum ov5640_frame_rate default_fps;
	const struct ov5640_mode_fps_info *fps;
	u32 num_fps;
};

struct ov5640 {
	struct v4l2_subdev		subdev;
	struct device *dev;
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	struct gpio_desc *pwdn_gpio;
	struct gpio_desc *reset_gpio;
	struct v4l2_pix_format pix;
	const struct ov5640_datafmt	*fmt;
	struct v4l2_captureparm streamcap;
	enum ov5640_frame_rate current_fr;
	enum ov5640_mode current_mode;
	bool powered;
	bool streaming;
	bool power_ref;
	bool on;
	struct mutex lock;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *power_line_frequency;

	/* Legacy placeholders kept until matching image-processing controls exist. */
	int brightness;
	int hue;
	int contrast;
	int saturation;
	int red;
	int green;
	int blue;
	int ae_mode;

	u32 mclk;
	u8 mclk_source;
	struct clk *sensor_clk;
	int csi;

	void (*io_init)(struct ov5640 *sensor);
};

/*!
 * Maintains the information on the current state of the sesor.
 */
static struct ov5640 ov5640_data;
static int prev_sysclk;
static int AE_Target = 52, night_mode;
static int prev_HTS;
static int AE_high, AE_low;

static const struct reg_value ov5640_global_init_setting[] = {
	{0x3008, 0x42, 0, 0},
	{0x3103, 0x03, 0, 0}, {0x3017, 0xff, 0, 0}, {0x3018, 0xff, 0, 0},
	{0x3034, 0x1a, 0, 0}, {0x3037, 0x13, 0, 0}, {0x3108, 0x01, 0, 0},
	{0x3630, 0x36, 0, 0}, {0x3631, 0x0e, 0, 0}, {0x3632, 0xe2, 0, 0},
	{0x3633, 0x12, 0, 0}, {0x3621, 0xe0, 0, 0}, {0x3704, 0xa0, 0, 0},
	{0x3703, 0x5a, 0, 0}, {0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0},
	{0x370b, 0x60, 0, 0}, {0x3705, 0x1a, 0, 0}, {0x3905, 0x02, 0, 0},
	{0x3906, 0x10, 0, 0}, {0x3901, 0x0a, 0, 0}, {0x3731, 0x12, 0, 0},
	{0x3600, 0x08, 0, 0}, {0x3601, 0x33, 0, 0}, {0x302d, 0x60, 0, 0},
	{0x3620, 0x52, 0, 0}, {0x371b, 0x20, 0, 0}, {0x471c, 0x50, 0, 0},
	{0x3a13, 0x43, 0, 0}, {0x3a18, 0x00, 0, 0}, {0x3a19, 0x7c, 0, 0},
	{0x3635, 0x13, 0, 0}, {0x3636, 0x03, 0, 0}, {0x3634, 0x40, 0, 0},
	{0x3622, 0x01, 0, 0}, {0x3c01, 0x34, 0, 0}, {0x3c04, 0x28, 0, 0},
	{0x3c05, 0x98, 0, 0}, {0x3c06, 0x00, 0, 0}, {0x3c07, 0x07, 0, 0},
	{0x3c08, 0x00, 0, 0}, {0x3c09, 0x1c, 0, 0}, {0x3c0a, 0x9c, 0, 0},
	{0x3c0b, 0x40, 0, 0}, {0x3810, 0x00, 0, 0}, {0x3811, 0x10, 0, 0},
	{0x3812, 0x00, 0, 0}, {0x3708, 0x64, 0, 0}, {0x4001, 0x02, 0, 0},
	{0x4005, 0x1a, 0, 0}, {0x3000, 0x00, 0, 0}, {0x3004, 0xff, 0, 0},
	{0x300e, 0x58, 0, 0}, {0x302e, 0x00, 0, 0}, {0x4300, 0x30, 0, 0},
	{0x501f, 0x00, 0, 0}, {0x440e, 0x00, 0, 0}, {0x5000, 0xa7, 0, 0},
	{0x3008, 0x02, 0, 0},
};

static const struct reg_value ov5640_init_setting_30fps_VGA[] = {
	{0x3008, 0x42, 0, 0},
	{0x3103, 0x03, 0, 0}, {0x3017, 0xff, 0, 0}, {0x3018, 0xff, 0, 0},
	{0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0}, {0x3036, 0x46, 0, 0},
	{0x3037, 0x13, 0, 0}, {0x3108, 0x01, 0, 0}, {0x3630, 0x36, 0, 0},
	{0x3631, 0x0e, 0, 0}, {0x3632, 0xe2, 0, 0}, {0x3633, 0x12, 0, 0},
	{0x3621, 0xe0, 0, 0}, {0x3704, 0xa0, 0, 0}, {0x3703, 0x5a, 0, 0},
	{0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0}, {0x370b, 0x60, 0, 0},
	{0x3705, 0x1a, 0, 0}, {0x3905, 0x02, 0, 0}, {0x3906, 0x10, 0, 0},
	{0x3901, 0x0a, 0, 0}, {0x3731, 0x12, 0, 0}, {0x3600, 0x08, 0, 0},
	{0x3601, 0x33, 0, 0}, {0x302d, 0x60, 0, 0}, {0x3620, 0x52, 0, 0},
	{0x371b, 0x20, 0, 0}, {0x471c, 0x50, 0, 0}, {0x3a13, 0x43, 0, 0},
	{0x3a18, 0x00, 0, 0}, {0x3a19, 0xf8, 0, 0}, {0x3635, 0x13, 0, 0},
	{0x3636, 0x03, 0, 0}, {0x3634, 0x40, 0, 0}, {0x3622, 0x01, 0, 0},
	{0x3c01, 0x34, 0, 0}, {0x3c04, 0x28, 0, 0}, {0x3c05, 0x98, 0, 0},
	{0x3c06, 0x00, 0, 0}, {0x3c07, 0x08, 0, 0}, {0x3c08, 0x00, 0, 0},
	{0x3c09, 0x1c, 0, 0}, {0x3c0a, 0x9c, 0, 0}, {0x3c0b, 0x40, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0}, {0x3814, 0x31, 0, 0},
	{0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x00, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0}, {0x3807, 0x9b, 0, 0},
	{0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0}, {0x380a, 0x01, 0, 0},
	{0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x68, 0, 0},
	{0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0}, {0x3810, 0x00, 0, 0},
	{0x3811, 0x10, 0, 0}, {0x3812, 0x00, 0, 0}, {0x3813, 0x06, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0}, {0x3708, 0x64, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x03, 0, 0},
	{0x3a03, 0xd8, 0, 0}, {0x3a08, 0x01, 0, 0}, {0x3a09, 0x27, 0, 0},
	{0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xf6, 0, 0}, {0x3a0e, 0x03, 0, 0},
	{0x3a0d, 0x04, 0, 0}, {0x3a14, 0x03, 0, 0}, {0x3a15, 0xd8, 0, 0},
	{0x4001, 0x02, 0, 0}, {0x4004, 0x02, 0, 0}, {0x3000, 0x00, 0, 0},
	{0x3002, 0x1c, 0, 0}, {0x3004, 0xff, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x300e, 0x58, 0, 0}, {0x302e, 0x00, 0, 0}, {0x4300, 0x30, 0, 0},
	{0x501f, 0x00, 0, 0}, {0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0},
	{0x440e, 0x00, 0, 0}, {0x460b, 0x35, 0, 0}, {0x460c, 0x22, 0, 0},
	{0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0}, {0x5000, 0xa7, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x5180, 0xff, 0, 0}, {0x5181, 0xf2, 0, 0},
	{0x5182, 0x00, 0, 0}, {0x5183, 0x14, 0, 0}, {0x5184, 0x25, 0, 0},
	{0x5185, 0x24, 0, 0}, {0x5186, 0x09, 0, 0}, {0x5187, 0x09, 0, 0},
	{0x5188, 0x09, 0, 0}, {0x5189, 0x88, 0, 0}, {0x518a, 0x54, 0, 0},
	{0x518b, 0xee, 0, 0}, {0x518c, 0xb2, 0, 0}, {0x518d, 0x50, 0, 0},
	{0x518e, 0x34, 0, 0}, {0x518f, 0x6b, 0, 0}, {0x5190, 0x46, 0, 0},
	{0x5191, 0xf8, 0, 0}, {0x5192, 0x04, 0, 0}, {0x5193, 0x70, 0, 0},
	{0x5194, 0xf0, 0, 0}, {0x5195, 0xf0, 0, 0}, {0x5196, 0x03, 0, 0},
	{0x5197, 0x01, 0, 0}, {0x5198, 0x04, 0, 0}, {0x5199, 0x6c, 0, 0},
	{0x519a, 0x04, 0, 0}, {0x519b, 0x00, 0, 0}, {0x519c, 0x09, 0, 0},
	{0x519d, 0x2b, 0, 0}, {0x519e, 0x38, 0, 0}, {0x5381, 0x1e, 0, 0},
	{0x5382, 0x5b, 0, 0}, {0x5383, 0x08, 0, 0}, {0x5384, 0x0a, 0, 0},
	{0x5385, 0x7e, 0, 0}, {0x5386, 0x88, 0, 0}, {0x5387, 0x7c, 0, 0},
	{0x5388, 0x6c, 0, 0}, {0x5389, 0x10, 0, 0}, {0x538a, 0x01, 0, 0},
	{0x538b, 0x98, 0, 0}, {0x5300, 0x08, 0, 0}, {0x5301, 0x30, 0, 0},
	{0x5302, 0x10, 0, 0}, {0x5303, 0x00, 0, 0}, {0x5304, 0x08, 0, 0},
	{0x5305, 0x30, 0, 0}, {0x5306, 0x08, 0, 0}, {0x5307, 0x16, 0, 0},
	{0x5309, 0x08, 0, 0}, {0x530a, 0x30, 0, 0}, {0x530b, 0x04, 0, 0},
	{0x530c, 0x06, 0, 0}, {0x5480, 0x01, 0, 0}, {0x5481, 0x08, 0, 0},
	{0x5482, 0x14, 0, 0}, {0x5483, 0x28, 0, 0}, {0x5484, 0x51, 0, 0},
	{0x5485, 0x65, 0, 0}, {0x5486, 0x71, 0, 0}, {0x5487, 0x7d, 0, 0},
	{0x5488, 0x87, 0, 0}, {0x5489, 0x91, 0, 0}, {0x548a, 0x9a, 0, 0},
	{0x548b, 0xaa, 0, 0}, {0x548c, 0xb8, 0, 0}, {0x548d, 0xcd, 0, 0},
	{0x548e, 0xdd, 0, 0}, {0x548f, 0xea, 0, 0}, {0x5490, 0x1d, 0, 0},
	{0x5580, 0x02, 0, 0}, {0x5583, 0x40, 0, 0}, {0x5584, 0x10, 0, 0},
	{0x5589, 0x10, 0, 0}, {0x558a, 0x00, 0, 0}, {0x558b, 0xf8, 0, 0},
	{0x5800, 0x23, 0, 0}, {0x5801, 0x14, 0, 0}, {0x5802, 0x0f, 0, 0},
	{0x5803, 0x0f, 0, 0}, {0x5804, 0x12, 0, 0}, {0x5805, 0x26, 0, 0},
	{0x5806, 0x0c, 0, 0}, {0x5807, 0x08, 0, 0}, {0x5808, 0x05, 0, 0},
	{0x5809, 0x05, 0, 0}, {0x580a, 0x08, 0, 0}, {0x580b, 0x0d, 0, 0},
	{0x580c, 0x08, 0, 0}, {0x580d, 0x03, 0, 0}, {0x580e, 0x00, 0, 0},
	{0x580f, 0x00, 0, 0}, {0x5810, 0x03, 0, 0}, {0x5811, 0x09, 0, 0},
	{0x5812, 0x07, 0, 0}, {0x5813, 0x03, 0, 0}, {0x5814, 0x00, 0, 0},
	{0x5815, 0x01, 0, 0}, {0x5816, 0x03, 0, 0}, {0x5817, 0x08, 0, 0},
	{0x5818, 0x0d, 0, 0}, {0x5819, 0x08, 0, 0}, {0x581a, 0x05, 0, 0},
	{0x581b, 0x06, 0, 0}, {0x581c, 0x08, 0, 0}, {0x581d, 0x0e, 0, 0},
	{0x581e, 0x29, 0, 0}, {0x581f, 0x17, 0, 0}, {0x5820, 0x11, 0, 0},
	{0x5821, 0x11, 0, 0}, {0x5822, 0x15, 0, 0}, {0x5823, 0x28, 0, 0},
	{0x5824, 0x46, 0, 0}, {0x5825, 0x26, 0, 0}, {0x5826, 0x08, 0, 0},
	{0x5827, 0x26, 0, 0}, {0x5828, 0x64, 0, 0}, {0x5829, 0x26, 0, 0},
	{0x582a, 0x24, 0, 0}, {0x582b, 0x22, 0, 0}, {0x582c, 0x24, 0, 0},
	{0x582d, 0x24, 0, 0}, {0x582e, 0x06, 0, 0}, {0x582f, 0x22, 0, 0},
	{0x5830, 0x40, 0, 0}, {0x5831, 0x42, 0, 0}, {0x5832, 0x24, 0, 0},
	{0x5833, 0x26, 0, 0}, {0x5834, 0x24, 0, 0}, {0x5835, 0x22, 0, 0},
	{0x5836, 0x22, 0, 0}, {0x5837, 0x26, 0, 0}, {0x5838, 0x44, 0, 0},
	{0x5839, 0x24, 0, 0}, {0x583a, 0x26, 0, 0}, {0x583b, 0x28, 0, 0},
	{0x583c, 0x42, 0, 0}, {0x583d, 0xce, 0, 0}, {0x5025, 0x00, 0, 0},
	{0x3a0f, 0x30, 0, 0}, {0x3a10, 0x28, 0, 0}, {0x3a1b, 0x30, 0, 0},
	{0x3a1e, 0x26, 0, 0}, {0x3a11, 0x60, 0, 0}, {0x3a1f, 0x14, 0, 0},
	{0x3008, 0x02, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_30fps_VGA_640_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0}, {0x3503, 0x00, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_VGA_640_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0}, {0x3503, 0x00, 0, 0},
};

static const struct reg_value ov5640_setting_30fps_QVGA_320_240[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x01, 0, 0}, {0x3809, 0x40, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0xf0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_QVGA_320_240[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x01, 0, 0}, {0x3809, 0x40, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0xf0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_30fps_NTSC_720_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0},
	{0x3807, 0xd4, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_NTSC_720_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0},
	{0x3807, 0xd4, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_30fps_PAL_720_576[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x60, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x09, 0, 0}, {0x3805, 0x7e, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x02, 0, 0}, {0x380b, 0x40, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_PAL_720_576[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x60, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x09, 0, 0}, {0x3805, 0x7e, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x02, 0, 0}, {0x3809, 0xd0, 0, 0},
	{0x380a, 0x02, 0, 0}, {0x380b, 0x40, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_30fps_720P_1280_720[] = {
	{0x3035, 0x21, 0, 0}, {0x3036, 0x69, 0, 0}, {0x3c07, 0x07, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0}, {0x3814, 0x31, 0, 0},
	{0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x00, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0xfa, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0}, {0x3807, 0xa9, 0, 0},
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x02, 0, 0},
	{0x380b, 0xd0, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x64, 0, 0},
	{0x380e, 0x02, 0, 0}, {0x380f, 0xe4, 0, 0}, {0x3813, 0x04, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x370c, 0x03, 0, 0}, {0x3a02, 0x02, 0, 0}, {0x3a03, 0xe0, 0, 0},
	{0x3a14, 0x02, 0, 0}, {0x3a15, 0xe0, 0, 0}, {0x4004, 0x02, 0, 0},
	{0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0}, {0x4713, 0x03, 0, 0},
	{0x4407, 0x04, 0, 0}, {0x460b, 0x37, 0, 0}, {0x460c, 0x20, 0, 0},
	{0x4837, 0x16, 0, 0}, {0x3824, 0x04, 0, 0}, {0x5001, 0x83, 0, 0},
	{0x3503, 0x00, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_720P_1280_720[] = {
	{0x3035, 0x41, 0, 0}, {0x3036, 0x69, 0, 0}, {0x3c07, 0x07, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0}, {0x3814, 0x31, 0, 0},
	{0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x00, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0xfa, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x3f, 0, 0}, {0x3806, 0x06, 0, 0}, {0x3807, 0xa9, 0, 0},
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x02, 0, 0},
	{0x380b, 0xd0, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x64, 0, 0},
	{0x380e, 0x02, 0, 0}, {0x380f, 0xe4, 0, 0}, {0x3813, 0x04, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x370c, 0x03, 0, 0}, {0x3a02, 0x02, 0, 0}, {0x3a03, 0xe0, 0, 0},
	{0x3a14, 0x02, 0, 0}, {0x3a15, 0xe0, 0, 0}, {0x4004, 0x02, 0, 0},
	{0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0}, {0x4713, 0x03, 0, 0},
	{0x4407, 0x04, 0, 0}, {0x460b, 0x37, 0, 0}, {0x460c, 0x20, 0, 0},
	{0x4837, 0x16, 0, 0}, {0x3824, 0x04, 0, 0}, {0x5001, 0x83, 0, 0},
	{0x3503, 0x00, 0, 0},
};

static const struct reg_value ov5640_setting_30fps_QCIF_176_144[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x00, 0, 0}, {0x3809, 0xb0, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0x90, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_QCIF_176_144[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x00, 0, 0}, {0x3809, 0xb0, 0, 0},
	{0x380a, 0x00, 0, 0}, {0x380b, 0x90, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x22, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x02, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_30fps_XGA_1024_768[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x04, 0, 0}, {0x3809, 0x00, 0, 0},
	{0x380a, 0x03, 0, 0}, {0x380b, 0x00, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_XGA_1024_768[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x04, 0, 0}, {0x3809, 0x00, 0, 0},
	{0x380a, 0x03, 0, 0}, {0x380b, 0x00, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x3037, 0x13, 0, 0},
};


static const struct reg_value ov5640_setting_15fps_1080P_1920_1080[] = {
	{0x3c07, 0x07, 0, 0}, {0x3820, 0x40, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0xee, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x05, 0, 0},
	{0x3807, 0xc3, 0, 0}, {0x3808, 0x07, 0, 0}, {0x3809, 0x80, 0, 0},
	{0x380a, 0x04, 0, 0}, {0x380b, 0x38, 0, 0}, {0x380c, 0x0b, 0, 0},
	{0x380d, 0x1c, 0, 0}, {0x380e, 0x07, 0, 0}, {0x380f, 0xb0, 0, 0},
	{0x3813, 0x04, 0, 0}, {0x3618, 0x04, 0, 0}, {0x3612, 0x2b, 0, 0},
	{0x3709, 0x12, 0, 0}, {0x370c, 0x00, 0, 0}, {0x3a02, 0x07, 0, 0},
	{0x3a03, 0xae, 0, 0}, {0x3a14, 0x07, 0, 0}, {0x3a15, 0xae, 0, 0},
	{0x4004, 0x06, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x02, 0, 0}, {0x4407, 0x0c, 0, 0}, {0x460b, 0x37, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x2c, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0x83, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct reg_value ov5640_setting_15fps_QSXGA_2592_1944[] = {
	{0x3c07, 0x07, 0, 0}, {0x3820, 0x40, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x00, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9f, 0, 0}, {0x3808, 0x0a, 0, 0}, {0x3809, 0x20, 0, 0},
	{0x380a, 0x07, 0, 0}, {0x380b, 0x98, 0, 0}, {0x380c, 0x0b, 0, 0},
	{0x380d, 0x1c, 0, 0}, {0x380e, 0x07, 0, 0}, {0x380f, 0xb0, 0, 0},
	{0x3813, 0x04, 0, 0}, {0x3618, 0x04, 0, 0}, {0x3612, 0x2b, 0, 0},
	{0x3709, 0x12, 0, 0}, {0x370c, 0x00, 0, 0}, {0x3a02, 0x07, 0, 0},
	{0x3a03, 0xae, 0, 0}, {0x3a14, 0x07, 0, 0}, {0x3a15, 0xae, 0, 0},
	{0x4004, 0x06, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x02, 0, 0}, {0x4407, 0x0c, 0, 0}, {0x460b, 0x37, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x2c, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0x83, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x3037, 0x13, 0, 0},
};

/* 0x3820 0x46, vflip ISP and Sensor together. */
static const struct reg_value ov5640_setting_30fps_800_480[] = {
	{0x3c07, 0x08, 0, 0}, {0x3820, 0x46, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3800, 0x00, 0, 0},
	{0x3801, 0x00, 0, 0}, {0x3802, 0x00, 0, 0}, {0x3803, 0x04, 0, 0},
	{0x3804, 0x0a, 0, 0}, {0x3805, 0x3f, 0, 0}, {0x3806, 0x07, 0, 0},
	{0x3807, 0x9b, 0, 0}, {0x3808, 0x03, 0, 0}, {0x3809, 0x20, 0, 0},
	{0x380a, 0x01, 0, 0}, {0x380b, 0xe0, 0, 0}, {0x380c, 0x07, 0, 0},
	{0x380d, 0x68, 0, 0}, {0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0},
	{0x3813, 0x06, 0, 0}, {0x3618, 0x00, 0, 0}, {0x3612, 0x29, 0, 0},
	{0x3709, 0x52, 0, 0}, {0x370c, 0x03, 0, 0}, {0x3a02, 0x0b, 0, 0},
	{0x3a03, 0x88, 0, 0}, {0x3a14, 0x0b, 0, 0}, {0x3a15, 0x88, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x3002, 0x1c, 0, 0}, {0x3006, 0xc3, 0, 0},
	{0x4713, 0x03, 0, 0}, {0x4407, 0x04, 0, 0}, {0x460b, 0x35, 0, 0},
	{0x460c, 0x20, 0, 0}, {0x4837, 0x22, 0, 0}, {0x3824, 0x01, 0, 0},
	{0x5001, 0xa3, 0, 0}, {0x3034, 0x1a, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3036, 0x69, 0, 0}, {0x3037, 0x13, 0, 0},
};

static const struct ov5640_mode_fps_info ov5640_vga_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_VGA_640_480,
	ARRAY_SIZE(ov5640_setting_15fps_VGA_640_480)},
	{ov5640_30_fps, ov5640_setting_30fps_VGA_640_480,
	ARRAY_SIZE(ov5640_setting_30fps_VGA_640_480)},
};

static const struct ov5640_mode_fps_info ov5640_qvga_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_QVGA_320_240,
	ARRAY_SIZE(ov5640_setting_15fps_QVGA_320_240)},
	{ov5640_30_fps, ov5640_setting_30fps_QVGA_320_240,
	ARRAY_SIZE(ov5640_setting_30fps_QVGA_320_240)},
};

static const struct ov5640_mode_fps_info ov5640_ntsc_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_NTSC_720_480,
	ARRAY_SIZE(ov5640_setting_15fps_NTSC_720_480)},
	{ov5640_30_fps, ov5640_setting_30fps_NTSC_720_480,
	ARRAY_SIZE(ov5640_setting_30fps_NTSC_720_480)},
};

static const struct ov5640_mode_fps_info ov5640_pal_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_PAL_720_576,
	ARRAY_SIZE(ov5640_setting_15fps_PAL_720_576)},
	{ov5640_30_fps, ov5640_setting_30fps_PAL_720_576,
	ARRAY_SIZE(ov5640_setting_30fps_PAL_720_576)},
};

static const struct ov5640_mode_fps_info ov5640_720p_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_720P_1280_720,
	ARRAY_SIZE(ov5640_setting_15fps_720P_1280_720)},
	{ov5640_30_fps, ov5640_setting_30fps_720P_1280_720,
	ARRAY_SIZE(ov5640_setting_30fps_720P_1280_720)},
};

static const struct ov5640_mode_fps_info ov5640_1080p_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_1080P_1920_1080,
	ARRAY_SIZE(ov5640_setting_15fps_1080P_1920_1080)},
};

static const struct ov5640_mode_fps_info ov5640_qsxga_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_QSXGA_2592_1944,
	ARRAY_SIZE(ov5640_setting_15fps_QSXGA_2592_1944)},
};

static const struct ov5640_mode_fps_info ov5640_qcif_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_QCIF_176_144,
	ARRAY_SIZE(ov5640_setting_15fps_QCIF_176_144)},
	{ov5640_30_fps, ov5640_setting_30fps_QCIF_176_144,
	ARRAY_SIZE(ov5640_setting_30fps_QCIF_176_144)},
};

static const struct ov5640_mode_fps_info ov5640_xga_fps[] = {
	{ov5640_15_fps, ov5640_setting_15fps_XGA_1024_768,
	ARRAY_SIZE(ov5640_setting_15fps_XGA_1024_768)},
	{ov5640_30_fps, ov5640_setting_30fps_XGA_1024_768,
	ARRAY_SIZE(ov5640_setting_30fps_XGA_1024_768)},
};

static const struct ov5640_mode_fps_info ov5640_800x480_fps[] = {
	{ov5640_30_fps, ov5640_setting_30fps_800_480,
	ARRAY_SIZE(ov5640_setting_30fps_800_480)},
};

static const struct ov5640_mode_info ov5640_modes[] = {
	{
		.mode = ov5640_mode_VGA_640_480,
		.width = 640,
		.height = 480,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 640, 480},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_vga_fps,
		.num_fps = ARRAY_SIZE(ov5640_vga_fps),
	}, {
		.mode = ov5640_mode_QVGA_320_240,
		.width = 320,
		.height = 240,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 320, 240},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_qvga_fps,
		.num_fps = ARRAY_SIZE(ov5640_qvga_fps),
	}, {
		.mode = ov5640_mode_NTSC_720_480,
		.width = 720,
		.height = 480,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1745},
		.crop = {0, 0, 720, 480},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_ntsc_fps,
		.num_fps = ARRAY_SIZE(ov5640_ntsc_fps),
	}, {
		.mode = ov5640_mode_PAL_720_576,
		.width = 720,
		.height = 576,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {96, 4, 2335, 1944},
		.crop = {0, 0, 720, 576},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_pal_fps,
		.num_fps = ARRAY_SIZE(ov5640_pal_fps),
	}, {
		.mode = ov5640_mode_720P_1280_720,
		.width = 1280,
		.height = 720,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 250, 2624, 1456},
		.crop = {0, 0, 1280, 720},
		.htot = 0x0764,
		.vts_def = 0x02e4,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_720p_fps,
		.num_fps = ARRAY_SIZE(ov5640_720p_fps),
	}, {
		.mode = ov5640_mode_1080P_1920_1080,
		.width = 1920,
		.height = 1080,
		.downsize = OV5640_DOWNSIZE_SCALING,
		.analog_crop = {0, 238, 2624, 1238},
		.crop = {0, 0, 1920, 1080},
		.htot = 0x0b1c,
		.vts_def = 0x07b0,
		.default_fps = ov5640_15_fps,
		.fps = ov5640_1080p_fps,
		.num_fps = ARRAY_SIZE(ov5640_1080p_fps),
	}, {
		.mode = ov5640_mode_QSXGA_2592_1944,
		.width = 2592,
		.height = 1944,
		.downsize = OV5640_DOWNSIZE_SCALING,
		.analog_crop = {0, 0, 2624, 1952},
		.crop = {0, 0, 2592, 1944},
		.htot = 0x0b1c,
		.vts_def = 0x07b0,
		.default_fps = ov5640_15_fps,
		.fps = ov5640_qsxga_fps,
		.num_fps = ARRAY_SIZE(ov5640_qsxga_fps),
	}, {
		.mode = ov5640_mode_QCIF_176_144,
		.width = 176,
		.height = 144,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 176, 144},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_qcif_fps,
		.num_fps = ARRAY_SIZE(ov5640_qcif_fps),
	}, {
		.mode = ov5640_mode_XGA_1024_768,
		.width = 1024,
		.height = 768,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 1024, 768},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_xga_fps,
		.num_fps = ARRAY_SIZE(ov5640_xga_fps),
	}, {
		.mode = ov5640_mode_800_480,
		.width = 800,
		.height = 480,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 800, 480},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.fps = ov5640_800x480_fps,
		.num_fps = ARRAY_SIZE(ov5640_800x480_fps),
	},
};

static struct regulator *io_regulator;
static struct regulator *core_regulator;
static struct regulator *analog_regulator;

static const struct regmap_config ov5640_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.cache_type = REGCACHE_NONE,
};

static int ov5640_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int ov5640_remove(struct i2c_client *client);
static int ov5640_runtime_suspend(struct device *dev);
static int ov5640_runtime_resume(struct device *dev);

static int ov5640_read_reg(u16 reg, u8 *val);
static int ov5640_write_reg(u16 reg, u8 val);
static int init_device(void);
static int ov5640_read_reg16(u16 reg, u16 *val);
static int ov5640_write_reg16(u16 reg, u16 val);
static int ov5640_write_reg16_low_first(u16 reg, u16 val);
static int ov5640_mod_reg(u16 reg, u8 mask, u8 val);

static const struct dev_pm_ops ov5640_pm_ops = {
	SET_RUNTIME_PM_OPS(ov5640_runtime_suspend, ov5640_runtime_resume, NULL)
};

static const struct i2c_device_id ov5640_id[] = {
	{"ov5640", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = "ov5640",
		  .pm = &ov5640_pm_ops,
		  },
	.probe  = ov5640_probe,
	.remove = ov5640_remove,
	.id_table = ov5640_id,
};

static const struct ov5640_datafmt ov5640_colour_fmts[] = {
	{MEDIA_BUS_FMT_RGB565_2X8_LE, V4L2_COLORSPACE_SRGB},
};

static struct ov5640 *to_ov5640(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov5640, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct ov5640_datafmt
			*ov5640_find_datafmt(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5640_colour_fmts); i++)
		if (ov5640_colour_fmts[i].code == code)
			return ov5640_colour_fmts + i;

	return NULL;
}

static bool ov5640_mode_info_valid(const struct ov5640_mode_info *mode_info)
{
	return mode_info && mode_info->width && mode_info->height &&
	       mode_info->fps && mode_info->num_fps;
}

static const struct ov5640_mode_info *
ov5640_get_mode_info(enum ov5640_mode mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5640_modes); i++) {
		if (ov5640_modes[i].mode == mode)
			return &ov5640_modes[i];
	}

	return NULL;
}

static bool ov5640_mode_valid(enum ov5640_mode mode)
{
	return ov5640_mode_info_valid(ov5640_get_mode_info(mode));
}

static const struct ov5640_mode_fps_info *
ov5640_get_mode_fps_info(const struct ov5640_mode_info *mode_info,
				 enum ov5640_frame_rate frame_rate)
{
	int i;

	if (!ov5640_mode_info_valid(mode_info))
		return NULL;

	for (i = 0; i < mode_info->num_fps; i++) {
		if (mode_info->fps[i].frame_rate == frame_rate &&
		    mode_info->fps[i].regs && mode_info->fps[i].num_regs)
			return &mode_info->fps[i];
	}

	return NULL;
}

static u32 ov5640_abs_diff(u32 a, u32 b)
{
	return a > b ? a - b : b - a;
}

static const struct ov5640_mode_info *
ov5640_find_nearest_mode(enum ov5640_frame_rate frame_rate, u32 width, u32 height)
{
	const struct ov5640_mode_info *best = NULL;
	u32 best_distance = ~0U;
	int i;

	if (frame_rate < ov5640_15_fps || frame_rate > ov5640_30_fps)
		frame_rate = ov5640_30_fps;

	for (i = 0; i < ARRAY_SIZE(ov5640_modes); i++) {
		const struct ov5640_mode_info *mode_info = &ov5640_modes[i];
		u32 distance;

		if (!ov5640_get_mode_fps_info(mode_info, frame_rate))
			continue;

		distance = ov5640_abs_diff(mode_info->width, width) +
			   ov5640_abs_diff(mode_info->height, height);
		if (!best || distance < best_distance) {
			best = mode_info;
			best_distance = distance;
		}
	}

	return best;
}

static const struct ov5640_mode_info *
ov5640_find_mode_by_size(enum ov5640_frame_rate frame_rate, u32 width, u32 height)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5640_modes); i++) {
		const struct ov5640_mode_info *mode_info = &ov5640_modes[i];

		if (!ov5640_get_mode_fps_info(mode_info, frame_rate))
			continue;

		if (mode_info->width == width && mode_info->height == height)
			return mode_info;
	}

	return NULL;
}

static const struct ov5640_mode_info *
ov5640_find_mode(u32 width, u32 height, bool nearest)
{
	const struct ov5640_mode_info *best = NULL;
	u32 best_distance = ~0U;
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5640_modes); i++) {
		const struct ov5640_mode_info *mode_info = &ov5640_modes[i];
		u32 distance;

		if (!ov5640_mode_info_valid(mode_info))
			continue;

		if (mode_info->width == width && mode_info->height == height)
			return mode_info;

		if (!nearest)
			continue;

		distance = ov5640_abs_diff(mode_info->width, width) +
			   ov5640_abs_diff(mode_info->height, height);
		if (!best || distance < best_distance) {
			best = mode_info;
			best_distance = distance;
		}
	}

	return best;
}

static bool ov5640_mode_supports_fps(enum ov5640_mode mode,
				     enum ov5640_frame_rate frame_rate)
{
	return ov5640_get_mode_fps_info(ov5640_get_mode_info(mode),
				       frame_rate) != NULL;
}

static int ov5640_enum_frame_rate_for_mode(enum ov5640_mode mode,
					   unsigned int index,
					   enum ov5640_frame_rate *frame_rate)
{
	int i;
	unsigned int count = 0;

	if (!frame_rate || !ov5640_mode_valid(mode))
		return -EINVAL;

	for (i = ov5640_15_fps; i <= ov5640_30_fps; i++) {
		if (!ov5640_mode_supports_fps(mode, i))
			continue;

		if (count == index) {
			*frame_rate = i;
			return 0;
		}

		count++;
	}

	return -EINVAL;
}

static u32 ov5640_pixelformat_from_code(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		return V4L2_PIX_FMT_RGB565;
	default:
		return 0;
	}
}

static int ov5640_apply_format(const struct ov5640_datafmt *fmt)
{
	int ret;

	if (!fmt)
		return -EINVAL;

	switch (fmt->code) {
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		ret = ov5640_write_reg(OV5640_REG_FORMAT_MUX_CONTROL,
					OV5640_FORMAT_MUX_RGB);
		if (ret < 0)
			return ret;

		return ov5640_write_reg(OV5640_REG_FORMAT_CONTROL00,
					OV5640_FORMAT_CTRL_RGB565);
	default:
		return -EINVAL;
	}
}

static void ov5640_init_default_state(struct ov5640 *sensor)
{
	const struct ov5640_mode_info *mode_info =
		ov5640_get_mode_info(ov5640_mode_800_480);

	mutex_init(&sensor->lock);

	sensor->fmt = &ov5640_colour_fmts[0];
	sensor->current_fr = ov5640_30_fps;
	sensor->current_mode = ov5640_mode_800_480;
	sensor->powered = false;
	sensor->streaming = false;
	sensor->on = false;

	sensor->pix.pixelformat = V4L2_PIX_FMT_RGB565;
	sensor->pix.width = mode_info->width;
	sensor->pix.height = mode_info->height;
	sensor->pix.field = V4L2_FIELD_NONE;
	sensor->pix.colorspace = sensor->fmt->colorspace;

	sensor->streamcap.capability = V4L2_MODE_HIGHQUALITY |
					V4L2_CAP_TIMEPERFRAME;
	sensor->streamcap.capturemode = V4L2_CAP_TIMEPERFRAME;
	sensor->streamcap.timeperframe.denominator = DEFAULT_FPS;
	sensor->streamcap.timeperframe.numerator = 1;
}

static inline void ov5640_set_power_down(struct ov5640 *sensor, bool enable)
{
	gpiod_set_value_cansleep(sensor->pwdn_gpio, enable);
}

static inline void ov5640_power_down(struct ov5640 *sensor, bool enable)
{
	ov5640_set_power_down(sensor, enable);
	msleep(2);
}

static inline void ov5640_set_reset(struct ov5640 *sensor, bool assert)
{
	gpiod_set_value_cansleep(sensor->reset_gpio, assert);
}

static inline void ov5640_reset(struct ov5640 *sensor)
{
	/* Keep the original reset/powerdown timing, but use logical GPIO states. */
	ov5640_set_reset(sensor, false);
	ov5640_set_power_down(sensor, true);
	msleep(5);
	ov5640_set_power_down(sensor, false);
	msleep(5);
	ov5640_set_reset(sensor, true);
	msleep(1);
	ov5640_set_reset(sensor, false);
	msleep(5);
	ov5640_set_power_down(sensor, true);
}

static int ov5640_hw_set_stream(bool enable)
{
	return ov5640_write_reg(OV5640_REG_STREAM_CTRL,
				enable ? OV5640_STREAM_ON : OV5640_STREAM_OFF);
}

static int ov5640_set_stream(struct ov5640 *sensor, bool enable)
{
	int ret;

	if (sensor->streaming == enable)
		return 0;

	if (enable && !sensor->powered)
		return -EPIPE;

	ret = ov5640_hw_set_stream(enable);
	if (ret < 0)
		return ret;

	sensor->streaming = enable;
	return 0;
}

static int ov5640_power_on(struct ov5640 *sensor)
{
	int ret;

	if (sensor->powered)
		return 0;

	ret = clk_prepare_enable(sensor->sensor_clk);
	if (ret < 0)
		return ret;

	if (sensor->io_init)
		sensor->io_init(sensor);

	ov5640_power_down(sensor, false);
	sensor->powered = true;
	sensor->on = true;

	return 0;
}

static void ov5640_power_off(struct ov5640 *sensor)
{
	int ret;

	if (!sensor->powered)
		return;

	if (sensor->streaming) {
		ret = ov5640_set_stream(sensor, false);
		if (ret < 0)
			dev_warn(sensor->dev,
				 "stream off before power down failed: %d\n", ret);
	} else {
		ret = ov5640_hw_set_stream(false);
		if (ret < 0)
			dev_dbg(sensor->dev,
				"stream-off register write failed: %d\n", ret);
	}

	ov5640_power_down(sensor, true);
	clk_disable_unprepare(sensor->sensor_clk);
	sensor->streaming = false;
	sensor->powered = false;
	sensor->on = false;
}

static int ov5640_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov5640 *sensor = to_ov5640(client);
	int ret;

	mutex_lock(&sensor->lock);

	ret = ov5640_power_on(sensor);
	if (ret < 0)
		goto out;

	ret = init_device();
	if (ret < 0)
		ov5640_power_off(sensor);

 out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ov5640_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov5640 *sensor = to_ov5640(client);

	mutex_lock(&sensor->lock);
	if (sensor->streaming) {
		mutex_unlock(&sensor->lock);
		return -EBUSY;
	}

	ov5640_power_off(sensor);
	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov5640_runtime_get(struct ov5640 *sensor)
{
	int ret;

	ret = pm_runtime_get_sync(sensor->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(sensor->dev);
		return ret;
	}

	return 0;
}

static void ov5640_runtime_put_autosuspend(struct ov5640 *sensor)
{
	pm_runtime_mark_last_busy(sensor->dev);
	pm_runtime_put_autosuspend(sensor->dev);
}

static int ov5640_regulator_enable(struct device *dev)
{
	int ret = 0;

	io_regulator = devm_regulator_get(dev, "DOVDD");
	if (!IS_ERR(io_regulator)) {
		regulator_set_voltage(io_regulator,
				      OV5640_VOLTAGE_DIGITAL_IO,
				      OV5640_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(io_regulator);
		if (ret) {
			dev_err(dev, "set io voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set io voltage ok\n");
		}
	} else {
		io_regulator = NULL;
		dev_warn(dev, "cannot get io voltage\n");
	}

	core_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(core_regulator)) {
		regulator_set_voltage(core_regulator,
				      OV5640_VOLTAGE_DIGITAL_CORE,
				      OV5640_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(core_regulator);
		if (ret) {
			dev_err(dev, "set core voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set core voltage ok\n");
		}
	} else {
		core_regulator = NULL;
		dev_warn(dev, "cannot get core voltage\n");
	}

	analog_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(analog_regulator)) {
		regulator_set_voltage(analog_regulator,
				      OV5640_VOLTAGE_ANALOG,
				      OV5640_VOLTAGE_ANALOG);
		ret = regulator_enable(analog_regulator);
		if (ret) {
			dev_err(dev, "set analog voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set analog voltage ok\n");
		}
	} else {
		analog_regulator = NULL;
		dev_warn(dev, "cannot get analog voltage\n");
	}

	return ret;
}

static int ov5640_write_reg(u16 reg, u8 val)
{
	int ret;

	ret = regmap_write(ov5640_data.regmap, reg, val);
	if (ret < 0)
		dev_err(&ov5640_data.i2c_client->dev,
			"write reg 0x%04x=0x%02x failed: %d\n",
			reg, val, ret);

	return ret;
}

static int ov5640_read_reg(u16 reg, u8 *val)
{
	unsigned int regval;
	int ret;

	if (!val)
		return -EINVAL;

	ret = regmap_read(ov5640_data.regmap, reg, &regval);
	if (ret < 0) {
		dev_err(&ov5640_data.i2c_client->dev,
			"read reg 0x%04x failed: %d\n", reg, ret);
		return ret;
	}

	*val = regval & 0xff;
	return 0;
}

static int ov5640_read_reg16(u16 reg, u16 *val)
{
	u8 high, low;
	int ret;

	if (!val)
		return -EINVAL;

	ret = ov5640_read_reg(reg, &high);
	if (ret < 0)
		return ret;

	ret = ov5640_read_reg(reg + 1, &low);
	if (ret < 0)
		return ret;

	*val = ((u16)high << 8) | low;
	return 0;
}

static int ov5640_write_reg16(u16 reg, u16 val)
{
	int ret;

	ret = ov5640_write_reg(reg, val >> 8);
	if (ret < 0)
		return ret;

	return ov5640_write_reg(reg + 1, val & 0xff);
}

static int ov5640_write_reg16_low_first(u16 reg, u16 val)
{
	int ret;

	ret = ov5640_write_reg(reg + 1, val & 0xff);
	if (ret < 0)
		return ret;

	return ov5640_write_reg(reg, val >> 8);
}

static int ov5640_mod_reg(u16 reg, u8 mask, u8 val)
{
	int ret;

	ret = regmap_update_bits(ov5640_data.regmap, reg, mask, val);
	if (ret < 0)
		dev_err(&ov5640_data.i2c_client->dev,
			"update reg 0x%04x mask 0x%02x val 0x%02x failed: %d\n",
			reg, mask, val, ret);

	return ret;
}

static int ov5640_set_flip(struct ov5640 *sensor)
{
	int ret;
	u8 hflip = sensor->hflip->val ? OV5640_TIMING_FLIP_MASK : 0;
	u8 vflip = sensor->vflip->val ? OV5640_TIMING_FLIP_MASK : 0;

	ret = ov5640_mod_reg(OV5640_REG_TIMING_TC_REG21,
				     OV5640_TIMING_FLIP_MASK, hflip);
	if (ret < 0)
		return ret;

	return ov5640_mod_reg(OV5640_REG_TIMING_TC_REG20,
			       OV5640_TIMING_FLIP_MASK, vflip);
}

static int ov5640_set_power_line_frequency(struct ov5640 *sensor)
{
	int ret;

	switch (sensor->power_line_frequency->val) {
	case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
		ret = ov5640_mod_reg(OV5640_REG_BANDING_FILTER_CTRL,
				     OV5640_BANDING_MANUAL_ENABLE,
				     OV5640_BANDING_MANUAL_ENABLE);
		if (ret < 0)
			return ret;

		return ov5640_mod_reg(OV5640_REG_BANDING_FILTER_MAN,
				       OV5640_BANDING_MANUAL_50HZ,
				       OV5640_BANDING_MANUAL_50HZ);
	case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
		ret = ov5640_mod_reg(OV5640_REG_BANDING_FILTER_CTRL,
				     OV5640_BANDING_MANUAL_ENABLE,
				     OV5640_BANDING_MANUAL_ENABLE);
		if (ret < 0)
			return ret;

		return ov5640_mod_reg(OV5640_REG_BANDING_FILTER_MAN,
				       OV5640_BANDING_MANUAL_50HZ, 0);
	case V4L2_CID_POWER_LINE_FREQUENCY_AUTO:
		return ov5640_mod_reg(OV5640_REG_BANDING_FILTER_CTRL,
			       OV5640_BANDING_MANUAL_ENABLE, 0);
	default:
		return -EINVAL;
	}
}

static int ov5640_apply_controls(struct ov5640 *sensor)
{
	int ret;

	if (!sensor->hflip || !sensor->vflip ||
	    !sensor->power_line_frequency)
		return 0;

	ret = ov5640_set_flip(sensor);
	if (ret < 0)
		return ret;

	return ov5640_set_power_line_frequency(sensor);
}

static int ov5640_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5640 *sensor =
		container_of(ctrl->handler, struct ov5640, ctrls);
	int ret = 0;

	mutex_lock(&sensor->lock);
	if (!sensor->powered)
		goto out;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = ov5640_set_flip(sensor);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = ov5640_set_power_line_frequency(sensor);
		break;
	default:
		ret = -EINVAL;
		break;
	}

 out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_ctrl_ops ov5640_ctrl_ops = {
	.s_ctrl = ov5640_s_ctrl,
};

static int ov5640_init_controls(struct ov5640 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrls;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 3);
	if (ret < 0)
		return ret;

	sensor->hflip = v4l2_ctrl_new_std(hdl, &ov5640_ctrl_ops,
					      V4L2_CID_HFLIP, 0, 1, 1, 1);
	sensor->vflip = v4l2_ctrl_new_std(hdl, &ov5640_ctrl_ops,
					      V4L2_CID_VFLIP, 0, 1, 1, 1);
	sensor->power_line_frequency =
		v4l2_ctrl_new_std_menu(hdl, &ov5640_ctrl_ops,
				       V4L2_CID_POWER_LINE_FREQUENCY,
				       V4L2_CID_POWER_LINE_FREQUENCY_AUTO,
				       1 << V4L2_CID_POWER_LINE_FREQUENCY_DISABLED,
				       V4L2_CID_POWER_LINE_FREQUENCY_AUTO);

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	sensor->subdev.ctrl_handler = hdl;
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
/**
 * ov5640_get_register - read one OV5640 register through V4L2 debug ioctl
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @reg: Debug register descriptor.  On entry, @reg->reg is the sensor
 *       register address to read.  On success, @reg->size is set to 1 and
 *       @reg->val contains the 8-bit register value.
 *
 * The OV5640 register map uses 16-bit register addresses and 8-bit register
 * values, so addresses outside 0x0000..0xffff are rejected.
 *
 * Return: 0 on success, -EINVAL for an out-of-range address, or a negative
 * error code from the I2C register read helper.
 */
static int ov5640_get_register(struct v4l2_subdev *sd,
					struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 val;

	if (reg->reg & ~0xffff)
		return -EINVAL;

	reg->size = 1;

	ret = ov5640_read_reg(reg->reg, &val);
	if (ret < 0)
		return ret;

	reg->val = (__u64)val;

	return 0;
}

/**
 * ov5640_set_register - write one OV5640 register through V4L2 debug ioctl
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @reg: Debug register descriptor.  @reg->reg is the sensor register address
 *       to write and @reg->val is the new register value.
 *
 * The OV5640 register map uses 16-bit register addresses and 8-bit register
 * values, so addresses outside 0x0000..0xffff and values outside 0x00..0xff
 * are rejected before touching the bus.
 *
 * Return: 0 on success, -EINVAL for an out-of-range address or value, or a
 * negative error code from the I2C register write helper.
 */
static int ov5640_set_register(struct v4l2_subdev *sd,
					const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg & ~0xffff || reg->val & ~0xff)
		return -EINVAL;

	return ov5640_write_reg(reg->reg, reg->val);
}
#endif

static int ov5640_soft_reset(void)
{
	int ret;

	/* sysclk from pad */
	ret = ov5640_write_reg(0x3103, 0x11);
	if (ret < 0)
		return ret;

	/* software reset */
	ret = ov5640_write_reg(0x3008, 0x82);
	if (ret < 0)
		return ret;

	/* delay at least 5ms */
	msleep(10);
	return 0;
}

/* set sensor driver capability
 * 0x302c[7:6] - strength
	00     - 1x
	01     - 2x
	10     - 3x
	11     - 4x
 */
static int ov5640_driver_capability(int strength)
{
	if (strength > 4 || strength < 1) {
		pr_err("The valid driver capability of ov5640 is 1x~4x\n");
		return -EINVAL;
	}

	return ov5640_mod_reg(OV5640_REG_PAD_OUTPUT00, 0xc0,
				((strength - 1) << 6));
}

/* calculate sysclk */
static int ov5640_get_sysclk(void)
{
	int xvclk = ov5640_data.mclk / 10000;
	int sysclk;
	int temp2;
	int Multiplier, PreDiv, VCO, SysDiv, Pll_rdiv, Bit_div2x, sclk_rdiv;
	int sclk_rdiv_map[] = {1, 2, 4, 8};
	u8 regval = 0;
	int ret;

	ret = ov5640_read_reg(OV5640_REG_SYSCLK_PLL_CTRL0, &regval);
	if (ret < 0)
		return ret;
	temp2 = regval & 0x0f;
	if (temp2 == 8 || temp2 == 10) {
		Bit_div2x = temp2 / 2;
	} else {
		pr_err("ov5640: unsupported bit mode %d\n", temp2);
		return -EINVAL;
	}

	ret = ov5640_read_reg(OV5640_REG_SYSCLK_PLL_CTRL1, &regval);
	if (ret < 0)
		return ret;
	SysDiv = regval >> 4;
	if (SysDiv == 0)
		SysDiv = 16;

	ret = ov5640_read_reg(OV5640_REG_SYSCLK_PLL_MULT, &regval);
	if (ret < 0)
		return ret;
	Multiplier = regval;

	ret = ov5640_read_reg(OV5640_REG_SYSCLK_PLL_PREDIV, &regval);
	if (ret < 0)
		return ret;
	PreDiv = regval & 0x0f;
	if (PreDiv == 0)
		return -EINVAL;
	Pll_rdiv = ((regval >> 4) & 0x01) + 1;

	ret = ov5640_read_reg(OV5640_REG_SYS_ROOT_DIVIDER, &regval);
	if (ret < 0)
		return ret;
	temp2 = regval & 0x03;

	sclk_rdiv = sclk_rdiv_map[temp2];
	VCO = xvclk * Multiplier / PreDiv;
	sysclk = VCO / SysDiv / Pll_rdiv * 2 / Bit_div2x / sclk_rdiv;

	return sysclk;
}

/* read HTS from register settings */
static int ov5640_get_HTS(void)
{
	u16 HTS;
	int ret;

	ret = ov5640_read_reg16(OV5640_REG_TIMING_HTS_H, &HTS);
	if (ret < 0)
		return ret;

	return HTS;
}

/* read VTS from register settings */
static int ov5640_get_VTS(void)
{
	u16 VTS;
	int ret;

	ret = ov5640_read_reg16(OV5640_REG_TIMING_VTS_H, &VTS);
	if (ret < 0)
		return ret;

	return VTS;
}

/* write VTS to registers */
static int ov5640_set_VTS(int VTS)
{
	return ov5640_write_reg16_low_first(OV5640_REG_TIMING_VTS_H,
					  VTS & 0xffff);
}

/* read shutter, in number of line period */
static int ov5640_get_shutter(void)
{
	int shutter;
	u8 regval;
	int ret;

	ret = ov5640_read_reg(OV5640_REG_AEC_PK_EXPOSURE_H, &regval);
	if (ret < 0)
		return ret;
	shutter = regval & 0x0f;

	ret = ov5640_read_reg(OV5640_REG_AEC_PK_EXPOSURE_M, &regval);
	if (ret < 0)
		return ret;
	shutter = (shutter << 8) + regval;

	ret = ov5640_read_reg(OV5640_REG_AEC_PK_EXPOSURE_L, &regval);
	if (ret < 0)
		return ret;
	shutter = (shutter << 4) + (regval >> 4);

	return shutter;
}

/* write shutter, in number of line period */
static int ov5640_set_shutter(int shutter)
{
	int ret;

	shutter &= 0xffff;

	ret = ov5640_write_reg(OV5640_REG_AEC_PK_EXPOSURE_L,
				 (shutter & 0x0f) << 4);
	if (ret < 0)
		return ret;

	ret = ov5640_write_reg(OV5640_REG_AEC_PK_EXPOSURE_M,
				 (shutter >> 4) & 0xff);
	if (ret < 0)
		return ret;

	return ov5640_write_reg(OV5640_REG_AEC_PK_EXPOSURE_H,
				      (shutter >> 12) & 0x0f);
}

/* read gain, 16 = 1x */
static int ov5640_get_gain16(void)
{
	u16 gain16;
	int ret;

	ret = ov5640_read_reg16(OV5640_REG_AEC_PK_REAL_GAIN_H, &gain16);
	if (ret < 0)
		return ret;

	return gain16 & 0x03ff;
}

/* write gain, 16 = 1x */
static int ov5640_set_gain16(int gain16)
{
	return ov5640_write_reg16_low_first(OV5640_REG_AEC_PK_REAL_GAIN_H,
					  gain16 & 0x03ff);
}

/* get banding filter value */
static int ov5640_get_light_freq(void)
{
	int light_frequency;
	u8 regval;
	int ret;

	ret = ov5640_read_reg(OV5640_REG_BANDING_FILTER_CTRL, &regval);
	if (ret < 0)
		return ret;

	if (regval & 0x80) {
		/* manual */
		ret = ov5640_read_reg(OV5640_REG_BANDING_FILTER_MAN, &regval);
		if (ret < 0)
			return ret;
		light_frequency = (regval & 0x04) ? 50 : 60;
	} else {
		/* auto */
		ret = ov5640_read_reg(OV5640_REG_BANDING_FILTER_AUTO, &regval);
		if (ret < 0)
			return ret;
		light_frequency = (regval & 0x01) ? 50 : 60;
	}

	return light_frequency;
}

static int ov5640_set_bandingfilter(void)
{
	int prev_VTS;
	int band_step60, max_band60, band_step50, max_band50;
	int ret;

	/* read preview PCLK */
	prev_sysclk = ov5640_get_sysclk();
	if (prev_sysclk < 0)
		return prev_sysclk;

	/* read preview HTS */
	prev_HTS = ov5640_get_HTS();
	if (prev_HTS < 0)
		return prev_HTS;

	/* read preview VTS */
	prev_VTS = ov5640_get_VTS();
	if (prev_VTS < 0)
		return prev_VTS;

	if (prev_HTS == 0 || prev_VTS <= 4)
		return -EINVAL;

	/* calculate banding filter */
	/* 60Hz */
	band_step60 = prev_sysclk * 100 / prev_HTS * 100 / 120;
	if (band_step60 <= 0)
		return -EINVAL;

	ret = ov5640_write_reg16(OV5640_REG_BANDING_60_STEP_H, band_step60);
	if (ret < 0)
		return ret;

	max_band60 = (int)((prev_VTS - 4) / band_step60);
	ret = ov5640_write_reg(OV5640_REG_BANDING_60_MAX, max_band60);
	if (ret < 0)
		return ret;

	/* 50Hz */
	band_step50 = prev_sysclk * 100 / prev_HTS;
	if (band_step50 <= 0)
		return -EINVAL;

	ret = ov5640_write_reg16(OV5640_REG_BANDING_50_STEP_H, band_step50);
	if (ret < 0)
		return ret;

	max_band50 = (int)((prev_VTS - 4) / band_step50);
	return ov5640_write_reg(OV5640_REG_BANDING_50_MAX, max_band50);
}

/* stable in high */
static int ov5640_set_AE_target(int target)
{
	int fast_high, fast_low;
	int ret;

	AE_low = target * 23 / 25; /* 0.92 */
	AE_high = target * 27 / 25; /* 1.08 */
	fast_high = AE_high << 1;

	if (fast_high > 255)
		fast_high = 255;
	fast_low = AE_low >> 1;

	ret = ov5640_write_reg(OV5640_REG_AEC_STABLE_HIGH, AE_high);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(OV5640_REG_AEC_STABLE_LOW, AE_low);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(OV5640_REG_AEC_CTRL1B, AE_high);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(OV5640_REG_AEC_CTRL1E, AE_low);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(OV5640_REG_AEC_FAST_HIGH, fast_high);
	if (ret < 0)
		return ret;
	return ov5640_write_reg(OV5640_REG_AEC_FAST_LOW, fast_low);
}

/* enable = 0 to turn off night mode
   enable = 1 to turn on night mode */
static int ov5640_set_night_mode(int enable)
{
	return ov5640_mod_reg(OV5640_REG_AEC_CTRL00, 0x04,
				 enable ? 0x04 : 0x00);
}

/* enable = 0 to turn off AEC/AGC
   enable = 1 to turn on AEC/AGC */
static int ov5640_turn_on_AE_AG(int enable)
{
	return ov5640_mod_reg(OV5640_REG_AEC_PK_MANUAL, 0x03,
				 enable ? 0x00 : 0x03);
}

/* download ov5640 settings to sensor through i2c */
static int ov5640_download_firmware(const struct reg_value *pModeSetting,
					    s32 ArySize)
{
	u32 Delay_ms;
	u16 RegAddr;
	u8 Mask;
	u8 Val;
	int i, retval = 0;

	for (i = 0; i < ArySize; ++i, ++pModeSetting) {
		Delay_ms = pModeSetting->u32Delay_ms;
		RegAddr = pModeSetting->u16RegAddr;
		Val = pModeSetting->u8Val;
		Mask = pModeSetting->u8Mask;

		if (Mask)
			retval = ov5640_mod_reg(RegAddr, Mask, Val);
		else
			retval = ov5640_write_reg(RegAddr, Val);
		if (retval < 0)
			return retval;

		if (Delay_ms)
			msleep(Delay_ms);
	}

	return retval;
}

/**
 * ov5640_init_mode - initialize the sensor into the default VGA mode
 *
 * Soft-reset the OV5640, download the common sensor initialization table,
 * then apply the default 30 fps VGA register table.  After the register
 * programming succeeds, configure drive strength, anti-banding, AE target,
 * and night mode state, then wait for several frames before exposing the
 * initialized 640x480 state to the rest of the driver.
 *
 * Return: 0 on success, or a negative error code when programming the sensor
 * fails.
 */
static int ov5640_init_mode(void)
{
	const struct reg_value *pModeSetting = NULL;
	int ArySize = 0, retval = 0;

	retval = ov5640_soft_reset();
	if (retval < 0)
		goto err;

	pModeSetting = ov5640_global_init_setting;
	ArySize = ARRAY_SIZE(ov5640_global_init_setting);
	retval = ov5640_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	pModeSetting = ov5640_init_setting_30fps_VGA;
	ArySize = ARRAY_SIZE(ov5640_init_setting_30fps_VGA);
	retval = ov5640_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	/* change driver capability to 1x.
	 * 2x may cause image instability on some boards.
	 */
	retval = ov5640_driver_capability(1);
	if (retval < 0)
		goto err;
	retval = ov5640_set_bandingfilter();
	if (retval < 0)
		goto err;
	retval = ov5640_set_AE_target(AE_Target);
	if (retval < 0)
		goto err;
	retval = ov5640_set_night_mode(night_mode);
	if (retval < 0)
		goto err;

	/* skip 9 vysnc: start capture at 10th vsync */
	msleep(300);

	/* turn off night mode */
	night_mode = 0;
	ov5640_data.current_fr = ov5640_30_fps;
	ov5640_data.current_mode = ov5640_mode_VGA_640_480;
	ov5640_data.pix.width = 640;
	ov5640_data.pix.height = 480;
err:
	return retval;
}

/* change to or back to subsampling mode set the mode directly
 * image size below 1280 * 960 is subsampling mode */
static int ov5640_change_mode_direct(enum ov5640_frame_rate frame_rate,
			    enum ov5640_mode mode)
{
	const struct ov5640_mode_info *mode_info;
	const struct ov5640_mode_fps_info *fps_info;
	const struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;

	if (frame_rate > ov5640_30_fps || frame_rate < ov5640_15_fps ||
	    mode > ov5640_mode_MAX || mode < ov5640_mode_MIN) {
		pr_err("Wrong ov5640 mode detected!\n");
		return -EINVAL;
	}

	mode_info = ov5640_get_mode_info(mode);
	fps_info = ov5640_get_mode_fps_info(mode_info, frame_rate);
	if (!fps_info)
		return -EINVAL;

	pModeSetting = fps_info->regs;
	ArySize = fps_info->num_regs;

	/* set ov5640 to subsampling mode */
	retval = ov5640_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	/* turn on AE AG for subsampling mode, in case the firmware didn't */
	retval = ov5640_turn_on_AE_AG(1);
	if (retval < 0)
		goto err;

	/* calculate banding filter */
	retval = ov5640_set_bandingfilter();
	if (retval < 0)
		goto err;

	/* set AE target */
	retval = ov5640_set_AE_target(AE_Target);
	if (retval < 0)
		goto err;

	/* update night mode setting */
	retval = ov5640_set_night_mode(night_mode);
	if (retval < 0)
		goto err;

	/* skip 9 vysnc: start capture at 10th vsync */
	if (mode == ov5640_mode_XGA_1024_768 && frame_rate == ov5640_30_fps) {
		pr_warning("ov5640: actual frame rate of XGA is 22.5fps\n");
		/* 1/22.5 * 9*/
		msleep(400);
		return retval;
	}

	if (frame_rate == ov5640_15_fps) {
		/* 1/15 * 9*/
		msleep(600);
	} else if (frame_rate == ov5640_30_fps) {
		/* 1/30 * 9*/
		msleep(300);
	}

err:
	return retval;
}

/* change to scaling mode go through exposure calucation
 * image size above 1280 * 960 is scaling mode */
static int ov5640_change_mode_exposure_calc(enum ov5640_frame_rate frame_rate,
			    enum ov5640_mode mode)
{
	const struct ov5640_mode_info *mode_info;
	const struct ov5640_mode_fps_info *fps_info;
	int prev_shutter, prev_gain16, average;
	int cap_shutter, cap_gain16;
	int cap_sysclk, cap_HTS, cap_VTS;
	int light_freq, cap_bandfilt, cap_maxband;
	long cap_gain16_shutter;
	u8 temp;
	const struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;

	/* check if the input mode and frame rate is valid */
	if (frame_rate > ov5640_30_fps || frame_rate < ov5640_15_fps ||
	    mode > ov5640_mode_MAX || mode < ov5640_mode_MIN)
		return -EINVAL;

	mode_info = ov5640_get_mode_info(mode);
	fps_info = ov5640_get_mode_fps_info(mode_info, frame_rate);
	if (!fps_info)
		return -EINVAL;

	pModeSetting = fps_info->regs;
	ArySize = fps_info->num_regs;

	/* read preview shutter */
	prev_shutter = ov5640_get_shutter();
	if (prev_shutter < 0)
		return prev_shutter;

	/* read preview gain */
	prev_gain16 = ov5640_get_gain16();
	if (prev_gain16 < 0)
		return prev_gain16;

	/* get average */
	retval = ov5640_read_reg(OV5640_REG_AVG_READOUT, &temp);
	if (retval < 0)
		return retval;
	average = temp;

	/* turn off night mode for capture */
	retval = ov5640_set_night_mode(0);
	if (retval < 0)
		return retval;

	/* turn off overlay */
	retval = ov5640_write_reg(OV5640_REG_FREX_CTRL, 0x06);
	if (retval < 0)
		return retval;

	/* Write capture setting */
	retval = ov5640_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	/* turn off AE AG when capture image. */
	retval = ov5640_turn_on_AE_AG(0);
	if (retval < 0)
		goto err;

	/* read capture VTS */
	cap_VTS = ov5640_get_VTS();
	if (cap_VTS < 0)
		return cap_VTS;
	cap_HTS = ov5640_get_HTS();
	if (cap_HTS < 0)
		return cap_HTS;
	cap_sysclk = ov5640_get_sysclk();
	if (cap_sysclk < 0)
		return cap_sysclk;

	if (prev_sysclk <= 0 || prev_HTS <= 0 || cap_HTS <= 0 || cap_VTS <= 4)
		return -EINVAL;

	/* calculate capture banding filter */
	light_freq = ov5640_get_light_freq();
	if (light_freq < 0)
		return light_freq;
	if (light_freq == 60) {
		/* 60Hz */
		cap_bandfilt = cap_sysclk * 100 / cap_HTS * 100 / 120;
	} else {
		/* 50Hz */
		cap_bandfilt = cap_sysclk * 100 / cap_HTS;
	}
	if (cap_bandfilt <= 0)
		return -EINVAL;
	cap_maxband = (int)((cap_VTS - 4) / cap_bandfilt);
	/* calculate capture shutter/gain16 */
	if (average > AE_low && average < AE_high) {
		/* in stable range */
		cap_gain16_shutter =
			prev_gain16 * prev_shutter * cap_sysclk/prev_sysclk *
			prev_HTS/cap_HTS * AE_Target / average;
	} else {
		cap_gain16_shutter =
			prev_gain16 * prev_shutter * cap_sysclk/prev_sysclk *
			prev_HTS/cap_HTS;
	}

	/* gain to shutter */
	if (cap_gain16_shutter < (cap_bandfilt * 16)) {
		/* shutter < 1/100 */
		cap_shutter = cap_gain16_shutter/16;
		if (cap_shutter < 1)
			cap_shutter = 1;
		cap_gain16 = cap_gain16_shutter/cap_shutter;
		if (cap_gain16 < 16)
			cap_gain16 = 16;
	} else {
		if (cap_gain16_shutter > (cap_bandfilt*cap_maxband*16)) {
			/* exposure reach max */
			cap_shutter = cap_bandfilt*cap_maxband;
			cap_gain16 = cap_gain16_shutter / cap_shutter;
		} else {
			/* 1/100 < cap_shutter =< max, cap_shutter = n/100 */
			cap_shutter =
				((int)(cap_gain16_shutter/16/cap_bandfilt))
				* cap_bandfilt;
			cap_gain16 = cap_gain16_shutter / cap_shutter;
		}
	}

	/* write capture gain */
	retval = ov5640_set_gain16(cap_gain16);
	if (retval < 0)
		goto err;

	/* write capture shutter */
	if (cap_shutter > (cap_VTS - 4)) {
		cap_VTS = cap_shutter + 4;
		retval = ov5640_set_VTS(cap_VTS);
		if (retval < 0)
			goto err;
	}

	retval = ov5640_set_shutter(cap_shutter);
	if (retval < 0)
		goto err;

	/* skip 2 vysnc: start capture at 3rd vsync
	 * frame rate of QSXGA and 1080P is 7.5fps: 1/7.5 * 2
	 */
	pr_warning("ov5640: the actual frame rate of %s is 7.5fps\n",
		mode == ov5640_mode_1080P_1920_1080 ? "1080P" : "QSXGA");
	msleep(267);
err:
	return retval;
}

static int ov5640_change_mode(enum ov5640_frame_rate frame_rate,
			    enum ov5640_mode mode)
{
	const struct ov5640_mode_info *mode_info;
	int retval = 0;

	if (frame_rate > ov5640_30_fps || frame_rate < ov5640_15_fps ||
	    mode > ov5640_mode_MAX || mode < ov5640_mode_MIN) {
		pr_err("Wrong ov5640 mode detected!\n");
		return -EINVAL;
	}

	mode_info = ov5640_get_mode_info(mode);
	if (!ov5640_get_mode_fps_info(mode_info, frame_rate))
		return -EINVAL;

	switch (mode_info->downsize) {
	case OV5640_DOWNSIZE_SCALING:
		retval = ov5640_change_mode_exposure_calc(frame_rate, mode);
		break;
	case OV5640_DOWNSIZE_SUBSAMPLING:
		retval = ov5640_change_mode_direct(frame_rate, mode);
		break;
	default:
		return -EINVAL;
	}

	if (retval == 0) {
		ov5640_data.current_fr = frame_rate;
		ov5640_data.current_mode = mode;
		ov5640_data.pix.width = mode_info->width;
		ov5640_data.pix.height = mode_info->height;
	}

	return retval;
}

/*!
 * ov5640_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
static int ov5640_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *sensor = to_ov5640(client);
	int ret;

	if (on) {
		mutex_lock(&sensor->lock);
		if (sensor->power_ref) {
			mutex_unlock(&sensor->lock);
			return 0;
		}
		mutex_unlock(&sensor->lock);

		ret = ov5640_runtime_get(sensor);
		if (ret < 0)
			return ret;

		mutex_lock(&sensor->lock);
		if (sensor->power_ref) {
			mutex_unlock(&sensor->lock);
			ov5640_runtime_put_autosuspend(sensor);
			return 0;
		}
		sensor->power_ref = true;
		mutex_unlock(&sensor->lock);

		return 0;
	}

	mutex_lock(&sensor->lock);
	if (!sensor->power_ref) {
		mutex_unlock(&sensor->lock);
		return 0;
	}
	sensor->power_ref = false;
	mutex_unlock(&sensor->lock);

	ov5640_runtime_put_autosuspend(sensor);
	return 0;
}

/**
 * ov5640_g_parm - return the current capture stream parameters
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @a: V4L2 stream parameter container to fill.
 *
 * Only V4L2_BUF_TYPE_VIDEO_CAPTURE is supported.  The callback copies the
 * cached capture capability, frame interval, and capture mode from the sensor
 * state into @a.
 *
 * Return: 0 on success, or -EINVAL if @a->type is not supported.
 */
static int ov5640_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *sensor = to_ov5640(client);
	bool started_streaming;
	bool was_streaming;
	int ret;

	if (enable) {
		mutex_lock(&sensor->lock);
		if (sensor->streaming) {
			mutex_unlock(&sensor->lock);
			return 0;
		}
		mutex_unlock(&sensor->lock);

		ret = ov5640_runtime_get(sensor);
		if (ret < 0)
			return ret;

		mutex_lock(&sensor->lock);
		if (sensor->streaming) {
			started_streaming = false;
			ret = 0;
		} else {
			ret = ov5640_set_stream(sensor, true);
			started_streaming = ret == 0;
		}
		mutex_unlock(&sensor->lock);

		if (ret < 0 || !started_streaming)
			ov5640_runtime_put_autosuspend(sensor);

		return ret;
	}

	mutex_lock(&sensor->lock);
	was_streaming = sensor->streaming;
	if (was_streaming)
		ret = ov5640_set_stream(sensor, false);
	else
		ret = 0;
	mutex_unlock(&sensor->lock);

	if (was_streaming && ret == 0)
		ov5640_runtime_put_autosuspend(sensor);

	return ret;
}

static int ov5640_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *sensor = to_ov5640(client);
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ov5640_s_parm - update the requested capture stream parameters
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @a: V4L2 stream parameter container supplied by the caller.
 *
 * Only V4L2_BUF_TYPE_VIDEO_CAPTURE is supported.  The callback normalizes the
 * requested frame interval to one of the discrete 15 fps and 30 fps modes,
 * checks that the current sensor mode supports it, and applies the matching
 * register table immediately when the device is powered and not streaming.
 *
 * Return: 0 on success, -EBUSY if streaming is active, or -EINVAL if the
 * buffer type, frame rate, or current mode/fps combination is not supported.
 */
static int ov5640_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *sensor = to_ov5640(client);
	struct v4l2_captureparm *cparm = &a->parm.capture;
	struct v4l2_fract *timeperframe = &cparm->timeperframe;
	const struct ov5640_mode_info *mode_info;
	const struct ov5640_datafmt *fmt;
	u32 tgt_fps;
	enum ov5640_frame_rate frame_rate;
	int ret = 0;

	switch (a->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (timeperframe->numerator == 0 ||
		    timeperframe->denominator == 0) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps > MAX_FPS) {
			timeperframe->denominator = MAX_FPS;
			timeperframe->numerator = 1;
		} else if (tgt_fps < MIN_FPS) {
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps == ov5640_framerates[ov5640_15_fps])
			frame_rate = ov5640_15_fps;
		else if (tgt_fps == ov5640_framerates[ov5640_30_fps])
			frame_rate = ov5640_30_fps;
		else {
			pr_err("The camera frame rate is not supported!\n");
			return -EINVAL;
		}

		timeperframe->numerator = 1;
		timeperframe->denominator = ov5640_framerates[frame_rate];

		mutex_lock(&sensor->lock);

		mode_info = ov5640_get_mode_info(sensor->current_mode);
		if (!ov5640_get_mode_fps_info(mode_info, frame_rate)) {
			ret = -EINVAL;
			goto unlock;
		}

		if (sensor->streaming && frame_rate != sensor->current_fr) {
			ret = -EBUSY;
			goto unlock;
		}

		if (sensor->powered && frame_rate != sensor->current_fr) {
			ret = ov5640_change_mode(frame_rate, sensor->current_mode);
			if (ret < 0)
				goto unlock;

			fmt = sensor->fmt ? sensor->fmt : &ov5640_colour_fmts[0];
			ret = ov5640_apply_format(fmt);
			if (ret < 0)
				goto unlock;

			ret = ov5640_apply_controls(sensor);
			if (ret < 0)
				goto unlock;

			ret = ov5640_hw_set_stream(false);
			if (ret < 0)
				goto unlock;
		}

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode = cparm->capturemode;
		sensor->current_fr = frame_rate;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;

unlock:
		mutex_unlock(&sensor->lock);
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		pr_debug("   type is not "
			"V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n", a->type);
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ov5640_try_fmt - validate a requested media bus frame format
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @mf: Media bus frame format to validate and adjust.
 *
 * Unsupported media bus codes are adjusted to the verified RGB565 path.  The
 * requested size is snapped to the nearest mode that is valid for the current
 * frame-rate cache; this callback never writes sensor registers.
 *
 * Return: 0 on success, or -EINVAL if no valid mode exists.
 */
static int ov5640_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *sensor = to_ov5640(client);
	const struct ov5640_datafmt *fmt = ov5640_find_datafmt(mf->code);
	const struct ov5640_mode_info *mode_info;

	if (!fmt)
		fmt = &ov5640_colour_fmts[0];

	mode_info = ov5640_find_nearest_mode(sensor->current_fr,
						mf->width, mf->height);
	if (!mode_info)
		return -EINVAL;

	mf->code = fmt->code;
	mf->colorspace = fmt->colorspace;
	mf->field = V4L2_FIELD_NONE;
	mf->width = mode_info->width;
	mf->height = mode_info->height;

	return 0;
}

/**
 * ov5640_s_fmt - program the active media bus format
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @mf: Requested media bus frame format, updated with the active frame size.
 *
 * This callback applies the same adjustment as ov5640_try_fmt(), then programs
 * the selected mode and the verified RGB565 output registers when the sensor is
 * powered.  If called while powered off, it only updates the cached request;
 * init_device() will apply that cache on the next power-on.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ov5640_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *sensor = to_ov5640(client);
	const struct ov5640_datafmt *fmt;
	const struct ov5640_mode_info *mode_info;
	int retval;

	retval = ov5640_try_fmt(sd, mf);
	if (retval < 0)
		return retval;

	fmt = ov5640_find_datafmt(mf->code);
	mode_info = ov5640_find_mode_by_size(sensor->current_fr,
						mf->width, mf->height);
	if (!fmt || !mode_info)
		return -EINVAL;

	mutex_lock(&sensor->lock);
	if (sensor->streaming) {
		retval = -EBUSY;
		goto out;
	}

	if (sensor->powered) {
		retval = ov5640_change_mode(sensor->current_fr, mode_info->mode);
		if (retval < 0)
			goto out;

		retval = ov5640_apply_format(fmt);
		if (retval < 0)
			goto out;

		retval = ov5640_apply_controls(sensor);
		if (retval < 0)
			goto out;

		retval = ov5640_hw_set_stream(false);
		if (retval < 0)
			goto out;
	}

	sensor->fmt = fmt;
	sensor->current_mode = mode_info->mode;
	sensor->pix.pixelformat = ov5640_pixelformat_from_code(fmt->code);
	sensor->pix.width = mf->width;
	sensor->pix.height = mf->height;
	sensor->pix.field = mf->field;
	sensor->pix.colorspace = mf->colorspace;

	retval = 0;
out:
	mutex_unlock(&sensor->lock);
	return retval;
}

/**
 * ov5640_g_fmt - return the cached media bus format
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @mf: Media bus frame format container to fill.
 *
 * The callback reports the media bus code and colorspace cached in the sensor
 * state and forces progressive field order.
 *
 * Return: 0.
 */
static int ov5640_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *sensor = to_ov5640(client);

	const struct ov5640_datafmt *fmt = sensor->fmt ? sensor->fmt :
					   &ov5640_colour_fmts[0];

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;
	mf->width	= sensor->pix.width;
	mf->height	= sensor->pix.height;

	return 0;
}

/**
 * ov5640_enum_fmt - enumerate supported media bus pixel codes
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @index: Zero-based format table index to enumerate.
 * @code: Destination for the media bus code at @index.
 *
 * Return: 0 on success, or -EINVAL when @index is outside the supported
 * format table.
 */
static int ov5640_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   u32 *code)
{
	if (index >= ARRAY_SIZE(ov5640_colour_fmts))
		return -EINVAL;

	*code = ov5640_colour_fmts[index].code;
	return 0;
}

/*!
 * ov5640_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ov5640_enum_framesizes(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	unsigned int count = 0;
	int i;

	if (!ov5640_find_datafmt(fse->code))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ov5640_modes); i++) {
		const struct ov5640_mode_info *mode_info = &ov5640_modes[i];

		if (!ov5640_mode_info_valid(mode_info))
			continue;

		if (count++ != fse->index)
			continue;

		fse->min_width = mode_info->width;
		fse->max_width = mode_info->width;
		fse->min_height = mode_info->height;
		fse->max_height = mode_info->height;
		return 0;
	}

	return -EINVAL;
}


/*!
 * ov5640_enum_frameintervals - V4L2 sensor interface handler for
 *			       VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ov5640_enum_frameintervals(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	const struct ov5640_mode_info *mode_info;
	enum ov5640_frame_rate frame_rate;
	int ret;

	if (fie->width == 0 || fie->height == 0 || fie->code == 0) {
		pr_warning("Please assign pixel format, width and height.\n");
		return -EINVAL;
	}

	if (!ov5640_find_datafmt(fie->code))
		return -EINVAL;

	mode_info = ov5640_find_mode(fie->width, fie->height, false);
	if (!mode_info)
		return -EINVAL;

	ret = ov5640_enum_frame_rate_for_mode(mode_info->mode,
					       fie->index, &frame_rate);
	if (ret < 0)
		return ret;

	fie->interval.numerator = 1;
	fie->interval.denominator = ov5640_framerates[frame_rate];
	return 0;
}

static int ov5640_set_clk_rate(void)
{
	u32 tgt_xclk;	/* target xclk */
	int ret;

	/* mclk */
	tgt_xclk = ov5640_data.mclk;
	tgt_xclk = min(tgt_xclk, (u32)OV5640_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)OV5640_XCLK_MIN);
	ov5640_data.mclk = tgt_xclk;

	pr_debug("   Setting mclk to %d MHz\n", tgt_xclk / 1000000);
	ret = clk_set_rate(ov5640_data.sensor_clk, ov5640_data.mclk);
	if (ret < 0)
		pr_debug("set rate filed, rate=%d\n", ov5640_data.mclk);
	return ret;
}

/*!
 * dev_init - V4L2 sensor init
 * 
 * init device according to the mclk and fps(ov5640_data.streamcap.timeperframe) in ov5640_data
 */
static int init_device(void)
{
	u32 tgt_xclk;	/* target xclk */
	u32 tgt_fps;	/* target frames per secound */
	enum ov5640_frame_rate frame_rate;
	enum ov5640_mode target_mode = ov5640_data.current_mode;
	const struct ov5640_mode_info *mode_info;
	int ret;

	ov5640_data.on = true;

	/* mclk */
	tgt_xclk = ov5640_data.mclk;

	/* Default camera frame rate is set in probe */
	tgt_fps = ov5640_data.streamcap.timeperframe.denominator /
		  ov5640_data.streamcap.timeperframe.numerator;

	if (tgt_fps == 15)
		frame_rate = ov5640_15_fps;
	else if (tgt_fps == 30)
		frame_rate = ov5640_30_fps;
	else
		return -EINVAL; /* Only support 15fps or 30fps now. */

	mode_info = ov5640_get_mode_info(target_mode);
	if (!ov5640_get_mode_fps_info(mode_info, frame_rate)) {
		mode_info = ov5640_find_nearest_mode(frame_rate,
						ov5640_data.pix.width,
						ov5640_data.pix.height);
		if (!mode_info)
			return -EINVAL;
		target_mode = mode_info->mode;
	}

	ret = ov5640_init_mode();
	if (ret < 0)
		return ret;

	if (ov5640_data.current_fr != frame_rate ||
	    ov5640_data.current_mode != target_mode) {
		ret = ov5640_change_mode(frame_rate, target_mode);
		if (ret < 0)
			return ret;
	}

	ret = ov5640_apply_format(ov5640_data.fmt);
	if (ret < 0)
		return ret;

	ret = ov5640_apply_controls(&ov5640_data);
	if (ret < 0)
		return ret;

	ret = ov5640_hw_set_stream(false);
	if (ret < 0)
		return ret;

	ov5640_data.streaming = false;
	return 0;
}

static struct v4l2_subdev_video_ops ov5640_subdev_video_ops = {
	.g_parm = ov5640_g_parm,
	.s_parm = ov5640_s_parm,
	.s_stream = ov5640_s_stream,

	.s_mbus_fmt	= ov5640_s_fmt,
	.g_mbus_fmt	= ov5640_g_fmt,
	.try_mbus_fmt	= ov5640_try_fmt,
	.enum_mbus_fmt	= ov5640_enum_fmt,
};

static const struct v4l2_subdev_pad_ops ov5640_subdev_pad_ops = {
	.enum_frame_size       = ov5640_enum_framesizes,
	.enum_frame_interval   = ov5640_enum_frameintervals,
};

static struct v4l2_subdev_core_ops ov5640_subdev_core_ops = {
	.s_power	= ov5640_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov5640_get_register,
	.s_register	= ov5640_set_register,
#endif
};

static struct v4l2_subdev_ops ov5640_subdev_ops = {
	.core	= &ov5640_subdev_core_ops,
	.video	= &ov5640_subdev_video_ops,
	.pad	= &ov5640_subdev_pad_ops,
};

/*!
 * ov5640 I2C probe function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int ov5640_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	int retval;
	u8 chip_id_high, chip_id_low;

	/* ov5640 pinctrl */
	/* driver core will bind pinctrl before probe in really_probe()(linux/drivers/base/dd.c), so actually it's not necessary here. */
	// pinctrl = devm_pinctrl_get_select_default(dev);
	// if (IS_ERR(pinctrl)) {
	// 	dev_err(dev, "setup pinctrl failed\n");
	// 	return PTR_ERR(pinctrl);
	// }

	/* Set initial values for the sensor struct. */
	/* 全局单实例 */
	memset(&ov5640_data, 0, sizeof(ov5640_data));
	ov5640_data.dev = dev;
	ov5640_data.i2c_client = client;

	ov5640_data.pwdn_gpio = devm_gpiod_get_optional(dev, "pwn",
							 GPIOD_OUT_HIGH);
	if (IS_ERR(ov5640_data.pwdn_gpio)) {
		retval = PTR_ERR(ov5640_data.pwdn_gpio);
		dev_err(dev, "failed to get sensor pwdn GPIO: %d\n", retval);
		return retval;
	}
	if (!ov5640_data.pwdn_gpio) {
		dev_err(dev, "no sensor pwdn pin available\n");
		return -ENODEV;
	}

	ov5640_data.reset_gpio = devm_gpiod_get_optional(dev, "rst",
							  GPIOD_OUT_LOW);
	if (IS_ERR(ov5640_data.reset_gpio)) {
		retval = PTR_ERR(ov5640_data.reset_gpio);
		dev_err(dev, "failed to get sensor reset GPIO: %d\n", retval);
		return retval;
	}
	if (!ov5640_data.reset_gpio) {
		dev_err(dev, "no sensor reset pin available\n");
		return -ENODEV;
	}
	ov5640_data.regmap = devm_regmap_init_i2c(client, &ov5640_regmap_config);
	if (IS_ERR(ov5640_data.regmap)) {
		retval = PTR_ERR(ov5640_data.regmap);
		dev_err(dev, "regmap init failed: %d\n", retval);
		return retval;
	}

	ov5640_data.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(ov5640_data.sensor_clk)) {
		dev_err(dev, "get mclk failed\n");
		return PTR_ERR(ov5640_data.sensor_clk);
	}

	retval = of_property_read_u32(dev->of_node, "mclk",
					&ov5640_data.mclk);
	if (retval) {
		dev_err(dev, "mclk frequency is invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",
					(u32 *) &(ov5640_data.mclk_source));
	if (retval) {
		dev_err(dev, "mclk_source invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "csi_id",
					&(ov5640_data.csi));
	if (retval) {
		dev_err(dev, "csi_id invalid\n");
		return retval;
	}

	/* Set mclk rate before clk on */
	retval = ov5640_set_clk_rate();
	if (retval < 0)
		return retval;

	ov5640_data.io_init = ov5640_reset;
	ov5640_init_default_state(&ov5640_data);

	/* 正点原子 ov5640 模块只需3.3v供电，应该模块内部给了不同电压，故无需此步骤 */
	// ov5640_regulator_enable(&client->dev);

	retval = ov5640_power_on(&ov5640_data);
	if (retval < 0)
		return retval;

	/* read register to make sure the device is OV5640 */
	retval = ov5640_read_reg(OV5640_CHIP_ID_HIGH_BYTE, &chip_id_high);
	if (retval < 0 || chip_id_high != 0x56) {
		pr_warning("camera ov5640 is not found\n");
		retval = -ENODEV;
		goto power_off;
	}
	retval = ov5640_read_reg(OV5640_CHIP_ID_LOW_BYTE, &chip_id_low);
	if (retval < 0 || chip_id_low != 0x40) {
		pr_warning("camera ov5640 is not found\n");
		retval = -ENODEV;
		goto power_off;
	}

	retval = init_device();
	if (retval < 0) {
		pr_warning("camera ov5640 init failed\n");
		goto power_off;
	}

	/*
	 * will do:
	 * v4l2_set_subdevdata(sd, client);
	 * i2c_set_clientdata(client, sd);
	 */
	v4l2_i2c_subdev_init(&ov5640_data.subdev, client, &ov5640_subdev_ops);

	retval = ov5640_init_controls(&ov5640_data);
	if (retval < 0) {
		dev_err(&client->dev, "control init failed: %d\n", retval);
		goto power_off;
	}

	retval = v4l2_async_register_subdev(&ov5640_data.subdev);
	if (retval < 0) {
		dev_err(&client->dev,
					"%s--Async register failed, ret=%d\n", __func__, retval);
		goto free_ctrls;
	}

	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, OV5640_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);

	pr_info("camera ov5640, is found\n");
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&ov5640_data.ctrls);
power_off:
	ov5640_power_off(&ov5640_data);
	return retval;
}

/*!
 * ov5640 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int ov5640_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&ov5640_data.ctrls);

	mutex_lock(&ov5640_data.lock);
	ov5640_data.power_ref = false;
	ov5640_power_off(&ov5640_data);
	if (!ov5640_data.powered)
		ov5640_power_down(&ov5640_data, true);
	mutex_unlock(&ov5640_data.lock);
	pm_runtime_set_suspended(&client->dev);

	if (analog_regulator)
		regulator_disable(analog_regulator);

	if (core_regulator)
		regulator_disable(core_regulator);

	if (io_regulator)
		regulator_disable(io_regulator);

	return 0;
}

module_i2c_driver(ov5640_i2c_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("OV5640 Camera Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
