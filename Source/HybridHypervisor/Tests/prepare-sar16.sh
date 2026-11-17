#!/bin/zsh
# Compile only the narrow SAR16 fixture and existing kernel with fault-frame checks; never execute Engine or a guest.
# Root runs: engine-run OUT/t_sar16 1; arch -x86_64 flatrun OUT/t_sar16 0; engine-run --kernel OUT/kernel
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
xcrun clang "${flags[@]}" -c "$here/x86/t_sar16.c" -o "$out/t_sar16.o"
xcrun clang "${flags[@]}" -c "$here/x86/flatlibc.c" -o "$out/flatlibc.o"
xcrun ld -arch x86_64 -preload -e _flat_start -no_uuid -pagezero_size 0 \
    -segaddr __TEXT 0x100000000000 -segaddr __DATA 0x100010000000 \
    -o "$out/t_sar16" "$out/t_sar16.o" "$out/flatlibc.o"
kflags=(-target x86_64-apple-macos10.9 -march=x86-64 -O2 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -static -mno-implicit-float
    -fno-vectorize -fno-slp-vectorize -w)
xcrun clang "${kflags[@]}" -c "$here/x86/kernel/kernel.c" -o "$out/kernel.o"
xcrun clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/kstubs.s" -o "$out/kstubs.o"
xcrun clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/kentry.s" -o "$out/kentry.o"
xcrun ld -arch x86_64 -preload -e _kstart -no_uuid -pagezero_size 0 \
    -segaddr __TEXT 0xFFFF800000100000 -segaddr __DATA 0xFFFF800000800000 \
    -o "$out/kernel" "$out/kentry.o" "$out/kernel.o" "$out/kstubs.o"
print 'Prepared SAR16 fixtures only; no runtime checks performed'
