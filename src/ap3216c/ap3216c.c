#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include "ap3216c.h"

#define AP3216C_CNT 1
#define AP3216C_NAME "ap3216c"
#define AP3216C_EVENT_BITS (AP3216C_EVT_ALS | AP3216C_EVT_PS)

struct ap3216c_dev {
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    struct i2c_client *client;

    struct ap3216c_config config;
    struct ap3216c_stats stats;
    struct ap3216c_sample last_sample;

    int irq;
    bool irq_requested;
    struct mutex bus_lock;
    spinlock_t data_lock;
};

static struct ap3216c_dev ap3216c_device;

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

    ret = i2c_transfer(client->adapter, msg, 2);
    if (ret == 2)
        return 0;

    dev_err(&client->dev, "i2c read failed: ret=%d reg=0x%02x len=%d\n",
            ret, reg, len);
    return (ret < 0) ? ret : -EREMOTEIO;
}

static int ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg, const u8 *buf, u8 len)
{
    int ret;
    u8 b[256];
    struct i2c_msg msg;
    struct i2c_client *client = dev->client;

    if (len > sizeof(b) - 1)
        return -EINVAL;

    b[0] = reg;
    memcpy(&b[1], buf, len);

    msg.addr = client->addr;
    msg.flags = 0;
    msg.buf = b;
    msg.len = len + 1;

    ret = i2c_transfer(client->adapter, &msg, 1);
    if (ret == 1)
        return 0;

    dev_err(&client->dev, "i2c write failed: ret=%d reg=0x%02x len=%u\n",
            ret, reg, len);
    return (ret < 0) ? ret : -EREMOTEIO;
}

static int ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 val)
{
    return ap3216c_write_regs(dev, reg, &val, 1);
}

static void ap3216c_fill_default_config(struct ap3216c_config *cfg)
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

static unsigned int ap3216c_mode_to_channels(unsigned int mode)
{
    switch (mode) {
    case AP3216C_MODE_POWER_DOWN:
        return 0;
    case AP3216C_MODE_ALS_ONLY:
        return AP3216C_CH_ALS;
    case AP3216C_MODE_PS_IR_ONLY:
        return AP3216C_CH_PS | AP3216C_CH_IR;
    case AP3216C_MODE_ALS_PS_IR:
        return AP3216C_CH_ALS | AP3216C_CH_PS | AP3216C_CH_IR;
    default:
        return 0;
    }
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

static int ap3216c_validate_event_mask(unsigned int mode, unsigned int event_mask)
{
    if (event_mask & ~AP3216C_EVENT_BITS)
        return -EINVAL;

    switch (mode) {
    case AP3216C_MODE_POWER_DOWN:
        return 0;
    case AP3216C_MODE_ALS_ONLY:
        return (event_mask & AP3216C_EVT_PS) ? -EINVAL : 0;
    case AP3216C_MODE_PS_IR_ONLY:
        return (event_mask & AP3216C_EVT_ALS) ? -EINVAL : 0;
    case AP3216C_MODE_ALS_PS_IR:
        return 0;
    default:
        return -EINVAL;
    }
}

static int ap3216c_validate_range(unsigned int range)
{
    switch (range) {
    case AP3216C_ALS_RANGE_20661_LUX:
    case AP3216C_ALS_RANGE_5162_LUX:
    case AP3216C_ALS_RANGE_1291_LUX:
    case AP3216C_ALS_RANGE_323_LUX:
        return 0;
    default:
        return -EINVAL;
    }
}

static int ap3216c_validate_config(const struct ap3216c_config *cfg)
{
    int ret;

    ret = ap3216c_validate_mode(cfg->mode);
    if (ret)
        return ret;

    ret = ap3216c_validate_event_mask(cfg->mode, cfg->event_mask);
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

static int ap3216c_update_bits_locked(struct ap3216c_dev *dev, u8 reg, u8 mask, u8 val)
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

static u32 ap3216c_als_raw_to_mlux(u16 raw, u8 als_config, u8 calibration)
{
    static const unsigned int scale_tenth_mlux[] = { 3500, 788, 197, 49 };
    unsigned int range = (als_config & AP3216C_ALS_RANGE_MASK) >> AP3216C_ALS_RANGE_SHIFT;
    u64 mlux;

    if (range >= ARRAY_SIZE(scale_tenth_mlux))
        range = 0;

    mlux = raw;
    mlux *= scale_tenth_mlux[range];
    mlux *= calibration;

    return (u32)DIV_ROUND_CLOSEST_ULL(mlux, 64 * 10);
}

static int ap3216c_read_sample_locked(struct ap3216c_dev *dev,
                                      struct ap3216c_sample *sample,
                                      unsigned int channels,
                                      unsigned int event_status)
{
    int ret;
    u8 raw[6];

    memset(sample, 0, sizeof(*sample));
    sample->valid_mask = channels;
    sample->mode = dev->config.mode;
    sample->event_status = event_status;

    ret = ap3216c_read_regs(dev, AP3216C_IR_DATA_LOW, raw, sizeof(raw));
    if (ret)
        return ret;

    if (channels & AP3216C_CH_IR) {
        if (raw[0] & AP3216C_IR_OVERFLOW_BIT) {
            sample->overflow_mask |= AP3216C_CH_IR;
        } else {
            sample->ir_raw = ((u16)raw[1] << 2) |
                             (raw[0] & AP3216C_IR_DATA_LOW_MASK);
        }
    }

    if (channels & AP3216C_CH_ALS) {
        sample->als_raw = ((u16)raw[3] << 8) | raw[2];
        sample->als_mlux = ap3216c_als_raw_to_mlux(sample->als_raw,
                                                   dev->config.als_range,
                                                   dev->config.als_calibration);
    }

    if (channels & AP3216C_CH_PS) {
        sample->ps_object = !!((raw[4] | raw[5]) & AP3216C_PS_OBJECT_BIT);
        if ((raw[4] | raw[5]) & AP3216C_PS_IR_OVERFLOW_BIT) {
            sample->overflow_mask |= AP3216C_CH_PS;
        } else {
            sample->ps_raw = ((u16)(raw[5] & AP3216C_PS_DATA_HIGH_MASK) << 4) |
                             (raw[4] & AP3216C_PS_DATA_LOW_MASK);
        }
    }

    return 0;
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

static int ap3216c_disable_als_event_locked(struct ap3216c_dev *dev)
{
    return ap3216c_write_als_threshold_locked(dev, 0, AP3216C_ALS_MAX_VALUE);
}

static int ap3216c_disable_ps_event_locked(struct ap3216c_dev *dev)
{
    return ap3216c_write_ps_threshold_locked(dev, 0, AP3216C_PS_MAX_VALUE);
}

static int ap3216c_program_event_thresholds_locked(struct ap3216c_dev *dev,
                                                   const struct ap3216c_config *cfg)
{
    int ret;

    if (cfg->event_mask & AP3216C_EVT_ALS)
        ret = ap3216c_write_als_threshold_locked(dev, cfg->als_th.low,
                                                 cfg->als_th.high);
    else
        ret = ap3216c_disable_als_event_locked(dev);
    if (ret)
        return ret;

    if (cfg->event_mask & AP3216C_EVT_PS)
        return ap3216c_write_ps_threshold_locked(dev, cfg->ps_th.low,
                                                 cfg->ps_th.high);

    return ap3216c_disable_ps_event_locked(dev);
}

static int ap3216c_apply_config_locked(struct ap3216c_dev *dev,
                                       const struct ap3216c_config *new_cfg)
{
    int ret;

    ret = ap3216c_validate_config(new_cfg);
    if (ret)
        return ret;

    ret = ap3216c_write_reg(dev, AP3216C_SYSTEM_CONFIG, AP3216C_MODE_POWER_DOWN);
    if (ret)
        return ret;

    ret = ap3216c_program_als_config_locked(dev, new_cfg);
    if (ret)
        return ret;

    ret = ap3216c_program_ps_config_locked(dev, new_cfg);
    if (ret)
        return ret;

    ret = ap3216c_program_event_thresholds_locked(dev, new_cfg);
    if (ret)
        return ret;

    ret = ap3216c_write_reg(dev, AP3216C_INT_CLEAR_MANNER,
                            AP3216C_INT_CLEAR_BY_READ);
    if (ret)
        return ret;

    ret = ap3216c_write_reg(dev, AP3216C_SYSTEM_CONFIG,
                            new_cfg->mode & AP3216C_SYSTEM_MODE_MASK);
    if (ret)
        return ret;

    msleep(20);
    dev->config = *new_cfg;

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

static void ap3216c_stats_set_last_status(struct ap3216c_dev *dev,
                                          unsigned int status)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->data_lock, flags);
    dev->stats.last_status = status;
    spin_unlock_irqrestore(&dev->data_lock, flags);
}

static void ap3216c_stats_record_read(struct ap3216c_dev *dev,
                                      const struct ap3216c_sample *sample)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->data_lock, flags);
    dev->last_sample = *sample;
    dev->stats.last_sample = *sample;
    dev->stats.read_count++;
    spin_unlock_irqrestore(&dev->data_lock, flags);
}

static void ap3216c_stats_record_irq_seen(struct ap3216c_dev *dev)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->data_lock, flags);
    dev->stats.irq_count++;
    spin_unlock_irqrestore(&dev->data_lock, flags);
}

static void ap3216c_stats_record_ignored_irq(struct ap3216c_dev *dev,
                                             unsigned int status)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->data_lock, flags);
    dev->stats.ignored_irq_count++;
    dev->stats.last_status = status;
    spin_unlock_irqrestore(&dev->data_lock, flags);
}

static void ap3216c_stats_record_event(struct ap3216c_dev *dev,
                                       unsigned int status,
                                       unsigned int effective,
                                       const struct ap3216c_sample *sample)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->data_lock, flags);
    dev->last_sample = *sample;
    dev->stats.last_sample = *sample;
    dev->stats.last_status = status;
    dev->stats.event_count++;
    if (effective & AP3216C_EVT_ALS)
        dev->stats.als_event_count++;
    if (effective & AP3216C_EVT_PS)
        dev->stats.ps_event_count++;
    spin_unlock_irqrestore(&dev->data_lock, flags);
}

static int ap3216c_clear_pending_events_locked(struct ap3216c_dev *dev)
{
    u8 raw[6];

    return ap3216c_read_regs(dev, AP3216C_IR_DATA_LOW, raw, sizeof(raw));
}

static irqreturn_t ap3216c_irq_thread(int irq, void *dev_id)
{
    struct ap3216c_dev *dev = dev_id;
    struct ap3216c_sample sample;
    unsigned int status;
    unsigned int effective;
    unsigned int channels;
    u8 status_reg;
    int ret;

    ap3216c_stats_record_irq_seen(dev);

    mutex_lock(&dev->bus_lock);

    ret = ap3216c_read_regs(dev, AP3216C_INT_STATUS, &status_reg, 1);
    if (ret) {
        ap3216c_stats_record_ignored_irq(dev, 0);
        goto out_unlock;
    }

    status = status_reg & AP3216C_EVENT_BITS;
    ap3216c_stats_set_last_status(dev, status);

    if (!status) {
        ap3216c_stats_record_ignored_irq(dev, status);
        goto out_unlock;
    }

    effective = status & dev->config.event_mask;
    if (!effective) {
        ap3216c_clear_pending_events_locked(dev);
        ap3216c_stats_record_ignored_irq(dev, status);
        goto out_unlock;
    }

    channels = ap3216c_mode_to_channels(dev->config.mode);
    if (!channels) {
        ap3216c_clear_pending_events_locked(dev);
        ap3216c_stats_record_ignored_irq(dev, status);
        goto out_unlock;
    }

    ret = ap3216c_read_sample_locked(dev, &sample, channels, status);
    if (ret) {
        ap3216c_stats_record_ignored_irq(dev, status);
        goto out_unlock;
    }

    ap3216c_stats_record_event(dev, status, effective, &sample);

out_unlock:
    mutex_unlock(&dev->bus_lock);
    return IRQ_HANDLED;
}

static int ap3216c_open(struct inode *inode, struct file *filp)
{
    struct i2c_client *client = ap3216c_device.client;

    if (!client) {
        pr_err("ap3216c open failed: i2c client not ready\n");
        return -ENODEV;
    }

    filp->private_data = &ap3216c_device;
    return 0;
}

static ssize_t ap3216c_read(struct file *filp, char __user *buf,
                            size_t cnt, loff_t *off)
{
    int ret;
    unsigned int channels;
    struct ap3216c_sample sample;
    struct ap3216c_dev *dev = filp->private_data;
    struct i2c_client *client = dev->client;

    if (cnt < sizeof(sample)) {
        dev_warn(&client->dev, "read buffer too small: cnt=%zu need=%zu\n",
                 cnt, sizeof(sample));
        return -EINVAL;
    }

    mutex_lock(&dev->bus_lock);
    channels = ap3216c_mode_to_channels(dev->config.mode);
    if (!channels) {
        mutex_unlock(&dev->bus_lock);
        return -EAGAIN;
    }

    ret = ap3216c_read_sample_locked(dev, &sample, channels, 0);
    mutex_unlock(&dev->bus_lock);
    if (ret)
        return ret;

    ap3216c_stats_record_read(dev, &sample);

    if (copy_to_user(buf, &sample, sizeof(sample))) {
        dev_err(&client->dev, "copy_to_user failed\n");
        return -EFAULT;
    }

    return sizeof(sample);
}

static long ap3216c_unlocked_ioctl(struct file *filp,
                                   unsigned int cmd, unsigned long arg)
{
    struct ap3216c_dev *dev = filp->private_data;
    struct ap3216c_config cfg;
    struct ap3216c_stats stats;
    struct ap3216c_threshold th;
    unsigned int val;
    unsigned long flags;
    int ret = 0;

    switch (cmd) {
    case AP3216C_CMD_SET_MODE:
        if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
            return -EFAULT;
        mutex_lock(&dev->bus_lock);
        cfg = dev->config;
        cfg.mode = val;
        ret = ap3216c_apply_config_locked(dev, &cfg);
        mutex_unlock(&dev->bus_lock);
        return ret;

    case AP3216C_CMD_GET_CONFIG:
        mutex_lock(&dev->bus_lock);
        cfg = dev->config;
        mutex_unlock(&dev->bus_lock);
        if (copy_to_user((void __user *)arg, &cfg, sizeof(cfg)))
            return -EFAULT;
        return 0;

    case AP3216C_CMD_SET_CONFIG:
        if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
            return -EFAULT;
        mutex_lock(&dev->bus_lock);
        ret = ap3216c_apply_config_locked(dev, &cfg);
        mutex_unlock(&dev->bus_lock);
        return ret;

    case AP3216C_CMD_SET_EVENT_MASK:
        if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
            return -EFAULT;
        mutex_lock(&dev->bus_lock);
        cfg = dev->config;
        cfg.event_mask = val;
        ret = ap3216c_apply_config_locked(dev, &cfg);
        mutex_unlock(&dev->bus_lock);
        return ret;

    case AP3216C_CMD_SET_ALS_RANGE:
        if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
            return -EFAULT;
        mutex_lock(&dev->bus_lock);
        cfg = dev->config;
        cfg.als_range = val;
        ret = ap3216c_apply_config_locked(dev, &cfg);
        mutex_unlock(&dev->bus_lock);
        return ret;

    case AP3216C_CMD_SET_ALS_TH:
        if (copy_from_user(&th, (void __user *)arg, sizeof(th)))
            return -EFAULT;
        mutex_lock(&dev->bus_lock);
        cfg = dev->config;
        cfg.als_th = th;
        ret = ap3216c_apply_config_locked(dev, &cfg);
        mutex_unlock(&dev->bus_lock);
        return ret;

    case AP3216C_CMD_SET_PS_TH:
        if (copy_from_user(&th, (void __user *)arg, sizeof(th)))
            return -EFAULT;
        mutex_lock(&dev->bus_lock);
        cfg = dev->config;
        cfg.ps_th = th;
        ret = ap3216c_apply_config_locked(dev, &cfg);
        mutex_unlock(&dev->bus_lock);
        return ret;

    case AP3216C_CMD_GET_STATS:
        spin_lock_irqsave(&dev->data_lock, flags);
        stats = dev->stats;
        spin_unlock_irqrestore(&dev->data_lock, flags);
        if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
            return -EFAULT;
        return 0;

    default:
        return -ENOTTY;
    }
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static const struct file_operations ap3216c_ops = {
    .owner = THIS_MODULE,
    .open = ap3216c_open,
    .read = ap3216c_read,
    .unlocked_ioctl = ap3216c_unlocked_ioctl,
    .release = ap3216c_release,
};

static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    int ret;
    int major;
    struct ap3216c_dev *dev = &ap3216c_device;

    major = dev->major;
    memset(dev, 0, sizeof(*dev));
    dev->major = major;
    dev->client = client;
    dev->irq = client->irq;
    mutex_init(&dev->bus_lock);
    spin_lock_init(&dev->data_lock);
    ap3216c_fill_default_config(&dev->config);
    i2c_set_clientdata(client, dev);

    if (dev->major) {
        dev->devid = MKDEV(dev->major, 0);
        ret = register_chrdev_region(dev->devid, AP3216C_CNT, AP3216C_NAME);
    } else {
        ret = alloc_chrdev_region(&dev->devid, 0, AP3216C_CNT, AP3216C_NAME);
        dev->major = MAJOR(dev->devid);
    }
    if (ret < 0) {
        dev_err(&client->dev, "alloc chrdev region failed: ret=%d\n", ret);
        return ret;
    }

    cdev_init(&dev->cdev, &ap3216c_ops);
    ret = cdev_add(&dev->cdev, dev->devid, AP3216C_CNT);
    if (ret) {
        dev_err(&client->dev, "cdev add failed: ret=%d\n", ret);
        goto err_unregister_chrdev;
    }

    dev->class = class_create(THIS_MODULE, AP3216C_NAME);
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        dev_err(&client->dev, "class create failed: ret=%d\n", ret);
        goto err_cdev_del;
    }

    dev->device = device_create(dev->class, NULL, dev->devid, NULL, AP3216C_NAME);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        dev_err(&client->dev, "device create failed: ret=%d\n", ret);
        goto err_class_destroy;
    }

    ret = ap3216c_hw_init(dev);
    if (ret)
        goto err_device_destroy;

    if (dev->irq > 0) {
        ret = request_threaded_irq(dev->irq, NULL, ap3216c_irq_thread,
                                   IRQF_ONESHOT | IRQF_TRIGGER_LOW,
                                   AP3216C_NAME, dev);
        if (ret) {
            dev_err(&client->dev, "request threaded irq %d failed: ret=%d\n",
                    dev->irq, ret);
            goto err_device_destroy;
        }
        dev->irq_requested = true;
    } else {
        dev_warn(&client->dev, "no irq from DTS; polling read/ioctl remain available\n");
    }

    dev_info(&client->dev, "ap3216c cdev ready: major=%d irq=%d\n",
             MAJOR(dev->devid), dev->irq);
    return 0;

err_device_destroy:
    device_destroy(dev->class, dev->devid);
err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister_chrdev:
    unregister_chrdev_region(dev->devid, AP3216C_CNT);
    i2c_set_clientdata(client, NULL);
    dev->client = NULL;
    return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
    struct ap3216c_dev *dev = i2c_get_clientdata(client);

    if (!dev)
        dev = &ap3216c_device;

    dev_info(&client->dev, "remove ap3216c\n");

    if (dev->irq_requested) {
        free_irq(dev->irq, dev);
        dev->irq_requested = false;
    }

    if (dev->client) {
        mutex_lock(&dev->bus_lock);
        ap3216c_write_reg(dev, AP3216C_SYSTEM_CONFIG, AP3216C_MODE_POWER_DOWN);
        mutex_unlock(&dev->bus_lock);
    }

    device_destroy(dev->class, dev->devid);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devid, AP3216C_CNT);
    i2c_set_clientdata(client, NULL);
    dev->client = NULL;

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
        .owner = THIS_MODULE,
        .name = AP3216C_NAME,
        .of_match_table = ap3216c_of_match,
    },
    .id_table = ap3216c_id,
};

module_i2c_driver(ap3216c_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FriedEgg");
