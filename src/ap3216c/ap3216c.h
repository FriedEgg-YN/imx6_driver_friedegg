#ifndef AP3216C_H
#define AP3216C_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

/*
 * AP3216C 3-in-1 ALS + PS + IRLED 传感器寄存器表。
 *
 * 说明：
 * 1. Reserved 位保持默认值；修改寄存器位域时优先使用 mask 做读改写。
 * 2. 多字节数据按 datasheet 建议先读低字节，再读高字节。
 * 3. 宏名尽量贴近 datasheet 语义；旧工程兼容别名不再保留。
 */

#define AP3216C_ADDR                         0x1E    /* 7-bit I2C 地址 */

#define AP3216C_U10_MAX                      1023
#define AP3216C_U16_MAX                      65535
#define AP3216C_IR_MAX_VALUE                 AP3216C_U10_MAX
#define AP3216C_PS_MAX_VALUE                 AP3216C_U10_MAX
#define AP3216C_ALS_MAX_VALUE                AP3216C_U16_MAX

/* -------------------------------------------------------------------------- */
/* System registers: 0x00 ~ 0x02                                              */
/* -------------------------------------------------------------------------- */

/*
 * 0x00 System Configuration，默认值 0x00
 *
 * bit[7:3] Reserved
 * bit[2:0] System Mode
 *   000: Power down，默认关断；停止转换，寄存器配置保留，ALS/PS/IR 数据清空。
 *   001: ALS active，只开启 ALS；ALS 典型转换时间 100ms。
 *   010: PS+IR active，只开启 PS+IR；ALS 数据不更新。
 *   011: ALS+PS+IR active；PS/IR 和 ALS 交替转换。
 *   100: Software reset；约 10ms 后所有寄存器恢复默认值。
 *   101: ALS once；完成一次 ALS 转换后自动回 Power down，数据保留。
 *   110: PS+IR once；完成一次 PS/IR 转换后自动回 Power down，数据保留。
 *   111: ALS+PS+IR once；完成一次全通道转换后自动回 Power down，数据保留。
 */
#define AP3216C_SYSTEM_CONFIG                0x00
#define AP3216C_SYSTEM_MODE_MASK             0x07
#define AP3216C_MODE_POWER_DOWN              0x00
#define AP3216C_MODE_ALS_ONLY                0x01
#define AP3216C_MODE_PS_IR_ONLY              0x02
#define AP3216C_MODE_ALS_PS_IR               0x03
#define AP3216C_MODE_SW_RESET                0x04
#define AP3216C_MODE_ALS_ONCE                0x05
#define AP3216C_MODE_PS_IR_ONCE              0x06
#define AP3216C_MODE_ALS_PS_IR_ONCE          0x07

/*
 * 0x01 INT Status，默认值 0x00
 *
 * bit[7:2] Reserved
 * bit[1]   PS INT 状态位；1 表示 PS 中断已触发。
 * bit[0]   ALS INT 状态位；1 表示 ALS 中断已触发。
 *
 * 清除方式由 0x02 CLR_MNR 决定：自动清除模式下读对应数据寄存器清除；
 * 手动清除模式下向对应 bit 写 1 清除，例如写 0x02 清 PS INT。
 */
#define AP3216C_INT_STATUS                   0x01
#define AP3216C_INTSTATUS_PS_BIT             0x02
#define AP3216C_INTSTATUS_ALS_BIT            0x01

/*
 * 0x02 INT Clear Manner，默认值 0x00
 *
 * bit[7:1] Reserved
 * bit[0]   CLR_MNR
 *   0: 默认，读取数据寄存器自动清中断；读 0x0D 清 ALS，读 0x0F 清 PS。
 *   1: 软件手动清中断；向 0x01 对应状态位写 1。
 */
#define AP3216C_INT_CLEAR_MANNER             0x02
#define AP3216C_INT_CLEAR_MANNER_MASK        0x01
#define AP3216C_INT_CLEAR_BY_READ            0x00
#define AP3216C_INT_CLEAR_BY_WRITE           0x01

/* -------------------------------------------------------------------------- */
/* Data registers: 0x0A ~ 0x0F                                                */
/* -------------------------------------------------------------------------- */

/*
 * 0x0A IR Data Low，只读
 *
 * bit[7]   IR_OF；1 表示红外强度过高，IR/PS 数据无效。
 * bit[6:2] Reserved
 * bit[1:0] IR ADC 低 2 位
 */
#define AP3216C_IR_DATA_LOW                  0x0A
#define AP3216C_IR_OVERFLOW_BIT              0x80
#define AP3216C_IR_DATA_LOW_MASK             0x03

/*
 * 0x0B IR Data High，只读
 *
 * bit[7:0] IR ADC 高 8 位
 *
 * IR 为 10-bit：
 *   IR = (reg[0x0B] << 2) | (reg[0x0A] & 0x03)
 */
#define AP3216C_IR_DATA_HIGH                 0x0B
#define AP3216C_IR_DATA_HIGH_MASK            0xFF

/*
 * 0x0C / 0x0D ALS Data，只读
 *
 * ALS 为 16-bit：
 *   ALS = (reg[0x0D] << 8) | reg[0x0C]
 */
#define AP3216C_ALS_DATA_LOW                 0x0C
#define AP3216C_ALS_DATA_HIGH                0x0D
#define AP3216C_ALS_DATA_LOW_MASK            0xFF
#define AP3216C_ALS_DATA_HIGH_MASK           0xFF

/*
 * 0x0E PS Data Low，只读
 *
 * bit[7]   OBJ；1 表示物体接近/closed，0 表示物体离开/away。
 * bit[6]   IR_OF；1 表示红外强度过高，PS 数据无效。
 * bit[5:4] Reserved
 * bit[3:0] PS ADC 低 4 位
 */
#define AP3216C_PS_DATA_LOW                  0x0E
#define AP3216C_PS_OBJECT_BIT                0x80
#define AP3216C_PS_IR_OVERFLOW_BIT           0x40
#define AP3216C_PS_DATA_LOW_MASK             0x0F

/*
 * 0x0F PS Data High，只读
 *
 * bit[7]   OBJ；与 0x0E bit[7] 重复。
 * bit[6]   IR_OF；与 0x0E bit[6] 重复。
 * bit[5:0] PS ADC 高 6 位
 *
 * PS 为 10-bit：
 *   PS = ((reg[0x0F] & 0x3F) << 4) | (reg[0x0E] & 0x0F)
 */
#define AP3216C_PS_DATA_HIGH                 0x0F
#define AP3216C_PS_DATA_HIGH_MASK            0x3F

/* -------------------------------------------------------------------------- */
/* ALS registers: 0x10, 0x19 ~ 0x1D                                           */
/* -------------------------------------------------------------------------- */

/*
 * 0x10 ALS Configuration，默认值 0x00
 *
 * bit[7:6] Reserved
 * bit[5:4] ALS dynamic range / gain
 *   00: 0 ~ 20661 lux，分辨率 0.35 lux/count，默认。
 *   01: 0 ~ 5162  lux，分辨率 0.0788 lux/count。
 *   10: 0 ~ 1291  lux，分辨率 0.0197 lux/count。
 *   11: 0 ~ 323   lux，分辨率 0.0049 lux/count。
 *
 * bit[3:0] ALS interrupt persist/filter
 *   0000: 连续 1 次转换满足阈值后触发，默认。
 *   0001: 连续 4 次转换满足阈值后触发。
 *   0010: 连续 8 次转换满足阈值后触发。
 *   ...
 *   1111: 连续 60 次转换满足阈值后触发。
 *
 * 注意：bit[3:0] 不是采样率，只影响 ALS 中断滤波/确认次数。
 */
#define AP3216C_ALS_CONFIG                   0x10
#define AP3216C_ALS_CONFIG_DEFAULT           0x00
#define AP3216C_ALS_RANGE_MASK               0x30
#define AP3216C_ALS_RANGE_SHIFT              4
#define AP3216C_ALS_RANGE_20661_LUX          0x00
#define AP3216C_ALS_RANGE_5162_LUX           0x10
#define AP3216C_ALS_RANGE_1291_LUX           0x20
#define AP3216C_ALS_RANGE_323_LUX            0x30
#define AP3216C_ALS_PERSIST_MASK             0x0F
#define AP3216C_ALS_PERSIST_DEFAULT          0x00
#define AP3216C_ALS_PERSIST_MIN              0x00
#define AP3216C_ALS_PERSIST_MAX              0x0F

/*
 * 0x19 ALS Calibration，默认值 0x40
 *
 * bit[7:0] ALS 玻璃/窗口损耗补偿系数。
 *   factor = register_value / 64
 *   0x40 表示 64/64 = 1.0，即不补偿。
 *   0x50 表示 80/64 = 1.25。
 */
#define AP3216C_ALS_CALIBRATION              0x19
#define AP3216C_ALS_CALIBRATION_DEFAULT      0x40
#define AP3216C_ALS_CALIBRATION_MASK         0xFF

/*
 * 0x1A / 0x1B ALS Low Threshold，默认值 0x0000
 *
 * ALS 低阈值中断条件：
 *   ALS_DATA < ALS_LOW_THRESHOLD
 */
#define AP3216C_ALS_LOW_TH_LOW               0x1A
#define AP3216C_ALS_LOW_TH_HIGH              0x1B
#define AP3216C_ALS_LOW_TH_DEFAULT           0x0000
#define AP3216C_ALS_TH_LOW_MASK              0xFF
#define AP3216C_ALS_TH_HIGH_MASK             0xFF

/*
 * 0x1C / 0x1D ALS High Threshold，默认值 0xFFFF
 *
 * ALS 高阈值中断条件：
 *   ALS_DATA > ALS_HIGH_THRESHOLD
 */
#define AP3216C_ALS_HIGH_TH_LOW              0x1C
#define AP3216C_ALS_HIGH_TH_HIGH             0x1D
#define AP3216C_ALS_HIGH_TH_DEFAULT          0xFFFF

/* -------------------------------------------------------------------------- */
/* PS registers: 0x20 ~ 0x2D                                                  */
/* -------------------------------------------------------------------------- */

/*
 * 0x20 PS Configuration，默认值 0x05
 *
 * bit[7:4] PS/IR integration time
 *   0000: 1T，默认。
 *   0001: 2T。
 *   ...
 *   1111: 16T。
 *
 * bit[3:2] PS gain
 *   00: x1。
 *   01: x2，默认。
 *   10: x4。
 *   11: x8。
 *
 * bit[1:0] PS interrupt persist/filter
 *   00: 连续 1 次转换满足状态变化后触发。
 *   01: 连续 2 次转换满足状态变化后触发，默认。
 *   10: 连续 4 次转换满足状态变化后触发。
 *   11: 连续 8 次转换满足状态变化后触发。
 *
 * 注意：bit[3:0] 不是采样率，而是 PS gain + PS persist 两个位域。
 */
#define AP3216C_PS_CONFIG                    0x20
#define AP3216C_PS_CONFIG_DEFAULT            0x05
#define AP3216C_PS_INTEGRATION_MASK          0xF0
#define AP3216C_PS_INTEGRATION_SHIFT         4
#define AP3216C_PS_INTEGRATION_1T            0x00
#define AP3216C_PS_INTEGRATION_2T            0x10
#define AP3216C_PS_INTEGRATION_16T           0xF0
#define AP3216C_PS_GAIN_MASK                 0x0C
#define AP3216C_PS_GAIN_SHIFT                2
#define AP3216C_PS_GAIN_X1                   0x00
#define AP3216C_PS_GAIN_X2                   0x04
#define AP3216C_PS_GAIN_X4                   0x08
#define AP3216C_PS_GAIN_X8                   0x0C
#define AP3216C_PS_PERSIST_MASK              0x03
#define AP3216C_PS_PERSIST_1_TIME            0x00
#define AP3216C_PS_PERSIST_2_TIMES           0x01
#define AP3216C_PS_PERSIST_4_TIMES           0x02
#define AP3216C_PS_PERSIST_8_TIMES           0x03

/*
 * 0x21 PS LED Control，默认值 0x13
 *
 * bit[7:6] Reserved
 * bit[5:4] 每次 PS 转换中的 IR LED pulse 数
 *   00: 0 pulse。
 *   01: 1 pulse，默认。
 *   10: 2 pulses。
 *   11: 3 pulses。
 *
 * bit[3:2] Reserved
 * bit[1:0] 最大 LED 驱动电流比例，典型 100% = 110mA
 *   00: 16.7%。
 *   01: 33.3%。
 *   10: 66.7%。
 *   11: 100%，默认。
 */
#define AP3216C_PS_LED_CONTROL               0x21
#define AP3216C_PS_LED_CONTROL_DEFAULT       0x13
#define AP3216C_PS_LED_PULSE_MASK            0x30
#define AP3216C_PS_LED_PULSE_SHIFT           4
#define AP3216C_PS_LED_PULSE_0               0x00
#define AP3216C_PS_LED_PULSE_1               0x10
#define AP3216C_PS_LED_PULSE_2               0x20
#define AP3216C_PS_LED_PULSE_3               0x30
#define AP3216C_PS_LED_RATIO_MASK            0x03
#define AP3216C_PS_LED_RATIO_16_7            0x00
#define AP3216C_PS_LED_RATIO_33_3            0x01
#define AP3216C_PS_LED_RATIO_66_7            0x02
#define AP3216C_PS_LED_RATIO_100             0x03

/*
 * 0x22 PS Interrupt Mode，默认值 0x01
 *
 * bit[7:1] Reserved
 * bit[0]   PS_Algo
 *   0: PS INT Mode 1，Zone type；PS 数据越过高/低绝对阈值并满足 persist 后触发。
 *   1: PS INT Mode 2，Hysteresis type，默认；away->near 使用高阈值，near->away 使用低阈值。
 */
#define AP3216C_PS_INT_MODE                  0x22
#define AP3216C_PS_INT_ALGO_MASK             0x01
#define AP3216C_PS_INT_ALGO_ZONE             0x00
#define AP3216C_PS_INT_ALGO_HYSTERESIS       0x01

/*
 * 0x23 PS Mean Time，默认值 0x00
 *
 * bit[7:2] Reserved
 * bit[1:0] Mean_time
 *   00: 12.5ms，默认。
 *   01: 25ms。
 *   10: 37.5ms。
 *   11: 50ms。
 */
#define AP3216C_PS_MEAN_TIME                 0x23
#define AP3216C_PS_MEAN_TIME_MASK            0x03
#define AP3216C_PS_MEAN_TIME_12_5MS          0x00
#define AP3216C_PS_MEAN_TIME_25MS            0x01
#define AP3216C_PS_MEAN_TIME_37_5MS          0x02
#define AP3216C_PS_MEAN_TIME_50MS            0x03

/*
 * 0x24 PS LED Waiting Time，默认值 0x00
 *
 * bit[7:6] Reserved
 * bit[5:0] PS LED waiting time，单位是 mean time
 *   0x00: 不等待，默认。
 *   0x01: 等待 1 个 mean time。
 *   ...
 *   0x3F: 等待 63 个 mean time。
 *
 * 等待时间越长，功耗越低，但 PS 响应越慢。
 */
#define AP3216C_PS_LED_WAITING_TIME          0x24
#define AP3216C_PS_LED_WAIT_MASK             0x3F
#define AP3216C_PS_LED_WAIT_DEFAULT          0x00
#define AP3216C_PS_LED_WAIT_MAX              0x3F

/*
 * 0x28 / 0x29 PS Calibration，默认值 0x000
 *
 * 9-bit 串扰补偿：
 *   PS_CAL = (reg[0x29] << 1) | (reg[0x28] & 0x01)
 */
#define AP3216C_PS_CALIBRATION_LOW           0x28
#define AP3216C_PS_CALIBRATION_HIGH          0x29
#define AP3216C_PS_CALIBRATION_LOW_MASK      0x01
#define AP3216C_PS_CALIBRATION_HIGH_MASK     0xFF
#define AP3216C_PS_CALIBRATION_DEFAULT       0x000

/*
 * 0x2A / 0x2B PS Low Threshold，默认字节：0x2A=0x00，0x2B=0x80
 *
 * 10-bit 阈值：
 *   PS_LOW_THRESHOLD = (reg[0x2B] << 2) | (reg[0x2A] & 0x03)
 *
 * 在迟滞模式下，near->away 由低阈值判定。
 */
#define AP3216C_PS_LOW_TH_LOW                0x2A
#define AP3216C_PS_LOW_TH_HIGH               0x2B
#define AP3216C_PS_TH_LOW_MASK               0x03
#define AP3216C_PS_TH_HIGH_MASK              0xFF
#define AP3216C_PS_LOW_TH_LOW_DEFAULT        0x00
#define AP3216C_PS_LOW_TH_HIGH_DEFAULT       0x80

/*
 * 0x2C / 0x2D PS High Threshold，默认字节：0x2C=0x00，0x2D=0x80
 *
 * 10-bit 阈值：
 *   PS_HIGH_THRESHOLD = (reg[0x2D] << 2) | (reg[0x2C] & 0x03)
 *
 * 在迟滞模式下，away->near 由高阈值判定。
 */
#define AP3216C_PS_HIGH_TH_LOW               0x2C
#define AP3216C_PS_HIGH_TH_HIGH              0x2D
#define AP3216C_PS_HIGH_TH_LOW_DEFAULT       0x00
#define AP3216C_PS_HIGH_TH_HIGH_DEFAULT      0x80


/* -------------------------------------------------------------------------- */
/* Shared cdev ABI                                                            */
/* -------------------------------------------------------------------------- */

#define AP3216C_CH_ALS                       (1U << 0)
#define AP3216C_CH_IR                        (1U << 1)
#define AP3216C_CH_PS                        (1U << 2)

#define AP3216C_EVT_ALS                      (1U << 0)
#define AP3216C_EVT_PS                       (1U << 1)

struct ap3216c_sample {
    unsigned int valid_mask;
    unsigned int overflow_mask;
    unsigned int mode;
    unsigned int event_status;

    unsigned short ir_raw;
    unsigned short als_raw;
    unsigned short ps_raw;

    unsigned int als_mlux;
    unsigned char ps_object;
    unsigned char reserved[3];
};

struct ap3216c_threshold {
    unsigned int low;
    unsigned int high;
};

struct ap3216c_config {
    unsigned int mode;
    unsigned int event_mask;
    unsigned int als_range;
    unsigned int als_persist;
    unsigned int als_calibration;
    unsigned int ps_integration;
    unsigned int ps_gain;
    unsigned int ps_persist;
    struct ap3216c_threshold als_th;
    struct ap3216c_threshold ps_th;
};

struct ap3216c_stats {
    unsigned int irq_count;
    unsigned int event_count;
    unsigned int als_event_count;
    unsigned int ps_event_count;
    unsigned int ignored_irq_count;
    unsigned int read_count;
    unsigned int last_status;
    struct ap3216c_sample last_sample;
};

#define AP3216C_IOC_MAGIC                    'A'

#define AP3216C_CMD_SET_MODE                 _IOW(AP3216C_IOC_MAGIC, 0x01, unsigned int)
#define AP3216C_CMD_GET_CONFIG               _IOR(AP3216C_IOC_MAGIC, 0x02, struct ap3216c_config)
#define AP3216C_CMD_SET_CONFIG               _IOW(AP3216C_IOC_MAGIC, 0x03, struct ap3216c_config)
#define AP3216C_CMD_SET_EVENT_MASK           _IOW(AP3216C_IOC_MAGIC, 0x04, unsigned int)
#define AP3216C_CMD_SET_ALS_RANGE            _IOW(AP3216C_IOC_MAGIC, 0x05, unsigned int)
#define AP3216C_CMD_SET_ALS_TH               _IOW(AP3216C_IOC_MAGIC, 0x06, struct ap3216c_threshold)
#define AP3216C_CMD_SET_PS_TH                _IOW(AP3216C_IOC_MAGIC, 0x07, struct ap3216c_threshold)
#define AP3216C_CMD_GET_STATS                _IOR(AP3216C_IOC_MAGIC, 0x08, struct ap3216c_stats)

#endif
