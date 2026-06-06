#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include "ap3216c.h"

#define AP3216C_CNT 1
#define AP3216C_NAME "ap3216c"

struct ap3216c_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    struct i2c_client *client;
};

static struct ap3216c_dev ap3216c_device;

static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *buf, int len)
{
    int ret;
    struct i2c_msg msg[2];
    struct i2c_client *client = dev->client;

    if (len <= 0)
        return -EINVAL;

    /* AP3216C 读取使用“写寄存器地址 + repeated start + 连续读”的组合事务。 */
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
    {
        dev_dbg(&client->dev, "i2c read ok: reg=0x%02x len=%d\n", reg, len);
        return 0;
    }

    dev_err(&client->dev, "i2c read failed: ret=%d reg=0x%02x len=%d\n", ret, reg, len);
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

    b[0] = reg;              // 先指定要写的寄存器，ap3216c约定
    memcpy(&b[1], buf, len); // 将要写的数据填入b

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

static int ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 val)
{
    return ap3216c_write_regs(dev, reg, &val, 1);
}

static int ap3216c_read_data_regs(struct ap3216c_dev *dev, u8 raw[6])
{
    int i;
    int ret;

    for (i = 0; i < 6; i++) {
        ret = ap3216c_read_regs(dev, AP3216C_IRDATALOW + i, &raw[i], 1);
        if (ret)
            return ret;
    }

    return 0;
}

static int ap3216c_hw_init(struct ap3216c_dev *dev)
{
    int ret;
    struct i2c_client *client = dev->client;

    ret = ap3216c_write_reg(dev, AP3216C_SYSTEMCONG, AP3216C_SW_RESET); // 软复位
    if (ret)
    {
        dev_err(&client->dev, "reset sensor failed: ret=%d\n", ret);
        return ret;
    }

    msleep(50);
    ret = ap3216c_write_reg(dev, AP3216C_SYSTEMCONG, AP3216C_MODE_ALS_PS_IR);
    if (ret)
    {
        dev_err(&client->dev, "mode init failed: ret=%d\n", ret);
        return ret;
    }

    u8 sys = 0;
    u8 alscfg = 0;
    u8 pscfg = 0;
    u8 intst = 0;

    ap3216c_read_regs(dev, AP3216C_SYSTEMCONG, &sys, 1);
    ap3216c_read_regs(dev, AP3216C_INTSTATUS, &intst, 1);
    ap3216c_read_regs(dev, AP3216C_ALSCONFIG, &alscfg, 1);
    ap3216c_read_regs(dev, AP3216C_PSCONFIG, &pscfg, 1);

    dev_info(&client->dev,
             "cfg: sys=0x%02x int=0x%02x alscfg=0x%02x pscfg=0x%02x\n",
             sys, intst, alscfg, pscfg);
    return ret;
}

static int ap3216c_open(struct inode *inode, struct file *filp)
{
    int ret;
    struct i2c_client *client = ap3216c_device.client;
    if (!client)
    {
        pr_err("open failed: i2c client not ready\n");
        return -ENODEV;
    }
    filp->private_data = &ap3216c_device;
    ret = ap3216c_hw_init(&ap3216c_device);
    dev_info(&client->dev, "2119device opened\n");
    return ret;
}

static ssize_t ap3216c_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    int ret;
    u8 raw[6];
    unsigned short data[3];

    struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;
    struct i2c_client *client = (struct i2c_client *)dev->client;

    if (cnt < sizeof(data))
    {
        dev_warn(&client->dev, "read buffer too small: cnt=%zu need=%zu\n", cnt, sizeof(data));
        return -EINVAL;
    }

    ret = ap3216c_read_data_regs(dev, raw);
    // ret = ap3216c_read_regs(dev, AP3216C_IRDATALOW, raw, sizeof(raw));
    if (ret)
        return ret;

    dev_info(&client->dev,
             "raw=%02x %02x %02x %02x %02x %02x ir_of=%d ps_ir_of=%d obj=%d\n",
             raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
             !!(raw[0] & 0x80), !!(raw[4] & 0x40), !!(raw[4] & 0x80));

    if (raw[0] & AP3216C_IR_OF_BIT)
        data[0] = 0;
    else
        data[0] = ((unsigned short)raw[1] << 2) | (raw[0] & AP3216C_IR_DATA_L_MASK); /* IR */
    data[1] = ((unsigned short)raw[3] << 8) | raw[2];
    if (raw[4] & AP3216C_PS_OF_BIT)
        data[2] = 0;
    else
        data[2] = ((unsigned short)(raw[5] & AP3216C_PS_DATA_H_MASK) << 4) | (raw[4] & AP3216C_PS_DATA_L_MASK); /* PS */

    if (copy_to_user(buf, data, sizeof(data)))
    {
        dev_err(&client->dev, "copy_to_user failed\n");
        return -EFAULT;
    }

    return sizeof(data);
}

static long ap3216c_unlocked_iotcl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return 0;
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
    struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;
    struct i2c_client *client;

    if (!dev || !dev->client)
    {
        pr_warn("release without valid context\n");
        return 0;
    }
    client = dev->client;

    return 0;
}

static const struct file_operations ap3216c_ops = {
    .owner = THIS_MODULE,
    .open = ap3216c_open,
    .read = ap3216c_read,
    .unlocked_ioctl = ap3216c_unlocked_iotcl,
    .release = ap3216c_release,
};

int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    // probe如何被调用的？其中client何时被填入？
    // probe过程中ap3216c_device是如何传入的？
    /* 1、构建设备号 */
    if (ap3216c_device.major)
    {
        ap3216c_device.devid = MKDEV(ap3216c_device.major, 0);
        ret = register_chrdev_region(ap3216c_device.devid, AP3216C_CNT, AP3216C_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&ap3216c_device.devid, 0, AP3216C_CNT, AP3216C_NAME);
        ap3216c_device.major = MAJOR(ap3216c_device.devid);
    }
    if (ret < 0)
    {
        dev_err(&client->dev, "alloc chrdev region failed: ret=%d\n", ret);
        return ret;
    }

    /* 2、注册设备 */
    cdev_init(&ap3216c_device.cdev, &ap3216c_ops);
    ret = cdev_add(&ap3216c_device.cdev, ap3216c_device.devid, AP3216C_CNT);
    if (ret)
    {
        dev_err(&client->dev, "cdev add failed: ret=%d\n", ret);
        goto err_unregister_chrdev;
    }

    /* 3、创建类 */
    ap3216c_device.class = class_create(THIS_MODULE, AP3216C_NAME);
    if (IS_ERR(ap3216c_device.class))
    {
        ret = PTR_ERR(ap3216c_device.class);
        dev_err(&client->dev, "class create failed: ret=%d\n", ret);
        goto err_cdev_del;
    }

    /* 4、创建设备 */
    ap3216c_device.device = device_create(ap3216c_device.class, NULL, ap3216c_device.devid, NULL, AP3216C_NAME);
    if (IS_ERR(ap3216c_device.device))
    {
        ret = PTR_ERR(ap3216c_device.device);
        dev_err(&client->dev, "device create failed: ret=%d\n", ret);
        goto err_class_destroy;
    }

    ap3216c_device.client = client;

    return 0;

err_class_destroy:
    class_destroy(ap3216c_device.class);
err_cdev_del:
    cdev_del(&ap3216c_device.cdev);
err_unregister_chrdev:
    unregister_chrdev_region(ap3216c_device.devid, AP3216C_CNT);
    return ret;
}

// remove顺序有什么说法，背后原理是什么
int ap3216c_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "remove ap3216c\n");

    cdev_del(&ap3216c_device.cdev);
    unregister_chrdev_region(ap3216c_device.devid, AP3216C_CNT);
    device_destroy(ap3216c_device.class, ap3216c_device.devid);
    class_destroy(ap3216c_device.class);
    ap3216c_device.client = NULL;

    return 0;
}

// 当前版本i2c_core要求有id_table
static const struct i2c_device_id ap3216c_id[] = {
    {"ap3216c", 0},
    {}};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

static const struct of_device_id ap3216c_of_match[] = {
    {.compatible = "alientek,ap3216c"},
    {/* Sentinel*/}};
MODULE_DEVICE_TABLE(of, ap3216c_of_match); // 这行干嘛的？？

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