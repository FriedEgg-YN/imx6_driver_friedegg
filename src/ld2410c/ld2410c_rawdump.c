#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <asm/termbits.h>

#include "include/friedegg/ld2410c.h"

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-d /dev/ttymxc2] [-b 256000] [-n 128] [-t 3000]\n",
		prog);
}

static int configure_uart(int fd, unsigned int baud)
{
	struct termios2 tio;

	if (ioctl(fd, TCGETS2, &tio) < 0)
		return -1;

	tio.c_iflag = 0;
	tio.c_oflag = 0;
	tio.c_lflag = 0;
	tio.c_cflag &= ~(CBAUD | CSIZE | PARENB | CSTOPB | CRTSCTS);
	tio.c_cflag |= BOTHER | CS8 | CLOCAL | CREAD;
	tio.c_ispeed = baud;
	tio.c_ospeed = baud;
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;

	return ioctl(fd, TCSETS2, &tio);
}

static void print_hex_line(unsigned int offset, const unsigned char *buf,
			   size_t len)
{
	size_t i;

	printf("%08x  ", offset);
	for (i = 0; i < 16; i++) {
		if (i < len)
			printf("%02x ", buf[i]);
		else
			printf("   ");
		if (i == 7)
			printf(" ");
	}
	printf(" |");
	for (i = 0; i < len; i++)
		putchar(buf[i] >= 0x20 && buf[i] <= 0x7e ? buf[i] : '.');
	printf("|\n");
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/ttymxc2";
	unsigned int baud = LD2410C_DEFAULT_BAUD;
	unsigned int count = 128;
	int timeout_ms = 3000;
	unsigned char line[16];
	unsigned int offset = 0;
	unsigned int line_count = 0;
	unsigned int done = 0;
	int opt;
	int fd;

	while ((opt = getopt(argc, argv, "d:b:n:t:h")) != -1) {
		switch (opt) {
		case 'd':
			dev = optarg;
			break;
		case 'b':
			baud = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'n':
			count = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 't':
			timeout_ms = (int)strtol(optarg, NULL, 0);
			break;
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	fd = open(dev, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return 1;
	}

	if (configure_uart(fd, baud) < 0) {
		fprintf(stderr, "configure %s: %s\n", dev, strerror(errno));
		close(fd);
		return 1;
	}

	fprintf(stderr, "dumping %u bytes from %s at %u baud\n", count, dev,
		baud);

	while (done < count) {
		struct pollfd pfd;
		unsigned char buf[64];
		ssize_t ret;
		unsigned int i;

		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "poll %s: %s\n", dev, strerror(errno));
			close(fd);
			return 1;
		}
		if (ret == 0) {
			fprintf(stderr, "timeout waiting for data\n");
			close(fd);
			return 2;
		}
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fprintf(stderr, "poll %s: revents=0x%x\n", dev,
				pfd.revents);
			close(fd);
			return 1;
		}
		if (!(pfd.revents & POLLIN))
			continue;

		ret = read(fd, buf, sizeof(buf));
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fprintf(stderr, "read %s: %s\n", dev, strerror(errno));
			close(fd);
			return 1;
		}
		if (!ret)
			continue;

		for (i = 0; i < (unsigned int)ret && done < count; i++) {
			line[line_count++] = buf[i];
			done++;
			if (line_count == sizeof(line) || done == count) {
				print_hex_line(offset, line, line_count);
				offset += line_count;
				line_count = 0;
			}
		}
	}

	close(fd);
	return 0;
}
