/*
 * Copyright (C) 2014-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file mx6s_csi.c
 *
 * @brief mx6sx CMOS Sensor interface functions
 *
 * @ingroup CSI
 */
#include <asm/dma.h>
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gcd.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/media-bus-format.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-of.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#define MX6S_CAM_DRV_NAME "mx6s-csi"
#define MX6S_CAM_VERSION "0.0.1"
#define MX6S_CAM_DRIVER_DESCRIPTION "i.MX6S_CSI"

#define MAX_VIDEO_MEM 64

/* reset values */
#define CSICR1_RESET_VAL	0x40000800
#define CSICR2_RESET_VAL	0x0
#define CSICR3_RESET_VAL	0x0

/* csi control reg 1 */
#define BIT_SWAP16_EN		(0x1 << 31)
#define BIT_EXT_VSYNC		(0x1 << 30)
#define BIT_EOF_INT_EN		(0x1 << 29)
#define BIT_PRP_IF_EN		(0x1 << 28)
#define BIT_CCIR_MODE		(0x1 << 27)
#define BIT_COF_INT_EN		(0x1 << 26)
#define BIT_SF_OR_INTEN		(0x1 << 25)
#define BIT_RF_OR_INTEN		(0x1 << 24)
#define BIT_SFF_DMA_DONE_INTEN  (0x1 << 22)
#define BIT_STATFF_INTEN	(0x1 << 21)
#define BIT_FB2_DMA_DONE_INTEN  (0x1 << 20)
#define BIT_FB1_DMA_DONE_INTEN  (0x1 << 19)
#define BIT_RXFF_INTEN		(0x1 << 18)
#define BIT_SOF_POL		(0x1 << 17)
#define BIT_SOF_INTEN		(0x1 << 16)
#define BIT_MCLKDIV		(0xF << 12)
#define BIT_HSYNC_POL		(0x1 << 11)
#define BIT_CCIR_EN		(0x1 << 10)
#define BIT_MCLKEN		(0x1 << 9)
#define BIT_FCC			(0x1 << 8)
#define BIT_PACK_DIR		(0x1 << 7)
#define BIT_CLR_STATFIFO	(0x1 << 6)
#define BIT_CLR_RXFIFO		(0x1 << 5)
#define BIT_GCLK_MODE		(0x1 << 4)
#define BIT_INV_DATA		(0x1 << 3)
#define BIT_INV_PCLK		(0x1 << 2)
#define BIT_REDGE		(0x1 << 1)
#define BIT_PIXEL_BIT		(0x1 << 0)

#define SHIFT_MCLKDIV		12

/* control reg 3 */
#define BIT_FRMCNT		(0xFFFF << 16)
#define BIT_FRMCNT_RST		(0x1 << 15)
#define BIT_DMA_REFLASH_RFF	(0x1 << 14)
#define BIT_DMA_REFLASH_SFF	(0x1 << 13)
#define BIT_DMA_REQ_EN_RFF	(0x1 << 12)
#define BIT_DMA_REQ_EN_SFF	(0x1 << 11)
#define BIT_STATFF_LEVEL	(0x7 << 8)
#define BIT_HRESP_ERR_EN	(0x1 << 7)
#define BIT_RXFF_LEVEL		(0x7 << 4)
#define BIT_TWO_8BIT_SENSOR	(0x1 << 3)
#define BIT_ZERO_PACK_EN	(0x1 << 2)
#define BIT_ECC_INT_EN		(0x1 << 1)
#define BIT_ECC_AUTO_EN		(0x1 << 0)

#define SHIFT_FRMCNT		16
#define SHIFT_RXFIFO_LEVEL	4

/* csi status reg */
#define BIT_ADDR_CH_ERR_INT (0x1 << 28)
#define BIT_FIELD0_INT      (0x1 << 27)
#define BIT_FIELD1_INT      (0x1 << 26)
#define BIT_SFF_OR_INT		(0x1 << 25)
#define BIT_RFF_OR_INT		(0x1 << 24)
#define BIT_DMA_TSF_DONE_SFF	(0x1 << 22)
#define BIT_STATFF_INT		(0x1 << 21)
#define BIT_DMA_TSF_DONE_FB2	(0x1 << 20)
#define BIT_DMA_TSF_DONE_FB1	(0x1 << 19)
#define BIT_RXFF_INT		(0x1 << 18)
#define BIT_EOF_INT		(0x1 << 17)
#define BIT_SOF_INT		(0x1 << 16)
#define BIT_F2_INT		(0x1 << 15)
#define BIT_F1_INT		(0x1 << 14)
#define BIT_COF_INT		(0x1 << 13)
#define BIT_HRESP_ERR_INT	(0x1 << 7)
#define BIT_ECC_INT		(0x1 << 1)
#define BIT_DRDY		(0x1 << 0)

/* csi control reg 18 */
#define BIT_CSI_ENABLE			(0x1 << 31)
#define BIT_MIPI_DATA_FORMAT_RAW8		(0x2a << 25)
#define BIT_MIPI_DATA_FORMAT_RAW10		(0x2b << 25)
#define BIT_MIPI_DATA_FORMAT_YUV422_8B	(0x1e << 25)
#define BIT_MIPI_DATA_FORMAT_MASK	(0x3F << 25)
#define BIT_MIPI_DATA_FORMAT_OFFSET	25
#define BIT_DATA_FROM_MIPI		(0x1 << 22)
#define BIT_MIPI_YU_SWAP		(0x1 << 21)
#define BIT_MIPI_DOUBLE_CMPNT	(0x1 << 20)
#define BIT_BASEADDR_CHG_ERR_EN	(0x1 << 9)
#define BIT_BASEADDR_SWITCH_SEL	(0x1 << 5)
#define BIT_BASEADDR_SWITCH_EN	(0x1 << 4)
#define BIT_PARALLEL24_EN		(0x1 << 3)
#define BIT_DEINTERLACE_EN		(0x1 << 2)
#define BIT_TVDECODER_IN_EN		(0x1 << 1)
#define BIT_NTSC_EN				(0x1 << 0)

#define CSI_MCLK_VF		1
#define CSI_MCLK_ENC		2
#define CSI_MCLK_RAW		4
#define CSI_MCLK_I2C		8

#define CSI_CSICR1		0x0
#define CSI_CSICR2		0x4
#define CSI_CSICR3		0x8
#define CSI_STATFIFO		0xC
#define CSI_CSIRXFIFO		0x10
#define CSI_CSIRXCNT		0x14
#define CSI_CSISR		0x18

#define CSI_CSIDBG		0x1C
#define CSI_CSIDMASA_STATFIFO	0x20
#define CSI_CSIDMATS_STATFIFO	0x24
#define CSI_CSIDMASA_FB1	0x28
#define CSI_CSIDMASA_FB2	0x2C
#define CSI_CSIFBUF_PARA	0x30
#define CSI_CSIIMAG_PARA	0x34

#define CSI_CSICR18		0x48
#define CSI_CSICR19		0x4c

#define NUM_FORMATS ARRAY_SIZE(formats)
#define MX6SX_MAX_SENSORS    1

/**
 * struct csi_signal_cfg_t - CSI 输入信号极性和位宽描述
 * @data_width: sensor 数据线宽度，对应并口输入的有效数据位数。
 * @clk_mode: sensor 时钟模式，通常描述 gated/non-gated pixel clock。
 * @ext_vsync: 是否使用外部 VSYNC 信号。
 * @Vsync_pol: VSYNC 有效极性。
 * @Hsync_pol: HSYNC 有效极性。
 * @pixclk_pol: pixel clock 采样边沿/极性。
 * @data_pol: 数据线极性，置位时表示输入数据取反。
 * @sens_clksrc: sensor 时钟源选择。
 *
 * 这是 Freescale 原始驱动保留下来的信号配置位图描述。本文件当前主要
 * 直接写 CSI 控制寄存器，未实例化该结构，但字段命名可帮助对照手册。
 */
struct csi_signal_cfg_t {
	unsigned data_width:3;
	unsigned clk_mode:2;
	unsigned ext_vsync:1;
	unsigned Vsync_pol:1;
	unsigned Hsync_pol:1;
	unsigned pixclk_pol:1;
	unsigned data_pol:1;
	unsigned sens_clksrc:1;
};

/**
 * struct csi_config_t - CSI 控制寄存器位图缓存描述
 * @swap16_en: CSICR1 SWAP16_EN，16-bit halfword 交换控制。
 * @ext_vsync: CSICR1 EXT_VSYNC，外部 VSYNC 选择。
 * @eof_int_en: CSICR1 EOF_INT_EN，帧结束中断使能。
 * @prp_if_en: CSICR1 PRP_IF_EN，预处理接口使能。
 * @ccir_mode: CSICR1 CCIR_MODE，BT.656/TV decoder 输入模式。
 * @cof_int_en: CSICR1 COF_INT_EN，change-of-field 中断使能。
 * @sf_or_inten: CSICR1 STAT FIFO overflow 中断使能。
 * @rf_or_inten: CSICR1 RxFIFO overflow 中断使能。
 * @sff_dma_done_inten: CSICR1 STAT FIFO DMA done 中断使能。
 * @statff_inten: CSICR1 STAT FIFO 中断使能。
 * @fb2_dma_done_inten: CSICR1 framebuffer 2 DMA done 中断使能。
 * @fb1_dma_done_inten: CSICR1 framebuffer 1 DMA done 中断使能。
 * @rxff_inten: CSICR1 RxFIFO data ready 中断使能。
 * @sof_pol: CSICR1 SOF 极性。
 * @sof_inten: CSICR1 SOF 中断使能。
 * @mclkdiv: CSICR1 MCLK 分频值。
 * @hsync_pol: CSICR1 HSYNC 极性。
 * @ccir_en: CSICR1 CCIR 使能。
 * @mclken: CSICR1 sensor MCLK 输出使能。
 * @fcc: CSICR1 FIFO control。
 * @pack_dir: CSICR1 打包方向。
 * @gclk_mode: CSICR1 gated clock 模式。
 * @inv_data: CSICR1 输入数据反相。
 * @inv_pclk: CSICR1 pixel clock 反相。
 * @redge: CSICR1 上升沿采样。
 * @pixel_bit: CSICR1 pixel bit 选择。
 * @frmcnt: CSICR3 帧计数器。
 * @frame_reset: CSICR3 帧计数器复位。
 * @dma_reflash_rff: CSICR3 RxFIFO DMA 刷新。
 * @dma_reflash_sff: CSICR3 STAT FIFO DMA 刷新。
 * @dma_req_en_rff: CSICR3 RxFIFO DMA 请求使能。
 * @dma_req_en_sff: CSICR3 STAT FIFO DMA 请求使能。
 * @statff_level: CSICR3 STAT FIFO 触发水位。
 * @hresp_err_en: CSICR3 AHB HRESP 错误中断使能。
 * @rxff_level: CSICR3 RxFIFO 触发水位。
 * @two_8bit_sensor: CSICR3 双 8-bit sensor 输入模式。
 * @zero_pack_en: CSICR3 零填充打包使能。
 * @ecc_int_en: CSICR3 ECC 中断使能。
 * @ecc_auto_en: CSICR3 ECC 自动校验使能。
 * @rxcnt: RxFIFO 计数器。
 *
 * 这是寄存器位语义的结构化镜像。本驱动当前使用 BIT_* 宏直接读改写
 * MMIO 寄存器，因此它主要作为学习和对照手册的辅助描述。
 */
struct csi_config_t {
	/* control reg 1 */
	unsigned int swap16_en:1;
	unsigned int ext_vsync:1;
	unsigned int eof_int_en:1;
	unsigned int prp_if_en:1;
	unsigned int ccir_mode:1;
	unsigned int cof_int_en:1;
	unsigned int sf_or_inten:1;
	unsigned int rf_or_inten:1;
	unsigned int sff_dma_done_inten:1;
	unsigned int statff_inten:1;
	unsigned int fb2_dma_done_inten:1;
	unsigned int fb1_dma_done_inten:1;
	unsigned int rxff_inten:1;
	unsigned int sof_pol:1;
	unsigned int sof_inten:1;
	unsigned int mclkdiv:4;
	unsigned int hsync_pol:1;
	unsigned int ccir_en:1;
	unsigned int mclken:1;
	unsigned int fcc:1;
	unsigned int pack_dir:1;
	unsigned int gclk_mode:1;
	unsigned int inv_data:1;
	unsigned int inv_pclk:1;
	unsigned int redge:1;
	unsigned int pixel_bit:1;

	/* control reg 3 */
	unsigned int frmcnt:16;
	unsigned int frame_reset:1;
	unsigned int dma_reflash_rff:1;
	unsigned int dma_reflash_sff:1;
	unsigned int dma_req_en_rff:1;
	unsigned int dma_req_en_sff:1;
	unsigned int statff_level:3;
	unsigned int hresp_err_en:1;
	unsigned int rxff_level:3;
	unsigned int two_8bit_sensor:1;
	unsigned int zero_pack_en:1;
	unsigned int ecc_int_en:1;
	unsigned int ecc_auto_en:1;
	/* fifo counter */
	unsigned int rxcnt;
};

/**
 * struct mx6s_fmt - CSI host 支持的视频格式映射
 * @name: 给 VIDIOC_ENUM_FMT 返回的人类可读格式名。
 * @fourcc: V4L2 fourcc。保留字段，当前和 @pixelformat 相同。
 * @pixelformat: 用户态 V4L2_PIX_FMT_* 像素格式。
 * @mbus_code: sensor subdev 端 media bus code。
 * @bpp: 每像素字节数，用于计算 sizeimage/bytesperline。
 *
 * CSI host 需要把用户态看到的 V4L2 pixel format 映射到 sensor subdev
 * 能协商的 media-bus format。OV5640 的枚举结果会通过 @mbus_code
 * 反查到这里的条目。
 */
struct mx6s_fmt {
	char  name[32];
	u32   fourcc;		/* v4l2 format id */
	u32   pixelformat;
	u32   mbus_code;
	int   bpp;
};

static struct mx6s_fmt formats[] = {
	{
		.name		= "UYVY-16",
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp		= 2,
	}, {
		.name		= "YUYV-16",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp		= 2,
	}, {
		.name		= "YUV32 (X-Y-U-V)",
		.fourcc		= V4L2_PIX_FMT_YUV32,
		.pixelformat	= V4L2_PIX_FMT_YUV32,
		.mbus_code	= MEDIA_BUS_FMT_AYUV8_1X32,
		.bpp		= 4,
	}, {
		.name		= "RAWRGB8 (SBGGR8)",
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.pixelformat	= V4L2_PIX_FMT_SBGGR8,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.bpp		= 1,
	}, {
		.name		= "RGB565_LE",
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.mbus_code	= MEDIA_BUS_FMT_RGB565_2X8_LE,
		.bpp		= 2,
	}
};

/**
 * struct mx6s_buf_internal - 驱动内部 buffer 队列节点
 * @queue: Linux 内核侵入式链表节点，用于 capture/active/discard 队列。
 * @bufnum: 当前写入 CSI_CSIDMASA_FB1/FB2 的硬件 buffer 槽号。
 * @discard: true 表示该节点指向丢帧用的内部 DMA buffer，不返回用户态。
 */
struct mx6s_buf_internal {
	struct list_head	queue;
	int					bufnum;
	bool				discard;
};

/**
 * struct mx6s_buffer - 单帧视频 buffer
 * @vb: videobuf2 的公共 buffer 对象。必须放在本结构体开头，便于旧代码
 *      和 VB2 core 使用外层对象地址。
 * @internal: CSI host 私有的链表节点和硬件槽位信息。
 */
struct mx6s_buffer {
	struct vb2_buffer			vb;
	struct mx6s_buf_internal	internal;
};

/**
 * struct mx6s_csi_mux - IOMUXC GPR 中的 CSI 输入 mux 描述
 * @gpr: syscon_node_to_regmap() 得到的 GPR regmap。
 * @req_gpr: 需要更新的 GPR 寄存器偏移，来自设备树 csi-mux-mipi。
 * @req_bit: 需要置位的 bit 编号，来自设备树 csi-mux-mipi。
 */
struct mx6s_csi_mux {
	struct regmap *gpr;
	u8 req_gpr;
	u8 req_bit;
};

struct mx6s_csi_dev;
static struct mx6s_fmt *format_by_fourcc(int fourcc);

/**
 * struct mx6s_csi_dev - i.MX6S/6ULL CSI host 驱动私有数据
 * @dev: 绑定的 platform device 的 struct device。
 * @vdev: 注册到 V4L2 core 的 /dev/videoX 设备。
 * @sd: 已绑定的 sensor v4l2_subdev，本工程通常是 OV5640。
 * @v4l2_dev: V4L2 顶层设备对象，承载 video node 和 subdev。
 * @vb2_vidq: videobuf2 capture 队列，管理用户 buffer 生命周期。
 * @alloc_ctx: vb2-dma-contig 分配上下文，保存 DMA 设备信息。
 * @ctrl_handler: V4L2 控件处理器，本驱动当前未填充控件。
 * @lock: 进程上下文互斥锁，保护 open/close/ioctl 队列配置路径。
 * @slock: IRQ 和进程上下文共享的自旋锁，保护 buffer 链表和硬件槽位。
 * @clk_disp_axi: CSI/显示子系统访问 AXI 总线所需时钟。
 * @clk_disp_dcic: 显示/DCIC 相关时钟，CSI 模块依赖该时钟域。
 * @clk_csi_mclk: 输出给 sensor 或 CSI 模块自身使用的 MCLK。
 * @regbase: devm_ioremap_resource() 映射后的 CSI MMIO 基址。
 * @irq: platform_get_irq() 获取的 CSI 中断号。
 * @type: 当前 V4L2 buffer type，通常为 V4L2_BUF_TYPE_VIDEO_CAPTURE。
 * @bytesperline: 每行字节数缓存，当前主要使用 @pix.bytesperline。
 * @std: 模拟制式 ID，用于隔行/TV decoder 模式。
 * @fmt: 当前选中的 host 格式映射。
 * @pix: 当前 V4L2 capture pixel format。
 * @mbus_code: 当前 sensor media-bus code。
 * @frame_count: 已完成帧序号，写入 v4l2_buffer.sequence。
 * @capture: 用户态已 QBUF、等待交给 CSI DMA 的 buffer 队列。
 * @active_bufs: 已写入 CSI FB1/FB2、正在被硬件 DMA 使用的队列。
 * @discard: 用户 buffer 不足时可复用的内部丢帧队列。
 * @discard_buffer: dma_alloc_coherent() 分配的丢帧 buffer CPU 地址。
 * @discard_buffer_dma: @discard_buffer 对应的 DMA 地址。
 * @discard_size: 丢帧 buffer 的图像有效大小。
 * @buf_discard: 两个内部队列节点，对应 CSI 双 buffer 槽。
 * @asd: 描述异步等待的 OV5640 subdev。
 * @subdev_notifier: V4L2 async notifier，负责等待 sensor probe。
 * @async_subdevs: notifier 使用的 subdev 匹配表，以 NULL 结尾。
 * @csi_mux_mipi: true 表示 CSI 输入来自 MIPI CSI bridge。
 * @csi_mux: MIPI 输入路径的 GPR mux 配置。
 */
struct mx6s_csi_dev {
	struct device		*dev;
	struct video_device *vdev;
	struct v4l2_subdev	*sd;
	struct v4l2_device	v4l2_dev;

	struct vb2_queue			vb2_vidq;
	struct vb2_alloc_ctx		*alloc_ctx;
	struct v4l2_ctrl_handler	ctrl_handler;

	struct mutex		lock;
	spinlock_t			slock;

	/* clock */
	struct clk	*clk_disp_axi;
	struct clk	*clk_disp_dcic;
	struct clk	*clk_csi_mclk;

	void __iomem *regbase;
	int irq;

	u32	 type;
	u32 bytesperline;
	v4l2_std_id std;
	struct mx6s_fmt		*fmt;
	struct v4l2_pix_format pix;
	u32 mbus_code;

	unsigned int frame_count;

	struct list_head	capture;
	struct list_head	active_bufs;
	struct list_head	discard;

	void						*discard_buffer;
	dma_addr_t					discard_buffer_dma;
	size_t						discard_size;
	struct mx6s_buf_internal	buf_discard[2];

	struct v4l2_async_subdev	asd;	/* 描述异步等待的 OV5640 subdev。 */
	struct v4l2_async_notifier	subdev_notifier;
	struct v4l2_async_subdev	*async_subdevs[2];

	bool csi_mux_mipi;
	struct mx6s_csi_mux csi_mux;
};

static void mx6s_init_default_format(struct mx6s_csi_dev *csi_dev)
{
	struct mx6s_fmt *fmt = format_by_fourcc(V4L2_PIX_FMT_RGB565);

	csi_dev->fmt = fmt;
	csi_dev->mbus_code = fmt->mbus_code;
	csi_dev->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	csi_dev->bytesperline = fmt->bpp * 800;
	csi_dev->pix.width = 800;
	csi_dev->pix.height = 480;
	csi_dev->pix.pixelformat = V4L2_PIX_FMT_RGB565;
	csi_dev->pix.field = V4L2_FIELD_NONE;
	csi_dev->pix.bytesperline = csi_dev->bytesperline;
	csi_dev->pix.sizeimage = csi_dev->bytesperline * csi_dev->pix.height;
	csi_dev->pix.colorspace = V4L2_COLORSPACE_SRGB;
}

/**
 * csi_read() - 读取 CSI MMIO 寄存器
 * @csi: CSI 私有数据，@regbase 必须已经映射。
 * @offset: 相对 CSI 寄存器基址的偏移，例如 CSI_CSICR1。
 *
 * __raw_readl() 是低层 MMIO 读 API，不做 endian 转换，也不提供额外
 * 内存屏障。这里访问的是 SoC 内部寄存器，偏移均由本驱动宏定义控制。
 *
 * Return: 寄存器当前 32-bit 原始值。
 */
static inline int csi_read(struct mx6s_csi_dev *csi, unsigned int offset)
{
	return __raw_readl(csi->regbase + offset);
}

/**
 * csi_write() - 写入 CSI MMIO 寄存器
 * @csi: CSI 私有数据，@regbase 必须已经映射。
 * @value: 要写入寄存器的 32-bit 值。
 * @offset: 相对 CSI 寄存器基址的偏移，例如 CSI_CSIDMASA_FB1。
 *
 * __raw_writel() 直接向 MMIO 地址写入原始值。调用者负责按硬件手册
 * 组织 bit，并负责需要的读改写顺序。
 */
static inline void csi_write(struct mx6s_csi_dev *csi, unsigned int value,
			     unsigned int offset)
{
	__raw_writel(value, csi->regbase + offset);
}

/**
 * notifier_to_mx6s_dev() - 由 async notifier 指针取回 CSI 私有对象
 * @n: 嵌入在 struct mx6s_csi_dev 中的 v4l2_async_notifier。
 *
 * container_of() 是内核常用的“由成员指针反推外层结构体”宏。这里
 * async core 只把 notifier 传给回调，驱动需要借它回到 mx6s_csi_dev。
 *
 * Return: 包含 @n 的 struct mx6s_csi_dev 指针。
 */
static inline struct mx6s_csi_dev
				*notifier_to_mx6s_dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct mx6s_csi_dev, subdev_notifier);
}

/**
 * format_by_fourcc() - 按 V4L2 fourcc 查找 host 格式描述
 * @fourcc: 用户态传入的 V4L2_PIX_FMT_* 像素格式。
 *
 * Return: 找到时返回 formats[] 中的条目，未找到时返回 NULL。
 */
static struct mx6s_fmt *format_by_fourcc(int fourcc)
{
	int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].pixelformat == fourcc)
			return formats + i;
	}

	pr_err("unknown pixelformat:'%4.4s'\n", (char *)&fourcc);
	return NULL;
}

/**
 * format_by_mbus() - 按 media-bus code 查找 host 格式描述
 * @code: sensor subdev 枚举出的 MEDIA_BUS_FMT_* code。
 *
 * Return: 找到时返回 formats[] 中的条目，未找到时返回 NULL。
 */
struct mx6s_fmt *format_by_mbus(u32 code)
{
	int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].mbus_code == code)
			return formats + i;
	}

	pr_err("unknown mbus:0x%x\n", code);
	return NULL;
}

/**
 * mx6s_ibuf_to_buf() - 由内部队列节点取回用户态 mx6s_buffer
 * @int_buf: 嵌入在 struct mx6s_buffer 中的 internal 节点。
 *
 * 丢帧节点不是 struct mx6s_buffer 的成员，调用前必须确认
 * @int_buf->discard 为 false。
 *
 * Return: 包含 @int_buf 的 struct mx6s_buffer 指针。
 */
static struct mx6s_buffer *mx6s_ibuf_to_buf(struct mx6s_buf_internal *int_buf)
{
	return container_of(int_buf, struct mx6s_buffer, internal);
}

/**
 * csi_clk_enable() - 打开 CSI 访问和采集所需时钟
 * @csi_dev: CSI 私有数据，三个 clk 指针必须已由 devm_clk_get() 获取。
 */
void csi_clk_enable(struct mx6s_csi_dev *csi_dev)
{
	/*
	 * clk_prepare_enable() 等价于 prepare + enable，适合在可睡眠的
	 * 进程上下文中开启时钟。CSI 访问寄存器前必须先打开相关总线和模块时钟。
	 */
	clk_prepare_enable(csi_dev->clk_disp_axi);
	clk_prepare_enable(csi_dev->clk_disp_dcic);
	clk_prepare_enable(csi_dev->clk_csi_mclk);
}

/**
 * csi_clk_disable() - 关闭 CSI 访问和采集所需时钟
 * @csi_dev: CSI 私有数据。
 */
void csi_clk_disable(struct mx6s_csi_dev *csi_dev)
{
	/*
	 * disable 顺序与 enable 相反，保证先关模块时钟，再关其依赖的显示/AXI
	 * 时钟。每次 clk_prepare_enable() 都要和 clk_disable_unprepare() 配对。
	 */
	clk_disable_unprepare(csi_dev->clk_csi_mclk);
	clk_disable_unprepare(csi_dev->clk_disp_dcic);
	clk_disable_unprepare(csi_dev->clk_disp_axi);
}

/**
 * csihw_reset() - 将 CSI 控制寄存器恢复为硬件复位默认值
 * @csi_dev: CSI 私有数据，寄存器和时钟必须可访问。
 */
static void csihw_reset(struct mx6s_csi_dev *csi_dev)
{
	/*
	 * 先置位帧计数复位，再把 CSI 控制寄存器写回硬件默认值。
	 * __raw_readl()/__raw_writel() 直接访问 MMIO 寄存器，不做额外字节序转换。
	 */
	__raw_writel(__raw_readl(csi_dev->regbase + CSI_CSICR3)
			| BIT_FRMCNT_RST,
			csi_dev->regbase + CSI_CSICR3);

	__raw_writel(CSICR1_RESET_VAL, csi_dev->regbase + CSI_CSICR1);
	__raw_writel(CSICR2_RESET_VAL, csi_dev->regbase + CSI_CSICR2);
	__raw_writel(CSICR3_RESET_VAL, csi_dev->regbase + CSI_CSICR3);
}

/**
 * csisw_reset() - 对运行中的 CSI 做软件复位
 * @csi_dev: CSI 私有数据。
 *
 * 复位顺序是：关闭 CSI、清 RxFIFO、刷新嵌入式 DMA、清 pending 状态、
 * 再重新打开 CSI。msleep() 会让出 CPU，只能在可睡眠上下文中使用。
 */
static void csisw_reset(struct mx6s_csi_dev *csi_dev)
{
	int cr1, cr3, cr18, isr;

	/* Disable csi  */
	cr18 = csi_read(csi_dev, CSI_CSICR18);
	cr18 &= ~BIT_CSI_ENABLE;
	csi_write(csi_dev, cr18, CSI_CSICR18);

	/* Clear RX FIFO */
	cr1 = csi_read(csi_dev, CSI_CSICR1);
	csi_write(csi_dev, cr1 & ~BIT_FCC, CSI_CSICR1);
	cr1 = csi_read(csi_dev, CSI_CSICR1);
	csi_write(csi_dev, cr1 | BIT_CLR_RXFIFO, CSI_CSICR1);

	/* DMA reflash */
	cr3 = csi_read(csi_dev, CSI_CSICR3);
	cr3 |= BIT_DMA_REFLASH_RFF | BIT_FRMCNT_RST;
	csi_write(csi_dev, cr3, CSI_CSICR3);

	/* msleep() 至少睡眠指定毫秒数，用来等待硬件 FIFO/DMA 状态稳定。 */
	msleep(2);

	cr1 = csi_read(csi_dev, CSI_CSICR1);
	csi_write(csi_dev, cr1 | BIT_FCC, CSI_CSICR1);

	isr = csi_read(csi_dev, CSI_CSISR);
	csi_write(csi_dev, isr, CSI_CSISR);

	/* Enable csi */
	cr18 |= BIT_CSI_ENABLE;
	csi_write(csi_dev, cr18, CSI_CSICR18);
}

/**
 * csi_init_interface() - 初始化 CSI 基础接口寄存器
 * @csi_dev: CSI 私有数据。
 *
 * 这里只配置保守默认值。真正的宽高、MIPI 数据类型和隔行模式在
 * mx6s_configure_csi() 中按当前 V4L2 格式重新设置。
 */
static void csi_init_interface(struct mx6s_csi_dev *csi_dev)
{
	unsigned int val = 0;
	unsigned int imag_para;

	/*
	 * 配置 CSI 基础输入时序和模块时钟：
	 * - SOF/HSYNC 极性；
	 * - 上升沿采样；
	 * - gated clock 模式；
	 * - FIFO control；
	 * - MCLK 分频和 MCLK 输出使能。
	 */
	val |= BIT_SOF_POL;
	val |= BIT_REDGE;
	val |= BIT_GCLK_MODE;
	val |= BIT_HSYNC_POL;
	val |= BIT_FCC;
	val |= 1 << SHIFT_MCLKDIV;
	val |= BIT_MCLKEN;
	__raw_writel(val, csi_dev->regbase + CSI_CSICR1);

	/*
	 * 初始化默认图像参数。后续真正设置格式时，mx6s_configure_csi()
	 * 会通过 csi_set_imagpara() 按用户选择的 width/height 覆盖这里。
	 */
	imag_para = (640 << 16) | 960;
	__raw_writel(imag_para, csi_dev->regbase + CSI_CSIIMAG_PARA);

	/* 刷新嵌入式 DMA 控制器的 RxFIFO 状态。 */
	val = BIT_DMA_REFLASH_RFF;
	__raw_writel(val, csi_dev->regbase + CSI_CSICR3);
}

/**
 * csi_enable_int() - 使能 CSI 采集中断
 * @csi_dev: CSI 私有数据。
 * @arg: 为 1 时额外使能 FB1/FB2 DMA done 中断；其他值只开 SOF/overflow。
 */
static void csi_enable_int(struct mx6s_csi_dev *csi_dev, int arg)
{
	unsigned long cr1 = __raw_readl(csi_dev->regbase + CSI_CSICR1);

	cr1 |= BIT_SOF_INTEN;
	cr1 |= BIT_RFF_OR_INT;
	if (arg == 1) {
		/* Still capture needs DMA interrupt. */
		cr1 |= BIT_FB1_DMA_DONE_INTEN;
		cr1 |= BIT_FB2_DMA_DONE_INTEN;
	}
	__raw_writel(cr1, csi_dev->regbase + CSI_CSICR1);
}

/**
 * csi_disable_int() - 关闭 CSI 采集中断
 * @csi_dev: CSI 私有数据。
 */
static void csi_disable_int(struct mx6s_csi_dev *csi_dev)
{
	unsigned long cr1 = __raw_readl(csi_dev->regbase + CSI_CSICR1);

	cr1 &= ~BIT_SOF_INTEN;
	cr1 &= ~BIT_RFF_OR_INT;
	cr1 &= ~BIT_FB1_DMA_DONE_INTEN;
	cr1 &= ~BIT_FB2_DMA_DONE_INTEN;
	__raw_writel(cr1, csi_dev->regbase + CSI_CSICR1);
}

/**
 * csi_enable() - 打开或关闭 CSI 模块
 * @csi_dev: CSI 私有数据。
 * @arg: 1 表示置位 BIT_CSI_ENABLE，其他值表示清除此位。
 */
static void csi_enable(struct mx6s_csi_dev *csi_dev, int arg)
{
	unsigned long cr = __raw_readl(csi_dev->regbase + CSI_CSICR18);

	if (arg == 1)
		cr |= BIT_CSI_ENABLE;
	else
		cr &= ~BIT_CSI_ENABLE;
	__raw_writel(cr, csi_dev->regbase + CSI_CSICR18);
}

/**
 * csi_buf_stride_set() - 设置 CSI frame buffer 行跨度
 * @csi_dev: CSI 私有数据。
 * @stride: 写入 CSI_CSIFBUF_PARA 的 stride，隔行模式下通常为图像宽度。
 */
static void csi_buf_stride_set(struct mx6s_csi_dev *csi_dev, u32 stride)
{
	__raw_writel(stride, csi_dev->regbase + CSI_CSIFBUF_PARA);
}

/**
 * csi_deinterlace_enable() - 配置 CSI 硬件去隔行开关
 * @csi_dev: CSI 私有数据。
 * @enable: true 置位 BIT_DEINTERLACE_EN，false 清除此位。
 */
static void csi_deinterlace_enable(struct mx6s_csi_dev *csi_dev, bool enable)
{
	unsigned long cr18 = __raw_readl(csi_dev->regbase + CSI_CSICR18);

	if (enable == true)
		cr18 |= BIT_DEINTERLACE_EN;
	else
		cr18 &= ~BIT_DEINTERLACE_EN;

	__raw_writel(cr18, csi_dev->regbase + CSI_CSICR18);
}

/**
 * csi_deinterlace_mode() - 配置去隔行制式模式
 * @csi_dev: CSI 私有数据。
 * @mode: V4L2 标准 ID。V4L2_STD_NTSC 置位 BIT_NTSC_EN，否则按 PAL 类模式处理。
 */
static void csi_deinterlace_mode(struct mx6s_csi_dev *csi_dev, int mode)
{
	unsigned long cr18 = __raw_readl(csi_dev->regbase + CSI_CSICR18);

	if (mode == V4L2_STD_NTSC)
		cr18 |= BIT_NTSC_EN;
	else
		cr18 &= ~BIT_NTSC_EN;

	__raw_writel(cr18, csi_dev->regbase + CSI_CSICR18);
}

/**
 * csi_tvdec_enable() - 配置 CSI 的 TV decoder/BT.656 输入模式
 * @csi_dev: CSI 私有数据。
 * @enable: true 使能 TV decoder 输入、base address 自动切换和 CCIR 模式。
 *
 * 隔行输入需要硬件按 field0/field1 切换写入地址，因此这里同时设置
 * BIT_BASEADDR_SWITCH_*。非隔行并口输入则恢复 SOF 极性和上升沿采样。
 */
static void csi_tvdec_enable(struct mx6s_csi_dev *csi_dev, bool enable)
{
	unsigned long cr18 = __raw_readl(csi_dev->regbase + CSI_CSICR18);
	unsigned long cr1 = __raw_readl(csi_dev->regbase + CSI_CSICR1);

	if (enable == true) {
		cr18 |= (BIT_TVDECODER_IN_EN |
				BIT_BASEADDR_SWITCH_EN |
				BIT_BASEADDR_SWITCH_SEL |
				BIT_BASEADDR_CHG_ERR_EN);
		cr1 |= BIT_CCIR_MODE;
		cr1 &= ~(BIT_SOF_POL | BIT_REDGE);
	} else {
		cr18 &= ~(BIT_TVDECODER_IN_EN |
				BIT_BASEADDR_SWITCH_EN |
				BIT_BASEADDR_SWITCH_SEL |
				BIT_BASEADDR_CHG_ERR_EN);
		cr1 &= ~BIT_CCIR_MODE;
		cr1 |= BIT_SOF_POL | BIT_REDGE;
	}

	__raw_writel(cr18, csi_dev->regbase + CSI_CSICR18);
	__raw_writel(cr1, csi_dev->regbase + CSI_CSICR1);
}

/**
 * csi_dmareq_rff_enable() - 允许 RxFIFO 向嵌入式 DMA 发起请求
 * @csi_dev: CSI 私有数据。
 *
 * RxFIFO 水位达到配置阈值后，CSI 会把数据 DMA 到当前 FB1/FB2 地址。
 * 同时使能 HRESP 错误检测，便于发现总线访问异常。
 */
static void csi_dmareq_rff_enable(struct mx6s_csi_dev *csi_dev)
{
	unsigned long cr3 = __raw_readl(csi_dev->regbase + CSI_CSICR3);
	unsigned long cr2 = __raw_readl(csi_dev->regbase + CSI_CSICR2);

	/* Burst Type of DMA Transfer from RxFIFO. INCR16 */
	cr2 |= 0xC0000000;

	cr3 |= BIT_DMA_REQ_EN_RFF;
	cr3 |= BIT_HRESP_ERR_EN;
	cr3 &= ~BIT_RXFF_LEVEL;
	cr3 |= 0x2 << 4;

	__raw_writel(cr3, csi_dev->regbase + CSI_CSICR3);
	__raw_writel(cr2, csi_dev->regbase + CSI_CSICR2);
}

/**
 * csi_dmareq_rff_disable() - 禁止 RxFIFO DMA 请求
 * @csi_dev: CSI 私有数据。
 */
static void csi_dmareq_rff_disable(struct mx6s_csi_dev *csi_dev)
{
	unsigned long cr3 = __raw_readl(csi_dev->regbase + CSI_CSICR3);

	/* 禁止 RxFIFO 触发 DMA 请求，并关闭 HRESP 错误中断使能。 */
	cr3 &= ~BIT_DMA_REQ_EN_RFF;
	cr3 &= ~BIT_HRESP_ERR_EN;
	__raw_writel(cr3, csi_dev->regbase + CSI_CSICR3);
}

/**
 * csi_set_imagpara() - 设置 CSI 输入图像宽高参数
 * @csi: CSI 私有数据。
 * @width: 写入 CSI_CSIIMAG_PARA 高 16 位的宽度参数。
 * @height: 写入 CSI_CSIIMAG_PARA 低 16 位的高度参数。
 *
 * 并口 8-bit 输入 YUV/RGB565 时，一个像素占两个 byte cycle，所以调用者
 * 会把 @width 调整成 pix->width * 2；MIPI 输入则保持像素宽度。
 */
static void csi_set_imagpara(struct mx6s_csi_dev *csi,
					int width, int height)
{
	int imag_para = 0;
	unsigned long cr3 = __raw_readl(csi->regbase + CSI_CSICR3);

	imag_para = (width << 16) | height;
	__raw_writel(imag_para, csi->regbase + CSI_CSIIMAG_PARA);

	/* Refresh the embedded DMA controller. */
	__raw_writel(cr3 | BIT_DMA_REFLASH_RFF, csi->regbase + CSI_CSICR3);
}

/* Videobuf2 queue operations. */
/**
 * mx6s_videobuf_setup() - 配置 VB2 buffer 数量、平面大小和分配上下文
 * @vq: VB2 capture 队列，open() 中的 q->drv_priv 指向 CSI 私有数据。
 * @fmt: VIDIOC_CREATE_BUFS 传入的目标格式；为 NULL 时表示 REQBUFS 路径。
 * @count: 输入为用户请求的 buffer 数，输出为驱动允许的 buffer 数。
 * @num_planes: 输出为 plane 数。本驱动所有格式都是单 plane。
 * @sizes: 输出每个 plane 的最小字节数。
 * @alloc_ctxs: 输出每个 plane 使用的 vb2 allocator 上下文。
 *
 * vb2_get_drv_priv() 返回 q->drv_priv，也就是 mx6s_csi_open() 填进去的
 * csi_dev。VB2 core 在 VIDIOC_REQBUFS 时调用该回调来决定分配多少内存。
 *
 * Return: 成功返回 0；CREATE_BUFS 请求与当前格式不匹配时返回 -EINVAL。
 */
static int mx6s_videobuf_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *count, unsigned int *num_planes,
			unsigned int sizes[], void *alloc_ctxs[])
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vq);

	dev_dbg(csi_dev->dev, "count=%d, size=%d\n", *count, sizes[0]);

	if (fmt) {
		const struct v4l2_pix_format *pix = &fmt->fmt.pix;

		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		if (pix->pixelformat && pix->pixelformat != csi_dev->pix.pixelformat)
			return -EINVAL;
		if (pix->width && pix->width != csi_dev->pix.width)
			return -EINVAL;
		if (pix->height && pix->height != csi_dev->pix.height)
			return -EINVAL;
		if (pix->sizeimage && pix->sizeimage < csi_dev->pix.sizeimage)
			return -EINVAL;
	}

	/*
	 * alloc_ctxs[0] 告诉 vb2_dma_contig_memops：第 0 个 plane 使用
	 * open() 中创建的连续 DMA 分配上下文。
	 */
	alloc_ctxs[0] = csi_dev->alloc_ctx;

	/* sizeimage 来自 TRY/S_FMT 后的当前像素格式，是单帧最小 DMA 大小。 */
	sizes[0] = csi_dev->pix.sizeimage;

	pr_debug("size=%d\n", sizes[0]);
	if (0 == *count)
		*count = 32;
	if (!*num_planes &&
	    sizes[0] * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = (MAX_VIDEO_MEM * 1024 * 1024) / sizes[0];

	*num_planes = 1;

	return 0;
}

/**
 * mx6s_videobuf_prepare() - 在 buffer 入队前校验并设置有效载荷大小
 * @vb: VB2 core 管理的单个 buffer。
 *
 * VB2 在 QBUF 路径调用该回调。驱动把 plane 0 的 payload 设置为当前
 * sizeimage，并检查映射出的平面容量是否足够容纳一帧。
 *
 * Return: 成功返回 0；buffer 太小时返回 -EINVAL。
 */
static int mx6s_videobuf_prepare(struct vb2_buffer *vb)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vb->vb2_queue);
	int ret = 0;

	dev_dbg(csi_dev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	if (vb2_plane_vaddr(vb, 0))
		memset((void *)vb2_plane_vaddr(vb, 0),
		       0xaa, vb2_get_plane_payload(vb, 0));
#endif

	/* vb2_set_plane_payload() 设置 DQBUF 时用户可见的 bytesused。 */
	vb2_set_plane_payload(vb, 0, csi_dev->pix.sizeimage);
	if (vb2_plane_vaddr(vb, 0) &&
	    vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0)) {
		ret = -EINVAL;
		goto out;
	}

	return 0;

out:
	return ret;
}

/**
 * mx6s_videobuf_queue() - 将用户 QBUF 的 buffer 放入 CSI 等待队列
 * @vb: 已由 VB2 core 准备好的 buffer。
 *
 * VB2 在 VIDIOC_QBUF 后调用该回调。这里不立即写硬件地址，只把 buffer
 * 挂到 capture 链表；start_streaming() 和 IRQ frame_done 路径再取用。
 */
static void mx6s_videobuf_queue(struct vb2_buffer *vb)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vb->vb2_queue);
	struct mx6s_buffer *buf = container_of(vb, struct mx6s_buffer, vb);
	unsigned long flags;

	dev_dbg(csi_dev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	/*
	 * capture 链表也会被 IRQ handler 消费。spin_lock_irqsave() 在进程
	 * 上下文关本地中断并加锁，避免和硬中断并发修改链表。
	 */
	spin_lock_irqsave(&csi_dev->slock, flags);

	/* list_add_tail() 保持用户 QBUF 顺序，CSI 按 FIFO 顺序采集。 */
	list_add_tail(&buf->internal.queue, &csi_dev->capture);

	spin_unlock_irqrestore(&csi_dev->slock, flags);
}

/**
 * mx6s_update_csi_buf() - 更新 CSI 双 buffer 中某个槽的 DMA 目标地址
 * @csi_dev: CSI 私有数据。
 * @phys: vb2_dma_contig_plane_dma_addr() 返回的 DMA 地址。
 * @bufnum: 0 对应 FB1，1 对应 FB2。
 */
static void mx6s_update_csi_buf(struct mx6s_csi_dev *csi_dev,
				 unsigned long phys, int bufnum)
{
	if (bufnum == 1)
		csi_write(csi_dev, phys, CSI_CSIDMASA_FB2);
	else
		csi_write(csi_dev, phys, CSI_CSIDMASA_FB1);
}

/**
 * mx6s_csi_init() - open 阶段初始化 CSI 硬件
 * @csi_dev: CSI 私有数据。
 */
static void mx6s_csi_init(struct mx6s_csi_dev *csi_dev)
{
	/* 1. 打开 CSI 寄存器访问、总线传输和模块运行所需的时钟。 */
	csi_clk_enable(csi_dev);
	/* 2. 将 CSI 控制寄存器恢复到已知默认状态。 */
	csihw_reset(csi_dev);
	/* 3. 写入基础输入时序、默认图像参数和 DMA 刷新配置。 */
	csi_init_interface(csi_dev);
	/* 4. open 阶段只初始化控制器，不立即让 RxFIFO 发起 DMA 请求。 */
	csi_dmareq_rff_disable(csi_dev);
}

/**
 * mx6s_csi_deinit() - close 阶段复位 CSI 并关闭时钟
 * @csi_dev: CSI 私有数据。
 */
static void mx6s_csi_deinit(struct mx6s_csi_dev *csi_dev)
{
	csihw_reset(csi_dev);
	csi_init_interface(csi_dev);
	csi_dmareq_rff_disable(csi_dev);
	csi_clk_disable(csi_dev);
}

/**
 * mx6s_csi_enable() - 按当前输入路径启动 CSI 采集硬件
 * @csi_dev: CSI 私有数据，格式和 DMA buffer 地址必须已配置。
 *
 * 并口输入路径等待 SOF 后刷新 DMA，再打开 RxFIFO DMA 请求，避免 sensor
 * 刚输出时丢字节导致图像错位。MIPI 输入路径不需要这个 SOF 等待流程。
 * local_irq_save() 临时关闭本地中断，保证等待 SOF 到使能 DMA 的窗口不被
 * 本 CPU 上的中断打断。
 *
 * Return: 成功返回 0；等待 SOF 或 DMA refresh 超时返回 -ETIME。
 */
static int mx6s_csi_enable(struct mx6s_csi_dev *csi_dev)
{
	struct v4l2_pix_format *pix = &csi_dev->pix;
	unsigned long flags;
	unsigned long val;
	int timeout, timeout2;

	csisw_reset(csi_dev);

	if (pix->field == V4L2_FIELD_INTERLACED)
		csi_tvdec_enable(csi_dev, true);

	/* For mipi csi input only */
	if (csi_dev->csi_mux_mipi == true) {
		csi_dmareq_rff_enable(csi_dev);
		csi_enable_int(csi_dev, 1);
		csi_enable(csi_dev, 1);
		return 0;
	}

	local_irq_save(flags);
	for (timeout = 10000000; timeout > 0; timeout--) {
		if (csi_read(csi_dev, CSI_CSISR) & BIT_SOF_INT) {
			val = csi_read(csi_dev, CSI_CSICR3);
			csi_write(csi_dev, val | BIT_DMA_REFLASH_RFF,
					CSI_CSICR3);
			/* Wait DMA reflash done */
			for (timeout2 = 1000000; timeout2 > 0; timeout2--) {
				if (csi_read(csi_dev, CSI_CSICR3) &
					BIT_DMA_REFLASH_RFF)
					cpu_relax();
				else
					break;
			}
			if (timeout2 <= 0) {
				pr_err("timeout when wait for reflash done.\n");
				local_irq_restore(flags);
				return -ETIME;
			}
			/* For imx6sl csi, DMA FIFO will auto start when sensor ready to work,
			 * so DMA should enable right after FIFO reset, otherwise dma will lost data
			 * and image will split.
			 */
			csi_dmareq_rff_enable(csi_dev);
			csi_enable_int(csi_dev, 1);
			csi_enable(csi_dev, 1);
			break;
		} else
			cpu_relax();
	}
	if (timeout <= 0) {
		pr_err("timeout when wait for SOF\n");
		local_irq_restore(flags);
		return -ETIME;
	}
	local_irq_restore(flags);

	return 0;
}

/**
 * mx6s_csi_disable() - 停止 CSI 采集硬件
 * @csi_dev: CSI 私有数据。
 */
static void mx6s_csi_disable(struct mx6s_csi_dev *csi_dev)
{
	struct v4l2_pix_format *pix = &csi_dev->pix;

	csi_dmareq_rff_disable(csi_dev);
	csi_disable_int(csi_dev);

	/* set CSI_CSIDMASA_FB1 and CSI_CSIDMASA_FB2 to default value */
	csi_write(csi_dev, 0, CSI_CSIDMASA_FB1);
	csi_write(csi_dev, 0, CSI_CSIDMASA_FB2);

	csi_buf_stride_set(csi_dev, 0);

	if (pix->field == V4L2_FIELD_INTERLACED) {
		csi_deinterlace_enable(csi_dev, false);
		csi_tvdec_enable(csi_dev, false);
	}

	csi_enable(csi_dev, 0);
}

/**
 * mx6s_configure_csi() - 按当前 V4L2 格式配置 CSI 图像参数和输入模式
 * @csi_dev: CSI 私有数据，@fmt 和 @pix 必须已经由 S_FMT 更新。
 *
 * 该函数负责把 V4L2 像素格式转换为 CSI 寄存器里的宽度、MIPI data type
 * 和隔行相关设置。并口 8-bit YUV/RGB565 需要把宽度转换成 byte cycle。
 *
 * Return: 成功返回 0；格式不支持时返回 -EINVAL。
 */
static int mx6s_configure_csi(struct mx6s_csi_dev *csi_dev)
{
	struct v4l2_pix_format *pix = &csi_dev->pix;
	u32 cr1, cr18;
	u32 width;

	if (pix->field == V4L2_FIELD_INTERLACED) {
		csi_deinterlace_enable(csi_dev, true);
		csi_buf_stride_set(csi_dev, csi_dev->pix.width);
		csi_deinterlace_mode(csi_dev, csi_dev->std);
	} else {
		csi_deinterlace_enable(csi_dev, false);
		csi_buf_stride_set(csi_dev, 0);
	}

	switch (csi_dev->fmt->pixelformat) {
	case V4L2_PIX_FMT_YUV32:
	case V4L2_PIX_FMT_SBGGR8:
		width = pix->width;
		break;
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_YUYV:
		if (csi_dev->csi_mux_mipi == true)
			width = pix->width;
		else
			/* For parallel 8-bit sensor input */
			width = pix->width * 2;
		break;
	default:
		pr_debug("   case not supported\n");
		return -EINVAL;
	}
	csi_set_imagpara(csi_dev, width, pix->height);

	if (csi_dev->csi_mux_mipi == true) {
		cr1 = csi_read(csi_dev, CSI_CSICR1);
		cr1 &= ~BIT_GCLK_MODE;
		csi_write(csi_dev, cr1, CSI_CSICR1);

		cr18 = csi_read(csi_dev, CSI_CSICR18);
		cr18 &= BIT_MIPI_DATA_FORMAT_MASK;
		cr18 |= BIT_DATA_FROM_MIPI;

		switch (csi_dev->fmt->pixelformat) {
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YUYV:
			cr18 |= BIT_MIPI_DATA_FORMAT_YUV422_8B;
			break;
		case V4L2_PIX_FMT_SBGGR8:
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW8;
			break;
		default:
			pr_debug("   fmt not supported\n");
			return -EINVAL;
		}

		csi_write(csi_dev, cr18, CSI_CSICR18);
	}
	return 0;
}

/**
 * mx6s_start_streaming() - VB2 streamon 时装载初始 DMA buffer 并启动 CSI
 * @vq: VB2 capture 队列。
 * @count: VB2 core 已准备好的 queued buffer 数量。
 *
 * CSI 硬件有 FB1/FB2 两个 DMA 目标槽，因此至少需要两个用户 buffer。
 * 当用户后续补 buffer 不及时，驱动会把帧写入 discard_buffer，保证硬件
 * 可以持续运行而不是因为没有目标地址停住。
 *
 * Return: 成功返回 0；buffer 不足返回 -ENOBUFS；丢帧 DMA buffer 分配失败
 * 返回 -ENOMEM；硬件启动失败返回相应负 errno。
 */
static int mx6s_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vq);
	struct vb2_buffer *vb;
	struct mx6s_buffer *buf, *tmp;
	unsigned long phys;
	unsigned long flags;
	int ret;

	if (count < 2)
		return -ENOBUFS;

	/*
	 * I didn't manage to properly enable/disable
	 * a per frame basis during running transfers,
	 * thus we allocate a buffer here and use it to
	 * discard frames when no buffer is available.
	 * Feel free to work on this ;)
	 */
	csi_dev->discard_size = csi_dev->pix.sizeimage;
	/*
	 * dma_alloc_coherent() 同时返回 CPU 可访问虚拟地址和硬件可用 DMA
	 * 地址，且该内存对设备/CPU 保持一致性，不需要手工 cache sync。
	 */
	csi_dev->discard_buffer = dma_alloc_coherent(csi_dev->v4l2_dev.dev,
					PAGE_ALIGN(csi_dev->discard_size),
					&csi_dev->discard_buffer_dma,
					GFP_DMA | GFP_KERNEL);
	if (!csi_dev->discard_buffer)
		return -ENOMEM;

	spin_lock_irqsave(&csi_dev->slock, flags);

	csi_dev->buf_discard[0].discard = true;
	list_add_tail(&csi_dev->buf_discard[0].queue,
		      &csi_dev->discard);

	csi_dev->buf_discard[1].discard = true;
	list_add_tail(&csi_dev->buf_discard[1].queue,
		      &csi_dev->discard);

	/* csi buf 0 */
	buf = list_first_entry(&csi_dev->capture, struct mx6s_buffer,
			       internal.queue);
	buf->internal.bufnum = 0;
	vb = &buf->vb;
	vb->state = VB2_BUF_STATE_ACTIVE;

	/* vb2_dma_contig_plane_dma_addr() 取 plane 0 的连续 DMA 总线地址。 */
	phys = vb2_dma_contig_plane_dma_addr(vb, 0);

	mx6s_update_csi_buf(csi_dev, phys, buf->internal.bufnum);
	list_move_tail(csi_dev->capture.next, &csi_dev->active_bufs);

	/* csi buf 1 */
	buf = list_first_entry(&csi_dev->capture, struct mx6s_buffer,
			       internal.queue);
	buf->internal.bufnum = 1;
	vb = &buf->vb;
	vb->state = VB2_BUF_STATE_ACTIVE;

	phys = vb2_dma_contig_plane_dma_addr(vb, 0);
	mx6s_update_csi_buf(csi_dev, phys, buf->internal.bufnum);
	list_move_tail(csi_dev->capture.next, &csi_dev->active_bufs);

	spin_unlock_irqrestore(&csi_dev->slock, flags);

	ret = mx6s_csi_enable(csi_dev);
	if (ret < 0) {
		spin_lock_irqsave(&csi_dev->slock, flags);

		list_del_init(&csi_dev->buf_discard[0].queue);
		list_del_init(&csi_dev->buf_discard[1].queue);

		list_for_each_entry_safe(buf, tmp,
					&csi_dev->active_bufs, internal.queue) {
			list_del_init(&buf->internal.queue);
			if (buf->vb.state == VB2_BUF_STATE_ACTIVE)
				vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		}

		list_for_each_entry_safe(buf, tmp,
					&csi_dev->capture, internal.queue) {
			list_del_init(&buf->internal.queue);
			if (buf->vb.state == VB2_BUF_STATE_ACTIVE)
				vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		}

		spin_unlock_irqrestore(&csi_dev->slock, flags);

		dma_free_coherent(csi_dev->v4l2_dev.dev,
				  PAGE_ALIGN(csi_dev->discard_size),
				  csi_dev->discard_buffer,
				  csi_dev->discard_buffer_dma);
		csi_dev->discard_buffer = NULL;
	}

	return ret;
}

/**
 * mx6s_stop_streaming() - VB2 streamoff 时停止 CSI 并归还所有未完成 buffer
 * @vq: VB2 capture 队列。
 *
 * VB2 要求驱动在 stop_streaming() 返回前，把已经交给硬件或驱动持有的
 * 所有 buffer 通过 vb2_buffer_done() 还给 core。
 */
static void mx6s_stop_streaming(struct vb2_queue *vq)
{
	struct mx6s_csi_dev *csi_dev = vb2_get_drv_priv(vq);
	unsigned long flags;
	struct mx6s_buffer *buf, *tmp;
	void *b;

	mx6s_csi_disable(csi_dev);

	spin_lock_irqsave(&csi_dev->slock, flags);

	list_for_each_entry_safe(buf, tmp,
				&csi_dev->active_bufs, internal.queue) {
		list_del_init(&buf->internal.queue);
		if (buf->vb.state == VB2_BUF_STATE_ACTIVE)
			/* 标记 ERROR 后，阻塞在 DQBUF/read/poll 的用户会被唤醒。 */
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	list_for_each_entry_safe(buf, tmp,
				&csi_dev->capture, internal.queue) {
		list_del_init(&buf->internal.queue);
		if (buf->vb.state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	INIT_LIST_HEAD(&csi_dev->capture);
	INIT_LIST_HEAD(&csi_dev->active_bufs);
	INIT_LIST_HEAD(&csi_dev->discard);

	b = csi_dev->discard_buffer;
	csi_dev->discard_buffer = NULL;

	spin_unlock_irqrestore(&csi_dev->slock, flags);

	/* size、CPU 地址和 DMA 地址必须与 dma_alloc_coherent() 时匹配。 */
	dma_free_coherent(csi_dev->v4l2_dev.dev,
				csi_dev->discard_size, b,
				csi_dev->discard_buffer_dma);
}

static struct vb2_ops mx6s_videobuf_ops = {
	.queue_setup     = mx6s_videobuf_setup,
	.buf_prepare     = mx6s_videobuf_prepare,
	.buf_queue       = mx6s_videobuf_queue,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.start_streaming = mx6s_start_streaming,
	.stop_streaming	 = mx6s_stop_streaming,
};

/**
 * mx6s_csi_frame_done() - 处理某个 CSI DMA buffer 槽的一帧完成事件
 * @csi_dev: CSI 私有数据，调用者必须持有 @slock。
 * @bufnum: 完成的硬件槽号，0 表示 FB1，1 表示 FB2。
 * @err: true 表示该帧应以 VB2_BUF_STATE_ERROR 返回。
 *
 * 该函数把 active buffer 还给 VB2，然后从 capture 队列取下一个用户
 * buffer 重新装入同一个硬件槽；如果用户没有及时 QBUF，则装入 discard
 * buffer 丢弃后续帧。
 */
static void mx6s_csi_frame_done(struct mx6s_csi_dev *csi_dev,
		int bufnum, bool err)
{
	struct mx6s_buf_internal *ibuf;
	struct mx6s_buffer *buf;
	struct vb2_buffer *vb;
	unsigned long phys;

	ibuf = list_first_entry(&csi_dev->active_bufs, struct mx6s_buf_internal,
			       queue);

	if (ibuf->discard) {
		/*
		 * Discard buffer must not be returned to user space.
		 * Just return it to the discard queue.
		 */
		list_move_tail(csi_dev->active_bufs.next, &csi_dev->discard);
	} else {
		buf = mx6s_ibuf_to_buf(ibuf);

		vb = &buf->vb;
		phys = vb2_dma_contig_plane_dma_addr(vb, 0);
		if (bufnum == 1) {
			if (csi_read(csi_dev, CSI_CSIDMASA_FB2) != phys) {
				dev_err(csi_dev->dev, "%lx != %x\n", phys,
					csi_read(csi_dev, CSI_CSIDMASA_FB2));
			}
		} else {
			if (csi_read(csi_dev, CSI_CSIDMASA_FB1) != phys) {
				dev_err(csi_dev->dev, "%lx != %x\n", phys,
					csi_read(csi_dev, CSI_CSIDMASA_FB1));
			}
		}
		dev_dbg(csi_dev->dev, "%s (vb=0x%p) 0x%p %lu\n", __func__, vb,
				vb2_plane_vaddr(vb, 0),
				vb2_get_plane_payload(vb, 0));

		list_del_init(&buf->internal.queue);
		/* v4l2_get_timestamp() 填充单调时间戳，sequence 是驱动帧计数。 */
		v4l2_get_timestamp(&vb->v4l2_buf.timestamp);
		vb->v4l2_buf.sequence = csi_dev->frame_count;
		if (err)
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		else
			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	}

	csi_dev->frame_count++;

	/* Config discard buffer to active_bufs */
	if (list_empty(&csi_dev->capture)) {
		if (list_empty(&csi_dev->discard)) {
			dev_warn(csi_dev->dev,
					"%s: trying to access empty discard list\n",
					__func__);
			return;
		}

		ibuf = list_first_entry(&csi_dev->discard,
					struct mx6s_buf_internal, queue);
		ibuf->bufnum = bufnum;

		list_move_tail(csi_dev->discard.next, &csi_dev->active_bufs);

		mx6s_update_csi_buf(csi_dev,
					csi_dev->discard_buffer_dma, bufnum);
		return;
	}

	buf = list_first_entry(&csi_dev->capture, struct mx6s_buffer,
			       internal.queue);

	buf->internal.bufnum = bufnum;

	list_move_tail(csi_dev->capture.next, &csi_dev->active_bufs);

	vb = &buf->vb;
	vb->state = VB2_BUF_STATE_ACTIVE;

	phys = vb2_dma_contig_plane_dma_addr(vb, 0);
	mx6s_update_csi_buf(csi_dev, phys, bufnum);
}

/**
 * mx6s_csi_irq_handler() - CSI 硬中断处理函数
 * @irq: Linux IRQ number，由 devm_request_irq() 注册时传入。
 * @data: dev_id，注册时传入的 struct mx6s_csi_dev 指针。
 *
 * 中断中读取并清除 CSI_CSISR，处理 FIFO overflow/HRESP/base-address 错误，
 * 并在 FB1/FB2 DMA done 时推进 VB2 buffer 队列。硬中断上下文不可睡眠，
 * 所以这里只做寄存器读写和链表操作。
 *
 * Return: IRQ_HANDLED 表示该中断已由本驱动处理。
 */
static irqreturn_t mx6s_csi_irq_handler(int irq, void *data)
{
	struct mx6s_csi_dev *csi_dev =  data;
	unsigned long status;
	u32 cr1, cr3, cr18;

	/* hardirq 中已经关闭本地中断，使用 spin_lock() 保护共享队列即可。 */
	spin_lock(&csi_dev->slock);

	status = csi_read(csi_dev, CSI_CSISR);
	/* CSI 状态位采用写 1 清除语义，读到什么 pending 位就写回什么位。 */
	csi_write(csi_dev, status, CSI_CSISR);

	if (list_empty(&csi_dev->active_bufs)) {
		dev_warn(csi_dev->dev,
				"%s: called while active list is empty\n",
				__func__);

		spin_unlock(&csi_dev->slock);
		return IRQ_HANDLED;
	}

	if (status & BIT_RFF_OR_INT)
		dev_warn(csi_dev->dev, "%s Rx fifo overflow\n", __func__);
	if (status & BIT_HRESP_ERR_INT)
		dev_warn(csi_dev->dev, "%s Hresponse error detected\n",
			__func__);

	if (status & (BIT_RFF_OR_INT|BIT_HRESP_ERR_INT)) {
		/* software reset */

		/* Disable csi  */
		cr18 = csi_read(csi_dev, CSI_CSICR18);
		cr18 &= ~BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);

		/* Clear RX FIFO */
		cr1 = csi_read(csi_dev, CSI_CSICR1);
		csi_write(csi_dev, cr1 & ~BIT_FCC, CSI_CSICR1);
		cr1 = csi_read(csi_dev, CSI_CSICR1);
		csi_write(csi_dev, cr1 | BIT_CLR_RXFIFO, CSI_CSICR1);

		cr1 = csi_read(csi_dev, CSI_CSICR1);
		csi_write(csi_dev, cr1 | BIT_FCC, CSI_CSICR1);

		/* DMA reflash */
		cr3 = csi_read(csi_dev, CSI_CSICR3);
		cr3 |= BIT_DMA_REFLASH_RFF;
		csi_write(csi_dev, cr3, CSI_CSICR3);

		/* Enable csi */
		cr18 |= BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);
	}

	if (status & BIT_ADDR_CH_ERR_INT) {
		/* Disable csi  */
		cr18 = csi_read(csi_dev, CSI_CSICR18);
		cr18 &= ~BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);

		/* DMA reflash */
		cr3 = csi_read(csi_dev, CSI_CSICR3);
		cr3 |= BIT_DMA_REFLASH_RFF;
		csi_write(csi_dev, cr3, CSI_CSICR3);

		/* Enable csi */
		cr18 |= BIT_CSI_ENABLE;
		csi_write(csi_dev, cr18, CSI_CSICR18);

		pr_debug("base address switching Change Err.\n");
	}

	if ((status & BIT_DMA_TSF_DONE_FB1) &&
		(status & BIT_DMA_TSF_DONE_FB2)) {
		/* For both FB1 and FB2 interrupter bits set case,
		 * CSI DMA is work in one of FB1 and FB2 buffer,
		 * but software can not know the state.
		 * Skip it to avoid base address updated
		 * when csi work in field0 and field1 will write to
		 * new base address.
		 * PDM TKT230775 */
		pr_debug("Skip two frames\n");
	} else if (status & BIT_DMA_TSF_DONE_FB1) {
		mx6s_csi_frame_done(csi_dev, 0, false);
	} else if (status & BIT_DMA_TSF_DONE_FB2) {
		mx6s_csi_frame_done(csi_dev, 1, false);
	}

	spin_unlock(&csi_dev->slock);

	return IRQ_HANDLED;
}

/*
 * File operations for the device
 */
/**
 * mx6s_csi_open - 打开 V4L2 视频采集节点
 * @file: 本次 open("/dev/videoX") 对应的文件实例。
 *
 * 返回: 成功返回 0，失败返回负 errno 值。
 */
static int mx6s_csi_open(struct file *file)
{
	/*
	 * video_drvdata() 通过 file 对应的 video_device 取回 probe 阶段
	 * video_set_drvdata() 绑定的设备级私有数据。
	 */
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	/* 已异步绑定的摄像头 sensor subdev，例如本工程中的 OV5640。 */
	struct v4l2_subdev *sd = csi_dev->sd;
	/* 本 CSI 设备的 videobuf2 采集队列，管理用户态 buffer 和 DMA。 */
	struct vb2_queue *q = &csi_dev->vb2_vidq;
	int ret = 0;

	/*
	 * file->private_data 属于本次 open 文件实例。这里保存 csi_dev，
	 * 后续 V4L2 ioctl core 会把它作为 priv/fh 参数传给 vidioc 回调。
	 */
	file->private_data = csi_dev;

	/*
	 * open 路径会初始化共享队列、电源和硬件寄存器，因此用设备锁串行化，
	 * 可被信号打断时返回 -ERESTARTSYS。
	 */
	if (mutex_lock_interruptible(&csi_dev->lock))
		return -ERESTARTSYS;

	/*
	 * 为 vb2_dma_contig_memops 创建分配上下文。上下文里保存 struct device，
	 * 之后 REQBUFS/USERPTR 路径可据此分配或映射适合 CSI DMA 的连续内存。
	 */
	csi_dev->alloc_ctx = vb2_dma_contig_init_ctx(csi_dev->dev);
	if (IS_ERR(csi_dev->alloc_ctx)) {
		ret = PTR_ERR(csi_dev->alloc_ctx);
		csi_dev->alloc_ctx = NULL;
		goto unlock;
	}

	/*
	 * 配置 videobuf2 队列的必需字段。vb2_queue_init() 会检查 type、
	 * io_modes、ops、mem_ops 等字段，并初始化队列内部链表、等待队列和锁。
	 */
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = csi_dev;
	q->ops = &mx6s_videobuf_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	/* 每个 vb2_buffer 外包一层 mx6s_buffer，用于挂入驱动自己的链表。 */
	q->buf_struct_size = sizeof(struct mx6s_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	/* 让 V4L2/VB2 core 在队列 ioctl 路径复用本驱动的设备锁。 */
	q->lock = &csi_dev->lock;

	ret = vb2_queue_init(q);
	if (ret < 0)
		goto eallocctx;

	/* 增加 runtime PM 使用计数并同步恢复 CSI 设备，确保寄存器可访问。 */
	pm_runtime_get_sync(csi_dev->dev);

	/* 采集期间提高 i.MX6 总线/DDR 频率，避免 CSI DMA 带宽不足。 */
	request_bus_freq(BUS_FREQ_HIGH);

	/*
	 * 通知 sensor subdev 退出低功耗，随后初始化 CSI 控制器寄存器和内部状态。
	 */
	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0)
		goto epower;

	mx6s_csi_init(csi_dev);

	mutex_unlock(&csi_dev->lock);

	return ret;
epower:
	release_bus_freq(BUS_FREQ_HIGH);
	pm_runtime_put_sync_suspend(csi_dev->dev);
eallocctx:
	vb2_dma_contig_cleanup_ctx(csi_dev->alloc_ctx);
unlock:
	mutex_unlock(&csi_dev->lock);
	return ret;
}

/**
 * mx6s_csi_close() - 关闭 V4L2 视频采集节点
 * @file: 本次 close 对应的文件实例。
 *
 * 释放 VB2 队列、关闭 sensor 电源、清理 DMA 分配上下文，并降低 runtime PM
 * 使用计数。调用顺序和 mx6s_csi_open() 基本相反。
 *
 * Return: 成功返回 0。
 */
static int mx6s_csi_close(struct file *file)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	mutex_lock(&csi_dev->lock);

	/* vb2_queue_release() 释放 REQBUFS 创建的队列资源并归还残留 buffer。 */
	vb2_queue_release(&csi_dev->vb2_vidq);

	mx6s_csi_deinit(csi_dev);
	v4l2_subdev_call(sd, core, s_power, 0);

	vb2_dma_contig_cleanup_ctx(csi_dev->alloc_ctx);
	mutex_unlock(&csi_dev->lock);

	file->private_data = NULL;

	release_bus_freq(BUS_FREQ_HIGH);

	pm_runtime_put_sync_suspend(csi_dev->dev);
	return 0;
}

/**
 * mx6s_csi_read() - 支持 read() 方式读取视频帧
 * @file: 打开的 /dev/videoX 文件实例。
 * @buf: 用户空间目标缓冲区。
 * @count: 用户缓冲区长度。
 * @ppos: 文件偏移，视频设备通常不使用实际位置语义。
 *
 * vb2_read() 是 VB2 file-io 辅助 API，会在内部执行 buffer 分配、排队、
 * 等待完成和拷贝到用户空间。
 *
 * Return: 成功返回拷贝的字节数；失败返回负 errno。
 */
static ssize_t mx6s_csi_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	int ret;

	dev_dbg(csi_dev->dev, "read called, buf %p\n", buf);

	mutex_lock(&csi_dev->lock);
	ret = vb2_read(&csi_dev->vb2_vidq, buf, count, ppos,
				file->f_flags & O_NONBLOCK);
	mutex_unlock(&csi_dev->lock);
	return ret;
}

/**
 * mx6s_csi_mmap() - 将 VB2 MMAP buffer 映射到用户空间
 * @file: 打开的 /dev/videoX 文件实例。
 * @vma: 用户进程的虚拟内存区域描述。
 *
 * vb2_mmap() 根据 QUERYBUF 返回的 offset 找到对应 buffer，并建立用户态
 * VMA 到 DMA buffer 的映射。
 *
 * Return: 成功返回 0；失败返回负 errno。
 */
static int mx6s_csi_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&csi_dev->lock))
		return -ERESTARTSYS;
	ret = vb2_mmap(&csi_dev->vb2_vidq, vma);
	mutex_unlock(&csi_dev->lock);

	pr_debug("vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

/*
 * V4L2 file operations。video_ioctl2() 是 V4L2 ioctl 分发入口，它会把
 * 用户态 VIDIOC_* 命令解码后调用 mx6s_csi_ioctl_ops 中对应的 vidioc 回调。
 */
static struct v4l2_file_operations mx6s_csi_fops = {
	.owner		= THIS_MODULE,
	.open		= mx6s_csi_open,
	.release	= mx6s_csi_close,
	.read		= mx6s_csi_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2, /* V4L2 ioctl handler */
	.mmap		= mx6s_csi_mmap,
};

/* Video node IOCTL callbacks. */
/**
 * mx6s_vidioc_enum_input() - 实现 VIDIOC_ENUMINPUT
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle，本驱动期望等于 file->private_data。
 * @inp: 用户传入/返回的输入源描述。
 *
 * 本 CSI host 只有一个 camera 输入，index 只能为 0。
 *
 * Return: 成功返回 0；输入 index 非 0 返回 -EINVAL。
 */
static int mx6s_vidioc_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	/* default is camera */
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strcpy(inp->name, "Camera");

	return 0;
}

/**
 * mx6s_vidioc_g_input() - 实现 VIDIOC_G_INPUT
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @i: 返回当前输入源 index。
 *
 * Return: 成功返回 0。
 */
static int mx6s_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return 0;
}

/**
 * mx6s_vidioc_s_input() - 实现 VIDIOC_S_INPUT
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @i: 要选择的输入源 index，只支持 0。
 *
 * Return: 成功返回 0；输入 index 非 0 返回 -EINVAL。
 */
static int mx6s_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

/**
 * mx6s_vidioc_querystd() - 实现 VIDIOC_QUERYSTD 并转发给 sensor subdev
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @a: 返回检测到的视频标准。
 *
 * v4l2_subdev_call() 会检查 subdev 对应 ops 是否存在；不存在时通常返回
 * -ENOIOCTLCMD，存在则调用 sensor 的 video.querystd 回调。
 *
 * Return: subdev 回调返回值。
 */
static int mx6s_vidioc_querystd(struct file *file, void *priv, v4l2_std_id *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, querystd, a);
}

/**
 * mx6s_vidioc_s_std() - 实现 VIDIOC_S_STD 并转发给 sensor subdev
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @a: 用户选择的视频标准 ID。
 *
 * Return: subdev video.s_std 的返回值。
 */
static int mx6s_vidioc_s_std(struct file *file, void *priv, v4l2_std_id a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, s_std, a);
}

/**
 * mx6s_vidioc_g_std() - 实现 VIDIOC_G_STD 并转发给 sensor subdev
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @a: 返回当前视频标准 ID。
 *
 * Return: subdev video.g_std 的返回值。
 */
static int mx6s_vidioc_g_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, g_std, a);
}

/**
 * mx6s_vidioc_reqbufs() - 实现 VIDIOC_REQBUFS
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @p: 用户请求的 buffer 类型、数量和内存类型。
 *
 * vb2_reqbufs() 会检查 memory/type，并调用 mx6s_videobuf_setup() 完成实际
 * buffer 分配准备。
 *
 * Return: VB2 core 返回值。
 */
static int mx6s_vidioc_reqbufs(struct file *file, void *priv,
			      struct v4l2_requestbuffers *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	return vb2_reqbufs(&csi_dev->vb2_vidq, p);
}

/**
 * mx6s_vidioc_querybuf() - 实现 VIDIOC_QUERYBUF
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @p: 用户指定 index，返回该 buffer 的长度、offset、flags 等信息。
 *
 * vb2_querybuf() 填充标准 V4L2 buffer 信息。本驱动在 buffer 已 mmap 时把
 * m.offset 改成 DMA 物理地址，属于 Freescale 旧驱动兼容行为。
 *
 * Return: VB2 core 返回值。
 */
static int mx6s_vidioc_querybuf(struct file *file, void *priv,
			       struct v4l2_buffer *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	int ret;

	WARN_ON(priv != file->private_data);

	ret = vb2_querybuf(&csi_dev->vb2_vidq, p);

	if (!ret) {
		/* return physical address */
		struct vb2_buffer *vb = csi_dev->vb2_vidq.bufs[p->index];
		if (p->flags & V4L2_BUF_FLAG_MAPPED)
			p->m.offset = vb2_dma_contig_plane_dma_addr(vb, 0);
	}
	return ret;
}

/**
 * mx6s_vidioc_qbuf() - 实现 VIDIOC_QBUF
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @p: 用户要排队的 buffer 描述。
 *
 * vb2_qbuf() 完成状态检查、prepare，并最终调用 mx6s_videobuf_queue()。
 *
 * Return: VB2 core 返回值。
 */
static int mx6s_vidioc_qbuf(struct file *file, void *priv,
			   struct v4l2_buffer *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	return vb2_qbuf(&csi_dev->vb2_vidq, p);
}

/**
 * mx6s_vidioc_dqbuf() - 实现 VIDIOC_DQBUF
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @p: 返回已完成 buffer 的描述。
 *
 * file->f_flags & O_NONBLOCK 控制无完成帧时是立即返回 -EAGAIN，还是阻塞
 * 等待 IRQ 路径调用 vb2_buffer_done()。
 *
 * Return: VB2 core 返回值。
 */
static int mx6s_vidioc_dqbuf(struct file *file, void *priv,
			    struct v4l2_buffer *p)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	return vb2_dqbuf(&csi_dev->vb2_vidq, p, file->f_flags & O_NONBLOCK);
}

/**
 * mx6s_vidioc_enum_fmt_vid_cap() - 实现 VIDIOC_ENUM_FMT
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @f: 用户传入格式 index，返回格式描述和 pixelformat。
 *
 * host 自身格式表受 sensor 能输出的 media-bus code 约束，因此先调用
 * sensor 的 enum_mbus_fmt，再用 format_by_mbus() 映射到 V4L2 pixel format。
 *
 * Return: 成功返回 0；枚举结束或格式不支持返回 -EINVAL。
 */
static int mx6s_vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
				       struct v4l2_fmtdesc *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	u32 code;
	struct mx6s_fmt *fmt;
	int ret;

	int index = f->index;

	WARN_ON(priv != file->private_data);

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, index, &code);
	if (ret < 0) {
		/* no more formats */
		dev_dbg(csi_dev->dev, "No more fmt\n");
		return -EINVAL;
	}

	fmt = format_by_mbus(code);
	if (!fmt) {
		dev_err(csi_dev->dev, "mbus (0x%08x) invalid.\n", code);
		return -EINVAL;
	}

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->pixelformat;

	return 0;
}

static int mx6s_negotiate_format(struct mx6s_csi_dev *csi_dev,
				      struct v4l2_pix_format *pix, bool apply,
				      struct mx6s_fmt **selected_fmt)
{
	struct v4l2_subdev *sd = csi_dev->sd;
	struct v4l2_mbus_framefmt mbus_fmt;
	struct mx6s_fmt *fmt;
	int ret;

	fmt = format_by_fourcc(pix->pixelformat);
	if (!fmt) {
		dev_err(csi_dev->dev, "Fourcc format (0x%08x) invalid.",
			pix->pixelformat);
		return -EINVAL;
	}

	if (pix->width == 0 || pix->height == 0) {
		dev_err(csi_dev->dev, "width %d, height %d is too small.\n",
			pix->width, pix->height);
		return -EINVAL;
	}

	v4l2_fill_mbus_format(&mbus_fmt, pix, fmt->mbus_code);
	if (apply)
		ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mbus_fmt);
	else
		ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mbus_fmt);
	if (ret < 0)
		return ret;

	fmt = format_by_mbus(mbus_fmt.code);
	if (!fmt)
		return -EINVAL;

	v4l2_fill_pix_format(pix, &mbus_fmt);
	pix->pixelformat = fmt->pixelformat;
	if (pix->field != V4L2_FIELD_INTERLACED)
		pix->field = V4L2_FIELD_NONE;

	pix->bytesperline = fmt->bpp * pix->width;
	pix->sizeimage = pix->bytesperline * pix->height;

	if (selected_fmt)
		*selected_fmt = fmt;

	return 0;
}

/**
 * mx6s_vidioc_try_fmt_vid_cap() - 实现 VIDIOC_TRY_FMT
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @f: 输入用户期望格式，输出 sensor/host 协商后的格式。
 *
 * TRY_FMT 只调用 sensor 的 try_mbus_fmt，不写 sensor 寄存器；如果 sensor
 * 把 media-bus code 调整为当前已验证的 RGB565，host 侧也同步更新用户可见
 * pixelformat、bytesperline 和 sizeimage。
 *
 * Return: 成功返回 0；格式或尺寸非法返回 -EINVAL；sensor 返回错误则透传。
 */
static int mx6s_vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	return mx6s_negotiate_format(csi_dev, &f->fmt.pix, false, NULL);
}

/**
 * mx6s_vidioc_s_fmt_vid_cap() - 实现 VIDIOC_S_FMT 并应用到 CSI 硬件
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @f: 用户请求的采集格式。
 *
 * S_FMT 调用 sensor 的 s_mbus_fmt 真正应用寄存器，再把协商结果缓存到
 * csi_dev->pix/fmt，最后调用 mx6s_configure_csi() 写入 CSI 图像参数寄存器。
 *
 * Return: 成功返回 0；协商失败返回负 errno。
 */
static int mx6s_vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct mx6s_fmt *fmt;
	int ret;

	ret = mx6s_negotiate_format(csi_dev, &f->fmt.pix, true, &fmt);
	if (ret < 0)
		return ret;

	csi_dev->fmt = fmt;
	csi_dev->mbus_code = fmt->mbus_code;
	csi_dev->pix = f->fmt.pix;
	csi_dev->bytesperline = f->fmt.pix.bytesperline;
	csi_dev->type = f->type;
	dev_dbg(csi_dev->dev, "set to pixelformat '%4.6s'\n",
		(char *)&csi_dev->fmt->name);

	return mx6s_configure_csi(csi_dev);
}

/**
 * mx6s_vidioc_g_fmt_vid_cap() - 实现 VIDIOC_G_FMT
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @f: 返回当前缓存的采集格式。
 *
 * Return: 成功返回 0。
 */
static int mx6s_vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	f->fmt.pix = csi_dev->pix;

	return 0;
}

/**
 * mx6s_vidioc_querycap() - 实现 VIDIOC_QUERYCAP
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @cap: 返回驱动名、设备名、bus 信息和能力位。
 *
 * Return: 成功返回 0。
 */
static int mx6s_vidioc_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	WARN_ON(priv != file->private_data);

	strlcpy(cap->driver, MX6S_CAM_DRV_NAME, sizeof(cap->driver));
	strlcpy(cap->card, MX6S_CAM_DRIVER_DESCRIPTION, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(csi_dev->dev));

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/**
 * mx6s_vidioc_streamon() - 实现 VIDIOC_STREAMON
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @i: 要启动的 buffer 类型，只支持 V4L2_BUF_TYPE_VIDEO_CAPTURE。
 *
 * 先通知 sensor subdev 开始输出图像，再让 vb2_streamon() 启动 CSI host。
 * 这样 mx6s_csi_enable() 等待 SOF 时，sensor 已经在输出帧同步。
 *
 * Return: 成功返回 0；类型不匹配返回 -EINVAL；VB2 启动失败则透传。
 */
static int mx6s_vidioc_streamon(struct file *file, void *priv,
			       enum v4l2_buf_type i)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	int ret;

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = v4l2_subdev_call(sd, video, s_stream, 1);
	if (ret < 0)
		return ret;

	ret = vb2_streamon(&csi_dev->vb2_vidq, i);
	if (ret < 0) {
		v4l2_subdev_call(sd, video, s_stream, 0);
		return ret;
	}

	return 0;
}

/**
 * mx6s_vidioc_streamoff() - 实现 VIDIOC_STREAMOFF
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @i: 要停止的 buffer 类型，只支持 V4L2_BUF_TYPE_VIDEO_CAPTURE。
 *
 * 先让 VB2 停止 host 采集并回收 buffer，再通知 sensor 停止输出。
 *
 * Return: 成功返回 0；类型不匹配返回 -EINVAL。
 */
static int mx6s_vidioc_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type i)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	WARN_ON(priv != file->private_data);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/*
	 * vb2_streamoff() 会进入 stop_streaming()，归还队列中剩余 buffer，
	 * 并把 VB2 队列状态切回非 streaming。
	 */
	vb2_streamoff(&csi_dev->vb2_vidq, i);

	return v4l2_subdev_call(sd, video, s_stream, 0);
}

/**
 * mx6s_vidioc_cropcap() - 实现 VIDIOC_CROPCAP 的占位回调
 * @file: 打开的 /dev/videoX 文件实例。
 * @fh: V4L2 core 传入的 file handle。
 * @a: 用户传入/返回的裁剪能力参数。
 *
 * 当前驱动不实现实际裁剪，只校验 buffer type 后返回成功。
 *
 * Return: 成功返回 0；类型不匹配返回 -EINVAL。
 */
static int mx6s_vidioc_cropcap(struct file *file, void *fh,
			      struct v4l2_cropcap *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	dev_dbg(csi_dev->dev, "VIDIOC_CROPCAP not implemented\n");

	return 0;
}

/**
 * mx6s_vidioc_g_crop() - 实现 VIDIOC_G_CROP 的占位回调
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @a: 用户传入/返回的裁剪参数。
 *
 * 当前驱动不实现实际裁剪，只校验 buffer type 后返回成功。
 *
 * Return: 成功返回 0；类型不匹配返回 -EINVAL。
 */
static int mx6s_vidioc_g_crop(struct file *file, void *priv,
			     struct v4l2_crop *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	dev_dbg(csi_dev->dev, "VIDIOC_G_CROP not implemented\n");

	return 0;
}

/**
 * mx6s_vidioc_s_crop() - 实现 VIDIOC_S_CROP 的占位回调
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @a: 用户请求的裁剪参数。
 *
 * 当前驱动不实现实际裁剪，只校验 buffer type 后返回成功。
 *
 * Return: 成功返回 0；类型不匹配返回 -EINVAL。
 */
static int mx6s_vidioc_s_crop(struct file *file, void *priv,
			     const struct v4l2_crop *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	dev_dbg(csi_dev->dev, "VIDIOC_S_CROP not implemented\n");

	return 0;
}

/**
 * mx6s_vidioc_g_parm() - 实现 VIDIOC_G_PARM 并转发给 sensor subdev
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @a: 返回当前流参数，如 timeperframe。
 *
 * Return: subdev video.g_parm 的返回值。
 */
static int mx6s_vidioc_g_parm(struct file *file, void *priv,
			     struct v4l2_streamparm *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, g_parm, a);
}

/**
 * mx6s_vidioc_s_parm() - 实现 VIDIOC_S_PARM 并转发给 sensor subdev
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @a: 用户请求的流参数，如帧率。
 *
 * Return: subdev video.s_parm 的返回值。
 */
static int mx6s_vidioc_s_parm(struct file *file, void *priv,
				struct v4l2_streamparm *a)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;

	return v4l2_subdev_call(sd, video, s_parm, a);
}

/**
 * mx6s_vidioc_enum_framesizes() - 实现 VIDIOC_ENUM_FRAMESIZES
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @fsize: 输入 pixel_format/index，返回支持的帧尺寸。
 *
 * 该回调把用户态 pixel_format 转成 mbus code，再调用 sensor pad ops
 * enum_frame_size 查询 OV5640 支持的分辨率。
 *
 * Return: 成功返回 0；格式不支持或 sensor 枚举失败返回负 errno。
 */
static int mx6s_vidioc_enum_framesizes(struct file *file, void *priv,
					 struct v4l2_frmsizeenum *fsize)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	struct mx6s_fmt *fmt;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmt = format_by_fourcc(fsize->pixel_format);
	if (!fmt || fmt->pixelformat != fsize->pixel_format)
		return -EINVAL;
	fse.code = fmt->mbus_code;

	ret = v4l2_subdev_call(sd, pad, enum_frame_size, NULL, &fse);
	if (ret)
		return ret;

	if (fse.min_width == fse.max_width &&
	    fse.min_height == fse.max_height) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = fse.min_width;
		fsize->discrete.height = fse.min_height;
		return 0;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->stepwise.min_width = fse.min_width;
	fsize->stepwise.max_width = fse.max_width;
	fsize->stepwise.min_height = fse.min_height;
	fsize->stepwise.max_height = fse.max_height;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

/**
 * mx6s_vidioc_enum_frameintervals() - 实现 VIDIOC_ENUM_FRAMEINTERVALS
 * @file: 打开的 /dev/videoX 文件实例。
 * @priv: V4L2 core 传入的 file handle。
 * @interval: 输入 pixel_format/width/height/index，返回离散帧间隔。
 *
 * Return: 成功返回 0；格式不支持或 sensor 枚举失败返回负 errno。
 */
static int mx6s_vidioc_enum_frameintervals(struct file *file, void *priv,
		struct v4l2_frmivalenum *interval)
{
	struct mx6s_csi_dev *csi_dev = video_drvdata(file);
	struct v4l2_subdev *sd = csi_dev->sd;
	struct mx6s_fmt *fmt;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = interval->index,
		.width = interval->width,
		.height = interval->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmt = format_by_fourcc(interval->pixel_format);
	if (!fmt || fmt->pixelformat != interval->pixel_format)
		return -EINVAL;
	fie.code = fmt->mbus_code;

	ret = v4l2_subdev_call(sd, pad, enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;
	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete = fie.interval;
	return 0;
}

/* V4L2 ioctl operation table，供 video_ioctl2() 按 VIDIOC_* 命令分发。 */
static const struct v4l2_ioctl_ops mx6s_csi_ioctl_ops = {
	.vidioc_querycap          = mx6s_vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = mx6s_vidioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = mx6s_vidioc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = mx6s_vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = mx6s_vidioc_s_fmt_vid_cap,
	.vidioc_cropcap       = mx6s_vidioc_cropcap,
	.vidioc_s_crop        = mx6s_vidioc_s_crop,
	.vidioc_g_crop        = mx6s_vidioc_g_crop,
	.vidioc_reqbufs       = mx6s_vidioc_reqbufs,
	.vidioc_create_bufs   = vb2_ioctl_create_bufs,
	.vidioc_querybuf      = mx6s_vidioc_querybuf,
	.vidioc_qbuf          = mx6s_vidioc_qbuf,
	.vidioc_dqbuf         = mx6s_vidioc_dqbuf,
	.vidioc_g_std         = mx6s_vidioc_g_std,
	.vidioc_s_std         = mx6s_vidioc_s_std,
	.vidioc_querystd      = mx6s_vidioc_querystd,
	.vidioc_enum_input    = mx6s_vidioc_enum_input,
	.vidioc_g_input       = mx6s_vidioc_g_input,
	.vidioc_s_input       = mx6s_vidioc_s_input,
	.vidioc_streamon      = mx6s_vidioc_streamon,
	.vidioc_streamoff     = mx6s_vidioc_streamoff,
	.vidioc_g_parm        = mx6s_vidioc_g_parm,
	.vidioc_s_parm        = mx6s_vidioc_s_parm,
	.vidioc_enum_framesizes = mx6s_vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = mx6s_vidioc_enum_frameintervals,
};

/**
 * subdev_notifier_bound() - V4L2 async core 找到匹配 sensor 后的绑定回调
 * @notifier: 本 CSI host 注册的 async notifier。
 * @subdev: 已完成 probe 并匹配 OF node 的 sensor subdev。
 * @asd: 触发本次匹配的 async subdev 描述。
 *
 * async core 根据 mx6sx_register_subdevs() 设置的 V4L2_ASYNC_MATCH_OF 规则，
 * 在 OV5640 subdev 注册时调用这里。驱动保存 subdev 指针，后续 open/ioctl
 * 就能通过 v4l2_subdev_call() 控制 sensor。
 *
 * Return: 成功返回 0；subdev 为空返回 -EINVAL。
 */
static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
			    struct v4l2_subdev *subdev,
			    struct v4l2_async_subdev *asd)
{
	struct mx6s_csi_dev *csi_dev = notifier_to_mx6s_dev(notifier);

	/* Find platform data for this sensor subdev */
	if (csi_dev->asd.match.of.node == subdev->dev->of_node)
		csi_dev->sd = subdev;

	if (subdev == NULL)
		return -EINVAL;

	v4l2_info(&csi_dev->v4l2_dev, "Registered sensor subdevice: %s\n",
		  subdev->name);

	return 0;
}

/**
 * mx6s_csi_mux_sel() - 根据设备树 mux 信息选择 CSI 输入路径。
 * @csi_dev: CSI 私有数据。其内嵌的 device 指针必须对应 CSI 设备树节点。
 *
 * 解析 CSI 设备节点中可选的 "csi-mux-mipi" 属性。该属性包含 3 个
 * cell：cell 0 是用于访问 IOMUXC GPR 寄存器块的 GPR syscon phandle，
 * cell 1 是传给 regmap_update_bits() 的 GPR 寄存器偏移，cell 2 是
 * 需要置位的 bit 编号，用于将输入路径切换到 MIPI CSI。
 *
 * 如果设备树中没有该属性，则保持默认 CSI 输入路径，并将 csi_mux_mipi
 * 标记为 false。
 *
 * Return: 成功返回 0，失败返回负 errno 值。
 */
static int mx6s_csi_mux_sel(struct mx6s_csi_dev *csi_dev)
{
	struct device_node *np = csi_dev->dev->of_node;
	struct device_node *node;
	phandle phandle;
	u32 out_val[3];
	int ret;

	/*
	 * of_property_read_u32_array() 从当前 CSI OF node 读取 3 个 u32 cell。
	 * 返回 0 表示属性存在且长度足够；返回负 errno 表示未配置或解析失败。
	 */
	ret = of_property_read_u32_array(np, "csi-mux-mipi", out_val, 3);
	if (ret) {
		dev_dbg(csi_dev->dev, "no csi-mux-mipi property found\n");
		csi_dev->csi_mux_mipi = false;
	} else {
		phandle = *out_val;

		/* of_find_node_by_phandle() 返回的 node 带引用，使用后要 of_node_put()。 */
		node = of_find_node_by_phandle(phandle);
		if (!node) {
			dev_dbg(csi_dev->dev, "not find gpr node by phandle\n");
			ret = PTR_ERR(node);
		}
		/* syscon_node_to_regmap() 把 syscon OF node 转成可读改写 GPR 的 regmap。 */
		csi_dev->csi_mux.gpr = syscon_node_to_regmap(node);
		if (IS_ERR(csi_dev->csi_mux.gpr)) {
			dev_err(csi_dev->dev, "failed to get gpr regmap\n");
			ret = PTR_ERR(csi_dev->csi_mux.gpr);
		}
		of_node_put(node);
		if (ret < 0)
			return ret;

		csi_dev->csi_mux.req_gpr = out_val[1];
		csi_dev->csi_mux.req_bit = out_val[2];

		/*
		 * regmap_update_bits() 做寄存器 read-modify-write：mask 指定要改
		 * 哪些位，val 指定这些位的新值。这里把 req_bit 置 1。
		 */
		regmap_update_bits(csi_dev->csi_mux.gpr, csi_dev->csi_mux.req_gpr,
			1 << csi_dev->csi_mux.req_bit, 1 << csi_dev->csi_mux.req_bit);

		csi_dev->csi_mux_mipi = true;
	}
	return ret;
}

/**
 * mx6sx_register_subdevs() - 注册设备树 OF graph 描述的 sensor subdev。
 * @csi_dev: CSI 私有数据。其内嵌的 device 指针提供要解析 port endpoint
 *           的 CSI 设备树节点。
 *
 * 遍历 CSI 设备树 graph，找到连接到 CSI endpoint 的远端 sensor 节点，
 * 并为它注册 V4L2 async notifier。notifier 使用 V4L2_ASYNC_MATCH_OF，
 * 表示通过比较 sensor 驱动的 dev->of_node 指针和 asd.match.of.node 来
 * 匹配远端 sensor。本板级配置中，预期的远端节点是 I2C 地址 0x3c 的
 * OV5640 sensor。
 *
 * Return: 成功返回 0，失败返回负 errno 值。
 */
static int mx6sx_register_subdevs(struct mx6s_csi_dev *csi_dev)
{
	/* parent 指向 CSI 设备树节点。 */
	struct device_node *parent = csi_dev->dev->of_node;
	struct device_node *node, *port, *rem;
	int ret;

	/* for_each_available_child_of_node() 只遍历 status 可用的子节点。 */
	for_each_available_child_of_node(parent, node) {
		/* of_node_cmp() 在节点名相等时返回 0。 */
		if (of_node_cmp(node->name, "port"))
			continue;

		/* The csi node can have only port subnode. */
		/* 获取 port 节点下的 endpoint 子节点。 */
		port = of_get_next_child(node, NULL);
		if (!port)
			continue;
		/*
		 * 沿 remote-endpoint 找到 sensor 节点。返回的节点已经增加引用计数，
		 * 使用完后必须通过 of_node_put() 释放。
		 */
		rem = of_graph_get_remote_port_parent(port);
		of_node_put(port);
		if (rem == NULL) {
			v4l2_info(&csi_dev->v4l2_dev,
						"Remote device at %s not found\n",
						port->full_name);
			return -1;
		}

		/*
		 * 等待远端 sensor subdev。V4L2_ASYNC_MATCH_OF 表示 async core
		 * 会比较 dev->of_node 和 match.of.node。
		 */
		csi_dev->asd.match_type = V4L2_ASYNC_MATCH_OF;
		csi_dev->asd.match.of.node = rem;
		csi_dev->async_subdevs[0] = &csi_dev->asd;

		of_node_put(rem);
		break;
	}

	/*
	 * 注册一个期望绑定的 subdev。当 OV5640 subdev probe 完成，且 OF node
	 * 匹配成功时，bound 回调会被调用。
	 */
	csi_dev->subdev_notifier.subdevs = csi_dev->async_subdevs;
	csi_dev->subdev_notifier.num_subdevs = 1;
	csi_dev->subdev_notifier.bound = subdev_notifier_bound;

	/*
	 * v4l2_async_notifier_register() 把等待列表交给 V4L2 async core。
	 * 如果 OV5640 subdev 已经注册，这里会立即完成绑定；否则 async core
	 * 会保存匹配规则，直到 sensor probe 时再尝试匹配。
	 */
	ret = v4l2_async_notifier_register(&csi_dev->v4l2_dev,
					&csi_dev->subdev_notifier);
	if (ret)
		dev_err(csi_dev->dev,
					"Error register async notifier regoster\n");

	return ret;
}

/**
 * mx6s_csi_probe() - CSI platform device 探测函数
 * @pdev: platform bus 根据设备树 compatible 创建并匹配到的设备。
 *
 * probe 完成资源获取、MMIO 映射、时钟获取、V4L2 device/video_device 注册、
 * IRQ 注册、async subdev notifier 注册和 runtime PM 启用。
 *
 * Return: 成功返回 0，失败返回负 errno 值。
 */
static int mx6s_csi_probe(struct platform_device *pdev)
{
	/*
	 * probe 前，DT 中的 reg/interrupts 已经被转换为 platform resource，
	 * 因此驱动可以直接从 platform_device 中获取这些资源。
	 */
	struct device *dev = &pdev->dev;
	struct mx6s_csi_dev *csi_dev;
	struct video_device *vdev;
	struct resource *res;
	int ret = 0;

	dev_dbg(dev, "initialising\n");

	/*
	 * devm_kzalloc() 是 device-managed 分配接口，返回清零后的内存；设备
	 * detach 或 probe 失败后的 devres 释放路径会自动释放它。
	 */
	csi_dev = devm_kzalloc(dev, sizeof(struct mx6s_csi_dev), GFP_ATOMIC);
	if (!csi_dev) {
		dev_err(dev, "Can't allocate private structure\n");
		return -ENODEV;
	}

	/*
	 * 资源索引 0 是 DT reg 属性描述的 CSI 寄存器窗口。
	 * IORESOURCE_MEM 表示内存映射 I/O，不是普通 RAM。
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* IRQ 索引 0 是 DT interrupts 属性描述的 CSI 中断。 */
	csi_dev->irq = platform_get_irq(pdev, 0);
	/* 缺少寄存器或 IRQ 资源，说明这个 platform device 描述不完整。 */
	if (res == NULL || csi_dev->irq < 0) {
		dev_err(dev, "Missing platform resources data\n");
		return -ENODEV;
	}

	/*
	 * devm_ioremap_resource() 同时完成 request_mem_region() 和 ioremap()，
	 * 返回可用 __iomem 指针或 ERR_PTR。
	 */
	csi_dev->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(csi_dev->regbase)) {
		dev_err(dev, "Failed platform resources map\n");
		return -ENODEV;
	}

	/* 等待 CSI DMA 填充的 buffer 队列。 */
	INIT_LIST_HEAD(&csi_dev->capture);
	/* 当前已经交给 CSI DMA 使用的 buffer 队列。 */
	INIT_LIST_HEAD(&csi_dev->active_bufs);
	/* 丢帧时使用的临时 buffer 队列。 */
	INIT_LIST_HEAD(&csi_dev->discard);

	/* devm_clk_get() 按 clock-names 查找 clock provider，并返回托管 clk。 */
	csi_dev->clk_disp_axi = devm_clk_get(dev, "disp-axi");
	if (IS_ERR(csi_dev->clk_disp_axi)) {
		dev_err(dev, "Could not get csi axi clock\n");
		return -ENODEV;
	}

	csi_dev->clk_disp_dcic = devm_clk_get(dev, "disp_dcic");
	if (IS_ERR(csi_dev->clk_disp_dcic)) {
		dev_err(dev, "Could not get disp dcic clock\n");
		return -ENODEV;
	}

	csi_dev->clk_csi_mclk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(csi_dev->clk_csi_mclk)) {
		dev_err(dev, "Could not get csi mclk clock\n");
		return -ENODEV;
	}

	csi_dev->dev = dev;

	/* 如果存在可选的 csi-mux-mipi 属性，则选择 MIPI CSI 输入路径。 */
	mx6s_csi_mux_sel(csi_dev);

	snprintf(csi_dev->v4l2_dev.name,
		 sizeof(csi_dev->v4l2_dev.name), "CSI");

	/*
	 * 将 V4L2 device 包装对象绑定到真实 struct device，初始化 V4L2
	 * 内部状态，并持有 dev 引用。
	 */
	ret = v4l2_device_register(dev, &csi_dev->v4l2_dev);
	if (ret < 0) {
		dev_err(dev, "v4l2_device_register() failed: %d\n", ret);
		return -ENODEV;
	}

	/* initialize locks */
	mutex_init(&csi_dev->lock);
	spin_lock_init(&csi_dev->slock);

	/* video_device_alloc() 分配面向 /dev/videoX 的 V4L2 字符设备对象。 */
	vdev = video_device_alloc();
	if (vdev == NULL) {
		ret = -ENOMEM;
		goto err_vdev;
	}
	/* 配置 V4L2 file operations 和 ioctl operations。 */
	snprintf(vdev->name, sizeof(vdev->name), "mx6s-csi");

	vdev->v4l2_dev		= &csi_dev->v4l2_dev;
	vdev->fops			= &mx6s_csi_fops;
	vdev->ioctl_ops		= &mx6s_csi_ioctl_ops;
	/* video_device_release() 是 video_device_alloc() 对应的默认释放函数。 */
	vdev->release		= video_device_release;
	vdev->lock			= &csi_dev->lock;

	vdev->queue = &csi_dev->vb2_vidq;

	csi_dev->vdev = vdev;

	mx6s_init_default_format(csi_dev);

	/* 让 file operations 可以通过 video_drvdata(file) 取回 csi_dev。 */
	video_set_drvdata(csi_dev->vdev, csi_dev);
	mutex_lock(&csi_dev->lock);

	/*
	 * video_register_device() 创建 /dev/videoX 并注册到 V4L2 core；
	 * VFL_TYPE_GRABBER 表示采集设备，第三个参数 -1 表示自动分配 minor。
	 */
	ret = video_register_device(csi_dev->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		video_device_release(csi_dev->vdev);
		mutex_unlock(&csi_dev->lock);
		goto err_vdev;
	}

	/*
	 * 注册 CSI IRQ 处理函数。irqflags 为 0 表示使用 firmware/irqchip
	 * 已描述的触发类型；dev_id 会传给中断处理函数。
	 */
	if (devm_request_irq(dev, csi_dev->irq, mx6s_csi_irq_handler,
				0, "csi", (void *)csi_dev)) {
		mutex_unlock(&csi_dev->lock);
		dev_err(dev, "Request CSI IRQ failed.\n");
		ret = -ENODEV;
		goto err_irq;
	}

	mutex_unlock(&csi_dev->lock);

	ret = mx6sx_register_subdevs(csi_dev);
	if (ret < 0)
		goto err_irq;

	/*
	 * 启用 runtime PM。之后 open/close 路径可以通过 pm_runtime_get_sync()
	 * 和 pm_runtime_put_sync_suspend() 平衡设备电源状态。
	 */
	pm_runtime_enable(csi_dev->dev);
	return 0;

err_irq:
	video_unregister_device(csi_dev->vdev);
err_vdev:
	v4l2_device_unregister(&csi_dev->v4l2_dev);
	return ret;
}

/**
 * mx6s_csi_remove() - CSI platform device 移除函数
 * @pdev: 即将解绑的 platform device。
 *
 * 注销顺序与 probe 相反：先注销 async notifier，再注销 video node 和
 * v4l2_device，最后关闭 runtime PM。
 *
 * Return: 成功返回 0。
 */
static int mx6s_csi_remove(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct mx6s_csi_dev *csi_dev =
				container_of(v4l2_dev, struct mx6s_csi_dev, v4l2_dev);

	/* v4l2_async_notifier_unregister() 断开与 sensor subdev 的异步绑定关系。 */
	v4l2_async_notifier_unregister(&csi_dev->subdev_notifier);

	video_unregister_device(csi_dev->vdev);
	v4l2_device_unregister(&csi_dev->v4l2_dev);

	pm_runtime_disable(csi_dev->dev);
	return 0;
}

/**
 * mx6s_csi_runtime_suspend() - runtime PM suspend 回调
 * @dev: CSI 设备。
 *
 * 当前外置学习驱动只记录调试信息，实际时钟关闭在 close 路径完成。
 *
 * Return: 成功返回 0。
 */
static int mx6s_csi_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "csi v4l2 busfreq high release.\n");
	return 0;
}

/**
 * mx6s_csi_runtime_resume() - runtime PM resume 回调
 * @dev: CSI 设备。
 *
 * 当前外置学习驱动只记录调试信息，实际时钟打开在 open 路径完成。
 *
 * Return: 成功返回 0。
 */
static int mx6s_csi_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "csi v4l2 busfreq high request.\n");
	return 0;
}

/* SET_RUNTIME_PM_OPS() 把 runtime suspend/resume 回调填入 dev_pm_ops。 */
static const struct dev_pm_ops mx6s_csi_pm_ops = {
	SET_RUNTIME_PM_OPS(mx6s_csi_runtime_suspend, mx6s_csi_runtime_resume, NULL)
};

/* of_device_id 表用于 platform bus 按 compatible 匹配设备树节点。 */
static const struct of_device_id mx6s_csi_dt_ids[] = {
	{ .compatible = "fsl,imx6s-csi", },
	{ /* sentinel */ }
};
/* MODULE_DEVICE_TABLE() 导出 OF modalias，便于模块自动加载。 */
MODULE_DEVICE_TABLE(of, mx6s_csi_dt_ids);

/* platform_driver 把 probe/remove 与 OF match table 交给 platform bus。 */
static struct platform_driver mx6s_csi_driver = {
	.driver		= {
		.name	= MX6S_CAM_DRV_NAME,
		.of_match_table = of_match_ptr(mx6s_csi_dt_ids),
		.pm = &mx6s_csi_pm_ops,
	},
	.probe	= mx6s_csi_probe,
	.remove	= mx6s_csi_remove,
};

/* module_platform_driver() 生成模块 init/exit，注册/注销 platform_driver。 */
module_platform_driver(mx6s_csi_driver);

MODULE_DESCRIPTION("i.MX6Sx SoC Camera Host driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
MODULE_VERSION(MX6S_CAM_VERSION);
