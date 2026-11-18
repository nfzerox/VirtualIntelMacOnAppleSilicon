#!/bin/zsh
# Cross-modifying code under Hypervisor.framework, as the Hybrid translator publishes it.
# Usage: run.sh <vcpus> <reuse slots, 0 = fresh> <consumer flags> <tso 0/1> <seconds> [host|none]
#   consumer flags: 1 = ISB after observing the pointer, 2 = consumer IC IVAU of the target
# Measured on Apple M3 (H15), 4 vCPUs, host sys_icache_invalidate before a store-release:
#   flags 0 reuse: ~98% of calls ran stale valid code; fresh: ~6 Unknown retries per call
#   flags 1: no stale fresh fetches; ISB on the executing vCPU is what makes new code current.
set -eu
here=${0:A:h}
out=$here/../../.build/images
mkdir -p "$out"
clang -O2 -Wall -Wextra -Wno-unguarded-availability-new -mmacosx-version-min=11.0 -march=armv8.5-a \
    -framework Hypervisor "$here/probe.c" "$here/guest.s" -o "$out/xmod-probe"
codesign --force --sign - --entitlements "$here/../../Engine.entitlements" "$out/xmod-probe"
exec "$out/xmod-probe" "$@"
