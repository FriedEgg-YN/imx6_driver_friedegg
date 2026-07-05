#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <asm-generic/ioctl.h>

/**
 * 仿真驱动，通过printk模拟跑马灯，open，start后循环打印，stop后持续打印其中一个灯
 * 仿真存在两个相同节点，复用此驱动
 */

#define CHASINGLED_IOC_MAGIC        'L'
#define CHASINGLED_IOC_START        _IO(CHASINGLED_IOC_MAGIC, 0)
#define CHASINGLED_IOC_STOP         _IO(CHASINGLED_IOC_MAGIC, 1)
#define CHASINGLED_IOC_CLOSE        _IO(CHASINGLED_IOC_MAGIC, 2)

#define CHASINGLED_DEFAULT_LEDS     4
#define CHASINGLED_DEFAULT_PERIOD   500
#define MISCCHASINGLED_NAME "miscchasingLED"

enum chasingled_mode {
    CHASINGLED_IDLE = 0,
    CHASINGLED_RUN,
    CHASINGLED_HOLD,
};

struct chasingled_dev {
    // 必须指针：指向内核设备模型现成对象，不能内嵌
    struct device *dev;
    struct device_node *nd;

    // 必须内嵌：miscdevice、定时器、自旋锁是设备配套资源，体积小、全程使用
    struct miscdevice miscdev;
    struct timer_list timer;
    spinlock_t lock;

    // 指针：只读字符串，内核静态常量，存地址省空间
    const char *name;

    // 内嵌：基础数值、枚举，简单数据，没必要指针
    enum chasingled_mode mode;
    unsigned int led_idx;
    unsigned int hold_led;
    unsigned int led_count;
    unsigned int period_ms;
};

/* 注意运行在软中断上下文中 */
static void chasingled_timer_func(unsigned long data)
{
    struct chasingled_dev *chasingled = (struct chasingled_dev *)data;
    unsigned long flags;
    enum chasingled_mode mode;
    unsigned int led;

    spin_lock_irqsave(&chasingled->lock, flags);

    mode = chasingled->mode;
    if (mode == CHASINGLED_RUN) {
        led = chasingled->led_idx;
        chasingled->hold_led = led;
        chasingled->led_idx = (chasingled->led_idx + 1) % chasingled->led_count;
    } else if (mode == CHASINGLED_HOLD) {
        led = chasingled->hold_led;
    } else {
        spin_unlock_irqrestore(&chasingled->lock, flags);
        return;
    }

    spin_unlock_irqrestore(&chasingled->lock, flags);

    printk(KERN_INFO "%s: LED %u ON\n", chasingled->name, led);

    spin_lock_irqsave(&chasingled->lock, flags);
    if (chasingled->mode != CHASINGLED_IDLE)
        mod_timer(&chasingled->timer, jiffies + msecs_to_jiffies(chasingled->period_ms));
    spin_unlock_irqrestore(&chasingled->lock, flags);
}

static int chasingled_open(struct inode *inode, struct file *file)
{
    /* misc core 会在调用open前将private_data设置为miscdevice指针 */
    struct miscdevice *miscdev = file->private_data;
    struct chasingled_dev *chasingled = container_of(miscdev, struct chasingled_dev, miscdev);

    file->private_data = chasingled;
    printk(KERN_INFO "%s: device opened\n", chasingled->name);
    return 0;
}

static int chasingled_release(struct inode *inode, struct file *file)
{
    struct chasingled_dev *chasingled = file->private_data;
    /* 避免release中回退mode，否则test运行完成将关闭fd，触发此回调 */
    // chasingled->mode = CHASINGLED_IDLE;
    // del_timer_sync(&chasingled->timer);

    printk(KERN_INFO "%s: device closed\n", chasingled->name);
    return 0;
}

static long chasingled_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct chasingled_dev *chasingled = file->private_data;
    unsigned long flags;

    switch (cmd) {
        case CHASINGLED_IOC_START:
            spin_lock_irqsave(&chasingled->lock, flags);
            chasingled->mode = CHASINGLED_RUN;
            spin_unlock_irqrestore(&chasingled->lock, flags);

            mod_timer(&chasingled->timer, jiffies);
            printk(KERN_INFO "%s: started\n", chasingled->name);
            break;

        case CHASINGLED_IOC_STOP:
            spin_lock_irqsave(&chasingled->lock, flags);
            chasingled->mode = CHASINGLED_HOLD;
            spin_unlock_irqrestore(&chasingled->lock, flags);

            mod_timer(&chasingled->timer, jiffies);
            printk(KERN_INFO "%s: stopped\n", chasingled->name);
            break;

        case CHASINGLED_IOC_CLOSE:
            spin_lock_irqsave(&chasingled->lock, flags);
            chasingled->mode = CHASINGLED_IDLE;
            spin_unlock_irqrestore(&chasingled->lock, flags);
            del_timer_sync(&chasingled->timer);
            printk(KERN_INFO "%s: closed\n", chasingled->name);
            break;

        default:
            return -ENOTTY;
    }

    return 0;
}

static struct file_operations chasingled_ops = {
    .owner = THIS_MODULE,
    .open = chasingled_open,
    .release = chasingled_release,
    .unlocked_ioctl = chasingled_ioctl,
};

static int chasingled_probe(struct platform_device *pdev)
{
    struct chasingled_dev *chasingled;
    struct device *dev = &pdev->dev;
    u32 value;
    int ret;

    /* 使用alloc而不是直接创建局部变量，局部变量位于栈，声明周期只限于本函数 */
    chasingled = devm_kzalloc(dev, sizeof(*chasingled), GFP_KERNEL);
    if (IS_ERR_OR_NULL(chasingled))
        return -ENOMEM;

    chasingled->dev = dev;
    chasingled->nd = dev->of_node;
    chasingled->led_count = CHASINGLED_DEFAULT_LEDS;
    chasingled->period_ms = CHASINGLED_DEFAULT_PERIOD;
    chasingled->mode = CHASINGLED_IDLE;

    if (chasingled->nd) {
        if (!of_property_read_u32(chasingled->nd, "led-count", &value)
            && value > 0) {
            chasingled->led_count = value;
        }
        if (!of_property_read_u32(chasingled->nd, "period-ms", &value)
            && value > 0) {
            chasingled->period_ms = value;
        }
        chasingled->name = devm_kasprintf(dev, GFP_KERNEL, "%s", chasingled->nd->name);
    } else {
        chasingled->name = devm_kasprintf(dev, GFP_KERNEL, "%s", dev_name(dev));
    }

    if (!chasingled->name)
        return -ENOMEM;
    
    spin_lock_init(&chasingled->lock);
    setup_timer(&chasingled->timer, chasingled_timer_func, (unsigned long)chasingled);

    chasingled->miscdev.minor = MISC_DYNAMIC_MINOR;
    chasingled->miscdev.name = chasingled->name;
    chasingled->miscdev.fops = &chasingled_ops;
    chasingled->miscdev.parent = dev;

    dev_set_drvdata(dev, chasingled);

    ret = misc_register(&chasingled->miscdev);
    if (ret) {
        dev_err(dev, "misc device register failed: %d\n", ret);
        return ret;
    }

    dev_info(dev, "%s registered, led_count=%u, period=%ums\n",
             chasingled->name, chasingled->led_count,
             chasingled->period_ms);
    return 0;
} 

static int chasingled_remove(struct platform_device *pdev)
{
    struct chasingled_dev *chasingled = platform_get_drvdata(pdev);
    unsigned long flags;

    spin_lock_irqsave(&chasingled->lock, flags);
    chasingled->mode = CHASINGLED_IDLE;
    spin_unlock_irqrestore(&chasingled->lock, flags);

    del_timer_sync(&chasingled->timer);
    misc_deregister(&chasingled->miscdev);
    return 0;
}

static const struct of_device_id chasingled_dt_ids[] = {
    {.compatible = "friedegg,chasingled", },
    { /* sentinel */}
};
MODULE_DEVICE_TABLE(of, chasingled_dt_ids);

static struct platform_driver chasingled_driver = {
    .driver = {
        .name = MISCCHASINGLED_NAME,
        .of_match_table = of_match_ptr(chasingled_dt_ids),
    },
    .probe = chasingled_probe,
    .remove = chasingled_remove,
};

module_platform_driver(chasingled_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FriedEgg");
MODULE_DESCRIPTION("printk chasing LED simulation driver");