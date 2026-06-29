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
#include <linux/slab.h>
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

enum ov5640_frame_size {
	ov5640_frame_size_MIN = 0,
	ov5640_frame_size_VGA_640_480 = 0,
	ov5640_frame_size_QVGA_320_240 = 1,
	ov5640_frame_size_NTSC_720_480 = 2,
	ov5640_frame_size_PAL_720_576 = 3,
	ov5640_frame_size_720P_1280_720 = 4,
	ov5640_frame_size_1080P_1920_1080 = 5,
	ov5640_frame_size_QSXGA_2592_1944 = 6,
	ov5640_frame_size_QCIF_176_144 = 7,
	ov5640_frame_size_XGA_1024_768 = 8,
	ov5640_frame_size_800_480 = 9,
	ov5640_frame_size_MAX = 9
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
	u32 code; /* MEDIA_BUS_FMT_*，描述 subdev 到 host 的总线像素编码 */
	enum v4l2_colorspace colorspace; /* V4L2_COLORSPACE_*，描述像素值的色彩空间 */
};

struct reg_value {
	u16 reg;
	u8 val;
	u8 mask;
	u32 delay_ms;
};

enum ov5640_downsize_mode {
	OV5640_DOWNSIZE_SUBSAMPLING,
	OV5640_DOWNSIZE_SCALING,
};

struct ov5640_mode_reg_table {
	enum ov5640_frame_rate frame_rate;
	const struct reg_value *regs;
	u32 num_regs;
};

struct ov5640_mode_info {
	enum ov5640_frame_size frame_size;
	u32 width;
	u32 height;
	enum ov5640_downsize_mode downsize;
	struct v4l2_rect analog_crop;
	struct v4l2_rect crop;
	u32 htot;
	u32 vts_def;
	u32 pixel_rate;
	enum ov5640_frame_rate default_fps;
	const struct ov5640_mode_reg_table *reg_tables;
	u32 num_reg_tables;
};

struct ov5640_state {
	struct v4l2_mbus_framefmt mbus_fmt; /* 当前 active media-bus 格式缓存 */
	struct v4l2_captureparm streamcap; /* 当前帧率/采集参数缓存 */
	enum ov5640_frame_rate frame_rate; /* 当前离散帧率枚举 */
	enum ov5640_frame_size frame_size; /* 当前传感器输出尺寸 */
	bool powered; /* 时钟/GPIO 已进入可访问硬件状态 */
	bool streaming; /* 传感器像素流是否正在输出 */
	unsigned int power_users; /* host open/close 持有的 PM 引用数 */
	int prev_sysclk; /* 预览模式 sysclk，用于曝光换算 */
	int prev_hts; /* 预览模式 HTS，用于曝光换算 */
	int ae_target; /* 自动曝光目标亮度 */
	int ae_high; /* 自动曝光稳定上限 */
	int ae_low; /* 自动曝光稳定下限 */
	int night_mode; /* 夜景模式缓存状态 */

	/* Legacy placeholders kept until matching image-processing controls exist. */
	int brightness; /* 预留亮度控制缓存 */
	int hue; /* 预留色调控制缓存 */
	int contrast; /* 预留对比度控制缓存 */
	int saturation; /* 预留饱和度控制缓存 */
	int red; /* 预留红色增益缓存 */
	int green; /* 预留绿色增益缓存 */
	int blue; /* 预留蓝色增益缓存 */
	int ae_mode; /* 预留自动曝光模式缓存 */
};

struct ov5640 {
	struct v4l2_subdev subdev; /* V4L2 subdev 内嵌对象 */
	struct device *dev; /* devm、日志和 runtime PM 所属设备 */
	struct i2c_client *i2c_client; /* 绑定到传感器的 I2C client */
	struct regmap *regmap; /* 16-bit 地址、8-bit 数据寄存器映射 */
	struct gpio_desc *pwdn_gpio; /* PWDN 管脚，控制传感器 power-down */
	struct gpio_desc *reset_gpio; /* RESET 管脚，控制传感器复位 */
	struct mutex lock; /* 保护状态缓存和寄存器编程 */
	struct ov5640_state state; /* V4L2、PM、曝光和遗留控制缓存状态 */
	struct v4l2_ctrl_handler ctrls; /* V4L2 控制集合 */
	struct v4l2_ctrl *hflip; /* 水平翻转控制 */
	struct v4l2_ctrl *vflip; /* 垂直翻转控制 */
	struct v4l2_ctrl *power_line_frequency; /* 工频抗闪控制 */
	struct regulator *io_regulator; /* DOVDD，可选 IO 电源 */
	struct regulator *core_regulator; /* DVDD，可选内核电源 */
	struct regulator *analog_regulator; /* AVDD，可选模拟电源 */
	u32 mclk; /* 外部输入时钟频率 */
	u8 mclk_source; /* legacy DT 时钟源选择 */
	struct clk *sensor_clk; /* CSI MCLK 时钟句柄 */
	int csi; /* legacy CSI 控制器编号 */
	void (*io_init)(struct ov5640 *sensor); /* 板级复位/上电时序回调 */
};

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

static const struct ov5640_mode_reg_table ov5640_vga_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_VGA_640_480,
	ARRAY_SIZE(ov5640_setting_15fps_VGA_640_480)},
	{ov5640_30_fps, ov5640_setting_30fps_VGA_640_480,
	ARRAY_SIZE(ov5640_setting_30fps_VGA_640_480)},
};

static const struct ov5640_mode_reg_table ov5640_qvga_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_QVGA_320_240,
	ARRAY_SIZE(ov5640_setting_15fps_QVGA_320_240)},
	{ov5640_30_fps, ov5640_setting_30fps_QVGA_320_240,
	ARRAY_SIZE(ov5640_setting_30fps_QVGA_320_240)},
};

static const struct ov5640_mode_reg_table ov5640_ntsc_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_NTSC_720_480,
	ARRAY_SIZE(ov5640_setting_15fps_NTSC_720_480)},
	{ov5640_30_fps, ov5640_setting_30fps_NTSC_720_480,
	ARRAY_SIZE(ov5640_setting_30fps_NTSC_720_480)},
};

static const struct ov5640_mode_reg_table ov5640_pal_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_PAL_720_576,
	ARRAY_SIZE(ov5640_setting_15fps_PAL_720_576)},
	{ov5640_30_fps, ov5640_setting_30fps_PAL_720_576,
	ARRAY_SIZE(ov5640_setting_30fps_PAL_720_576)},
};

static const struct ov5640_mode_reg_table ov5640_720p_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_720P_1280_720,
	ARRAY_SIZE(ov5640_setting_15fps_720P_1280_720)},
	{ov5640_30_fps, ov5640_setting_30fps_720P_1280_720,
	ARRAY_SIZE(ov5640_setting_30fps_720P_1280_720)},
};

static const struct ov5640_mode_reg_table ov5640_1080p_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_1080P_1920_1080,
	ARRAY_SIZE(ov5640_setting_15fps_1080P_1920_1080)},
};

static const struct ov5640_mode_reg_table ov5640_qsxga_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_QSXGA_2592_1944,
	ARRAY_SIZE(ov5640_setting_15fps_QSXGA_2592_1944)},
};

static const struct ov5640_mode_reg_table ov5640_qcif_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_QCIF_176_144,
	ARRAY_SIZE(ov5640_setting_15fps_QCIF_176_144)},
	{ov5640_30_fps, ov5640_setting_30fps_QCIF_176_144,
	ARRAY_SIZE(ov5640_setting_30fps_QCIF_176_144)},
};

static const struct ov5640_mode_reg_table ov5640_xga_reg_tables[] = {
	{ov5640_15_fps, ov5640_setting_15fps_XGA_1024_768,
	ARRAY_SIZE(ov5640_setting_15fps_XGA_1024_768)},
	{ov5640_30_fps, ov5640_setting_30fps_XGA_1024_768,
	ARRAY_SIZE(ov5640_setting_30fps_XGA_1024_768)},
};

static const struct ov5640_mode_reg_table ov5640_800x480_reg_tables[] = {
	{ov5640_30_fps, ov5640_setting_30fps_800_480,
	ARRAY_SIZE(ov5640_setting_30fps_800_480)},
};

static const struct ov5640_mode_info ov5640_modes[] = {
	{
		.frame_size = ov5640_frame_size_VGA_640_480,
		.width = 640,
		.height = 480,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 640, 480},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_vga_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_vga_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_QVGA_320_240,
		.width = 320,
		.height = 240,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 320, 240},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_qvga_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_qvga_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_NTSC_720_480,
		.width = 720,
		.height = 480,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1745},
		.crop = {0, 0, 720, 480},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_ntsc_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_ntsc_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_PAL_720_576,
		.width = 720,
		.height = 576,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {96, 4, 2335, 1944},
		.crop = {0, 0, 720, 576},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_pal_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_pal_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_720P_1280_720,
		.width = 1280,
		.height = 720,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 250, 2624, 1456},
		.crop = {0, 0, 1280, 720},
		.htot = 0x0764,
		.vts_def = 0x02e4,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_720p_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_720p_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_1080P_1920_1080,
		.width = 1920,
		.height = 1080,
		.downsize = OV5640_DOWNSIZE_SCALING,
		.analog_crop = {0, 238, 2624, 1238},
		.crop = {0, 0, 1920, 1080},
		.htot = 0x0b1c,
		.vts_def = 0x07b0,
		.default_fps = ov5640_15_fps,
		.reg_tables = ov5640_1080p_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_1080p_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_QSXGA_2592_1944,
		.width = 2592,
		.height = 1944,
		.downsize = OV5640_DOWNSIZE_SCALING,
		.analog_crop = {0, 0, 2624, 1952},
		.crop = {0, 0, 2592, 1944},
		.htot = 0x0b1c,
		.vts_def = 0x07b0,
		.default_fps = ov5640_15_fps,
		.reg_tables = ov5640_qsxga_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_qsxga_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_QCIF_176_144,
		.width = 176,
		.height = 144,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 176, 144},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_qcif_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_qcif_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_XGA_1024_768,
		.width = 1024,
		.height = 768,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 1024, 768},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_xga_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_xga_reg_tables),
	}, {
		.frame_size = ov5640_frame_size_800_480,
		.width = 800,
		.height = 480,
		.downsize = OV5640_DOWNSIZE_SUBSAMPLING,
		.analog_crop = {0, 4, 2624, 1944},
		.crop = {0, 0, 800, 480},
		.htot = 0x0768,
		.vts_def = 0x03d8,
		.default_fps = ov5640_30_fps,
		.reg_tables = ov5640_800x480_reg_tables,
		.num_reg_tables = ARRAY_SIZE(ov5640_800x480_reg_tables),
	},
};

static const struct regmap_config ov5640_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.cache_type = REGCACHE_NONE,
};

static int ov5640_write_reg(struct ov5640 *sensor, u16 reg, u8 val)
{
	int ret;

	ret = regmap_write(sensor->regmap, reg, val);
	if (ret < 0)
		dev_err(&sensor->i2c_client->dev,
			"write reg 0x%04x=0x%02x failed: %d\n",
			reg, val, ret);

	return ret;
}

static int ov5640_read_reg(struct ov5640 *sensor, u16 reg, u8 *val)
{
	unsigned int regval;
	int ret;

	if (!val)
		return -EINVAL;

	ret = regmap_read(sensor->regmap, reg, &regval);
	if (ret < 0) {
		dev_err(&sensor->i2c_client->dev,
			"read reg 0x%04x failed: %d\n", reg, ret);
		return ret;
	}

	*val = regval & 0xff;
	return 0;
}

static int ov5640_read_reg16(struct ov5640 *sensor, u16 reg, u16 *val)
{
	u8 high, low;
	int ret;

	if (!val)
		return -EINVAL;

	ret = ov5640_read_reg(sensor, reg, &high);
	if (ret < 0)
		return ret;

	ret = ov5640_read_reg(sensor, reg + 1, &low);
	if (ret < 0)
		return ret;

	*val = ((u16)high << 8) | low;
	return 0;
}

static int ov5640_write_reg16(struct ov5640 *sensor, u16 reg, u16 val)
{
	int ret;

	ret = ov5640_write_reg(sensor, reg, val >> 8);
	if (ret < 0)
		return ret;

	return ov5640_write_reg(sensor, reg + 1, val & 0xff);
}

/* Some OV5640 16-bit registers latch the new value when the high byte is written. */
static int ov5640_write_reg16_low_first(struct ov5640 *sensor, u16 reg, u16 val)
{
	int ret;

	ret = ov5640_write_reg(sensor, reg + 1, val & 0xff);
	if (ret < 0)
		return ret;

	return ov5640_write_reg(sensor, reg, val >> 8);
}

static int ov5640_mod_reg(struct ov5640 *sensor, u16 reg, u8 mask, u8 val)
{
	int ret;

	ret = regmap_update_bits(sensor->regmap, reg, mask, val);
	if (ret < 0)
		dev_err(&sensor->i2c_client->dev,
			"update reg 0x%04x mask 0x%02x val 0x%02x failed: %d\n",
			reg, mask, val, ret);

	return ret;
}

static const struct ov5640_datafmt ov5640_colour_fmts[] = {
	{MEDIA_BUS_FMT_RGB565_2X8_LE, V4L2_COLORSPACE_SRGB},
};

static inline struct ov5640 *sd_to_ov5640(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5640, subdev);
}

static inline struct ov5640 *i2c_to_ov5640(const struct i2c_client *client)
{
	return sd_to_ov5640(i2c_get_clientdata(client));
}

/* Find a data format by a pixel code in an array */
static const struct ov5640_datafmt *ov5640_find_datafmt(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5640_colour_fmts); i++)
		if (ov5640_colour_fmts[i].code == code)
			return ov5640_colour_fmts + i;

	return NULL;
}

static inline bool
ov5640_mode_info_valid(const struct ov5640_mode_info *mode_info)
{
	return mode_info && mode_info->width && mode_info->height &&
	       mode_info->reg_tables && mode_info->num_reg_tables;
}

static const struct ov5640_mode_info *
ov5640_get_mode_info(enum ov5640_frame_size frame_size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5640_modes); i++) {
		if (ov5640_modes[i].frame_size == frame_size)
			return &ov5640_modes[i];
	}

	return NULL;
}

static inline bool ov5640_mode_valid(enum ov5640_frame_size frame_size)
{
	return ov5640_mode_info_valid(ov5640_get_mode_info(frame_size));
}

static const struct ov5640_mode_reg_table *
ov5640_get_mode_reg_table(const struct ov5640_mode_info *mode_info,
			  enum ov5640_frame_rate frame_rate)
{
	int i;

	if (!ov5640_mode_info_valid(mode_info))
		return NULL;

	for (i = 0; i < mode_info->num_reg_tables; i++) {
		const struct ov5640_mode_reg_table *reg_table =
			&mode_info->reg_tables[i];

		if (reg_table->frame_rate == frame_rate &&
		    reg_table->regs && reg_table->num_regs)
			return reg_table;
	}

	return NULL;
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
		u32 width_delta;
		u32 height_delta;
		u32 distance;

		if (!ov5640_get_mode_reg_table(mode_info, frame_rate))
			continue;

		width_delta = mode_info->width > width ?
			      mode_info->width - width : width - mode_info->width;
		height_delta = mode_info->height > height ?
			       mode_info->height - height : height - mode_info->height;
		distance = width_delta + height_delta;

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

		if (!ov5640_get_mode_reg_table(mode_info, frame_rate))
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
		u32 width_delta;
		u32 height_delta;
		u32 distance;

		if (!ov5640_mode_info_valid(mode_info))
			continue;

		if (mode_info->width == width && mode_info->height == height)
			return mode_info;

		if (!nearest)
			continue;

		width_delta = mode_info->width > width ?
			      mode_info->width - width : width - mode_info->width;
		height_delta = mode_info->height > height ?
			       mode_info->height - height : height - mode_info->height;
		distance = width_delta + height_delta;

		if (!best || distance < best_distance) {
			best = mode_info;
			best_distance = distance;
		}
	}

	return best;
}

static inline bool ov5640_mode_supports_fps(enum ov5640_frame_size frame_size,
					    enum ov5640_frame_rate frame_rate)
{
	return ov5640_get_mode_reg_table(ov5640_get_mode_info(frame_size),
					 frame_rate) != NULL;
}

static int ov5640_enum_frame_rate_for_mode(enum ov5640_frame_size frame_size,
					   unsigned int index,
					   enum ov5640_frame_rate *frame_rate)
{
	int i;
	unsigned int count = 0;

	if (!frame_rate || !ov5640_mode_valid(frame_size))
		return -EINVAL;

	for (i = ov5640_15_fps; i <= ov5640_30_fps; i++) {
		if (!ov5640_mode_supports_fps(frame_size, i))
			continue;

		if (count == index) {
			*frame_rate = i;
			return 0;
		}

		count++;
	}

	return -EINVAL;
}

static int ov5640_apply_format(struct ov5640 *sensor,
			       const struct ov5640_datafmt *fmt)
{
	int ret;

	if (!fmt)
		return -EINVAL;

	switch (fmt->code) {
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		ret = ov5640_write_reg(sensor, OV5640_REG_FORMAT_MUX_CONTROL,
					OV5640_FORMAT_MUX_RGB);
		if (ret < 0)
			return ret;

		return ov5640_write_reg(sensor, OV5640_REG_FORMAT_CONTROL00,
					OV5640_FORMAT_CTRL_RGB565);
	default:
		return -EINVAL;
	}
}

static void ov5640_init_default_state(struct ov5640 *sensor)
{
	const struct ov5640_mode_info *mode_info =
		ov5640_get_mode_info(ov5640_frame_size_800_480);

	mutex_init(&sensor->lock);

	sensor->state.frame_rate = ov5640_30_fps;
	sensor->state.frame_size = ov5640_frame_size_800_480;
	sensor->state.powered = false;
	sensor->state.streaming = false;
	sensor->state.power_users = 0;
	sensor->state.prev_sysclk = 0;
	sensor->state.prev_hts = 0;
	sensor->state.ae_target = 52;
	sensor->state.ae_high = 0;
	sensor->state.ae_low = 0;
	sensor->state.night_mode = 0;

	sensor->state.mbus_fmt.width = mode_info->width;
	sensor->state.mbus_fmt.height = mode_info->height;
	sensor->state.mbus_fmt.code = ov5640_colour_fmts[0].code;
	sensor->state.mbus_fmt.field = V4L2_FIELD_NONE;
	sensor->state.mbus_fmt.colorspace = ov5640_colour_fmts[0].colorspace;

	sensor->state.streamcap.capability = V4L2_MODE_HIGHQUALITY |
					V4L2_CAP_TIMEPERFRAME;
	sensor->state.streamcap.capturemode = V4L2_CAP_TIMEPERFRAME;
	sensor->state.streamcap.timeperframe.denominator = DEFAULT_FPS;
	sensor->state.streamcap.timeperframe.numerator = 1;
}

static inline void ov5640_power_down(struct ov5640 *sensor, bool enable)
{
	gpiod_set_value_cansleep(sensor->pwdn_gpio, enable);
	msleep(2);
}

static inline void ov5640_reset(struct ov5640 *sensor)
{
	/* Keep the original reset/powerdown timing, but use logical GPIO states. */
	gpiod_set_value_cansleep(sensor->reset_gpio, false);
	gpiod_set_value_cansleep(sensor->pwdn_gpio, true);
	msleep(5);
	gpiod_set_value_cansleep(sensor->pwdn_gpio, false);
	msleep(5);
	gpiod_set_value_cansleep(sensor->reset_gpio, true);
	msleep(1);
	gpiod_set_value_cansleep(sensor->reset_gpio, false);
	msleep(5);
	gpiod_set_value_cansleep(sensor->pwdn_gpio, true);
}

static int ov5640_hw_set_stream(struct ov5640 *sensor, bool enable)
{
	return ov5640_write_reg(sensor, OV5640_REG_STREAM_CTRL,
				enable ? OV5640_STREAM_ON : OV5640_STREAM_OFF);
}

static int ov5640_set_stream(struct ov5640 *sensor, bool enable)
{
	int ret;

	if (sensor->state.streaming == enable)
		return 0;

	if (enable && !sensor->state.powered)
		return -EPIPE;

	ret = ov5640_hw_set_stream(sensor, enable);
	if (ret < 0)
		return ret;

	sensor->state.streaming = enable;
	return 0;
}

static int ov5640_power_on(struct ov5640 *sensor)
{
	int ret;

	if (sensor->state.powered)
		return 0;

	ret = clk_prepare_enable(sensor->sensor_clk);
	if (ret < 0)
		return ret;

	if (sensor->io_init)
		sensor->io_init(sensor);

	ov5640_power_down(sensor, false);
	sensor->state.powered = true;

	return 0;
}

static void ov5640_power_off(struct ov5640 *sensor)
{
	int ret;

	if (!sensor->state.powered)
		return;

	if (sensor->state.streaming) {
		ret = ov5640_set_stream(sensor, false);
		if (ret < 0)
			dev_warn(sensor->dev,
				 "stream off before power down failed: %d\n", ret);
	} else {
		ret = ov5640_hw_set_stream(sensor, false);
		if (ret < 0)
			dev_dbg(sensor->dev,
				"stream-off register write failed: %d\n", ret);
	}

	ov5640_power_down(sensor, true);
	clk_disable_unprepare(sensor->sensor_clk);
	sensor->state.streaming = false;
	sensor->state.powered = false;
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

static inline void ov5640_runtime_put_autosuspend(struct ov5640 *sensor)
{
	pm_runtime_mark_last_busy(sensor->dev);
	pm_runtime_put_autosuspend(sensor->dev);
}

static int ov5640_regulator_enable(struct ov5640 *sensor)
{
	struct device *dev = sensor->dev;
	int ret = 0;

	sensor->io_regulator = devm_regulator_get(dev, "DOVDD");
	if (!IS_ERR(sensor->io_regulator)) {
		regulator_set_voltage(sensor->io_regulator,
				      OV5640_VOLTAGE_DIGITAL_IO,
				      OV5640_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(sensor->io_regulator);
		if (ret) {
			dev_err(dev, "set io voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set io voltage ok\n");
		}
	} else {
		sensor->io_regulator = NULL;
		dev_warn(dev, "cannot get io voltage\n");
	}

	sensor->core_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(sensor->core_regulator)) {
		regulator_set_voltage(sensor->core_regulator,
				      OV5640_VOLTAGE_DIGITAL_CORE,
				      OV5640_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(sensor->core_regulator);
		if (ret) {
			dev_err(dev, "set core voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set core voltage ok\n");
		}
	} else {
		sensor->core_regulator = NULL;
		dev_warn(dev, "cannot get core voltage\n");
	}

	sensor->analog_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(sensor->analog_regulator)) {
		regulator_set_voltage(sensor->analog_regulator,
				      OV5640_VOLTAGE_ANALOG,
				      OV5640_VOLTAGE_ANALOG);
		ret = regulator_enable(sensor->analog_regulator);
		if (ret) {
			dev_err(dev, "set analog voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set analog voltage ok\n");
		}
	} else {
		sensor->analog_regulator = NULL;
		dev_warn(dev, "cannot get analog voltage\n");
	}

	return ret;
}

static int ov5640_set_flip(struct ov5640 *sensor)
{
	int ret;
	u8 hflip = sensor->hflip->val ? OV5640_TIMING_FLIP_MASK : 0;
	u8 vflip = sensor->vflip->val ? OV5640_TIMING_FLIP_MASK : 0;

	ret = ov5640_mod_reg(sensor, OV5640_REG_TIMING_TC_REG21,
				     OV5640_TIMING_FLIP_MASK, hflip);
	if (ret < 0)
		return ret;

	return ov5640_mod_reg(sensor, OV5640_REG_TIMING_TC_REG20,
			       OV5640_TIMING_FLIP_MASK, vflip);
}

static int ov5640_set_power_line_frequency(struct ov5640 *sensor)
{
	int ret;

	switch (sensor->power_line_frequency->val) {
	case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
		ret = ov5640_mod_reg(sensor, OV5640_REG_BANDING_FILTER_CTRL,
				     OV5640_BANDING_MANUAL_ENABLE,
				     OV5640_BANDING_MANUAL_ENABLE);
		if (ret < 0)
			return ret;

		return ov5640_mod_reg(sensor, OV5640_REG_BANDING_FILTER_MAN,
				       OV5640_BANDING_MANUAL_50HZ,
				       OV5640_BANDING_MANUAL_50HZ);
	case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
		ret = ov5640_mod_reg(sensor, OV5640_REG_BANDING_FILTER_CTRL,
				     OV5640_BANDING_MANUAL_ENABLE,
				     OV5640_BANDING_MANUAL_ENABLE);
		if (ret < 0)
			return ret;

		return ov5640_mod_reg(sensor, OV5640_REG_BANDING_FILTER_MAN,
				       OV5640_BANDING_MANUAL_50HZ, 0);
	case V4L2_CID_POWER_LINE_FREQUENCY_AUTO:
		return ov5640_mod_reg(sensor, OV5640_REG_BANDING_FILTER_CTRL,
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
	if (!sensor->state.powered)
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
	struct ov5640 *sensor = sd_to_ov5640(sd);
	int ret;
	u8 val;

	if (reg->reg & ~0xffff)
		return -EINVAL;

	reg->size = 1;

	ret = ov5640_read_reg(sensor, reg->reg, &val);
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
	struct ov5640 *sensor = sd_to_ov5640(sd);

	if (reg->reg & ~0xffff || reg->val & ~0xff)
		return -EINVAL;

	return ov5640_write_reg(sensor, reg->reg, reg->val);
}
#endif

static int ov5640_soft_reset(struct ov5640 *sensor)
{
	int ret;

	/* sysclk from pad */
	ret = ov5640_write_reg(sensor, 0x3103, 0x11);
	if (ret < 0)
		return ret;

	/* software reset */
	ret = ov5640_write_reg(sensor, 0x3008, 0x82);
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
static int ov5640_driver_capability(struct ov5640 *sensor, int strength)
{
	if (strength > 4 || strength < 1) {
		pr_err("The valid driver capability of ov5640 is 1x~4x\n");
		return -EINVAL;
	}

	return ov5640_mod_reg(sensor, OV5640_REG_PAD_OUTPUT00, 0xc0,
				((strength - 1) << 6));
}

/* calculate sysclk */
static int ov5640_get_sysclk(struct ov5640 *sensor)
{
	int xvclk = sensor->mclk / 10000;
	int sysclk;
	int temp2;
	int Multiplier, PreDiv, VCO, SysDiv, Pll_rdiv, Bit_div2x, sclk_rdiv;
	int sclk_rdiv_map[] = {1, 2, 4, 8};
	u8 regval = 0;
	int ret;

	ret = ov5640_read_reg(sensor, OV5640_REG_SYSCLK_PLL_CTRL0, &regval);
	if (ret < 0)
		return ret;
	temp2 = regval & 0x0f;
	if (temp2 == 8 || temp2 == 10) {
		Bit_div2x = temp2 / 2;
	} else {
		pr_err("ov5640: unsupported bit mode %d\n", temp2);
		return -EINVAL;
	}

	ret = ov5640_read_reg(sensor, OV5640_REG_SYSCLK_PLL_CTRL1, &regval);
	if (ret < 0)
		return ret;
	SysDiv = regval >> 4;
	if (SysDiv == 0)
		SysDiv = 16;

	ret = ov5640_read_reg(sensor, OV5640_REG_SYSCLK_PLL_MULT, &regval);
	if (ret < 0)
		return ret;
	Multiplier = regval;

	ret = ov5640_read_reg(sensor, OV5640_REG_SYSCLK_PLL_PREDIV, &regval);
	if (ret < 0)
		return ret;
	PreDiv = regval & 0x0f;
	if (PreDiv == 0)
		return -EINVAL;
	Pll_rdiv = ((regval >> 4) & 0x01) + 1;

	ret = ov5640_read_reg(sensor, OV5640_REG_SYS_ROOT_DIVIDER, &regval);
	if (ret < 0)
		return ret;
	temp2 = regval & 0x03;

	sclk_rdiv = sclk_rdiv_map[temp2];
	VCO = xvclk * Multiplier / PreDiv;
	sysclk = VCO / SysDiv / Pll_rdiv * 2 / Bit_div2x / sclk_rdiv;

	return sysclk;
}

/* read HTS from sensor registers */
static int ov5640_get_HTS(struct ov5640 *sensor)
{
	u16 HTS;
	int ret;

	ret = ov5640_read_reg16(sensor, OV5640_REG_TIMING_HTS_H, &HTS);
	if (ret < 0)
		return ret;

	return HTS;
}

/* read VTS from sensor registers */
static int ov5640_get_VTS(struct ov5640 *sensor)
{
	u16 VTS;
	int ret;

	ret = ov5640_read_reg16(sensor, OV5640_REG_TIMING_VTS_H, &VTS);
	if (ret < 0)
		return ret;

	return VTS;
}

/* write VTS to registers */
static int ov5640_set_VTS(struct ov5640 *sensor, int VTS)
{
	return ov5640_write_reg16_low_first(sensor, OV5640_REG_TIMING_VTS_H,
					  VTS & 0xffff);
}

/* read shutter, in number of line period */
static int ov5640_get_shutter(struct ov5640 *sensor)
{
	int shutter;
	u8 regval;
	int ret;

	ret = ov5640_read_reg(sensor, OV5640_REG_AEC_PK_EXPOSURE_H, &regval);
	if (ret < 0)
		return ret;
	shutter = regval & 0x0f;

	ret = ov5640_read_reg(sensor, OV5640_REG_AEC_PK_EXPOSURE_M, &regval);
	if (ret < 0)
		return ret;
	shutter = (shutter << 8) + regval;

	ret = ov5640_read_reg(sensor, OV5640_REG_AEC_PK_EXPOSURE_L, &regval);
	if (ret < 0)
		return ret;
	shutter = (shutter << 4) + (regval >> 4);

	return shutter;
}

/* write shutter, in number of line period */
static int ov5640_set_shutter(struct ov5640 *sensor, int shutter)
{
	int ret;

	shutter &= 0xffff;

	ret = ov5640_write_reg(sensor, OV5640_REG_AEC_PK_EXPOSURE_L,
				 (shutter & 0x0f) << 4);
	if (ret < 0)
		return ret;

	ret = ov5640_write_reg(sensor, OV5640_REG_AEC_PK_EXPOSURE_M,
				 (shutter >> 4) & 0xff);
	if (ret < 0)
		return ret;

	return ov5640_write_reg(sensor, OV5640_REG_AEC_PK_EXPOSURE_H,
				      (shutter >> 12) & 0x0f);
}

/* read gain, 16 = 1x */
static int ov5640_get_gain16(struct ov5640 *sensor)
{
	u16 gain16;
	int ret;

	ret = ov5640_read_reg16(sensor, OV5640_REG_AEC_PK_REAL_GAIN_H, &gain16);
	if (ret < 0)
		return ret;

	return gain16 & 0x03ff;
}

/* write gain, 16 = 1x */
static int ov5640_set_gain16(struct ov5640 *sensor, int gain16)
{
	return ov5640_write_reg16_low_first(sensor, OV5640_REG_AEC_PK_REAL_GAIN_H,
					  gain16 & 0x03ff);
}

/* get banding filter value */
static int ov5640_get_light_freq(struct ov5640 *sensor)
{
	int light_frequency;
	u8 regval;
	int ret;

	ret = ov5640_read_reg(sensor, OV5640_REG_BANDING_FILTER_CTRL, &regval);
	if (ret < 0)
		return ret;

	if (regval & 0x80) {
		/* manual */
		ret = ov5640_read_reg(sensor, OV5640_REG_BANDING_FILTER_MAN, &regval);
		if (ret < 0)
			return ret;
		light_frequency = (regval & 0x04) ? 50 : 60;
	} else {
		/* auto */
		ret = ov5640_read_reg(sensor, OV5640_REG_BANDING_FILTER_AUTO, &regval);
		if (ret < 0)
			return ret;
		light_frequency = (regval & 0x01) ? 50 : 60;
	}

	return light_frequency;
}

static int ov5640_set_bandingfilter(struct ov5640 *sensor)
{
	int prev_VTS;
	int band_step60, max_band60, band_step50, max_band50;
	int ret;

	/* read preview PCLK */
	sensor->state.prev_sysclk = ov5640_get_sysclk(sensor);
	if (sensor->state.prev_sysclk < 0)
		return sensor->state.prev_sysclk;

	/* read preview HTS */
	sensor->state.prev_hts = ov5640_get_HTS(sensor);
	if (sensor->state.prev_hts < 0)
		return sensor->state.prev_hts;

	/* read preview VTS */
	prev_VTS = ov5640_get_VTS(sensor);
	if (prev_VTS < 0)
		return prev_VTS;

	if (sensor->state.prev_hts == 0 || prev_VTS <= 4)
		return -EINVAL;

	/* calculate banding filter */
	/* 60Hz */
	band_step60 = sensor->state.prev_sysclk * 100 / sensor->state.prev_hts * 100 / 120;
	if (band_step60 <= 0)
		return -EINVAL;

	ret = ov5640_write_reg16(sensor, OV5640_REG_BANDING_60_STEP_H, band_step60);
	if (ret < 0)
		return ret;

	max_band60 = (int)((prev_VTS - 4) / band_step60);
	ret = ov5640_write_reg(sensor, OV5640_REG_BANDING_60_MAX, max_band60);
	if (ret < 0)
		return ret;

	/* 50Hz */
	band_step50 = sensor->state.prev_sysclk * 100 / sensor->state.prev_hts;
	if (band_step50 <= 0)
		return -EINVAL;

	ret = ov5640_write_reg16(sensor, OV5640_REG_BANDING_50_STEP_H, band_step50);
	if (ret < 0)
		return ret;

	max_band50 = (int)((prev_VTS - 4) / band_step50);
	return ov5640_write_reg(sensor, OV5640_REG_BANDING_50_MAX, max_band50);
}

/* stable in high */
static int ov5640_set_AE_target(struct ov5640 *sensor, int target)
{
	int fast_high, fast_low;
	int ret;

	sensor->state.ae_low = target * 23 / 25; /* 0.92 */
	sensor->state.ae_high = target * 27 / 25; /* 1.08 */
	fast_high = sensor->state.ae_high << 1;

	if (fast_high > 255)
		fast_high = 255;
	fast_low = sensor->state.ae_low >> 1;

	ret = ov5640_write_reg(sensor, OV5640_REG_AEC_STABLE_HIGH, sensor->state.ae_high);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(sensor, OV5640_REG_AEC_STABLE_LOW, sensor->state.ae_low);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(sensor, OV5640_REG_AEC_CTRL1B, sensor->state.ae_high);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(sensor, OV5640_REG_AEC_CTRL1E, sensor->state.ae_low);
	if (ret < 0)
		return ret;
	ret = ov5640_write_reg(sensor, OV5640_REG_AEC_FAST_HIGH, fast_high);
	if (ret < 0)
		return ret;
	return ov5640_write_reg(sensor, OV5640_REG_AEC_FAST_LOW, fast_low);
}

/* enable = 0 to turn off night mode
   enable = 1 to turn on night mode */
static int ov5640_set_night_mode(struct ov5640 *sensor, int enable)
{
	return ov5640_mod_reg(sensor, OV5640_REG_AEC_CTRL00, 0x04,
				 enable ? 0x04 : 0x00);
}

/* enable = 0 to turn off AEC/AGC
   enable = 1 to turn on AEC/AGC */
static int ov5640_turn_on_AE_AG(struct ov5640 *sensor, int enable)
{
	return ov5640_mod_reg(sensor, OV5640_REG_AEC_PK_MANUAL, 0x03,
				 enable ? 0x00 : 0x03);
}

/* Write one OV5640 register table through I2C. */
static int ov5640_write_reg_table(struct ov5640 *sensor,
					    const struct reg_value *regs,
					    s32 num_regs)
{
	u32 delay_ms;
	u16 reg;
	u8 mask;
	u8 val;
	int i, retval = 0;

	for (i = 0; i < num_regs; ++i, ++regs) {
		delay_ms = regs->delay_ms;
		reg = regs->reg;
		val = regs->val;
		mask = regs->mask;

		if (mask)
			retval = ov5640_mod_reg(sensor, reg, mask, val);
		else
			retval = ov5640_write_reg(sensor, reg, val);
		if (retval < 0)
			return retval;

		if (delay_ms)
			msleep(delay_ms);
	}

	return retval;
}

/**
 * ov5640_init_mode - initialize the sensor into the default VGA frame size
 *
 * Soft-reset the OV5640, write the common sensor initialization table,
 * then apply the default 30 fps VGA register table.  After the register
 * programming succeeds, configure drive strength, anti-banding, AE target,
 * and night mode state, then wait for several frames before exposing the
 * initialized 640x480 state to the rest of the driver.
 *
 * Return: 0 on success, or a negative error code when programming the sensor
 * fails.
 */
static int ov5640_init_mode(struct ov5640 *sensor)
{
	const struct reg_value *regs = NULL;
	int num_regs = 0, retval = 0;

	retval = ov5640_soft_reset(sensor);
	if (retval < 0)
		goto err;

	regs = ov5640_global_init_setting;
	num_regs = ARRAY_SIZE(ov5640_global_init_setting);
	retval = ov5640_write_reg_table(sensor, regs, num_regs);
	if (retval < 0)
		goto err;

	regs = ov5640_init_setting_30fps_VGA;
	num_regs = ARRAY_SIZE(ov5640_init_setting_30fps_VGA);
	retval = ov5640_write_reg_table(sensor, regs, num_regs);
	if (retval < 0)
		goto err;

	/* change driver capability to 1x.
	 * 2x may cause image instability on some boards.
	 */
	retval = ov5640_driver_capability(sensor, 1);
	if (retval < 0)
		goto err;
	retval = ov5640_set_bandingfilter(sensor);
	if (retval < 0)
		goto err;
	retval = ov5640_set_AE_target(sensor, sensor->state.ae_target);
	if (retval < 0)
		goto err;
	retval = ov5640_set_night_mode(sensor, sensor->state.night_mode);
	if (retval < 0)
		goto err;

	/* skip 9 vysnc: start capture at 10th vsync */
	msleep(300);

	/* turn off night mode */
	sensor->state.night_mode = 0;
	sensor->state.frame_rate = ov5640_30_fps;
	sensor->state.frame_size = ov5640_frame_size_VGA_640_480;
	sensor->state.mbus_fmt.width = 640;
	sensor->state.mbus_fmt.height = 480;
err:
	return retval;
}

/* change to or back to subsampling mode set the frame size directly
 * image size below 1280 * 960 is subsampling mode */
static int ov5640_change_mode_direct(struct ov5640 *sensor,
			    enum ov5640_frame_rate frame_rate,
			    enum ov5640_frame_size frame_size)
{
	const struct ov5640_mode_info *mode_info;
	const struct ov5640_mode_reg_table *reg_table;
	const struct reg_value *regs = NULL;
	s32 num_regs = 0;
	int retval = 0;

	if (frame_rate > ov5640_30_fps || frame_rate < ov5640_15_fps ||
	    frame_size > ov5640_frame_size_MAX || frame_size < ov5640_frame_size_MIN) {
		pr_err("Wrong ov5640 frame size detected!\n");
		return -EINVAL;
	}

	mode_info = ov5640_get_mode_info(frame_size);
	reg_table = ov5640_get_mode_reg_table(mode_info, frame_rate);
	if (!reg_table)
		return -EINVAL;

	regs = reg_table->regs;
	num_regs = reg_table->num_regs;

	/* set ov5640 to subsampling mode */
	retval = ov5640_write_reg_table(sensor, regs, num_regs);
	if (retval < 0)
		goto err;

	/* turn on AE AG for subsampling mode, in case the register table did not */
	retval = ov5640_turn_on_AE_AG(sensor, 1);
	if (retval < 0)
		goto err;

	/* calculate banding filter */
	retval = ov5640_set_bandingfilter(sensor);
	if (retval < 0)
		goto err;

	/* set AE target */
	retval = ov5640_set_AE_target(sensor, sensor->state.ae_target);
	if (retval < 0)
		goto err;

	/* update night mode setting */
	retval = ov5640_set_night_mode(sensor, sensor->state.night_mode);
	if (retval < 0)
		goto err;

	/* skip 9 vysnc: start capture at 10th vsync */
	if (frame_size == ov5640_frame_size_XGA_1024_768 && frame_rate == ov5640_30_fps) {
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
static int ov5640_change_mode_exposure_calc(struct ov5640 *sensor,
			    enum ov5640_frame_rate frame_rate,
			    enum ov5640_frame_size frame_size)
{
	const struct ov5640_mode_info *mode_info;
	const struct ov5640_mode_reg_table *reg_table;
	int prev_shutter, prev_gain16, average;
	int cap_shutter, cap_gain16;
	int cap_sysclk, cap_HTS, cap_VTS;
	int light_freq, cap_bandfilt, cap_maxband;
	long cap_gain16_shutter;
	u8 temp;
	const struct reg_value *regs = NULL;
	s32 num_regs = 0;
	int retval = 0;

	/* check if the input frame size and frame rate is valid */
	if (frame_rate > ov5640_30_fps || frame_rate < ov5640_15_fps ||
	    frame_size > ov5640_frame_size_MAX || frame_size < ov5640_frame_size_MIN)
		return -EINVAL;

	mode_info = ov5640_get_mode_info(frame_size);
	reg_table = ov5640_get_mode_reg_table(mode_info, frame_rate);
	if (!reg_table)
		return -EINVAL;

	regs = reg_table->regs;
	num_regs = reg_table->num_regs;

	/* read preview shutter */
	prev_shutter = ov5640_get_shutter(sensor);
	if (prev_shutter < 0)
		return prev_shutter;

	/* read preview gain */
	prev_gain16 = ov5640_get_gain16(sensor);
	if (prev_gain16 < 0)
		return prev_gain16;

	/* get average */
	retval = ov5640_read_reg(sensor, OV5640_REG_AVG_READOUT, &temp);
	if (retval < 0)
		return retval;
	average = temp;

	/* turn off night mode for capture */
	retval = ov5640_set_night_mode(sensor, 0);
	if (retval < 0)
		return retval;

	/* turn off overlay */
	retval = ov5640_write_reg(sensor, OV5640_REG_FREX_CTRL, 0x06);
	if (retval < 0)
		return retval;

	/* Write capture register table */
	retval = ov5640_write_reg_table(sensor, regs, num_regs);
	if (retval < 0)
		goto err;

	/* turn off AE AG when capture image. */
	retval = ov5640_turn_on_AE_AG(sensor, 0);
	if (retval < 0)
		goto err;

	/* read capture VTS */
	cap_VTS = ov5640_get_VTS(sensor);
	if (cap_VTS < 0)
		return cap_VTS;
	cap_HTS = ov5640_get_HTS(sensor);
	if (cap_HTS < 0)
		return cap_HTS;
	cap_sysclk = ov5640_get_sysclk(sensor);
	if (cap_sysclk < 0)
		return cap_sysclk;

	if (sensor->state.prev_sysclk <= 0 || sensor->state.prev_hts <= 0 || cap_HTS <= 0 || cap_VTS <= 4)
		return -EINVAL;

	/* calculate capture banding filter */
	light_freq = ov5640_get_light_freq(sensor);
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
	if (average > sensor->state.ae_low && average < sensor->state.ae_high) {
		/* in stable range */
		cap_gain16_shutter =
			prev_gain16 * prev_shutter * cap_sysclk/sensor->state.prev_sysclk *
			sensor->state.prev_hts/cap_HTS * sensor->state.ae_target / average;
	} else {
		cap_gain16_shutter =
			prev_gain16 * prev_shutter * cap_sysclk/sensor->state.prev_sysclk *
			sensor->state.prev_hts/cap_HTS;
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
	retval = ov5640_set_gain16(sensor, cap_gain16);
	if (retval < 0)
		goto err;

	/* write capture shutter */
	if (cap_shutter > (cap_VTS - 4)) {
		cap_VTS = cap_shutter + 4;
		retval = ov5640_set_VTS(sensor, cap_VTS);
		if (retval < 0)
			goto err;
	}

	retval = ov5640_set_shutter(sensor, cap_shutter);
	if (retval < 0)
		goto err;

	/* skip 2 vysnc: start capture at 3rd vsync
	 * frame rate of QSXGA and 1080P is 7.5fps: 1/7.5 * 2
	 */
	pr_warning("ov5640: the actual frame rate of %s is 7.5fps\n",
		frame_size == ov5640_frame_size_1080P_1920_1080 ? "1080P" : "QSXGA");
	msleep(267);
err:
	return retval;
}

static int ov5640_change_mode(struct ov5640 *sensor,
			    enum ov5640_frame_rate frame_rate,
			    enum ov5640_frame_size frame_size)
{
	const struct ov5640_mode_info *mode_info;
	int retval = 0;

	if (frame_rate > ov5640_30_fps || frame_rate < ov5640_15_fps ||
	    frame_size > ov5640_frame_size_MAX || frame_size < ov5640_frame_size_MIN) {
		pr_err("Wrong ov5640 frame size detected!\n");
		return -EINVAL;
	}

	mode_info = ov5640_get_mode_info(frame_size);
	if (!ov5640_get_mode_reg_table(mode_info, frame_rate))
		return -EINVAL;

	switch (mode_info->downsize) {
	case OV5640_DOWNSIZE_SCALING:
		retval = ov5640_change_mode_exposure_calc(sensor, frame_rate, frame_size);
		break;
	case OV5640_DOWNSIZE_SUBSAMPLING:
		retval = ov5640_change_mode_direct(sensor, frame_rate, frame_size);
		break;
	default:
		return -EINVAL;
	}

	if (retval == 0) {
		sensor->state.frame_rate = frame_rate;
		sensor->state.frame_size = frame_size;
		sensor->state.mbus_fmt.width = mode_info->width;
		sensor->state.mbus_fmt.height = mode_info->height;
	}

	return retval;
}

/**
 * ov5640_s_power - keep or release the host open power reference
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @on: Non-zero gets one runtime PM reference; zero releases one.
 *
 * The i.MX6 CSI host still uses .s_power from open()/close(). Keep that
 * legacy contract, but express it as a counted runtime PM user instead of a
 * direct hardware power toggle.
 *
 * Return: 0 on success, or a negative runtime PM error code.
 */
static int ov5640_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov5640 *sensor = sd_to_ov5640(sd);
	int ret;

	if (on) {
		ret = ov5640_runtime_get(sensor);
		if (ret < 0)
			return ret;

		mutex_lock(&sensor->lock);
		sensor->state.power_users++;
		mutex_unlock(&sensor->lock);
		return 0;
	}

	mutex_lock(&sensor->lock);
	if (!sensor->state.power_users) {
		mutex_unlock(&sensor->lock);
		return 0;
	}
	sensor->state.power_users--;
	mutex_unlock(&sensor->lock);

	ov5640_runtime_put_autosuspend(sensor);
	return 0;
}

/**
 * ov5640_s_stream - start or stop sensor pixel output
 * @sd: V4L2 sub-device that represents the OV5640 sensor.
 * @enable: Non-zero starts DVP output; zero stops it.
 *
 * Stream-on keeps a runtime PM reference while OV5640 is actively producing
 * frames. Stream-off writes the sensor stream control register and releases
 * that reference when a stream was actually active.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ov5640_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov5640 *sensor = sd_to_ov5640(sd);
	bool started_streaming;
	bool was_streaming;
	int ret;

	if (enable) {
		mutex_lock(&sensor->lock);
		if (sensor->state.streaming) {
			mutex_unlock(&sensor->lock);
			return 0;
		}
		mutex_unlock(&sensor->lock);

		ret = ov5640_runtime_get(sensor);
		if (ret < 0)
			return ret;

		mutex_lock(&sensor->lock);
		if (sensor->state.streaming) {
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
	was_streaming = sensor->state.streaming;
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
	struct ov5640 *sensor = sd_to_ov5640(sd);
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->state.streamcap.capability;
		cparm->timeperframe = sensor->state.streamcap.timeperframe;
		cparm->capturemode = sensor->state.streamcap.capturemode;
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
 * checks that the current sensor frame size supports it, and applies the matching
 * register table immediately when the device is powered and not streaming.
 *
 * Return: 0 on success, -EBUSY if streaming is active, or -EINVAL if the
 * buffer type, frame rate, or current frame size/fps combination is not supported.
 */
static int ov5640_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct ov5640 *sensor = sd_to_ov5640(sd);
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

		mode_info = ov5640_get_mode_info(sensor->state.frame_size);
		if (!ov5640_get_mode_reg_table(mode_info, frame_rate)) {
			ret = -EINVAL;
			goto unlock;
		}

		if (sensor->state.streaming && frame_rate != sensor->state.frame_rate) {
			ret = -EBUSY;
			goto unlock;
		}

		if (sensor->state.powered && frame_rate != sensor->state.frame_rate) {
			ret = ov5640_change_mode(sensor, frame_rate, sensor->state.frame_size);
			if (ret < 0)
				goto unlock;

			fmt = ov5640_find_datafmt(sensor->state.mbus_fmt.code);
			if (!fmt)
				fmt = &ov5640_colour_fmts[0];
			ret = ov5640_apply_format(sensor, fmt);
			if (ret < 0)
				goto unlock;

			ret = ov5640_apply_controls(sensor);
			if (ret < 0)
				goto unlock;

			ret = ov5640_hw_set_stream(sensor, false);
			if (ret < 0)
				goto unlock;
		}

		sensor->state.streamcap.timeperframe = *timeperframe;
		sensor->state.streamcap.capturemode = cparm->capturemode;
		sensor->state.frame_rate = frame_rate;
		cparm->capability = sensor->state.streamcap.capability;
		cparm->timeperframe = sensor->state.streamcap.timeperframe;
		cparm->capturemode = sensor->state.streamcap.capturemode;

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
 * requested size is snapped to the nearest frame size that is valid for the current
 * frame-rate cache; this callback never writes sensor registers.
 *
 * Return: 0 on success, or -EINVAL if no valid frame size exists.
 */
static int ov5640_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct ov5640 *sensor = sd_to_ov5640(sd);
	const struct ov5640_datafmt *fmt = ov5640_find_datafmt(mf->code);
	const struct ov5640_mode_info *mode_info;

	if (!fmt)
		fmt = &ov5640_colour_fmts[0];

	mode_info = ov5640_find_nearest_mode(sensor->state.frame_rate,
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
 * the selected frame size and the verified RGB565 output registers when the sensor is
 * powered.  If called while powered off, it only updates the cached request;
 * init_device(sensor) will apply that cache on the next power-on.
 *
 * Return: 0 on success, or a negative error code.
 */
static int ov5640_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct ov5640 *sensor = sd_to_ov5640(sd);
	const struct ov5640_datafmt *fmt;
	const struct ov5640_mode_info *mode_info;
	int retval;

	retval = ov5640_try_fmt(sd, mf);
	if (retval < 0)
		return retval;

	fmt = ov5640_find_datafmt(mf->code);
	mode_info = ov5640_find_mode_by_size(sensor->state.frame_rate,
						mf->width, mf->height);
	if (!fmt || !mode_info)
		return -EINVAL;

	mutex_lock(&sensor->lock);
	if (sensor->state.streaming) {
		retval = -EBUSY;
		goto out;
	}

	if (sensor->state.powered) {
		retval = ov5640_change_mode(sensor, sensor->state.frame_rate, mode_info->frame_size);
		if (retval < 0)
			goto out;

		retval = ov5640_apply_format(sensor, fmt);
		if (retval < 0)
			goto out;

		retval = ov5640_apply_controls(sensor);
		if (retval < 0)
			goto out;

		retval = ov5640_hw_set_stream(sensor, false);
		if (retval < 0)
			goto out;
	}

	sensor->state.frame_size = mode_info->frame_size;
	sensor->state.mbus_fmt = *mf;

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
 * The callback returns the active media-bus format cached in the sensor
 * state.
 *
 * Return: 0.
 */
static int ov5640_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct ov5640 *sensor = sd_to_ov5640(sd);

	*mf = sensor->state.mbus_fmt;

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

	ret = ov5640_enum_frame_rate_for_mode(mode_info->frame_size,
					       fie->index, &frame_rate);
	if (ret < 0)
		return ret;

	fie->interval.numerator = 1;
	fie->interval.denominator = ov5640_framerates[frame_rate];
	return 0;
}

static int ov5640_set_clk_rate(struct ov5640 *sensor)
{
	u32 tgt_xclk;	/* target xclk */
	int ret;

	/* mclk */
	tgt_xclk = sensor->mclk;
	tgt_xclk = min(tgt_xclk, (u32)OV5640_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)OV5640_XCLK_MIN);
	sensor->mclk = tgt_xclk;

	pr_debug("   Setting mclk to %d MHz\n", tgt_xclk / 1000000);
	ret = clk_set_rate(sensor->sensor_clk, sensor->mclk);
	if (ret < 0)
		pr_debug("set rate filed, rate=%d\n", sensor->mclk);
	return ret;
}

/*!
 * dev_init - V4L2 sensor init
 * 
 * init device according to the mclk and fps(sensor->state.streamcap.timeperframe) in sensor
 */
static int init_device(struct ov5640 *sensor)
{
	u32 tgt_xclk;	/* target xclk */
	u32 tgt_fps;	/* target frames per secound */
	enum ov5640_frame_rate frame_rate;
	enum ov5640_frame_size target_frame_size = sensor->state.frame_size;
	const struct ov5640_mode_info *mode_info;
	const struct ov5640_datafmt *fmt;
	int ret;


	/* mclk */
	tgt_xclk = sensor->mclk;

	/* Default camera frame rate is set in probe */
	tgt_fps = sensor->state.streamcap.timeperframe.denominator /
		  sensor->state.streamcap.timeperframe.numerator;

	if (tgt_fps == 15)
		frame_rate = ov5640_15_fps;
	else if (tgt_fps == 30)
		frame_rate = ov5640_30_fps;
	else
		return -EINVAL; /* Only support 15fps or 30fps now. */

	mode_info = ov5640_get_mode_info(target_frame_size);
	if (!ov5640_get_mode_reg_table(mode_info, frame_rate)) {
		mode_info = ov5640_find_nearest_mode(frame_rate,
						sensor->state.mbus_fmt.width,
						sensor->state.mbus_fmt.height);
		if (!mode_info)
			return -EINVAL;
		target_frame_size = mode_info->frame_size;
	}

	ret = ov5640_init_mode(sensor);
	if (ret < 0)
		return ret;

	if (sensor->state.frame_rate != frame_rate ||
	    sensor->state.frame_size != target_frame_size) {
		ret = ov5640_change_mode(sensor, frame_rate, target_frame_size);
		if (ret < 0)
			return ret;
	}

	fmt = ov5640_find_datafmt(sensor->state.mbus_fmt.code);
	if (!fmt)
		fmt = &ov5640_colour_fmts[0];

	ret = ov5640_apply_format(sensor, fmt);
	if (ret < 0)
		return ret;

	ret = ov5640_apply_controls(sensor);
	if (ret < 0)
		return ret;

	ret = ov5640_hw_set_stream(sensor, false);
	if (ret < 0)
		return ret;

	sensor->state.streaming = false;
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
	struct device *dev = &client->dev;
	struct ov5640 *sensor;
	int retval;
	u32 mclk_source;
	u8 chip_id_high, chip_id_low;

	/* ov5640 pinctrl */
	/* driver core will bind pinctrl before probe in really_probe()(linux/drivers/base/dd.c), so actually it's not necessary here. */
	// pinctrl = devm_pinctrl_get_select_default(dev);
	// if (IS_ERR(pinctrl)) {
	// 	dev_err(dev, "setup pinctrl failed\n");
	// 	return PTR_ERR(pinctrl);
	// }

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->dev = dev;
	sensor->i2c_client = client;

	sensor->pwdn_gpio = devm_gpiod_get(dev, "pwn", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->pwdn_gpio)) {
		retval = PTR_ERR(sensor->pwdn_gpio);
		dev_err(dev, "failed to get sensor pwdn GPIO: %d\n", retval);
		return retval;
	}

	sensor->reset_gpio = devm_gpiod_get(dev, "rst", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset_gpio)) {
		retval = PTR_ERR(sensor->reset_gpio);
		dev_err(dev, "failed to get sensor reset GPIO: %d\n", retval);
		return retval;
	}
	sensor->regmap = devm_regmap_init_i2c(client, &ov5640_regmap_config);
	if (IS_ERR(sensor->regmap)) {
		retval = PTR_ERR(sensor->regmap);
		dev_err(dev, "regmap init failed: %d\n", retval);
		return retval;
	}

	sensor->sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(sensor->sensor_clk)) {
		dev_err(dev, "get mclk failed\n");
		return PTR_ERR(sensor->sensor_clk);
	}

	retval = of_property_read_u32(dev->of_node, "mclk",
					&sensor->mclk);
	if (retval) {
		dev_err(dev, "mclk frequency is invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",
					&mclk_source);
	if (retval) {
		dev_err(dev, "mclk_source invalid\n");
		return retval;
	}
	if (mclk_source > 0xff) {
		dev_err(dev, "mclk_source out of range: %u\n", mclk_source);
		return -EINVAL;
	}
	sensor->mclk_source = mclk_source;

	retval = of_property_read_u32(dev->of_node, "csi_id",
					&(sensor->csi));
	if (retval) {
		dev_err(dev, "csi_id invalid\n");
		return retval;
	}

	/* Set mclk rate before clk on */
	retval = ov5640_set_clk_rate(sensor);
	if (retval < 0)
		return retval;

	sensor->io_init = ov5640_reset;
	ov5640_init_default_state(sensor);

	/* 正点原子 ov5640 模块只需3.3v供电，应该模块内部给了不同电压，故无需此步骤 */
	// ov5640_regulator_enable(&client->dev);

	retval = ov5640_power_on(sensor);
	if (retval < 0)
		return retval;

	/* read register to make sure the device is OV5640 */
	retval = ov5640_read_reg(sensor, OV5640_CHIP_ID_HIGH_BYTE, &chip_id_high);
	if (retval < 0 || chip_id_high != 0x56) {
		pr_warning("camera ov5640 is not found\n");
		retval = -ENODEV;
		goto power_off;
	}
	retval = ov5640_read_reg(sensor, OV5640_CHIP_ID_LOW_BYTE, &chip_id_low);
	if (retval < 0 || chip_id_low != 0x40) {
		pr_warning("camera ov5640 is not found\n");
		retval = -ENODEV;
		goto power_off;
	}

	retval = init_device(sensor);
	if (retval < 0) {
		pr_warning("camera ov5640 init failed\n");
		goto power_off;
	}

	/*
	 * will do:
	 * v4l2_set_subdevdata(sd, client);
	 * i2c_set_clientdata(client, sd);
	 */
	v4l2_i2c_subdev_init(&sensor->subdev, client, &ov5640_subdev_ops);

	retval = ov5640_init_controls(sensor);
	if (retval < 0) {
		dev_err(&client->dev, "control init failed: %d\n", retval);
		goto power_off;
	}

	retval = v4l2_async_register_subdev(&sensor->subdev);
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
	v4l2_ctrl_handler_free(&sensor->ctrls);
power_off:
	ov5640_power_off(sensor);
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
	struct ov5640 *sensor = i2c_to_ov5640(client);

	pm_runtime_disable(&client->dev);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&sensor->ctrls);

	mutex_lock(&sensor->lock);
	sensor->state.power_users = 0;
	ov5640_power_off(sensor);
	if (!sensor->state.powered)
		ov5640_power_down(sensor, true);
	mutex_unlock(&sensor->lock);
	pm_runtime_set_suspended(&client->dev);

	if (sensor->analog_regulator)
		regulator_disable(sensor->analog_regulator);

	if (sensor->core_regulator)
		regulator_disable(sensor->core_regulator);

	if (sensor->io_regulator)
		regulator_disable(sensor->io_regulator);

	return 0;
}

static int ov5640_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov5640 *sensor = i2c_to_ov5640(client);
	int ret;

	mutex_lock(&sensor->lock);

	ret = ov5640_power_on(sensor);
	if (ret < 0)
		goto out;

	ret = init_device(sensor);
	if (ret < 0)
		ov5640_power_off(sensor);

 out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ov5640_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov5640 *sensor = i2c_to_ov5640(client);

	mutex_lock(&sensor->lock);
	if (sensor->state.streaming) {
		mutex_unlock(&sensor->lock);
		return -EBUSY;
	}

	ov5640_power_off(sensor);
	mutex_unlock(&sensor->lock);

	return 0;
}

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

module_i2c_driver(ov5640_i2c_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("OV5640 Camera Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
