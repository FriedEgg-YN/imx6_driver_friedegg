#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "ap3216c.h"

static const char *mode_name(unsigned int mode)
{
    switch (mode) {
    case AP3216C_MODE_POWER_DOWN:
        return "POWER_DOWN";
    case AP3216C_MODE_ALS_ONLY:
        return "ALS_ONLY";
    case AP3216C_MODE_PS_IR_ONLY:
        return "PS_IR_ONLY";
    case AP3216C_MODE_ALS_PS_IR:
        return "ALS_PS_IR";
    default:
        return "UNKNOWN";
    }
}

static const char *range_name(unsigned int range)
{
    switch (range) {
    case AP3216C_ALS_RANGE_20661_LUX:
        return "20661 lux";
    case AP3216C_ALS_RANGE_5162_LUX:
        return "5162 lux";
    case AP3216C_ALS_RANGE_1291_LUX:
        return "1291 lux";
    case AP3216C_ALS_RANGE_323_LUX:
        return "323 lux";
    default:
        return "unknown";
    }
}

static void fill_default_config(struct ap3216c_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = AP3216C_MODE_ALS_PS_IR;
    cfg->event_mask = AP3216C_EVT_ALS | AP3216C_EVT_PS;
    cfg->als_range = AP3216C_ALS_RANGE_20661_LUX;
    cfg->als_persist = AP3216C_ALS_PERSIST_DEFAULT;
    cfg->als_calibration = AP3216C_ALS_CALIBRATION_DEFAULT;
    cfg->ps_integration = AP3216C_PS_INTEGRATION_1T;
    cfg->ps_gain = AP3216C_PS_GAIN_X2;
    cfg->ps_persist = AP3216C_PS_PERSIST_2_TIMES;
    cfg->als_th.low = AP3216C_ALS_LOW_TH_DEFAULT;
    cfg->als_th.high = AP3216C_ALS_HIGH_TH_DEFAULT;
    cfg->ps_th.low = 100;
    cfg->ps_th.high = 200;
}

static void print_config(const struct ap3216c_config *cfg)
{
    printf("config: mode=%s(0x%x) event_mask=0x%x als_range=%s(0x%x) "
           "als_persist=0x%x als_calibration=%u ps_integration=0x%x "
           "ps_gain=0x%x ps_persist=0x%x\n",
           mode_name(cfg->mode), cfg->mode, cfg->event_mask,
           range_name(cfg->als_range), cfg->als_range,
           cfg->als_persist, cfg->als_calibration,
           cfg->ps_integration, cfg->ps_gain, cfg->ps_persist);
    printf("        als_th=[%u,%u] ps_th=[%u,%u]\n",
           cfg->als_th.low, cfg->als_th.high,
           cfg->ps_th.low, cfg->ps_th.high);
}

static void print_sample(const char *tag, const struct ap3216c_sample *sample)
{
    printf("%s: mode=%s valid=0x%x overflow=0x%x status=0x%x "
           "ir=%u als=%u als_mlux=%u ps=%u object=%u\n",
           tag, mode_name(sample->mode), sample->valid_mask,
           sample->overflow_mask, sample->event_status,
           sample->ir_raw, sample->als_raw, sample->als_mlux,
           sample->ps_raw, sample->ps_object);
}

static void print_stats(const struct ap3216c_stats *stats)
{
    printf("stats: irq=%u event=%u als_event=%u ps_event=%u ignored_irq=%u "
           "read=%u last_status=0x%x\n",
           stats->irq_count, stats->event_count,
           stats->als_event_count, stats->ps_event_count,
           stats->ignored_irq_count, stats->read_count,
           stats->last_status);
    print_sample("stats.last_sample", &stats->last_sample);
}

static int expect_true(const char *name, int condition)
{
    if (condition) {
        printf("[PASS] %s\n", name);
        return 0;
    }

    printf("[FAIL] %s\n", name);
    return -1;
}

static int get_config(int fd, struct ap3216c_config *cfg)
{
    if (ioctl(fd, AP3216C_CMD_GET_CONFIG, cfg) < 0) {
        printf("GET_CONFIG failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int get_stats(int fd, struct ap3216c_stats *stats)
{
    if (ioctl(fd, AP3216C_CMD_GET_STATS, stats) < 0) {
        printf("GET_STATS failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int set_config(int fd, struct ap3216c_config *cfg, const char *tag)
{
    if (ioctl(fd, AP3216C_CMD_SET_CONFIG, cfg) < 0) {
        printf("%s SET_CONFIG failed: %s\n", tag, strerror(errno));
        return -1;
    }
    printf("[PASS] %s SET_CONFIG\n", tag);
    return 0;
}

static int read_sample(int fd, struct ap3216c_sample *sample, const char *tag)
{
    ssize_t ret;

    memset(sample, 0, sizeof(*sample));
    ret = read(fd, sample, sizeof(*sample));
    if (ret != (ssize_t)sizeof(*sample)) {
        if (ret < 0)
            printf("%s read failed: %s\n", tag, strerror(errno));
        else
            printf("%s short read: %zd bytes (expect %zu)\n",
                   tag, ret, sizeof(*sample));
        return -1;
    }

    print_sample(tag, sample);
    return 0;
}

static int set_event_mask(int fd, unsigned int mask, const char *tag)
{
    if (ioctl(fd, AP3216C_CMD_SET_EVENT_MASK, &mask) < 0) {
        printf("%s SET_EVENT_MASK 0x%x failed: %s\n",
               tag, mask, strerror(errno));
        return -1;
    }
    printf("[PASS] %s SET_EVENT_MASK 0x%x\n", tag, mask);
    return 0;
}

static int expect_event_mask_rejected(int fd, unsigned int mask, const char *tag)
{
    int ret;

    errno = 0;
    ret = ioctl(fd, AP3216C_CMD_SET_EVENT_MASK, &mask);
    if (ret < 0 && errno == EINVAL) {
        printf("[PASS] %s rejected with EINVAL\n", tag);
        return 0;
    }

    if (ret < 0)
        printf("[FAIL] %s rejected with unexpected errno=%d (%s)\n",
               tag, errno, strerror(errno));
    else
        printf("[FAIL] %s unexpectedly accepted\n", tag);

    return -1;
}

static int run_fulltest(int fd)
{
    struct ap3216c_config cfg;
    struct ap3216c_sample sample;
    struct ap3216c_stats stats;
    unsigned int ranges[] = {
        AP3216C_ALS_RANGE_20661_LUX,
        AP3216C_ALS_RANGE_5162_LUX,
        AP3216C_ALS_RANGE_1291_LUX,
        AP3216C_ALS_RANGE_323_LUX,
    };
    unsigned int mask;
    size_t i;
    int failures = 0;

    if (get_config(fd, &cfg) < 0)
        return -1;
    printf("[PASS] GET_CONFIG default/current\n");
    print_config(&cfg);

    mask = AP3216C_MODE_ALS_PS_IR;
    if (ioctl(fd, AP3216C_CMD_SET_MODE, &mask) < 0) {
        printf("SET_MODE ALS_PS_IR failed: %s\n", strerror(errno));
        return -1;
    }
    printf("[PASS] SET_MODE ALS_PS_IR\n");
    if (get_config(fd, &cfg) < 0)
        return -1;
    failures += expect_true("GET_CONFIG mode == ALS_PS_IR",
                            cfg.mode == AP3216C_MODE_ALS_PS_IR);

    fill_default_config(&cfg);
    cfg.mode = AP3216C_MODE_ALS_ONLY;
    cfg.event_mask = AP3216C_EVT_ALS;
    cfg.als_range = AP3216C_ALS_RANGE_20661_LUX;
    cfg.als_th.low = 0;
    cfg.als_th.high = AP3216C_ALS_MAX_VALUE;
    if (set_config(fd, &cfg, "ALS_ONLY + EVT_ALS") < 0)
        return -1;
    if (read_sample(fd, &sample, "ALS_ONLY sample") < 0)
        return -1;
    failures += expect_true("ALS_ONLY valid_mask", sample.valid_mask == AP3216C_CH_ALS);

    for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        unsigned int range = ranges[i];

        if (ioctl(fd, AP3216C_CMD_SET_ALS_RANGE, &range) < 0) {
            printf("SET_ALS_RANGE %s failed: %s\n",
                   range_name(range), strerror(errno));
            return -1;
        }
        printf("[PASS] SET_ALS_RANGE %s\n", range_name(range));
        if (read_sample(fd, &sample, "ALS range sample") < 0)
            return -1;
        failures += expect_true("ALS range valid_mask", sample.valid_mask == AP3216C_CH_ALS);
    }

    fill_default_config(&cfg);
    cfg.mode = AP3216C_MODE_PS_IR_ONLY;
    cfg.event_mask = AP3216C_EVT_PS;
    cfg.ps_th.low = 100;
    cfg.ps_th.high = 200;
    if (set_config(fd, &cfg, "PS_IR_ONLY + EVT_PS") < 0)
        return -1;
    if (read_sample(fd, &sample, "PS_IR_ONLY sample") < 0)
        return -1;
    failures += expect_true("PS_IR_ONLY valid_mask",
                            sample.valid_mask == (AP3216C_CH_PS | AP3216C_CH_IR));

    fill_default_config(&cfg);
    cfg.mode = AP3216C_MODE_ALS_PS_IR;
    cfg.event_mask = AP3216C_EVT_ALS;
    if (set_config(fd, &cfg, "ALS_PS_IR + EVT_ALS") < 0)
        return -1;
    if (read_sample(fd, &sample, "ALS_PS_IR sample") < 0)
        return -1;
    failures += expect_true("ALS_PS_IR valid_mask",
                            sample.valid_mask == (AP3216C_CH_ALS |
                                                  AP3216C_CH_IR |
                                                  AP3216C_CH_PS));

    mask = AP3216C_EVT_PS;
    if (set_event_mask(fd, mask, "ALS_PS_IR") < 0)
        return -1;
    if (get_config(fd, &cfg) < 0)
        return -1;
    failures += expect_true("GET_CONFIG event_mask == EVT_PS",
                            cfg.event_mask == AP3216C_EVT_PS);

    mask = AP3216C_EVT_ALS | AP3216C_EVT_PS;
    if (set_event_mask(fd, mask, "ALS_PS_IR") < 0)
        return -1;
    if (get_config(fd, &cfg) < 0)
        return -1;
    failures += expect_true("GET_CONFIG event_mask == EVT_ALS|EVT_PS",
                            cfg.event_mask == (AP3216C_EVT_ALS | AP3216C_EVT_PS));

    fill_default_config(&cfg);
    cfg.mode = AP3216C_MODE_ALS_ONLY;
    cfg.event_mask = AP3216C_EVT_ALS;
    if (set_config(fd, &cfg, "prepare ALS_ONLY") < 0)
        return -1;
    failures += expect_event_mask_rejected(fd, AP3216C_EVT_PS,
                                           "invalid PS event under ALS_ONLY");

    fill_default_config(&cfg);
    cfg.mode = AP3216C_MODE_PS_IR_ONLY;
    cfg.event_mask = AP3216C_EVT_PS;
    if (set_config(fd, &cfg, "prepare PS_IR_ONLY") < 0)
        return -1;
    failures += expect_event_mask_rejected(fd, AP3216C_EVT_ALS,
                                           "invalid ALS event under PS_IR_ONLY");

    if (ioctl(fd, AP3216C_CMD_GET_STATS, &stats) < 0) {
        printf("GET_STATS failed: %s\n", strerror(errno));
        return -1;
    }
    printf("[PASS] GET_STATS\n");
    print_stats(&stats);

    if (failures)
        return -1;

    return 0;
}

static int run_read_loop(int fd)
{
    struct ap3216c_sample sample;

    while (1) {
        if (read_sample(fd, &sample, "read") < 0)
            return -1;
        usleep(200000);
    }
}

static void setup_irqtest_thresholds(struct ap3216c_config *cfg,
                                     const struct ap3216c_sample *sample)
{
    if (sample->als_raw > 0) {
        cfg->als_th.low = 0;
        cfg->als_th.high = sample->als_raw - 1;
    } else {
        cfg->als_th.low = 1;
        cfg->als_th.high = AP3216C_ALS_MAX_VALUE;
    }

    cfg->ps_th.low = 0;
    cfg->ps_th.high = sample->ps_raw > 0 ? sample->ps_raw - 1 : 1;
    if (cfg->ps_th.high > AP3216C_PS_MAX_VALUE)
        cfg->ps_th.high = AP3216C_PS_MAX_VALUE;
}

static int run_irqtest(int fd, unsigned int seconds, unsigned int poll_ms)
{
    struct ap3216c_config cfg;
    struct ap3216c_sample sample;
    struct ap3216c_stats stats;
    unsigned int last_event_count;
    unsigned int last_irq_count;
    time_t start;

    fill_default_config(&cfg);
    cfg.mode = AP3216C_MODE_ALS_PS_IR;
    cfg.event_mask = AP3216C_EVT_ALS | AP3216C_EVT_PS;
    cfg.als_th.low = 0;
    cfg.als_th.high = AP3216C_ALS_MAX_VALUE;
    cfg.ps_th.low = 0;
    cfg.ps_th.high = AP3216C_PS_MAX_VALUE;
    cfg.ps_persist = AP3216C_PS_PERSIST_1_TIME;

    if (set_config(fd, &cfg, "IRQTEST warmup ALS_PS_IR + EVT_ALS|EVT_PS") < 0)
        return -1;
    if (read_sample(fd, &sample, "irqtest baseline") < 0)
        return -1;

    setup_irqtest_thresholds(&cfg, &sample);
    if (set_config(fd, &cfg, "IRQTEST trigger thresholds") < 0)
        return -1;

    printf("irqtest thresholds: als_th=[%u,%u] ps_th=[%u,%u]\n",
           cfg.als_th.low, cfg.als_th.high, cfg.ps_th.low, cfg.ps_th.high);
    printf("waiting for ALS/PS hardware IRQ events for %u seconds, poll=%u ms\n",
           seconds, poll_ms);
    printf("move an object near/far from the sensor if PS events do not appear\n");

    if (get_stats(fd, &stats) < 0)
        return -1;
    last_event_count = stats.event_count;
    last_irq_count = stats.irq_count;
    start = time(NULL);

    while ((unsigned int)(time(NULL) - start) < seconds) {
        if (get_stats(fd, &stats) < 0)
            return -1;

        if (stats.event_count != last_event_count) {
            printf("[IRQ] irq=%u event=%u als_event=%u ps_event=%u "
                   "ignored=%u status=0x%x\n",
                   stats.irq_count, stats.event_count,
                   stats.als_event_count, stats.ps_event_count,
                   stats.ignored_irq_count, stats.last_status);
            print_sample("irq.last_sample", &stats.last_sample);
            last_event_count = stats.event_count;
            last_irq_count = stats.irq_count;
        } else if (stats.irq_count != last_irq_count) {
            printf("[IRQ] irq=%u ignored/event-filtered=%u status=0x%x\n",
                   stats.irq_count, stats.ignored_irq_count,
                   stats.last_status);
            last_irq_count = stats.irq_count;
        }

        usleep(poll_ms * 1000);
    }

    if (get_stats(fd, &stats) < 0)
        return -1;
    printf("irqtest done\n");
    print_stats(&stats);

    return 0;
}

static unsigned int parse_uint_arg(const char *arg, unsigned int fallback)
{
    char *end;
    unsigned long val;

    if (!arg)
        return fallback;

    errno = 0;
    val = strtoul(arg, &end, 0);
    if (errno || end == arg || *end != '\0')
        return fallback;

    return (unsigned int)val;
}

static void usage(const char *prog)
{
    printf("usage: %s /dev/ap3216c fulltest\n", prog);
    printf("       %s /dev/ap3216c read\n", prog);
    printf("       %s /dev/ap3216c irqtest [seconds] [poll_ms]\n", prog);
}

int main(int argc, char *argv[])
{
    int fd;
    int ret;

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        printf("can't open %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    if (strcmp(argv[2], "fulltest") == 0) {
        ret = run_fulltest(fd);
    } else if (strcmp(argv[2], "read") == 0) {
        ret = run_read_loop(fd);
    } else if (strcmp(argv[2], "irqtest") == 0) {
        ret = run_irqtest(fd,
                          parse_uint_arg(argc > 3 ? argv[3] : NULL, 30),
                          parse_uint_arg(argc > 4 ? argv[4] : NULL, 200));
    } else {
        usage(argv[0]);
        ret = -1;
    }

    close(fd);
    return ret ? 1 : 0;
}
