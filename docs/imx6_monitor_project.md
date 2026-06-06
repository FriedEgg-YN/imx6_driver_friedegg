# i.MX6ULL Web Monitoring Node

## Goal

This project turns the existing AP3216C and OV5640 driver work into a small
monitoring node:

- OV5640 is captured through the Linux V4L2 streaming API.
- The latest RGB565 frame is optionally copied to `/dev/fb0` for local LCD
  preview.
- The latest frame is JPEG encoded and served as `/snapshot.jpg` and
  `/stream.mjpg`.
- AP3216C values are exposed in `/api/status`.
- Buildroot installs the service as `/usr/bin/imx6-monitor` and starts it with
  `/etc/init.d/S90imx6-monitor`.

## Why This Technical Stack

MJPEG over HTTP is the first implementation target because it is easy to open
from a Windows browser, easy to debug with `curl`, and does not require a heavy
RTSP/WebRTC stack on the i.MX6ULL. RTSP/H.264 remains a useful future direction,
but it would shift the work toward multimedia framework integration and encoder
performance.

The service uses V4L2 MMAP directly instead of GStreamer so the project keeps
the Linux-driver learning value visible: `VIDIOC_QUERYCAP`, `VIDIOC_S_FMT`,
`VIDIOC_REQBUFS`, `VIDIOC_QBUF`, `VIDIOC_DQBUF`, and `VIDIOC_STREAMON` are all
part of the code path. The LCD preview stays on fbdev because the current board
already validates that path, and it is a practical fit for this BSP.

JPEG encoding uses the Buildroot `jpeg` virtual package with `jpeg-turbo` as the
selected provider. Encoding is not the core learning target, so the project uses
a mature library and keeps the engineering attention on the capture path,
device-node integration, and board validation.

The HTTP server is a small C socket server. For the current endpoints
(`/`, `/snapshot.jpg`, `/stream.mjpg`, `/api/status`) this avoids bringing in a
large web framework while still demonstrating embedded service design.

## Build and Deploy

Reconfigure Buildroot once after enabling the new package:

```sh
bash bsp/build_and_deploy.sh config reset buildroot
```

Build and deploy the rootfs:

```sh
bash bsp/build_and_deploy.sh rootfs
```

The service starts automatically on the next NFS-root boot. For manual testing:

```sh
/etc/init.d/S90imx6-monitor stop
imx6-monitor -d /dev/video0 -f /dev/fb0 -s /dev/ap3216c -p 8080 -W 320 -H 240 -r 10
```

From Windows, open:

```text
http://<board-ip>:8080/
http://<board-ip>:8080/snapshot.jpg
http://<board-ip>:8080/stream.mjpg
http://<board-ip>:8080/api/status
```

## Board Validation Checklist

```sh
dmesg | grep -i -E 'ov5640|csi|ap3216c|video'
ls -l /dev/video* /dev/fb0 /dev/ap3216c
cat /proc/modules | grep -E 'ov5640|mx6s_capture|ap3216c'
wget -O - http://127.0.0.1:8080/api/status
wget -O /tmp/snapshot.jpg http://127.0.0.1:8080/snapshot.jpg
```

For stability testing, keep the service running for at least 30 minutes and
record CPU usage, frame rate target, browser reconnect behavior, and whether the
LCD preview remains smooth.
