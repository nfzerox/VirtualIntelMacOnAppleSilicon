#!/bin/zsh
# Builds and runs the multi-vCPU cost probe. Usage: run.sh [locks|loads|tlbi|reach|patch]
set -eu
here=${0:A:h}
out=$here/../../.build/images
mkdir -p "$out"
clang -O2 -Wall -Wextra -march=armv8.5-a -framework Hypervisor "$here/probe.c" "$here/guest.s" -o "$out/smp-probe"
codesign --force --sign - --entitlements "$here/../../Engine.entitlements" "$out/smp-probe"
exec "$out/smp-probe" "$@"
