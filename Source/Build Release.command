#!/bin/zsh
# Finder-friendly entry point. Source and caches are isolated automatically.
set -euo pipefail
script_dir=${0:A:h}
exec "$script_dir/Build.command" --fresh-release "$@"
