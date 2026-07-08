#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include <pthread.h>
#include <time.h>

#define FB_DEV "/dev/fb0"
#define FRAMEBUFFER_COUNT 3

typedef struct camera_format {
    unsigned char description[32];
    unsigned int pixelformat;
} cam_fmt;

typedef struct cam_buf_info {
    unsigned short *start;
    unsigned long length;
} cam_buf_info;

static int width;
static int height;
static unsigned short *screen_base = NULL;
static int fb_fd = -1;
static int v4l2_fd = -1;
static cam_buf_info buf_infos[FRAMEBUFFER_COUNT];
static cam_fmt cam_fmts[10];
static int frm_width, frm_height;

static int fb_dev_init(void)
{
    struct fb_var_screeninfo fb_var = {0};
    struct fb_fix_screeninfo fb_fix = {0};
    unsigned long screen_size;

    fb_fd = open(FB_DEV, O_RDWR);
    if (0 > fb_fd) {
        fprintf(stderr, "open error: %s: %s\n", FB_DEV, strerror(errno));
        return -1;
    }

    ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fix);

    screen_size = fb_fix.line_length * fb_var.yres;
    width = fb_var.xres;
    height = fb_var.yres;
    printf("LCD: %dx%d, %d bpp\n", width, height, fb_var.bits_per_pixel);

    screen_base = mmap(NULL, screen_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fb_fd, 0);
    if (MAP_FAILED == (void *)screen_base) {
        perror("mmap error");
        close(fb_fd);
        return -1;
    }

    /* Clear screen to black */
    memset(screen_base, 0x0000, screen_size);
    return 0;
}

static int v4l2_dev_init(const char *device)
{
    struct v4l2_capability cap = {0};

    v4l2_fd = open(device, O_RDWR);
    if (0 > v4l2_fd) {
        fprintf(stderr, "open error: %s: %s\n", device, strerror(errno));
        return -1;
    }

    ioctl(v4l2_fd, VIDIOC_QUERYCAP, &cap);

    if (!(V4L2_CAP_VIDEO_CAPTURE & cap.capabilities)) {
        fprintf(stderr, "Error: %s: No capture video device!\n", device);
        close(v4l2_fd);
        return -1;
    }

    return 0;
}

static void v4l2_enum_formats(void)
{
    struct v4l2_fmtdesc fmtdesc = {0};
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
        cam_fmts[fmtdesc.index].pixelformat = fmtdesc.pixelformat;
        strcpy(cam_fmts[fmtdesc.index].description, fmtdesc.description);
        fmtdesc.index++;
    }
}

static void v4l2_print_formats(void)
{
    struct v4l2_frmsizeenum frmsize = {0};
    struct v4l2_frmivalenum frmival = {0};
    int i;

    frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    frmival.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    for (i = 0; cam_fmts[i].pixelformat; i++) {
        printf("format<0x%x>, description<%s>\n",
               cam_fmts[i].pixelformat, cam_fmts[i].description);

        frmsize.index = 0;
        frmsize.pixel_format = cam_fmts[i].pixelformat;
        frmival.pixel_format = cam_fmts[i].pixelformat;

        while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize)) {
            printf("size<%d*%d> ", frmsize.discrete.width,
                   frmsize.discrete.height);
            frmsize.index++;

            frmival.index = 0;
            frmival.width = frmsize.discrete.width;
            frmival.height = frmsize.discrete.height;
            while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival)) {
                printf("<%dfps>", frmival.discrete.denominator /
                                  frmival.discrete.numerator);
                frmival.index++;
            }
            printf("\n");
        }
        printf("\n");
    }
}

static int v4l2_set_format(void)
{
    struct v4l2_format fmt = {0};
    struct v4l2_streamparm streamparm = {0};

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;

    if (0 > ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt)) {
        fprintf(stderr, "ioctl error: VIDIOC_S_FMT: %s\n", strerror(errno));
        return -1;
    }

    if (V4L2_PIX_FMT_RGB565 != fmt.fmt.pix.pixelformat) {
        fprintf(stderr, "Error: the device does not support RGB565 format!\n");
        return -1;
    }

    frm_width = fmt.fmt.pix.width;
    frm_height = fmt.fmt.pix.height;
    printf("Video frame: %d x %d\n", frm_width, frm_height);

    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2_fd, VIDIOC_G_PARM, &streamparm);

    if (V4L2_CAP_TIMEPERFRAME & streamparm.parm.capture.capability) {
        streamparm.parm.capture.timeperframe.numerator = 1;
        streamparm.parm.capture.timeperframe.denominator = 30;
        if (0 > ioctl(v4l2_fd, VIDIOC_S_PARM, &streamparm)) {
            fprintf(stderr, "ioctl error: VIDIOC_S_PARM: %s\n",
                    strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int v4l2_init_buffer(void)
{
    struct v4l2_requestbuffers reqbuf = {0};
    struct v4l2_buffer buf = {0};

    reqbuf.count = FRAMEBUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (0 > ioctl(v4l2_fd, VIDIOC_REQBUFS, &reqbuf)) {
        fprintf(stderr, "ioctl error: VIDIOC_REQBUFS: %s\n", strerror(errno));
        return -1;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for (buf.index = 0; buf.index < FRAMEBUFFER_COUNT; buf.index++) {
        ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf);
        buf_infos[buf.index].length = buf.length;
        buf_infos[buf.index].start = mmap(NULL, buf.length,
                                          PROT_READ | PROT_WRITE, MAP_SHARED,
                                          v4l2_fd, buf.m.offset);
        if (MAP_FAILED == buf_infos[buf.index].start) {
            perror("mmap error");
            return -1;
        }
    }

    for (buf.index = 0; buf.index < FRAMEBUFFER_COUNT; buf.index++) {
        if (0 > ioctl(v4l2_fd, VIDIOC_QBUF, &buf)) {
            fprintf(stderr, "ioctl error: VIDIOC_QBUF: %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int v4l2_stream_on(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 > ioctl(v4l2_fd, VIDIOC_STREAMON, &type)) {
        fprintf(stderr, "ioctl error: VIDIOC_STREAMON: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

#ifndef V4L2_CID_AUTO_FOCUS_START
#define V4L2_CID_AUTO_FOCUS_START  (V4L2_CID_CAMERA_CLASS_BASE + 28)
#define V4L2_CID_AUTO_FOCUS_STOP   (V4L2_CID_CAMERA_CLASS_BASE + 29)
#define V4L2_CID_AUTO_FOCUS_STATUS (V4L2_CID_CAMERA_CLASS_BASE + 30)
#endif

#ifndef V4L2_AUTO_FOCUS_STATUS_BUSY
#define V4L2_AUTO_FOCUS_STATUS_IDLE    (0 << 0)
#define V4L2_AUTO_FOCUS_STATUS_BUSY    (1 << 0)
#define V4L2_AUTO_FOCUS_STATUS_REACHED (1 << 1)
#define V4L2_AUTO_FOCUS_STATUS_FAILED  (1 << 2)
#endif

#define OV5640_CID_AF_BASE        (V4L2_CID_USER_BASE | 0xf000)
#define OV5640_CID_AF_ZONE_MODE   (OV5640_CID_AF_BASE + 0)
#define OV5640_CID_AF_TOUCH_X     (OV5640_CID_AF_BASE + 1)
#define OV5640_CID_AF_TOUCH_Y     (OV5640_CID_AF_BASE + 2)
#define OV5640_CID_AF_ZONE_RESULT (OV5640_CID_AF_BASE + 3)

#define OV5640_AF_ZONE_MODE_DEFAULT 0
#define OV5640_AF_ZONE_MODE_TOUCH   1
#define AF_TIMEOUT_MS 5000ULL
#define AF_POLL_MS 100ULL
#define AF_RESTART_DELAY_MS 1000ULL

enum test_af_mode {
    TEST_AF_DEFAULT,
    TEST_AF_TOUCH,
};

static unsigned long long get_time_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;

    return (ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL);
}

static int v4l2_set_ctrl(unsigned int id, int value)
{
    struct v4l2_control ctrl = {0};

    ctrl.id = id;
    ctrl.value = value;

    return ioctl(v4l2_fd, VIDIOC_S_CTRL, &ctrl);
}

static int v4l2_get_ctrl(unsigned int id, int *value)
{
    struct v4l2_control ctrl = {0};

    if (!value) {
        errno = EINVAL;
        return -1;
    }

    ctrl.id = id;

    if (ioctl(v4l2_fd, VIDIOC_G_CTRL, &ctrl) < 0)
        return -1;

    *value = ctrl.value;
    return 0;
}

static int parse_coord(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    if (!text || !value) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    parsed = strtol(text, &end, 0);
    if (errno || !end || *end || parsed < 0 || parsed > INT_MAX) {
        errno = EINVAL;
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <video_dev> default\n", prog);
    fprintf(stderr, "  %s <video_dev> touch <x> <y>\n", prog);
    fprintf(stderr, "Loop preview forever and repeat AF on the selected zone.\n");
}

static const char *af_status_name(int status)
{
    if (status & V4L2_AUTO_FOCUS_STATUS_REACHED)
        return "REACHED";
    if (status & V4L2_AUTO_FOCUS_STATUS_FAILED)
        return "FAILED";
    if (status & V4L2_AUTO_FOCUS_STATUS_BUSY)
        return "BUSY";
    return "IDLE";
}

static int v4l2_read_frame_once(void)
{
    struct v4l2_buffer buf = {0};
    unsigned short *base;
    unsigned short *start;
    int min_w, min_h;
    int j;

    min_w = (width > frm_width) ? frm_width : width;
    min_h = (height > frm_height) ? frm_height : height;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
        fprintf(stderr, "ioctl error: VIDIOC_DQBUF: %s\n", strerror(errno));
        return -1;
    }

    if (buf.index >= FRAMEBUFFER_COUNT) {
        fprintf(stderr, "Error: invalid buffer index %u\n", buf.index);
        errno = EINVAL;
        return -1;
    }

    for (j = 0, base = screen_base, start = buf_infos[buf.index].start;
         j < min_h; j++) {
        memcpy(base, start, min_w * 2);
        base += width;
        start += frm_width;
    }

    if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "ioctl error: VIDIOC_QBUF: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int configure_af_zone(enum test_af_mode mode, int x, int y)
{
    if (mode == TEST_AF_DEFAULT) {
        if (v4l2_set_ctrl(OV5640_CID_AF_ZONE_MODE,
                          OV5640_AF_ZONE_MODE_DEFAULT) < 0) {
            fprintf(stderr, "ioctl error: set af_zone_mode=default: %s\n",
                    strerror(errno));
            return -1;
        }
        printf("AF zone: default\n");
        return 0;
    }

    if (v4l2_set_ctrl(OV5640_CID_AF_TOUCH_X, x) < 0 ||
        v4l2_set_ctrl(OV5640_CID_AF_TOUCH_Y, y) < 0 ||
        v4l2_set_ctrl(OV5640_CID_AF_ZONE_MODE,
                      OV5640_AF_ZONE_MODE_TOUCH) < 0) {
        fprintf(stderr, "ioctl error: set touch AF zone (%d,%d): %s\n",
                x, y, strerror(errno));
        return -1;
    }

    printf("AF zone: touch center=(%d,%d) frame=%dx%d\n",
           x, y, frm_width, frm_height);
    return 0;
}

static int start_af_attempt(unsigned long attempt,
                            unsigned long long *start_ms)
{
    unsigned long long now_ms;

    now_ms = get_time_ms();
    if (!now_ms)
        now_ms = 1;

    *start_ms = now_ms;

    if (v4l2_set_ctrl(V4L2_CID_AUTO_FOCUS_START, 0) < 0) {
        fprintf(stderr, "ioctl error: AUTO_FOCUS_START: %s\n", strerror(errno));
        return -1;
    }

    printf("AF #%lu started\n", attempt);
    fflush(stdout);
    return 0;
}

static int poll_af_attempt(unsigned long attempt,
                           unsigned long long start_ms,
                           int *last_status,
                           int *done)
{
    unsigned long long now_ms;
    unsigned long long elapsed_ms;
    int status = V4L2_AUTO_FOCUS_STATUS_IDLE;
    int zone_result = 0;

    now_ms = get_time_ms();
    if (!now_ms) {
        *done = 0;
        return 0;
    }

    elapsed_ms = now_ms - start_ms;

    if (v4l2_get_ctrl(V4L2_CID_AUTO_FOCUS_STATUS, &status) < 0) {
        fprintf(stderr, "ioctl error: AUTO_FOCUS_STATUS: %s\n",
                strerror(errno));
        return -1;
    }

    if (v4l2_get_ctrl(OV5640_CID_AF_ZONE_RESULT, &zone_result) < 0)
        zone_result = 0;

    if (status != *last_status) {
        printf("AF #%lu status: %s elapsed=%llu ms zone_result=0x%02x\n",
               attempt, af_status_name(status), elapsed_ms, zone_result);
        fflush(stdout);
        *last_status = status;
    }

    if (status & (V4L2_AUTO_FOCUS_STATUS_REACHED |
                  V4L2_AUTO_FOCUS_STATUS_FAILED)) {
        printf("AF #%lu result: %s elapsed=%llu ms zone_result=0x%02x\n",
               attempt, af_status_name(status), elapsed_ms, zone_result);
        fflush(stdout);
        *done = 1;
        return 0;
    }

    if (elapsed_ms >= AF_TIMEOUT_MS) {
        fprintf(stderr,
                "AF #%lu timeout: elapsed=%llu ms last_status=%s zone_result=0x%02x\n",
                attempt, elapsed_ms, af_status_name(status), zone_result);
        if (v4l2_set_ctrl(V4L2_CID_AUTO_FOCUS_STOP, 0) < 0)
            fprintf(stderr, "ioctl warning: AUTO_FOCUS_STOP: %s\n",
                    strerror(errno));
        *done = 1;
        return 0;
    }

    *done = 0;
    return 0;
}

static void *run_af_loop(void *arg)
{
    unsigned long attempt = 0;
    unsigned long long af_start_ms = 0;
    int last_status = -1;
    int done = 0;

    (void)arg;

    printf("AF loop: timeout=%llu ms poll=%llu ms restart_delay=%llu ms\n",
           AF_TIMEOUT_MS, AF_POLL_MS, AF_RESTART_DELAY_MS);
    fflush(stdout);

    for (;;) {
        attempt++;
        last_status = -1;
        done = 0;

        if (start_af_attempt(attempt, &af_start_ms) < 0)
            break;

        do {
            usleep(AF_POLL_MS * 1000);
            if (poll_af_attempt(attempt, af_start_ms, &last_status,
                                &done) < 0)
                return NULL;
        } while (!done);

        usleep(AF_RESTART_DELAY_MS * 1000);
    }

    fprintf(stderr, "AF loop stopped\n");
    return NULL;
}

static int run_preview_loop(void)
{
    for (;;) {
        if (v4l2_read_frame_once() < 0)
            return -1;
    }
}

int main(int argc, char *argv[])
{
    enum test_af_mode af_mode;
    pthread_t af_thread;
    int pthread_ret;
    int touch_x = 0;
    int touch_y = 0;

    if (argc == 3 && strcmp(argv[2], "default") == 0) {
        af_mode = TEST_AF_DEFAULT;
    } else if (argc == 5 && strcmp(argv[2], "touch") == 0) {
        af_mode = TEST_AF_TOUCH;
        if (parse_coord(argv[3], &touch_x) < 0 ||
            parse_coord(argv[4], &touch_y) < 0) {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    } else {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (fb_dev_init())
        exit(EXIT_FAILURE);

    if (v4l2_dev_init(argv[1]))
        exit(EXIT_FAILURE);

    v4l2_enum_formats();
    v4l2_print_formats();

    if (v4l2_set_format())
        exit(EXIT_FAILURE);

    if (v4l2_init_buffer())
        exit(EXIT_FAILURE);

    if (v4l2_stream_on())
        exit(EXIT_FAILURE);

    printf("Starting video preview on LCD...\n");

    if (configure_af_zone(af_mode, touch_x, touch_y) < 0)
        exit(EXIT_FAILURE);

    pthread_ret = pthread_create(&af_thread, NULL, run_af_loop, NULL);
    if (pthread_ret) {
        fprintf(stderr, "pthread_create error: %s\n", strerror(pthread_ret));
        exit(EXIT_FAILURE);
    }

    pthread_detach(af_thread);

    if (run_preview_loop() < 0)
        exit(EXIT_FAILURE);

    exit(EXIT_SUCCESS);
}
