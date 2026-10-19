#!/bin/zsh
# Finder-friendly entry point; build-app selects a supported Python toolchain.
set -euo pipefail
script_dir=${0:A:h}
exec python3 "$script_dir/scripts/build-app" "$@"
