# Raspberry Pi 5 + Camera Module 3 Port

This project can run on Raspberry Pi 5 with Camera Module 3 by keeping
`LocalTracker` independent from camera capture and opening the camera through a
Raspberry Pi `libcamera`/GStreamer source.

## Raspberry Pi OS Setup

Use a current Raspberry Pi OS release. Camera Module 3 uses the modern
`libcamera` camera stack; the old legacy camera stack is not supported for newer
camera modules.

Install the C++ build and OpenCV dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config
sudo apt install -y libopencv-dev
sudo apt install -y gstreamer1.0-tools gstreamer1.0-libcamera
```

Check that the camera is visible to Raspberry Pi camera tools:

```bash
rpicam-hello --list-cameras
rpicam-hello --timeout 5000
```

If these commands fail, fix the camera connection/software stack before testing
OpenCV. The tracker depends on frames arriving, but it does not configure the
sensor directly.

## Build On Raspberry Pi

From the project root:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

`CMakeLists.txt` only sets the Windows `OpenCV_DIR` on Windows. On Raspberry Pi,
`find_package(OpenCV REQUIRED)` should find the OpenCV package installed by
`libopencv-dev`.

## Run With Camera Module 3

Set `capture.mode` to `pi-camera` in `config.json`:

```json
{
  "capture": {
    "mode": "pi-camera",
    "source": "/home/pi/test.mp4",
    "camera_index": 0,
    "width": 640,
    "height": 480,
    "fps": 30,
    "screen_left": 0,
    "screen_top": 0,
    "screen_width": 640,
    "screen_height": 480
  },
  "crsf": {
    "device": "",
    "baudrate": 420000
  }
}
```

Then run the app without runtime command-line options:

```bash
./build/cv
```

To start the same Pi build automatically after the Raspberry Pi desktop logs in,
see `docs/raspberry_pi_startup.md`.

The program opens this GStreamer pipeline internally:

```text
libcamerasrc ! video/x-raw,width=640,height=480,framerate=30/1,format=NV12 ! videoconvert ! video/x-raw,format=BGR ! appsink name=appsink drop=true max-buffers=1 sync=false
```

You can reduce CPU load by lowering the resolution or FPS:

```json
"width": 320,
"height": 240,
"fps": 30
```

## Other Inputs

Use a video file:

```json
"mode": "video",
"source": "/home/pi/test.mp4"
```

Use a normal V4L2-style camera index if available:

```json
"mode": "camera",
"camera_index": 0
```

## Troubleshooting

- If `capture.mode` is `pi-camera` and the camera fails, run
  `gst-inspect-1.0 libcamerasrc`.
- If `libcamerasrc` is missing, install `gstreamer1.0-libcamera`.
- If OpenCV cannot open the pipeline, confirm your OpenCV build has GStreamer
  enabled.
- If you see `qt.qpa.xcb: could not connect to display`, you are probably
  running from SSH without a forwarded or local display. Start the app from the
  Raspberry Pi desktop session, or use a working X11/Wayland forwarding setup.
- If tracking is slow, start with `320x240` or `640x480` instead of full sensor
  resolution.
- If the camera is not detected, validate with `rpicam-hello --list-cameras`
  first.
