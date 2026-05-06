#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "sys/ioctl.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "errno.h"
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include "ap3216creg.h"

#define APP_NAME "ap3216cApp"
#define APP_LOGE(fmt, ...) \
	fprintf(stderr, "[%s][ERR] " fmt "\n", APP_NAME, ##__VA_ARGS__)
#define APP_LOGI(fmt, ...) \
	fprintf(stdout, "[%s][INFO] " fmt "\n", APP_NAME, ##__VA_ARGS__)

#define LOCK_DEFAULT_LOOPS 200

struct lockrace_thread_stats {
	int read_ok;
	int read_fail;
	int ioctl_ok;
	int ioctl_fail;
};

struct lockrace_ctx {
	int fd;
	int loops;
	struct lockrace_thread_stats stats;
};

static void *lockrace_reader_thread(void *arg)
{
	int i;
	int ret;
	unsigned short databuf[3];
	struct lockrace_ctx *ctx = (struct lockrace_ctx *)arg;

	for (i = 0; i < ctx->loops; i++) {
		ret = read(ctx->fd, databuf, sizeof(databuf));
		if (ret == (int)sizeof(databuf)) {
			ctx->stats.read_ok++;
		} else {
			ctx->stats.read_fail++;
		}
		usleep(10000);
	}

	return NULL;
}

static void *lockrace_ioctl_thread(void *arg)
{
	int i;
	int ret;
	int mode_seq[3] = {
		AP3216C_MODE_ALS_ONLY,
		AP3216C_MODE_PS_IR_ONLY,
		AP3216C_MODE_ALS_PS_IR,
	};
	struct lockrace_ctx *ctx = (struct lockrace_ctx *)arg;

	for (i = 0; i < ctx->loops; i++) {
		ret = ioctl(ctx->fd, AP3216C_CMD_SET_MODE, mode_seq[i % 3]);
		if (ret == 0)
			ctx->stats.ioctl_ok++;
		else
			ctx->stats.ioctl_fail++;

		ret = ioctl(ctx->fd, AP3216C_CMD_SET_ALS_RATE, i & AP3216C_ALS_RATE_MASK);
		if (ret == 0)
			ctx->stats.ioctl_ok++;
		else
			ctx->stats.ioctl_fail++;

		ret = ioctl(ctx->fd, AP3216C_CMD_SET_PS_RATE, i & AP3216C_PS_RATE_MASK);
		if (ret == 0)
			ctx->stats.ioctl_ok++;
		else
			ctx->stats.ioctl_fail++;

		usleep(10000);
	}

	return NULL;
}

static int run_read_mode(const char *filename)
{
	int fd;
	unsigned short databuf[3];
	unsigned short ir, als, ps;
	int ret;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		if (errno == EBUSY) {
			APP_LOGE("device busy: %s is already opened by another process", filename);
			return -1;
		}
		APP_LOGE("can't open file %s: %s", filename, strerror(errno));
		return -1;
	}

	APP_LOGI("mode=read, open %s success", filename);

	while (1) {
		ret = read(fd, databuf, sizeof(databuf));
		if (ret == (int)sizeof(databuf)) {
			ir = databuf[0];
			als = databuf[1];
			ps = databuf[2];
			APP_LOGI("ir=%u, als=%u, ps=%u", ir, als, ps);
		} else if (ret < 0) {
			APP_LOGE("read failed: %s", strerror(errno));
			break;
		} else {
			APP_LOGE("short read: %d bytes (expect %zu)", ret, sizeof(databuf));
		}
		usleep(200000); /* 200ms */
	}

	close(fd);
	return 0;
}

/*
 * 双进程并发 open 测试：
 * 父子进程同时尝试打开设备，独占策略下每轮应仅有一个成功。
 */
static int run_atomic2_mode(const char *filename, int rounds)
{
	int i;
	int pass = 0;
	int fail = 0;

	for (i = 0; i < rounds; i++) {
		int pipefd[2];
		pid_t pid;
		int parent_ok = 0;
		int child_ok = 0;
		int status = 0;
		char start_token = 'S';

		if (pipe(pipefd) < 0) {
			APP_LOGE("pipe failed: %s", strerror(errno));
			return -1;
		}

		pid = fork();
		if (pid < 0) {
			APP_LOGE("fork failed: %s", strerror(errno));
			close(pipefd[0]);
			close(pipefd[1]);
			return -1;
		}

		if (pid == 0) {
			int fd;
			char token;
			int rc;
			int saved_errno = 0;

			close(pipefd[1]);
			rc = read(pipefd[0], &token, 1);
			close(pipefd[0]);
			if (rc != 1)
				_exit(3);

			fd = open(filename, O_RDWR);
			saved_errno = errno;
			if (fd >= 0) {
				APP_LOGI("[child][round=%d] open success", i + 1);
				usleep(150000);
				close(fd);
				_exit(0);
			}

			if (saved_errno == EBUSY) {
				APP_LOGI("[child][round=%d] open busy", i + 1);
				_exit(2);
			}

			APP_LOGE("[child][round=%d] open failed: %s", i + 1, strerror(saved_errno));
			_exit(1);
		}

		close(pipefd[0]);
		if (write(pipefd[1], &start_token, 1) != 1)
			APP_LOGE("write start token failed: %s", strerror(errno));
		close(pipefd[1]);

		{
			int fd;
			int saved_errno = 0;
			fd = open(filename, O_RDWR);
			saved_errno = errno;
			if (fd >= 0) {
				parent_ok = 1;
				APP_LOGI("[parent][round=%d] open success", i + 1);
				usleep(150000);
				close(fd);
			} else if (saved_errno == EBUSY) {
				APP_LOGI("[parent][round=%d] open busy", i + 1);
			} else {
				APP_LOGE("[parent][round=%d] open failed: %s", i + 1, strerror(saved_errno));
			}
		}

		if (waitpid(pid, &status, 0) < 0) {
			APP_LOGE("waitpid failed: %s", strerror(errno));
			return -1;
		}

		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			child_ok = 1;

		if ((parent_ok + child_ok) == 1) {
			pass++;
			APP_LOGI("[round=%d] PASS (exactly one process opened device)", i + 1);
		} else {
			fail++;
			APP_LOGE("[round=%d] FAIL (parent_ok=%d child_ok=%d)", i + 1, parent_ok, child_ok);
		}

		usleep(80000);
	}

	APP_LOGI("atomic2 summary: rounds=%d pass=%d fail=%d", rounds, pass, fail);
	return (fail == 0) ? 0 : -1;
}

/*
 * ioctl 配置演示模式：
 * 参数按寄存器位值直传，便于和驱动日志、手册寄存器字段一一对应。
 */
static int run_ioctldemo_mode(const char *filename, int mode, int als_rate, int ps_rate)
{
	int fd;
	unsigned short databuf[3];
	int ret;
	int i;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		if (errno == EBUSY) {
			APP_LOGE("device busy: %s is already opened by another process", filename);
			return -1;
		}
		APP_LOGE("can't open file %s: %s", filename, strerror(errno));
		return -1;
	}

	APP_LOGI("mode=ioctldemo, open %s success", filename);

	ret = ioctl(fd, AP3216C_CMD_SET_MODE, mode);
	if (ret < 0) {
		APP_LOGE("ioctl SET_MODE failed: mode=%d err=%s", mode, strerror(errno));
		close(fd);
		return -1;
	}
	APP_LOGI("ioctl SET_MODE ok: mode=%d", mode);

	ret = ioctl(fd, AP3216C_CMD_SET_ALS_RATE, als_rate);
	if (ret < 0) {
		APP_LOGE("ioctl SET_ALS_RATE failed: als_rate=0x%x err=%s", als_rate, strerror(errno));
		close(fd);
		return -1;
	}
	APP_LOGI("ioctl SET_ALS_RATE ok: als_rate=0x%x", als_rate);

	ret = ioctl(fd, AP3216C_CMD_SET_PS_RATE, ps_rate);
	if (ret < 0) {
		APP_LOGE("ioctl SET_PS_RATE failed: ps_rate=0x%x err=%s", ps_rate, strerror(errno));
		close(fd);
		return -1;
	}
	APP_LOGI("ioctl SET_PS_RATE ok: ps_rate=0x%x", ps_rate);

	for (i = 0; i < 5; i++) {
		ret = read(fd, databuf, sizeof(databuf));
		if (ret == (int)sizeof(databuf)) {
			APP_LOGI("sample[%d]: ir=%u, als=%u, ps=%u", i + 1, databuf[0], databuf[1], databuf[2]);
		} else if (ret < 0) {
			APP_LOGE("read failed after ioctl: %s", strerror(errno));
			close(fd);
			return -1;
		} else {
			APP_LOGE("short read after ioctl: %d bytes", ret);
			close(fd);
			return -1;
		}
		usleep(200000);
	}

	close(fd);
	return 0;
}

/*
 * lockrace: 单进程双线程并发测试
 * 线程A持续 read，线程B持续 ioctl，验证锁设计在并发下的稳定性。
 */
static int run_lockrace_mode(const char *filename, int loops)
{
	int fd;
	int ret;
	pthread_t th_reader;
	pthread_t th_ioctl;
	struct lockrace_ctx reader_ctx;
	struct lockrace_ctx ioctl_ctx;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		APP_LOGE("lockrace open failed: %s", strerror(errno));
		return -1;
	}

	memset(&reader_ctx, 0, sizeof(reader_ctx));
	memset(&ioctl_ctx, 0, sizeof(ioctl_ctx));
	reader_ctx.fd = fd;
	ioctl_ctx.fd = fd;
	reader_ctx.loops = loops;
	ioctl_ctx.loops = loops;

	ret = pthread_create(&th_reader, NULL, lockrace_reader_thread, &reader_ctx);
	if (ret != 0) {
		APP_LOGE("pthread_create reader failed: %s", strerror(ret));
		close(fd);
		return -1;
	}

	ret = pthread_create(&th_ioctl, NULL, lockrace_ioctl_thread, &ioctl_ctx);
	if (ret != 0) {
		APP_LOGE("pthread_create ioctl failed: %s", strerror(ret));
		pthread_join(th_reader, NULL);
		close(fd);
		return -1;
	}

	pthread_join(th_reader, NULL);
	pthread_join(th_ioctl, NULL);

	APP_LOGI("lockrace summary: loops=%d read_ok=%d read_fail=%d ioctl_ok=%d ioctl_fail=%d",
		loops,
		reader_ctx.stats.read_ok,
		reader_ctx.stats.read_fail,
		ioctl_ctx.stats.ioctl_ok,
		ioctl_ctx.stats.ioctl_fail);

	close(fd);
	return (reader_ctx.stats.read_fail == 0 && ioctl_ctx.stats.ioctl_fail == 0) ? 0 : -1;
}

/*
 * lockstress: 单线程高频“ioctl+read”循环
 * 用于长时间压测并统计错误率。
 */
static int run_lockstress_mode(const char *filename, int loops)
{
	int i;
	int fd;
	int ret;
	int ok = 0;
	int fail = 0;
	unsigned short databuf[3];

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		APP_LOGE("lockstress open failed: %s", strerror(errno));
		return -1;
	}

	for (i = 0; i < loops; i++) {
		ret = ioctl(fd, AP3216C_CMD_SET_MODE, AP3216C_MODE_ALS_PS_IR);
		if (ret < 0)
			fail++;
		else
			ok++;

		ret = ioctl(fd, AP3216C_CMD_SET_ALS_RATE, i & AP3216C_ALS_RATE_MASK);
		if (ret < 0)
			fail++;
		else
			ok++;

		ret = ioctl(fd, AP3216C_CMD_SET_PS_RATE, i & AP3216C_PS_RATE_MASK);
		if (ret < 0)
			fail++;
		else
			ok++;

		ret = read(fd, databuf, sizeof(databuf));
		if (ret == (int)sizeof(databuf))
			ok++;
		else
			fail++;
	}

	APP_LOGI("lockstress summary: loops=%d ok=%d fail=%d", loops, ok, fail);
	close(fd);
	return (fail == 0) ? 0 : -1;
}

/*
 * irqdemo: 读取驱动事件模式与统计信息。
 * 在 POLL_SIM 模式下可辅助展示“中断框架+轮询注入”链路。
 */
static int run_irqdemo_mode(const char *filename, int loops)
{
	int i;
	int fd;
	int ret;
	int mode = AP3216C_EVENT_MODE_UNKNOWN;
	struct ap3216c_event_stats stats;
	struct ap3216c_event_stats prev;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		APP_LOGE("irqdemo open failed: %s", strerror(errno));
		return -1;
	}

	ret = ioctl(fd, AP3216C_CMD_GET_EVENT_MODE, &mode);
	if (ret < 0) {
		APP_LOGE("ioctl GET_EVENT_MODE failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	if (mode == AP3216C_EVENT_MODE_HW_IRQ)
		APP_LOGI("event mode: HW_IRQ");
	else if (mode == AP3216C_EVENT_MODE_POLL_SIM)
		APP_LOGI("event mode: POLL_SIM");
	else
		APP_LOGI("event mode: UNKNOWN(%d)", mode);

	memset(&prev, 0, sizeof(prev));
	for (i = 0; i < loops; i++) {
		ret = ioctl(fd, AP3216C_CMD_GET_EVENT_STATS, &stats);
		if (ret < 0) {
			APP_LOGE("ioctl GET_EVENT_STATS failed: %s", strerror(errno));
			close(fd);
			return -1;
		}

		// if ((i % 10) == 9)
		// {
		// 	APP_LOGI("stats[%d]: total=%u(+%u) hw=%u poll=%u manual=%u last_ps=%u last_src=%u",
		// 			 i + 1,
		// 			 stats.total_events,
		// 			 stats.total_events - prev.total_events,
		// 			 stats.hw_irq_events,
		// 			 stats.poll_sim_events,
		// 			 stats.manual_events,
		// 			 stats.last_ps,
		// 			 stats.last_source);
		// 	APP_LOGI("irqdiag[%d]: entries=%u no_status=%u filtered=%u",
		// 			 i + 1,
		// 			 stats.irq_entries,
		// 			 stats.irq_no_status_events,
		// 			 stats.irq_filtered_events);
		// }

		// /* 每 5 轮手动注入一次，演示事件处理链路可达。 */
		// if ((i % 5) == 4) {
		// 	ret = ioctl(fd, AP3216C_CMD_TRIGGER_EVENT);
		// 	if (ret < 0)
		// 		APP_LOGE("ioctl TRIGGER_EVENT failed: %s", strerror(errno));
		// 	else
		// 		APP_LOGI("manual trigger injected at loop %d", i + 1);
		// }

		prev = stats;
		usleep(200000);
	}

	close(fd);
	return 0;
}

/*
 * irqcfg: 运行时切换事件模式并设置触发阈值。
 * mode: 1=HW_IRQ, 2=POLL_SIM
 */
static int run_irqcfg_mode(const char *filename, int mode, int ps_th, int als_delta_th, int poll_interval_ms)
{
	int fd;
	int ret;
	int current_mode = AP3216C_EVENT_MODE_UNKNOWN;
	struct ap3216c_event_stats stats;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		APP_LOGE("irqcfg open failed: %s", strerror(errno));
		return -1;
	}

	ret = ioctl(fd, AP3216C_CMD_SET_EVENT_MODE, mode);
	if (ret < 0) {
		APP_LOGE("ioctl SET_EVENT_MODE failed: mode=%d err=%s", mode, strerror(errno));
		close(fd);
		return -1;
	}
	APP_LOGI("ioctl SET_EVENT_MODE ok: mode=%d", mode);

	ret = ioctl(fd, AP3216C_CMD_SET_PS_TRIGGER_TH, ps_th);
	if (ret < 0) {
		APP_LOGE("ioctl SET_PS_TRIGGER_TH failed: th=%d err=%s", ps_th, strerror(errno));
		close(fd);
		return -1;
	}
	APP_LOGI("ioctl SET_PS_TRIGGER_TH ok: th=%d", ps_th);

	ret = ioctl(fd, AP3216C_CMD_SET_ALS_DELTA_TH, als_delta_th);
	if (ret < 0) {
		APP_LOGE("ioctl SET_ALS_DELTA_TH failed: th=%d err=%s", als_delta_th, strerror(errno));
		close(fd);
		return -1;
	}
	APP_LOGI("ioctl SET_ALS_DELTA_TH ok: th=%d", als_delta_th);

	ret = ioctl(fd, AP3216C_CMD_SET_POLL_INTERVAL_MS, poll_interval_ms);
	if (ret < 0) {
		APP_LOGE("ioctl SET_POLL_INTERVAL_MS failed: interval=%d err=%s", poll_interval_ms, strerror(errno));
		close(fd);
		return -1;
	}
	APP_LOGI("ioctl SET_POLL_INTERVAL_MS ok: interval=%dms", poll_interval_ms);

	ret = ioctl(fd, AP3216C_CMD_GET_EVENT_MODE, &current_mode);
	if (ret < 0) {
		APP_LOGE("ioctl GET_EVENT_MODE failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	ret = ioctl(fd, AP3216C_CMD_GET_EVENT_STATS, &stats);
	if (ret < 0) {
		APP_LOGE("ioctl GET_EVENT_STATS failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	APP_LOGI("irqcfg summary: mode=%d total=%u hw=%u poll=%u manual=%u last_ps=%u",
		current_mode,
		stats.total_events,
		stats.hw_irq_events,
		stats.poll_sim_events,
		stats.manual_events,
		stats.last_ps);

	close(fd);
	return 0;
}


/*
 * @description		: main主程序
 * @param - argc 	: argv数组元素个数
 * @param - argv 	: 具体参数
 * @return 			: 0 成功;其他 失败
 */
int main(int argc, char *argv[])
{
	const char *mode = "read";
	char *filename;
	int rounds = 10;
	int loops = LOCK_DEFAULT_LOOPS;
	int mode_val;
	int als_rate;
	int ps_rate;

	if (argc < 2 || argc > 7) {
		APP_LOGE("usage: ./ap3216cApp /dev/ap3216c [mode] [args...]");
		APP_LOGE("mode: read(default) | atomic2 [rounds] | ioctldemo <mode> <als_rate> <ps_rate>");
		APP_LOGE("mode: lockrace [loops] | lockstress [loops] | irqdemo [loops]");
		APP_LOGE("mode: irqcfg <event_mode> <ps_th> <als_delta_th> [poll_interval_ms]");
		APP_LOGE("example: ./ap3216cApp /dev/ap3216c atomic2 20");
		APP_LOGE("example: ./ap3216cApp /dev/ap3216c ioctldemo 3 2 1");
		APP_LOGE("example: ./ap3216cApp /dev/ap3216c lockrace 300");
		APP_LOGE("example: ./ap3216cApp /dev/ap3216c lockstress 1000");
		APP_LOGE("example: ./ap3216cApp /dev/ap3216c irqdemo 30");
		APP_LOGE("example: ./ap3216cApp /dev/ap3216c irqcfg 2 200 200 10");
		return -1;
	}

	filename = argv[1];
	if (argc >= 3)
		mode = argv[2];

	if (argc == 4) {
		rounds = atoi(argv[3]);
		if (rounds <= 0) {
			APP_LOGE("invalid rounds: %s", argv[3]);
			return -1;
		}
	}

	if (strcmp(mode, "read") == 0)
		return run_read_mode(filename);

	if (strcmp(mode, "atomic2") == 0)
		return run_atomic2_mode(filename, rounds);

	if (strcmp(mode, "lockrace") == 0) {
		if (argc >= 4) {
			loops = atoi(argv[3]);
			if (loops <= 0) {
				APP_LOGE("invalid loops: %s", argv[3]);
				return -1;
			}
		}
		return run_lockrace_mode(filename, loops);
	}

	if (strcmp(mode, "lockstress") == 0) {
		if (argc >= 4) {
			loops = atoi(argv[3]);
			if (loops <= 0) {
				APP_LOGE("invalid loops: %s", argv[3]);
				return -1;
			}
		}
		return run_lockstress_mode(filename, loops);
	}

	if (strcmp(mode, "irqdemo") == 0) {
		if (argc >= 4) {
			loops = atoi(argv[3]);
			if (loops <= 0) {
				APP_LOGE("invalid loops: %s", argv[3]);
				return -1;
			}
		}
		return run_irqdemo_mode(filename, loops);
	}

	if (strcmp(mode, "ioctldemo") == 0) {
		if (argc != 6) {
			APP_LOGE("usage: ./ap3216cApp /dev/ap3216c ioctldemo <mode> <als_rate> <ps_rate>");
			return -1;
		}

		mode_val = atoi(argv[3]);
		als_rate = (int)strtol(argv[4], NULL, 0);
		ps_rate = (int)strtol(argv[5], NULL, 0);
		return run_ioctldemo_mode(filename, mode_val, als_rate, ps_rate);
	}

	if (strcmp(mode, "irqcfg") == 0) {
		int event_mode;
		int ps_th;
		int als_delta_th;
		int poll_interval_ms = 10;

		if (argc != 6 && argc != 7) {
			APP_LOGE("usage: ./ap3216cApp /dev/ap3216c irqcfg <event_mode> <ps_th> <als_delta_th> [poll_interval_ms]");
			APP_LOGE("event_mode: 1=HW_IRQ, 2=POLL_SIM");
			return -1;
		}

		event_mode = atoi(argv[3]);
		ps_th = atoi(argv[4]);
		als_delta_th = atoi(argv[5]);
		if (argc == 7)
			poll_interval_ms = atoi(argv[6]);

		if (event_mode != AP3216C_EVENT_MODE_HW_IRQ && event_mode != AP3216C_EVENT_MODE_POLL_SIM) {
			APP_LOGE("invalid event_mode=%d, expected 1 or 2", event_mode);
			return -1;
		}

		if (ps_th < 0 || als_delta_th < 0 || poll_interval_ms <= 0) {
			APP_LOGE("invalid thresholds/interval: ps_th=%d als_delta_th=%d poll_interval_ms=%d",
				ps_th,
				als_delta_th,
				poll_interval_ms);
			return -1;
		}

		return run_irqcfg_mode(filename, event_mode, ps_th, als_delta_th, poll_interval_ms);
	}

	APP_LOGE("unknown mode: %s", mode);
	APP_LOGE("mode: read(default) | atomic2 [rounds] | ioctldemo <mode> <als_rate> <ps_rate>");
	APP_LOGE("mode: lockrace [loops] | lockstress [loops] | irqdemo [loops]");
	APP_LOGE("mode: irqcfg <event_mode> <ps_th> <als_delta_th> [poll_interval_ms]");
	return -1;
}

