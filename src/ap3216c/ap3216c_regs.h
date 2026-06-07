#ifndef AP3216C_REGS_H
#define AP3216C_REGS_H

#define AP3216C_ADDR                         0x1E

#define AP3216C_U10_MAX                      1023
#define AP3216C_U16_MAX                      65535
#define AP3216C_IR_MAX_VALUE                 AP3216C_U10_MAX
#define AP3216C_PS_MAX_VALUE                 AP3216C_U10_MAX
#define AP3216C_ALS_MAX_VALUE                AP3216C_U16_MAX

/* System registers */
#define AP3216C_SYSTEM_CONFIG                0x00
#define AP3216C_SYSTEM_MODE_MASK             0x07
#define AP3216C_MODE_POWER_DOWN              0x00
#define AP3216C_MODE_ALS_ONLY                0x01
#define AP3216C_MODE_PS_IR_ONLY              0x02
#define AP3216C_MODE_ALS_PS_IR               0x03
#define AP3216C_MODE_SW_RESET                0x04

#define AP3216C_INT_STATUS                   0x01
#define AP3216C_INTSTATUS_PS_BIT             0x02
#define AP3216C_INTSTATUS_ALS_BIT            0x01
#define AP3216C_INT_STATUS_MASK \
	(AP3216C_INTSTATUS_PS_BIT | AP3216C_INTSTATUS_ALS_BIT)

#define AP3216C_INT_CLEAR_MANNER             0x02
#define AP3216C_INT_CLEAR_BY_READ            0x00
#define AP3216C_INT_CLEAR_BY_WRITE           0x01

/* Data registers */
#define AP3216C_IR_DATA_LOW                  0x0A
#define AP3216C_IR_OVERFLOW_BIT              0x80
#define AP3216C_IR_DATA_LOW_MASK             0x03
#define AP3216C_IR_DATA_HIGH                 0x0B

#define AP3216C_ALS_DATA_LOW                 0x0C
#define AP3216C_ALS_DATA_HIGH                0x0D

#define AP3216C_PS_DATA_LOW                  0x0E
#define AP3216C_PS_OBJECT_BIT                0x80
#define AP3216C_PS_IR_OVERFLOW_BIT           0x40
#define AP3216C_PS_DATA_LOW_MASK             0x0F
#define AP3216C_PS_DATA_HIGH                 0x0F
#define AP3216C_PS_DATA_HIGH_MASK            0x3F

/* ALS registers */
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
#define AP3216C_ALS_PERSIST_MAX              0x0F

#define AP3216C_ALS_CALIBRATION              0x19
#define AP3216C_ALS_CALIBRATION_DEFAULT      0x40
#define AP3216C_ALS_CALIBRATION_MASK         0xFF

#define AP3216C_ALS_LOW_TH_LOW               0x1A
#define AP3216C_ALS_LOW_TH_HIGH              0x1B
#define AP3216C_ALS_LOW_TH_DEFAULT           0x0000
#define AP3216C_ALS_TH_LOW_MASK              0xFF
#define AP3216C_ALS_TH_HIGH_MASK             0xFF

#define AP3216C_ALS_HIGH_TH_LOW              0x1C
#define AP3216C_ALS_HIGH_TH_HIGH             0x1D
#define AP3216C_ALS_HIGH_TH_DEFAULT          0xFFFF

/* PS registers */
#define AP3216C_PS_CONFIG                    0x20
#define AP3216C_PS_CONFIG_DEFAULT            0x05
#define AP3216C_PS_INTEGRATION_MASK          0xF0
#define AP3216C_PS_INTEGRATION_1T            0x00
#define AP3216C_PS_GAIN_MASK                 0x0C
#define AP3216C_PS_GAIN_X2                   0x04
#define AP3216C_PS_PERSIST_MASK              0x03
#define AP3216C_PS_PERSIST_1_TIME            0x00
#define AP3216C_PS_PERSIST_2_TIMES           0x01

#define AP3216C_PS_INT_MODE                  0x22
#define AP3216C_PS_INT_ALGO_MASK             0x01
#define AP3216C_PS_INT_ALGO_HYSTERESIS       0x01

#define AP3216C_PS_LOW_TH_LOW                0x2A
#define AP3216C_PS_LOW_TH_HIGH               0x2B
#define AP3216C_PS_TH_LOW_MASK               0x03
#define AP3216C_PS_TH_HIGH_MASK              0xFF

#define AP3216C_PS_HIGH_TH_LOW               0x2C
#define AP3216C_PS_HIGH_TH_HIGH              0x2D

#endif
