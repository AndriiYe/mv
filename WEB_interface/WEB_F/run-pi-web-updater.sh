#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$app_dir/../.." && pwd)"
env_file="$app_dir/pi-web-updater.env"
venv_python="$app_dir/.venv/bin/python"

if [ -f "$env_file" ]; then
    set -a
    # shellcheck disable=SC1090
    . "$env_file"
    set +a
fi

export CV_PROJECT_DIR="${CV_PROJECT_DIR:-$project_dir}"
export PI_WEB_HOST="${PI_WEB_HOST:-0.0.0.0}"
export PI_WEB_PORT="${PI_WEB_PORT:-8080}"

if [ ! -x "$venv_python" ]; then
    echo "Missing virtual environment Python: $venv_python" >&2
    echo "Run: bash $app_dir/install-pi-web-autostart.sh" >&2
    exit 1
fi

cd "$app_dir"
exec "$venv_python" "$app_dir/main.py"
