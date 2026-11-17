#!/bin/zsh
# Compile only the same-value PTE SMP fixture. Never runs a guest.
# Add --atomics after OUTPUT to test32/64-bit CAS/exchange/LSE on tracked PTEs.
set -eu
here=${0:A:h}
out=${1:?a fresh absolute scratch output directory is required}
variant=${2:-}
[[ -z "$variant" || "$variant" == --atomics ]] || exit 64
case "${out:h}" in /private/tmp|/tmp) ;; *) print -u2 'Output must be directly inside /private/tmp or /tmp'; exit 2 ;; esac
case "${out:t}" in ''|.|..) print -u2 'Invalid scratch directory name'; exit 2 ;; esac
mkdir "$out"
flags=(-target x86_64-apple-macos10.9 -march=x86-64 -O2 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -static
    -mno-implicit-float -fno-vectorize -fno-slp-vectorize -Wall -Wextra -Werror -Wno-unused-function)
[[ -z "$variant" ]] || flags+=(-DPTE_ATOMIC_FORMS=1)
xcrun clang "${flags[@]}" -c "$here/x86/kernel/pte-same64-smp.c" -o "$out/pte-same64-smp.o"
xcrun clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/smpstubs.s" -o "$out/smpstubs.o"
xcrun clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/pte-same64-user.s" -o "$out/pte-same64-user.o"
xcrun ld -arch x86_64 -preload -e _kstart -no_uuid -pagezero_size 0 \
    -segaddr __TEXT 0xFFFF800000100000 -segaddr __DATA 0xFFFF800000800000 \
    -o "$out/pte-same64-smp" "$out/pte-same64-smp.o" "$out/smpstubs.o" "$out/pte-same64-user.o"
print "Compiled only: $out/pte-same64-smp"
