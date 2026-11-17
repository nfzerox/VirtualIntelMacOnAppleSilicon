#!/bin/zsh
# Differential test: transcript of the fuzz image under Rosetta versus under the engine. Prints the first differing forms.
here=${0:A:h}
images=$here/../.build/images
tmp=${TMPDIR:-/tmp}/engine-fuzz.$$
mkdir -p "$tmp"
arch -x86_64 "$images/flatrun" "$images/fuzz" > "$tmp/rosetta.txt" 2> "$tmp/rosetta.err"
"$here/../.build/release/engine-run" "$images/fuzz" > "$tmp/engine.txt" 2> "$tmp/engine.err"
rc=$?
echo "rosetta $(wc -l < "$tmp/rosetta.txt") forms, engine $(wc -l < "$tmp/engine.txt") forms, engine rc $rc"
tail -1 "$tmp/engine.err"
diff "$tmp/rosetta.txt" "$tmp/engine.txt" | grep '^>' | head -${1:-30}
diff "$tmp/rosetta.txt" "$tmp/engine.txt" | grep -c '^>'
rm -rf "$tmp"
