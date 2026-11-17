#!/bin/zsh
# Compile ONLY this freestanding fixture to an explicit isolated scratch directory. Never runs it.
# Experimental runtime: ENGINE_SHARE=1 ENGINE_STATS=1 LC_HVDBT_KEEPER_INACTIVE_GUARD=1 engine-run --kernel <out>/keeper-inactive
# Require functional exit0 AND host sharedDirs>0/suppressed>0/thresholdFallbacks>0. Repeat with ENGINE_POOL_PAGES=128;
# require pool-reclaims>0 for pressure coverage. This is one-vCPU targeted coverage, not an SMP qualification.
set -eu
here=${0:A:h}
out=${1:?an isolated absolute scratch output directory is required}
[[ "$out" == /* ]] || { print -u2 'Output must be an absolute scratch directory'; exit 2; }
mkdir "$out"
flags=(-target x86_64-apple-macos10.9 -march=x86-64 -O2 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -static
    -mno-implicit-float -fno-vectorize -fno-slp-vectorize -Wno-unused-function)
clang "${flags[@]}" -c "$here/x86/kernel/keeper-inactive.c" -o "$out/keeper-inactive.o"
clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/kstubs.s" -o "$out/kstubs.o"
clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/kentry.s" -o "$out/kentry.o"
ld -arch x86_64 -preload -e _kstart -no_uuid -pagezero_size 0 \
    -segaddr __TEXT 0xFFFF800000100000 -segaddr __DATA 0xFFFF800000800000 \
    -o "$out/keeper-inactive" "$out/kentry.o" "$out/keeper-inactive.o" "$out/kstubs.o"
print "Compiled only: $out/keeper-inactive"
