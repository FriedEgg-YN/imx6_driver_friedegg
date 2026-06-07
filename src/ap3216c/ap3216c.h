#ifndef AP3216C_H
#define AP3216C_H

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "ap3216c_regs.h"

#define AP3216C_NAME                         "ap3216c"

#define AP3216C_SAMPLE_ALS                   (1U << 0)
#define AP3216C_SAMPLE_IR                    (1U << 1)
#define AP3216C_SAMPLE_PS                    (1U << 2)

#define AP3216C_EVENT_ALS_RISING             (1U << 0)
#define AP3216C_EVENT_ALS_FALLING            (1U << 1)
#define AP3216C_EVENT_PS_RISING              (1U << 2)
#define AP3216C_EVENT_PS_FALLING             (1U << 3)
#define AP3216C_EVENT_ALS_MASK \
	(AP3216C_EVENT_ALS_RISING | AP3216C_EVENT_ALS_FALLING)
#define AP3216C_EVENT_PS_MASK \
	(AP3216C_EVENT_PS_RISING | AP3216C_EVENT_PS_FALLING)

struct dentry;
struct i2c_client;

enum ap3216c_channel {
	AP3216C_CHAN_ALS,
	AP3216C_CHAN_IR,
	AP3216C_CHAN_PS,
};

struct ap3216c_threshold {
	unsigned int low;
	unsigned int high;
};

struct ap3216c_config {
	unsigned int mode;
	unsigned int als_range;
	unsigned int als_persist;
	unsigned int als_calibration;
	unsigned int ps_integration;
	unsigned int ps_gain;
	unsigned int ps_persist;
	struct ap3216c_threshold als_th;
	struct ap3216c_threshold ps_th;
};

struct ap3216c_raw_sample {
	unsigned int valid_mask;
	unsigned int overflow_mask;
	unsigned int event_status;
	u16 ir_raw;
	u16 als_raw;
	u16 ps_raw;
	bool ps_object;
};

#ifdef CONFIG_AP3216C_STATS
struct ap3216c_stats {
	unsigned int irq_count;
	unsigned int event_count;
	unsigned int als_event_count;
	unsigned int ps_event_count;
	unsigned int ignored_irq_count;
	unsigned int read_raw_count;
	unsigned int last_status;
	struct ap3216c_raw_sample last_sample;
};
#endif

struct ap3216c_dev {
	struct i2c_client *client;
	struct mutex bus_lock;
	spinlock_t data_lock;
	struct ap3216c_config config;
	unsigned int event_enable_mask;
	int irq;

#ifdef CONFIG_AP3216C_STATS
	u32 stats_enable;
	struct ap3216c_stats stats;
	struct dentry *stats_dentry;
#endif
};

#endif
