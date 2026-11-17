#!/bin/zsh
# Usage: run.sh <image> [a0 a1]. Runs one flat image under the engine with a hard time limit. The tool arms alarm(29) itself.
here=${0:A:h}
exec "$here/../.build/release/engine-run" "$here/../.build/images/$1" "${@:2}"
