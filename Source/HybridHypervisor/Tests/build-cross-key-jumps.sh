#!/bin/zsh
# Compile only the focused cross-key fixture, never boot an OS or run the image.
# ENGINE_SHARE=1 ENGINE_STATS=1 engine-run --kernel OUTPUT/cross-key-jumps
set -euo pipefail
here=${0:A:h}
out=${1:?an absolute isolated scratch output directory is required}
fixture=${2:-cross-key-jumps}
[[ "$out" == /* && "$out" != / ]] || { print -u2 'Use an absolute scratch directory'; exit 64; }
[[ "$fixture" != */* && -f "$here/x86/kernel/$fixture.c" ]] || { print -u2 'Use a kernel fixture source name'; exit 64; }
mkdir -p "$out"
flags=(-target x86_64-apple-macos10.9 -O2 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -static
    -mno-implicit-float -fno-vectorize -fno-slp-vectorize -Wall -Wextra -Werror -Wno-unused-function)
xcrun clang "${flags[@]}" -c "$here/x86/kernel/$fixture.c" -o "$out/$fixture.o"
for name in kentry kstubs; do
    xcrun clang -target x86_64-apple-macos10.9 -c "$here/x86/kernel/$name.s" -o "$out/$name.o"
done
xcrun ld -arch x86_64 -preload -e _kstart -no_uuid -pagezero_size 0 \
    -segaddr __TEXT 0xFFFF800000100000 -segaddr __DATA 0xFFFF800000800000 \
    -o "$out/$fixture" "$out/kentry.o" "$out/$fixture.o" "$out/kstubs.o"
print "Compiled only: $out/$fixture"
