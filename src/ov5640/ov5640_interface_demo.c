// SPDX-License-Identifier: GPL-2.0-only
/*
 * ov5640_interface_demo.c - enumerate and exercise the userspace API exposed
 * by this repository's mx6s_capture.c + ov5640.c driver pair.
 *
 * Build on target or with the project cross toolchain, for example:
 *   arm-linux-gnueabihf-gcc -Wall -Wextra -O2 ov5640_interface_demo.c \
 *       -o ov5640_interface_demo
 *
 * Typical runs:
 *   ./ov5640_interface_demo /dev/video1 --list
 *   ./ov5640_interface_demo /dev/video1 --configure --width 800 --height 480 --fps 30
 *   ./ov5640_interface_demo /dev/video1 --mmap --count 30
 *   ./ov5640_interface_demo /dev/video1 --read --count 1
 *   ./ov5640_interface_demo /dev/video1 --hflip 1 --vflip 1 --power-line 50
 *
 * Interface categories and driver callback paths
 * ==============================================
 *
 * 1. Device lifetime and file operations
 *
 *   open("/dev/videoX", O_RDWR)
 *     -> mx6s_csi_fops.open
 *     -> mx6s_csi_open()
 *     -> vb2_dma_contig_init_ctx(), vb2_queue_init()
 *     -> pm_runtime_get_sync(), request_bus_freq(BUS_FREQ_HIGH)
 *     -> v4l2_subdev_call(sd, core, s_power, 1)
 *     -> ov5640_subdev_core_ops.s_power
 *     -> ov5640_s_power()
 *
 *   close(fd)
 *     -> mx6s_csi_fops.release
 *     -> mx6s_csi_close()
 *     -> vb2_queue_release(), mx6s_csi_deinit()
 *     -> v4l2_subdev_call(sd, core, s_power, 0)
 *     -> ov5640_s_power()
 *
 *   read(fd, ...)
 *     -> mx6s_csi_fops.read
 *     -> mx6s_csi_read()
 *     -> vb2_read()
 *     -> mx6s_videobuf_ops queue_setup/buf_prepare/buf_queue/start_streaming
 *     -> CSI IRQ -> mx6s_csi_irq_handler()
 *     -> mx6s_csi_frame_done()
 *     -> vb2_buffer_done()
 *
 *   poll(fd, ...)
 *     -> mx6s_csi_fops.poll
 *     -> vb2_fop_poll()
 *
 *   mmap(fd, offset from VIDIOC_QUERYBUF)
 *     -> mx6s_csi_fops.mmap
 *     -> mx6s_csi_mmap()
 *     -> vb2_mmap()
 *
 *   ioctl(fd, VIDIOC_*)
 *     -> mx6s_csi_fops.unlocked_ioctl
 *     -> video_ioctl2()
 *     -> mx6s_csi_ioctl_ops.<vidioc callback>
 *
 * 2. Capability, input, and topology discovery
 *
 *   VIDIOC_QUERYCAP
 *     -> mx6s_vidioc_querycap()
 *
 *   VIDIOC_ENUMINPUT / VIDIOC_G_INPUT / VIDIOC_S_INPUT
 *     -> mx6s_vidioc_enum_input()
 *     -> mx6s_vidioc_g_input()
 *     -> mx6s_vidioc_s_input()
 *     The host exposes one camera input: index 0.
 *
 * 3. Format, frame size, and frame interval discovery
 *
 *   VIDIOC_ENUM_FMT
 *     -> mx6s_vidioc_enum_fmt_vid_cap()
 *     -> v4l2_subdev_call(sd, video, enum_mbus_fmt, index, &code)
 *     -> ov5640_subdev_video_ops.enum_mbus_fmt
 *     -> ov5640_enum_fmt()
 *     -> host maps MEDIA_BUS_FMT_RGB565_2X8_LE to V4L2_PIX_FMT_RGB565.
 *
 *   VIDIOC_ENUM_FRAMESIZES
 *     -> mx6s_vidioc_enum_framesizes()
 *     -> format_by_fourcc()
 *     -> v4l2_subdev_call(sd, pad, enum_frame_size, NULL, &fse)
 *     -> ov5640_subdev_pad_ops.enum_frame_size
 *     -> ov5640_enum_framesizes()
 *
 *   VIDIOC_ENUM_FRAMEINTERVALS
 *     -> mx6s_vidioc_enum_frameintervals()
 *     -> format_by_fourcc()
 *     -> v4l2_subdev_call(sd, pad, enum_frame_interval, NULL, &fie)
 *     -> ov5640_subdev_pad_ops.enum_frame_interval
 *     -> ov5640_enum_frameintervals()
 *
 * 4. Format and stream-parameter configuration
 *
 *   VIDIOC_TRY_FMT
 *     -> mx6s_vidioc_try_fmt_vid_cap()
 *     -> mx6s_negotiate_format(apply=false)
 *     -> v4l2_subdev_call(sd, video, try_mbus_fmt, &mbus_fmt)
 *     -> ov5640_try_fmt()
 *     TRY_FMT only normalizes the request; it does not write sensor registers.
 *
 *   VIDIOC_S_FMT
 *     -> mx6s_vidioc_s_fmt_vid_cap()
 *     -> mx6s_negotiate_format(apply=true)
 *     -> v4l2_subdev_call(sd, video, s_mbus_fmt, &mbus_fmt)
 *     -> ov5640_s_fmt()
 *     -> ov5640_change_mode(), ov5640_apply_format(), ov5640_apply_controls()
 *     -> mx6s_configure_csi()
 *
 *   VIDIOC_G_FMT
 *     -> mx6s_vidioc_g_fmt_vid_cap()
 *
 *   VIDIOC_G_PARM / VIDIOC_S_PARM
 *     -> mx6s_vidioc_g_parm() / mx6s_vidioc_s_parm()
 *     -> v4l2_subdev_call(sd, video, g_parm/s_parm, ...)
 *     -> ov5640_g_parm() / ov5640_s_parm()
 *     The OV5640 path supports discrete 15 fps and 30 fps combinations.
 *
 * 5. V4L2 controls aggregated from the OV5640 subdev
 *
 *   VIDIOC_QUERYCTRL / VIDIOC_QUERYMENU / VIDIOC_G_CTRL / VIDIOC_S_CTRL
 *     -> video_ioctl2()
 *     -> V4L2 control core using the video_device/v4l2_device ctrl_handler
 *     -> OV5640 subdev ctrl_handler created by ov5640_init_controls()
 *     -> ov5640_ctrl_ops.s_ctrl
 *     -> ov5640_s_ctrl()
 *     -> ov5640_set_flip() or ov5640_set_power_line_frequency()
 *
 *   Controls present in ov5640.c:
 *     V4L2_CID_HFLIP, V4L2_CID_VFLIP, V4L2_CID_POWER_LINE_FREQUENCY.
 *
 * 6. Streaming buffer API
 *
 *   VIDIOC_REQBUFS
 *     -> mx6s_vidioc_reqbufs()
 *     -> vb2_reqbufs()
 *     -> mx6s_videobuf_setup()
 *
 *   VIDIOC_CREATE_BUFS
 *     -> vb2_ioctl_create_bufs()
 *     -> mx6s_videobuf_setup()
 *
 *   VIDIOC_QUERYBUF
 *     -> mx6s_vidioc_querybuf()
 *     -> vb2_querybuf()
 *
 *   VIDIOC_QBUF
 *     -> mx6s_vidioc_qbuf()
 *     -> vb2_qbuf()
 *     -> mx6s_videobuf_prepare()
 *     -> mx6s_videobuf_queue()
 *
 *   VIDIOC_STREAMON
 *     -> mx6s_vidioc_streamon()
 *     -> v4l2_subdev_call(sd, video, s_stream, 1)
 *     -> ov5640_s_stream()
 *     -> vb2_streamon()
 *     -> mx6s_start_streaming()
 *     -> mx6s_csi_enable()
 *
 *   VIDIOC_DQBUF
 *     -> mx6s_vidioc_dqbuf()
 *     -> vb2_dqbuf()
 *     Waits for CSI IRQ path to call vb2_buffer_done().
 *
 *   VIDIOC_STREAMOFF
 *     -> mx6s_vidioc_streamoff()
 *     -> vb2_streamoff()
 *     -> mx6s_stop_streaming()
 *     -> v4l2_subdev_call(sd, video, s_stream, 0)
 *     -> ov5640_s_stream()
 *
 * 7. Crop and old analog-TV standard API
 *
 *   VIDIOC_CROPCAP / VIDIOC_G_CROP / VIDIOC_S_CROP
 *     -> mx6s_vidioc_cropcap()/g_crop()/s_crop()
 *     These are placeholders in mx6s_capture.c: they validate the buffer type
 *     but do not program an OV5640 crop rectangle.
 *
 *   VIDIOC_G_STD / VIDIOC_S_STD / VIDIOC_QUERYSTD
 *     -> mx6s_vidioc_g_std()/s_std()/querystd()
 *     -> v4l2_subdev_call(sd, video, g_std/s_std/querystd, ...)
 *     ov5640.c does not install those subdev video callbacks, so with this
 *     sensor they should be treated as legacy host hooks that normally return
 *     "not supported".
 *
 * 8. Conditional debug-register API
 *
 *   ov5640.c implements subdev core .g_register/.s_register only under
 *   CONFIG_VIDEO_ADV_DEBUG. mx6s_capture.c does not expose matching video-node
 *   ioctl callbacks, and ov5640.c does not create a userspace subdev node here,
 *   so this demo intentionally does not access raw OV5640 registers.
 *
 * Classic usage combinations
 * ==========================
 *
 *   Discovery:
 *     open -> VIDIOC_QUERYCAP -> VIDIOC_ENUMINPUT/G_INPUT
 *          -> VIDIOC_ENUM_FMT -> VIDIOC_ENUM_FRAMESIZES
 *          -> VIDIOC_ENUM_FRAMEINTERVALS -> VIDIOC_QUERYCTRL/G_CTRL -> close
 *
 *   Configure before streaming:
 *     open -> VIDIOC_S_INPUT(0) -> VIDIOC_TRY_FMT -> VIDIOC_S_FMT
 *          -> VIDIOC_G_PARM -> VIDIOC_S_PARM -> optional VIDIOC_S_CTRL
 *
 *   Classic MMAP capture:
 *     configure -> VIDIOC_REQBUFS(MMAP) -> VIDIOC_QUERYBUF -> mmap
 *               -> VIDIOC_QBUF all buffers -> VIDIOC_STREAMON
 *               -> poll -> VIDIOC_DQBUF -> consume -> VIDIOC_QBUF
 *               -> VIDIOC_STREAMOFF -> munmap -> VIDIOC_REQBUFS(count=0)
 *
 *   Simpler read capture:
 *     configure -> read(fd, frame, sizeimage)
 *     The file operation exists even though mx6s_vidioc_querycap() advertises
 *     V4L2_CAP_STREAMING and not V4L2_CAP_READWRITE.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef V4L2_CID_POWER_LINE_FREQUENCY_DISABLED
#define V4L2_CID_POWER_LINE_FREQUENCY_DISABLED 0
#endif

#ifndef V4L2_CID_POWER_LINE_FREQUENCY_50HZ
#define V4L2_CID_POWER_LINE_FREQUENCY_50HZ 1
#endif

#ifndef V4L2_CID_POWER_LINE_FREQUENCY_60HZ
#define V4L2_CID_POWER_LINE_FREQUENCY_60HZ 2
#endif

#ifndef V4L2_CID_POWER_LINE_FREQUENCY_AUTO
#define V4L2_CID_POWER_LINE_FREQUENCY_AUTO 3
#endif

struct demo_options {
	const char *device;
	unsigned int width;
	unsigned int height;
	unsigned int fps;
	unsigned int count;
	unsigned int buffers;
	bool list;
	bool configure;
	bool mmap_capture;
	bool read_capture;
	bool userptr_capture;
	bool show_std_crop;
	bool create_bufs;
	int hflip;
	int vflip;
	int power_line_frequency;
};

struct mmap_buffer {
	void *start;
	size_t length;
};

static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

static const char *fourcc_to_str(uint32_t fourcc, char out[5])
{
	out[0] = fourcc & 0xff;
	out[1] = (fourcc >> 8) & 0xff;
	out[2] = (fourcc >> 16) & 0xff;
	out[3] = (fourcc >> 24) & 0xff;
	out[4] = '\0';
	return out;
}

static const char *field_name(uint32_t field)
{
	switch (field) {
	case V4L2_FIELD_ANY:
		return "ANY";
	case V4L2_FIELD_NONE:
		return "NONE";
	case V4L2_FIELD_TOP:
		return "TOP";
	case V4L2_FIELD_BOTTOM:
		return "BOTTOM";
	case V4L2_FIELD_INTERLACED:
		return "INTERLACED";
	default:
		return "other";
	}
}

static const char *power_line_name(int value)
{
	switch (value) {
	case V4L2_CID_POWER_LINE_FREQUENCY_DISABLED:
		return "disabled";
	case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
		return "50Hz";
	case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
		return "60Hz";
	case V4L2_CID_POWER_LINE_FREQUENCY_AUTO:
		return "auto";
	default:
		return "unknown";
	}
}

static void print_errno_note(const char *name)
{
	printf("  %-28s -> %s (%d)\n", name, strerror(errno), errno);
}

static void print_pix_format(const char *prefix, const struct v4l2_pix_format *pix)
{
	char fourcc[5];

	printf("%s%ux%u %s bytesperline=%u sizeimage=%u field=%s colorspace=%u\n",
	       prefix, pix->width, pix->height,
	       fourcc_to_str(pix->pixelformat, fourcc),
	       pix->bytesperline, pix->sizeimage,
	       field_name(pix->field), pix->colorspace);
}

static void usage(const char *prog)
{
	printf("Usage: %s [device] [options]\n", prog);
	printf("\n");
	printf("Options:\n");
	printf("  --list                 enumerate capabilities, formats, controls (default)\n");
	printf("  --configure            run S_INPUT + TRY_FMT + S_FMT + G/S_PARM\n");
	printf("  --mmap                 run classic MMAP streaming capture\n");
	printf("  --read                 run read() capture path\n");
	printf("  --userptr              run USERPTR streaming capture path\n");
	printf("  --create-bufs          demonstrate VIDIOC_CREATE_BUFS, then release buffers\n");
	printf("  --std-crop             call crop placeholders and old std forwarding hooks\n");
	printf("  --width N              requested width, default 800\n");
	printf("  --height N             requested height, default 480\n");
	printf("  --fps N                requested fps, default 30\n");
	printf("  --count N              frame count for capture demos, default 5\n");
	printf("  --buffers N            buffer count for streaming demos, default 4\n");
	printf("  --hflip 0|1            set V4L2_CID_HFLIP\n");
	printf("  --vflip 0|1            set V4L2_CID_VFLIP\n");
	printf("  --power-line auto|50|60 set V4L2_CID_POWER_LINE_FREQUENCY\n");
	printf("  --help                 show this help\n");
}

static int parse_u32(const char *text, unsigned int *value)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || !end || *end != '\0' || parsed > UINT32_MAX)
		return -1;

	*value = (unsigned int)parsed;
	return 0;
}

static int parse_bool_arg(const char *text, int *value)
{
	if (!strcmp(text, "0") || !strcmp(text, "false") || !strcmp(text, "off")) {
		*value = 0;
		return 0;
	}
	if (!strcmp(text, "1") || !strcmp(text, "true") || !strcmp(text, "on")) {
		*value = 1;
		return 0;
	}

	return -1;
}

static int parse_power_line_arg(const char *text, int *value)
{
	if (!strcmp(text, "auto")) {
		*value = V4L2_CID_POWER_LINE_FREQUENCY_AUTO;
		return 0;
	}
	if (!strcmp(text, "50")) {
		*value = V4L2_CID_POWER_LINE_FREQUENCY_50HZ;
		return 0;
	}
	if (!strcmp(text, "60")) {
		*value = V4L2_CID_POWER_LINE_FREQUENCY_60HZ;
		return 0;
	}

	return -1;
}

static int parse_args(int argc, char **argv, struct demo_options *opt)
{
	int i;

	opt->device = "/dev/video1";
	opt->width = 800;
	opt->height = 480;
	opt->fps = 30;
	opt->count = 5;
	opt->buffers = 4;
	opt->list = true;
	opt->hflip = -1;
	opt->vflip = -1;
	opt->power_line_frequency = -1;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--help")) {
			usage(argv[0]);
			exit(EXIT_SUCCESS);
		} else if (!strcmp(argv[i], "--list")) {
			opt->list = true;
		} else if (!strcmp(argv[i], "--configure")) {
			opt->configure = true;
		} else if (!strcmp(argv[i], "--mmap")) {
			opt->mmap_capture = true;
			opt->configure = true;
		} else if (!strcmp(argv[i], "--read")) {
			opt->read_capture = true;
			opt->configure = true;
		} else if (!strcmp(argv[i], "--userptr")) {
			opt->userptr_capture = true;
			opt->configure = true;
		} else if (!strcmp(argv[i], "--create-bufs")) {
			opt->create_bufs = true;
			opt->configure = true;
		} else if (!strcmp(argv[i], "--std-crop")) {
			opt->show_std_crop = true;
		} else if (!strcmp(argv[i], "--width") && i + 1 < argc) {
			if (parse_u32(argv[++i], &opt->width))
				return -1;
		} else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
			if (parse_u32(argv[++i], &opt->height))
				return -1;
		} else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
			if (parse_u32(argv[++i], &opt->fps))
				return -1;
		} else if (!strcmp(argv[i], "--count") && i + 1 < argc) {
			if (parse_u32(argv[++i], &opt->count))
				return -1;
		} else if (!strcmp(argv[i], "--buffers") && i + 1 < argc) {
			if (parse_u32(argv[++i], &opt->buffers))
				return -1;
		} else if (!strcmp(argv[i], "--hflip") && i + 1 < argc) {
			if (parse_bool_arg(argv[++i], &opt->hflip))
				return -1;
		} else if (!strcmp(argv[i], "--vflip") && i + 1 < argc) {
			if (parse_bool_arg(argv[++i], &opt->vflip))
				return -1;
		} else if (!strcmp(argv[i], "--power-line") && i + 1 < argc) {
			if (parse_power_line_arg(argv[++i], &opt->power_line_frequency))
				return -1;
		} else if (argv[i][0] != '-') {
			opt->device = argv[i];
		} else {
			return -1;
		}
	}

	if (opt->buffers < 2)
		opt->buffers = 2;
	if (opt->count == 0)
		opt->count = 1;

	return 0;
}

static void show_querycap(int fd)
{
	struct v4l2_capability cap;

	memset(&cap, 0, sizeof(cap));
	printf("\n[capability]\n");
	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
		print_errno_note("VIDIOC_QUERYCAP");
		return;
	}

	printf("  driver=%s card=%s bus=%s\n", cap.driver, cap.card, cap.bus_info);
	printf("  capabilities=0x%08x device_caps=0x%08x\n",
	       cap.capabilities, cap.device_caps);
	printf("  expected from mx6s_vidioc_querycap: VIDEO_CAPTURE + STREAMING\n");
}

static void show_inputs(int fd)
{
	struct v4l2_input input;
	unsigned int current = 0;

	printf("\n[input]\n");
	memset(&input, 0, sizeof(input));
	input.index = 0;
	if (xioctl(fd, VIDIOC_ENUMINPUT, &input) == 0) {
		printf("  VIDIOC_ENUMINPUT[0] name=%s type=%u\n",
		       input.name, input.type);
	} else {
		print_errno_note("VIDIOC_ENUMINPUT[0]");
	}

	if (xioctl(fd, VIDIOC_G_INPUT, &current) == 0) {
		printf("  VIDIOC_G_INPUT current=%u\n", current);
	} else {
		print_errno_note("VIDIOC_G_INPUT");
	}

	current = 0;
	if (xioctl(fd, VIDIOC_S_INPUT, &current) == 0) {
		printf("  VIDIOC_S_INPUT 0 ok\n");
	} else {
		print_errno_note("VIDIOC_S_INPUT");
	}
}

static void show_frame_intervals(int fd, uint32_t pixfmt,
				 unsigned int width, unsigned int height)
{
	struct v4l2_frmivalenum ival;
	char fourcc[5];

	for (memset(&ival, 0, sizeof(ival)), ival.pixel_format = pixfmt,
	     ival.width = width, ival.height = height;
	     xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) == 0;
	     ival.index++) {
		if (ival.type == V4L2_FRMIVAL_TYPE_DISCRETE &&
		    ival.discrete.numerator && ival.discrete.denominator) {
			printf("      interval[%u] %u/%u sec (%u fps)\n",
			       ival.index, ival.discrete.numerator,
			       ival.discrete.denominator,
			       ival.discrete.denominator / ival.discrete.numerator);
		} else {
			printf("      interval[%u] type=%u\n", ival.index, ival.type);
		}
	}

	if (ival.index == 0 && errno != EINVAL) {
		printf("      interval enum for %s %ux%u ended with %s\n",
		       fourcc_to_str(pixfmt, fourcc), width, height, strerror(errno));
	}
}

static void show_frame_sizes(int fd, uint32_t pixfmt)
{
	struct v4l2_frmsizeenum size;

	memset(&size, 0, sizeof(size));
	size.pixel_format = pixfmt;

	while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0) {
		if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
			printf("    size[%u] %ux%u\n", size.index,
			       size.discrete.width, size.discrete.height);
			show_frame_intervals(fd, pixfmt,
					     size.discrete.width, size.discrete.height);
		} else {
			printf("    size[%u] type=%u min=%ux%u max=%ux%u step=%ux%u\n",
			       size.index, size.type,
			       size.stepwise.min_width, size.stepwise.min_height,
			       size.stepwise.max_width, size.stepwise.max_height,
			       size.stepwise.step_width, size.stepwise.step_height);
		}
		size.index++;
	}
}

static void show_formats(int fd)
{
	struct v4l2_fmtdesc fmt;
	char fourcc[5];

	printf("\n[formats, frame sizes, frame intervals]\n");
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
		printf("  fmt[%u] %s desc=%s flags=0x%x\n",
		       fmt.index, fourcc_to_str(fmt.pixelformat, fourcc),
		       fmt.description, fmt.flags);
		show_frame_sizes(fd, fmt.pixelformat);
		fmt.index++;
	}

	if (fmt.index == 0)
		print_errno_note("VIDIOC_ENUM_FMT[0]");
}

static int get_format(int fd, struct v4l2_format *fmt)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (xioctl(fd, VIDIOC_G_FMT, fmt) == -1) {
		print_errno_note("VIDIOC_G_FMT");
		return -1;
	}

	return 0;
}

static int configure_format_and_fps(int fd, const struct demo_options *opt,
				    struct v4l2_format *active_fmt)
{
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	unsigned int input = 0;

	printf("\n[configure]\n");

	if (xioctl(fd, VIDIOC_S_INPUT, &input) == -1) {
		print_errno_note("VIDIOC_S_INPUT");
		return -1;
	}
	printf("  selected input 0\n");

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = opt->width;
	fmt.fmt.pix.height = opt->height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	/*
	 * VIDIOC_TRY_FMT path:
	 * video_ioctl2 -> mx6s_vidioc_try_fmt_vid_cap
	 * -> mx6s_negotiate_format(apply=false)
	 * -> ov5640_try_fmt.
	 */
	if (xioctl(fd, VIDIOC_TRY_FMT, &fmt) == -1) {
		print_errno_note("VIDIOC_TRY_FMT");
		return -1;
	}
	print_pix_format("  TRY_FMT normalized: ", &fmt.fmt.pix);

	/*
	 * VIDIOC_S_FMT path:
	 * video_ioctl2 -> mx6s_vidioc_s_fmt_vid_cap
	 * -> ov5640_s_fmt -> mx6s_configure_csi.
	 * This must be done before buffers are requested and before streaming.
	 */
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
		print_errno_note("VIDIOC_S_FMT");
		return -1;
	}
	print_pix_format("  S_FMT active:       ", &fmt.fmt.pix);

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_G_PARM, &parm) == -1) {
		print_errno_note("VIDIOC_G_PARM");
	} else {
		printf("  G_PARM timeperframe=%u/%u capturemode=0x%x capability=0x%x\n",
		       parm.parm.capture.timeperframe.numerator,
		       parm.parm.capture.timeperframe.denominator,
		       parm.parm.capture.capturemode,
		       parm.parm.capture.capability);
	}

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = opt->fps;
	parm.parm.capture.capturemode = V4L2_CAP_TIMEPERFRAME;

	/*
	 * VIDIOC_S_PARM path:
	 * video_ioctl2 -> mx6s_vidioc_s_parm -> ov5640_s_parm.
	 * ov5640_s_parm accepts 15/30 fps and rejects unsupported size/fps pairs.
	 */
	if (xioctl(fd, VIDIOC_S_PARM, &parm) == -1) {
		print_errno_note("VIDIOC_S_PARM");
		return -1;
	}
	printf("  S_PARM active timeperframe=%u/%u (%u fps)\n",
	       parm.parm.capture.timeperframe.numerator,
	       parm.parm.capture.timeperframe.denominator,
	       parm.parm.capture.timeperframe.denominator /
	       parm.parm.capture.timeperframe.numerator);

	if (get_format(fd, active_fmt) == -1)
		return -1;
	print_pix_format("  G_FMT confirmed:    ", &active_fmt->fmt.pix);

	return 0;
}

static bool is_menu_control(uint32_t type)
{
	if (type == V4L2_CTRL_TYPE_MENU)
		return true;
#ifdef V4L2_CTRL_TYPE_INTEGER_MENU
	if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
		return true;
#endif
	return false;
}

static void show_one_control(int fd, uint32_t id)
{
	struct v4l2_queryctrl query;
	struct v4l2_control ctrl;

	memset(&query, 0, sizeof(query));
	query.id = id;
	if (xioctl(fd, VIDIOC_QUERYCTRL, &query) == -1) {
		print_errno_note("VIDIOC_QUERYCTRL");
		return;
	}

	if (query.flags & V4L2_CTRL_FLAG_DISABLED) {
		printf("  ctrl 0x%08x disabled\n", id);
		return;
	}

	printf("  ctrl id=0x%08x name=%s type=%u min=%d max=%d step=%d default=%d\n",
	       query.id, query.name, query.type, query.minimum, query.maximum,
	       query.step, query.default_value);

	if (is_menu_control(query.type)) {
		int value;

		for (value = query.minimum; value <= query.maximum; value++) {
			struct v4l2_querymenu menu;

			memset(&menu, 0, sizeof(menu));
			menu.id = id;
			menu.index = value;
			if (xioctl(fd, VIDIOC_QUERYMENU, &menu) == 0)
				printf("    menu[%d]=%s\n", value, menu.name);
		}
	}

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = id;
	if (xioctl(fd, VIDIOC_G_CTRL, &ctrl) == 0) {
		if (id == V4L2_CID_POWER_LINE_FREQUENCY)
			printf("    current=%d (%s)\n",
			       ctrl.value, power_line_name(ctrl.value));
		else
			printf("    current=%d\n", ctrl.value);
	} else {
		print_errno_note("VIDIOC_G_CTRL");
	}
}

static int set_one_control(int fd, uint32_t id, int value, const char *name)
{
	struct v4l2_control ctrl;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = id;
	ctrl.value = value;

	/*
	 * VIDIOC_S_CTRL path:
	 * video_ioctl2 -> V4L2 control core -> ov5640_s_ctrl.
	 * HFLIP/VFLIP end in ov5640_set_flip(); power-line frequency ends in
	 * ov5640_set_power_line_frequency().
	 */
	if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
		print_errno_note(name);
		return -1;
	}

	printf("  %s set to %d\n", name, value);
	return 0;
}

static int show_and_optionally_set_controls(int fd, const struct demo_options *opt)
{
	int ret = 0;

	printf("\n[controls]\n");
	show_one_control(fd, V4L2_CID_HFLIP);
	show_one_control(fd, V4L2_CID_VFLIP);
	show_one_control(fd, V4L2_CID_POWER_LINE_FREQUENCY);

	if (opt->hflip >= 0)
		ret |= set_one_control(fd, V4L2_CID_HFLIP, opt->hflip,
				       "VIDIOC_S_CTRL HFLIP");
	if (opt->vflip >= 0)
		ret |= set_one_control(fd, V4L2_CID_VFLIP, opt->vflip,
				       "VIDIOC_S_CTRL VFLIP");
	if (opt->power_line_frequency >= 0)
		ret |= set_one_control(fd, V4L2_CID_POWER_LINE_FREQUENCY,
				       opt->power_line_frequency,
				       "VIDIOC_S_CTRL POWER_LINE_FREQUENCY");

	return ret ? -1 : 0;
}

static void show_crop_and_std_interfaces(int fd)
{
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	v4l2_std_id std = 0;

	printf("\n[crop placeholders and legacy std forwarding]\n");

	memset(&cropcap, 0, sizeof(cropcap));
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_CROPCAP, &cropcap) == 0) {
		printf("  VIDIOC_CROPCAP ok (placeholder in mx6s_capture.c)\n");
	} else {
		print_errno_note("VIDIOC_CROPCAP");
	}

	memset(&crop, 0, sizeof(crop));
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_G_CROP, &crop) == 0) {
		printf("  VIDIOC_G_CROP ok left=%d top=%d width=%d height=%d "
		       "(driver does not fill real crop)\n",
		       crop.c.left, crop.c.top, crop.c.width, crop.c.height);
	} else {
		print_errno_note("VIDIOC_G_CROP");
	}

	memset(&crop, 0, sizeof(crop));
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_S_CROP, &crop) == 0) {
		printf("  VIDIOC_S_CROP ok (placeholder, no sensor crop programming)\n");
	} else {
		print_errno_note("VIDIOC_S_CROP");
	}

	if (xioctl(fd, VIDIOC_G_STD, &std) == 0) {
		printf("  VIDIOC_G_STD std=0x%llx\n", (unsigned long long)std);
	} else {
		print_errno_note("VIDIOC_G_STD expected unsupported for OV5640");
	}

	std = 0;
	if (xioctl(fd, VIDIOC_QUERYSTD, &std) == 0) {
		printf("  VIDIOC_QUERYSTD std=0x%llx\n", (unsigned long long)std);
	} else {
		print_errno_note("VIDIOC_QUERYSTD expected unsupported for OV5640");
	}
}

static void show_classic_combinations(void)
{
	printf("\n[classic combinations]\n");
	printf("  discovery: open -> QUERYCAP -> ENUMINPUT/G_INPUT -> ENUM_FMT ->\n");
	printf("             ENUM_FRAMESIZES -> ENUM_FRAMEINTERVALS -> QUERYCTRL/G_CTRL -> close\n");
	printf("  configure: S_INPUT(0) -> TRY_FMT -> S_FMT -> G_PARM -> S_PARM -> G_FMT\n");
	printf("  controls:  QUERYCTRL/QUERYMENU -> G_CTRL -> S_CTRL before or during preview\n");
	printf("  mmap:      REQBUFS -> QUERYBUF -> mmap -> QBUF* -> STREAMON ->\n");
	printf("             poll -> DQBUF -> QBUF -> STREAMOFF -> munmap -> REQBUFS(0)\n");
	printf("  read:      configure -> read(fd, frame, sizeimage)\n");
	printf("  note:      G/S/QUERYSTD are host legacy forwarding hooks; ov5640.c has no\n");
	printf("             g_std/s_std/querystd callbacks, so they are not useful here.\n");
}

static int cleanup_reqbufs(int fd, enum v4l2_memory memory)
{
	struct v4l2_requestbuffers req;

	memset(&req, 0, sizeof(req));
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = memory;
	req.count = 0;

	if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
		print_errno_note("VIDIOC_REQBUFS count=0");
		return -1;
	}

	return 0;
}

static int demo_create_buffers(int fd, const struct v4l2_format *active_fmt)
{
#ifdef VIDIOC_CREATE_BUFS
	struct v4l2_create_buffers create;

	printf("\n[create buffers]\n");
	memset(&create, 0, sizeof(create));
	create.count = 1;
	create.memory = V4L2_MEMORY_MMAP;
	create.format = *active_fmt;

	/*
	 * VIDIOC_CREATE_BUFS path:
	 * video_ioctl2 -> vb2_ioctl_create_bufs -> mx6s_videobuf_setup.
	 * This is an optional alternative to allocating all buffers with
	 * VIDIOC_REQBUFS in one shot.
	 */
	if (xioctl(fd, VIDIOC_CREATE_BUFS, &create) == -1) {
		print_errno_note("VIDIOC_CREATE_BUFS");
		return -1;
	}

	printf("  requested 1 buffer, driver reports count=%u index=%u\n",
	       create.count, create.index);
	return cleanup_reqbufs(fd, V4L2_MEMORY_MMAP);
#else
	(void)fd;
	(void)active_fmt;
	printf("\n[create buffers]\n");
	printf("  local userspace headers do not define VIDIOC_CREATE_BUFS\n");
	return 0;
#endif
}

static int setup_mmap_buffers(int fd, unsigned int count,
			      struct mmap_buffer **out_buffers,
			      unsigned int *out_count)
{
	struct v4l2_requestbuffers req;
	struct mmap_buffer *buffers;
	unsigned int i;

	memset(&req, 0, sizeof(req));
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	req.count = count;

	/*
	 * VIDIOC_REQBUFS path:
	 * video_ioctl2 -> mx6s_vidioc_reqbufs -> vb2_reqbufs
	 * -> mx6s_videobuf_setup.
	 */
	if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
		print_errno_note("VIDIOC_REQBUFS MMAP");
		return -1;
	}
	if (req.count < 2) {
		fprintf(stderr, "driver returned only %u buffer(s), need at least 2\n",
			req.count);
		return -1;
	}

	buffers = calloc(req.count, sizeof(*buffers));
	if (!buffers) {
		perror("calloc");
		return -1;
	}

	for (i = 0; i < req.count; i++) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		/*
		 * VIDIOC_QUERYBUF path:
		 * video_ioctl2 -> mx6s_vidioc_querybuf -> vb2_querybuf.
		 */
		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
			print_errno_note("VIDIOC_QUERYBUF");
			goto fail;
		}

		buffers[i].length = buf.length;
		buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
					MAP_SHARED, fd, buf.m.offset);
		if (buffers[i].start == MAP_FAILED) {
			perror("mmap");
			buffers[i].start = NULL;
			goto fail;
		}

		/*
		 * VIDIOC_QBUF path:
		 * video_ioctl2 -> mx6s_vidioc_qbuf -> vb2_qbuf
		 * -> mx6s_videobuf_prepare -> mx6s_videobuf_queue.
		 */
		if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			print_errno_note("VIDIOC_QBUF initial");
			goto fail;
		}
	}

	*out_buffers = buffers;
	*out_count = req.count;
	return 0;

fail:
	for (i = 0; i < req.count; i++) {
		if (buffers[i].start)
			munmap(buffers[i].start, buffers[i].length);
	}
	free(buffers);
	cleanup_reqbufs(fd, V4L2_MEMORY_MMAP);
	return -1;
}

static int stream_loop(int fd, unsigned int frames, const char *tag)
{
	unsigned int done;

	for (done = 0; done < frames; done++) {
		struct pollfd pfd;
		struct v4l2_buffer buf;
		int pret;

		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = fd;
		pfd.events = POLLIN;

		/*
		 * poll path:
		 * mx6s_csi_fops.poll -> vb2_fop_poll.
		 */
		do {
			pret = poll(&pfd, 1, 3000);
		} while (pret == -1 && errno == EINTR);

		if (pret == -1) {
			perror("poll");
			return -1;
		}
		if (pret == 0) {
			fprintf(stderr, "%s: timeout waiting for frame\n", tag);
			return -1;
		}

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		/*
		 * VIDIOC_DQBUF path:
		 * video_ioctl2 -> mx6s_vidioc_dqbuf -> vb2_dqbuf.
		 * CSI IRQ completion path has already called vb2_buffer_done().
		 */
		if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
			print_errno_note("VIDIOC_DQBUF");
			return -1;
		}

		printf("  %s frame=%u index=%u bytesused=%u sequence=%u timestamp=%ld.%06ld\n",
		       tag, done, buf.index, buf.bytesused, buf.sequence,
		       (long)buf.timestamp.tv_sec, (long)buf.timestamp.tv_usec);

		if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			print_errno_note("VIDIOC_QBUF recycle");
			return -1;
		}
	}

	return 0;
}

static int demo_mmap_capture(int fd, const struct demo_options *opt)
{
	struct mmap_buffer *buffers = NULL;
	unsigned int nbufs = 0;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	unsigned int i;
	int ret = -1;

	printf("\n[mmap capture]\n");
	if (setup_mmap_buffers(fd, opt->buffers, &buffers, &nbufs) == -1)
		return -1;
	printf("  queued %u MMAP buffers\n", nbufs);

	/*
	 * VIDIOC_STREAMON path:
	 * video_ioctl2 -> mx6s_vidioc_streamon
	 * -> ov5640_s_stream(1)
	 * -> vb2_streamon -> mx6s_start_streaming -> mx6s_csi_enable.
	 */
	if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) {
		print_errno_note("VIDIOC_STREAMON");
		goto out_unmap;
	}

	ret = stream_loop(fd, opt->count, "mmap");

	/*
	 * VIDIOC_STREAMOFF path:
	 * video_ioctl2 -> mx6s_vidioc_streamoff
	 * -> vb2_streamoff -> mx6s_stop_streaming
	 * -> ov5640_s_stream(0).
	 */
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
		print_errno_note("VIDIOC_STREAMOFF");
		ret = -1;
	}

out_unmap:
	for (i = 0; i < nbufs; i++)
		munmap(buffers[i].start, buffers[i].length);
	free(buffers);
	cleanup_reqbufs(fd, V4L2_MEMORY_MMAP);
	return ret;
}

static int demo_userptr_capture(int fd, const struct demo_options *opt,
				const struct v4l2_format *active_fmt)
{
	void **buffers = NULL;
	size_t size = active_fmt->fmt.pix.sizeimage;
	struct v4l2_requestbuffers req;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	unsigned int i;
	int ret = -1;

	printf("\n[userptr capture]\n");
	printf("  USERPTR is enabled by mx6s_csi_open() via VB2_USERPTR; whether it\n");
	printf("  succeeds depends on vb2_dma_contig being able to pin/map the memory.\n");

	memset(&req, 0, sizeof(req));
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;
	req.count = opt->buffers;
	if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
		print_errno_note("VIDIOC_REQBUFS USERPTR");
		return -1;
	}

	buffers = calloc(req.count, sizeof(*buffers));
	if (!buffers) {
		perror("calloc");
		goto out_release;
	}

	for (i = 0; i < req.count; i++) {
		struct v4l2_buffer buf;

		if (posix_memalign(&buffers[i], 4096, size)) {
			fprintf(stderr, "posix_memalign failed\n");
			goto out_free;
		}
		memset(buffers[i], 0, size);

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_USERPTR;
		buf.index = i;
		buf.length = size;
		buf.m.userptr = (unsigned long)buffers[i];

		if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			print_errno_note("VIDIOC_QBUF USERPTR");
			goto out_free;
		}
	}

	if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) {
		print_errno_note("VIDIOC_STREAMON USERPTR");
		goto out_free;
	}

	/*
	 * The DQBUF/QBUF callback chain is the same as MMAP; only the user
	 * memory description differs.
	 */
	for (i = 0; i < opt->count; i++) {
		struct pollfd pfd;
		struct v4l2_buffer buf;
		int pret;

		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = fd;
		pfd.events = POLLIN;
		pret = poll(&pfd, 1, 3000);
		if (pret <= 0) {
			if (pret == 0)
				fprintf(stderr, "userptr: timeout waiting for frame\n");
			else
				perror("poll");
			goto out_streamoff;
		}

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_USERPTR;
		if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
			print_errno_note("VIDIOC_DQBUF USERPTR");
			goto out_streamoff;
		}
		printf("  userptr frame=%u bytesused=%u sequence=%u\n",
		       i, buf.bytesused, buf.sequence);
		if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			print_errno_note("VIDIOC_QBUF USERPTR recycle");
			goto out_streamoff;
		}
	}

	ret = 0;

out_streamoff:
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
		print_errno_note("VIDIOC_STREAMOFF USERPTR");
		ret = -1;
	}
out_free:
	if (buffers) {
		for (i = 0; i < req.count; i++)
			free(buffers[i]);
		free(buffers);
	}
out_release:
	cleanup_reqbufs(fd, V4L2_MEMORY_USERPTR);
	return ret;
}

static int demo_read_capture(int fd, const struct demo_options *opt,
			     const struct v4l2_format *active_fmt)
{
	size_t size = active_fmt->fmt.pix.sizeimage;
	void *frame;
	unsigned int i;

	printf("\n[read capture]\n");
	printf("  read path: mx6s_csi_read -> vb2_read -> VB2 queue ops -> CSI IRQ\n");

	frame = malloc(size);
	if (!frame) {
		perror("malloc");
		return -1;
	}

	for (i = 0; i < opt->count; i++) {
		ssize_t got;

		do {
			got = read(fd, frame, size);
		} while (got == -1 && errno == EINTR);

		if (got == -1) {
			perror("read");
			free(frame);
			return -1;
		}

		printf("  read frame=%u bytes=%zd\n", i, got);
	}

	free(frame);
	return 0;
}

int main(int argc, char **argv)
{
	struct demo_options opt;
	struct v4l2_format active_fmt;
	bool have_active_fmt = false;
	int fd;
	int ret = EXIT_SUCCESS;

	if (parse_args(argc, argv, &opt) < 0) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	fd = open(opt.device, O_RDWR);
	if (fd < 0) {
		perror(opt.device);
		return EXIT_FAILURE;
	}

	printf("device: %s\n", opt.device);

	if (opt.list) {
		show_querycap(fd);
		show_inputs(fd);
		show_formats(fd);
		if (get_format(fd, &active_fmt) == 0) {
			have_active_fmt = true;
			printf("\n[current format]\n");
			print_pix_format("  ", &active_fmt.fmt.pix);
		}
		show_and_optionally_set_controls(fd, &opt);
		show_classic_combinations();
	}

	if (opt.show_std_crop)
		show_crop_and_std_interfaces(fd);

	if (opt.configure || !have_active_fmt) {
		if (configure_format_and_fps(fd, &opt, &active_fmt) < 0) {
			ret = EXIT_FAILURE;
			goto out_close;
		}
		have_active_fmt = true;
	}

	if ((opt.hflip >= 0 || opt.vflip >= 0 || opt.power_line_frequency >= 0) &&
	    !opt.list) {
		if (show_and_optionally_set_controls(fd, &opt) < 0)
			ret = EXIT_FAILURE;
	}

	if (opt.create_bufs && demo_create_buffers(fd, &active_fmt) < 0)
		ret = EXIT_FAILURE;

	if (opt.mmap_capture && demo_mmap_capture(fd, &opt) < 0)
		ret = EXIT_FAILURE;

	if (opt.userptr_capture &&
	    demo_userptr_capture(fd, &opt, &active_fmt) < 0)
		ret = EXIT_FAILURE;

	if (opt.read_capture && demo_read_capture(fd, &opt, &active_fmt) < 0)
		ret = EXIT_FAILURE;

out_close:
	close(fd);
	return ret;
}
