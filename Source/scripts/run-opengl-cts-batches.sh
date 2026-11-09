#!/bin/bash
# Run matching Khronos case families independently on physical and guest Macs.
# Usage: bash run-opengl-cts-batches.sh [--isolate-cases] [--watchdog enable|disable] [--process-library DYLIB] CTS_DIR NEW_LOG_DIR CASE_GLOB [...]
set -eu
isolate=0
process_library=
watchdog=enable
while [ "$#" -gt 0 ]; do
    case "$1" in
        --isolate-cases) isolate=1; shift ;;
        --watchdog)
            [ "$#" -ge 2 ] || exit 64
            case "$2" in enable|disable) watchdog="$2";; *) echo 'Choose enable or disable for the CTS watchdog.' >&2;exit 64;; esac
            shift 2 ;;
        --process-library)
            [ "$#" -ge 2 ] && [ -f "$2" ] || { echo 'Choose an existing diagnostic library.' >&2; exit 64; }
            process_library="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; shift 2 ;;
        *) break ;;
    esac
done
if [ "$#" -lt 3 ]; then
    echo "Usage: $0 [--isolate-cases] [--watchdog enable|disable] [--process-library DYLIB] CTS_DIR NEW_LOG_DIR CASE_GLOB [...]" >&2
    exit 64
fi
cts=$(cd "$1" && pwd)
if [ ! -x "$cts/glcts" ] || [ ! -r "$cts/gl_cts/data/gl33/arrays.test" ]; then
    echo "The CTS directory must contain the executable and its unmodified case data." >&2
    exit 66
fi
mkdir -m 700 "$2"
logs=$(cd "$2" && pwd)
printf 'watchdog=%s\n' "$watchdog" >"$logs/runner-options.txt"
shift 2
cd "$cts"
export LC_ALL=C
index=0
overall_rc=0
run_cts() {
    # The optional diagnostic must affect only our CTS process, not mkdir,
    # shell utilities or unrelated platform-binary architectures. Never set
    # a login/session-wide DYLD_INSERT_LIBRARIES environment variable.
    if [ -n "$process_library" ]; then
        env DYLD_INSERT_LIBRARIES="$process_library" ./glcts "$@"
    else
        ./glcts "$@"
    fi
}
run_case() {
    run_cts --deqp-case="$1" --deqp-gl-context-type=cgl \
        --deqp-surface-type=fbo --deqp-surface-width=256 --deqp-surface-height=256 \
        --deqp-gl-config-name=rgba8888d24s8 --deqp-watchdog="$watchdog" \
        --deqp-log-images=disable --deqp-log-shader-sources=disable \
        --deqp-log-filename="$2.qpa" >"$2.stdout" 2>&1
}
for cases do
    index=$((index + 1))
    file=$(printf '%s/%03d' "$logs" "$index")
    printf '%s\n' "$cases" >"$file.filter"
    rc=0
    run_cts --deqp-case="$cases" --deqp-runmode=stdout-caselist \
        --deqp-log-filename="$file.enumeration.qpa" >"$file.cases" 2>"$file.enumeration.stderr" || rc=$?
    if [ "$rc" -eq 0 ]; then
        if [ "$isolate" -eq 0 ]; then
            run_case "$cases" "$file" || rc=$?
            # CTS can write #endSession and exit zero after a test's deinit
            # aborts the family. Recover only cases never started, preserving
            # any failed/incomplete original case rather than retrying it.
            [ -f "$file.qpa" ] || touch "$file.qpa"
            awk 'FILENAME==ARGV[1] { if ($1=="#beginTestCaseResult") seen[$2]=1; next }
                 $1=="TEST:" && !seen[$2] { print $2 }' \
                "$file.qpa" "$file.cases" >"$file.missing"
            recovery=0
            while IFS= read -r name; do
                recovery=$((recovery + 1))
                part=$(printf '%s.recovery.%04d' "$file" "$recovery")
                status=0
                run_case "$name" "$part" || status=$?
                printf '%s\t%s\t%s\n' "$recovery" "$status" "$name" >>"$file.recovery.tsv"
                if [ -f "$part.qpa" ]; then cat "$part.qpa" >>"$file.qpa"; fi
                if [ "$status" -ne 0 ]; then rc=$status; fi
            done <"$file.missing"
        else
            # A native driver/test crash must not hide the following cases.
            # Retain every original QPA and exit status. The concatenated
            # family log is for compare-opengl-cts.py, whose expected list
            # still detects absent and unterminated individual results.
            case_index=0
            while IFS= read -r line; do
                case "$line" in
                    "TEST: "*) name=${line#TEST: } ;;
                    *) continue ;;
                esac
                case_index=$((case_index + 1))
                part=$(printf '%s.%04d' "$file" "$case_index")
                status=0
                run_case "$name" "$part" || status=$?
                printf '%s\t%s\t%s\n' "$case_index" "$status" "$name" >>"$file.status.tsv"
                if [ -f "$part.qpa" ]; then cat "$part.qpa" >>"$file.qpa"; fi
                if [ "$status" -ne 0 ]; then rc=$status; fi
            done <"$file.cases"
            if [ "$case_index" -eq 0 ]; then rc=65; fi
        fi
    fi
    printf '%s\t%s\t%s\n' "$index" "$rc" "$cases" | tee -a "$logs/batches.tsv"
    if [ "$rc" -ne 0 ] && [ "$overall_rc" -eq 0 ]; then overall_rc=$rc; fi
done
exit "$overall_rc"
