#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iio/events.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/unaligned/be_byteshift.h>
#include <linux/iio/trigger.h>

/* Device constants */
#define AP3216C_NAME "ap3216c"
#define AP3216C_ADDR 0x1E

#define AP3216C_U10_MAX 1023
#define AP3216C_U16_MAX 65535
#define AP3216C_IR_MAX_VALUE AP3216C_U10_MAX
#define AP3216C_PS_MAX_VALUE AP3216C_U10_MAX
#define AP3216C_ALS_MAX_VALUE AP3216C_U16_MAX

/* System registers */
#define AP3216C_SYSTEM_CONFIG 0x00
#define AP3216C_SYSTEM_MODE_MASK 0x07
#define AP3216C_MODE_POWER_DOWN 0x00
#define AP3216C_MODE_ALS_ONLY 0x01
#define AP3216C_MODE_PS_IR_ONLY 0x02
#define AP3216C_MODE_ALS_PS_IR 0x03
#define AP3216C_MODE_SW_RESET 0x04

#define AP3216C_INT_STATUS 0x01
#define AP3216C_INTSTATUS_PS_BIT 0x02
#define AP3216C_INTSTATUS_ALS_BIT 0x01
#define AP3216C_INT_STATUS_MASK \
	(AP3216C_INTSTATUS_PS_BIT | AP3216C_INTSTATUS_ALS_BIT)

#define AP3216C_INT_CLEAR_MANNER 0x02
#define AP3216C_INT_CLEAR_BY_READ 0x00
#define AP3216C_INT_CLEAR_BY_WRITE 0x01

/* Data registers */
#define AP3216C_IR_DATA_LOW 0x0A
#define AP3216C_IR_OVERFLOW_BIT 0x80
#define AP3216C_IR_DATA_LOW_MASK 0x03
#define AP3216C_IR_DATA_HIGH 0x0B

#define AP3216C_ALS_DATA_LOW 0x0C
#define AP3216C_ALS_DATA_HIGH 0x0D

#define AP3216C_PS_DATA_LOW 0x0E
#define AP3216C_PS_OBJECT_BIT 0x80
#define AP3216C_PS_IR_OVERFLOW_BIT 0x40
#define AP3216C_PS_DATA_LOW_MASK 0x0F
#define AP3216C_PS_DATA_HIGH 0x0F
#define AP3216C_PS_DATA_HIGH_MASK 0x3F

/* ALS registers */
#define AP3216C_ALS_CONFIG 0x10
#define AP3216C_ALS_CONFIG_DEFAULT 0x00
#define AP3216C_ALS_RANGE_MASK 0x30
#define AP3216C_ALS_RANGE_SHIFT 4
#define AP3216C_ALS_RANGE_20661_LUX 0x00
#define AP3216C_ALS_RANGE_5162_LUX 0x10
#define AP3216C_ALS_RANGE_1291_LUX 0x20
#define AP3216C_ALS_RANGE_323_LUX 0x30
#define AP3216C_ALS_PERSIST_MASK 0x0F
#define AP3216C_ALS_PERSIST_DEFAULT 0x00
#define AP3216C_ALS_PERSIST_MAX 0x0F

#define AP3216C_ALS_CALIBRATION 0x19
#define AP3216C_ALS_CALIBRATION_DEFAULT 0x40
#define AP3216C_ALS_CALIBRATION_MASK 0xFF

#define AP3216C_ALS_LOW_TH_LOW 0x1A
#define AP3216C_ALS_LOW_TH_HIGH 0x1B
#define AP3216C_ALS_LOW_TH_DEFAULT 0x0000
#define AP3216C_ALS_TH_LOW_MASK 0xFF
#define AP3216C_ALS_TH_HIGH_MASK 0xFF

#define AP3216C_ALS_HIGH_TH_LOW 0x1C
#define AP3216C_ALS_HIGH_TH_HIGH 0x1D
#define AP3216C_ALS_HIGH_TH_DEFAULT 0xFFFF

/* PS registers */
#define AP3216C_PS_CONFIG 0x20
#define AP3216C_PS_CONFIG_DEFAULT 0x05
#define AP3216C_PS_LED_CTRL 0x21
#define AP3216C_PS_INTEGRATION_MASK 0xF0
#define AP3216C_PS_INTEGRATION_1T 0x00
#define AP3216C_PS_GAIN_MASK 0x0C
#define AP3216C_PS_GAIN_X2 0x04
#define AP3216C_PS_PERSIST_MASK 0x03
#define AP3216C_PS_PERSIST_1_TIME 0x00
#define AP3216C_PS_PERSIST_2_TIMES 0x01

#define AP3216C_PS_INT_MODE 0x22
#define AP3216C_PS_INT_ALGO_MASK 0x01
#define AP3216C_PS_INT_ALGO_ZONE 0x00
#define AP3216C_PS_INT_ALGO_HYSTERESIS 0x01

#define AP3216C_PS_LOW_TH_LOW 0x2A
#define AP3216C_PS_LOW_TH_HIGH 0x2B
#define AP3216C_PS_TH_LOW_MASK 0x03
#define AP3216C_PS_TH_HIGH_MASK 0xFF

#define AP3216C_PS_HIGH_TH_LOW 0x2C
#define AP3216C_PS_HIGH_TH_HIGH 0x2D

/* Internal driver masks */
#define AP3216C_SAMPLE_ALS BIT(0)
#define AP3216C_SAMPLE_IR BIT(1)
#define AP3216C_SAMPLE_PS BIT(2)
#define AP3216C_SAMPLE_ALL \
	(AP3216C_SAMPLE_ALS | AP3216C_SAMPLE_IR | AP3216C_SAMPLE_PS)

#define AP3216C_EVENT_ALS_RISING BIT(0)
#define AP3216C_EVENT_ALS_FALLING BIT(1)
#define AP3216C_EVENT_PS_RISING BIT(2)
#define AP3216C_EVENT_PS_FALLING BIT(3)
#define AP3216C_EVENT_ALS_MASK \
	(AP3216C_EVENT_ALS_RISING | AP3216C_EVENT_ALS_FALLING)
#define AP3216C_EVENT_PS_MASK \
	(AP3216C_EVENT_PS_RISING | AP3216C_EVENT_PS_FALLING)

/* Static lookup tables */
static const char *const ap3216c_mode_names[] = {
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

static const char *const ap3216c_ps_int_algo_names[] = {
	"zone",
	"hysteresis",
};

static const unsigned int ap3216c_ps_int_algo_values[] = {
	AP3216C_PS_INT_ALGO_ZONE,
	AP3216C_PS_INT_ALGO_HYSTERESIS,
};

/*
 * ap3216c环境光传感器分辨率,扩大1000000倍,
 * 量程依次为0～20661，0～5162，0～1291，0～323。单位：lux
 */
static const int als_scale_ap3216c[] = {315000, 78800, 19700, 4900};

/* Private data structures */
struct ap3216c_threshold
{
	u16 low;
	u16 high;
};

struct ap3216c_sample
{
	u16 als;
	u16 ir;
	u16 ps;
	bool ps_object;
	bool ir_overflow;
	bool ps_overflow;
};

struct ap3216c_dev
{
	struct i2c_client *client; /* i2c 设备 */
	struct regmap *regmap;	   /* regmap */
	struct regmap_config regmap_config;
	struct mutex lock;
	struct iio_trigger *trig;
	u8 mode;
	int irq;
	unsigned int event_enable_mask;
	struct ap3216c_threshold als_th;
	struct ap3216c_threshold ps_th;
	u8 ps_int_algo;
	u8 last_int_status;
};

/* Generic mapping helpers */
static int ap3216c_mode_to_index(unsigned int mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ap3216c_mode_values); i++)
	{
		if (ap3216c_mode_values[i] == mode)
			return i;
	}

	return 0;
}

static unsigned int ap3216c_mode_to_sample_mask(u8 mode)
{
	switch (mode)
	{
	case AP3216C_MODE_POWER_DOWN:
		return 0;

	case AP3216C_MODE_ALS_ONLY:
		return AP3216C_SAMPLE_ALS;

	case AP3216C_MODE_PS_IR_ONLY:
		return AP3216C_SAMPLE_PS | AP3216C_SAMPLE_IR;

	case AP3216C_MODE_ALS_PS_IR:
		return AP3216C_SAMPLE_ALL;

	default:
		return 0;
	}
}

static unsigned int ap3216c_chan_to_sample_mask(const struct iio_chan_spec *chan)
{
	switch (chan->address)
	{
	case AP3216C_ALS_DATA_LOW:
		return AP3216C_SAMPLE_ALS;
	case AP3216C_IR_DATA_LOW:
		return AP3216C_SAMPLE_IR;
	case AP3216C_PS_DATA_LOW:
		return AP3216C_SAMPLE_PS;
	default:
		return 0;
	}
}

static int ap3216c_channel_read_mask(u8 mode,
									 const struct iio_chan_spec *chan,
									 unsigned int *read_mask)
{
	unsigned int requested = ap3216c_chan_to_sample_mask(chan);
	unsigned int active = ap3216c_mode_to_sample_mask(mode);

	if (!requested)
		return -EINVAL;

	if (!(active & requested))
		return -EAGAIN;

	*read_mask = requested;
	return 0;
}

static int ap3216c_event_bit(const struct iio_chan_spec *chan,
							 enum iio_event_type type,
							 enum iio_event_direction dir)
{
	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	switch (chan->type)
	{
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

static int ap3216c_ps_int_algo_to_index(u8 algo)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ap3216c_ps_int_algo_values); i++)
	{
		if (ap3216c_ps_int_algo_values[i] == algo)
			return i;
	}

	return 1;
}

/* Hardware access helpers */
static unsigned char ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg)
{
	unsigned int data = 0;

	regmap_read(dev->regmap, reg, &data);
	return (u8)data;
}

static int ap3216c_set_mode(struct ap3216c_dev *dev, u8 mode)
{
	int ret;

	if (mode > AP3216C_MODE_ALS_PS_IR)
		return -EINVAL;

	ret = regmap_update_bits(dev->regmap, AP3216C_SYSTEM_CONFIG,
							 AP3216C_SYSTEM_MODE_MASK, mode);
	if (ret)
		return ret;

	dev->mode = mode;
	msleep(20);

	return 0;
}

static int ap3216c_write_als_scale(struct ap3216c_dev *dev, int val)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(als_scale_ap3216c); i++)
	{
		if (als_scale_ap3216c[i] == val)
		{
			ret = regmap_update_bits(dev->regmap, AP3216C_ALS_CONFIG,
									 AP3216C_ALS_RANGE_MASK,
									 i << AP3216C_ALS_RANGE_SHIFT);
			if (ret)
				return ret;
			/*
			 * 切换scale后需要一定时间保证生效，50ms不够
			 * 当前只在 write_raw中被调用，先会获取mutex再sleep，读同样需要mutex
			 * 保证修改后读取串行化
			 */
			msleep(100);
			return 0;
		}
	}

	return -EINVAL;
}

static int ap3216c_read_sample(struct ap3216c_dev *dev,
							   struct ap3216c_sample *sample,
							   unsigned int read_mask)
{
	u8 data[2];
	int ret;

	if (read_mask & ~AP3216C_SAMPLE_ALL)
		return -EINVAL;

	memset(sample, 0, sizeof(*sample));

	if (read_mask & AP3216C_SAMPLE_ALS)
	{
		ret = regmap_bulk_read(dev->regmap, AP3216C_ALS_DATA_LOW,
							   data, 2);
		if (ret)
			return ret;

		sample->als = ((u16)data[1] << 8) | data[0];
	}

	if (read_mask & AP3216C_SAMPLE_IR)
	{
		ret = regmap_bulk_read(dev->regmap, AP3216C_IR_DATA_LOW,
							   data, 2);
		if (ret)
			return ret;

		sample->ir_overflow = data[0] & AP3216C_IR_OVERFLOW_BIT;
		if (!sample->ir_overflow)
			sample->ir = ((u16)data[1] << 2) |
						 (data[0] & AP3216C_IR_DATA_LOW_MASK);
	}

	if (read_mask & AP3216C_SAMPLE_PS)
	{
		ret = regmap_bulk_read(dev->regmap, AP3216C_PS_DATA_LOW,
							   data, 2);
		if (ret)
			return ret;

		sample->ps_object = !!((data[0] | data[1]) &
							   AP3216C_PS_OBJECT_BIT);
		sample->ps_overflow = !!((data[0] | data[1]) &
								 AP3216C_PS_IR_OVERFLOW_BIT);
		if (!sample->ps_overflow)
			sample->ps = ((u16)(data[1] & AP3216C_PS_DATA_HIGH_MASK) << 4) |
						 (data[0] & AP3216C_PS_DATA_LOW_MASK);
	}

	return 0;
}

static int ap3216c_read_als_processed(struct ap3216c_dev *dev,
									  u16 raw, int *val, int *val2)
{
	int ret;
	unsigned int regdata;
	unsigned int range;
	u64 lux_micro;
	u32 rem;

	ret = regmap_read(dev->regmap, AP3216C_ALS_CONFIG, &regdata);
	if (ret)
		return ret;

	range = (regdata & AP3216C_ALS_RANGE_MASK) >>
			AP3216C_ALS_RANGE_SHIFT;

	lux_micro = (u64)raw * als_scale_ap3216c[range];

	*val = div_u64_rem(lux_micro, 1000000, &rem);
	*val2 = rem;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ap3216c_write_als_threshold(struct ap3216c_dev *dev, u16 low, u16 high)
{
	int ret;

	if (low > high || high > AP3216C_ALS_MAX_VALUE)
		return -EINVAL;

	ret = regmap_write(dev->regmap, AP3216C_ALS_LOW_TH_LOW,
					   low & AP3216C_ALS_TH_LOW_MASK);
	if (ret)
		return ret;
	ret = regmap_write(dev->regmap, AP3216C_ALS_LOW_TH_HIGH,
					   (low >> 8) & AP3216C_ALS_TH_HIGH_MASK);
	if (ret)
		return ret;
	ret = regmap_write(dev->regmap, AP3216C_ALS_HIGH_TH_LOW,
					   high & AP3216C_ALS_TH_LOW_MASK);
	if (ret)
		return ret;

	return regmap_write(dev->regmap, AP3216C_ALS_HIGH_TH_HIGH,
						(high >> 8) & AP3216C_ALS_TH_HIGH_MASK);
}

static int ap3216c_write_ps_threshold(struct ap3216c_dev *dev, u16 low, u16 high)
{
	int ret;

	if (low > high || high > AP3216C_PS_MAX_VALUE)
		return -EINVAL;

	ret = regmap_write(dev->regmap, AP3216C_PS_LOW_TH_LOW,
					   low & AP3216C_PS_TH_LOW_MASK);
	if (ret)
		return ret;
	ret = regmap_write(dev->regmap, AP3216C_PS_LOW_TH_HIGH,
					   (low >> 2) & AP3216C_PS_TH_HIGH_MASK);
	if (ret)
		return ret;
	ret = regmap_write(dev->regmap, AP3216C_PS_HIGH_TH_LOW,
					   high & AP3216C_PS_TH_LOW_MASK);
	if (ret)
		return ret;

	return regmap_write(dev->regmap, AP3216C_PS_HIGH_TH_HIGH,
						(high >> 2) & AP3216C_PS_TH_HIGH_MASK);
}

static int ap3216c_program_event_thresholds(struct ap3216c_dev *dev)
{
	u16 als_low = 0;
	u16 als_high = AP3216C_ALS_MAX_VALUE;
	u16 ps_low = 0;
	u16 ps_high = AP3216C_PS_MAX_VALUE;
	int ret;

	if (dev->event_enable_mask & AP3216C_EVENT_ALS_FALLING)
		als_low = dev->als_th.low;
	if (dev->event_enable_mask & AP3216C_EVENT_ALS_RISING)
		als_high = dev->als_th.high;
	if (dev->event_enable_mask & AP3216C_EVENT_PS_MASK)
	{
		ps_low = dev->ps_th.low;
		ps_high = dev->ps_th.high;
	}

	ret = ap3216c_write_als_threshold(dev, als_low, als_high);
	if (ret)
		return ret;

	return ap3216c_write_ps_threshold(dev, ps_low, ps_high);
}

static int ap3216c_clear_pending_events(struct ap3216c_dev *dev)
{
	struct ap3216c_sample sample;
	unsigned int read_mask = AP3216C_SAMPLE_ALS | AP3216C_SAMPLE_PS;

	return ap3216c_read_sample(dev, &sample, read_mask);
}

static int ap3216c_reginit(struct ap3216c_dev *dev)
{
	int ret;

	/* 初始化AP3216C */
	ret = regmap_write(dev->regmap, AP3216C_SYSTEM_CONFIG,
					   AP3216C_MODE_SW_RESET);
	if (ret)
		return ret;
	mdelay(50);

	ret = regmap_write(dev->regmap, AP3216C_INT_CLEAR_MANNER,
					   AP3216C_INT_CLEAR_BY_READ);
	if (ret)
		return ret;

	ret = regmap_update_bits(dev->regmap, AP3216C_PS_INT_MODE,
							 AP3216C_PS_INT_ALGO_MASK, dev->ps_int_algo);
	if (ret)
		return ret;

	ret = ap3216c_program_event_thresholds(dev);
	if (ret)
		return ret;

	ret = ap3216c_clear_pending_events(dev);
	if (ret)
		return ret;

	return ap3216c_set_mode(dev, AP3216C_MODE_ALS_PS_IR);
}

/* IIO ext-info callbacks */
static int ap3216c_set_operating_mode(struct iio_dev *indio_dev,
									  const struct iio_chan_spec *chan,
									  unsigned int mode)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	int ret;

	if (mode >= ARRAY_SIZE(ap3216c_mode_values))
		return -EINVAL;

	mutex_lock(&dev->lock);
	ret = ap3216c_set_mode(dev, ap3216c_mode_values[mode]);
	mutex_unlock(&dev->lock);

	return ret;
}

static int ap3216c_get_operating_mode(struct iio_dev *indio_dev,
									  const struct iio_chan_spec *chan)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	int mode;

	mutex_lock(&dev->lock);
	mode = ap3216c_mode_to_index(dev->mode);
	mutex_unlock(&dev->lock);

	return mode;
}

static int ap3216c_set_ps_int_algo(struct iio_dev *indio_dev,
								   const struct iio_chan_spec *chan,
								   unsigned int algo)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	int ret;

	if (algo >= ARRAY_SIZE(ap3216c_ps_int_algo_values))
		return -EINVAL;

	mutex_lock(&dev->lock);
	ret = regmap_update_bits(dev->regmap, AP3216C_PS_INT_MODE,
							 AP3216C_PS_INT_ALGO_MASK,
							 ap3216c_ps_int_algo_values[algo]);
	if (!ret)
	{
		dev->ps_int_algo = ap3216c_ps_int_algo_values[algo];
		ret = ap3216c_clear_pending_events(dev);
		if (ret)
			dev_warn(&dev->client->dev,
					 "clear pending events failed: %d\n", ret);
		ret = 0;
	}
	mutex_unlock(&dev->lock);

	return ret;
}

static int ap3216c_get_ps_int_algo(struct iio_dev *indio_dev,
								   const struct iio_chan_spec *chan)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	int algo;

	mutex_lock(&dev->lock);
	algo = ap3216c_ps_int_algo_to_index(dev->ps_int_algo);
	mutex_unlock(&dev->lock);

	return algo;
}

static const struct iio_enum ap3216c_operating_mode_enum = {
	.items = ap3216c_mode_names,
	.num_items = ARRAY_SIZE(ap3216c_mode_names),
	.set = ap3216c_set_operating_mode,
	.get = ap3216c_get_operating_mode,
};

static const struct iio_enum ap3216c_ps_int_algo_enum = {
	.items = ap3216c_ps_int_algo_names,
	.num_items = ARRAY_SIZE(ap3216c_ps_int_algo_names),
	.set = ap3216c_set_ps_int_algo,
	.get = ap3216c_get_ps_int_algo,
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
	{}};

static const struct iio_chan_spec_ext_info ap3216c_ps_ext_info[] = {
	IIO_ENUM("interrupt_algorithm", IIO_SHARED_BY_TYPE,
			 &ap3216c_ps_int_algo_enum),
	{
		.name = "interrupt_algorithm_available",
		.shared = IIO_SHARED_BY_TYPE,
		.read = iio_enum_available_read,
		.private = (uintptr_t)&ap3216c_ps_int_algo_enum,
	},
	{}};

/* IIO raw callbacks */
static int ap3216c_read_raw(struct iio_dev *indio_dev,
							struct iio_chan_spec const *chan,
							int *val, int *val2, long mask)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct ap3216c_sample sample;
	unsigned int read_mask;
	unsigned char regdata = 0;
	int ret = 0;

	switch (mask)
	{
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&dev->lock);

		ret = ap3216c_channel_read_mask(dev->mode, chan, &read_mask);
		if (ret)
			goto out_unlock;

		ret = ap3216c_read_sample(dev, &sample, read_mask);
		if (ret)
			goto out_unlock;

		switch (read_mask)
		{
		case AP3216C_SAMPLE_ALS:
			*val = sample.als;
			break;
		case AP3216C_SAMPLE_IR:
			if (sample.ir_overflow)
			{
				ret = -EOVERFLOW;
				goto out_unlock;
			}
			*val = sample.ir;
			break;
		case AP3216C_SAMPLE_PS:
			if (sample.ps_overflow)
			{
				ret = -EOVERFLOW;
				goto out_unlock;
			}
			*val = sample.ps;
			break;
		default:
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type)
		{
		case IIO_LIGHT: /* ALS量程 */
			mutex_lock(&dev->lock);
			regdata = (ap3216c_read_reg(dev, AP3216C_ALS_CONFIG) & 0X30) >> 4;
			*val = 0;
			*val2 = als_scale_ap3216c[regdata];
			mutex_unlock(&dev->lock);
			return IIO_VAL_INT_PLUS_MICRO; /* 值为val+val2/1000000 */
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_PROCESSED:
		if (chan->type != IIO_LIGHT)
			return -EINVAL;

		mutex_lock(&dev->lock);
		ret = ap3216c_channel_read_mask(dev->mode, chan, &read_mask);
		if (ret)
			goto out_unlock;

		ret = ap3216c_read_sample(dev, &sample, read_mask);
		if (ret)
			goto out_unlock;

		ret = ap3216c_read_als_processed(dev, sample.als, val, val2);
		break;
	default:
		return -EINVAL;
	}

out_unlock:
	mutex_unlock(&dev->lock);
	return ret;
}

static int ap3216c_write_raw(struct iio_dev *indio_dev,
							 struct iio_chan_spec const *chan,
							 int val, int val2, long mask)
{
	int ret = 0;
	struct ap3216c_dev *dev = iio_priv(indio_dev);

	switch (mask)
	{
	case IIO_CHAN_INFO_SCALE: /* 设置ALS量程 */
		switch (chan->type)
		{
		case IIO_LIGHT: /* 设置ALS量程 */
			mutex_lock(&dev->lock);
			ret = ap3216c_write_als_scale(dev, val2);
			mutex_unlock(&dev->lock);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ap3216c_write_raw_get_fmt(struct iio_dev *indio_dev,
									 struct iio_chan_spec const *chan, long mask)
{
	if (mask == IIO_CHAN_INFO_SCALE && chan->type == IIO_LIGHT)
		return IIO_VAL_INT_PLUS_MICRO;
	return -EINVAL;
}

/* IIO event callbacks */
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

	mutex_lock(&dev->lock);
	enabled = !!(dev->event_enable_mask & bit);
	mutex_unlock(&dev->lock);

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

	mutex_lock(&dev->lock);
	old_mask = dev->event_enable_mask;
	if (state)
		dev->event_enable_mask |= bit;
	else
		dev->event_enable_mask &= ~bit;

	ret = ap3216c_program_event_thresholds(dev);
	if (ret)
	{
		dev->event_enable_mask = old_mask;
	}
	else
	{
		ret = ap3216c_clear_pending_events(dev);
		if (ret)
			dev_warn(&dev->client->dev,
					 "clear pending events failed: %d\n", ret);
		ret = 0;
	}
	mutex_unlock(&dev->lock);

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

	if (type != IIO_EV_TYPE_THRESH || info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	mutex_lock(&dev->lock);
	switch (chan->type)
	{
	case IIO_LIGHT:
		if (dir == IIO_EV_DIR_RISING)
			*val = dev->als_th.high;
		else if (dir == IIO_EV_DIR_FALLING)
			*val = dev->als_th.low;
		else
			ret = -EINVAL;
		break;
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			*val = dev->ps_th.high;
		else if (dir == IIO_EV_DIR_FALLING)
			*val = dev->ps_th.low;
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dev->lock);

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
	struct ap3216c_threshold old_als_th;
	struct ap3216c_threshold old_ps_th;
	struct ap3216c_threshold als_th;
	struct ap3216c_threshold ps_th;
	int ret = 0;

	if (type != IIO_EV_TYPE_THRESH || info != IIO_EV_INFO_VALUE ||
		val < 0 || val2 != 0)
		return -EINVAL;

	mutex_lock(&dev->lock);
	old_als_th = dev->als_th;
	old_ps_th = dev->ps_th;
	als_th = old_als_th;
	ps_th = old_ps_th;

	switch (chan->type)
	{
	case IIO_LIGHT:
		if (val > AP3216C_ALS_MAX_VALUE)
		{
			ret = -EINVAL;
			break;
		}
		if (dir == IIO_EV_DIR_RISING)
			als_th.high = val;
		else if (dir == IIO_EV_DIR_FALLING)
			als_th.low = val;
		else
			ret = -EINVAL;
		break;
	case IIO_PROXIMITY:
		if (val > AP3216C_PS_MAX_VALUE)
		{
			ret = -EINVAL;
			break;
		}
		if (dir == IIO_EV_DIR_RISING)
			ps_th.high = val;
		else if (dir == IIO_EV_DIR_FALLING)
			ps_th.low = val;
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret && (als_th.low > als_th.high || ps_th.low > ps_th.high))
		ret = -EINVAL;
	if (!ret)
	{
		dev->als_th = als_th;
		dev->ps_th = ps_th;
		ret = ap3216c_program_event_thresholds(dev);
		if (ret)
		{
			dev->als_th = old_als_th;
			dev->ps_th = old_ps_th;
		}
	}
	if (!ret)
	{
		ret = ap3216c_clear_pending_events(dev);
		if (ret)
			dev_warn(&dev->client->dev,
					 "clear pending events failed: %d\n", ret);
		ret = 0;
	}
	mutex_unlock(&dev->lock);

	return ret;
}

static int ap3216c_push_als_event(struct iio_dev *indio_dev,
								  const struct ap3216c_sample *sample)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	enum iio_event_direction dir;
	unsigned int event_bit;
	u64 code;

	if (!(dev->event_enable_mask & AP3216C_EVENT_ALS_MASK))
		return 0;

	if (sample->als > dev->als_th.high)
	{
		dir = IIO_EV_DIR_RISING;
		event_bit = AP3216C_EVENT_ALS_RISING;
	}
	else if (sample->als < dev->als_th.low)
	{
		dir = IIO_EV_DIR_FALLING;
		event_bit = AP3216C_EVENT_ALS_FALLING;
	}
	else
	{
		return 0;
	}

	if (!(dev->event_enable_mask & event_bit))
		return 0;

	code = IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0, IIO_EV_TYPE_THRESH, dir);
	iio_push_event(indio_dev, code, iio_get_time_ns());

	return 1;
}

static int ap3216c_push_ps_event(struct iio_dev *indio_dev,
								 const struct ap3216c_sample *sample)
{
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	enum iio_event_direction dir;
	unsigned int event_bit;
	u64 code;

	if (!(dev->event_enable_mask & AP3216C_EVENT_PS_MASK))
		return 0;
	if (sample->ps_overflow)
	{
		dev_warn_ratelimited(&dev->client->dev,
							 "PS sample overflow; proximity event skipped\n");
		return 0;
	}

	if (sample->ps_object)
	{
		dir = IIO_EV_DIR_RISING;
		event_bit = AP3216C_EVENT_PS_RISING;
	}
	else
	{
		dir = IIO_EV_DIR_FALLING;
		event_bit = AP3216C_EVENT_PS_FALLING;
	}

	if (!(dev->event_enable_mask & event_bit))
		return 0;

	code = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0, IIO_EV_TYPE_THRESH, dir);
	iio_push_event(indio_dev, code, iio_get_time_ns());

	return 1;
}

static irqreturn_t ap3216c_irq_thread(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ap3216c_dev *dev = iio_priv(indio_dev);
	struct ap3216c_sample sample;
	unsigned int active;
	unsigned int read_mask;
	unsigned int status;
	unsigned int status_reg;
	int ret;

	mutex_lock(&dev->lock);
	ret = regmap_read(dev->regmap, AP3216C_INT_STATUS, &status_reg);
	if (ret)
		goto out_unlock;

	status = status_reg & AP3216C_INT_STATUS_MASK;
	dev->last_int_status = status;
	if (!status)
		goto out_unlock;

	active = ap3216c_mode_to_sample_mask(dev->mode);
	read_mask = active & AP3216C_SAMPLE_ALL;
	if (!read_mask)
	{
		ap3216c_clear_pending_events(dev);
		goto out_unlock;
	}

	ret = ap3216c_read_sample(dev, &sample, read_mask);
	if (ret)
		goto out_unlock;

	if ((status & AP3216C_INTSTATUS_ALS_BIT) &&
		(active & AP3216C_SAMPLE_ALS))
		ap3216c_push_als_event(indio_dev, &sample);

	if ((status & AP3216C_INTSTATUS_PS_BIT) &&
		(active & AP3216C_SAMPLE_PS))
		ap3216c_push_ps_event(indio_dev, &sample);

out_unlock:
	mutex_unlock(&dev->lock);
	return IRQ_HANDLED;
}

/* IIO descriptors */
static const struct iio_event_spec ap3216c_als_events[] = {
	{.type = IIO_EV_TYPE_THRESH, .dir = IIO_EV_DIR_RISING, .mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE)},
	{.type = IIO_EV_TYPE_THRESH, .dir = IIO_EV_DIR_FALLING, .mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE)},
};

static const struct iio_event_spec ap3216c_ps_events[] = {
	{.type = IIO_EV_TYPE_THRESH, .dir = IIO_EV_DIR_RISING, .mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE)},
	{.type = IIO_EV_TYPE_THRESH, .dir = IIO_EV_DIR_FALLING, .mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE)},
};

/*
 * ap3216c通道，1路ALS(环境关)，1路PS(距离传感器)，1路IR
 */
static const struct iio_chan_spec ap3216c_channels[] = {
	/* ALS通道 */
	{
		.type = IIO_LIGHT,
		.address = AP3216C_ALS_DATA_LOW,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
							  BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_PROCESSED),
		.ext_info = ap3216c_ext_info,
		.event_spec = ap3216c_als_events,
		.num_event_specs = ARRAY_SIZE(ap3216c_als_events),
	},

	/* PS通道 */
	{
		.type = IIO_PROXIMITY,
		.address = AP3216C_PS_DATA_LOW,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.ext_info = ap3216c_ps_ext_info,
		.event_spec = ap3216c_ps_events,
		.num_event_specs = ARRAY_SIZE(ap3216c_ps_events),
	},

	/* IR通道 */
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.address = AP3216C_IR_DATA_LOW,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
};

/*
 * iio_info结构体变量
 */
static const struct iio_info ap3216c_info = {
	.read_raw = ap3216c_read_raw,
	.write_raw = ap3216c_write_raw,
	.write_raw_get_fmt = &ap3216c_write_raw_get_fmt, /* 用户空间写数据格式 */
	.read_event_config = ap3216c_read_event_config,
	.write_event_config = ap3216c_write_event_config,
	.read_event_value = ap3216c_read_event_value,
	.write_event_value = ap3216c_write_event_value,
};

/* I2C driver entry */
static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct ap3216c_dev *dev;
	struct iio_dev *indio_dev;

	/*  1、申请iio_dev内存 */
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*dev));
	if (!indio_dev)
		return -ENOMEM;

	/* 2、获取ap3216c_dev结构体地址 */
	dev = iio_priv(indio_dev);
	dev->client = client;

	i2c_set_clientdata(client, indio_dev); /* 保存ap3216cdev结构体 */

	/* 初始化regmap_config设置 */
	dev->regmap_config.reg_bits = 8; /* 寄存器长度8bit */
	dev->regmap_config.val_bits = 8; /* 值长度8bit */

	/* 初始化IIC接口的regmap */
	dev->regmap = regmap_init_i2c(client, &dev->regmap_config);
	if (IS_ERR(dev->regmap))
	{
		ret = PTR_ERR(dev->regmap);
		goto err_regmap_exit;
	}

	mutex_init(&dev->lock);

	/* 中断号及相关初始化参数 */
	dev->irq = client->irq;
	dev->als_th.low = 0;
	dev->als_th.high = AP3216C_ALS_MAX_VALUE;
	dev->event_enable_mask = 0;
	dev->last_int_status = 0;
	dev->ps_th.low = 100;
	dev->ps_th.high = 200;
	dev->ps_int_algo = AP3216C_PS_INT_ALGO_HYSTERESIS;

	if (dev->irq > 0)
	{
		ret = devm_request_threaded_irq(&client->dev, dev->irq, NULL,
										ap3216c_irq_thread,
										IRQF_ONESHOT | IRQF_TRIGGER_LOW,
										AP3216C_NAME, indio_dev);
		if (ret)
			goto err_regmap_exit;
	}

	ret = ap3216c_reginit(dev);
	if (ret)
		goto err_regmap_exit;

	/* 4、iio_dev的其他成员变量 */
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ap3216c_info;
	indio_dev->name = AP3216C_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE; /* 直接模式，提供sysfs接口 */
	indio_dev->channels = ap3216c_channels;
	indio_dev->num_channels = ARRAY_SIZE(ap3216c_channels);

	/* 5、注册iio_dev */
	ret = iio_device_register(indio_dev);
	dev_info(&dev->client->dev, "registed!\n");

	if (ret < 0)
	{
		dev_err(&client->dev, "iio_device_register failed\n");
		goto err_regmap_exit;
	}
	return 0;

err_regmap_exit:
	regmap_exit(dev->regmap);
	return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ap3216c_dev *dev;

	dev = iio_priv(indio_dev);
	mutex_lock(&dev->lock);
	ap3216c_set_mode(dev, AP3216C_MODE_POWER_DOWN);
	mutex_unlock(&dev->lock);

	iio_device_unregister(indio_dev);
	regmap_exit(dev->regmap);

	return 0;
}

static const struct i2c_device_id ap3216c_id[] = {
	{"ap3216c", 0},
	{}};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

static const struct of_device_id ap3216c_of_match[] = {
	{.compatible = "alientek,ap3216c"},
	{/* Sentinel */}};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static struct i2c_driver ap3216c_driver = {
	.probe = ap3216c_probe,
	.remove = ap3216c_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "ap3216c",
		.of_match_table = ap3216c_of_match,
	},
	.id_table = ap3216c_id,
};

module_i2c_driver(ap3216c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FriedEgg");
