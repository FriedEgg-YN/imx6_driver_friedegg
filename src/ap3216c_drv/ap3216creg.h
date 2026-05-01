#ifndef AP3216C_H
#define AP3216C_H

#include <linux/ioctl.h>
/***************************************************************
Copyright © ALIENTEK Co., Ltd. 1998-2029. All rights reserved.
文件名		: ap3216creg.h
作者	  	: 左忠凯
版本	   	: V1.0
描述	   	: AP3216C寄存器地址描述头文件
其他	   	: 无
论坛 	   	: www.openedv.com
日志	   	: 初版V1.0 2019/9/2 左忠凯创建
***************************************************************/

#define AP3216C_ADDR    	0X1E	/* AP3216C器件地址  */

/*
 * AP3216C寄存器地址定义
 * 这些宏用于 i2c 读写时指定寄存器偏移。
 */
#define AP3216C_SYSTEMCONG	0x00	/* 配置寄存器       */
#define AP3216C_INTSTATUS	0X01	/* 中断状态寄存器   */
#define AP3216C_INTCLEAR	0X02	/* 中断清除寄存器   */
#define AP3216C_IRDATALOW	0x0A	/* IR数据低字节     */
#define AP3216C_IRDATAHIGH	0x0B	/* IR数据高字节     */
#define AP3216C_ALSDATALOW	0x0C	/* ALS数据低字节    */
#define AP3216C_ALSDATAHIGH	0X0D	/* ALS数据高字节    */
#define AP3216C_PSDATALOW	0X0E	/* PS数据低字节     */
#define AP3216C_PSDATAHIGH	0X0F	/* PS数据高字节     */

/* 采样配置寄存器（用于 ioctl 配置示例） */
#define AP3216C_ALSCONFIG	0x10	/* ALS配置寄存器 */
#define AP3216C_PSCONFIG	0x20	/* PS配置寄存器  */

/*
 * IR/PS数据位定义
 * 驱动在解析传感器原始寄存器值时会使用这些掩码。
 */
#define AP3216C_IR_OF_BIT		0x80	/* IRDATALOW bit7=1 表示 IR 数据无效 */
#define AP3216C_IR_DATA_L_MASK	0x03	/* IR 低字节有效位：bit[1:0] */
#define AP3216C_PS_OF_BIT		0x40	/* PSDATALOW bit6=1 表示 PS 数据无效 */
#define AP3216C_PS_DATA_H_MASK	0x3F	/* PS 高字节有效位：PSDATAHIGH bit[5:0] */
#define AP3216C_PS_DATA_L_MASK	0x0F	/* PS 低字节有效位：PSDATALOW bit[3:0] */

/* 中断状态位定义（按常用 AP3216C 手册位图：ALS/PS）。 */
#define AP3216C_INTSTATUS_ALS_BIT	0x01
#define AP3216C_INTSTATUS_PS_BIT	0x02

/* 工作模式定义（SYSTEMCONG bit[2:0]） */
#define AP3216C_MODE_POWER_DOWN	0x00
#define AP3216C_MODE_ALS_ONLY	0x01
#define AP3216C_MODE_PS_IR_ONLY	0x02
#define AP3216C_MODE_ALS_PS_IR	0x03

/* 采样率位域定义：本项目采用低 4 位作为配置值输入 */
#define AP3216C_ALS_RATE_MASK	0x0F
#define AP3216C_PS_RATE_MASK	0x0F
#define AP3216C_ALS_RATE_MIN	0x00
#define AP3216C_ALS_RATE_MAX	0x0F
#define AP3216C_PS_RATE_MIN	    0x00
#define AP3216C_PS_RATE_MAX	    0x0F

/* ioctl 命令定义 */
#define AP3216C_IOC_MAGIC	'A'
#define AP3216C_CMD_SET_MODE	_IOW(AP3216C_IOC_MAGIC, 0x01, int)
#define AP3216C_CMD_SET_ALS_RATE	_IOW(AP3216C_IOC_MAGIC, 0x02, int)
#define AP3216C_CMD_SET_PS_RATE	_IOW(AP3216C_IOC_MAGIC, 0x03, int)

/* 事件来源定义：用于区分真实中断与轮询注入。 */
#define AP3216C_EVENT_SRC_NONE		0
#define AP3216C_EVENT_SRC_HW_IRQ	1
#define AP3216C_EVENT_SRC_POLL_SIM	2
#define AP3216C_EVENT_SRC_MANUAL	3

/* 事件模式定义：当前框架运行在哪种模式。 */
#define AP3216C_EVENT_MODE_UNKNOWN	0
#define AP3216C_EVENT_MODE_HW_IRQ	1
#define AP3216C_EVENT_MODE_POLL_SIM	2

/* 用户态可读取的事件统计快照。 */
struct ap3216c_event_stats {
	unsigned int total_events;
	unsigned int hw_irq_events;
	unsigned int poll_sim_events;
	unsigned int manual_events;
	unsigned int last_ps;
	unsigned int last_source;
};

/* 兜底方案相关 ioctl：查询模式/统计，以及手动触发一次事件。 */
#define AP3216C_CMD_GET_EVENT_MODE	_IOR(AP3216C_IOC_MAGIC, 0x11, int)
#define AP3216C_CMD_GET_EVENT_STATS	_IOR(AP3216C_IOC_MAGIC, 0x12, struct ap3216c_event_stats)
#define AP3216C_CMD_TRIGGER_EVENT	_IO(AP3216C_IOC_MAGIC, 0x13)
#define AP3216C_CMD_SET_EVENT_MODE	_IOW(AP3216C_IOC_MAGIC, 0x14, int)
#define AP3216C_CMD_SET_PS_TRIGGER_TH	_IOW(AP3216C_IOC_MAGIC, 0x15, int)
#define AP3216C_CMD_SET_ALS_DELTA_TH	_IOW(AP3216C_IOC_MAGIC, 0x16, int)
#define AP3216C_CMD_SET_POLL_INTERVAL_MS	_IOW(AP3216C_IOC_MAGIC, 0x17, int)

#endif

