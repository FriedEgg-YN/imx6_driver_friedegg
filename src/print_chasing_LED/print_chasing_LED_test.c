#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CHASINGLED_IOC_MAGIC    'L'
#define CHASINGLED_IOC_START    _IO(CHASINGLED_IOC_MAGIC, 0)
#define CHASINGLED_IOC_STOP     _IO(CHASINGLED_IOC_MAGIC, 1)
#define CHASINGLED_IOC_CLOSE    _IO(CHASINGLED_IOC_MAGIC, 2)

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s <dev> start\n"
        "  %s <dev> stop\n"
        "  %s <dev> demo [run_seconds] [hold_seconds]\n"
        "\n"
        "Examples:\n"
        "  %s /dev/dummy-chasingled1 start\n"
        "  %s /dev/dummy-chasingled1 stop\n"
        "  %s /dev/dummy-chasingled2 demo 5 3\n",
        prog, prog, prog, prog, prog, prog);
}

static int send_ioctl(int fd, unsigned long request, const char *name)
{
    if (ioctl(fd, request) < 0) {
        fprintf(stderr, "ioctl %s failed: %s\n", name, strerror(errno));
        return -1;
    }

    printf("ioctl %s ok\n", name);
    return 0;
}

static unsigned int parse_seconds(const char *s, unsigned int fallback)
{
    char *end = NULL;
    unsigned long value;

    if (!s)
        return fallback;

    errno = 0;
    value = strtoul(s, &end, 10);
    if (errno || !end || *end != 0)
        return fallback;

    return (unsigned int)value;
}

int main(int argc, char *argv[])
{
    const char *devname;
    const char *cmd;
    int fd;
    int ret = 0;

    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    devname = argv[1];
    cmd = argv[2];

    fd = open(devname, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", devname, strerror(errno));
        return EXIT_FAILURE;
    }

    if (strcmp(cmd, "start") == 0) {
        ret = send_ioctl(fd, CHASINGLED_IOC_START, "START");
    } else if (strcmp(cmd, "stop") == 0) {
        ret = send_ioctl(fd, CHASINGLED_IOC_STOP, "STOP");
    } else if (strcmp(cmd, "demo") == 0) {
        unsigned int run_seconds = parse_seconds(argc > 3 ? argv[3] : NULL, 5);
        unsigned int hold_seconds = parse_seconds(argc > 4 ? argv[4] : NULL, 3);

        ret = send_ioctl(fd, CHASINGLED_IOC_START, "START");
        if (!ret) {
            printf("running for %u second(s), watch dmesg...\n", run_seconds);
            sleep(run_seconds);
            ret = send_ioctl(fd, CHASINGLED_IOC_STOP, "STOP");
        }
        if (!ret) {
            printf("holding for %u second(s), watch dmesg...\n", hold_seconds);
            sleep(hold_seconds);
        }
        close(fd);
    } else if (strcmp(cmd, "close") == 0) {
        send_ioctl(fd, CHASINGLED_IOC_CLOSE, "CLOSE");
        close(fd);
    } else {
        usage(argv[0]);
        ret = -1;
    }
    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
