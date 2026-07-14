#!/bin/sh

set -u

# V4L2 capture node under test.
DEV=/dev/video1

# Number of frames captured by each matrix case. Empty means auto:
# stream-count = requested fps * 2.
COUNT=""

# Matrix preset. "smoke" is a short stable subset; "full" includes wider
# PDF-target sizes/fps and can take much longer.
MODE=smoke

# Requested V4L2 fourcc list. These are requests; the ACT_* columns printed by
# the script are split into driver-reported values after S_FMT/S_PARM and
# stream-measured fps parsed from v4l2-ctl streaming timestamps.
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
  -c, --count N          override stream frame count per case, default: requested fps * 2
      --formats LIST     quoted fourcc list, default: "RGBP UYVY YUYV GREY"
      --sizes LIST       quoted WxH list, overrides smoke/full sizes
      --fps LIST         quoted fps list, overrides smoke/full fps
      --full             test wider mode/fps matrix including PDF target rates
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
  $0 --full
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

run_v4l2_stream_command()
{
	stream_count=$1
	stream_fmt=${2:-}
	stream_width=${3:-}
	stream_height=${4:-}
	stream_fps=${5:-}

	if [ -n "$stream_fmt" ]; then
		v4l2-ctl --verbose -d "$DEV" \
			--set-fmt-video=width="$stream_width",height="$stream_height",pixelformat="$stream_fmt" \
			--set-parm="$stream_fps" \
			--get-fmt-video \
			--get-parm \
			--stream-mmap \
			--stream-count="$stream_count"
	else
		v4l2-ctl --verbose -d "$DEV" \
			--stream-mmap \
			--stream-count="$stream_count"
	fi
}

run_stream_capture()
{
	if [ "$STREAM_TIMEOUT" -le 0 ] 2>/dev/null; then
		run_v4l2_stream_command "$@"
		return $?
	fi

	run_v4l2_stream_command "$@" &
	stream_pid=$!

	(
		sleep "$STREAM_TIMEOUT"
		kill "$stream_pid" 2>/dev/null
	) >/dev/null 2>&1 &
	timer_pid=$!

	wait "$stream_pid"
	stream_rc=$?
	kill "$timer_pid" 2>/dev/null
	wait "$timer_pid" 2>/dev/null

	return "$stream_rc"
}

extract_driver_fps()
{
	sed -n "s/.*Frames per second[[:space:]]*: \([0-9.][0-9.]*\).*/\1/p" | tail -n 1
}

extract_stream_fps()
{
	sed -n \
		-e "s/.*fps: \([0-9.][0-9.]*\).*/\1/p" \
		-e "s/.*[[:space:]]\([0-9.][0-9.]*\)[[:space:]]fps.*/\1/p" |
		head -n 1
}

stream_count_for_fps()
{
	requested_fps=$1

	awk -v fps="$requested_fps" '
		BEGIN {
			if (fps <= 0)
				exit 1;
			count = int(fps * 2 + 0.5);
			if (count < 1)
				count = 1;
			print count;
		}
	'
}

fps_matches_request()
{
	actual=$1
	requested=$2

	if [ -z "$actual" ] || [ "$actual" = "-" ]; then
		return 1
	fi

	awk -v actual="$actual" -v requested="$requested" "
		BEGIN {
			if (actual <= 0 || requested <= 0)
				exit 1;
			diff = actual - requested;
			if (diff < 0)
				diff = -diff;
			tol = requested * 0.05;
			if (tol < 1)
				tol = 1;
			exit(diff <= tol ? 0 : 1);
		}"
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
		v4l2-ctl -d "$DEV" \
			--set-fmt-video=width="$warm_width",height="$warm_height",pixelformat="$warm_fmt" \
			>/dev/null 2>&1
		rc_fmt=$?

		v4l2-ctl -d "$DEV" --set-parm="$warm_fps" >/dev/null 2>&1
		rc_parm=$?

		if [ "$rc_fmt" -eq 0 ] && [ "$rc_parm" -eq 0 ]; then
			stream_out=$(run_stream_capture "$WARMUP_COUNT" "$warm_fmt" "$warm_width" "$warm_height" "$warm_fps" 2>&1)
			rc_stream=$?
		else
			stream_out=""
			rc_stream=1
		fi

		if [ "$rc_fmt" -eq 0 ] && [ "$rc_parm" -eq 0 ] && [ "$rc_stream" -eq 0 ]; then
			echo "warmup: PASS attempt=$warm_attempt"
			return 0
		fi

		echo "warmup: FAIL attempt=$warm_attempt rc_fmt=$rc_fmt rc_parm=$rc_parm rc_stream=$rc_stream"
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

echo "device: $DEV"
echo "mode: $MODE"
echo "formats: $FORMATS"
echo "sizes: $SIZES"
echo "fps: $FPS_LIST"
if [ -n "$COUNT" ]; then
	echo "stream-count: $COUNT"
else
	echo "stream-count: auto (requested fps * 2)"
fi
echo "warmup: $WARMUP"
echo "warmup-combo: $WARMUP_FORMAT $WARMUP_SIZE ${WARMUP_FPS}fps"
echo "warmup-count: $WARMUP_COUNT"
echo "stream-retries: $STREAM_RETRIES"
echo "stream-timeout: $STREAM_TIMEOUT"
echo

if [ "$LIST_ONLY" -eq 1 ]; then
	v4l2-ctl -d "$DEV" --list-formats-ext
	exit $?
fi

if [ "$WARMUP" -eq 1 ]; then
	run_warmup
	echo
fi

printf '%-11s %-9s %-10s %-6s %-9s %-10s %-9s %-9s\n' \
	"RESULT" "REQ_FMT" "REQ_SIZE" "REQFPS" "ACT_FMT" "ACT_SIZE" "DRV_FPS" "ACT_FPS"

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
			result=PASS
			fmt_out=""
			get_fmt_out=""
			parm_out=""
			get_parm_out=""
			stream_out=""
			if [ -n "$COUNT" ]; then
				case_count=$COUNT
			else
				case_count=$(stream_count_for_fps "$fps")
				[ -n "$case_count" ] || case_count=1
			fi

			fmt_out=$(v4l2-ctl -d "$DEV" \
				--set-fmt-video=width="$width",height="$height",pixelformat="$fmt" 2>&1)
			rc_fmt=$?
			get_fmt_out=$(v4l2-ctl -d "$DEV" --get-fmt-video 2>&1)

			if [ "$rc_fmt" -ne 0 ]; then
				result=FAIL_FMT
			else
				parm_out=$(v4l2-ctl -d "$DEV" --set-parm="$fps" 2>&1)
				rc_parm=$?
				get_parm_out=$(v4l2-ctl -d "$DEV" --get-parm 2>&1)

				if [ "$rc_parm" -ne 0 ]; then
					result=FAIL_PARM
				else
					stream_attempt=0
					while :; do
						stream_attempt=$((stream_attempt + 1))
						current_stream_out=$(run_stream_capture "$case_count" "$fmt" "$width" "$height" "$fps" 2>&1)
						rc_stream=$?
						stream_out="${stream_out}
${current_stream_out}"
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

						sleep "$RETRY_SLEEP"
					done
				fi
			fi

			case_out=$(printf '%s\n%s\n%s\n%s\n%s\n' \
				"$fmt_out" "$get_fmt_out" "$parm_out" "$get_parm_out" "$stream_out")
			act_fmt=$(printf '%s\n' "$case_out" | sed -n "s/.*Pixel Format[[:space:]]*: '\([^']*\)'.*/\1/p" | tail -n 1)
			act_size=$(printf '%s\n' "$case_out" | sed -n 's/.*Width\/Height[[:space:]]*: \([0-9][0-9]*\)\/\([0-9][0-9]*\).*/\1x\2/p' | tail -n 1)
			drv_fps=$(printf '%s\n' "$case_out" | extract_driver_fps)
			act_fps=$(printf '%s\n' "$stream_out" | extract_stream_fps)

			[ -n "$act_fmt" ] || act_fmt="-"
			[ -n "$act_size" ] || act_size="-"
			[ -n "$drv_fps" ] || drv_fps="-"
			[ -n "$act_fps" ] || act_fps="-"

			if [ "$result" = "PASS" ] || [ "$result" = "PASS_RETRY" ]; then
				adjusted=0
				no_fps=0
				[ "$act_fmt" = "$fmt" ] || adjusted=1
				[ "$act_size" = "$size" ] || adjusted=1
				if [ "$act_fps" = "-" ]; then
					no_fps=1
					adjusted=1
				else
					fps_matches_request "$act_fps" "$fps" || adjusted=1
				fi
				if [ "$no_fps" -eq 1 ]; then
					result=PASS_NOFPS
				elif [ "$adjusted" -eq 1 ]; then
					result=PASS_ADJUST
				fi
				pass=$((pass + 1))
			else
				fail=$((fail + 1))
			fi

			printf '%-11s %-9s %-10s %-6s %-9s %-10s %-9s %-9s\n' \
				"$result" "$fmt" "$size" "$fps" "$act_fmt" "$act_size" "$drv_fps" "$act_fps"
		done
	done
done

echo
echo "summary: total=$total pass=$pass fail=$fail"

if [ "$fail" -ne 0 ]; then
	exit 1
fi

exit 0
