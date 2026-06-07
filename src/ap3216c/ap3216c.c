#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "ap3216c.h"

static const unsigned int ap3216c_als_scale_micro[] = {
	350000, 78800, 19700, 4900,
};

static const unsigned int ap3216c_als_ranges[] = {
	AP3216C_ALS_RANGE_20661_LUX,
	AP3216C_ALS_RANGE_5162_LUX,
	AP3216C_ALS_RANGE_1291_LUX,
	AP3216C_ALS_RANGE_323_LUX,
};

static const char * const ap3216c_mode_names[] = {
	"power_down",
	"als",
	"ps_ir",
	"als_ps_ir",
};

static const unsigned int ap3216c_mode_values[] = {
	AP3216C_MODE_POWER_DOWN,
	AP3216C_MODE_ALS_ONLY,
	AP3216C_MODE_PS_IR_ONLY,
	AP3216C_MODE_ALS_PS_IR,
};

static unsigned int ap3216c_mode_to_channels(unsigned int mode)
{
	switch (mode) {
	case AP3216C_MODE_POWER_DOWN:
		return 0;
	case AP3216C_MODE_ALS_ONLY:
		return AP3216C_SAMPLE_ALS;
	case AP3216C_MODE_PS_IR_ONLY:
		return AP3216C_SAMPLE_PS | AP3216C_SAMPLE_IR;
	case AP3216C_MODE_ALS_PS_IR:
		return AP3216C_SAMPLE_ALS | AP3216C_SAMPLE_PS | AP3216C_SAMPLE_IR;
	default:
		return 0;
	}
}

static unsigned int ap3216c_channel_to_sample_bit(unsigned long address)
{
	switch (address) {
	case AP3216C_CHAN_ALS:
		return AP3216C_SAMPLE_ALS;
	case AP3216C_CHAN_IR:
		return AP3216C_SAMPLE_IR;
	case AP3216C_CHAN_PS:
		return AP3216C_SAMPLE_PS;
	default:
		return 0;
	}
}

static int ap3216c_mode_to_index(unsigned int mode)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(ap3216c_mode_values); i++)
		if (ap3216c_mode_values[i] == mode)
			return i;

	return 0;
}

static int ap3216c_validate_mode(unsigned int mode)
{
	switch (mode) {
	case AP3216C_MODE_POWER_DOWN:
	case AP3216C_MODE_ALS_ONLY:
	case AP3216C_MODE_PS_IR_ONLY:
	case AP3216C_MODE_ALS_PS_IR:
		return 0;
	default:
		return -EINVAL;
	}
}

static int ap3216c_validate_range(unsigned int range)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(ap3216c_als_ranges); i++)
		if (ap3216c_als_ranges[i] == range)
			return 0;

	return -EINVAL;
}

static int ap3216c_validate_config(const struct ap3216c_config *cfg)
{
	int ret;

	ret = ap3216c_validate_mode(cfg->mode);
	if (ret)
		return ret;

	ret = ap3216c_validate_range(cfg->als_range);
	if (ret)
		return ret;

	if (cfg->als_persist > AP3216C_ALS_PERSIST_MAX)
		return -EINVAL;
	if (cfg->als_calibration > AP3216C_ALS_CALIBRATION_MASK)
		return -EINVAL;
	if (cfg->ps_integration & ~AP3216C_PS_INTEGRATION_MASK)
		return -EINVAL;
	if (cfg->ps_gain & ~AP3216C_PS_GAIN_MASK)
		return -EINVAL;
	if (cfg->ps_persist & ~AP3216C_PS_PERSIST_MASK)
		return -EINVAL;
	if (cfg->als_th.low > cfg->als_th.high ||
	    cfg->als_th.high > AP3216C_ALS_MAX_VALUE)
		return -EINVAL;
	if (cfg->ps_th.low > cfg->ps_th.high ||
	    cfg->ps_th.high > AP3216C_PS_MAX_VALUE)
		return -EINVAL;

	return 0;
}

static void ap3216c_fill_default_config(struct ap3216c_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->mode = AP3216C_MODE_ALS_PS_IR;
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

/**
 * ap3216c_read_regs() - read a consecutive register block over I2C
 * @dev: AP3216C device state.
 * @reg: First register address to read.
 * @buf: Destination buffer.
 * @len: Number of bytes to read, must be positive.
 *
 * Context: caller serializes hardware access with bus_lock when the read is
 * part of a larger hardware transaction.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *buf, int len)
{
	int ret;
	struct i2c_msg msg[2];
	struct i2c_client *client = dev->client;

	if (len <= 0)
		return -EINVAL;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = 1;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = len;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret == ARRAY_SIZE(msg))
		return 0;

	dev_err(&client->dev, "i2c read failed: ret=%d reg=0x%02x len=%d\n",
		ret, reg, len);
	return ret < 0 ? ret : -EREMOTEIO;
}

/**
 * ap3216c_write_regs() - write a consecutive register block over I2C
 * @dev: AP3216C device state.
 * @reg: First register address to write.
 * @buf: Source buffer.
 * @len: Number of payload bytes to write.
 *
 * Context: caller serializes hardware access with bus_lock when the write is
 * part of a larger hardware transaction.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg,
				      const u8 *buf, u8 len)
{
	int ret;
	u8 tmp[32];
	struct i2c_msg msg;
	struct i2c_client *client = dev->client;

	if (len > sizeof(tmp) - 1)
		return -EINVAL;

	tmp[0] = reg;
	memcpy(&tmp[1], buf, len);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = tmp;
	msg.len = len + 1;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret == 1)
		return 0;

	dev_err(&client->dev, "i2c write failed: ret=%d reg=0x%02x len=%u\n",
		ret, reg, len);
	return ret < 0 ? ret : -EREMOTEIO;
}

static int ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 val)
{
	return ap3216c_write_regs(dev, reg, &val, 1);
}

/**
 * ap3216c_update_bits_locked() - update selected bits in a register
 * @dev: AP3216C device state.
 * @reg: Register address.
 * @mask: Bits to update.
 * @val: New bit values before masking.
 *
 * Context: caller must hold bus_lock.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int ap3216c_update_bits_locked(struct ap3216c_dev *dev,
				       u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 old_val;
	u8 new_val;

	ret = ap3216c_read_regs(dev, reg, &old_val, 1);
	if (ret)
		return ret;

	new_val = (old_val & ~mask) | (val & mask);
	if (new_val == old_val)
		return 0;

	return ap3216c_write_reg(dev, reg, new_val);
}

static int ap3216c_als_range_index(unsigned int range)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(ap3216c_als_ranges); i++)
		if (ap3216c_als_ranges[i] == range)
			return i;

	return 0;
}

static int ap3216c_scale_to_range(int val, int val2, unsigned int *range)
{
	size_t i;

	if (val != 0 || val2 < 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ap3216c_als_scale_micro); i++) {
		if (val2 == ap3216c_als_scale_micro[i]) {
			*range = ap3216c_als_ranges[i];
			return 0;
		}
	}

	return -EINVAL;
}

static int ap3216c_calibration_to_reg(int val, int val2, unsigned int *reg)
{
	int calibration;

	if (val < 0 || val > 3 || val2 < 0 || val2 >= 1000000)
		return -EINVAL;

	calibration = val * 64 + DIV_ROUND_CLOSEST(val2 * 64, 1000000);
	if (calibration < 0 || calibration > AP3216C_ALS_CALIBRATION_MASK)
		return -EINVAL;

	*reg = calibration;
	return 0;
}

#ifdef CONFIG_AP3216C_STATS
/**
 * ap3216c_als_raw_to_mlux() - convert ALS raw data to milli-lux
 * @raw: Raw ALS ADC count.
 * @range: Cached AP3216C ALS range register bits.
 * @calibration: Cached AP3216C ALS calibration register value.
 *
 * Return: Converted milli-lux using integer arithmetic.
 */
static u32 ap3216c_als_raw_to_mlux(u16 raw, unsigned int range,
					   unsigned int calibration)
{
	unsigned int idx = ap3216c_als_range_index(range);
	u64 mlux;

	mlux = raw;
	mlux *= ap3216c_als_scale_micro[idx];
	mlux *= calibration;

	return (u32)DIV_ROUND_CLOSEST_ULL(mlux, 64 * 1000);
}
#endif

/**
 * ap3216c_read_sample_locked() - read and decode AP3216C raw sample registers
 * @dev: AP3216C device state.
 * @sample: Destination sample cache.
 * @channels: Bitmask of channels expected to be valid in the current mode.
 * @event_status: Interrupt status bits associated with this sample, or 0.
 *
 * Context: caller must hold bus_lock. The chip latches each channel's high
 * byte when its low byte is read, so read low/high as separate register pairs.
 * Reading ALS high and PS high also clears pending AP3216C interrupts when
 * configured for clear-by-read.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int ap3216c_read_pair_locked(struct ap3216c_dev *dev,
				    u8 low_reg, u8 high_reg,
				    u8 *low, u8 *high)
{
	int ret;

	ret = ap3216c_read_regs(dev, low_reg, low, 1);
	if (ret)
		return ret;

	return ap3216c_read_regs(dev, high_reg, high, 1);
}

static int ap3216c_read_sample_locked(struct ap3216c_dev *dev,
				      struct ap3216c_raw_sample *sample,
				      unsigned int channels,
				      unsigned int event_status)
{
	int ret;
	u8 low;
	u8 high;

	memset(sample, 0, sizeof(*sample));
	sample->valid_mask = channels;
	sample->event_status = event_status;

	if (channels & AP3216C_SAMPLE_IR) {
		ret = ap3216c_read_pair_locked(dev, AP3216C_IR_DATA_LOW,
					       AP3216C_IR_DATA_HIGH,
					       &low, &high);
		if (ret)
			return ret;

		if (low & AP3216C_IR_OVERFLOW_BIT) {
			sample->overflow_mask |= AP3216C_SAMPLE_IR;
		} else {
			sample->ir_raw = ((u16)high << 2) |
				(low & AP3216C_IR_DATA_LOW_MASK);
		}
	}

	if (channels & AP3216C_SAMPLE_ALS) {
		ret = ap3216c_read_pair_locked(dev, AP3216C_ALS_DATA_LOW,
					       AP3216C_ALS_DATA_HIGH,
					       &low, &high);
		if (ret)
			return ret;

		sample->als_raw = ((u16)high << 8) | low;
	}

	if (channels & AP3216C_SAMPLE_PS) {
		ret = ap3216c_read_pair_locked(dev, AP3216C_PS_DATA_LOW,
					       AP3216C_PS_DATA_HIGH,
					       &low, &high);
		if (ret)
			return ret;

		sample->ps_object = !!((low | high) & AP3216C_PS_OBJECT_BIT);
		if ((low | high) & AP3216C_PS_IR_OVERFLOW_BIT) {
			sample->overflow_mask |= AP3216C_SAMPLE_PS;
		} else {
			sample->ps_raw = ((u16)(high & AP3216C_PS_DATA_HIGH_MASK) << 4) |
				(low & AP3216C_PS_DATA_LOW_MASK);
		}
	}

	return 0;
}

static int ap3216c_clear_pending_events_locked(struct ap3216c_dev *dev)
{
	u8 low;
	u8 high;
	int ret;

	ret = ap3216c_read_pair_locked(dev, AP3216C_ALS_DATA_LOW,
				       AP3216C_ALS_DATA_HIGH, &low, &high);
	if (ret)
		return ret;

	return ap3216c_read_pair_locked(dev, AP3216C_PS_DATA_LOW,
					AP3216C_PS_DATA_HIGH, &low, &high);
}

static void ap3216c_clear_pending_events_best_effort_locked(struct ap3216c_dev *dev)
{
	int ret;

	ret = ap3216c_clear_pending_events_locked(dev);
	if (ret)
		dev_warn(&dev->client->dev,
			 "clear pending events failed: ret=%d\n", ret);
}

static int ap3216c_write_ps_threshold_locked(struct ap3216c_dev *dev,
					     unsigned int low,
					     unsigned int high)
{
	int ret;

	if (low > high || high > AP3216C_PS_MAX_VALUE)
		return -EINVAL;

	ret = ap3216c_write_reg(dev, AP3216C_PS_LOW_TH_LOW,
				  low & AP3216C_PS_TH_LOW_MASK);
	if (ret)
		return ret;
	ret = ap3216c_write_reg(dev, AP3216C_PS_LOW_TH_HIGH,
				  (low >> 2) & AP3216C_PS_TH_HIGH_MASK);
	if (ret)
		return ret;
	ret = ap3216c_write_reg(dev, AP3216C_PS_HIGH_TH_LOW,
				  high & AP3216C_PS_TH_LOW_MASK);
	if (ret)
		return ret;

	return ap3216c_write_reg(dev, AP3216C_PS_HIGH_TH_HIGH,
				       (high >> 2) & AP3216C_PS_TH_HIGH_MASK);
}

static int ap3216c_write_als_threshold_locked(struct ap3216c_dev *dev,
					      unsigned int low,
					      unsigned int high)
{
	int ret;

	if (low > high || high > AP3216C_ALS_MAX_VALUE)
		return -EINVAL;

	ret = ap3216c_write_reg(dev, AP3216C_ALS_LOW_TH_LOW,
				  low & AP3216C_ALS_TH_LOW_MASK);
	if (ret)
		return ret;
	ret = ap3216c_write_reg(dev, AP3216C_ALS_LOW_TH_HIGH,
				  (low >> 8) & AP3216C_ALS_TH_HIGH_MASK);
	if (ret)
		return ret;
	ret = ap3216c_write_reg(dev, AP3216C_ALS_HIGH_TH_LOW,
				  high & AP3216C_ALS_TH_LOW_MASK);
	if (ret)
		return ret;

	return ap3216c_write_reg(dev, AP3216C_ALS_HIGH_TH_HIGH,
				       (high >> 8) & AP3216C_ALS_TH_HIGH_MASK);
}

static int ap3216c_program_als_config_locked(struct ap3216c_dev *dev,
					     const struct ap3216c_config *cfg)
{
	int ret;
	u8 val = cfg->als_range | (cfg->als_persist & AP3216C_ALS_PERSIST_MASK);

	ret = ap3216c_update_bits_locked(dev, AP3216C_ALS_CONFIG,
					  AP3216C_ALS_RANGE_MASK |
					  AP3216C_ALS_PERSIST_MASK,
					  val);
	if (ret)
		return ret;

	return ap3216c_write_reg(dev, AP3216C_ALS_CALIBRATION,
				       cfg->als_calibration & AP3216C_ALS_CALIBRATION_MASK);
}

static int ap3216c_program_ps_config_locked(struct ap3216c_dev *dev,
					    const struct ap3216c_config *cfg)
{
	int ret;
	u8 val = cfg->ps_integration | cfg->ps_gain | cfg->ps_persist;

	ret = ap3216c_update_bits_locked(dev, AP3216C_PS_CONFIG,
					  AP3216C_PS_INTEGRATION_MASK |
					  AP3216C_PS_GAIN_MASK |
					  AP3216C_PS_PERSIST_MASK,
					  val);
	if (ret)
		return ret;

	return ap3216c_update_bits_locked(dev, AP3216C_PS_INT_MODE,
					AP3216C_PS_INT_ALGO_MASK,
					AP3216C_PS_INT_ALGO_HYSTERESIS);
}

static int ap3216c_program_event_thresholds_locked(struct ap3216c_dev *dev,
						   const struct ap3216c_config *cfg)
{
	unsigned int als_low = 0;
	unsigned int als_high = AP3216C_ALS_MAX_VALUE;
	unsigned int ps_low = 0;
	unsigned int ps_high = AP3216C_PS_MAX_VALUE;
	int ret;

	if (dev->event_enable_mask & AP3216C_EVENT_ALS_FALLING)
		als_low = cfg->als_th.low;
	if (dev->event_enable_mask & AP3216C_EVENT_ALS_RISING)
		als_high = cfg->als_th.high;

	if (dev->event_enable_mask & AP3216C_EVENT_PS_MASK) {
		ps_low = cfg->ps_th.low;
		ps_high = cfg->ps_th.high;
	}

	ret = ap3216c_write_als_threshold_locked(dev, als_low, als_high);
	if (ret)
		return ret;

	return ap3216c_write_ps_threshold_locked(dev, ps_low, ps_high);
}

/**
 * ap3216c_apply_config_locked() - program cached AP3216C configuration
 * @dev: AP3216C device state.
 * @cfg: New validated configuration to apply.
 *
 * Context: caller must hold bus_lock.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int ap3216c_apply_config_locked(struct ap3216c_dev *dev,
				       const struct ap3216c_config *cfg)
{
	int ret;

	ret = ap3216c_validate_config(cfg);
	if (ret)
		return ret;

	ret = ap3216c_write_reg(dev, AP3216C_SYSTEM_CONFIG,
				  AP3216C_MODE_POWER_DOWN);
	if (ret)
		return ret;

	ret = ap3216c_program_als_config_locked(dev, cfg);
	if (ret)
		return ret;

	ret = ap3216c_program_ps_config_locked(dev, cfg);
	if (ret)
		return ret;

	ret = ap3216c_program_event_thresholds_locked(dev, cfg);
	if (ret)
		return ret;

	ret = ap3216c_write_reg(dev, AP3216C_INT_CLEAR_MANNER,
				  AP3216C_INT_CLEAR_BY_READ);
	if (ret)
		return ret;

	ret = ap3216c_write_reg(dev, AP3216C_SYSTEM_CONFIG,
				  cfg->mode & AP3216C_SYSTEM_MODE_MASK);
	if (ret)
		return ret;

	msleep(20);
	dev->config = *cfg;

	return 0;
}

static int ap3216c_hw_init(struct ap3216c_dev *dev)
{
	int ret;
	struct i2c_client *client = dev->client;

	mutex_lock(&dev->bus_lock);
	ret = ap3216c_write_reg(dev, AP3216C_SYSTEM_CONFIG, AP3216C_MODE_SW_RESET);
	if (ret) {
		dev_err(&client->dev, "reset sensor failed: ret=%d\n", ret);
		goto out_unlock;
	}

	msleep(50);
	ret = ap3216c_apply_config_locked(dev, &dev->config);
	if (ret)
		dev_err(&client->dev, "default config apply failed: ret=%d\n", ret);

out_unlock:
	mutex_unlock(&dev->bus_lock);
	return ret;
}

#ifdef CONFIG_AP3216C_STATS
static void ap3216c_stats_read_inc(struct ap3216c_dev *dev,
					   const struct ap3216c_raw_sample *sample)
{
	unsigned long flags;

	if (!dev->stats_enable)
		return;

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->stats.read_raw_count++;
	dev->stats.last_sample = *sample;
	spin_unlock_irqrestore(&dev->data_lock, flags);
}

static void ap3216c_stats_irq_inc(struct ap3216c_dev *dev)
{
	unsigned long flags;

	if (!dev->stats_enable)
		return;

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->stats.irq_count++;
	spin_unlock_irqrestore(&dev->data_lock, flags);
}

static void ap3216c_stats_ignored_irq(struct ap3216c_dev *dev,
				       unsigned int status)
{
	unsigned long flags;

	if (!dev->stats_enable)
		return;

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->stats.ignored_irq_count++;
	dev->stats.last_status = status;
	spin_unlock_irqrestore(&dev->data_lock, flags);
}

static void ap3216c_stats_event_update(struct ap3216c_dev *dev,
					       unsigned int status,
					       unsigned int event_bit,
					       const struct ap3216c_raw_sample *sample)
{
	unsigned long flags;

	if (!dev->stats_enable)
		return;

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->stats.event_count++;
	if (event_bit & AP3216C_EVENT_ALS_MASK)
		dev->stats.als_event_count++;
	if (event_bit & AP3216C_EVENT_PS_MASK)
		dev->stats.ps_event_count++;
	dev->stats.last_status = status;
	dev->stats.last_sample = *sample;
	spin_unlock_irqrestore(&dev->data_lock, flags);
}

static int ap3216c_stats_show(struct seq_file *s, void *unused)
{
	struct ap3216c_dev *dev = s->private;
	struct ap3216c_config cfg;
	struct ap3216c_stats stats;
	u32 als_mlux;
	unsigned long flags;

	mutex_lock(&dev->bus_lock);
	cfg = dev->config;
	spin_lock_irqsave(&dev->data_lock, flags);
	stats = dev->stats;
	spin_unlock_irqrestore(&dev->data_lock, flags);
	mutex_unlock(&dev->bus_lock);

	als_mlux = ap3216c_als_raw_to_mlux(stats.last_sample.als_raw,
					 cfg.als_range, cfg.als_calibration);

	seq_printf(s, "stats_enable=%u\n", dev->stats_enable ? 1 : 0);
	seq_printf(s, "irq_count=%u\n", stats.irq_count);
	seq_printf(s, "event_count=%u\n", stats.event_count);
	seq_printf(s, "als_event_count=%u\n", stats.als_event_count);
	seq_printf(s, "ps_event_count=%u\n", stats.ps_event_count);
	seq_printf(s, "ignored_irq_count=%u\n", stats.ignored_irq_count);
	seq_printf(s, "read_raw_count=%u\n", stats.read_raw_count);
	seq_printf(s, "last_status=0x%x\n", stats.last_status);
	seq_printf(s, "last_valid_mask=0x%x\n", stats.last_sample.valid_mask);
	seq_printf(s, "last_overflow_mask=0x%x\n", stats.last_sample.overflow_mask);
	seq_printf(s, "last_ir_raw=%u\n", stats.last_sample.ir_raw);
	seq_printf(s, "last_als_raw=%u\n", stats.last_sample.als_raw);
	seq_printf(s, "last_als_mlux=%u\n", als_mlux);
	seq_printf(s, "last_ps_raw=%u\n", stats.last_sample.ps_raw);
	seq_printf(s, "last_ps_object=%u\n", stats.last_sample.ps_object ? 1 : 0);

	return 0;
}

static int ap3216c_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, ap3216c_stats_show, inode->i_private);
}

static const struct file_operations ap3216c_stats_fops = {
	.owner = THIS_MODULE,
	.open = ap3216c_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t ap3216c_stats_reset_write(struct file *file,
					 const char __user *buf,
					 size_t count, loff_t *ppos)
{
	struct ap3216c_dev *dev = file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dev->data_lock, flags);
	memset(&dev->stats, 0, sizeof(dev->stats));
	spin_unlock_irqrestore(&dev->data_lock, flags);

	return count;
}

static const struct file_operations ap3216c_stats_reset_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = ap3216c_stats_reset_write,
	.llseek = noop_llseek,
};

static void ap3216c_debugfs_init(struct iio_dev *indio_dev)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct dentry *root = iio_get_debugfs_dentry(indio_dev);

	if (!root)
		return;

	dev->stats_dentry = debugfs_create_dir("stats", root);
	if (IS_ERR_OR_NULL(dev->stats_dentry)) {
		dev->stats_dentry = NULL;
		return;
	}

	debugfs_create_bool("stats_enable", S_IRUGO | S_IWUSR,
			    dev->stats_dentry, &dev->stats_enable);
	debugfs_create_file("stats", S_IRUGO, dev->stats_dentry,
			    dev, &ap3216c_stats_fops);
	debugfs_create_file("stats_reset", S_IWUSR, dev->stats_dentry,
			    dev, &ap3216c_stats_reset_fops);
}

static void ap3216c_debugfs_remove(struct ap3216c_dev *dev)
{
	debugfs_remove_recursive(dev->stats_dentry);
	dev->stats_dentry = NULL;
}
#else
static void ap3216c_stats_read_inc(struct ap3216c_dev *dev,
					   const struct ap3216c_raw_sample *sample) { }
static void ap3216c_stats_irq_inc(struct ap3216c_dev *dev) { }
static void ap3216c_stats_ignored_irq(struct ap3216c_dev *dev,
				       unsigned int status) { }
static void ap3216c_stats_event_update(struct ap3216c_dev *dev,
					       unsigned int status,
					       unsigned int event_bit,
					       const struct ap3216c_raw_sample *sample) { }
static void ap3216c_debugfs_init(struct iio_dev *indio_dev) { }
static void ap3216c_debugfs_remove(struct ap3216c_dev *dev) { }
#endif

static int ap3216c_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct ap3216c_raw_sample sample;
	unsigned int active;
	unsigned int bit;
	int idx;
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		bit = ap3216c_channel_to_sample_bit(chan->address);
		if (!bit)
			return -EINVAL;

		mutex_lock(&dev->bus_lock);
		active = ap3216c_mode_to_channels(dev->config.mode);
		if (!(active & bit)) {
			ret = -EAGAIN;
			goto out_unlock;
		}

		ret = ap3216c_read_sample_locked(dev, &sample, active, 0);
		if (ret)
			goto out_unlock;

		switch (chan->address) {
		case AP3216C_CHAN_ALS:
			*val = sample.als_raw;
			break;
		case AP3216C_CHAN_IR:
			if (sample.overflow_mask & AP3216C_SAMPLE_IR) {
				ret = -EOVERFLOW;
				goto out_unlock;
			}
			*val = sample.ir_raw;
			break;
		case AP3216C_CHAN_PS:
			if (sample.overflow_mask & AP3216C_SAMPLE_PS) {
				ret = -EOVERFLOW;
				goto out_unlock;
			}
			*val = sample.ps_raw;
			break;
		default:
			ret = -EINVAL;
			goto out_unlock;
		}

		ap3216c_stats_read_inc(dev, &sample);
		mutex_unlock(&dev->bus_lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&dev->bus_lock);
		idx = ap3216c_als_range_index(dev->config.als_range);
		mutex_unlock(&dev->bus_lock);
		*val = 0;
		*val2 = ap3216c_als_scale_micro[idx];
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		mutex_lock(&dev->bus_lock);
		*val = dev->config.als_calibration / 64;
		*val2 = DIV_ROUND_CLOSEST((dev->config.als_calibration % 64) * 1000000,
					   64);
		mutex_unlock(&dev->bus_lock);
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;

out_unlock:
	mutex_unlock(&dev->bus_lock);
	return ret;
}

static int ap3216c_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct ap3216c_config cfg;
	unsigned int new_val;
	int ret;

	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	mutex_lock(&dev->bus_lock);
	cfg = dev->config;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = ap3216c_scale_to_range(val, val2, &new_val);
		if (ret)
			break;
		cfg.als_range = new_val;
		ret = ap3216c_apply_config_locked(dev, &cfg);
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		ret = ap3216c_calibration_to_reg(val, val2, &new_val);
		if (ret)
			break;
		cfg.als_calibration = new_val;
		ret = ap3216c_apply_config_locked(dev, &cfg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&dev->bus_lock);
	return ret;
}

static int ap3216c_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
	case IIO_CHAN_INFO_CALIBSCALE:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ap3216c_event_bit(const struct iio_chan_spec *chan,
			     enum iio_event_type type,
			     enum iio_event_direction dir)
{
	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	switch (chan->type) {
	case IIO_LIGHT:
		if (dir == IIO_EV_DIR_RISING)
			return AP3216C_EVENT_ALS_RISING;
		if (dir == IIO_EV_DIR_FALLING)
			return AP3216C_EVENT_ALS_FALLING;
		break;
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			return AP3216C_EVENT_PS_RISING;
		if (dir == IIO_EV_DIR_FALLING)
			return AP3216C_EVENT_PS_FALLING;
		break;
	default:
		break;
	}

	return -EINVAL;
}

static int ap3216c_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	int bit;
	int enabled;

	bit = ap3216c_event_bit(chan, type, dir);
	if (bit < 0)
		return bit;

	mutex_lock(&dev->bus_lock);
	enabled = !!(dev->event_enable_mask & bit);
	mutex_unlock(&dev->bus_lock);

	return enabled;
}

static int ap3216c_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      int state)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	unsigned int old_mask;
	int bit;
	int ret;

	bit = ap3216c_event_bit(chan, type, dir);
	if (bit < 0)
		return bit;
	if (state && dev->irq <= 0)
		return -ENODEV;

	mutex_lock(&dev->bus_lock);
	old_mask = dev->event_enable_mask;
	if (state)
		dev->event_enable_mask |= bit;
	else
		dev->event_enable_mask &= ~bit;

	ret = ap3216c_program_event_thresholds_locked(dev, &dev->config);
	if (ret) {
		dev->event_enable_mask = old_mask;
	} else {
		ap3216c_clear_pending_events_best_effort_locked(dev);
	}
	mutex_unlock(&dev->bus_lock);

	return ret;
}

static int ap3216c_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	int ret = 0;

	if (info != IIO_EV_INFO_VALUE || type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	mutex_lock(&dev->bus_lock);
	switch (chan->type) {
	case IIO_LIGHT:
		if (dir == IIO_EV_DIR_RISING)
			*val = dev->config.als_th.high;
		else if (dir == IIO_EV_DIR_FALLING)
			*val = dev->config.als_th.low;
		else
			ret = -EINVAL;
		break;
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			*val = dev->config.ps_th.high;
		else if (dir == IIO_EV_DIR_FALLING)
			*val = dev->config.ps_th.low;
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dev->bus_lock);

	*val2 = 0;
	return ret ? ret : IIO_VAL_INT;
}

static int ap3216c_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct ap3216c_config cfg;
	int ret = 0;

	if (info != IIO_EV_INFO_VALUE || type != IIO_EV_TYPE_THRESH ||
	    val < 0 || val2 != 0)
		return -EINVAL;

	mutex_lock(&dev->bus_lock);
	cfg = dev->config;

	switch (chan->type) {
	case IIO_LIGHT:
		if (val > AP3216C_ALS_MAX_VALUE) {
			ret = -EINVAL;
			break;
		}
		if (dir == IIO_EV_DIR_RISING)
			cfg.als_th.high = val;
		else if (dir == IIO_EV_DIR_FALLING)
			cfg.als_th.low = val;
		else
			ret = -EINVAL;
		break;
	case IIO_PROXIMITY:
		if (val > AP3216C_PS_MAX_VALUE) {
			ret = -EINVAL;
			break;
		}
		if (dir == IIO_EV_DIR_RISING)
			cfg.ps_th.high = val;
		else if (dir == IIO_EV_DIR_FALLING)
			cfg.ps_th.low = val;
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret)
		ret = ap3216c_validate_config(&cfg);
	if (!ret)
		ret = ap3216c_program_event_thresholds_locked(dev, &cfg);
	if (!ret) {
		dev->config = cfg;
		ap3216c_clear_pending_events_best_effort_locked(dev);
	}

	mutex_unlock(&dev->bus_lock);

	return ret;
}

static int ap3216c_set_operating_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      unsigned int mode)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct ap3216c_config cfg;
	int ret;

	if (mode >= ARRAY_SIZE(ap3216c_mode_values))
		return -EINVAL;

	mutex_lock(&dev->bus_lock);
	cfg = dev->config;
	cfg.mode = ap3216c_mode_values[mode];
	ret = ap3216c_apply_config_locked(dev, &cfg);
	mutex_unlock(&dev->bus_lock);

	return ret;
}

static int ap3216c_get_operating_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	int mode;

	mutex_lock(&dev->bus_lock);
	mode = ap3216c_mode_to_index(dev->config.mode);
	mutex_unlock(&dev->bus_lock);

	return mode;
}

static const struct iio_enum ap3216c_operating_mode_enum = {
	.items = ap3216c_mode_names,
	.num_items = ARRAY_SIZE(ap3216c_mode_names),
	.set = ap3216c_set_operating_mode,
	.get = ap3216c_get_operating_mode,
};

static const struct iio_chan_spec_ext_info ap3216c_ext_info[] = {
	IIO_ENUM("operating_mode", IIO_SHARED_BY_ALL,
		 &ap3216c_operating_mode_enum),
	{
		.name = "operating_mode_available",
		.shared = IIO_SHARED_BY_ALL,
		.read = iio_enum_available_read,
		.private = (uintptr_t)&ap3216c_operating_mode_enum,
	},
	{ }
};

static int ap3216c_debugfs_reg_access(struct iio_dev *indio_dev,
				      unsigned int reg, unsigned int writeval,
				      unsigned int *readval)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	u8 val;
	int ret;

	if (reg > 0xff || writeval > 0xff)
		return -EINVAL;

	mutex_lock(&dev->bus_lock);
	if (readval) {
		ret = ap3216c_read_regs(dev, reg, &val, 1);
		if (!ret)
			*readval = val;
	} else {
		ret = ap3216c_write_reg(dev, reg, writeval);
	}
	mutex_unlock(&dev->bus_lock);

	return ret;
}

static int ap3216c_push_als_event_locked(struct iio_dev *indio_dev,
					 struct ap3216c_raw_sample *sample,
					 unsigned int status)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	enum iio_event_direction dir;
	unsigned int event_bit;
	u64 code;

	if (!(dev->event_enable_mask & AP3216C_EVENT_ALS_MASK))
		return 0;

	if (sample->als_raw > dev->config.als_th.high) {
		dir = IIO_EV_DIR_RISING;
		event_bit = AP3216C_EVENT_ALS_RISING;
	} else if (sample->als_raw < dev->config.als_th.low) {
		dir = IIO_EV_DIR_FALLING;
		event_bit = AP3216C_EVENT_ALS_FALLING;
	} else {
		return 0;
	}

	if (!(dev->event_enable_mask & event_bit))
		return 0;

	code = IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0, IIO_EV_TYPE_THRESH, dir);
	iio_push_event(indio_dev, code, iio_get_time_ns());
	ap3216c_stats_event_update(dev, status, event_bit, sample);

	return 1;
}

static int ap3216c_push_ps_event_locked(struct iio_dev *indio_dev,
					struct ap3216c_raw_sample *sample,
					unsigned int status)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	enum iio_event_direction dir;
	unsigned int event_bit;
	u64 code;

	if (!(dev->event_enable_mask & AP3216C_EVENT_PS_MASK))
		return 0;
	if (sample->overflow_mask & AP3216C_SAMPLE_PS)
		return 0;

	if (sample->ps_raw > dev->config.ps_th.high) {
		dir = IIO_EV_DIR_RISING;
		event_bit = AP3216C_EVENT_PS_RISING;
	} else if (sample->ps_raw < dev->config.ps_th.low) {
		dir = IIO_EV_DIR_FALLING;
		event_bit = AP3216C_EVENT_PS_FALLING;
	} else {
		return 0;
	}

	if (!(dev->event_enable_mask & event_bit))
		return 0;

	code = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0, IIO_EV_TYPE_THRESH, dir);
	iio_push_event(indio_dev, code, iio_get_time_ns());
	ap3216c_stats_event_update(dev, status, event_bit, sample);

	return 1;
}

/**
 * ap3216c_irq_thread() - handle AP3216C threshold interrupts
 * @irq: Linux IRQ number.
 * @dev_id: IIO device pointer passed during IRQ request.
 *
 * The handler reads AP3216C status, fetches a sample to clear the hardware
 * interrupt, maps enabled ALS/PS threshold conditions to IIO events, and
 * updates optional stats.
 *
 * Return: IRQ_HANDLED after the status line is consumed.
 */
static irqreturn_t ap3216c_irq_thread(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct ap3216c_raw_sample sample;
	unsigned int status;
	unsigned int active;
	u8 status_reg;
	int pushed = 0;
	int ret;

	ap3216c_stats_irq_inc(dev);

	mutex_lock(&dev->bus_lock);
	ret = ap3216c_read_regs(dev, AP3216C_INT_STATUS, &status_reg, 1);
	if (ret) {
		ap3216c_stats_ignored_irq(dev, 0);
		goto out_unlock;
	}

	status = status_reg & AP3216C_INT_STATUS_MASK;
	if (!status) {
		ap3216c_stats_ignored_irq(dev, status);
		goto out_unlock;
	}

	active = ap3216c_mode_to_channels(dev->config.mode);
	if (!active) {
		ap3216c_clear_pending_events_locked(dev);
		ap3216c_stats_ignored_irq(dev, status);
		goto out_unlock;
	}

	ret = ap3216c_read_sample_locked(dev, &sample, active, status);
	if (ret) {
		ap3216c_stats_ignored_irq(dev, status);
		goto out_unlock;
	}

	if ((status & AP3216C_INTSTATUS_ALS_BIT) &&
	    (active & AP3216C_SAMPLE_ALS))
		pushed += ap3216c_push_als_event_locked(indio_dev, &sample, status);

	if ((status & AP3216C_INTSTATUS_PS_BIT) &&
	    (active & AP3216C_SAMPLE_PS))
		pushed += ap3216c_push_ps_event_locked(indio_dev, &sample, status);

	if (!pushed)
		ap3216c_stats_ignored_irq(dev, status);
	dev_dbg(&dev->client->dev,
		"irq status=0x%x pushed=%d als=%u ps=%u mask=0x%x th als=[%u,%u] ps=[%u,%u]\n",
		status, pushed, sample.als_raw, sample.ps_raw,
		dev->event_enable_mask, dev->config.als_th.low,
		dev->config.als_th.high, dev->config.ps_th.low,
		dev->config.ps_th.high);

out_unlock:
	mutex_unlock(&dev->bus_lock);
	return IRQ_HANDLED;
}

static const struct iio_event_spec ap3216c_als_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_event_spec ap3216c_ps_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec ap3216c_channels[] = {
	{
		.type = IIO_LIGHT,
		.address = AP3216C_CHAN_ALS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_CALIBSCALE),
		.event_spec = ap3216c_als_events,
		.num_event_specs = ARRAY_SIZE(ap3216c_als_events),
		.ext_info = ap3216c_ext_info,
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.address = AP3216C_CHAN_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
	{
		.type = IIO_PROXIMITY,
		.address = AP3216C_CHAN_PS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = ap3216c_ps_events,
		.num_event_specs = ARRAY_SIZE(ap3216c_ps_events),
	},
};

static IIO_CONST_ATTR(in_illuminance_scale_available,
		      "0.350000 0.078800 0.019700 0.004900");

static struct attribute *ap3216c_attrs[] = {
	&iio_const_attr_in_illuminance_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ap3216c_attr_group = {
	.attrs = ap3216c_attrs,
};

static const struct iio_info ap3216c_info = {
	.driver_module = THIS_MODULE,
	.attrs = &ap3216c_attr_group,
	.read_raw = ap3216c_read_raw,
	.write_raw = ap3216c_write_raw,
	.write_raw_get_fmt = ap3216c_write_raw_get_fmt,
	.read_event_config = ap3216c_read_event_config,
	.write_event_config = ap3216c_write_event_config,
	.read_event_value = ap3216c_read_event_value,
	.write_event_value = ap3216c_write_event_value,
	.debugfs_reg_access = ap3216c_debugfs_reg_access,
};

static int ap3216c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ap3216c_dev *dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*dev));
	if (!indio_dev)
		return -ENOMEM;

	dev = iio_priv(indio_dev);
	dev->client = client;
	dev->irq = client->irq;
	mutex_init(&dev->bus_lock);
	spin_lock_init(&dev->data_lock);
	ap3216c_fill_default_config(&dev->config);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = AP3216C_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ap3216c_channels;
	indio_dev->num_channels = ARRAY_SIZE(ap3216c_channels);
	indio_dev->info = &ap3216c_info;

	i2c_set_clientdata(client, indio_dev);

	ret = ap3216c_hw_init(dev);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret)
		return ret;

	if (dev->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, dev->irq, NULL,
						ap3216c_irq_thread,
						IRQF_ONESHOT | IRQF_TRIGGER_LOW,
						AP3216C_NAME, indio_dev);
		if (ret) {
			dev_err(&client->dev, "request threaded irq %d failed: ret=%d\n",
				dev->irq, ret);
			return ret;
		}
	} else {
		dev_warn(&client->dev, "no irq from DTS; raw channels remain available\n");
	}

	ap3216c_debugfs_init(indio_dev);
	dev_info(&client->dev, "ap3216c IIO ready: irq=%d\n", dev->irq);

	return 0;
}

static int ap3216c_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ap3216c_dev *dev;

	if (!indio_dev)
		return 0;

	dev = iio_priv(indio_dev);
	ap3216c_debugfs_remove(dev);

	mutex_lock(&dev->bus_lock);
	ap3216c_write_reg(dev, AP3216C_SYSTEM_CONFIG, AP3216C_MODE_POWER_DOWN);
	mutex_unlock(&dev->bus_lock);

	return 0;
}

static const struct i2c_device_id ap3216c_id[] = {
	{ "ap3216c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

static const struct of_device_id ap3216c_of_match[] = {
	{ .compatible = "alientek,ap3216c" },
	{ }
};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static struct i2c_driver ap3216c_driver = {
	.probe = ap3216c_probe,
	.remove = ap3216c_remove,
	.driver = {
		.name = AP3216C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ap3216c_of_match,
	},
	.id_table = ap3216c_id,
};

module_i2c_driver(ap3216c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FriedEgg");
MODULE_DESCRIPTION("AP3216C ALS/IR/PS IIO driver");
