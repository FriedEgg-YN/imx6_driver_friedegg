#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/uaccess.h>

#include <friedegg/ld2410c.h>

#define LD2410C_NAME "ld2410c"
#define LD2410C_RX_BUF_SIZE 256
#define LD2410C_ACK_MAX 64
#define LD2410C_CMD_TIMEOUT_MS 700

#define LD2410C_REPORT_HEADER_0 0xf4
#define LD2410C_REPORT_HEADER_1 0xf3
#define LD2410C_REPORT_HEADER_2 0xf2
#define LD2410C_REPORT_HEADER_3 0xf1
#define LD2410C_REPORT_TAIL_0 0xf8
#define LD2410C_REPORT_TAIL_1 0xf7
#define LD2410C_REPORT_TAIL_2 0xf6
#define LD2410C_REPORT_TAIL_3 0xf5

#define LD2410C_CMD_HEADER_0 0xfd
#define LD2410C_CMD_HEADER_1 0xfc
#define LD2410C_CMD_HEADER_2 0xfb
#define LD2410C_CMD_HEADER_3 0xfa
#define LD2410C_CMD_TAIL_0 0x04
#define LD2410C_CMD_TAIL_1 0x03
#define LD2410C_CMD_TAIL_2 0x02
#define LD2410C_CMD_TAIL_3 0x01

#define LD2410C_CMD_ENABLE_CONFIG 0x00ff
#define LD2410C_CMD_END_CONFIG 0x00fe
#define LD2410C_CMD_SET_MAX_GATE 0x0060
#define LD2410C_CMD_READ_CONFIG 0x0061
#define LD2410C_CMD_ENABLE_ENGINEERING 0x0062
#define LD2410C_CMD_DISABLE_ENGINEERING 0x0063
#define LD2410C_CMD_SET_GATE_SENSITIVITY 0x0064
#define LD2410C_CMD_GET_VERSION 0x00a0
#define LD2410C_CMD_SET_BAUD 0x00a1
#define LD2410C_CMD_FACTORY_RESET 0x00a2
#define LD2410C_CMD_REBOOT 0x00a3
#define LD2410C_CMD_SET_RESOLUTION 0x00aa
#define LD2410C_CMD_SET_AUX 0x00ad
#define LD2410C_CMD_START_NOISE 0x000b
#define LD2410C_CMD_GET_NOISE 0x001b

struct ld2410c_dev {
	struct device *dev;
	struct miscdevice miscdev;
	struct input_dev *input;
	int out_gpio;
	int out_irq;
	bool out_active_low;
	struct tty_struct *tty;
	struct mutex tty_lock;
	struct mutex tx_lock;
	spinlock_t state_lock;
	wait_queue_head_t read_wq;
	wait_queue_head_t ack_wq;
	struct ld2410c_state state;
	struct ld2410c_config config;
	u16 ack_cmd;
	u16 ack_status;
	u8 ack_payload[LD2410C_ACK_MAX];
	u16 ack_len;
	u32 ack_seq;
};

struct ld2410c_ldisc {
	struct ld2410c_dev *ld;
	struct tty_struct *tty;
	u8 rx_buf[LD2410C_RX_BUF_SIZE];
	size_t rx_count;
};

struct ld2410c_file {
	struct ld2410c_dev *ld;
	u64 last_sequence;
};

static DEFINE_MUTEX(ld2410c_global_lock);
static struct ld2410c_dev *ld2410c_global;

static u16 ld2410c_get_le16(const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

static void ld2410c_put_le16(u8 *p, u16 v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
}

static void ld2410c_put_le32(u8 *p, u32 v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

static void ld2410c_publish_locked(struct ld2410c_dev *ld)
{
	ld->state.sequence++;
	ld->state.timestamp_ns = ktime_get_ns();
}

static void ld2410c_copy_state(struct ld2410c_dev *ld, struct ld2410c_state *state)
{
	unsigned long flags;

	spin_lock_irqsave(&ld->state_lock, flags);
	*state = ld->state;
	spin_unlock_irqrestore(&ld->state_lock, flags);
}

static void ld2410c_copy_config(struct ld2410c_dev *ld, struct ld2410c_config *config)
{
	unsigned long flags;

	spin_lock_irqsave(&ld->state_lock, flags);
	*config = ld->config;
	spin_unlock_irqrestore(&ld->state_lock, flags);
}

static bool ld2410c_has_new_state(struct ld2410c_file *ctx)
{
	struct ld2410c_state state;

	ld2410c_copy_state(ctx->ld, &state);
	return state.sequence != ctx->last_sequence;
}

static void ld2410c_update_out(struct ld2410c_dev *ld)
{
	unsigned long flags;
	int raw;
	int active;

	if (!gpio_is_valid(ld->out_gpio))
		return;

	raw = gpio_get_value(ld->out_gpio);
	active = ld->out_active_low ? !raw : raw;

	spin_lock_irqsave(&ld->state_lock, flags);
	ld->state.out_level = raw ? 1 : 0;
	ld->state.flags |= LD2410C_STATE_F_OUT_VALID;
	if (active)
		ld->state.flags |= LD2410C_STATE_F_OUT_ACTIVE;
	else
		ld->state.flags &= ~LD2410C_STATE_F_OUT_ACTIVE;
	ld2410c_publish_locked(ld);
	spin_unlock_irqrestore(&ld->state_lock, flags);

	if (ld->input) {
		input_report_switch(ld->input, SW_FRONT_PROXIMITY, active);
		input_sync(ld->input);
	}

	wake_up_interruptible(&ld->read_wq);
}

static irqreturn_t ld2410c_out_irq(int irq, void *data)
{
	struct ld2410c_dev *ld = data;

	ld2410c_update_out(ld);
	return IRQ_HANDLED;
}

static bool ld2410c_is_report_header(const u8 *p)
{
	return p[0] == LD2410C_REPORT_HEADER_0 &&
	       p[1] == LD2410C_REPORT_HEADER_1 &&
	       p[2] == LD2410C_REPORT_HEADER_2 &&
	       p[3] == LD2410C_REPORT_HEADER_3;
}

static bool ld2410c_is_cmd_header(const u8 *p)
{
	return p[0] == LD2410C_CMD_HEADER_0 &&
	       p[1] == LD2410C_CMD_HEADER_1 &&
	       p[2] == LD2410C_CMD_HEADER_2 &&
	       p[3] == LD2410C_CMD_HEADER_3;
}

static bool ld2410c_has_report_tail(const u8 *p)
{
	return p[0] == LD2410C_REPORT_TAIL_0 &&
	       p[1] == LD2410C_REPORT_TAIL_1 &&
	       p[2] == LD2410C_REPORT_TAIL_2 &&
	       p[3] == LD2410C_REPORT_TAIL_3;
}

static bool ld2410c_has_cmd_tail(const u8 *p)
{
	return p[0] == LD2410C_CMD_TAIL_0 &&
	       p[1] == LD2410C_CMD_TAIL_1 &&
	       p[2] == LD2410C_CMD_TAIL_2 &&
	       p[3] == LD2410C_CMD_TAIL_3;
}

static void ld2410c_count_parse_error(struct ld2410c_dev *ld)
{
	unsigned long flags;

	spin_lock_irqsave(&ld->state_lock, flags);
	ld->state.error_count++;
	spin_unlock_irqrestore(&ld->state_lock, flags);
}

static void ld2410c_parse_report(struct ld2410c_dev *ld, const u8 *payload, u16 len)
{
	struct ld2410c_state next;
	unsigned long flags;
	const u8 *target;
	int i;

	if (len < 13 || payload[1] != 0xaa ||
	    payload[len - 2] != 0x55 || payload[len - 1] != 0x00) {
		ld2410c_count_parse_error(ld);
		return;
	}

	target = &payload[2];

	spin_lock_irqsave(&ld->state_lock, flags);
	next = ld->state;
	next.flags |= LD2410C_STATE_F_REPORT_VALID;
	next.flags &= ~LD2410C_STATE_F_ENGINEERING;
	next.target_state = target[0];
	next.motion_distance_cm = ld2410c_get_le16(&target[1]);
	next.motion_energy = target[3];
	next.static_distance_cm = ld2410c_get_le16(&target[4]);
	next.static_energy = target[6];
	next.detect_distance_cm = ld2410c_get_le16(&target[7]);
	next.frame_count++;

	if (payload[0] == 0x01 && len >= 35) {
		const u8 *eng = &target[9];

		next.flags |= LD2410C_STATE_F_ENGINEERING;
		next.max_motion_gate = eng[0];
		next.max_static_gate = eng[1];
		for (i = 0; i < LD2410C_MAX_GATES; i++)
			next.motion_gate_energy[i] = eng[2 + i];
		for (i = 0; i < LD2410C_MAX_GATES; i++)
			next.static_gate_energy[i] = eng[11 + i];
		next.light = eng[20];
		next.out_level = eng[21] ? 1 : 0;
		next.flags |= LD2410C_STATE_F_OUT_VALID;
		if (next.out_level)
			next.flags |= LD2410C_STATE_F_OUT_ACTIVE;
		else
			next.flags &= ~LD2410C_STATE_F_OUT_ACTIVE;
	}

	ld->state = next;
	ld2410c_publish_locked(ld);
	spin_unlock_irqrestore(&ld->state_lock, flags);

	wake_up_interruptible(&ld->read_wq);
}

static void ld2410c_parse_read_config_ack(struct ld2410c_dev *ld,
					  const u8 *payload, u16 len)
{
	unsigned long flags;
	struct ld2410c_config cfg;
	u8 max_gate;
	u8 gates;
	int i;

	if (len < 6 || payload[0] != 0xaa)
		return;

	max_gate = payload[1];
	gates = max_gate + 1;
	if (gates > LD2410C_MAX_GATES)
		gates = LD2410C_MAX_GATES;
	if (len < 4 + gates * 2 + 2)
		return;

	memset(&cfg, 0, sizeof(cfg));
	cfg.max_gate = max_gate;
	cfg.motion_gate = payload[2];
	cfg.static_gate = payload[3];
	for (i = 0; i < gates; i++)
		cfg.motion_sensitivity[i] = payload[4 + i];
	for (i = 0; i < gates; i++)
		cfg.static_sensitivity[i] = payload[4 + gates + i];
	cfg.idle_time_s = ld2410c_get_le16(&payload[4 + gates * 2]);
	cfg.flags = 1;

	spin_lock_irqsave(&ld->state_lock, flags);
	ld->config = cfg;
	spin_unlock_irqrestore(&ld->state_lock, flags);
}

static void ld2410c_parse_ack(struct ld2410c_dev *ld, const u8 *payload, u16 len)
{
	unsigned long flags;
	u16 ack_word;
	u16 cmd;
	u16 status;
	u16 copy_len;

	if (len < 4) {
		ld2410c_count_parse_error(ld);
		return;
	}

	ack_word = ld2410c_get_le16(payload);
	cmd = ack_word & ~0x0100;
	status = ld2410c_get_le16(&payload[2]);
	copy_len = len > 4 ? len - 4 : 0;
	if (copy_len > LD2410C_ACK_MAX)
		copy_len = LD2410C_ACK_MAX;

	spin_lock_irqsave(&ld->state_lock, flags);
	ld->ack_cmd = cmd;
	ld->ack_status = status;
	ld->ack_len = copy_len;
	if (copy_len)
		memcpy(ld->ack_payload, &payload[4], copy_len);
	ld->ack_seq++;
	ld->state.flags |= LD2410C_STATE_F_ACK_VALID;
	ld2410c_publish_locked(ld);
	spin_unlock_irqrestore(&ld->state_lock, flags);

	if (cmd == LD2410C_CMD_READ_CONFIG && status == 0)
		ld2410c_parse_read_config_ack(ld, &payload[4], len - 4);

	wake_up_interruptible(&ld->ack_wq);
	wake_up_interruptible(&ld->read_wq);
}

static void ld2410c_consume_frame(struct ld2410c_ldisc *ctx)
{
	struct ld2410c_dev *ld = ctx->ld;
	bool report = false;
	bool cmd = false;
	u16 length;
	size_t total;
	size_t i;

	while (ctx->rx_count >= 4) {
		report = false;
		cmd = false;

		for (i = 0; i < ctx->rx_count - 3; i++) {
			report = ld2410c_is_report_header(&ctx->rx_buf[i]);
			cmd = !report && ld2410c_is_cmd_header(&ctx->rx_buf[i]);
			if (report || cmd) {
				if (i > 0) {
					memmove(ctx->rx_buf, ctx->rx_buf + i, ctx->rx_count - i);
					ctx->rx_count -= i;
				}
				break;
			}
		}
		if (!report && !cmd) {
			memmove(ctx->rx_buf, ctx->rx_buf + ctx->rx_count - 3, 3);
			ctx->rx_count = 3;
			return;
		}
		if (ctx->rx_count < 10)
			return;

		length = ld2410c_get_le16(&ctx->rx_buf[4]);
		total = 4 + 2 + length + 4;
		if (total > LD2410C_RX_BUF_SIZE) {
			memmove(ctx->rx_buf, ctx->rx_buf + 4, ctx->rx_count - 4);
			ctx->rx_count -= 4;
			ld2410c_count_parse_error(ld);
			continue;
		}
		if (ctx->rx_count < total)
			return;
		if ((report && !ld2410c_has_report_tail(&ctx->rx_buf[total - 4])) ||
			(cmd && !ld2410c_has_cmd_tail(&ctx->rx_buf[total - 4]))) {
			memmove(ctx->rx_buf, ctx->rx_buf + 4, ctx->rx_count - 4);
			ctx->rx_count -= 4;
			ld2410c_count_parse_error(ld);
			continue;
		}
		if (report) {
			ld2410c_parse_report(ld, &ctx->rx_buf[6], length);
		} else if (cmd) {
			ld2410c_parse_ack(ld, &ctx->rx_buf[6], length);
		}

		ctx->rx_count -= total;
		if (ctx->rx_count)
			memmove(ctx->rx_buf, ctx->rx_buf + total, ctx->rx_count);
	}
}

static int ld2410c_ldisc_open(struct tty_struct *tty)
{
	struct ld2410c_ldisc *ctx;
	struct ld2410c_dev *ld;
	int ret = 0;

	mutex_lock(&ld2410c_global_lock);
	ld = ld2410c_global;
	if (!ld) {
		mutex_unlock(&ld2410c_global_lock);
		return -ENODEV;
	}

	mutex_lock(&ld->tty_lock);
	if (ld->tty) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	ctx->ld = ld;
	ctx->tty = tty_kref_get(tty);
	tty->disc_data = ctx;
	tty->receive_room = 65536;
	ld->tty = ctx->tty;

out_unlock:
	mutex_unlock(&ld->tty_lock);
	mutex_unlock(&ld2410c_global_lock);
	return ret;
}

static void ld2410c_ldisc_close(struct tty_struct *tty)
{
	struct ld2410c_ldisc *ctx = tty->disc_data;
	struct ld2410c_dev *ld;

	if (!ctx)
		return;

	ld = ctx->ld;
	mutex_lock(&ld->tty_lock);
	if (ld->tty == ctx->tty)
		ld->tty = NULL;
	mutex_unlock(&ld->tty_lock);

	tty->disc_data = NULL;
	tty_kref_put(ctx->tty);
	kfree(ctx);
}

static void ld2410c_ldisc_receive(struct tty_struct *tty,
				  const unsigned char *cp, char *fp, int count)
{
	struct ld2410c_ldisc *ctx = tty->disc_data;
	int i;

	(void)fp;

	if (!ctx || !ctx->ld)
		return;

	for (i = 0; i < count; i++) {
		if (ctx->rx_count >= LD2410C_RX_BUF_SIZE) {
			ctx->rx_count = 0;
			ld2410c_count_parse_error(ctx->ld);
		}
		ctx->rx_buf[ctx->rx_count++] = cp[i];
		ld2410c_consume_frame(ctx);
	}
}

static struct tty_ldisc_ops ld2410c_ldisc_ops = {
	.owner = THIS_MODULE,
	.magic = TTY_LDISC_MAGIC,
	.name = "n_ld2410c",
	.open = ld2410c_ldisc_open,
	.close = ld2410c_ldisc_close,
	.receive_buf = ld2410c_ldisc_receive,
};

static int ld2410c_write_tty(struct ld2410c_dev *ld, const u8 *buf, size_t len)
{
	size_t done = 0;
	int ret = 0;

	mutex_lock(&ld->tty_lock);
	if (!ld->tty || !ld->tty->ops || !ld->tty->ops->write) {
		ret = -ENODEV;
		goto out_unlock;
	}

	while (done < len) {
		int written = ld->tty->ops->write(ld->tty, buf + done, len - done);

		if (written <= 0) {
			ret = written ? written : -EIO;
			goto out_unlock;
		}
		done += written;
	}

out_unlock:
	mutex_unlock(&ld->tty_lock);
	return ret;
}

static bool ld2410c_ack_ready(struct ld2410c_dev *ld, u16 cmd, u32 seq)
{
	unsigned long flags;
	bool ready;

	spin_lock_irqsave(&ld->state_lock, flags);
	ready = ld->ack_seq != seq && ld->ack_cmd == cmd;
	spin_unlock_irqrestore(&ld->state_lock, flags);

	return ready;
}

static int ld2410c_send_cmd_wait_locked(struct ld2410c_dev *ld, u16 cmd,
					const u8 *payload, u16 payload_len,
					u8 *ack_payload, u16 *ack_len)
{
	u8 frame[80];
	u16 frame_len = payload_len + 2;
	size_t total = frame_len + 10;
	unsigned long flags;
	u32 seq;
	int ret;

	if (total > sizeof(frame))
		return -EINVAL;

	spin_lock_irqsave(&ld->state_lock, flags);
	seq = ld->ack_seq;
	spin_unlock_irqrestore(&ld->state_lock, flags);

	frame[0] = LD2410C_CMD_HEADER_0;
	frame[1] = LD2410C_CMD_HEADER_1;
	frame[2] = LD2410C_CMD_HEADER_2;
	frame[3] = LD2410C_CMD_HEADER_3;
	ld2410c_put_le16(&frame[4], frame_len);
	ld2410c_put_le16(&frame[6], cmd);
	if (payload_len)
		memcpy(&frame[8], payload, payload_len);
	frame[8 + payload_len] = LD2410C_CMD_TAIL_0;
	frame[9 + payload_len] = LD2410C_CMD_TAIL_1;
	frame[10 + payload_len] = LD2410C_CMD_TAIL_2;
	frame[11 + payload_len] = LD2410C_CMD_TAIL_3;

	ret = ld2410c_write_tty(ld, frame, total);
	if (ret)
		return ret;

	ret = wait_event_interruptible_timeout(ld->ack_wq,
			ld2410c_ack_ready(ld, cmd, seq),
			msecs_to_jiffies(LD2410C_CMD_TIMEOUT_MS));
	if (ret < 0)
		return ret;
	if (!ret)
		return -ETIMEDOUT;

	spin_lock_irqsave(&ld->state_lock, flags);
	ret = ld->ack_status == 0 ? 0 : -EIO;
	if (!ret && ack_payload && ack_len) {
		u16 n = ld->ack_len;

		if (n > *ack_len)
			n = *ack_len;
		memcpy(ack_payload, ld->ack_payload, n);
		*ack_len = n;
	} else if (ack_len) {
		*ack_len = 0;
	}
	spin_unlock_irqrestore(&ld->state_lock, flags);

	return ret;
}

static int ld2410c_run_config_cmd(struct ld2410c_dev *ld, u16 cmd,
				  const u8 *payload, u16 payload_len,
				  u8 *ack_payload, u16 *ack_len,
				  bool send_end)
{
	u8 enable_payload[2];
	int ret;

	mutex_lock(&ld->tx_lock);

	ld2410c_put_le16(enable_payload, 0x0001);
	ret = ld2410c_send_cmd_wait_locked(ld, LD2410C_CMD_ENABLE_CONFIG,
					   enable_payload, sizeof(enable_payload),
					   NULL, NULL);
	if (ret)
		goto out_unlock;

	ret = ld2410c_send_cmd_wait_locked(ld, cmd, payload, payload_len,
					   ack_payload, ack_len);
	if (ret)
		goto out_unlock;

	if (send_end)
		ret = ld2410c_send_cmd_wait_locked(ld, LD2410C_CMD_END_CONFIG,
						   NULL, 0, NULL, NULL);

out_unlock:
	mutex_unlock(&ld->tx_lock);
	return ret;
}

static int ld2410c_baud_to_index(u32 baud)
{
	switch (baud) {
	case 1:
	case 9600:
		return 1;
	case 2:
	case 19200:
		return 2;
	case 3:
	case 38400:
		return 3;
	case 4:
	case 57600:
		return 4;
	case 5:
	case 115200:
		return 5;
	case 6:
	case 230400:
		return 6;
	case 7:
	case 256000:
		return 7;
	case 8:
	case 460800:
		return 8;
	default:
		return -EINVAL;
	}
}

static int ld2410c_read_config(struct ld2410c_dev *ld, struct ld2410c_config *cfg)
{
	int ret;

	ret = ld2410c_run_config_cmd(ld, LD2410C_CMD_READ_CONFIG,
				     NULL, 0, NULL, NULL, true);
	if (ret)
		return ret;

	ld2410c_copy_config(ld, cfg);
	return 0;
}

static int ld2410c_set_max_gate(struct ld2410c_dev *ld,
				const struct ld2410c_gate_config *cfg)
{
	u8 payload[18];

	if (cfg->motion_gate > 8 || cfg->static_gate > 8)
		return -EINVAL;

	ld2410c_put_le16(&payload[0], 0x0000);
	ld2410c_put_le32(&payload[2], cfg->motion_gate);
	ld2410c_put_le16(&payload[6], 0x0001);
	ld2410c_put_le32(&payload[8], cfg->static_gate);
	ld2410c_put_le16(&payload[12], 0x0002);
	ld2410c_put_le32(&payload[14], cfg->idle_time_s);

	return ld2410c_run_config_cmd(ld, LD2410C_CMD_SET_MAX_GATE,
				      payload, sizeof(payload), NULL, NULL, true);
}

static int ld2410c_set_gate_sensitivity(struct ld2410c_dev *ld,
					const struct ld2410c_gate_sensitivity *sens)
{
	u8 payload[18];

	if (sens->gate != 0xffff && sens->gate > 8)
		return -EINVAL;
	if (sens->motion_sensitivity > 100 || sens->static_sensitivity > 100)
		return -EINVAL;

	ld2410c_put_le16(&payload[0], 0x0000);
	ld2410c_put_le32(&payload[2], sens->gate);
	ld2410c_put_le16(&payload[6], 0x0001);
	ld2410c_put_le32(&payload[8], sens->motion_sensitivity);
	ld2410c_put_le16(&payload[12], 0x0002);
	ld2410c_put_le32(&payload[14], sens->static_sensitivity);

	return ld2410c_run_config_cmd(ld, LD2410C_CMD_SET_GATE_SENSITIVITY,
				      payload, sizeof(payload), NULL, NULL, true);
}

static int ld2410c_set_engineering(struct ld2410c_dev *ld,
				   const struct ld2410c_mode *mode)
{
	u16 cmd = mode->enable ? LD2410C_CMD_ENABLE_ENGINEERING :
				 LD2410C_CMD_DISABLE_ENGINEERING;

	return ld2410c_run_config_cmd(ld, cmd, NULL, 0, NULL, NULL, true);
}

static int ld2410c_get_version(struct ld2410c_dev *ld,
			       struct ld2410c_version *version)
{
	u8 ack[16];
	u16 ack_len = sizeof(ack);
	int ret;

	memset(version, 0, sizeof(*version));
	ret = ld2410c_run_config_cmd(ld, LD2410C_CMD_GET_VERSION, NULL, 0,
				     ack, &ack_len, true);
	if (ret)
		return ret;
	if (ack_len >= 8) {
		version->firmware_type = ld2410c_get_le16(&ack[0]);
		version->major = ld2410c_get_le16(&ack[2]);
		memcpy(version->minor, &ack[4], 4);
		snprintf(version->text, sizeof(version->text),
			 "type=%u major=%u minor=%02x%02x%02x%02x",
			 version->firmware_type, version->major,
			 version->minor[0], version->minor[1],
			 version->minor[2], version->minor[3]);
	}

	return 0;
}

static int ld2410c_set_baud(struct ld2410c_dev *ld, const struct ld2410c_baud *baud)
{
	u8 payload[2];
	int index = ld2410c_baud_to_index(baud->baud);

	if (index < 0)
		return index;

	ld2410c_put_le16(payload, index);
	return ld2410c_run_config_cmd(ld, LD2410C_CMD_SET_BAUD,
				      payload, sizeof(payload), NULL, NULL, true);
}

static int ld2410c_set_resolution(struct ld2410c_dev *ld,
				  const struct ld2410c_resolution *resolution)
{
	u8 payload[2];

	if (resolution->index > 1)
		return -EINVAL;

	ld2410c_put_le16(payload, resolution->index);
	return ld2410c_run_config_cmd(ld, LD2410C_CMD_SET_RESOLUTION,
				      payload, sizeof(payload), NULL, NULL, true);
}

static int ld2410c_set_aux(struct ld2410c_dev *ld,
			   const struct ld2410c_aux_control *aux)
{
	u8 payload[4];

	if (aux->mode > 2)
		return -EINVAL;

	payload[0] = aux->mode;
	payload[1] = aux->threshold;
	payload[2] = aux->out_default_high ? 1 : 0;
	payload[3] = 0;

	return ld2410c_run_config_cmd(ld, LD2410C_CMD_SET_AUX,
				      payload, sizeof(payload), NULL, NULL, true);
}

static int ld2410c_start_noise(struct ld2410c_dev *ld, struct ld2410c_noise *noise)
{
	u8 payload[2];

	if (!noise->duration_s)
		noise->duration_s = 60;

	ld2410c_put_le16(payload, noise->duration_s);
	noise->status = 1;
	return ld2410c_run_config_cmd(ld, LD2410C_CMD_START_NOISE,
				      payload, sizeof(payload), NULL, NULL, true);
}

static int ld2410c_get_noise(struct ld2410c_dev *ld, struct ld2410c_noise *noise)
{
	u8 ack[4];
	u16 ack_len = sizeof(ack);
	int ret;

	ret = ld2410c_run_config_cmd(ld, LD2410C_CMD_GET_NOISE, NULL, 0,
				     ack, &ack_len, true);
	if (ret)
		return ret;
	if (ack_len >= 2)
		noise->status = ld2410c_get_le16(ack);

	return 0;
}

static int ld2410c_simple_config_cmd(struct ld2410c_dev *ld, u16 cmd, bool send_end)
{
	return ld2410c_run_config_cmd(ld, cmd, NULL, 0, NULL, NULL, send_end);
}

static int ld2410c_misc_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct ld2410c_dev *ld = container_of(misc, struct ld2410c_dev, miscdev);
	struct ld2410c_file *ctx;
	struct ld2410c_state state;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ld2410c_copy_state(ld, &state);
	ctx->ld = ld;
	ctx->last_sequence = state.sequence;
	file->private_data = ctx;

	return 0;
}

static int ld2410c_misc_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t ld2410c_misc_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct ld2410c_file *ctx = file->private_data;
	struct ld2410c_state state;
	int ret;

	(void)ppos;

	if (count < sizeof(state))
		return -EINVAL;

	if (!ld2410c_has_new_state(ctx)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(ctx->ld->read_wq,
					       ld2410c_has_new_state(ctx));
		if (ret)
			return ret;
	}

	ld2410c_copy_state(ctx->ld, &state);
	if (copy_to_user(buf, &state, sizeof(state)))
		return -EFAULT;

	ctx->last_sequence = state.sequence;
	return sizeof(state);
}

static unsigned int ld2410c_misc_poll(struct file *file, poll_table *wait)
{
	struct ld2410c_file *ctx = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &ctx->ld->read_wq, wait);
	if (ld2410c_has_new_state(ctx))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static long ld2410c_misc_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct ld2410c_file *ctx = file->private_data;
	struct ld2410c_dev *ld = ctx->ld;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case LD2410C_IOC_GET_STATE: {
		struct ld2410c_state state;

		ld2410c_copy_state(ld, &state);
		if (copy_to_user(argp, &state, sizeof(state)))
			return -EFAULT;
		return 0;
	}
	case LD2410C_IOC_READ_CONFIG: {
		struct ld2410c_config cfg;
		int ret = ld2410c_read_config(ld, &cfg);

		if (ret)
			return ret;
		if (copy_to_user(argp, &cfg, sizeof(cfg)))
			return -EFAULT;
		return 0;
	}
	case LD2410C_IOC_SET_MAX_GATE: {
		struct ld2410c_gate_config cfg;

		if (copy_from_user(&cfg, argp, sizeof(cfg)))
			return -EFAULT;
		return ld2410c_set_max_gate(ld, &cfg);
	}
	case LD2410C_IOC_SET_GATE_SENSITIVITY: {
		struct ld2410c_gate_sensitivity sens;

		if (copy_from_user(&sens, argp, sizeof(sens)))
			return -EFAULT;
		return ld2410c_set_gate_sensitivity(ld, &sens);
	}
	case LD2410C_IOC_SET_ENGINEERING_MODE: {
		struct ld2410c_mode mode;

		if (copy_from_user(&mode, argp, sizeof(mode)))
			return -EFAULT;
		return ld2410c_set_engineering(ld, &mode);
	}
	case LD2410C_IOC_GET_VERSION: {
		struct ld2410c_version version;
		int ret = ld2410c_get_version(ld, &version);

		if (ret)
			return ret;
		if (copy_to_user(argp, &version, sizeof(version)))
			return -EFAULT;
		return 0;
	}
	case LD2410C_IOC_SET_BAUD: {
		struct ld2410c_baud baud;

		if (copy_from_user(&baud, argp, sizeof(baud)))
			return -EFAULT;
		return ld2410c_set_baud(ld, &baud);
	}
	case LD2410C_IOC_SET_RESOLUTION: {
		struct ld2410c_resolution resolution;

		if (copy_from_user(&resolution, argp, sizeof(resolution)))
			return -EFAULT;
		return ld2410c_set_resolution(ld, &resolution);
	}
	case LD2410C_IOC_SET_AUX_CONTROL: {
		struct ld2410c_aux_control aux;

		if (copy_from_user(&aux, argp, sizeof(aux)))
			return -EFAULT;
		return ld2410c_set_aux(ld, &aux);
	}
	case LD2410C_IOC_START_NOISE_CALIBRATION: {
		struct ld2410c_noise noise;
		int ret;

		if (copy_from_user(&noise, argp, sizeof(noise)))
			return -EFAULT;
		ret = ld2410c_start_noise(ld, &noise);
		if (ret)
			return ret;
		if (copy_to_user(argp, &noise, sizeof(noise)))
			return -EFAULT;
		return 0;
	}
	case LD2410C_IOC_GET_NOISE_STATUS: {
		struct ld2410c_noise noise;
		int ret;

		memset(&noise, 0, sizeof(noise));
		ret = ld2410c_get_noise(ld, &noise);
		if (ret)
			return ret;
		if (copy_to_user(argp, &noise, sizeof(noise)))
			return -EFAULT;
		return 0;
	}
	case LD2410C_IOC_FACTORY_RESET:
		return ld2410c_simple_config_cmd(ld, LD2410C_CMD_FACTORY_RESET, true);
	case LD2410C_IOC_REBOOT:
		return ld2410c_simple_config_cmd(ld, LD2410C_CMD_REBOOT, false);
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations ld2410c_fops = {
	.owner = THIS_MODULE,
	.open = ld2410c_misc_open,
	.release = ld2410c_misc_release,
	.read = ld2410c_misc_read,
	.poll = ld2410c_misc_poll,
	.unlocked_ioctl = ld2410c_misc_ioctl,
	.llseek = no_llseek,
};

static int ld2410c_probe(struct platform_device *pdev)
{
	struct ld2410c_dev *ld;
	enum of_gpio_flags of_flags;
	int ret;

	ld = devm_kzalloc(&pdev->dev, sizeof(*ld), GFP_KERNEL);
	if (!ld)
		return -ENOMEM;

	ld->dev = &pdev->dev;
	ld->out_gpio = -EINVAL;
	mutex_init(&ld->tty_lock);
	mutex_init(&ld->tx_lock);
	spin_lock_init(&ld->state_lock);
	init_waitqueue_head(&ld->read_wq);
	init_waitqueue_head(&ld->ack_wq);

	ld->out_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "out-gpios",
					       0, &of_flags);
	if (gpio_is_valid(ld->out_gpio)) {
		ld->out_active_low = !!(of_flags & OF_GPIO_ACTIVE_LOW);
		ret = devm_gpio_request_one(&pdev->dev, ld->out_gpio,
					    GPIOF_IN, "ld2410c-out");
		if (ret)
			return ret;

		ld->input = devm_input_allocate_device(&pdev->dev);
		if (!ld->input)
			return -ENOMEM;

		ld->input->name = "LD2410C presence";
		ld->input->phys = "ld2410c/out0";
		ld->input->dev.parent = &pdev->dev;
		input_set_capability(ld->input, EV_SW, SW_FRONT_PROXIMITY);

		ret = input_register_device(ld->input);
		if (ret)
			return ret;

		ld->out_irq = gpio_to_irq(ld->out_gpio);
		if (ld->out_irq < 0)
			return ld->out_irq;

		ret = devm_request_irq(&pdev->dev, ld->out_irq, ld2410c_out_irq,
				       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				       "ld2410c-out", ld);
		if (ret)
			return ret;
	} else {
		dev_warn(&pdev->dev, "out-gpios missing; input switch disabled\n");
	}

	ld->miscdev.minor = MISC_DYNAMIC_MINOR;
	ld->miscdev.name = "ld2410c0";
	ld->miscdev.fops = &ld2410c_fops;
	ld->miscdev.parent = &pdev->dev;
	ret = misc_register(&ld->miscdev);
	if (ret)
		return ret;

	mutex_lock(&ld2410c_global_lock);
	if (ld2410c_global) {
		mutex_unlock(&ld2410c_global_lock);
		misc_deregister(&ld->miscdev);
		return -EBUSY;
	}
	ld2410c_global = ld;
	mutex_unlock(&ld2410c_global_lock);

	platform_set_drvdata(pdev, ld);
	ld2410c_update_out(ld);

	dev_info(&pdev->dev, "LD2410C driver ready; ldisc=%d misc=/dev/%s\n",
		 LD2410C_LDISC, ld->miscdev.name);
	return 0;
}

static int ld2410c_remove(struct platform_device *pdev)
{
	struct ld2410c_dev *ld = platform_get_drvdata(pdev);

	mutex_lock(&ld2410c_global_lock);
	if (ld2410c_global == ld)
		ld2410c_global = NULL;
	mutex_unlock(&ld2410c_global_lock);

	misc_deregister(&ld->miscdev);
	return 0;
}

static const struct of_device_id ld2410c_of_match[] = {
	{ .compatible = "friedegg,ld2410c" },
	{ }
};
MODULE_DEVICE_TABLE(of, ld2410c_of_match);

static struct platform_driver ld2410c_driver = {
	.probe = ld2410c_probe,
	.remove = ld2410c_remove,
	.driver = {
		.name = LD2410C_NAME,
		.of_match_table = ld2410c_of_match,
	},
};

static int __init ld2410c_init(void)
{
	int ret;

	ret = tty_register_ldisc(LD2410C_LDISC, &ld2410c_ldisc_ops);
	if (ret)
		return ret;

	ret = platform_driver_register(&ld2410c_driver);
	if (ret) {
		tty_unregister_ldisc(LD2410C_LDISC);
		return ret;
	}

	return 0;
}

static void __exit ld2410c_exit(void)
{
	platform_driver_unregister(&ld2410c_driver);
	tty_unregister_ldisc(LD2410C_LDISC);
}

module_init(ld2410c_init);
module_exit(ld2410c_exit);

MODULE_AUTHOR("friedegg");
MODULE_DESCRIPTION("LD2410C presence radar OUT/input, tty ldisc parser and misc UAPI");
MODULE_LICENSE("GPL");
