#!/bin/zsh
# Compile only the targeted shared-keeper SMP fixture. Never runs a guest.
# Use a fresh absolute scratch directory; keep production build inputs intact.
set -eu
here=${0:A:h}
out=${1:?a fresh absolute scratch output directory is required}
[[ "$out" == /* ]] || { print -u2 'Output must be an absolute scratch directory'; exit 2; }
# mkdir requires a fresh target; no existing build or user files are replaced.
mkdir "$out"
flags=(-target x86_64-apple-macos10.9 -march=x86-64 -O2 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -static
    -mno-implicit-float -fno-vectorize -fno-slp-vectorize -Wall -Wextra -Werror -Wno-unused-function)
xcrun clang "${flags[@]}" -c "$here/x86/kernel/keeper-smp.c" -o "$out/keeper-smp.o"
xcrun clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/smpstubs.s" -o "$out/smpstubs.o"
objects=("$out/keeper-smp.o" "$out/smpstubs.o")
if [[ -f "$here/x86/kernel/keeper-smp-user.s" ]]; then
    xcrun clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/keeper-smp-user.s" -o "$out/keeper-smp-user.o"
    objects+=("$out/keeper-smp-user.o")
fi
xcrun ld -arch x86_64 -preload -e _kstart -no_uuid -pagezero_size 0 \
    -segaddr __TEXT 0xFFFF800000100000 -segaddr __DATA 0xFFFF800000800000 \
    -o "$out/keeper-smp" "${objects[@]}"
print "Compiled only: $out/keeper-smp"
