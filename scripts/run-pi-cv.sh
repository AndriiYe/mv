#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-build}"
target="${TARGET:-cv}"
start_delay="${START_DELAY:-2}"
log_dir="${XDG_STATE_HOME:-$HOME/.local/state}/cv-tracker"
log_file="$log_dir/run.log"

mkdir -p "$log_dir"

{
    echo ""
    echo "==== $(date -Is) starting $project_dir/$build_dir/$target ===="
    cd "$project_dir"

    if [ ! -x "./$build_dir/$target" ]; then
        echo "Error: ./$build_dir/$target does not exist or is not executable."
        echo "Build first with: cmake -S . -B $build_dir -G Ninja && cmake --build $build_dir"
        exit 127
    fi

    sleep "$start_delay"
    exec "./$build_dir/$target"
} >> "$log_file" 2>&1
