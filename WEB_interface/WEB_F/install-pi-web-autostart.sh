#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$app_dir/../.." && pwd)"
venv_dir="$app_dir/.venv"
env_file="$app_dir/pi-web-updater.env"
desktop_dir="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
desktop_file="$desktop_dir/cv-web-updater.desktop"
runner="$app_dir/run-pi-web-updater.sh"
log_dir="${XDG_STATE_HOME:-$HOME/.local/state}/cv-web-updater"
log_file="$log_dir/web.log"

if [ "${1:-}" = "--remove" ]; then
    rm -f "$desktop_file"
    echo "Removed $desktop_file"
    exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required. Install it with: sudo apt install -y python3 python3-venv" >&2
    exit 1
fi

if [ ! -d "$venv_dir" ]; then
    python3 -m venv "$venv_dir"
fi

"$venv_dir/bin/python" -m pip install --upgrade pip
"$venv_dir/bin/python" -m pip install -r "$app_dir/requirements.txt"

if [ ! -f "$env_file" ]; then
    cp "$app_dir/pi-web-updater.env.example" "$env_file"
    sed -i "s|^CV_PROJECT_DIR=.*|CV_PROJECT_DIR=$project_dir|" "$env_file"
    sed -i "s|^CV_RUNNER=.*|CV_RUNNER=$project_dir/scripts/run-pi-cv.sh|" "$env_file"
    echo "Created $env_file"
    echo "Edit PI_WEB_PASSWORD in that file before using this outside a trusted LAN."
fi

chmod +x "$runner"
mkdir -p "$desktop_dir" "$log_dir"

cat > "$desktop_file" <<EOF
[Desktop Entry]
Type=Application
Name=CV Web Updater
Comment=Start the OpenCV tracker maintenance web UI
Exec=/bin/sh -c '"$runner" >> "$log_file" 2>&1'
Path=$app_dir
Terminal=false
X-GNOME-Autostart-enabled=true
EOF

echo "Installed desktop autostart:"
echo "  $desktop_file"
echo ""
echo "Run it now with:"
echo "  $runner"
echo ""
echo "Open from another device on the same WiFi:"
echo "  http://raspberrypi.local:8080"
echo "  or http://<raspberry-pi-ip>:8080"
echo ""
echo "Logs:"
echo "  tail -f $log_file"
echo ""
echo "To remove autostart:"
echo "  bash $app_dir/install-pi-web-autostart.sh --remove"
