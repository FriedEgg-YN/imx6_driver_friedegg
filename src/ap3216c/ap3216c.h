#ifndef AP3216C_H
#define AP3216C_H

#define AP3216C_ADDR    	0X1E	/* AP3216C器件地址  */

/* 配置寄存器及对应工作模式 */
#define AP3216C_SYSTEMCONG	    0x00	/* 配置寄存器 */
#define AP3216C_MODE_POWER_DOWN	0x00    /* Power Down (Default) */
#define AP3216C_MODE_ALS_ONLY	0x01    /* ALS */
#define AP3216C_MODE_PS_IR_ONLY	0x02    /* PS+IR */
#define AP3216C_MODE_ALS_PS_IR	0x03    /* ALS+PS+IR */
#define AP3216C_SW_RESET        0x04    /* software reset */

#define AP3216C_INTSTATUS	0X01	/* 中断状态寄存器：bit0, ALS INT(RO); bit1, PS INT(RO)*/
#define AP3216C_INTCLEAR	0X02	/* 中断清除寄存器   */

/* IR数据及掩码 */
#define AP3216C_IRDATALOW	    0x0A	/* IR数据低字节地址     */
#define AP3216C_IRDATAHIGH	    0x0B	/* IR数据高字节地址     */
#define AP3216C_IR_OF_BIT		0x80	/* IRDATALOW bit7=1 表示 IR 和 PS 数据无效 */
#define AP3216C_IR_DATA_L_MASK	0x03	/* IR 低字节有效位：bit[1:0] */

/* ALS数据16bit全部有效 */
#define AP3216C_ALSDATALOW	0x0C	/* ALS数据低字节地址    */
#define AP3216C_ALSDATAHIGH	0X0D	/* ALS数据高字节地址    */

/* PS数据及掩码 */
#define AP3216C_PSDATALOW	    0X0E	/* PS数据低字节地址     */
#define AP3216C_PSDATAHIGH	    0X0F	/* PS数据高字节地址     */
#define AP3216C_PS_OF_BIT		0x40	/* PSDATALOW & PSDATAHIGH bit6=1 表示 IR 和 PS 数据无效 */
#define AP3216C_PS_OBJ_BIT      0x80    /* PSDATALOW & PSDATAHIGH bit7=1 object closed, 反之away，判决条件取决与阈值配置 */
#define AP3216C_PS_DATA_H_MASK	0x3F	/* PS 高字节有效位：PSDATAHIGH bit[5:0] */
#define AP3216C_PS_DATA_L_MASK	0x0F	/* PS 低字节有效位：PSDATALOW bit[3:0] */

/* 暂无修改需求，具体参考数据手册 */
#define AP3216C_ALSCONFIG	0x10	/* ALS配置寄存器 */
#define AP3216C_PSCONFIG	0x20	/* PS配置寄存器  */

/* 阈值寄存器 */
#define AP3216C_ALS_LTH_L		0x1A	/* ALS低阈值 低字节 */
#define AP3216C_ALS_LTH_H		0x1B	/* ALS低阈值 高字节 */
#define AP3216C_ALS_HTH_L		0x1C	/* ALS高阈值 低字节 */
#define AP3216C_ALS_HTH_H		0x1D	/* ALS高阈值 高字节 */
#define AP3216C_PS_LTH_L		0x2A	/* PS低阈值 低字节 (Bit 1:0) */
#define AP3216C_PS_LTH_H		0x2B	/* PS低阈值 高字节 (Bit 7:0) */
#define AP3216C_PS_HTH_L		0x2C	/* PS高阈值 低字节 (Bit 1:0) */
#define AP3216C_PS_HTH_H		0x2D	/* PS高阈值 高字节 (Bit 7:0) */

#define AP3216C_PS_INT_FORM		0x22	/* PS INT Form */

#endif