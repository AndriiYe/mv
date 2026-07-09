# CV Web Updater

Tiny Bottle-based maintenance UI for the Raspberry Pi copy of this OpenCV
tracker project.

It can:

- show tracker, Git, build, and config status
- stop/start/restart the tracker
- pull the current Git branch and rebuild with CMake
- update and restart in one button press
- view, edit, upload, download, and validate `config.json`

## Raspberry Pi Setup

From the project checkout on the Raspberry Pi:

```bash
cd ~/mv
bash WEB_interface/WEB_F/install-pi-web-autostart.sh
```

The installer creates a Python virtual environment, installs Bottle, creates
`WEB_interface/WEB_F/pi-web-updater.env`, and registers a desktop autostart
entry.

Set a password before using the UI on a shared network:

```bash
nano WEB_interface/WEB_F/pi-web-updater.env
```

Uncomment and change:

```bash
PI_WEB_PASSWORD=change-this-password
```

Start it immediately without rebooting:

```bash
WEB_interface/WEB_F/run-pi-web-updater.sh
```

Then open:

```text
http://raspberrypi.local:8080
```

or:

```text
http://<raspberry-pi-ip>:8080
```

## Update Flow

1. Edit code on the PC.
2. Commit and push to GitHub.
3. Open the Pi web UI.
4. Press `Update + Restart`.

The Pi runs:

```bash
git fetch origin
git pull --ff-only
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Then it starts `scripts/run-pi-cv.sh`.

`config.json` is ignored by Git in this project, so Pi-local settings are not
overwritten by normal code updates.

## Configuration

The web UI edits:

```text
~/mv/config.json
```

Before saving, it validates JSON and creates a timestamped backup next to the
file, for example:

```text
config.json.20260709-141500.bak
```

## Environment Options

Edit `WEB_interface/WEB_F/pi-web-updater.env` on the Pi.

Common settings:

```bash
PI_WEB_HOST=0.0.0.0
PI_WEB_PORT=8080
PI_WEB_USERNAME=pi
PI_WEB_PASSWORD=your-password

CV_PROJECT_DIR=/home/pi/mv
CV_BUILD_DIR=build
CV_TARGET=cv
CV_RUNNER=/home/pi/mv/scripts/run-pi-cv.sh
CV_CMAKE_BUILD_TYPE=Release
```

If the tracker is later managed by systemd, use:

```bash
CV_TRACKER_SERVICE=cv-tracker.service
CV_TRACKER_SERVICE_SCOPE=user
```

## Notes

This project currently opens an OpenCV display window. For that reason the web
updater is installed as desktop autostart, so commands launched from the UI can
inherit the graphical login session.

Keep this UI on a trusted local network only. It intentionally executes update
and process-control commands for the Pi project.
