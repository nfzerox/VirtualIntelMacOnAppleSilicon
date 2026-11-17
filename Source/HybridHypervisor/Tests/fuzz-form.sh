#!/bin/zsh
# Shows the first rounds where one fuzz form's output differs between Rosetta and the engine. Usage: fuzz-form.sh <form> [lines]
here=${0:A:h}
images=$here/../.build/images
n=$(( $1 + 1 ))
tmp=${TMPDIR:-/tmp}/engine-form.$$
mkdir -p "$tmp"
arch -x86_64 "$images/flatrun" "$images/fuzz" $n 1 > "$tmp/r.txt" 2>&1
"$here/../.build/release/engine-run" "$images/fuzz" $n 1 > "$tmp/e.txt" 2>&1
tail -1 "$tmp/r.txt" | cut -c1-80
paste -d'\n' "$tmp/r.txt" "$tmp/e.txt" | awk 'NR%2==1{a=$0;next}{ if (a!=$0 && a !~ /^round/) { print "R:" a; print "E:" $0; if (++n>=4*0+'${2:-6}') exit } else if (a ~ /^ in/) { last=a } }' | cut -c1-200
grep -c . "$tmp/e.txt"
rm -rf "$tmp"
