#!/bin/zsh
# The engine's regression suite: sanity, the fuzz and test-i386 transcripts against Rosetta, SMC, the freestanding kernel.
here=${0:A:h}
images=$here/../.build/images
engine=$here/../.build/release/engine-run
fail=0

python3 "$here/test-flag-decode-reuse.py" && echo "pass decoded-input flag analysis" || {
    echo "FAIL decoded-input flag analysis"
    fail=1
}

policy=$(mktemp -d "${TMPDIR:-/tmp}/engine-segment-policy.XXXXXX") || exit 1
if clang -O2 -Wall -Wextra -Werror "$here/compat-segment-policy.c" -o "$policy/check" && "$policy/check"; then
    echo "pass supervisor/user segment policies"
else
    echo "FAIL supervisor/user segment policies"
    fail=1
fi
if xcrun swiftc -O -target arm64-apple-macosx11.0 \
    "$here/../Sources/Engine/TranslationLock.swift" \
    "$here/translation-lock.swift" -o "$policy/translation-lock" && "$policy/translation-lock"; then
    echo "pass host translator lock"
else
    echo "FAIL host translator lock"
    fail=1
fi
rm -f "$policy/check"
rm -f "$policy/translation-lock"
if clang -O2 -Wall -Wextra -Werror -I"$here/../Sources/CEngine/include" \
    "$here/../Sources/CEngine/latency.c" "$here/latency-ring.c" -o "$policy/latency-ring" && "$policy/latency-ring"; then
    echo "pass bounded host latency recorder"
else
    echo "FAIL bounded host latency recorder"
    fail=1
fi
rm -f "$policy/latency-ring"
if clang -O2 -Wall -Wextra -Werror -I"$here/../Sources/CEngine/include" \
    "$here/x87-helper-census.c" -o "$policy/x87-census" && "$policy/x87-census"; then
    echo "pass disabled/opt-in x87 helper census"
else
    echo "FAIL disabled/opt-in x87 helper census"
    fail=1
fi
rm -f "$policy/x87-census"
if clang -O2 -Wall -Wextra -Werror -I"$here/../Sources/CEngine/include" \
    "$here/note-rescan-profile.c" -o "$policy/note-rescans" && "$policy/note-rescans"; then
    echo "pass bounded note rescan recorder"
else
    echo "FAIL bounded note rescan recorder"
    fail=1
fi
rm -f "$policy/note-rescans"
rmdir "$policy"

check_same() { # name image [args]
    local name=$1 image=$2
    shift 2
    if [[ "$(arch -x86_64 "$images/flatrun" "$images/$image" "$@" 2>&1 | grep -v '^time')" == "$("$engine" "$images/$image" "$@" 2>&1 | grep -v '^time')" ]]; then
        echo "pass $name"
    else
        echo "FAIL $name"
        fail=1
    fi
}

check_same sanity t_sanity
check_same smc t_smc
check_same rep t_rep
check_same x87 t_x87
check_same x87-loads t_x87_loads
"$engine" "$images/t_x87_loads" 1 || fail=1
check_same shr16 t_shr16
"$engine" "$images/t_shr16" 1 || fail=1
"$engine" "$images/t_sse_scalar_lanes" || fail=1
check_same corpus-O0 t_corpus_O0
check_same corpus-O1 t_corpus_O1
check_same corpus-O2 t_corpus_O2
check_same corpus-O3 t_corpus_O3
check_same corpus-Os t_corpus_Os
check_same corpus-ssse3 t_corpus_ssse3
check_same fuzz fuzz
ENGINE_FAST_FLAGS=1 check_same fuzz-fast-flags fuzz
zsh "$here/diff-i386.sh" 4 | tail -1 | grep -q 'differences: 0$' && echo "pass test-i386" || { echo "FAIL test-i386"; fail=1; }

"$engine" --kernel "$images/kernel" > /dev/null && echo "pass kernel" || { echo "FAIL kernel"; fail=1; }
for fixture in descriptor-widths kernel-far-jumps; do
    if zsh "$here/build-cross-key-jumps.sh" "${images:A}" "$fixture" && \
        ENGINE_ALARM=30 "$engine" --kernel "$images/$fixture"; then
        echo "pass $fixture"
    else
        echo "FAIL $fixture"
        fail=1
    fi
done
# kernel-far-null-stack deliberately needs TCG for its transient invalid
# Engine stack model. Compare that image under real QEMU+hybrid and plain TCG;
# a standalone Engine pass is not the expected behavior for that fixture.
keeper=$(mktemp -d "$images/keeper.XXXXXX") || exit 1
if zsh "$here/build-keeper-smp-fixture.sh" "$keeper/smp"; then
    for cpus in 2 4; do
        ENGINE_SHARE=1 ENGINE_STATS=1 LC_HVDBT_KEEPER_INACTIVE_GUARD=1 ENGINE_ALARM=60 "$engine" --smp "$cpus" \
            "$keeper/smp/keeper-smp" >"$keeper/smp$cpus.out" 2>"$keeper/smp$cpus.err"
        observed=$?
        if python3 "$here/check-keeper-smp-fixture.py" "$keeper/smp$cpus.out" \
            "$keeper/smp$cpus.err" "$observed" --cpus "$cpus" --guard 1; then
            echo "pass experimental shared-keeper retirement, $cpus vCPUs"
        else
            echo "FAIL experimental shared-keeper retirement, $cpus vCPUs"
            fail=1
        fi
    done
else
    echo "FAIL building keeper SMP fixture"
    fail=1
fi
if zsh "$here/build-keeper-fixture.sh" "$keeper/single"; then
    ENGINE_SHARE=1 ENGINE_STATS=1 LC_HVDBT_KEEPER_INACTIVE_GUARD=1 ENGINE_POOL_PAGES=128 ENGINE_ALARM=60 "$engine" \
        --kernel "$keeper/single/keeper-inactive" >"$keeper/pool.out" 2>"$keeper/pool.err"
    observed=$?
    if python3 "$here/check-keeper-fixture.py" "$keeper/pool.out" "$keeper/pool.err" \
        "$observed" --require-pool-reclaim; then
        echo "pass experimental shared-keeper retirement under pool pressure"
    else
        echo "FAIL experimental shared-keeper retirement under pool pressure"
        fail=1
    fi
else
    echo "FAIL building keeper pool fixture"
    fail=1
fi
"$engine" --kernel-capi "$images/kernel" > /dev/null 2>&1 && echo "pass kernel through the C API" || { echo "FAIL kernel through the C API"; fail=1; }
"$engine" --hybrid "$images/hybrid" > /dev/null && echo "pass hybrid" || { echo "FAIL hybrid"; fail=1; }
ENGINE_HYBRID_SMP=1 ENGINE_ALARM=120 "$engine" --hybrid "$images/hybrid" > /dev/null && echo "pass hybrid, a second vCPU joining" || { echo "FAIL hybrid, a second vCPU joining"; fail=1; }
LC_HVDBT_NO_SHARE=1 "$engine" --hybrid "$images/hybrid" > /dev/null && echo "pass hybrid, no sharing" || { echo "FAIL hybrid, no sharing"; fail=1; }
LC_HVDBT_NO_REUSE=1 "$engine" --hybrid "$images/hybrid" > /dev/null && echo "pass hybrid, no reuse" || { echo "FAIL hybrid, no reuse"; fail=1; }

# 32-bit code in compatibility mode, in its own kernel. The transcript's hash is TCG's: Tests/diff-compat.sh --update writes it, after comparing with QEMU itself.
compat=$here/x86/compat
if ENGINE_TZCNT=1 ENGINE_ALARM=120 "$engine" --kernel "$images/compat" 2>/dev/null | python3 "$compat/transcript.py" "$compat/tcg-known.txt" | shasum | cut -d' ' -f1 | grep -qf "$compat/tcg-transcript.sha"; then
    echo "pass compat, as TCG's transcript"
else
    echo "FAIL compat: see Tests/diff-compat.sh"
    fail=1
fi
ENGINE_TZCNT=1 "$engine" --kernel-capi "$images/compat" >/dev/null 2>&1 && echo "pass compat through the C API" || { echo "FAIL compat through the C API"; fail=1; }
for vcpus in 2 4 8; do
    ENGINE_ALARM=60 "$engine" --smp $vcpus "$images/smp" > /dev/null && echo "pass $vcpus vCPUs" || { echo "FAIL $vcpus vCPUs"; fail=1; }
    ENGINE_SHARE=1 ENGINE_ALARM=60 "$engine" --smp $vcpus "$images/smp" > /dev/null && echo "pass $vcpus vCPUs, keeper rule" || { echo "FAIL $vcpus vCPUs, keeper rule"; fail=1; }
done
exit $fail
