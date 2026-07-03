# Raspberry Pi Startup

The current app opens an OpenCV display window with `namedWindow`, `imshow`, and
`waitKeyEx`. For that reason, the safest startup method is Raspberry Pi desktop
autostart: the program starts after the graphical desktop logs in.

A plain boot-time systemd service is not recommended for the current build
because it can start before a display session exists and fail with display
connection errors.

## Install Desktop Autostart

On the Raspberry Pi, from the project checkout:

```bash
cd ~/mv
cmake -S . -B build -G Ninja
cmake --build build
bash scripts/install-pi-desktop-autostart.sh
```

Reboot and let the Pi desktop log in:

```bash
sudo reboot
```

The runner starts `./build/cv` from the project directory, so `config.json` can
stay in the project root next to the build directory.

To start the OpenCV window fullscreen, set this in `config.json`:

```json
"display": {
  "fullscreen": true
}
```

## Enable Desktop Autologin

Desktop autostart runs after login. If the Pi should start the tracker without
manual login, enable desktop autologin:

```bash
sudo raspi-config
```

Choose `System Options`, then `Boot / Auto Login`, then a desktop autologin
option for the `pi` user.

## Logs

The startup runner writes stdout and stderr here:

```bash
tail -f ~/.local/state/cv-tracker/run.log
```

If the app does not appear after reboot, check that log first. Common causes are
an unbuilt `build/cv`, a missing `config.json`, a camera pipeline error, or CRSF
serial permissions.

## Remove Autostart

From the project checkout:

```bash
bash scripts/install-pi-desktop-autostart.sh --remove
```

## Headless Boot Later

If the app later gets a no-window/headless mode, it can be moved to a boot-time
systemd service. Until then, desktop autostart matches the app's current display
requirements.
