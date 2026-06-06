#ifndef AP3216C_H
#define AP3216C_H

#define AP3216C_ADDR    	0X1E	/* AP3216C器件地址  */

#define AP3216C_SYSTEMCONG	0x00	/* 配置寄存器       */

#define AP3216C_INTSTATUS	0X01	/* 中断状态寄存器   */
#define AP3216C_INTCLEAR	0X02	/* 中断清除寄存器   */

#define AP3216C_IRDATALOW	0x0A	/* IR数据低字节     */
#define AP3216C_IRDATAHIGH	0x0B	/* IR数据高字节     */
#define AP3216C_ALSDATALOW	0x0C	/* ALS数据低字节    */
#define AP3216C_ALSDATAHIGH	0X0D	/* ALS数据高字节    */
#define AP3216C_PSDATALOW	0X0E	/* PS数据低字节     */
#define AP3216C_PSDATAHIGH	0X0F	/* PS数据高字节     */

#define AP3216C_ALSCONFIG	0x10	/* ALS配置寄存器 */
#define AP3216C_PSCONFIG	0x20	/* PS配置寄存器  */

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