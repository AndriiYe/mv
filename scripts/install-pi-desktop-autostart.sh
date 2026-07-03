#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app_id="${APP_ID:-cv-tracker}"
desktop_dir="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
desktop_file="$desktop_dir/$app_id.desktop"
runner="$project_dir/scripts/run-pi-cv.sh"
build_dir="${BUILD_DIR:-build}"
target="${TARGET:-cv}"

if [ "${1:-}" = "--remove" ]; then
    rm -f "$desktop_file"
    echo "Removed $desktop_file"
    exit 0
fi

if [ ! -f "$runner" ]; then
    echo "Error: runner script not found: $runner" >&2
    exit 1
fi

if [ ! -x "$project_dir/$build_dir/$target" ]; then
    echo "Error: $project_dir/$build_dir/$target does not exist or is not executable." >&2
    echo "Build first with:" >&2
    echo "  cmake -S . -B $build_dir -G Ninja" >&2
    echo "  cmake --build $build_dir" >&2
    exit 1
fi

chmod +x "$runner"
mkdir -p "$desktop_dir"

cat > "$desktop_file" <<EOF
[Desktop Entry]
Type=Application
Name=OpenCV CRSF Tracker
Comment=Start the OpenCV tracker after the Raspberry Pi desktop logs in
Exec=$runner
Path=$project_dir
Terminal=false
X-GNOME-Autostart-enabled=true
EOF

echo "Installed desktop autostart:"
echo "  $desktop_file"
echo ""
echo "The tracker will start after the Raspberry Pi desktop logs in."
echo "Logs:"
echo "  tail -f \"\${XDG_STATE_HOME:-\$HOME/.local/state}/cv-tracker/run.log\""
echo ""
echo "To remove autostart:"
echo "  $0 --remove"
