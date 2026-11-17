#!/bin/zsh
# QEMU's test-i386 transcript under Rosetta versus the engine. 16-bit shld and shrd with a count above 16 are undefined in the SDM,
# and Rosetta's CF differs from ours there, so those lines drop out. test_enter is skipped (a0=512): Rosetta raises SIGILL on enter.
# Every other line must match.
here=${0:A:h}
images=$here/../.build/images
tmp=${TMPDIR:-/tmp}/engine-i386.$$
mkdir -p "$tmp"
keep() { python3 "$here/filter-i386.py" }
arch -x86_64 "$images/flatrun" "$images/t_i386" 512 2>&1 | keep > "$tmp/r"
"$here/../.build/release/engine-run" "$images/t_i386" 512 2>&1 | keep > "$tmp/e"
echo "lines: rosetta $(wc -l < "$tmp/r") engine $(wc -l < "$tmp/e")"
diff "$tmp/r" "$tmp/e" | head -${1:-30} | cut -c1-120
echo "differences: $(diff "$tmp/r" "$tmp/e" | grep -c '^>')"
rm -rf "$tmp"
