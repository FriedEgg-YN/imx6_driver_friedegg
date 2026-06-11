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
#define IIO_MOD_LIGHT_IR 13

#define AP3216C_ALS_MAX_VALUE 65535U
#define AP3216C_PS_MAX_VALUE 1023U
#define DEFAULT_IRQ_SECONDS 30
#define DEFAULT_IRQ_EVENT_LIMIT 128
#define TEST2_GROUPS 10
#define CELL_LEN 128

#define BIT(n) (1U << (n))
#define EVENT_RISING BIT(0)
#define EVENT_FALLING BIT(1)
#define EVENT_BOTH (EVENT_RISING | EVENT_FALLING)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct iio_event_data {
	uint64_t id;
	int64_t timestamp;
};

struct attr_ref {
	const char * const *names;
	size_t count;
};

struct saved_attr {
	char attr[128];
	char value[128];
	int valid;
};

struct table_row {
	const char *name;
	char cells[TEST2_GROUPS][CELL_LEN];
};

struct irq_config {
	int seconds;
	unsigned int event_limit;
	unsigned int als_mask;
	unsigned int ps_mask;
	unsigned int als_low;
	unsigned int als_high;
	unsigned int ps_low;
	unsigned int ps_high;
	int have_als_low;
	int have_als_high;
	int have_ps_low;
	int have_ps_high;
	const char *ps_algo;
};

static volatile sig_atomic_t stop_requested;

static const char * const name_attrs[] = { "name" };
static const char * const mode_attrs[] = { "operating_mode" };
static const char * const mode_available_attrs[] = { "operating_mode_available" };
static const char * const als_raw_attrs[] = {
	"in_illuminance_raw",
	"in_illuminance0_raw",
};
static const char * const als_processed_attrs[] = {
	"in_illuminance_input",
	"in_illuminance0_input",
};
static const char * const als_scale_attrs[] = {
	"in_illuminance_scale",
	"in_illuminance0_scale",
};
static const char * const ir_raw_attrs[] = {
	"in_intensity_ir_raw",
	"in_intensity0_ir_raw",
};
static const char * const ps_raw_attrs[] = {
	"in_proximity_raw",
	"in_proximity0_raw",
};
static const char * const ps_algo_attrs[] = {
	"in_proximity_interrupt_algorithm",
	"in_proximity0_interrupt_algorithm",
};
static const char * const ps_algo_available_attrs[] = {
	"in_proximity_interrupt_algorithm_available",
	"in_proximity0_interrupt_algorithm_available",
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

static const char * const mode_names[] = {
	"power_down",
	"als",
	"ps_ir",
	"als_ps_ir",
};

static const char * const als_scale_values[] = {
	"0.315000",
	"0.078800",
	"0.019700",
	"0.004900",
};

static void handle_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static void install_signal_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

static void trim_line(char *s)
{
	size_t len = strlen(s);

	while (len > 0) {
		char c = s[len - 1];

		if (c != '\n' && c != '\r' && c != ' ' && c != '\t')
			break;
		s[--len] = '\0';
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
	char path[512];

	if (make_path(path, sizeof(path), dir, attr) < 0)
		return 0;

	return path_exists(path);
}

static int read_attr(const char *dir, const char *attr, char *buf, size_t size)
{
	char path[512];
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
	char path[512];
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
	const char *attr = find_existing_attr(dir, attrs, count);

	return read_attr(dir, attr, buf, size);
}

static int write_attr_any(const char *dir, const char * const *attrs,
			  size_t count, const char *value)
{
	const char *attr = find_existing_attr(dir, attrs, count);

	return write_attr(dir, attr, value);
}

static void save_attr_any(const char *dir, const char * const *attrs,
			  size_t count, struct saved_attr *saved)
{
	const char *attr = find_existing_attr(dir, attrs, count);

	memset(saved, 0, sizeof(*saved));
	snprintf(saved->attr, sizeof(saved->attr), "%s", attr);
	if (read_attr(dir, attr, saved->value, sizeof(saved->value)) == 0)
		saved->valid = 1;
}

static void restore_attr(const char *dir, const struct saved_attr *saved)
{
	if (!saved->valid)
		return;

	if (write_attr(dir, saved->attr, saved->value) < 0)
		printf("[WARN] restore %s failed: %s\n", saved->attr,
		       strerror(errno));
}

static int write_uint_any(const char *dir, const char * const *attrs,
			  size_t count, unsigned int value)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%u\n", value);
	return write_attr_any(dir, attrs, count, buf);
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

static void cell_from_errno(char *cell, size_t size, int err)
{
	snprintf(cell, size, "ERR:%d %s", err, strerror(err));
}

static void read_cell(const char *dir, const struct attr_ref *ref,
		      char *cell, size_t size)
{
	if (read_attr_any(dir, ref->names, ref->count, cell, size) < 0)
		cell_from_errno(cell, size, errno);
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
	const char *base = strrchr(path, '/');

	return parse_iio_index_from_name(base ? base + 1 : path);
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
		int index;

		if (strncmp(de->d_name, "iio:device", 10) != 0)
			continue;

		index = parse_iio_index_from_name(de->d_name);
		if (index < 0)
			continue;
		if (make_iio_sysfs_dir(path, sizeof(path), index) < 0)
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

	dp = opendir(IIO_SYSFS_DIR);
	if (!dp) {
		printf("open %s failed: %s\n", IIO_SYSFS_DIR, strerror(errno));
		return 1;
	}

	while ((de = readdir(dp)) != NULL) {
		int index;

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

static int set_event_enable(const char *dir, const char * const *attrs,
			    size_t count, int enable, const char *label,
			    int verbose)
{
	if (write_attr_any(dir, attrs, count, enable ? "1\n" : "0\n") == 0)
		return 0;

	if (verbose)
		printf("[WARN] set %s=%d failed: %s\n", label, enable,
		       strerror(errno));
	return -1;
}

static int set_event_enables(const char *dir, unsigned int als_mask,
			     unsigned int ps_mask, int verbose)
{
	int failures = 0;

	failures += set_event_enable(dir, als_rising_en_attrs,
				     ARRAY_SIZE(als_rising_en_attrs),
				     !!(als_mask & EVENT_RISING),
				     "als_rising_en", verbose) < 0;
	failures += set_event_enable(dir, als_falling_en_attrs,
				     ARRAY_SIZE(als_falling_en_attrs),
				     !!(als_mask & EVENT_FALLING),
				     "als_falling_en", verbose) < 0;
	failures += set_event_enable(dir, ps_rising_en_attrs,
				     ARRAY_SIZE(ps_rising_en_attrs),
				     !!(ps_mask & EVENT_RISING),
				     "ps_rising_en", verbose) < 0;
	failures += set_event_enable(dir, ps_falling_en_attrs,
				     ARRAY_SIZE(ps_falling_en_attrs),
				     !!(ps_mask & EVENT_FALLING),
				     "ps_falling_en", verbose) < 0;

	return failures ? -1 : 0;
}

static int disable_interrupts(const char *dir, int verbose)
{
	return set_event_enables(dir, 0, 0, verbose);
}

static void save_event_enables(const char *dir, struct saved_attr saved[4])
{
	save_attr_any(dir, als_rising_en_attrs, ARRAY_SIZE(als_rising_en_attrs),
		      &saved[0]);
	save_attr_any(dir, als_falling_en_attrs, ARRAY_SIZE(als_falling_en_attrs),
		      &saved[1]);
	save_attr_any(dir, ps_rising_en_attrs, ARRAY_SIZE(ps_rising_en_attrs),
		      &saved[2]);
	save_attr_any(dir, ps_falling_en_attrs, ARRAY_SIZE(ps_falling_en_attrs),
		      &saved[3]);
}

static void restore_event_enables(const char *dir, struct saved_attr saved[4])
{
	restore_attr(dir, &saved[0]);
	restore_attr(dir, &saved[1]);
	restore_attr(dir, &saved[2]);
	restore_attr(dir, &saved[3]);
}

static void print_separator(size_t row_width, const size_t *col_widths,
			    size_t cols)
{
	size_t i;
	size_t j;

	for (j = 0; j < row_width + 2; j++)
		putchar('-');
	for (i = 0; i < cols; i++) {
		putchar('+');
		for (j = 0; j < col_widths[i] + 2; j++)
			putchar('-');
	}
	putchar('\n');
}

static void print_table(const char *title, const char *row_header,
			const char * const *col_names, size_t cols,
			const struct table_row *rows, size_t row_count)
{
	size_t row_width = strlen(row_header);
	size_t col_widths[TEST2_GROUPS];
	size_t i;
	size_t j;

	for (i = 0; i < row_count; i++) {
		size_t len = strlen(rows[i].name);

		if (len > row_width)
			row_width = len;
	}

	for (j = 0; j < cols; j++) {
		col_widths[j] = strlen(col_names[j]);
		for (i = 0; i < row_count; i++) {
			size_t len = strlen(rows[i].cells[j]);

			if (len > col_widths[j])
				col_widths[j] = len;
		}
	}

	printf("\n%s\n", title);
	print_separator(row_width, col_widths, cols);
	printf(" %-*s ", (int)row_width, row_header);
	for (j = 0; j < cols; j++)
		printf("| %-*s ", (int)col_widths[j], col_names[j]);
	putchar('\n');
	print_separator(row_width, col_widths, cols);
	for (i = 0; i < row_count; i++) {
		printf(" %-*s ", (int)row_width, rows[i].name);
		for (j = 0; j < cols; j++)
			printf("| %-*s ", (int)col_widths[j], rows[i].cells[j]);
		putchar('\n');
	}
	print_separator(row_width, col_widths, cols);
}

static int write_mode(const char *dir, const char *mode)
{
	char value[32];

	snprintf(value, sizeof(value), "%s\n", mode);
	return write_attr_any(dir, mode_attrs, ARRAY_SIZE(mode_attrs), value);
}

static int write_scale(const char *dir, const char *scale)
{
	char value[32];

	snprintf(value, sizeof(value), "%s\n", scale);
	return write_attr_any(dir, als_scale_attrs, ARRAY_SIZE(als_scale_attrs),
			      value);
}

static int run_mode_test(const char *dir)
{
	static const struct attr_ref refs[] = {
		{ name_attrs, ARRAY_SIZE(name_attrs) },
		{ mode_attrs, ARRAY_SIZE(mode_attrs) },
		{ mode_available_attrs, ARRAY_SIZE(mode_available_attrs) },
		{ als_raw_attrs, ARRAY_SIZE(als_raw_attrs) },
		{ als_processed_attrs, ARRAY_SIZE(als_processed_attrs) },
		{ als_scale_attrs, ARRAY_SIZE(als_scale_attrs) },
		{ ir_raw_attrs, ARRAY_SIZE(ir_raw_attrs) },
		{ ps_raw_attrs, ARRAY_SIZE(ps_raw_attrs) },
		{ ps_algo_attrs, ARRAY_SIZE(ps_algo_attrs) },
		{ ps_algo_available_attrs, ARRAY_SIZE(ps_algo_available_attrs) },
		{ als_rising_value_attrs, ARRAY_SIZE(als_rising_value_attrs) },
		{ als_falling_value_attrs, ARRAY_SIZE(als_falling_value_attrs) },
		{ als_rising_en_attrs, ARRAY_SIZE(als_rising_en_attrs) },
		{ als_falling_en_attrs, ARRAY_SIZE(als_falling_en_attrs) },
		{ ps_rising_value_attrs, ARRAY_SIZE(ps_rising_value_attrs) },
		{ ps_falling_value_attrs, ARRAY_SIZE(ps_falling_value_attrs) },
		{ ps_rising_en_attrs, ARRAY_SIZE(ps_rising_en_attrs) },
		{ ps_falling_en_attrs, ARRAY_SIZE(ps_falling_en_attrs) },
	};
	struct table_row rows[ARRAY_SIZE(refs)];
	struct saved_attr saved_mode;
	struct saved_attr saved_events[4];
	size_t i;
	size_t j;
	int failures = 0;

	save_attr_any(dir, mode_attrs, ARRAY_SIZE(mode_attrs), &saved_mode);
	save_event_enables(dir, saved_events);

	for (i = 0; i < ARRAY_SIZE(refs); i++)
		rows[i].name = find_existing_attr(dir, refs[i].names,
						  refs[i].count);

	for (j = 0; j < ARRAY_SIZE(mode_names); j++) {
		if (disable_interrupts(dir, 1) < 0)
			failures++;
		if (write_mode(dir, mode_names[j]) < 0) {
			int err = errno;

			for (i = 0; i < ARRAY_SIZE(refs); i++)
				cell_from_errno(rows[i].cells[j],
						sizeof(rows[i].cells[j]), err);
			failures++;
			continue;
		}

		usleep(50000);
		for (i = 0; i < ARRAY_SIZE(refs); i++)
			read_cell(dir, &refs[i], rows[i].cells[j],
				  sizeof(rows[i].cells[j]));
	}

	print_table("TEST1 mode switch result", "interface", mode_names,
		    ARRAY_SIZE(mode_names), rows, ARRAY_SIZE(rows));

	disable_interrupts(dir, 0);
	restore_attr(dir, &saved_mode);
	restore_event_enables(dir, saved_events);

	return failures ? 1 : 0;
}

static int run_als_test(const char *dir)
{
	char group_names[TEST2_GROUPS][8];
	const char *group_name_ptrs[TEST2_GROUPS];
	struct table_row rows[ARRAY_SIZE(als_scale_values)];
	struct saved_attr saved_mode;
	struct saved_attr saved_scale;
	struct saved_attr saved_events[4];
	size_t group;
	size_t scale;
	int failures = 0;

	save_attr_any(dir, mode_attrs, ARRAY_SIZE(mode_attrs), &saved_mode);
	save_attr_any(dir, als_scale_attrs, ARRAY_SIZE(als_scale_attrs),
		      &saved_scale);
	save_event_enables(dir, saved_events);

	disable_interrupts(dir, 1);
	if (write_mode(dir, "als") < 0) {
		printf("[FAIL] set operating_mode=als failed: %s\n",
		       strerror(errno));
		failures++;
	}
	usleep(50000);

	for (scale = 0; scale < ARRAY_SIZE(als_scale_values); scale++)
		rows[scale].name = als_scale_values[scale];

	for (group = 0; group < TEST2_GROUPS; group++) {
		snprintf(group_names[group], sizeof(group_names[group]), "g%02u",
			 (unsigned int)group + 1);
		group_name_ptrs[group] = group_names[group];

		for (scale = 0; scale < ARRAY_SIZE(als_scale_values); scale++) {
			if (disable_interrupts(dir, 0) < 0)
				failures++;
			if (write_scale(dir, als_scale_values[scale]) < 0) {
				cell_from_errno(rows[scale].cells[group],
						sizeof(rows[scale].cells[group]),
						errno);
				failures++;
				continue;
			}

			if (read_attr_any(dir, als_processed_attrs,
					  ARRAY_SIZE(als_processed_attrs),
					  rows[scale].cells[group],
					  sizeof(rows[scale].cells[group])) < 0) {
				cell_from_errno(rows[scale].cells[group],
						sizeof(rows[scale].cells[group]),
						errno);
				failures++;
			}
		}
	}

	print_table("TEST2 ALS scale processed result", "scale",
		    group_name_ptrs, TEST2_GROUPS, rows, ARRAY_SIZE(rows));

	disable_interrupts(dir, 0);
	restore_attr(dir, &saved_scale);
	restore_attr(dir, &saved_mode);
	restore_event_enables(dir, saved_events);

	return failures ? 1 : 0;
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

static const char *source_name(unsigned int chan_type)
{
	switch (chan_type) {
	case IIO_LIGHT:
		return "ALS";
	case IIO_PROXIMITY:
		return "PS";
	case IIO_INTENSITY:
		return "IR";
	default:
		return "unknown";
	}
}

static void print_interrupt_event(const char *dir,
				  const struct iio_event_data *event)
{
	unsigned int type = IIO_EVENT_CODE_EXTRACT_TYPE(event->id);
	unsigned int dir_code = IIO_EVENT_CODE_EXTRACT_DIR(event->id);
	unsigned int chan_type = IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event->id);
	unsigned int modifier = IIO_EVENT_CODE_EXTRACT_MODIFIER(event->id);
	int chan = IIO_EVENT_CODE_EXTRACT_CHAN(event->id);
	char raw[128] = "-";

	if (chan_type == IIO_LIGHT)
		read_attr_any(dir, als_raw_attrs, ARRAY_SIZE(als_raw_attrs),
			      raw, sizeof(raw));
	else if (chan_type == IIO_PROXIMITY)
		read_attr_any(dir, ps_raw_attrs, ARRAY_SIZE(ps_raw_attrs),
			      raw, sizeof(raw));

	printf("interrupt: source=%s dir=%s type=%s chan=%d raw=%s "
	       "timestamp=%lld id=0x%016llx modifier=%s\n",
	       source_name(chan_type), event_dir_name(dir_code),
	       event_type_name(type), chan, raw,
	       (long long)event->timestamp,
	       (unsigned long long)event->id, modifier_name(modifier));
}

static int parse_uint_range(const char *s, unsigned int max,
			    unsigned int *value)
{
	char *endp;
	unsigned long val;

	errno = 0;
	val = strtoul(s, &endp, 10);
	if (errno || !endp || *endp != '\0' || val > max)
		return -1;

	*value = (unsigned int)val;
	return 0;
}

static int parse_seconds_arg(const char *s, int *seconds)
{
	unsigned int value;

	if (parse_uint_range(s, 3600U, &value) < 0 || value == 0)
		return -1;
	*seconds = (int)value;
	return 0;
}

static int parse_event_limit(const char *s, unsigned int *limit)
{
	unsigned int value;

	if (parse_uint_range(s, 100000U, &value) < 0 || value == 0)
		return -1;
	*limit = value;
	return 0;
}

static int parse_event_mask(const char *s, unsigned int *mask)
{
	if (strcmp(s, "off") == 0 || strcmp(s, "0") == 0) {
		*mask = 0;
		return 0;
	}
	if (strcmp(s, "on") == 0 || strcmp(s, "both") == 0 ||
	    strcmp(s, "1") == 0) {
		*mask = EVENT_BOTH;
		return 0;
	}
	if (strcmp(s, "rising") == 0 || strcmp(s, "high") == 0) {
		*mask = EVENT_RISING;
		return 0;
	}
	if (strcmp(s, "falling") == 0 || strcmp(s, "low") == 0) {
		*mask = EVENT_FALLING;
		return 0;
	}

	return -1;
}

static int option_value(int argc, char **argv, int *index, const char *name,
			const char **value)
{
	size_t len = strlen(name);
	const char *arg = argv[*index];

	if (strcmp(arg, name) == 0) {
		if (*index + 1 >= argc)
			return -1;
		*value = argv[++(*index)];
		return 1;
	}

	if (strncmp(arg, name, len) == 0 && arg[len] == '=') {
		*value = arg + len + 1;
		return 1;
	}

	return 0;
}

static void init_irq_config(struct irq_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->seconds = DEFAULT_IRQ_SECONDS;
	cfg->event_limit = DEFAULT_IRQ_EVENT_LIMIT;
	cfg->als_mask = EVENT_BOTH;
	cfg->ps_mask = EVENT_BOTH;
}

static int parse_irq_args(int argc, char **argv, int start,
			  const char **dev_arg, struct irq_config *cfg)
{
	int i;
	int dev_seen = 0;

	init_irq_config(cfg);
	*dev_arg = NULL;

	for (i = start; i < argc; i++) {
		const char *value = NULL;
		int matched;

		matched = option_value(argc, argv, &i, "--seconds", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_seconds_arg(value, &cfg->seconds) < 0)
				return -1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--event-limit", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_event_limit(value, &cfg->event_limit) < 0)
				return -1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--als", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_event_mask(value, &cfg->als_mask) < 0)
				return -1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--ps", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_event_mask(value, &cfg->ps_mask) < 0)
				return -1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--als-low", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_uint_range(value, AP3216C_ALS_MAX_VALUE,
					     &cfg->als_low) < 0)
				return -1;
			cfg->have_als_low = 1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--als-high", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_uint_range(value, AP3216C_ALS_MAX_VALUE,
					     &cfg->als_high) < 0)
				return -1;
			cfg->have_als_high = 1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--ps-low", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_uint_range(value, AP3216C_PS_MAX_VALUE,
					     &cfg->ps_low) < 0)
				return -1;
			cfg->have_ps_low = 1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--ps-high", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (parse_uint_range(value, AP3216C_PS_MAX_VALUE,
					     &cfg->ps_high) < 0)
				return -1;
			cfg->have_ps_high = 1;
			continue;
		}

		matched = option_value(argc, argv, &i, "--ps-algo", &value);
		if (matched < 0)
			return -1;
		if (matched) {
			if (strcmp(value, "zone") != 0 &&
			    strcmp(value, "hysteresis") != 0)
				return -1;
			cfg->ps_algo = value;
			continue;
		}

		if (strcmp(argv[i], "--help") == 0)
			return -1;
		if (argv[i][0] == '-')
			return -1;
		if (dev_seen)
			return -1;
		*dev_arg = argv[i];
		dev_seen = 1;
	}

	return 0;
}

static int saved_uint_or_default(const char *dir, struct saved_attr *saved,
				 const char * const *attrs, size_t count,
				 unsigned int fallback, unsigned int *value)
{
	char *endp;
	unsigned long val;

	if (saved->valid) {
		errno = 0;
		val = strtoul(saved->value, &endp, 10);
		if (!errno && endp && *endp == '\0' && val <= 0xffffffffUL) {
			*value = (unsigned int)val;
			return 0;
		}
	}

	if (read_uint_any(dir, attrs, count, value) == 0)
		return 0;

	*value = fallback;
	return 0;
}

static int open_event_fd(const char *dir, int *event_fd)
{
	char dev_path[256];
	int index;
	int fd;
	int ret;

	index = parse_iio_index_from_path(dir);
	if (index < 0) {
		printf("cannot derive /dev/iio:deviceX from %s\n", dir);
		return -1;
	}

	if (make_iio_dev_path(dev_path, sizeof(dev_path), index) < 0) {
		printf("make iio char device path failed: %s\n", strerror(errno));
		return -1;
	}

	fd = open(dev_path, O_RDONLY);
	if (fd < 0) {
		printf("open %s failed: %s\n", dev_path, strerror(errno));
		return -1;
	}

	ret = ioctl(fd, IIO_GET_EVENT_FD_IOCTL, event_fd);
	close(fd);
	if (ret < 0 || *event_fd < 0) {
		printf("get IIO event fd failed: %s\n", strerror(errno));
		return -1;
	}

	printf("event fd opened from %s\n", dev_path);
	return 0;
}

static int run_irq_test(const char *dir, const struct irq_config *cfg)
{
	struct saved_attr saved_mode;
	struct saved_attr saved_algo;
	struct saved_attr saved_events[4];
	struct saved_attr saved_thresholds[4];
	struct pollfd pfd;
	struct iio_event_data event;
	time_t deadline;
	unsigned int als_low;
	unsigned int als_high;
	unsigned int ps_low;
	unsigned int ps_high;
	int event_fd = -1;
	int events = 0;
	int rc = 1;

	if (!cfg->als_mask && !cfg->ps_mask) {
		printf("irq test: no interrupt source enabled\n");
		return 1;
	}

	save_attr_any(dir, mode_attrs, ARRAY_SIZE(mode_attrs), &saved_mode);
	save_attr_any(dir, ps_algo_attrs, ARRAY_SIZE(ps_algo_attrs),
		      &saved_algo);
	save_event_enables(dir, saved_events);
	save_attr_any(dir, als_rising_value_attrs,
		      ARRAY_SIZE(als_rising_value_attrs), &saved_thresholds[0]);
	save_attr_any(dir, als_falling_value_attrs,
		      ARRAY_SIZE(als_falling_value_attrs), &saved_thresholds[1]);
	save_attr_any(dir, ps_rising_value_attrs,
		      ARRAY_SIZE(ps_rising_value_attrs), &saved_thresholds[2]);
	save_attr_any(dir, ps_falling_value_attrs,
		      ARRAY_SIZE(ps_falling_value_attrs), &saved_thresholds[3]);

	if (open_event_fd(dir, &event_fd) < 0)
		goto out_restore;

	disable_interrupts(dir, 1);

	if (write_mode(dir, "als_ps_ir") < 0) {
		printf("[FAIL] set operating_mode=als_ps_ir failed: %s\n",
		       strerror(errno));
		goto out_restore;
	}
	usleep(50000);

	if (cfg->ps_algo &&
	    write_attr_any(dir, ps_algo_attrs, ARRAY_SIZE(ps_algo_attrs),
			   cfg->ps_algo) < 0) {
		printf("[FAIL] set ps interrupt algorithm failed: %s\n",
		       strerror(errno));
		goto out_restore;
	}

	saved_uint_or_default(dir, &saved_thresholds[1],
			      als_falling_value_attrs,
			      ARRAY_SIZE(als_falling_value_attrs), 0,
			      &als_low);
	saved_uint_or_default(dir, &saved_thresholds[0],
			      als_rising_value_attrs,
			      ARRAY_SIZE(als_rising_value_attrs),
			      AP3216C_ALS_MAX_VALUE, &als_high);
	saved_uint_or_default(dir, &saved_thresholds[3],
			      ps_falling_value_attrs,
			      ARRAY_SIZE(ps_falling_value_attrs), 0,
			      &ps_low);
	saved_uint_or_default(dir, &saved_thresholds[2],
			      ps_rising_value_attrs,
			      ARRAY_SIZE(ps_rising_value_attrs),
			      AP3216C_PS_MAX_VALUE, &ps_high);

	if (cfg->have_als_low)
		als_low = cfg->als_low;
	if (cfg->have_als_high)
		als_high = cfg->als_high;
	if (cfg->have_ps_low)
		ps_low = cfg->ps_low;
	if (cfg->have_ps_high)
		ps_high = cfg->ps_high;

	if (als_low > als_high) {
		printf("[FAIL] ALS threshold invalid: low=%u high=%u\n",
		       als_low, als_high);
		goto out_restore;
	}
	if (ps_low > ps_high) {
		printf("[FAIL] PS threshold invalid: low=%u high=%u\n",
		       ps_low, ps_high);
		goto out_restore;
	}

	if (write_uint_any(dir, als_falling_value_attrs,
			   ARRAY_SIZE(als_falling_value_attrs), als_low) < 0 ||
	    write_uint_any(dir, als_rising_value_attrs,
			   ARRAY_SIZE(als_rising_value_attrs), als_high) < 0 ||
	    write_uint_any(dir, ps_falling_value_attrs,
			   ARRAY_SIZE(ps_falling_value_attrs), ps_low) < 0 ||
	    write_uint_any(dir, ps_rising_value_attrs,
			   ARRAY_SIZE(ps_rising_value_attrs), ps_high) < 0) {
		printf("[FAIL] write threshold failed: %s\n", strerror(errno));
		goto out_restore;
	}

	printf("irq config: ALS=%s low=%u high=%u, PS=%s low=%u high=%u\n",
	       cfg->als_mask == EVENT_BOTH ? "both" :
	       (cfg->als_mask == EVENT_RISING ? "rising" :
		(cfg->als_mask == EVENT_FALLING ? "falling" : "off")),
	       als_low, als_high,
	       cfg->ps_mask == EVENT_BOTH ? "both" :
	       (cfg->ps_mask == EVENT_RISING ? "rising" :
		(cfg->ps_mask == EVENT_FALLING ? "falling" : "off")),
	       ps_low, ps_high);
	if (cfg->ps_algo)
		printf("irq config: PS interrupt_algorithm=%s\n", cfg->ps_algo);

	if (set_event_enables(dir, cfg->als_mask, cfg->ps_mask, 1) < 0)
		goto out_restore;

	stop_requested = 0;
	install_signal_handlers();
	deadline = time(NULL) + cfg->seconds;

	printf("waiting for interrupts for %d seconds, press Ctrl-C to stop\n",
	       cfg->seconds);
	while (!stop_requested && time(NULL) < deadline &&
	       (unsigned int)events < cfg->event_limit) {
		int timeout_ms = (int)(deadline - time(NULL)) * 1000;
		int ret;

		if (timeout_ms <= 0)
			break;

		pfd.fd = event_fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			printf("poll event fd failed: %s\n", strerror(errno));
			goto out_restore;
		}
		if (ret == 0)
			break;

		ret = read(event_fd, &event, sizeof(event));
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			printf("read event fd failed: %s\n", strerror(errno));
			goto out_restore;
		}
		if (ret != (int)sizeof(event)) {
			printf("short event read: %d bytes\n", ret);
			goto out_restore;
		}

		print_interrupt_event(dir, &event);
		events++;
	}

	if (events)
		printf("irq test: received %d interrupt event(s)\n", events);
	else
		printf("irq test: no interrupt event received\n");
	rc = events ? 0 : 1;

out_restore:
	if (event_fd >= 0)
		close(event_fd);
	disable_interrupts(dir, 0);
	restore_attr(dir, &saved_thresholds[1]);
	restore_attr(dir, &saved_thresholds[0]);
	restore_attr(dir, &saved_thresholds[3]);
	restore_attr(dir, &saved_thresholds[2]);
	restore_attr(dir, &saved_algo);
	restore_attr(dir, &saved_mode);
	restore_event_enables(dir, saved_events);

	return rc;
}

static void usage(const char *prog)
{
	printf("usage:\n");
	printf("  %s scan\n", prog);
	printf("  %s test1 [auto|iio:deviceX|N|/sys/...]\n", prog);
	printf("  %s test2 [auto|iio:deviceX|N|/sys/...]\n", prog);
	printf("  %s irq [device] [options]\n", prog);
	printf("\nirq options:\n");
	printf("  --als off|rising|falling|both   default: both\n");
	printf("  --ps off|rising|falling|both    default: both\n");
	printf("  --als-low N --als-high N        range: 0..65535\n");
	printf("  --ps-low N --ps-high N          range: 0..1023\n");
	printf("  --ps-algo zone|hysteresis\n");
	printf("  --seconds N                     default: %d\n",
	       DEFAULT_IRQ_SECONDS);
	printf("  --event-limit N                 default: %d\n",
	       DEFAULT_IRQ_EVENT_LIMIT);
}

int main(int argc, char **argv)
{
	char dir[256];
	const char *cmd;
	const char *dev_arg = NULL;

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	cmd = argv[1];
	if (strcmp(cmd, "scan") == 0)
		return run_scan();

	if (strcmp(cmd, "test1") == 0 || strcmp(cmd, "mode") == 0) {
		if (argc >= 3)
			dev_arg = argv[2];
		if (resolve_device(dev_arg, dir, sizeof(dir)) < 0) {
			printf("resolve AP3216C IIO device failed: %s\n",
			       strerror(errno));
			return 1;
		}
		return run_mode_test(dir);
	}

	if (strcmp(cmd, "test2") == 0 || strcmp(cmd, "als") == 0) {
		if (argc >= 3)
			dev_arg = argv[2];
		if (resolve_device(dev_arg, dir, sizeof(dir)) < 0) {
			printf("resolve AP3216C IIO device failed: %s\n",
			       strerror(errno));
			return 1;
		}
		return run_als_test(dir);
	}

	if (strcmp(cmd, "irq") == 0 || strcmp(cmd, "interrupt") == 0) {
		struct irq_config cfg;

		if (parse_irq_args(argc, argv, 2, &dev_arg, &cfg) < 0) {
			usage(argv[0]);
			return 1;
		}
		if (resolve_device(dev_arg, dir, sizeof(dir)) < 0) {
			printf("resolve AP3216C IIO device failed: %s\n",
			       strerror(errno));
			return 1;
		}
		return run_irq_test(dir, &cfg);
	}

	usage(argv[0]);
	return 1;
}
