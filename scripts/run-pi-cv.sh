#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-build}"
target="${TARGET:-cv}"
start_delay="${START_DELAY:-2}"
fullscreen="${CV_FULLSCREEN:-1}"
display_wait_seconds="${CV_DISPLAY_WAIT_SECONDS:-20}"
log_dir="${XDG_STATE_HOME:-$HOME/.local/state}/cv-tracker"
log_file="$log_dir/run.log"

mkdir -p "$log_dir"

prepare_display_environment() {
    if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
        export XDG_RUNTIME_DIR="/run/user/$(id -u)"
    fi

    if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ] && [ -S "$XDG_RUNTIME_DIR/bus" ]; then
        export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
    fi

    local deadline=$((SECONDS + display_wait_seconds))
    while true; do
        if [ -z "${WAYLAND_DISPLAY:-}" ]; then
            local wayland_socket
            for wayland_socket in "$XDG_RUNTIME_DIR"/wayland-*; do
                if [ -S "$wayland_socket" ]; then
                    export WAYLAND_DISPLAY="$(basename "$wayland_socket")"
                    break
                fi
            done
        fi

        if [ -z "${DISPLAY:-}" ] && [ -S /tmp/.X11-unix/X0 ]; then
            export DISPLAY=":0"
        fi

        if [ -z "${XAUTHORITY:-}" ] && [ -f "$HOME/.Xauthority" ]; then
            export XAUTHORITY="$HOME/.Xauthority"
        fi

        if [ -n "${WAYLAND_DISPLAY:-}" ] || [ -n "${DISPLAY:-}" ] || [ "$display_wait_seconds" = "0" ]; then
            return 0
        fi

        if [ "$SECONDS" -ge "$deadline" ]; then
            echo "Warning: no desktop display socket found after ${display_wait_seconds}s."
            echo "Start the Raspberry Pi desktop session, then press Start again."
            return 0
        fi

        sleep 1
    done
}

{
    echo ""
    echo "==== $(date -Is) starting $project_dir/$build_dir/$target ===="
    echo "CV_FULLSCREEN=$fullscreen"
    echo "CV_DISPLAY_WAIT_SECONDS=$display_wait_seconds"
    cd "$project_dir"

    if [ ! -x "./$build_dir/$target" ]; then
        echo "Error: ./$build_dir/$target does not exist or is not executable."
        echo "Build first with: cmake -S . -B $build_dir -G Ninja && cmake --build $build_dir"
        exit 127
    fi

    sleep "$start_delay"
    prepare_display_environment
    echo "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}"
    echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-}"
    echo "DISPLAY=${DISPLAY:-}"
    echo "XAUTHORITY=${XAUTHORITY:-}"
    export CV_FULLSCREEN="$fullscreen"
    exec "./$build_dir/$target"
} >> "$log_file" 2>&1
