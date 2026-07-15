#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <asm/termbits.h>

#include "include/friedegg/ld2410c.h"

static volatile sig_atomic_t running = 1;

static void handle_signal(int signo)
{
	(void)signo;
	running = 0;
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-d /dev/ttymxc2] [-b 256000]\n", prog);
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

int main(int argc, char **argv)
{
	const char *dev = "/dev/ttymxc2";
	unsigned int baud = LD2410C_DEFAULT_BAUD;
	int opt;
	int fd;
	int ldisc = LD2410C_LDISC;

	while ((opt = getopt(argc, argv, "d:b:h")) != -1) {
		switch (opt) {
		case 'd':
			dev = optarg;
			break;
		case 'b':
			baud = (unsigned int)strtoul(optarg, NULL, 0);
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

	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		fprintf(stderr, "TIOCSETD %d: %s\n", ldisc, strerror(errno));
		close(fd);
		return 1;
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	printf("attached %s at %u baud to line discipline %d\n", dev, baud, ldisc);
	fflush(stdout);

	while (running)
		pause();

	close(fd);
	return 0;
}
