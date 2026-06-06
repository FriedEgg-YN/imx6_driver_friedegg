/*
 * i.MX6ULL monitoring node
 *
 * Data path:
 *   OV5640/CSI -> V4L2 MMAP -> latest RGB565 frame -> LCD fbdev preview
 *                                                \-> JPEG -> HTTP/MJPEG
 *   AP3216C char device -> status thread -> /api/status
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <jpeglib.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define APP_NAME "imx6-monitor"
#define DEFAULT_VIDEO_DEV "/dev/video0"
#define DEFAULT_FB_DEV "/dev/fb0"
#define DEFAULT_SENSOR_DEV "/dev/ap3216c"
#define DEFAULT_HTTP_PORT 8080
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240
#define DEFAULT_FPS 10
#define DEFAULT_JPEG_QUALITY 75
#define CAMERA_BUFFER_COUNT 3
#define HTTP_BACKLOG 8
#define MJPEG_BOUNDARY "imx6monitor"
#define SENSOR_POLL_MS 500
#define LOW_LIGHT_THRESHOLD 80
#define NEAR_OBJECT_THRESHOLD 200

struct app_config {
	const char *video_dev;
	const char *fb_dev;
	const char *sensor_dev;
	int http_port;
	int width;
	int height;
	int fps;
	int jpeg_quality;
	bool lcd_enabled;
};

struct camera_buffer {
	void *start;
	size_t length;
};

struct fb_device {
	int fd;
	uint16_t *base;
	size_t screen_size;
	int width;
	int height;
	int line_length;
	int bpp;
	bool enabled;
};

struct camera_device {
	int fd;
	int width;
	int height;
	int stride_pixels;
	struct camera_buffer buffers[CAMERA_BUFFER_COUNT];
	int buffer_count;
};

struct sensor_status {
	bool present;
	unsigned int ir;
	unsigned int als;
	unsigned int ps;
	unsigned long read_count;
	unsigned long error_count;
	char state[32];
};

struct monitor_state {
	struct app_config cfg;
	pthread_mutex_t frame_lock;
	pthread_mutex_t sensor_lock;
	pthread_cond_t frame_cond;
	uint16_t *latest_frame;
	size_t latest_frame_bytes;
	unsigned long frame_seq;
	unsigned long captured_frames;
	unsigned long camera_errors;
	time_t start_time;
	struct sensor_status sensor;
	volatile sig_atomic_t stop;
};

static struct monitor_state g_state;

static void log_msg(const char *level, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "[%s][%s] ", APP_NAME, level);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void handle_signal(int signo)
{
	(void)signo;
	g_state.stop = 1;
}

static long monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int sleep_interruptible_ms(int ms)
{
	struct timespec req;

	req.tv_sec = ms / 1000;
	req.tv_nsec = (long)(ms % 1000) * 1000000L;
	while (!g_state.stop && nanosleep(&req, &req) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return 0;
}

static int parse_int(const char *value, int min, int max, const char *name)
{
	char *end = NULL;
	long parsed;

	errno = 0;
	parsed = strtol(value, &end, 10);
	if (errno || !end || *end != '\0' || parsed < min || parsed > max) {
		fprintf(stderr, "invalid %s: %s (expected %d..%d)\n", name, value, min, max);
		exit(EXIT_FAILURE);
	}
	return (int)parsed;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"Options:\n"
		"  -d <dev>       V4L2 device (default: %s)\n"
		"  -f <dev>       framebuffer device (default: %s)\n"
		"  -s <dev>       AP3216C device (default: %s)\n"
		"  -p <port>      HTTP port (default: %d)\n"
		"  -W <width>     capture width (default: %d)\n"
		"  -H <height>    capture height (default: %d)\n"
		"  -r <fps>       target capture/MJPEG fps (default: %d)\n"
		"  -q <quality>   JPEG quality 1..95 (default: %d)\n"
		"  -n             disable LCD preview\n",
		prog, DEFAULT_VIDEO_DEV, DEFAULT_FB_DEV, DEFAULT_SENSOR_DEV,
		DEFAULT_HTTP_PORT, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_FPS,
		DEFAULT_JPEG_QUALITY);
}

static void parse_args(int argc, char **argv, struct app_config *cfg)
{
	int opt;

	cfg->video_dev = DEFAULT_VIDEO_DEV;
	cfg->fb_dev = DEFAULT_FB_DEV;
	cfg->sensor_dev = DEFAULT_SENSOR_DEV;
	cfg->http_port = DEFAULT_HTTP_PORT;
	cfg->width = DEFAULT_WIDTH;
	cfg->height = DEFAULT_HEIGHT;
	cfg->fps = DEFAULT_FPS;
	cfg->jpeg_quality = DEFAULT_JPEG_QUALITY;
	cfg->lcd_enabled = true;

	while ((opt = getopt(argc, argv, "d:f:s:p:W:H:r:q:nh")) != -1) {
		switch (opt) {
		case 'd':
			cfg->video_dev = optarg;
			break;
		case 'f':
			cfg->fb_dev = optarg;
			break;
		case 's':
			cfg->sensor_dev = optarg;
			break;
		case 'p':
			cfg->http_port = parse_int(optarg, 1, 65535, "port");
			break;
		case 'W':
			cfg->width = parse_int(optarg, 16, 2592, "width");
			break;
		case 'H':
			cfg->height = parse_int(optarg, 16, 1944, "height");
			break;
		case 'r':
			cfg->fps = parse_int(optarg, 1, 30, "fps");
			break;
		case 'q':
			cfg->jpeg_quality = parse_int(optarg, 1, 95, "jpeg quality");
			break;
		case 'n':
			cfg->lcd_enabled = false;
			break;
		case 'h':
		default:
			usage(argv[0]);
			exit(opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE);
		}
	}
}

static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int fb_open(struct fb_device *fb, const char *dev)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;

	memset(fb, 0, sizeof(*fb));
	fb->fd = -1;

	fb->fd = open(dev, O_RDWR);
	if (fb->fd < 0) {
		log_msg("WARN", "open %s failed: %s; LCD preview disabled", dev, strerror(errno));
		return -1;
	}

	memset(&var, 0, sizeof(var));
	memset(&fix, 0, sizeof(fix));
	if (xioctl(fb->fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    xioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		log_msg("WARN", "query %s failed: %s; LCD preview disabled", dev, strerror(errno));
		close(fb->fd);
		fb->fd = -1;
		return -1;
	}

	if (var.bits_per_pixel != 16) {
		log_msg("WARN", "%s is %u bpp, expected RGB565 16 bpp; LCD preview disabled",
			dev, var.bits_per_pixel);
		close(fb->fd);
		fb->fd = -1;
		return -1;
	}

	fb->screen_size = (size_t)fix.line_length * var.yres;
	fb->base = mmap(NULL, fb->screen_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fb->fd, 0);
	if (fb->base == MAP_FAILED) {
		log_msg("WARN", "mmap %s failed: %s; LCD preview disabled", dev, strerror(errno));
		close(fb->fd);
		fb->fd = -1;
		fb->base = NULL;
		return -1;
	}

	fb->width = (int)var.xres;
	fb->height = (int)var.yres;
	fb->line_length = (int)fix.line_length;
	fb->bpp = (int)var.bits_per_pixel;
	fb->enabled = true;
	memset(fb->base, 0, fb->screen_size);
	log_msg("INFO", "LCD preview: %s %dx%d %dbpp", dev, fb->width, fb->height, fb->bpp);
	return 0;
}

static void fb_close(struct fb_device *fb)
{
	if (fb->base && fb->base != MAP_FAILED)
		munmap(fb->base, fb->screen_size);
	if (fb->fd >= 0)
		close(fb->fd);
	memset(fb, 0, sizeof(*fb));
	fb->fd = -1;
}

static void fb_blit_rgb565(struct fb_device *fb, const uint16_t *src,
			   int src_w, int src_h, int src_stride_pixels)
{
	int copy_w;
	int copy_h;
	int y;

	if (!fb->enabled || !fb->base)
		return;

	copy_w = src_w < fb->width ? src_w : fb->width;
	copy_h = src_h < fb->height ? src_h : fb->height;
	for (y = 0; y < copy_h; y++) {
		uint8_t *dst_line = (uint8_t *)fb->base + y * fb->line_length;
		memcpy(dst_line, src + y * src_stride_pixels,
		       (size_t)copy_w * sizeof(uint16_t));
	}
}

static int camera_open(struct camera_device *cam, const struct app_config *cfg)
{
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	struct v4l2_requestbuffers req;
	enum v4l2_buf_type type;
	int i;

	memset(cam, 0, sizeof(*cam));
	cam->fd = -1;

	cam->fd = open(cfg->video_dev, O_RDWR | O_NONBLOCK);
	if (cam->fd < 0) {
		log_msg("ERR", "open %s failed: %s", cfg->video_dev, strerror(errno));
		return -1;
	}

	memset(&cap, 0, sizeof(cap));
	if (xioctl(cam->fd, VIDIOC_QUERYCAP, &cap) < 0) {
		log_msg("ERR", "VIDIOC_QUERYCAP failed: %s", strerror(errno));
		return -1;
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
	    !(cap.capabilities & V4L2_CAP_STREAMING)) {
		log_msg("ERR", "%s lacks capture or streaming capability", cfg->video_dev);
		return -1;
	}
	log_msg("INFO", "camera driver=%s card=%s bus=%s",
		cap.driver, cap.card, cap.bus_info);

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = cfg->width;
	fmt.fmt.pix.height = cfg->height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
	if (xioctl(cam->fd, VIDIOC_S_FMT, &fmt) < 0) {
		log_msg("ERR", "VIDIOC_S_FMT RGB565 %dx%d failed: %s",
			cfg->width, cfg->height, strerror(errno));
		return -1;
	}
	if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
		log_msg("ERR", "camera returned pixel format 0x%08x, expected RGB565",
			fmt.fmt.pix.pixelformat);
		return -1;
	}
	cam->width = (int)fmt.fmt.pix.width;
	cam->height = (int)fmt.fmt.pix.height;
	if (cam->width != cfg->width || cam->height != cfg->height) {
		log_msg("ERR", "camera adjusted format to %dx%d, expected %dx%d",
			cam->width, cam->height, cfg->width, cfg->height);
		return -1;
	}
	cam->stride_pixels = fmt.fmt.pix.bytesperline ?
			     (int)fmt.fmt.pix.bytesperline / 2 : cam->width;
	log_msg("INFO", "camera format: %dx%d RGB565 stride=%d",
		cam->width, cam->height, cam->stride_pixels);

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(cam->fd, VIDIOC_G_PARM, &parm) == 0 &&
	    (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
		parm.parm.capture.timeperframe.numerator = 1;
		parm.parm.capture.timeperframe.denominator = cfg->fps;
		if (xioctl(cam->fd, VIDIOC_S_PARM, &parm) < 0)
			log_msg("WARN", "VIDIOC_S_PARM %dfps failed: %s", cfg->fps, strerror(errno));
	}

	memset(&req, 0, sizeof(req));
	req.count = CAMERA_BUFFER_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(cam->fd, VIDIOC_REQBUFS, &req) < 0) {
		log_msg("ERR", "VIDIOC_REQBUFS failed: %s", strerror(errno));
		return -1;
	}
	if (req.count < 2) {
		log_msg("ERR", "camera allocated too few buffers: %u", req.count);
		return -1;
	}
	cam->buffer_count = (int)req.count;
	if (cam->buffer_count > CAMERA_BUFFER_COUNT)
		cam->buffer_count = CAMERA_BUFFER_COUNT;

	for (i = 0; i < cam->buffer_count; i++) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = (unsigned int)i;
		if (xioctl(cam->fd, VIDIOC_QUERYBUF, &buf) < 0) {
			log_msg("ERR", "VIDIOC_QUERYBUF[%d] failed: %s", i, strerror(errno));
			return -1;
		}
		cam->buffers[i].length = buf.length;
		cam->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
					     MAP_SHARED, cam->fd, buf.m.offset);
		if (cam->buffers[i].start == MAP_FAILED) {
			log_msg("ERR", "mmap camera buffer[%d] failed: %s", i, strerror(errno));
			return -1;
		}
	}

	for (i = 0; i < cam->buffer_count; i++) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = (unsigned int)i;
		if (xioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) {
			log_msg("ERR", "VIDIOC_QBUF[%d] failed: %s", i, strerror(errno));
			return -1;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(cam->fd, VIDIOC_STREAMON, &type) < 0) {
		log_msg("ERR", "VIDIOC_STREAMON failed: %s", strerror(errno));
		return -1;
	}

	return 0;
}

static void camera_close(struct camera_device *cam)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int i;

	if (cam->fd >= 0)
		xioctl(cam->fd, VIDIOC_STREAMOFF, &type);

	for (i = 0; i < cam->buffer_count; i++) {
		if (cam->buffers[i].start && cam->buffers[i].start != MAP_FAILED)
			munmap(cam->buffers[i].start, cam->buffers[i].length);
	}
	if (cam->fd >= 0)
		close(cam->fd);
	memset(cam, 0, sizeof(*cam));
	cam->fd = -1;
}

static int camera_dequeue(struct camera_device *cam, struct v4l2_buffer *buf)
{
	fd_set fds;
	struct timeval tv;
	int ret;

	FD_ZERO(&fds);
	FD_SET(cam->fd, &fds);
	tv.tv_sec = 2;
	tv.tv_usec = 0;

	ret = select(cam->fd + 1, &fds, NULL, NULL, &tv);
	if (ret < 0) {
		if (errno == EINTR)
			return 1;
		log_msg("ERR", "select camera failed: %s", strerror(errno));
		return -1;
	}
	if (ret == 0) {
		log_msg("WARN", "camera dequeue timeout");
		return 1;
	}

	memset(buf, 0, sizeof(*buf));
	buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;
	if (xioctl(cam->fd, VIDIOC_DQBUF, buf) < 0) {
		if (errno == EAGAIN)
			return 1;
		log_msg("ERR", "VIDIOC_DQBUF failed: %s", strerror(errno));
		return -1;
	}
	return 0;
}

static void copy_rgb565_tight(uint16_t *dst, const uint16_t *src,
			      int width, int height, int stride_pixels)
{
	int y;

	if (stride_pixels == width) {
		memcpy(dst, src, (size_t)width * height * sizeof(uint16_t));
		return;
	}

	for (y = 0; y < height; y++)
		memcpy(dst + y * width, src + y * stride_pixels,
		       (size_t)width * sizeof(uint16_t));
}

static void update_latest_frame(struct monitor_state *state,
				const uint16_t *src, int width, int height,
				int stride_pixels)
{
	pthread_mutex_lock(&state->frame_lock);
	copy_rgb565_tight(state->latest_frame, src, width, height, stride_pixels);
	state->frame_seq++;
	state->captured_frames++;
	pthread_cond_broadcast(&state->frame_cond);
	pthread_mutex_unlock(&state->frame_lock);
}

static void *camera_thread(void *arg)
{
	struct monitor_state *state = arg;
	struct camera_device cam;
	struct fb_device fb;
	long frame_interval_ms;

	memset(&fb, 0, sizeof(fb));
	fb.fd = -1;

	if (camera_open(&cam, &state->cfg) < 0) {
		pthread_mutex_lock(&state->frame_lock);
		state->camera_errors++;
		pthread_mutex_unlock(&state->frame_lock);
		state->stop = 1;
		return NULL;
	}

	if (state->cfg.lcd_enabled)
		fb_open(&fb, state->cfg.fb_dev);

	frame_interval_ms = 1000L / state->cfg.fps;
	if (frame_interval_ms < 1)
		frame_interval_ms = 1;

	while (!state->stop) {
		struct v4l2_buffer buf;
		int ret;
		long start_ms;
		long elapsed_ms;

		start_ms = monotonic_ms();
		ret = camera_dequeue(&cam, &buf);
		if (ret < 0) {
			pthread_mutex_lock(&state->frame_lock);
			state->camera_errors++;
			pthread_mutex_unlock(&state->frame_lock);
			break;
		}
		if (ret > 0)
			continue;

		if (buf.index < (unsigned int)cam.buffer_count) {
			const uint16_t *src = cam.buffers[buf.index].start;

			update_latest_frame(state, src, cam.width, cam.height,
					    cam.stride_pixels);
			fb_blit_rgb565(&fb, src, cam.width, cam.height,
				       cam.stride_pixels);
		}

		if (xioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) {
			log_msg("ERR", "VIDIOC_QBUF after capture failed: %s", strerror(errno));
			pthread_mutex_lock(&state->frame_lock);
			state->camera_errors++;
			pthread_mutex_unlock(&state->frame_lock);
			break;
		}

		elapsed_ms = monotonic_ms() - start_ms;
		if (elapsed_ms < frame_interval_ms)
			sleep_interruptible_ms((int)(frame_interval_ms - elapsed_ms));
	}

	fb_close(&fb);
	camera_close(&cam);
	state->stop = 1;
	return NULL;
}

static void classify_sensor_state(struct sensor_status *sensor)
{
	if (!sensor->present) {
		strcpy(sensor->state, "missing");
		return;
	}
	if (sensor->ps >= NEAR_OBJECT_THRESHOLD) {
		strcpy(sensor->state, "near-object");
		return;
	}
	if (sensor->als <= LOW_LIGHT_THRESHOLD) {
		strcpy(sensor->state, "low-light");
		return;
	}
	strcpy(sensor->state, "normal");
}

static void *sensor_thread(void *arg)
{
	struct monitor_state *state = arg;

	while (!state->stop) {
		int fd;
		unsigned short data[3] = {0, 0, 0};
		struct sensor_status snapshot;

		memset(&snapshot, 0, sizeof(snapshot));
		fd = open(state->cfg.sensor_dev, O_RDONLY);
		if (fd >= 0) {
			ssize_t ret = read(fd, data, sizeof(data));

			if (ret == (ssize_t)sizeof(data)) {
				snapshot.present = true;
				snapshot.ir = data[0];
				snapshot.als = data[1];
				snapshot.ps = data[2];
			} else {
				snapshot.error_count++;
			}
			close(fd);
		} else {
			snapshot.error_count++;
		}

		pthread_mutex_lock(&state->sensor_lock);
		if (snapshot.present) {
			state->sensor.present = true;
			state->sensor.ir = snapshot.ir;
			state->sensor.als = snapshot.als;
			state->sensor.ps = snapshot.ps;
			state->sensor.read_count++;
		} else {
			state->sensor.present = false;
			state->sensor.error_count++;
		}
		classify_sensor_state(&state->sensor);
		pthread_mutex_unlock(&state->sensor_lock);

		sleep_interruptible_ms(SENSOR_POLL_MS);
	}

	return NULL;
}

static int write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0) {
		ssize_t ret = send(fd, p, len, MSG_NOSIGNAL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (ret == 0)
			return -1;
		p += ret;
		len -= (size_t)ret;
	}
	return 0;
}

static int write_fmt(int fd, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len < 0)
		return -1;
	if ((size_t)len >= sizeof(buf))
		len = (int)sizeof(buf) - 1;
	return write_all(fd, buf, (size_t)len);
}

static void rgb565_to_rgb888(const uint16_t *src, uint8_t *dst, int pixels)
{
	int i;

	for (i = 0; i < pixels; i++) {
		uint16_t v = src[i];
		uint8_t r5 = (uint8_t)((v >> 11) & 0x1f);
		uint8_t g6 = (uint8_t)((v >> 5) & 0x3f);
		uint8_t b5 = (uint8_t)(v & 0x1f);

		dst[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
		dst[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
		dst[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
	}
}

static int encode_jpeg_rgb565(const uint16_t *rgb565, int width, int height,
			      int quality, unsigned char **jpeg_buf,
			      unsigned long *jpeg_size)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	uint8_t *rgb = NULL;
	JSAMPROW row_pointer[1];
	int row_stride;

	*jpeg_buf = NULL;
	*jpeg_size = 0;

	rgb = malloc((size_t)width * height * 3);
	if (!rgb)
		return -1;
	rgb565_to_rgb888(rgb565, rgb, width * height);

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, jpeg_buf, jpeg_size);
	cinfo.image_width = (JDIMENSION)width;
	cinfo.image_height = (JDIMENSION)height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	row_stride = width * 3;
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = &rgb[cinfo.next_scanline * row_stride];
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	free(rgb);
	return 0;
}

static int snapshot_jpeg(struct monitor_state *state, unsigned char **jpeg_buf,
			 unsigned long *jpeg_size, unsigned long *frame_seq)
{
	uint16_t *copy;
	size_t bytes = state->latest_frame_bytes;
	int ret;

	copy = malloc(bytes);
	if (!copy)
		return -1;

	pthread_mutex_lock(&state->frame_lock);
	if (state->frame_seq == 0) {
		pthread_mutex_unlock(&state->frame_lock);
		free(copy);
		return 1;
	}
	memcpy(copy, state->latest_frame, bytes);
	if (frame_seq)
		*frame_seq = state->frame_seq;
	pthread_mutex_unlock(&state->frame_lock);

	ret = encode_jpeg_rgb565(copy, state->cfg.width, state->cfg.height,
				 state->cfg.jpeg_quality, jpeg_buf, jpeg_size);
	free(copy);
	return ret;
}

static int wait_for_new_frame(struct monitor_state *state, unsigned long last_seq)
{
	int ret = 0;

	pthread_mutex_lock(&state->frame_lock);
	while (!state->stop && state->frame_seq <= last_seq)
		pthread_cond_wait(&state->frame_cond, &state->frame_lock);
	if (state->stop)
		ret = -1;
	pthread_mutex_unlock(&state->frame_lock);
	return ret;
}

static void send_not_found(int fd)
{
	write_fmt(fd,
		  "HTTP/1.1 404 Not Found\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n"
		  "not found\n");
}

static void send_service_unavailable(int fd, const char *msg)
{
	write_fmt(fd,
		  "HTTP/1.1 503 Service Unavailable\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n"
		  "%s\n", msg);
}

static void send_index(int fd)
{
	static const char page[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n\r\n"
		"<!doctype html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>i.MX6 Monitor</title>"
		"<style>"
		"body{font-family:system-ui,Arial,sans-serif;margin:0;background:#101418;color:#eef3f8}"
		"main{max-width:980px;margin:0 auto;padding:20px}"
		"h1{font-size:24px;margin:0 0 12px}"
		".grid{display:grid;grid-template-columns:2fr 1fr;gap:16px}"
		"img{width:100%;background:#000;border:1px solid #33404a}"
		"pre{white-space:pre-wrap;background:#1a222a;padding:12px;border:1px solid #33404a}"
		"@media(max-width:760px){.grid{grid-template-columns:1fr}}"
		"</style></head><body><main>"
		"<h1>i.MX6ULL Monitoring Node</h1>"
		"<div class=\"grid\"><section><img src=\"/stream.mjpg\" alt=\"camera stream\"></section>"
		"<section><pre id=\"status\">loading...</pre></section></div>"
		"<script>"
		"async function tick(){try{let r=await fetch('/api/status',{cache:'no-store'});"
		"document.getElementById('status').textContent=JSON.stringify(await r.json(),null,2);}"
		"catch(e){document.getElementById('status').textContent=String(e)}}"
		"tick();setInterval(tick,1000);"
		"</script></main></body></html>";

	write_all(fd, page, sizeof(page) - 1);
}

static void send_status(struct monitor_state *state, int fd)
{
	struct sensor_status sensor;
	unsigned long frame_seq;
	unsigned long captured_frames;
	unsigned long camera_errors;
	long uptime;

	pthread_mutex_lock(&state->sensor_lock);
	sensor = state->sensor;
	pthread_mutex_unlock(&state->sensor_lock);

	pthread_mutex_lock(&state->frame_lock);
	frame_seq = state->frame_seq;
	captured_frames = state->captured_frames;
	camera_errors = state->camera_errors;
	pthread_mutex_unlock(&state->frame_lock);

	uptime = (long)(time(NULL) - state->start_time);
	write_fmt(fd,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: application/json\r\n"
		  "Cache-Control: no-store\r\n"
		  "Connection: close\r\n\r\n"
		  "{"
		  "\"app\":\"%s\","
		  "\"uptime_sec\":%ld,"
		  "\"video\":{\"device\":\"%s\",\"width\":%d,\"height\":%d,\"fps_target\":%d,"
		  "\"frame_seq\":%lu,\"captured_frames\":%lu,\"errors\":%lu},"
		  "\"sensor\":{\"device\":\"%s\",\"present\":%s,\"ir\":%u,\"als\":%u,\"ps\":%u,"
		  "\"state\":\"%s\",\"read_count\":%lu,\"error_count\":%lu}"
		  "}\n",
		  APP_NAME, uptime, state->cfg.video_dev, state->cfg.width,
		  state->cfg.height, state->cfg.fps, frame_seq, captured_frames,
		  camera_errors, state->cfg.sensor_dev,
		  sensor.present ? "true" : "false", sensor.ir, sensor.als,
		  sensor.ps, sensor.state, sensor.read_count, sensor.error_count);
}

static void send_snapshot(struct monitor_state *state, int fd)
{
	unsigned char *jpeg = NULL;
	unsigned long jpeg_size = 0;
	int ret;

	ret = snapshot_jpeg(state, &jpeg, &jpeg_size, NULL);
	if (ret == 1) {
		send_service_unavailable(fd, "no frame captured yet");
		return;
	}
	if (ret < 0 || !jpeg) {
		send_service_unavailable(fd, "jpeg encode failed");
		return;
	}

	write_fmt(fd,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: image/jpeg\r\n"
		  "Content-Length: %lu\r\n"
		  "Cache-Control: no-store\r\n"
		  "Connection: close\r\n\r\n",
		  jpeg_size);
	write_all(fd, jpeg, jpeg_size);
	free(jpeg);
}

static void send_mjpeg(struct monitor_state *state, int fd)
{
	unsigned long last_seq = 0;

	if (write_fmt(fd,
		      "HTTP/1.1 200 OK\r\n"
		      "Content-Type: multipart/x-mixed-replace; boundary=%s\r\n"
		      "Cache-Control: no-store\r\n"
		      "Connection: close\r\n\r\n",
		      MJPEG_BOUNDARY) < 0)
		return;

	while (!state->stop) {
		unsigned char *jpeg = NULL;
		unsigned long jpeg_size = 0;
		unsigned long seq = 0;
		int ret;

		if (wait_for_new_frame(state, last_seq) < 0)
			break;

		ret = snapshot_jpeg(state, &jpeg, &jpeg_size, &seq);
		if (ret != 0 || !jpeg)
			continue;
		last_seq = seq;

		if (write_fmt(fd,
			      "--%s\r\n"
			      "Content-Type: image/jpeg\r\n"
			      "Content-Length: %lu\r\n\r\n",
			      MJPEG_BOUNDARY, jpeg_size) < 0 ||
		    write_all(fd, jpeg, jpeg_size) < 0 ||
		    write_all(fd, "\r\n", 2) < 0) {
			free(jpeg);
			break;
		}
		free(jpeg);
	}
}

static int read_request_path(int fd, char *path, size_t path_size)
{
	char buf[1024];
	ssize_t len;
	char method[16];

	len = recv(fd, buf, sizeof(buf) - 1, 0);
	if (len <= 0)
		return -1;
	buf[len] = '\0';

	if (sscanf(buf, "%15s %255s", method, path) != 2)
		return -1;
	if (strcmp(method, "GET") != 0)
		return -1;
	path[path_size - 1] = '\0';
	return 0;
}

struct client_context {
	struct monitor_state *state;
	int fd;
};

static void handle_client(struct monitor_state *state, int client_fd)
{
	char path[256] = "/";

	if (read_request_path(client_fd, path, sizeof(path)) < 0) {
		send_not_found(client_fd);
		return;
	}

	if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
		send_index(client_fd);
	else if (strcmp(path, "/api/status") == 0)
		send_status(state, client_fd);
	else if (strcmp(path, "/snapshot.jpg") == 0)
		send_snapshot(state, client_fd);
	else if (strcmp(path, "/stream.mjpg") == 0)
		send_mjpeg(state, client_fd);
	else
		send_not_found(client_fd);
}

static void *client_thread(void *arg)
{
	struct client_context *ctx = arg;

	handle_client(ctx->state, ctx->fd);
	close(ctx->fd);
	free(ctx);
	return NULL;
}

static int http_listen(int port)
{
	int fd;
	int yes = 1;
	struct sockaddr_in addr;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		log_msg("ERR", "socket failed: %s", strerror(errno));
		return -1;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_msg("ERR", "bind port %d failed: %s", port, strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, HTTP_BACKLOG) < 0) {
		log_msg("ERR", "listen failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

static void http_loop(struct monitor_state *state)
{
	int listen_fd;

	listen_fd = http_listen(state->cfg.http_port);
	if (listen_fd < 0) {
		state->stop = 1;
		return;
	}

	log_msg("INFO", "HTTP server listening on 0.0.0.0:%d", state->cfg.http_port);
	while (!state->stop) {
		fd_set fds;
		struct timeval tv;
		int ret;
		int client_fd;

		FD_ZERO(&fds);
		FD_SET(listen_fd, &fds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		ret = select(listen_fd + 1, &fds, NULL, NULL, &tv);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			log_msg("ERR", "select listen failed: %s", strerror(errno));
			break;
		}
		if (ret == 0)
			continue;

		client_fd = accept(listen_fd, NULL, NULL);
		if (client_fd < 0) {
			if (errno == EINTR)
				continue;
			log_msg("WARN", "accept failed: %s", strerror(errno));
			continue;
		}

		{
			struct client_context *ctx = malloc(sizeof(*ctx));
			pthread_t tid;

			if (!ctx) {
				close(client_fd);
				continue;
			}
			ctx->state = state;
			ctx->fd = client_fd;
			ret = pthread_create(&tid, NULL, client_thread, ctx);
			if (ret != 0) {
				log_msg("WARN", "create client thread failed: %s", strerror(ret));
				free(ctx);
				handle_client(state, client_fd);
				close(client_fd);
				continue;
			}
			pthread_detach(tid);
		}
	}

	close(listen_fd);
	state->stop = 1;
}

int main(int argc, char **argv)
{
	pthread_t cam_tid;
	pthread_t sensor_tid;
	int ret;

	memset(&g_state, 0, sizeof(g_state));
	parse_args(argc, argv, &g_state.cfg);
	g_state.latest_frame_bytes = (size_t)g_state.cfg.width * g_state.cfg.height * sizeof(uint16_t);
	g_state.latest_frame = calloc(1, g_state.latest_frame_bytes);
	if (!g_state.latest_frame) {
		log_msg("ERR", "allocate latest frame failed");
		return EXIT_FAILURE;
	}
	strcpy(g_state.sensor.state, "unknown");
	g_state.start_time = time(NULL);

	pthread_mutex_init(&g_state.frame_lock, NULL);
	pthread_mutex_init(&g_state.sensor_lock, NULL);
	pthread_cond_init(&g_state.frame_cond, NULL);
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	log_msg("INFO", "starting: video=%s sensor=%s port=%d capture=%dx%d fps=%d lcd=%s",
		g_state.cfg.video_dev, g_state.cfg.sensor_dev, g_state.cfg.http_port,
		g_state.cfg.width, g_state.cfg.height, g_state.cfg.fps,
		g_state.cfg.lcd_enabled ? "on" : "off");

	ret = pthread_create(&cam_tid, NULL, camera_thread, &g_state);
	if (ret != 0) {
		log_msg("ERR", "create camera thread failed: %s", strerror(ret));
		free(g_state.latest_frame);
		return EXIT_FAILURE;
	}

	ret = pthread_create(&sensor_tid, NULL, sensor_thread, &g_state);
	if (ret != 0) {
		log_msg("ERR", "create sensor thread failed: %s", strerror(ret));
		g_state.stop = 1;
		pthread_join(cam_tid, NULL);
		free(g_state.latest_frame);
		return EXIT_FAILURE;
	}

	http_loop(&g_state);

	g_state.stop = 1;
	pthread_cond_broadcast(&g_state.frame_cond);
	pthread_join(cam_tid, NULL);
	pthread_join(sensor_tid, NULL);
	free(g_state.latest_frame);
	pthread_mutex_destroy(&g_state.frame_lock);
	pthread_mutex_destroy(&g_state.sensor_lock);
	pthread_cond_destroy(&g_state.frame_cond);
	log_msg("INFO", "stopped");
	return EXIT_SUCCESS;
}
