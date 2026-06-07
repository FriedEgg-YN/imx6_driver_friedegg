#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define AP3216C_NAME "ap3216c"
#define IIO_SYSFS_DIR "/sys/bus/iio/devices"
#define IIO_DEV_DIR "/dev"

#define IIO_GET_EVENT_FD_IOCTL _IOR('i', 0x90, int)

#define IIO_EVENT_CODE_EXTRACT_TYPE(mask) (((mask) >> 56) & 0xff)
#define IIO_EVENT_CODE_EXTRACT_DIR(mask) (((mask) >> 48) & 0x7f)
#define IIO_EVENT_CODE_EXTRACT_MODIFIER(mask) (((mask) >> 40) & 0xff)
#define IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(mask) (((mask) >> 32) & 0xff)
#define IIO_EVENT_CODE_EXTRACT_CHAN(mask) ((int16_t)((mask) & 0xffff))

#define IIO_EV_TYPE_THRESH 0
#define IIO_EV_DIR_RISING 1
#define IIO_EV_DIR_FALLING 2
#define IIO_LIGHT 6
#define IIO_INTENSITY 7
#define IIO_PROXIMITY 8
#define IIO_MOD_LIGHT_IR 14

#define AP3216C_ALS_MAX_VALUE 65535
#define AP3216C_PS_MAX_VALUE 1023
#define AP3216C_EVENT_BASELINE_SETTLE_US 250000
#define AP3216C_ALS_EVENT_MARGIN 2048
#define AP3216C_PS_EVENT_MARGIN 96
#define AP3216C_EVENT_LIMIT 32

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct iio_event_data {
	uint64_t id;
	int64_t timestamp;
};

static volatile sig_atomic_t stop_eventtest;

static void handle_eventtest_signal(int sig)
{
	(void)sig;
	stop_eventtest = 1;
}

static void install_eventtest_signal_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_eventtest_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

static const char * const als_raw_attrs[] = {
	"in_illuminance_raw",
	"in_illuminance0_raw",
};

static const char * const als_scale_attrs[] = {
	"in_illuminance_scale",
	"in_illuminance0_scale",
};

static const char * const als_scale_available_attrs[] = {
	"in_illuminance_scale_available",
	"in_illuminance0_scale_available",
};

static const char * const als_calibscale_attrs[] = {
	"in_illuminance_calibscale",
	"in_illuminance0_calibscale",
};

static const char * const ir_raw_attrs[] = {
	"in_intensity_ir_raw",
	"in_intensity0_ir_raw",
};

static const char * const ps_raw_attrs[] = {
	"in_proximity_raw",
	"in_proximity0_raw",
};

static const char * const als_rising_value_attrs[] = {
	"events/in_illuminance_thresh_rising_value",
	"events/in_illuminance0_thresh_rising_value",
};

static const char * const als_falling_value_attrs[] = {
	"events/in_illuminance_thresh_falling_value",
	"events/in_illuminance0_thresh_falling_value",
};

static const char * const als_rising_en_attrs[] = {
	"events/in_illuminance_thresh_rising_en",
	"events/in_illuminance0_thresh_rising_en",
};

static const char * const als_falling_en_attrs[] = {
	"events/in_illuminance_thresh_falling_en",
	"events/in_illuminance0_thresh_falling_en",
};

static const char * const ps_rising_value_attrs[] = {
	"events/in_proximity_thresh_rising_value",
	"events/in_proximity0_thresh_rising_value",
};

static const char * const ps_falling_value_attrs[] = {
	"events/in_proximity_thresh_falling_value",
	"events/in_proximity0_thresh_falling_value",
};

static const char * const ps_rising_en_attrs[] = {
	"events/in_proximity_thresh_rising_en",
	"events/in_proximity0_thresh_rising_en",
};

static const char * const ps_falling_en_attrs[] = {
	"events/in_proximity_thresh_falling_en",
	"events/in_proximity0_thresh_falling_en",
};

static void trim_line(char *s)
{
	size_t len;

	len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
			   s[len - 1] == ' ' || s[len - 1] == '\t')) {
		s[len - 1] = '\0';
		len--;
	}
}

static int make_path(char *path, size_t size, const char *dir,
		     const char *name)
{
	int ret;

	ret = snprintf(path, size, "%s/%s", dir, name);
	if (ret < 0 || (size_t)ret >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}

	return 0;
}

static int path_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

static int attr_exists(const char *dir, const char *attr)
{
	char path[256];

	if (make_path(path, sizeof(path), dir, attr) < 0)
		return 0;

	return path_exists(path);
}

static int read_attr(const char *dir, const char *attr, char *buf, size_t size)
{
	char path[256];
	FILE *fp;

	if (make_path(path, sizeof(path), dir, attr) < 0)
		return -1;

	fp = fopen(path, "r");
	if (!fp)
		return -1;

	errno = 0;
	if (!fgets(buf, size, fp)) {
		if (!errno)
			errno = EIO;
		fclose(fp);
		return -1;
	}

	fclose(fp);
	trim_line(buf);
	return 0;
}

static int write_attr(const char *dir, const char *attr, const char *value)
{
	char path[256];
	FILE *fp;

	if (make_path(path, sizeof(path), dir, attr) < 0)
		return -1;

	fp = fopen(path, "w");
	if (!fp)
		return -1;

	if (fputs(value, fp) == EOF) {
		fclose(fp);
		return -1;
	}

	if (fclose(fp) != 0)
		return -1;

	return 0;
}

static const char *find_existing_attr(const char *dir,
				      const char * const *attrs,
				      size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (attr_exists(dir, attrs[i]))
			return attrs[i];
	}

	return attrs[0];
}

static int read_attr_any(const char *dir, const char * const *attrs,
			 size_t count, char *buf, size_t size)
{
	const char *attr;

	attr = find_existing_attr(dir, attrs, count);
	return read_attr(dir, attr, buf, size);
}

static int write_attr_any(const char *dir, const char * const *attrs,
			  size_t count, const char *value)
{
	const char *attr;

	attr = find_existing_attr(dir, attrs, count);
	return write_attr(dir, attr, value);
}

static void print_read_result(const char *dir, const char *label,
			      const char * const *attrs, size_t count)
{
	char buf[128];
	const char *attr;

	attr = find_existing_attr(dir, attrs, count);
	if (read_attr(dir, attr, buf, sizeof(buf)) < 0) {
		printf("%-28s ERR  %s (%s)\n", label, attr, strerror(errno));
		return;
	}

	printf("%-28s %-12s %s\n", label, attr, buf);
}

static int parse_iio_index_from_name(const char *name)
{
	int index;

	if (sscanf(name, "iio:device%d", &index) == 1)
		return index;

	return -1;
}

static int parse_iio_index_from_path(const char *path)
{
	const char *base;

	base = strrchr(path, '/');
	if (base)
		base++;
	else
		base = path;

	return parse_iio_index_from_name(base);
}

static int make_iio_sysfs_dir(char *out, size_t size, int index)
{
	int ret;

	ret = snprintf(out, size, "%s/iio:device%d", IIO_SYSFS_DIR, index);
	if (ret < 0 || (size_t)ret >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}

	return 0;
}

static int make_iio_dev_path(char *out, size_t size, int index)
{
	int ret;

	ret = snprintf(out, size, "%s/iio:device%d", IIO_DEV_DIR, index);
	if (ret < 0 || (size_t)ret >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}

	return 0;
}

static int find_iio_by_name(const char *name, char *out, size_t size)
{
	DIR *dp;
	struct dirent *de;
	char path[256];
	char read_name[128];

	dp = opendir(IIO_SYSFS_DIR);
	if (!dp)
		return -1;

	while ((de = readdir(dp)) != NULL) {
		if (strncmp(de->d_name, "iio:device", 10) != 0)
			continue;

		if (make_iio_sysfs_dir(path, sizeof(path),
				       parse_iio_index_from_name(de->d_name)) < 0)
			continue;

		if (read_attr(path, "name", read_name, sizeof(read_name)) < 0)
			continue;

		if (strcmp(read_name, name) == 0) {
			closedir(dp);
			if (snprintf(out, size, "%s", path) >= (int)size) {
				errno = ENAMETOOLONG;
				return -1;
			}
			return 0;
		}
	}

	closedir(dp);
	errno = ENODEV;
	return -1;
}

static int resolve_device(const char *arg, char *out, size_t size)
{
	int index;
	char *endp;
	long val;

	if (!arg || strcmp(arg, "auto") == 0)
		return find_iio_by_name(AP3216C_NAME, out, size);

	if (strncmp(arg, "/sys/", 5) == 0) {
		if (snprintf(out, size, "%s", arg) >= (int)size) {
			errno = ENAMETOOLONG;
			return -1;
		}
		return 0;
	}

	if (strncmp(arg, "/dev/iio:device", 15) == 0) {
		index = parse_iio_index_from_path(arg);
		if (index < 0) {
			errno = EINVAL;
			return -1;
		}
		return make_iio_sysfs_dir(out, size, index);
	}

	if (strncmp(arg, "iio:device", 10) == 0) {
		index = parse_iio_index_from_name(arg);
		if (index < 0) {
			errno = EINVAL;
			return -1;
		}
		return make_iio_sysfs_dir(out, size, index);
	}

	errno = 0;
	val = strtol(arg, &endp, 10);
	if (errno == 0 && endp && *endp == '\0' && val >= 0)
		return make_iio_sysfs_dir(out, size, (int)val);

	return find_iio_by_name(arg, out, size);
}

static int run_scan(void)
{
	DIR *dp;
	struct dirent *de;
	char path[256];
	char name[128];
	int found = 0;
	int index;

	dp = opendir(IIO_SYSFS_DIR);
	if (!dp) {
		printf("open %s failed: %s\n", IIO_SYSFS_DIR, strerror(errno));
		return 1;
	}

	while ((de = readdir(dp)) != NULL) {
		if (strncmp(de->d_name, "iio:device", 10) != 0)
			continue;

		index = parse_iio_index_from_name(de->d_name);
		if (index < 0)
			continue;

		if (make_iio_sysfs_dir(path, sizeof(path), index) < 0)
			continue;

		if (read_attr(path, "name", name, sizeof(name)) < 0)
			snprintf(name, sizeof(name), "<unknown>");

		printf("%s  name=%s%s\n", de->d_name, name,
		       strcmp(name, AP3216C_NAME) == 0 ? "  *" : "");
		if (strcmp(name, AP3216C_NAME) == 0)
			found = 1;
	}

	closedir(dp);
	return found ? 0 : 1;
}

static int run_read(const char *dir)
{
	char name[128];

	if (read_attr(dir, "name", name, sizeof(name)) < 0) {
		printf("read %s/name failed: %s\n", dir, strerror(errno));
		return 1;
	}

	printf("device: %s (%s)\n", dir, name);
	print_read_result(dir, "operating_mode", (const char *[]) {
		"operating_mode",
	}, 1);
	print_read_result(dir, "als_raw", als_raw_attrs, ARRAY_SIZE(als_raw_attrs));
	print_read_result(dir, "als_scale", als_scale_attrs, ARRAY_SIZE(als_scale_attrs));
	print_read_result(dir, "als_calibscale", als_calibscale_attrs,
			  ARRAY_SIZE(als_calibscale_attrs));
	print_read_result(dir, "ir_raw", ir_raw_attrs, ARRAY_SIZE(ir_raw_attrs));
	print_read_result(dir, "ps_raw", ps_raw_attrs, ARRAY_SIZE(ps_raw_attrs));

	return 0;
}

static int check_read(const char *dir, const char *label,
		      const char * const *attrs, size_t count)
{
	char buf[128];

	if (read_attr_any(dir, attrs, count, buf, sizeof(buf)) < 0) {
		printf("[FAIL] read %-28s %s\n", label, strerror(errno));
		return 1;
	}

	printf("[PASS] read %-28s %s\n", label, buf);
	return 0;
}

static int check_write(const char *dir, const char *label,
		       const char * const *attrs, size_t count,
		       const char *value)
{
	if (write_attr_any(dir, attrs, count, value) < 0) {
		printf("[FAIL] write %-27s %s\n", label, strerror(errno));
		return 1;
	}

	printf("[PASS] write %-27s %s", label, value);
	if (value[0] && value[strlen(value) - 1] != '\n')
		printf("\n");
	return 0;
}

static void restore_attr(const char *dir, const char *attr, const char *value,
			 int valid)
{
	if (!valid)
		return;

	if (write_attr(dir, attr, value) < 0)
		printf("[WARN] restore %s failed: %s\n", attr, strerror(errno));
}

static int run_fulltest(const char *dir)
{
	const char *scale_attr;
	const char *calib_attr;
	char name[128];
	char saved_mode[128];
	char saved_scale[128];
	char saved_calib[128];
	int have_mode = 0;
	int have_scale = 0;
	int have_calib = 0;
	int failures = 0;
	size_t i;
	static const char * const scales[] = {
		"0.350000\n",
		"0.078800\n",
		"0.019700\n",
		"0.004900\n",
	};

	scale_attr = find_existing_attr(dir, als_scale_attrs,
					ARRAY_SIZE(als_scale_attrs));
	calib_attr = find_existing_attr(dir, als_calibscale_attrs,
					ARRAY_SIZE(als_calibscale_attrs));

	if (read_attr(dir, "name", name, sizeof(name)) < 0) {
		printf("[FAIL] read device name: %s\n", strerror(errno));
		return 1;
	}
	failures += strcmp(name, AP3216C_NAME) != 0;
	printf("[%s] name == %s (%s)\n",
	       strcmp(name, AP3216C_NAME) == 0 ? "PASS" : "FAIL",
	       AP3216C_NAME, name);

	have_mode = read_attr(dir, "operating_mode", saved_mode,
			      sizeof(saved_mode)) == 0;
	have_scale = read_attr(dir, scale_attr, saved_scale,
			       sizeof(saved_scale)) == 0;
	have_calib = read_attr(dir, calib_attr, saved_calib,
			       sizeof(saved_calib)) == 0;

	failures += check_read(dir, "operating_mode_available",
			       (const char *[]) { "operating_mode_available" },
			       1);
	failures += check_write(dir, "operating_mode",
				(const char *[]) { "operating_mode" }, 1,
				"als_ps_ir\n");
	failures += check_read(dir, "operating_mode",
			       (const char *[]) { "operating_mode" }, 1);
	failures += check_read(dir, "als_scale_available",
			       als_scale_available_attrs,
			       ARRAY_SIZE(als_scale_available_attrs));

	for (i = 0; i < ARRAY_SIZE(scales); i++) {
		failures += check_write(dir, "als_scale", als_scale_attrs,
					ARRAY_SIZE(als_scale_attrs), scales[i]);
		failures += check_read(dir, "als_scale", als_scale_attrs,
				       ARRAY_SIZE(als_scale_attrs));
	}

	failures += check_write(dir, "als_calibscale", als_calibscale_attrs,
				ARRAY_SIZE(als_calibscale_attrs), "1.000000\n");
	failures += check_read(dir, "als_calibscale", als_calibscale_attrs,
			       ARRAY_SIZE(als_calibscale_attrs));
	failures += check_read(dir, "als_raw", als_raw_attrs,
			       ARRAY_SIZE(als_raw_attrs));
	failures += check_read(dir, "ir_raw", ir_raw_attrs,
			       ARRAY_SIZE(ir_raw_attrs));
	failures += check_read(dir, "ps_raw", ps_raw_attrs,
			       ARRAY_SIZE(ps_raw_attrs));

	failures += check_write(dir, "als_falling_value",
				als_falling_value_attrs,
				ARRAY_SIZE(als_falling_value_attrs), "0\n");
	failures += check_write(dir, "als_rising_value",
				als_rising_value_attrs,
				ARRAY_SIZE(als_rising_value_attrs), "65535\n");
	failures += check_write(dir, "ps_falling_value",
				ps_falling_value_attrs,
				ARRAY_SIZE(ps_falling_value_attrs), "100\n");
	failures += check_write(dir, "ps_rising_value",
				ps_rising_value_attrs,
				ARRAY_SIZE(ps_rising_value_attrs), "200\n");
	failures += check_write(dir, "als_rising_en",
				als_rising_en_attrs,
				ARRAY_SIZE(als_rising_en_attrs), "0\n");
	failures += check_write(dir, "als_falling_en",
				als_falling_en_attrs,
				ARRAY_SIZE(als_falling_en_attrs), "0\n");
	failures += check_write(dir, "ps_rising_en",
				ps_rising_en_attrs,
				ARRAY_SIZE(ps_rising_en_attrs), "0\n");
	failures += check_write(dir, "ps_falling_en",
				ps_falling_en_attrs,
				ARRAY_SIZE(ps_falling_en_attrs), "0\n");

	restore_attr(dir, calib_attr, saved_calib, have_calib);
	restore_attr(dir, scale_attr, saved_scale, have_scale);
	restore_attr(dir, "operating_mode", saved_mode, have_mode);

	if (failures)
		printf("fulltest: %d failure(s)\n", failures);
	else
		printf("fulltest: all checks passed\n");

	return failures ? 1 : 0;
}

static const char *chan_type_name(unsigned int type)
{
	switch (type) {
	case IIO_LIGHT:
		return "light";
	case IIO_INTENSITY:
		return "intensity";
	case IIO_PROXIMITY:
		return "proximity";
	default:
		return "unknown";
	}
}

static const char *event_type_name(unsigned int type)
{
	switch (type) {
	case IIO_EV_TYPE_THRESH:
		return "thresh";
	default:
		return "unknown";
	}
}

static const char *event_dir_name(unsigned int dir)
{
	switch (dir) {
	case IIO_EV_DIR_RISING:
		return "rising";
	case IIO_EV_DIR_FALLING:
		return "falling";
	default:
		return "unknown";
	}
}

static const char *modifier_name(unsigned int modifier)
{
	switch (modifier) {
	case 0:
		return "none";
	case IIO_MOD_LIGHT_IR:
		return "light_ir";
	default:
		return "unknown";
	}
}

static void print_event(const struct iio_event_data *event)
{
	unsigned int type = IIO_EVENT_CODE_EXTRACT_TYPE(event->id);
	unsigned int dir = IIO_EVENT_CODE_EXTRACT_DIR(event->id);
	unsigned int chan_type = IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event->id);
	unsigned int modifier = IIO_EVENT_CODE_EXTRACT_MODIFIER(event->id);
	int chan = IIO_EVENT_CODE_EXTRACT_CHAN(event->id);

	printf("event: id=0x%016llx ts=%lld type=%s dir=%s chan=%s%d mod=%s\n",
	       (unsigned long long)event->id,
	       (long long)event->timestamp,
	       event_type_name(type), event_dir_name(dir),
	       chan_type_name(chan_type), chan, modifier_name(modifier));
}

static int read_uint_any(const char *dir, const char * const *attrs,
			 size_t count, unsigned int *value)
{
	char buf[128];
	char *endp;
	unsigned long val;

	if (read_attr_any(dir, attrs, count, buf, sizeof(buf)) < 0)
		return -1;

	errno = 0;
	val = strtoul(buf, &endp, 10);
	if (errno || !endp || *endp != '\0' || val > 0xffffffffUL) {
		errno = EINVAL;
		return -1;
	}

	*value = (unsigned int)val;
	return 0;
}

static int write_uint_any(const char *dir, const char * const *attrs,
			  size_t count, unsigned int value)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%u\n", value);
	return write_attr_any(dir, attrs, count, buf);
}

static int parse_interval_ms(const char *s)
{
	char *endp;
	long val;

	if (!s)
		return 500;

	errno = 0;
	val = strtol(s, &endp, 10);
	if (errno || !endp || *endp != '\0' || val <= 0 || val > 60000)
		return -1;

	return (int)val;
}

static int run_readloop(const char *dir, int interval_ms)
{
	char name[128];
	char mode[128];
	unsigned int als;
	unsigned int ir;
	unsigned int ps;
	unsigned int seq = 0;

	if (read_attr(dir, "name", name, sizeof(name)) < 0) {
		printf("read %s/name failed: %s\n", dir, strerror(errno));
		return 1;
	}

	if (write_attr(dir, "operating_mode", "als_ps_ir\n") < 0) {
		printf("set operating_mode=als_ps_ir failed: %s\n",
		       strerror(errno));
		return 1;
	}

	if (read_attr(dir, "operating_mode", mode, sizeof(mode)) < 0)
		snprintf(mode, sizeof(mode), "<unknown>");

	printf("device: %s (%s), operating_mode=%s, interval=%d ms\n",
	       dir, name, mode, interval_ms);
	printf("press Ctrl-C to stop\n");
	printf("%-8s %-12s %-12s %-12s\n", "seq", "als_raw", "ir_raw",
	       "ps_raw");
	fflush(stdout);

	stop_eventtest = 0;
	install_eventtest_signal_handlers();

	while (!stop_eventtest) {
		if (read_uint_any(dir, als_raw_attrs,
				  ARRAY_SIZE(als_raw_attrs), &als) < 0) {
			printf("[ERR] read ALS raw failed: %s\n", strerror(errno));
			return 1;
		}
		if (read_uint_any(dir, ir_raw_attrs,
				  ARRAY_SIZE(ir_raw_attrs), &ir) < 0) {
			printf("[ERR] read IR raw failed: %s\n", strerror(errno));
			return 1;
		}
		if (read_uint_any(dir, ps_raw_attrs,
				  ARRAY_SIZE(ps_raw_attrs), &ps) < 0) {
			printf("[ERR] read PS raw failed: %s\n", strerror(errno));
			return 1;
		}

		printf("%-8u %-12u %-12u %-12u\n", seq++, als, ir, ps);
		fflush(stdout);
		usleep((useconds_t)interval_ms * 1000);
	}

	printf("readloop: stopped\n");
	return 0;
}

static unsigned int threshold_low(unsigned int current, unsigned int margin)
{
	return current > margin ? current - margin : 0;
}

static unsigned int threshold_high(unsigned int current, unsigned int margin,
				   unsigned int max)
{
	if (margin >= max || current >= max - margin)
		return max;

	return current + margin;
}

static int read_fresh_uint_any(const char *dir, const char * const *attrs,
			       size_t count, unsigned int *value)
{
	unsigned int ignored;

	read_uint_any(dir, attrs, count, &ignored);
	usleep(AP3216C_EVENT_BASELINE_SETTLE_US);

	if (stop_eventtest) {
		errno = EINTR;
		return -1;
	}

	return read_uint_any(dir, attrs, count, value);
}

static void disable_eventtest_events(const char *dir)
{
	write_attr_any(dir, als_rising_en_attrs,
		       ARRAY_SIZE(als_rising_en_attrs), "0\n");
	write_attr_any(dir, als_falling_en_attrs,
		       ARRAY_SIZE(als_falling_en_attrs), "0\n");
	write_attr_any(dir, ps_rising_en_attrs,
		       ARRAY_SIZE(ps_rising_en_attrs), "0\n");
	write_attr_any(dir, ps_falling_en_attrs,
		       ARRAY_SIZE(ps_falling_en_attrs), "0\n");
	if (stop_eventtest)
		return;

	write_uint_any(dir, als_falling_value_attrs,
		       ARRAY_SIZE(als_falling_value_attrs), 0);
	if (stop_eventtest)
		return;
	write_uint_any(dir, als_rising_value_attrs,
		       ARRAY_SIZE(als_rising_value_attrs), AP3216C_ALS_MAX_VALUE);
	if (stop_eventtest)
		return;
	write_uint_any(dir, ps_falling_value_attrs,
		       ARRAY_SIZE(ps_falling_value_attrs), 0);
	if (stop_eventtest)
		return;
	write_uint_any(dir, ps_rising_value_attrs,
		       ARRAY_SIZE(ps_rising_value_attrs), AP3216C_PS_MAX_VALUE);
}

static int configure_eventtest_thresholds(const char *dir)
{
	unsigned int als = 0;
	unsigned int ps = 0;
	unsigned int als_low = 0;
	unsigned int als_high = AP3216C_ALS_MAX_VALUE;
	unsigned int ps_low = 0;
	unsigned int ps_high = AP3216C_PS_MAX_VALUE;
	int ret = 0;
	int enabled = 0;

	disable_eventtest_events(dir);
	if (stop_eventtest)
		return -EINTR;

	if (write_attr(dir, "operating_mode", "als_ps_ir\n") < 0)
		printf("[WARN] set operating_mode failed: %s\n", strerror(errno));
	if (stop_eventtest)
		return -EINTR;

	if (read_fresh_uint_any(dir, als_raw_attrs,
				ARRAY_SIZE(als_raw_attrs), &als) == 0) {
		als_low = threshold_low(als, AP3216C_ALS_EVENT_MARGIN);
		als_high = threshold_high(als, AP3216C_ALS_EVENT_MARGIN,
					  AP3216C_ALS_MAX_VALUE);
		ret += write_uint_any(dir, als_falling_value_attrs,
				      ARRAY_SIZE(als_falling_value_attrs),
				      als_low) < 0;
		ret += write_uint_any(dir, als_rising_value_attrs,
				      ARRAY_SIZE(als_rising_value_attrs),
				      als_high) < 0;
		printf("ALS event window: current=%u low=%u high=%u\n",
		       als, als_low, als_high);
	} else {
		printf("[WARN] read ALS raw failed: %s\n", strerror(errno));
	}
	if (stop_eventtest)
		return -EINTR;

	if (read_fresh_uint_any(dir, ps_raw_attrs,
				ARRAY_SIZE(ps_raw_attrs), &ps) == 0) {
		// ps_low = threshold_low(ps, AP3216C_PS_EVENT_MARGIN);
		// ps_high = threshold_high(ps, AP3216C_PS_EVENT_MARGIN,
		// 			 AP3216C_PS_MAX_VALUE);
		ps_low = 100;
		ps_high = 200;
		ret += write_uint_any(dir, ps_falling_value_attrs,
				      ARRAY_SIZE(ps_falling_value_attrs),
				      ps_low) < 0;
		ret += write_uint_any(dir, ps_rising_value_attrs,
				      ARRAY_SIZE(ps_rising_value_attrs),
				      ps_high) < 0;
		printf("PS event window: current=%u low=%u high=%u\n",
		       ps, ps_low, ps_high);
	} else {
		printf("[WARN] read PS raw failed: %s\n", strerror(errno));
	}
	if (stop_eventtest)
		return -EINTR;

	if (als_high < AP3216C_ALS_MAX_VALUE) {
		if (write_attr_any(dir, als_rising_en_attrs,
				   ARRAY_SIZE(als_rising_en_attrs), "1\n") < 0)
			printf("[WARN] enable ALS rising event failed: %s\n",
			       strerror(errno));
		else
			enabled++;
	}
	if (als_low > 0) {
		if (write_attr_any(dir, als_falling_en_attrs,
				   ARRAY_SIZE(als_falling_en_attrs), "1\n") < 0)
			printf("[WARN] enable ALS falling event failed: %s\n",
			       strerror(errno));
		else
			enabled++;
	}
	if (ps_high < AP3216C_PS_MAX_VALUE) {
		if (write_attr_any(dir, ps_rising_en_attrs,
				   ARRAY_SIZE(ps_rising_en_attrs), "1\n") < 0)
			printf("[WARN] enable PS rising event failed: %s\n",
			       strerror(errno));
		else
			enabled++;
	}
	if (ps_low > 0) {
		if (write_attr_any(dir, ps_falling_en_attrs,
				   ARRAY_SIZE(ps_falling_en_attrs), "1\n") < 0)
			printf("[WARN] enable PS falling event failed: %s\n",
			       strerror(errno));
		else
			enabled++;
	}

	if (!enabled) {
		printf("[WARN] no event direction was enabled\n");
		ret++;
	}

	return ret ? -1 : 0;
}

static int run_eventtest(const char *dir, int seconds)
{
	char dev_path[256];
	struct pollfd pfd;
	struct iio_event_data event;
	time_t deadline;
	int dev_index;
	int fd = -1;
	int event_fd = -1;
	int ret;
	int events = 0;
	int rc = 1;

	dev_index = parse_iio_index_from_path(dir);
	if (dev_index < 0) {
		printf("cannot derive /dev/iio:deviceX from %s\n", dir);
		return 1;
	}

	stop_eventtest = 0;
	install_eventtest_signal_handlers();

	if (make_iio_dev_path(dev_path, sizeof(dev_path), dev_index) < 0) {
		printf("make dev path failed: %s\n", strerror(errno));
		goto out_cleanup;
	}

	fd = open(dev_path, O_RDONLY);
	if (fd < 0) {
		printf("open %s failed: %s\n", dev_path, strerror(errno));
		goto out_cleanup;
	}

	ret = ioctl(fd, IIO_GET_EVENT_FD_IOCTL, &event_fd);
	close(fd);
	fd = -1;
	if (ret < 0 || event_fd < 0) {
		printf("get event fd failed: %s\n", strerror(errno));
		goto out_cleanup;
	}

	if (configure_eventtest_thresholds(dir) < 0)
		printf("[WARN] threshold setup was incomplete\n");
	if (stop_eventtest)
		goto out_cleanup;

	printf("waiting for AP3216C IIO events on %s for %d seconds\n",
	       dev_path, seconds);
	printf("change ambient light or proximity state to cross the window\n");
	deadline = time(NULL) + seconds;

	while (!stop_eventtest && time(NULL) < deadline &&
	       events < AP3216C_EVENT_LIMIT) {
		int timeout_ms = (int)(deadline - time(NULL)) * 1000;

		if (timeout_ms <= 0)
			break;

		pfd.fd = event_fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0) {
			if (errno == EINTR && stop_eventtest)
				break;
			if (errno == EINTR)
				continue;
			printf("poll event fd failed: %s\n", strerror(errno));
			goto out_cleanup;
		}
		if (ret == 0)
			break;

		ret = read(event_fd, &event, sizeof(event));
		if (ret < 0) {
			if (errno == EINTR && stop_eventtest)
				break;
			printf("read event fd failed: %s\n", strerror(errno));
			goto out_cleanup;
		}
		if (ret != (int)sizeof(event)) {
			printf("short event read: %d bytes\n", ret);
			goto out_cleanup;
		}

		print_event(&event);
		events++;
	}

	if (!events)
		printf("eventtest: no event received\n");
	else
		printf("eventtest: received %d event(s)\n", events);

	if (events >= AP3216C_EVENT_LIMIT)
		printf("eventtest: event limit reached, disabling events\n");
	if (stop_eventtest)
		printf("eventtest: interrupted, disabling events\n");

	rc = events ? 0 : 1;

out_cleanup:
	if (event_fd >= 0)
		close(event_fd);
	if (fd >= 0)
		close(fd);
	disable_eventtest_events(dir);

	return rc;
}

static int parse_seconds(const char *s)
{
	char *endp;
	long val;

	if (!s)
		return 10;

	errno = 0;
	val = strtol(s, &endp, 10);
	if (errno || !endp || *endp != '\0' || val <= 0 || val > 3600)
		return -1;

	return (int)val;
}

static void usage(const char *prog)
{
	printf("usage: %s scan\n", prog);
	printf("       %s read [auto|iio:deviceX|N|/sys/...]\n", prog);
	printf("       %s readloop [auto|iio:deviceX|N|/sys/...] [interval_ms]\n", prog);
	printf("       %s fulltest [auto|iio:deviceX|N|/sys/...]\n", prog);
	printf("       %s eventoff [auto|iio:deviceX|N|/sys/...]\n", prog);
	printf("       %s eventtest [auto|iio:deviceX|N|/sys/...] [seconds]\n", prog);
}

int main(int argc, char **argv)
{
	char dir[256];
	const char *cmd;
	const char *dev_arg = NULL;
	int seconds;
	int interval_ms;

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	cmd = argv[1];
	if (strcmp(cmd, "scan") == 0)
		return run_scan();

	if (strcmp(cmd, "read") != 0 &&
	    strcmp(cmd, "readloop") != 0 &&
	    strcmp(cmd, "fulltest") != 0 &&
	    strcmp(cmd, "eventoff") != 0 &&
	    strcmp(cmd, "eventtest") != 0) {
		usage(argv[0]);
		return 1;
	}

	if (argc >= 3)
		dev_arg = argv[2];

	if (resolve_device(dev_arg, dir, sizeof(dir)) < 0) {
		printf("resolve AP3216C IIO device failed: %s\n", strerror(errno));
		return 1;
	}

	if (strcmp(cmd, "read") == 0)
		return run_read(dir);
	if (strcmp(cmd, "readloop") == 0) {
		interval_ms = parse_interval_ms(argc >= 4 ? argv[3] : NULL);
		if (interval_ms < 0) {
			usage(argv[0]);
			return 1;
		}
		return run_readloop(dir, interval_ms);
	}
	if (strcmp(cmd, "fulltest") == 0)
		return run_fulltest(dir);
	if (strcmp(cmd, "eventoff") == 0) {
		disable_eventtest_events(dir);
		printf("AP3216C IIO events disabled on %s\n", dir);
		return 0;
	}

	seconds = parse_seconds(argc >= 4 ? argv[3] : NULL);
	if (seconds < 0) {
		usage(argv[0]);
		return 1;
	}

	return run_eventtest(dir, seconds);
}
