#!/bin/zsh
# Compile only; no Engine build/execution. Root runs engine-run OUT/t_cl_self_count 1 and arch -x86_64 flatrun OUT/t_cl_self_count 0.
set -eu
here=${0:A:h}
out=${1:?new output directory}
if [[ -e "$out" ]]; then
    print -u2 'Output directory must be new'
    exit 64
fi
mkdir -p "$out"
flags=(-target x86_64-apple-macos10.9 -march=x86-64 -O2 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions -mno-red-zone -I"$here/x86"
    -Wno-incompatible-library-redeclaration -Wno-format)
xcrun clang "${flags[@]}" -c "$here/x86/t_cl_self_count.c" -o "$out/t_cl_self_count.o"
xcrun clang "${flags[@]}" -c "$here/x86/flatlibc.c" -o "$out/flatlibc.o"
xcrun ld -arch x86_64 -preload -e _flat_start -no_uuid -pagezero_size 0 \
    -segaddr __TEXT 0x100000000000 -segaddr __DATA 0x100010000000 \
    -o "$out/t_cl_self_count" "$out/t_cl_self_count.o" "$out/flatlibc.o"
print 'Prepared CL self-count fixture only; no runtime checks performed'
