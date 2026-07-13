#!/bin/sh

set -u

# V4L2 capture node under test.
DEV=/dev/video1

# Number of frames captured by each matrix case.
COUNT=30

# Matrix preset. "smoke" is a short stable subset; "full" includes wider
# PDF-target sizes/fps and can take much longer.
MODE=smoke

# Requested V4L2 fourcc list. These are requests; the ACT_* columns printed by
# the script are the authoritative values returned by the driver after S_FMT and
# S_PARM negotiation.
FORMATS="RGBP UYVY YUYV GREY"

# Requested size presets. Unsupported request sizes may be snapped by the driver
# to a supported discrete mode; such cases are reported as PASS_ADJUST.
SMOKE_SIZES="800x480 640x480 320x240 1280x720"
FULL_SIZES="176x144 320x240 640x480 720x480 720x576 800x480 1024x768 1280x720 1280x960 1920x1080 2592x1944"

# Requested frame-rate presets. Unsupported fps for the active size fails at
# S_PARM unless S_FMT has already snapped the size to one that supports it.
SMOKE_FPS="15 30 45 60"
FULL_FPS="15 30 45 60 90 120"

# Optional user overrides for the requested size/fps lists. Empty means derive
# them from MODE.
SIZES=""
FPS_LIST=""

# Empty LOG_DIR creates /tmp/ov5640-v4l2-matrix-<time> on the target.
LOG_DIR=""
LIST_ONLY=0

# Warm-up runs a known stable stream before the matrix to absorb cold-start
# sensor/CSI timing. Keep this combo exact and stable so warm-up itself does not
# alter state through nearest-mode adjustment.
WARMUP=1
WARMUP_FORMAT="RGBP"
WARMUP_SIZE="800x480"
WARMUP_FPS="30"
WARMUP_COUNT=5
WARMUP_SLEEP=1

# Per-case stream retry. This retries only the stream step after S_FMT/S_PARM
# have succeeded; it does not convert FAIL_FMT or FAIL_PARM into success.
STREAM_RETRIES=1
RETRY_SLEEP=1

# Maximum seconds to wait for one v4l2-ctl stream command. A value of 0 disables
# the guard. This prevents a no-frame IRQ/DMA case from blocking the whole matrix.
STREAM_TIMEOUT=15

usage()
{
	cat <<EOF_USAGE
Usage: $0 [options]

Options:
  -d, --device DEV       video device, default: /dev/video1
  -c, --count N          stream frame count per case, default: 30
      --formats LIST     quoted fourcc list, default: "RGBP UYVY YUYV GREY"
      --sizes LIST       quoted WxH list, overrides smoke/full sizes
      --fps LIST         quoted fps list, overrides smoke/full fps
      --full             test wider mode/fps matrix including PDF target rates
      --log-dir DIR      output log directory, default: /tmp/ov5640-v4l2-matrix-<time>
      --list-only        only dump --list-formats-ext and exit
      --no-warmup        skip the pre-matrix warm-up stream
      --warmup-format F  warm-up fourcc, default: RGBP
      --warmup-size WxH  warm-up size, default: 800x480
      --warmup-fps FPS   warm-up fps, default: 30
      --warmup-count N   frame count for warm-up stream, default: 5
      --warmup-sleep S   seconds before warm-up retry, default: 1
      --retries N        stream retries per matrix case, default: 1
      --retry-sleep S    seconds before per-case stream retry, default: 1
      --stream-timeout S seconds before killing a stuck stream, default: 15
  -h, --help             show this help

Examples:
  $0 -d /dev/video1
  $0 --full -c 10
  $0 --sizes "640x480 1280x720" --fps "15 30 60" --formats "UYVY YUYV"
EOF_USAGE
}

need_arg()
{
	if [ "$#" -lt 2 ]; then
		echo "Missing value for $1" >&2
		exit 2
	fi
}

first_word()
{
	for word in $1; do
		printf '%s\n' "$word"
		return 0
	done

	return 1
}

run_stream_capture()
{
	stream_count=$1

	if [ "$STREAM_TIMEOUT" -le 0 ] 2>/dev/null; then
		v4l2-ctl -d "$DEV" --stream-mmap --stream-count="$stream_count"
		return $?
	fi

	v4l2-ctl -d "$DEV" --stream-mmap --stream-count="$stream_count" &
	stream_pid=$!

	(
		sleep "$STREAM_TIMEOUT"
		echo "stream timeout after ${STREAM_TIMEOUT}s; killing v4l2-ctl"
		kill "$stream_pid" 2>/dev/null
	) &
	timer_pid=$!

	wait "$stream_pid"
	stream_rc=$?
	kill "$timer_pid" 2>/dev/null
	wait "$timer_pid" 2>/dev/null

	return "$stream_rc"
}

run_warmup()
{
	warm_fmt=$WARMUP_FORMAT
	warm_size=$WARMUP_SIZE
	warm_fps=$WARMUP_FPS

	case "$warm_size" in
	*x*)
		warm_width=${warm_size%x*}
		warm_height=${warm_size#*x}
		;;
	*)
		echo "warmup: skip invalid size: $warm_size" >&2
		return 0
		;;
	esac

	warm_attempt=1
	while [ "$warm_attempt" -le 2 ]; do
		warm_log="$LOG_DIR/warmup_attempt${warm_attempt}.log"
		{
			echo "warmup attempt=$warm_attempt fmt=$warm_fmt size=$warm_size fps=$warm_fps"
			echo
			echo "[set-fmt]"
			echo "v4l2-ctl -d $DEV --set-fmt-video=width=$warm_width,height=$warm_height,pixelformat=$warm_fmt"
		} >"$warm_log"

		v4l2-ctl -d "$DEV" \
			--set-fmt-video=width="$warm_width",height="$warm_height",pixelformat="$warm_fmt" \
			>>"$warm_log" 2>&1
		rc_fmt=$?

		{
			echo
			echo "[set-parm]"
			echo "v4l2-ctl -d $DEV --set-parm=$warm_fps"
		} >>"$warm_log"
		v4l2-ctl -d "$DEV" --set-parm="$warm_fps" >>"$warm_log" 2>&1
		rc_parm=$?

		{
			echo
			echo "[stream]"
			echo "v4l2-ctl -d $DEV --stream-mmap --stream-count=$WARMUP_COUNT"
		} >>"$warm_log"
		if [ "$rc_fmt" -eq 0 ] && [ "$rc_parm" -eq 0 ]; then
			run_stream_capture "$WARMUP_COUNT" >>"$warm_log" 2>&1
			rc_stream=$?
		else
			rc_stream=1
		fi

		if [ "$rc_fmt" -eq 0 ] && [ "$rc_parm" -eq 0 ] && [ "$rc_stream" -eq 0 ]; then
			echo "warmup: PASS attempt=$warm_attempt log=$warm_log"
			return 0
		fi

		echo "warmup: FAIL attempt=$warm_attempt log=$warm_log"
		warm_attempt=$((warm_attempt + 1))
		if [ "$warm_attempt" -le 2 ]; then
			sleep "$WARMUP_SLEEP"
		fi
	done

	echo "warmup: failed; continue matrix to expose the real failing stage"
	return 0
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	-d|--device)
		need_arg "$@"
		DEV=$2
		shift 2
		;;
	-c|--count)
		need_arg "$@"
		COUNT=$2
		shift 2
		;;
	--formats)
		need_arg "$@"
		FORMATS=$2
		shift 2
		;;
	--sizes)
		need_arg "$@"
		SIZES=$2
		shift 2
		;;
	--fps)
		need_arg "$@"
		FPS_LIST=$2
		shift 2
		;;
	--full)
		MODE=full
		shift
		;;
	--log-dir)
		need_arg "$@"
		LOG_DIR=$2
		shift 2
		;;
	--list-only)
		LIST_ONLY=1
		shift
		;;
	--no-warmup)
		WARMUP=0
		shift
		;;
	--warmup-format)
		need_arg "$@"
		WARMUP_FORMAT=$2
		shift 2
		;;
	--warmup-size)
		need_arg "$@"
		WARMUP_SIZE=$2
		shift 2
		;;
	--warmup-fps)
		need_arg "$@"
		WARMUP_FPS=$2
		shift 2
		;;
	--warmup-count)
		need_arg "$@"
		WARMUP_COUNT=$2
		shift 2
		;;
	--warmup-sleep)
		need_arg "$@"
		WARMUP_SLEEP=$2
		shift 2
		;;
	--retries)
		need_arg "$@"
		STREAM_RETRIES=$2
		shift 2
		;;
	--retry-sleep)
		need_arg "$@"
		RETRY_SLEEP=$2
		shift 2
		;;
	--stream-timeout)
		need_arg "$@"
		STREAM_TIMEOUT=$2
		shift 2
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		echo "Unknown option: $1" >&2
		usage >&2
		exit 2
		;;
	esac
done

if [ -z "$SIZES" ]; then
	if [ "$MODE" = "full" ]; then
		SIZES=$FULL_SIZES
	else
		SIZES=$SMOKE_SIZES
	fi
fi

if [ -z "$FPS_LIST" ]; then
	if [ "$MODE" = "full" ]; then
		FPS_LIST=$FULL_FPS
	else
		FPS_LIST=$SMOKE_FPS
	fi
fi

if ! command -v v4l2-ctl >/dev/null 2>&1; then
	echo "v4l2-ctl not found; install v4l-utils on target rootfs." >&2
	exit 127
fi

if [ ! -e "$DEV" ]; then
	echo "Video device not found: $DEV" >&2
	exit 1
fi

if [ -z "$LOG_DIR" ]; then
	LOG_DIR="/tmp/ov5640-v4l2-matrix-$(date +%Y%m%d-%H%M%S)"
fi

mkdir -p "$LOG_DIR" || exit 1

LIST_LOG="$LOG_DIR/list-formats-ext.log"
v4l2-ctl -d "$DEV" --list-formats-ext >"$LIST_LOG" 2>&1

echo "device: $DEV"
echo "mode: $MODE"
echo "formats: $FORMATS"
echo "sizes: $SIZES"
echo "fps: $FPS_LIST"
echo "stream-count: $COUNT"
echo "warmup: $WARMUP"
echo "warmup-combo: $WARMUP_FORMAT $WARMUP_SIZE ${WARMUP_FPS}fps"
echo "warmup-count: $WARMUP_COUNT"
echo "stream-retries: $STREAM_RETRIES"
echo "stream-timeout: $STREAM_TIMEOUT"
echo "log-dir: $LOG_DIR"
echo

if [ "$LIST_ONLY" -eq 1 ]; then
	cat "$LIST_LOG"
	exit 0
fi

if [ "$WARMUP" -eq 1 ]; then
	run_warmup
	echo
fi

printf '%-11s %-9s %-10s %-6s %-9s %-10s %-9s %s\n' \
	"RESULT" "REQ_FMT" "REQ_SIZE" "REQFPS" "ACT_FMT" "ACT_SIZE" "ACT_FPS" "LOG"

total=0
pass=0
fail=0

for fmt in $FORMATS; do
	for size in $SIZES; do
		case "$size" in
		*x*)
			width=${size%x*}
			height=${size#*x}
			;;
		*)
			echo "Skip invalid size: $size" >&2
			continue
			;;
		esac

		for fps in $FPS_LIST; do
			total=$((total + 1))
			base="${fmt}_${size}_${fps}fps"
			log="$LOG_DIR/$base.log"
			result=PASS

			{
				echo "case: fmt=$fmt size=$size fps=$fps"
				echo
				echo "[set-fmt]"
				echo "v4l2-ctl -d $DEV --set-fmt-video=width=$width,height=$height,pixelformat=$fmt"
			} >"$log"

			v4l2-ctl -d "$DEV" \
				--set-fmt-video=width="$width",height="$height",pixelformat="$fmt" \
				>>"$log" 2>&1
			rc_fmt=$?

			{
				echo
				echo "[get-fmt after set-fmt]"
			} >>"$log"
			v4l2-ctl -d "$DEV" --get-fmt-video >>"$log" 2>&1

			if [ "$rc_fmt" -ne 0 ]; then
				result=FAIL_FMT
			else
				{
					echo
					echo "[set-parm]"
					echo "v4l2-ctl -d $DEV --set-parm=$fps"
				} >>"$log"
				v4l2-ctl -d "$DEV" --set-parm="$fps" >>"$log" 2>&1
				rc_parm=$?

				{
					echo
					echo "[get-parm after set-parm]"
				} >>"$log"
				v4l2-ctl -d "$DEV" --get-parm >>"$log" 2>&1

				if [ "$rc_parm" -ne 0 ]; then
					result=FAIL_PARM
				else
					{
						echo
						echo "[stream]"
						echo "v4l2-ctl -d $DEV --stream-mmap --stream-count=$COUNT"
					} >>"$log"
					stream_attempt=0
					while :; do
						stream_attempt=$((stream_attempt + 1))
						run_stream_capture "$COUNT" >>"$log" 2>&1
						rc_stream=$?
						if [ "$rc_stream" -eq 0 ]; then
							if [ "$stream_attempt" -gt 1 ]; then
								result=PASS_RETRY
							fi
							break
						fi

						if [ "$stream_attempt" -gt "$STREAM_RETRIES" ]; then
							result=FAIL_STREAM
							break
						fi

						{
							echo
							echo "[stream retry after failure attempt=$stream_attempt]"
							echo "sleep $RETRY_SLEEP"
						} >>"$log"
						sleep "$RETRY_SLEEP"
					done
				fi
			fi

			act_fmt=$(sed -n "s/.*Pixel Format[[:space:]]*: '\([^']*\)'.*/\1/p" "$log" | tail -n 1)
			act_size=$(sed -n 's/.*Width\/Height[[:space:]]*: \([0-9][0-9]*\)\/\([0-9][0-9]*\).*/\1x\2/p' "$log" | tail -n 1)
			act_fps=$(sed -n 's/.*Frames per second[[:space:]]*: \([0-9.][0-9.]*\).*/\1/p' "$log" | tail -n 1)

			[ -n "$act_fmt" ] || act_fmt="-"
			[ -n "$act_size" ] || act_size="-"
			[ -n "$act_fps" ] || act_fps="-"

			if [ "$result" = "PASS" ] || [ "$result" = "PASS_RETRY" ]; then
				adjusted=0
				act_fps_int=${act_fps%%.*}
				[ "$act_fmt" = "$fmt" ] || adjusted=1
				[ "$act_size" = "$size" ] || adjusted=1
				[ "$act_fps_int" = "$fps" ] || adjusted=1
				if [ "$adjusted" -eq 1 ]; then
					result=PASS_ADJUST
				fi
				pass=$((pass + 1))
			else
				fail=$((fail + 1))
			fi

			printf '%-11s %-9s %-10s %-6s %-9s %-10s %-9s %s\n' \
				"$result" "$fmt" "$size" "$fps" "$act_fmt" "$act_size" "$act_fps" "$log"
		done
	done
done

echo
echo "summary: total=$total pass=$pass fail=$fail"
echo "format enumeration: $LIST_LOG"

if [ "$fail" -ne 0 ]; then
	exit 1
fi

exit 0
