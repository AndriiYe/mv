#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$app_dir/../.." && pwd)"
venv_dir="$app_dir/.venv"
env_file="$app_dir/pi-web-updater.env"
runner="$app_dir/run-pi-web-updater.sh"
service_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
service_file="$service_dir/cv-web-updater.service"
desktop_file="${XDG_CONFIG_HOME:-$HOME/.config}/autostart/cv-web-updater.desktop"

if [ "${1:-}" = "--remove" ]; then
    systemctl --user disable --now cv-web-updater.service >/dev/null 2>&1 || true
    rm -f "$service_file"
    systemctl --user daemon-reload
    echo "Removed user service:"
    echo "  $service_file"
    exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required. Install it with: sudo apt install -y python3 python3-venv" >&2
    exit 1
fi

if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemctl is required for background service installation." >&2
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
mkdir -p "$service_dir"

cat > "$service_file" <<EOF
[Unit]
Description=CV Web Updater
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
WorkingDirectory=$app_dir
ExecStart=$runner
Restart=on-failure
RestartSec=3

[Install]
WantedBy=default.target
EOF

# Avoid running both the old desktop autostart and the user service.
rm -f "$desktop_file"

systemctl --user daemon-reload
systemctl --user enable --now cv-web-updater.service

echo "Installed and started user service:"
echo "  $service_file"
echo ""
echo "Status:"
echo "  systemctl --user status cv-web-updater.service"
echo ""
echo "Logs:"
echo "  journalctl --user -u cv-web-updater.service -f"
echo ""
echo "Open from another device on the same WiFi:"
echo "  http://raspberrypi.local:8080"
echo "  or http://<raspberry-pi-ip>:8080"
echo ""
echo "To keep this service running before login, run once:"
echo "  sudo loginctl enable-linger $USER"
echo ""
echo "To remove:"
echo "  bash $app_dir/install-pi-web-systemd.sh --remove"
