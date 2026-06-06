#define pr_fmt(fmt) "ap3216c: " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include "ap3216creg.h"

#define AP3216C_CNT 1
#define AP3216C_NAME "ap3216c"
#define AP3216C_POLL_INTERVAL_MS 10
#define AP3216C_DEFAULT_PS_TRIGGER_TH 200
#define AP3216C_DEFAULT_ALS_DELTA_TH 200
#define AP3216C_POLL_INTERVAL_MIN_MS 5
#define AP3216C_POLL_INTERVAL_MAX_MS 1000
#define AP3216C_PS_MAX_VALUE 1023
#define AP3216C_ALS_MAX_VALUE 65535

enum ap3216c_runtime_event_mode
{
	AP3216C_RUNTIME_EVENT_MODE_UNKNOWN = AP3216C_EVENT_MODE_UNKNOWN,
	AP3216C_RUNTIME_EVENT_MODE_HW_IRQ = AP3216C_EVENT_MODE_HW_IRQ,
	AP3216C_RUNTIME_EVENT_MODE_POLL_SIM = AP3216C_EVENT_MODE_POLL_SIM,
};

struct ap3216c_dev
{
	dev_t devid;				/* 设备号 	 */
	struct cdev cdev;			/* cdev 	*/
	struct class *class;		/* 类 		*/
	struct device *device;		/* 设备 	 */
	int major;					/* 主设备号 */
	void *private_data;			/* 私有数据 */
	unsigned short ir, als, ps; /* 三个光传感器数据 */
	atomic_t open_cnt;			/* 设备打开计数：独占策略下仅允许 0/1 */
	struct mutex bus_lock;		/* 保护可睡眠的 I2C 访问路径 */
	spinlock_t data_lock;		/* 保护 ir/als/ps 内存态一致性 */
	int irq;
	bool irq_requested;
	enum ap3216c_runtime_event_mode event_mode;
	struct delayed_work poll_work;
	unsigned int poll_interval_ms;
	unsigned int ps_trigger_th;
	unsigned int als_delta_th;
	unsigned int last_als_sample;
	bool als_baseline_valid;
	struct ap3216c_event_stats event_stats;
};

static struct ap3216c_dev ap3216cdev;

static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *val, int len);
static int ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 data);
static irqreturn_t ap3216c_irq_thread(int irq, void *dev_id);
static int ap3216c_readdata(struct ap3216c_dev *dev, bool interruptible);

static int ap3216c_lock_bus(struct ap3216c_dev *dev, bool interruptible)
{
	if (interruptible)
		return mutex_lock_interruptible(&dev->bus_lock);

	mutex_lock(&dev->bus_lock);
	return 0;
}

static int ap3216c_request_hw_irq(struct ap3216c_dev *dev)
{
	int ret;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	if (dev->irq_requested)
		return 0;

	if (dev->irq <= 0)
		return -ENODEV;

	ret = request_threaded_irq(dev->irq,
							   NULL,
							   ap3216c_irq_thread,
							   IRQF_ONESHOT | IRQF_TRIGGER_LOW,
							   AP3216C_NAME,
							   dev);
	if (ret)
	{
		if (client)
			dev_warn(&client->dev, "request irq failed: irq=%d ret=%d\n", dev->irq, ret);
		return ret;
	}

	dev->irq_requested = true;
	if (client)
		dev_info(&client->dev, "irq enabled: irq=%d\n", dev->irq);
	return 0;
}

static void ap3216c_release_hw_irq(struct ap3216c_dev *dev)
{
	if (!dev->irq_requested)
		return;

	free_irq(dev->irq, dev);
	dev->irq_requested = false;
}

static bool ap3216c_should_trigger_event(struct ap3216c_dev *dev,
										 unsigned int ps,
										 unsigned int als,
										 unsigned int *als_delta)
{
	unsigned long flags;
	unsigned int delta = 0;
	unsigned int prev_als = 0;
	unsigned int ps_th;
	unsigned int als_th;
	bool has_baseline;
	bool trigger = false;

	spin_lock_irqsave(&dev->data_lock, flags);
	ps_th = dev->ps_trigger_th;
	als_th = dev->als_delta_th;
	has_baseline = dev->als_baseline_valid;
	if (has_baseline)
	{
		prev_als = dev->last_als_sample;
		delta = (als >= prev_als) ? (als - prev_als) : (prev_als - als);
	}

	if (ps >= ps_th)
		trigger = true;

	if (has_baseline && als_th > 0 && delta >= als_th)
		trigger = true;

	dev->last_als_sample = als;
	dev->als_baseline_valid = true;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (als_delta)
		*als_delta = delta;

	return trigger;
}

static int ap3216c_set_event_mode(struct ap3216c_dev *dev,
								  enum ap3216c_runtime_event_mode target_mode)
{
	unsigned long flags;
	unsigned int poll_interval;
	int ret = 0;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	if (target_mode != AP3216C_RUNTIME_EVENT_MODE_HW_IRQ &&
		target_mode != AP3216C_RUNTIME_EVENT_MODE_POLL_SIM)
		return -EINVAL;

	spin_lock_irqsave(&dev->data_lock, flags);
	if (dev->event_mode == target_mode)
	{
		spin_unlock_irqrestore(&dev->data_lock, flags);
		return 0;
	}
	poll_interval = dev->poll_interval_ms;
	dev->event_mode = target_mode;
	dev->als_baseline_valid = false;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (target_mode == AP3216C_RUNTIME_EVENT_MODE_HW_IRQ)
	{
		cancel_delayed_work_sync(&dev->poll_work);
		ret = ap3216c_request_hw_irq(dev);
		if (ret)
		{
			spin_lock_irqsave(&dev->data_lock, flags);
			dev->event_mode = AP3216C_RUNTIME_EVENT_MODE_POLL_SIM;
			spin_unlock_irqrestore(&dev->data_lock, flags);
			schedule_delayed_work(&dev->poll_work, msecs_to_jiffies(poll_interval));
			return ret;
		}
		if (client)
			dev_info(&client->dev, "switch event mode: HW_IRQ\n");
		return 0;
	}

	ap3216c_release_hw_irq(dev);
	schedule_delayed_work(&dev->poll_work, msecs_to_jiffies(poll_interval));
	if (client)
		dev_info(&client->dev, "switch event mode: POLL_SIM interval=%ums\n", poll_interval);

	return 0;
}

/* 统一事件处理入口：真实中断与轮询注入都走这里，保证框架一致。 */
static void ap3216c_event_core_handle(struct ap3216c_dev *dev, unsigned int source, unsigned int ps)
{
	unsigned long flags;
	unsigned int total_events;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->event_stats.total_events++;
	total_events = dev->event_stats.total_events;
	dev->event_stats.last_ps = ps;
	dev->event_stats.last_source = source;
	switch (source)
	{
	case AP3216C_EVENT_SRC_HW_IRQ:
		dev->event_stats.hw_irq_events++;
		break;
	case AP3216C_EVENT_SRC_POLL_SIM:
		dev->event_stats.poll_sim_events++;
		break;
	case AP3216C_EVENT_SRC_MANUAL:
		dev->event_stats.manual_events++;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (client)
		dev_info(&client->dev, "event handled: src=%u ps=%u total=%u\n",
				 source, ps, total_events);
}

/* 轮询任务：硬件 IRQ 不可用时，周期检测 PS 阈值并注入事件。 */
static void ap3216c_poll_work_func(struct work_struct *work)
{
	unsigned long flags;
	unsigned int curr_ps = 0;
	unsigned int curr_als = 0;
	unsigned int als_delta;
	unsigned int poll_interval;
	bool should_trigger;
	bool poll_enabled;
	struct ap3216c_dev *dev = container_of(to_delayed_work(work),
									   struct ap3216c_dev,
									   poll_work);
	struct i2c_client *client = (struct i2c_client *)dev->private_data;
	int ret;

	ret = ap3216c_readdata(dev, false);

	spin_lock_irqsave(&dev->data_lock, flags);
	poll_interval = dev->poll_interval_ms;
	poll_enabled = (dev->event_mode == AP3216C_RUNTIME_EVENT_MODE_POLL_SIM);
	if (!ret)
	{
		curr_ps = dev->ps;
		curr_als = dev->als;
	}
	spin_unlock_irqrestore(&dev->data_lock, flags);

	if (!poll_enabled)
		return;

	if (ret)
	{
		if (client)
			dev_dbg(&client->dev, "poll read failed: ret=%d\n", ret);
		goto reschedule;
	}

	should_trigger = ap3216c_should_trigger_event(dev, curr_ps, curr_als, &als_delta);
	if (should_trigger)
	{
		if (client)
			dev_dbg(&client->dev,
					"poll trigger: ps=%u als=%u delta=%u\n",
					curr_ps,
					curr_als,
					als_delta);
		ap3216c_event_core_handle(dev, AP3216C_EVENT_SRC_POLL_SIM, curr_ps);
	}

reschedule:
	schedule_delayed_work(&dev->poll_work, msecs_to_jiffies(poll_interval));
}

static void ap3216c_clear_irq(struct ap3216c_dev *dev)
{
	int ret;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	ret = ap3216c_lock_bus(dev, false);
	if (ret)
		return;

	ret = ap3216c_write_reg(dev, AP3216C_INTCLEAR, 0x00);
	mutex_unlock(&dev->bus_lock);

	if (ret && client)
		dev_warn(&client->dev, "clear irq failed: ret=%d\n", ret);
}

/* 真实 IRQ 线程化处理：当前保留框架，便于后续硬件恢复时直接启用。 */
static irqreturn_t ap3216c_irq_thread(int irq, void *dev_id)
{
	u8 status = 0;
	unsigned int ps;
	unsigned int als;
	unsigned int als_delta;
	unsigned long flags;
	bool should_trigger;
	int ret;
	struct ap3216c_dev *dev = (struct ap3216c_dev *)dev_id;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->event_stats.irq_entries++;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	ret = ap3216c_lock_bus(dev, false);
	if (ret)
		return IRQ_HANDLED;
	ret = ap3216c_read_regs(dev, AP3216C_INTSTATUS, &status, 1);
	mutex_unlock(&dev->bus_lock);
	if (ret)
	{
		if (client)
			dev_warn(&client->dev, "read irq status failed: ret=%d\n", ret);
		ap3216c_clear_irq(dev);
		return IRQ_HANDLED;
	}

	if (!(status & (AP3216C_INTSTATUS_PS_BIT | AP3216C_INTSTATUS_ALS_BIT)))
	{
		spin_lock_irqsave(&dev->data_lock, flags);
		dev->event_stats.irq_no_status_events++;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		if (client)
			dev_dbg(&client->dev, "irq without interested status bits: 0x%02x\n", status);
		ap3216c_clear_irq(dev);
		return IRQ_HANDLED;
	}

	ret = ap3216c_readdata(dev, false);
	if (ret)
	{
		if (client)
			dev_warn(&client->dev, "irq data read failed: ret=%d\n", ret);
		ap3216c_clear_irq(dev);
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&dev->data_lock, flags);
	ps = dev->ps;
	als = dev->als;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	should_trigger = ap3216c_should_trigger_event(dev, ps, als, &als_delta);
	if (should_trigger)
		ap3216c_event_core_handle(dev, AP3216C_EVENT_SRC_HW_IRQ, ps);
	else
	{
		spin_lock_irqsave(&dev->data_lock, flags);
		dev->event_stats.irq_filtered_events++;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		if (client)
			dev_dbg(&client->dev, "irq filtered by threshold: ps=%u als=%u delta=%u\n",
					ps,
					als,
					als_delta);
	}

	/* 按寄存器手册清中断，避免中断线粘连。 */
	ap3216c_clear_irq(dev);
	return IRQ_HANDLED;
}

/*
 * @description	: 从ap3216c读取多个寄存器数据
 * @param - dev:  ap3216c设备
 * @param - reg:  要读取的寄存器首地址
 * @param - val:  读取到的数据
 * @param - len:  要读取的数据长度
 * @return 		: 操作结果
 */
static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *val, int len)
{
	int ret;
	struct i2c_msg msg[2];
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	if (len <= 0)
		return -EINVAL;

	/* AP3216C 读取使用“写寄存器地址 + repeated start + 连续读”的组合事务。 */
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = 1;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = len;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret == 2)
	{
		dev_dbg(&client->dev, "i2c read ok: reg=0x%02x len=%d\n", reg, len);
		return 0;
	}

	dev_err(&client->dev, "i2c read failed: ret=%d reg=0x%02x len=%d\n", ret, reg, len);
	return (ret < 0) ? ret : -EREMOTEIO;
}

/*
 * @description	: 向ap3216c多个寄存器写入数据
 * @param - dev:  ap3216c设备
 * @param - reg:  要写入的寄存器首地址
 * @param - val:  要写入的数据缓冲区
 * @param - len:  要写入的数据长度
 * @return 	  :   操作结果
 */
static int ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg, const u8 *buf, u8 len)
{
	int ret;
	u8 b[256];
	struct i2c_msg msg;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

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
	{
		dev_dbg(&client->dev, "i2c write ok: reg=0x%02x len=%u\n", reg, len);
		return 0;
	}

	dev_err(&client->dev, "i2c write failed: ret=%d reg=0x%02x len=%u\n", ret, reg, len);
	return (ret < 0) ? ret : -EREMOTEIO;
}

/*
 * @description	: 向ap3216c指定寄存器写入指定的值，写一个寄存器
 * @param - dev:  ap3216c设备
 * @param - reg:  要写的寄存器
 * @param - data: 要写入的值
 * @return   :    操作结果
 */
static int ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 data)
{
	return ap3216c_write_regs(dev, reg, &data, 1);
}

/*
 * 寄存器位更新工具函数：
 * 先读原值，再按 mask 更新目标位，最后写回，避免覆盖无关位。
 */
static int ap3216c_update_bits(struct ap3216c_dev *dev, u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 old_val;
	u8 new_val;

	/*
	 * update_bits 需要保持“读-改-写”原子语义，
	 * 因为 i2c_transfer 可能睡眠，这里必须使用 mutex。
	 */
	ret = ap3216c_lock_bus(dev, true);
	if (ret)
		return ret;

	ret = ap3216c_read_regs(dev, reg, &old_val, 1);
	if (ret)
	{
		mutex_unlock(&dev->bus_lock);
		return ret;
	}

	new_val = (old_val & (~mask)) | (val & mask);
	if (new_val == old_val)
	{
		mutex_unlock(&dev->bus_lock);
		return 0;
	}

	ret = ap3216c_write_reg(dev, reg, new_val);
	mutex_unlock(&dev->bus_lock);
	return ret;
}

static int ap3216c_hw_init(struct ap3216c_dev *dev)
{
	int ret;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	ret = ap3216c_lock_bus(dev, true);
	if (ret)
		return ret;

	ret = ap3216c_write_reg(dev, AP3216C_SYSTEMCONG, 0x04);
	if (ret)
	{
		dev_err(&client->dev, "reset sensor failed: ret=%d\n", ret);
		goto out_unlock;
	}

	msleep(50);
	ret = ap3216c_write_reg(dev, AP3216C_SYSTEMCONG, AP3216C_MODE_ALS_PS_IR);
	if (ret)
	{
		dev_err(&client->dev, "enable sensor failed: ret=%d\n", ret);
		goto out_unlock;
	}

	ret = ap3216c_write_reg(dev, AP3216C_PS_INT_FORM, 0x01);
	if (ret)
		dev_err(&client->dev, "set ps interrupt form failed: ret=%d\n", ret);

out_unlock:
	mutex_unlock(&dev->bus_lock);
	return ret;
}

static int ap3216c_config_ps_threshold_locked(struct ap3216c_dev *dev, unsigned int threshold)
{
	int ret;

	/* 0x2C 保存低 2 位，0x2D 保存高 8 位，组合成 10-bit PS 阈值。 */
	ret = ap3216c_write_reg(dev, AP3216C_PS_HTH_L, threshold & 0x03);
	if (ret)
		return ret;

	ret = ap3216c_write_reg(dev, AP3216C_PS_HTH_H, (threshold >> 2) & 0xFF);
	if (ret)
		return ret;

	ret = ap3216c_write_reg(dev, AP3216C_PS_LTH_L, 0x00);
	if (ret)
		return ret;

	return ap3216c_write_reg(dev, AP3216C_PS_LTH_H, 0x00);
}

/*
 * @description	: 读取 AP3216C 的 IR/ALS/PS 原始数据并更新内存快照。
 *				: 如果同时打开 ALS、IR+PS，两次数据读取的时间间隔要大于 112.5ms。
 * @param - dev	: AP3216C 设备
 * @param - interruptible: 是否允许等待 bus_lock 时被信号打断
 * @return 		: 0 成功；负值为错误码。
 */
static int ap3216c_readdata(struct ap3216c_dev *dev, bool interruptible)
{
	unsigned long flags;
	u8 buf[6];
	unsigned short ir;
	unsigned short als;
	unsigned short ps;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;
	int ret;

	ret = ap3216c_lock_bus(dev, interruptible);
	if (ret)
		return ret;

	ret = ap3216c_read_regs(dev, AP3216C_IRDATALOW, buf, sizeof(buf));
	mutex_unlock(&dev->bus_lock);
	if (ret)
		return ret;

	/* IR: bit7 为无效标志，bit[1:0] 为低 2 位，下一字节为高 8 位。 */
	if (buf[0] & AP3216C_IR_OF_BIT)
		ir = 0;
	else
		ir = ((unsigned short)buf[1] << 2) | (buf[0] & AP3216C_IR_DATA_L_MASK);

	als = ((unsigned short)buf[3] << 8) | buf[2];

	/* PS: bit6 为无效标志，高字节 bit[5:0] 和低字节 bit[3:0] 组合为 10 位值。 */
	if (buf[4] & AP3216C_PS_OF_BIT)
		ps = 0;
	else
		ps = ((unsigned short)(buf[5] & AP3216C_PS_DATA_H_MASK) << 4) |
			 (buf[4] & AP3216C_PS_DATA_L_MASK);

	spin_lock_irqsave(&dev->data_lock, flags);
	dev->ir = ir;
	dev->als = als;
	dev->ps = ps;
	spin_unlock_irqrestore(&dev->data_lock, flags);

	dev_dbg(&client->dev, "sensor raw: ir=%u als=%u ps=%u\n", ir, als, ps);
	return 0;
}

/*
 * @description		: 打开设备
 * @param - inode 	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 * 					  一般在open的时候将private_data指向设备结构体。
 * @return 			: 0 成功;其他 失败
 */
static int ap3216c_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct i2c_client *client = (struct i2c_client *)ap3216cdev.private_data;

	if (!client)
	{
		pr_err("open failed: i2c client not ready\n");
		return -ENODEV;
	}

	/* 独占打开：仅当 open_cnt 仍为 0 时允许当前进程持有设备。 */
	if (atomic_cmpxchg(&ap3216cdev.open_cnt, 0, 1) != 0)
	{
		dev_warn(&client->dev, "device busy, reject open\n");
		return -EBUSY;
	}

	ret = ap3216c_hw_init(&ap3216cdev);
	if (ret)
	{
		atomic_set(&ap3216cdev.open_cnt, 0);
		return ret;
	}

	filp->private_data = &ap3216cdev;
	dev_info(&client->dev, "device opened, open_cnt=%d\n", atomic_read(&ap3216cdev.open_cnt));
	return 0;
}

/*
 * @description		: 从设备读取数据
 * @param - filp 	: 要打开的设备文件(文件描述符)
 * @param - buf 	: 返回给用户空间的数据缓冲区
 * @param - cnt 	: 要读取的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return 			: 读取的字节数，如果为负值，表示读取失败
 */
static ssize_t ap3216c_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	unsigned long flags;
	unsigned short data[3];
	unsigned long err = 0;
	int ret;

	struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;

	/* 用户缓冲区至少要能容纳 3 个 short: IR/ALS/PS。 */
	if (cnt < sizeof(data))
	{
		dev_warn(&client->dev, "read buffer too small: cnt=%zu need=%zu\n", cnt, sizeof(data));
		return -EINVAL;
	}

	ret = ap3216c_readdata(dev, true);
	if (ret)
		return ret;

	/* 先用 spinlock 拿到一致快照，再在锁外 copy_to_user。 */
	spin_lock_irqsave(&dev->data_lock, flags);
	data[0] = dev->ir;
	data[1] = dev->als;
	data[2] = dev->ps;
	spin_unlock_irqrestore(&dev->data_lock, flags);
	/*
	 * copy_to_user 是内核向用户空间拷贝数据的标准 API。
	 * 不能直接解引用用户空间指针，否则可能触发页错误或安全问题。
	 */
	err = copy_to_user(buf, data, sizeof(data));
	if (err)
	{
		dev_err(&client->dev, "copy_to_user failed: uncopied=%ld\n", err);
		return -EFAULT;
	}

	return sizeof(data);
}

/*
 * @description		: 关闭/释放设备
 * @param - filp 	: 要关闭的设备文件(文件描述符)
 * @return 			: 0 成功;其他 失败
 */
static int ap3216c_release(struct inode *inode, struct file *filp)
{
	struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;
	struct i2c_client *client;
	int old;

	if (!dev || !dev->private_data)
	{
		pr_warn("release without valid context\n");
		atomic_set(&ap3216cdev.open_cnt, 0);
		return 0;
	}

	client = (struct i2c_client *)dev->private_data;

	old = atomic_cmpxchg(&dev->open_cnt, 1, 0);
	if (old != 1)
	{
		dev_warn(&client->dev, "release with unexpected open_cnt=%d, force reset\n", old);
		atomic_set(&dev->open_cnt, 0);
		return 0;
	}

	dev_info(&client->dev, "device released, open_cnt=%d\n", atomic_read(&dev->open_cnt));
	return 0;
}

/*
 * @description		: ioctl 配置接口
 * @param - filp 	: 设备文件
 * @param - cmd  	: ioctl 命令
 * @param - arg  	: 参数（本项目使用整型位值直传）
 * @return 			: 0 成功; 负值 失败
 */
static long ap3216c_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int val = (int)arg;
	int ret = 0;
	unsigned long flags;
	int mode;
	unsigned int ps_snapshot;
	struct ap3216c_event_stats stats;
	struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;
	struct i2c_client *client;

	if (!dev || !dev->private_data)
		return -ENODEV;

	client = (struct i2c_client *)dev->private_data;

	switch (cmd)
	{
	case AP3216C_CMD_SET_MODE:
		if (val < AP3216C_MODE_POWER_DOWN || val > AP3216C_MODE_ALS_PS_IR)
		{
			dev_warn(&client->dev, "invalid mode=%d\n", val);
			return -EINVAL;
		}

		ret = ap3216c_lock_bus(dev, true);
		if (ret)
			return ret;
		ret = ap3216c_write_reg(dev, AP3216C_SYSTEMCONG, (u8)val);
		mutex_unlock(&dev->bus_lock);
		if (ret)
		{
			dev_err(&client->dev, "set mode failed: mode=%d ret=%d\n", val, ret);
			return ret;
		}

		/* 模式切换后留出稳定时间，避免紧接着读取得到瞬态值。 */
		msleep(20);
		dev_info(&client->dev, "set mode ok: mode=%d\n", val);
		break;

	case AP3216C_CMD_SET_ALS_RATE:
		if (val < AP3216C_ALS_RATE_MIN || val > AP3216C_ALS_RATE_MAX)
		{
			dev_warn(&client->dev, "invalid als_rate=0x%x\n", val);
			return -EINVAL;
		}

		ret = ap3216c_update_bits(dev, AP3216C_ALSCONFIG, AP3216C_ALS_RATE_MASK, (u8)val);
		if (ret)
		{
			dev_err(&client->dev, "set als_rate failed: rate=0x%x ret=%d\n", val, ret);
			return ret;
		}

		dev_info(&client->dev, "set als_rate ok: rate=0x%x\n", val);
		break;

	case AP3216C_CMD_SET_PS_RATE:
		if (val < AP3216C_PS_RATE_MIN || val > AP3216C_PS_RATE_MAX)
		{
			dev_warn(&client->dev, "invalid ps_rate=0x%x\n", val);
			return -EINVAL;
		}

		ret = ap3216c_update_bits(dev, AP3216C_PSCONFIG, AP3216C_PS_RATE_MASK, (u8)val);
		if (ret)
		{
			dev_err(&client->dev, "set ps_rate failed: rate=0x%x ret=%d\n", val, ret);
			return ret;
		}

		dev_info(&client->dev, "set ps_rate ok: rate=0x%x\n", val);
		break;

	case AP3216C_CMD_GET_EVENT_MODE:
		spin_lock_irqsave(&dev->data_lock, flags);
		mode = (int)dev->event_mode;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		if (copy_to_user((void __user *)arg, &mode, sizeof(mode)))
			return -EFAULT;
		break;

	case AP3216C_CMD_GET_EVENT_STATS:
		spin_lock_irqsave(&dev->data_lock, flags);
		stats = dev->event_stats;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			return -EFAULT;
		break;

	case AP3216C_CMD_TRIGGER_EVENT:
		ret = ap3216c_readdata(dev, true);
		if (ret)
			return ret;
		spin_lock_irqsave(&dev->data_lock, flags);
		ps_snapshot = dev->ps;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		ap3216c_event_core_handle(dev, AP3216C_EVENT_SRC_MANUAL, ps_snapshot);
		break;

	case AP3216C_CMD_SET_EVENT_MODE:
		if (val != AP3216C_EVENT_MODE_HW_IRQ && val != AP3216C_EVENT_MODE_POLL_SIM)
			return -EINVAL;
		ret = ap3216c_set_event_mode(dev, (enum ap3216c_runtime_event_mode)val);
		if (ret)
			return ret;
		break;

	case AP3216C_CMD_SET_PS_TRIGGER_TH:
		if (val < 0 || val > AP3216C_PS_MAX_VALUE)
			return -EINVAL;

		ret = ap3216c_lock_bus(dev, true);
		if (ret)
			return ret;
		ret = ap3216c_config_ps_threshold_locked(dev, (unsigned int)val);
		mutex_unlock(&dev->bus_lock);
		if (ret)
		{
			dev_err(&client->dev, "set ps trigger threshold failed: threshold=%d ret=%d\n", val, ret);
			return ret;
		}

		spin_lock_irqsave(&dev->data_lock, flags);
		dev->ps_trigger_th = (unsigned int)val;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		dev_info(&client->dev, "set ps trigger threshold=%d to HW regs\n", val);
		break;

	case AP3216C_CMD_SET_ALS_DELTA_TH:
		if (val < 0 || val > AP3216C_ALS_MAX_VALUE)
			return -EINVAL;
		spin_lock_irqsave(&dev->data_lock, flags);
		dev->als_delta_th = (unsigned int)val;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		dev_info(&client->dev, "set als delta threshold=%d\n", val);
		break;

	case AP3216C_CMD_SET_POLL_INTERVAL_MS:
		if (val < AP3216C_POLL_INTERVAL_MIN_MS || val > AP3216C_POLL_INTERVAL_MAX_MS)
			return -EINVAL;
		spin_lock_irqsave(&dev->data_lock, flags);
		dev->poll_interval_ms = (unsigned int)val;
		mode = (int)dev->event_mode;
		spin_unlock_irqrestore(&dev->data_lock, flags);
		if (mode == AP3216C_RUNTIME_EVENT_MODE_POLL_SIM)
		{
			cancel_delayed_work_sync(&dev->poll_work);
			schedule_delayed_work(&dev->poll_work,
								  msecs_to_jiffies((unsigned int)val));
		}
		dev_info(&client->dev, "set poll interval=%dms\n", val);
		break;

	default:
		dev_warn(&client->dev, "unsupported ioctl cmd=0x%x\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

/* AP3216C操作函数 */
static const struct file_operations ap3216c_ops = {
	.owner = THIS_MODULE,
	.open = ap3216c_open,
	.read = ap3216c_read,
	.unlocked_ioctl = ap3216c_unlocked_ioctl,
	.release = ap3216c_release,
};

/*
 * @description     : i2c驱动的probe函数，当驱动与
 *                    设备匹配以后此函数就会执行
 * @param - client  : i2c设备
 * @param - id      : i2c设备ID
 * @return          : 0，成功;其他负值,失败
 */
static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	/* 1、构建设备号 */
	if (ap3216cdev.major)
	{
		ap3216cdev.devid = MKDEV(ap3216cdev.major, 0);
		ret = register_chrdev_region(ap3216cdev.devid, AP3216C_CNT, AP3216C_NAME);
	}
	else
	{
		ret = alloc_chrdev_region(&ap3216cdev.devid, 0, AP3216C_CNT, AP3216C_NAME);
		ap3216cdev.major = MAJOR(ap3216cdev.devid);
	}
	if (ret < 0)
	{
		dev_err(&client->dev, "alloc chrdev region failed: ret=%d\n", ret);
		return ret;
	}

	/* 2、注册设备 */
	cdev_init(&ap3216cdev.cdev, &ap3216c_ops);
	ret = cdev_add(&ap3216cdev.cdev, ap3216cdev.devid, AP3216C_CNT);
	if (ret)
	{
		dev_err(&client->dev, "cdev add failed: ret=%d\n", ret);
		goto err_unregister_chrdev;
	}

	/* 3、创建类 */
	ap3216cdev.class = class_create(THIS_MODULE, AP3216C_NAME);
	if (IS_ERR(ap3216cdev.class))
	{
		ret = PTR_ERR(ap3216cdev.class);
		dev_err(&client->dev, "class create failed: ret=%d\n", ret);
		goto err_cdev_del;
	}

	/* 4、创建设备 */
	ap3216cdev.device = device_create(ap3216cdev.class, NULL, ap3216cdev.devid, NULL, AP3216C_NAME);
	if (IS_ERR(ap3216cdev.device))
	{
		ret = PTR_ERR(ap3216cdev.device);
		dev_err(&client->dev, "device create failed: ret=%d\n", ret);
		goto err_class_destroy;
	}

	ap3216cdev.private_data = client;
	atomic_set(&ap3216cdev.open_cnt, 0);
	mutex_init(&ap3216cdev.bus_lock);
	spin_lock_init(&ap3216cdev.data_lock);
	INIT_DELAYED_WORK(&ap3216cdev.poll_work, ap3216c_poll_work_func);
	ap3216cdev.irq = client->irq;
	ap3216cdev.irq_requested = false;
	ap3216cdev.event_mode = AP3216C_RUNTIME_EVENT_MODE_UNKNOWN;
	ap3216cdev.poll_interval_ms = AP3216C_POLL_INTERVAL_MS;
	ap3216cdev.ps_trigger_th = AP3216C_DEFAULT_PS_TRIGGER_TH;
	ap3216cdev.als_delta_th = AP3216C_DEFAULT_ALS_DELTA_TH;
	ap3216cdev.last_als_sample = 0;
	ap3216cdev.als_baseline_valid = false;
	memset(&ap3216cdev.event_stats, 0, sizeof(ap3216cdev.event_stats));

	if (ap3216cdev.irq > 0)
	{
		ret = ap3216c_request_hw_irq(&ap3216cdev);
		if (!ret)
		{
			ap3216cdev.event_mode = AP3216C_RUNTIME_EVENT_MODE_HW_IRQ;
			dev_info(&client->dev, "event mode=HW_IRQ irq=%d\n", ap3216cdev.irq);
		}
		else
		{
			dev_warn(&client->dev,
					 "request irq failed: irq=%d ret=%d, fallback to poll simulation\n",
					 ap3216cdev.irq,
					 ret);
		}
	}

	if (!ap3216cdev.irq_requested)
	{
		ap3216cdev.event_mode = AP3216C_RUNTIME_EVENT_MODE_POLL_SIM;
		schedule_delayed_work(&ap3216cdev.poll_work,
							  msecs_to_jiffies(ap3216cdev.poll_interval_ms));
		dev_info(&client->dev, "event mode=POLL_SIM interval=%dms\n",
				 ap3216cdev.poll_interval_ms);
	}
	dev_info(&client->dev, "probe success: i2c addr=0x%02x\n", client->addr);

	return 0;

err_class_destroy:
	class_destroy(ap3216cdev.class);
err_cdev_del:
	cdev_del(&ap3216cdev.cdev);
err_unregister_chrdev:
	unregister_chrdev_region(ap3216cdev.devid, AP3216C_CNT);
	return ret;
}

/*
 * @description     : i2c驱动的remove函数，移除i2c驱动的时候此函数会执行
 * @param - client 	: i2c设备
 * @return          : 0，成功;其他负值,失败
 */
static int ap3216c_remove(struct i2c_client *client)
{
	unsigned long flags;

	dev_info(&client->dev, "remove device\n");

	spin_lock_irqsave(&ap3216cdev.data_lock, flags);
	ap3216cdev.event_mode = AP3216C_RUNTIME_EVENT_MODE_UNKNOWN;
	spin_unlock_irqrestore(&ap3216cdev.data_lock, flags);
	cancel_delayed_work_sync(&ap3216cdev.poll_work);
	ap3216c_release_hw_irq(&ap3216cdev);

	/* 删除设备 */
	cdev_del(&ap3216cdev.cdev);
	unregister_chrdev_region(ap3216cdev.devid, AP3216C_CNT);

	/* 注销掉类和设备 */
	device_destroy(ap3216cdev.class, ap3216cdev.devid);
	class_destroy(ap3216cdev.class);
	ap3216cdev.private_data = NULL;
	return 0;
}

/* 传统匹配方式ID列表 */
static const struct i2c_device_id ap3216c_id[] = {
	{"alientek,ap3216c", 0},
	{}};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

/* 设备树匹配列表 */
static const struct of_device_id ap3216c_of_match[] = {
	{.compatible = "alientek,ap3216c"},
	{/* Sentinel */}};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

/* i2c驱动结构体 */
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

/*
 * @description	: 驱动入口函数
 * @param 		: 无
 * @return 		: 无
 */
static int __init ap3216c_init(void)
{
	int ret = 0;

	pr_info("register i2c driver\n");
	ret = i2c_add_driver(&ap3216c_driver);
	if (ret)
		pr_err("i2c_add_driver failed: ret=%d\n", ret);
	else
		pr_info("i2c driver registered\n");

	return ret;
}

/*
 * @description	: 驱动出口函数
 * @param 		: 无
 * @return 		: 无
 */
static void __exit ap3216c_exit(void)
{
	pr_info("unregister i2c driver\n");
	i2c_del_driver(&ap3216c_driver);
}

module_i2c_driver(ap3216c_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FriedEgg");
