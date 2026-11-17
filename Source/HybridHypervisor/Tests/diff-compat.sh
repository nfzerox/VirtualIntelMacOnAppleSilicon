#!/bin/zsh
# 32-bit code on the engine against QEMU's TCG, the reference for 32-bit since Rosetta on this macOS doesn't run i386. The compat kernel, its fuzz forms and test-i386 run in
# a ring-3 compatibility segment under both. Boots nothing but that image: no disk, no firmware. Usage: QEMU=<qemu-system-x86_64> diff-compat.sh [lines of diff to show]
# Without QEMU, it finds a copy in Source/Build. With --update, writes the transcript's hash for run-all.sh to check.
# QEMU_HYBRID=1 compares real QEMU+engine+TCG hand-offs against plain TCG,
# including the supervisor based-ES copy window that standalone Engine skips.
here=${0:A:h}
images=$here/../.build/images
engine=$here/../.build/release/engine-run
compat=$here/x86/compat
tmp=$(mktemp -d "${TMPDIR:-/tmp}/engine-compat.XXXXXX") || exit 2
qemu_source=${VIM_QEMU_SOURCE:-$here/../../vendor/qemu}
qemu=${QEMU:-$(ls -t "$qemu_source"/../../Build/AppRuntime-*/qemu-system-x86_64(N) | head -1)}
[[ -x $qemu ]] || { echo "no qemu-system-x86_64: set QEMU"; exit 2; }

clang -target i386-unknown-none-elf -c "$compat/boot.s" -o "$tmp/boot.o" || exit 2
python3 "$compat/link32.py" 0xF0000 "$tmp/boot.bin" "$tmp/boot.o" || exit 2
python3 "$compat/mkqemu.py" "$images/compat" "$tmp/boot.bin" "$tmp/rom.bin" "$tmp/ram.bin" 0x80000000 || exit 2

keep() { python3 "$compat/transcript.py" "$compat/tcg-known.txt" }
# TCG's sysenter is #UD in long mode unless the vendor is Intel's, as XNU's guests have it.
LC_HVDBT=0 timeout 600 "$qemu" -display none -no-user-config -nodefaults -M pc -accel tcg,thread=multi -cpu max,vendor=GenuineIntel -smp 1 -m 2G -bios "$tmp/rom.bin" \
    -device loader,file="$tmp/ram.bin",addr=0x100000,force-raw=on \
    -chardev file,id=con,path="$tmp/tcg.raw" -device isa-debugcon,iobase=0xe9,chardev=con \
    -device isa-debug-exit,iobase=0xf4,iosize=4 </dev/null >"$tmp/qemu.out" 2>&1
tcg_exit=$?
keep <"$tmp/tcg.raw" >"$tmp/tcg"
expected_engine_exit=0
hybrid_active=1
if [[ ${QEMU_HYBRID:-0} == 1 ]]; then
    LC_HVDBT=1 timeout 600 "$qemu" -display none -no-user-config -nodefaults -M pc -accel tcg,thread=multi -cpu max,vendor=GenuineIntel -smp 1 -m 2G -bios "$tmp/rom.bin" \
        -device loader,file="$tmp/ram.bin",addr=0x100000,force-raw=on \
        -chardev file,id=con,path="$tmp/hybrid.raw" -device isa-debugcon,iobase=0xe9,chardev=con \
        -device isa-debug-exit,iobase=0xf4,iosize=4 </dev/null >"$tmp/engine.err" 2>&1
    engine_exit=$?
    expected_engine_exit=1
    if ! grep -q 'hvdbt: the engine takes over' "$tmp/engine.err" || grep -q 'staying on TCG' "$tmp/engine.err"; then
        hybrid_active=0
        echo 'FAIL: requested hybrid never became active; a TCG fallback is not engine qualification'
    fi
    keep <"$tmp/hybrid.raw" >"$tmp/engine"
else
    ENGINE_TZCNT=1 ENGINE_ALARM=${ENGINE_ALARM:-120} "$engine" --kernel "$images/compat" </dev/null 2>"$tmp/engine.err" | keep >"$tmp/engine"
    engine_exit=$pipestatus[1]
fi
echo "lines: tcg $(wc -l <"$tmp/tcg") engine $(wc -l <"$tmp/engine"); exits: tcg $tcg_exit (1 is 0 through isa-debug-exit) engine $engine_exit"
[[ -s $tmp/qemu.out ]] && head -5 "$tmp/qemu.out"
[[ -s $tmp/engine.err ]] && head -5 "$tmp/engine.err" | cut -c1-300
n=${1:-30}
[[ $n == --update ]] && n=0
diff "$tmp/tcg" "$tmp/engine" | head -n "$n" | cut -c1-160
differences=$(diff "$tmp/tcg" "$tmp/engine" | grep -c '^>')
echo "differences: $differences"
if [[ $1 == --update && $differences == 0 && $tcg_exit == 1 && $engine_exit == $expected_engine_exit && $hybrid_active == 1 ]]; then
    shasum "$tmp/tcg" | cut -d' ' -f1 >"$compat/tcg-transcript.sha"
    echo "wrote $compat/tcg-transcript.sha"
fi
[[ -n $COMPAT_KEEP ]] && { cp "$tmp/tcg" "$COMPAT_KEEP.tcg"; cp "$tmp/engine" "$COMPAT_KEEP.engine"; }
rm -rf "$tmp"
[[ $differences == 0 && $tcg_exit == 1 && $engine_exit == $expected_engine_exit && $hybrid_active == 1 ]]
