# CV Web Updater

Tiny Bottle-based maintenance UI for the Raspberry Pi copy of this OpenCV
tracker project.

It can:

- stop/start the tracker
- rebuild with CMake
- view, edit, upload, download, and validate `config.json`

## Raspberry Pi Setup

From the project checkout on the Raspberry Pi:

```bash
cd ~/mv
bash WEB_interface/WEB_F/install-pi-web-systemd.sh
```

The installer creates a Python virtual environment, installs Bottle, creates
`WEB_interface/WEB_F/pi-web-updater.env`, and registers `cv-web-updater.service`
as a user systemd service.

Set a password before using the UI on a shared network:

```bash
nano WEB_interface/WEB_F/pi-web-updater.env
```

Uncomment and change:

```bash
PI_WEB_PASSWORD=change-this-password
```

Restart after changing the password:

```bash
systemctl --user restart cv-web-updater.service
```

Then open:

```text
http://raspberrypi.local:8080
```

or:

```text
http://<raspberry-pi-ip>:8080
```

To keep the web updater running before desktop login, run once:

```bash
sudo loginctl enable-linger $USER
```

Useful service commands:

```bash
systemctl --user status cv-web-updater.service
journalctl --user -u cv-web-updater.service -f
systemctl --user restart cv-web-updater.service
```

## Update Flow

1. Edit code on the PC.
2. Commit and push to GitHub.
3. Pull the new code on the Pi.
4. Open the Pi web UI.
5. Press `Build`, then `Start`.

The `Build` button runs:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The `Start` button starts `scripts/run-pi-cv.sh`.

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

This project currently opens an OpenCV display window, so the tracker itself can
still use desktop autostart. The web updater does not need the display and is
better as a background user service.

Keep this UI on a trusted local network only. It intentionally executes update
and process-control commands for the Pi project.
