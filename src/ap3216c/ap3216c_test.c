#include "stdio.h"
#include "errno.h"
#include "fcntl.h"
#include "ap3216c.h"

int main(int argc, char *argv[])
{
    int fd;
    unsigned short buf[3];
    unsigned short ir, als, ps;
    int ret;
    char *filename = argv[1];

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        if (errno == EBUSY) {
            printf("device busy\n");
            return -1;
        }
        printf("can't open file\n");
        return -1;
    }

    while(1) {
        ret = read(fd, buf, sizeof(buf));
        if (ret == (int) sizeof(buf)) {
            ir = buf[0];
            als = buf[1];
            ps = buf[2];
            printf("ir = %u, als = %u, ps = %u\n", ir, als, ps);
        } else if (ret < 0) {
            printf("read failed: %s", strerror(errno));
            break;
        } else {
            printf("short read: %d bytes (expect %zu)", ret, sizeof(buf));
        }
        usleep(200000);
    }
    close(fd);
    return 0;
}