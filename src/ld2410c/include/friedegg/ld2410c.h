#ifndef FRIEDEGG_LD2410C_H
#define FRIEDEGG_LD2410C_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <linux/types.h>
#include <sys/ioctl.h>
#endif

#define LD2410C_LDISC 29
#define LD2410C_MAX_GATES 9
#define LD2410C_DEFAULT_BAUD 256000U

#define LD2410C_TARGET_NONE 0x00
#define LD2410C_TARGET_MOVING 0x01
#define LD2410C_TARGET_STATIC 0x02
#define LD2410C_TARGET_MOVING_STATIC 0x03
#define LD2410C_TARGET_NOISE_RUNNING 0x04
#define LD2410C_TARGET_NOISE_SUCCESS 0x05
#define LD2410C_TARGET_NOISE_FAILED 0x06

#define LD2410C_STATE_F_REPORT_VALID (1U << 0)
#define LD2410C_STATE_F_ENGINEERING (1U << 1)
#define LD2410C_STATE_F_OUT_VALID (1U << 2)
#define LD2410C_STATE_F_OUT_ACTIVE (1U << 3)
#define LD2410C_STATE_F_ACK_VALID (1U << 4)

struct ld2410c_state {
	__u64 sequence;
	__u64 timestamp_ns;
	__u32 flags;
	__u32 frame_count;
	__u32 error_count;
	__u8 target_state;
	__u8 out_level;
	__u8 max_motion_gate;
	__u8 max_static_gate;
	__u16 motion_distance_cm;
	__u16 static_distance_cm;
	__u16 detect_distance_cm;
	__u8 motion_energy;
	__u8 static_energy;
	__u8 motion_gate_energy[LD2410C_MAX_GATES];
	__u8 static_gate_energy[LD2410C_MAX_GATES];
	__u8 light;
	__u8 reserved[5];
};

struct ld2410c_config {
	__u8 max_gate;
	__u8 motion_gate;
	__u8 static_gate;
	__u8 reserved0;
	__u8 motion_sensitivity[LD2410C_MAX_GATES];
	__u8 static_sensitivity[LD2410C_MAX_GATES];
	__u16 idle_time_s;
	__u32 flags;
};

struct ld2410c_gate_config {
	__u16 motion_gate;
	__u16 static_gate;
	__u16 idle_time_s;
	__u16 reserved;
};

struct ld2410c_gate_sensitivity {
	__u16 gate;
	__u8 motion_sensitivity;
	__u8 static_sensitivity;
	__u32 reserved;
};

struct ld2410c_mode {
	__u8 enable;
	__u8 reserved[3];
};

struct ld2410c_version {
	__u16 firmware_type;
	__u16 major;
	__u8 minor[4];
	char text[32];
};

struct ld2410c_baud {
	__u32 baud;
};

struct ld2410c_resolution {
	__u8 index;
	__u8 reserved[3];
};

struct ld2410c_aux_control {
	__u8 mode;
	__u8 threshold;
	__u8 out_default_high;
	__u8 reserved;
};

struct ld2410c_noise {
	__u16 duration_s;
	__u16 status;
};

#define LD2410C_IOC_MAGIC 'L'
#define LD2410C_IOC_GET_STATE _IOR(LD2410C_IOC_MAGIC, 0x01, struct ld2410c_state)
#define LD2410C_IOC_READ_CONFIG _IOR(LD2410C_IOC_MAGIC, 0x02, struct ld2410c_config)
#define LD2410C_IOC_SET_MAX_GATE _IOW(LD2410C_IOC_MAGIC, 0x03, struct ld2410c_gate_config)
#define LD2410C_IOC_SET_GATE_SENSITIVITY _IOW(LD2410C_IOC_MAGIC, 0x04, struct ld2410c_gate_sensitivity)
#define LD2410C_IOC_SET_ENGINEERING_MODE _IOW(LD2410C_IOC_MAGIC, 0x05, struct ld2410c_mode)
#define LD2410C_IOC_GET_VERSION _IOR(LD2410C_IOC_MAGIC, 0x06, struct ld2410c_version)
#define LD2410C_IOC_SET_BAUD _IOW(LD2410C_IOC_MAGIC, 0x07, struct ld2410c_baud)
#define LD2410C_IOC_SET_RESOLUTION _IOW(LD2410C_IOC_MAGIC, 0x08, struct ld2410c_resolution)
#define LD2410C_IOC_SET_AUX_CONTROL _IOW(LD2410C_IOC_MAGIC, 0x09, struct ld2410c_aux_control)
#define LD2410C_IOC_START_NOISE_CALIBRATION _IOWR(LD2410C_IOC_MAGIC, 0x0a, struct ld2410c_noise)
#define LD2410C_IOC_GET_NOISE_STATUS _IOR(LD2410C_IOC_MAGIC, 0x0b, struct ld2410c_noise)
#define LD2410C_IOC_FACTORY_RESET _IO(LD2410C_IOC_MAGIC, 0x0c)
#define LD2410C_IOC_REBOOT _IO(LD2410C_IOC_MAGIC, 0x0d)

#endif
