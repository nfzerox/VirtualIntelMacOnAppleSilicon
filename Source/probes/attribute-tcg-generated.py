"""Join exclusive sample PCs to bounded QEMU out_asm logging.

Reports coverage, rejects PCs with conflicting logged instructions. Logging
must cover the sample; this is diagnostic, not a timing benchmark. Previously
cached blocks remain unknown. Categories are conservative instruction patterns,
not guest instruction semantics. Guest addresses may be PC-relative offsets.
"""
import collections
import contextlib
import io
import pathlib
import re
import runpy
import sys

sample, code = sys.argv[1:3]
with contextlib.redirect_stdout(io.StringIO()):
    state = runpy.run_path(str(pathlib.Path(__file__).with_name('summarize-tcg-sample.py')))
weights = state['generated_pcs']
matches = collections.defaultdict(set)
block = []


def consume():
    categories = {}
    # Recognize the inspected AArch64 software-TLB lookup, ending at its
    # conditional slow-path branch. Do not classify the actual guest access.
    for i, (pc, word, mnemonic, operands) in enumerate(block):
        if mnemonic == 'ldp' and re.fullmatch(r'x16, x17, \[x19, #-0x[0-9a-f]+\]', operands):
            tail = block[i:i+12]
            if len(tail) < 8 or tail[1][2] != 'and' or tail[2][2:] != ('add', 'x17, x17, x16'):
                continue
            for j, entry in enumerate(tail):
                if entry[2] == 'b.ne' and j >= 6:
                    for p, *_ in tail[:j+1]:
                        categories[p] = 'inline software-TLB check'
                    break
    if len(block) >= 4 and block[0][1] == 'd503249f' and block[1][2] == 'ldur' and block[2][2] == 'tbnz' and block[3][2] == 'sturb':
        for p, *_ in block[:4]:
            categories[p] = 'block entry/exit-request check'
    for pc, word, mnemonic, operands in block:
        if pc in weights:
            category = categories.get(pc, 'other mapped ARM instruction')
            matches[pc].add((word, category))


with open(code) as source:
    for line in source:
        if line.startswith('OUT:'):
            consume()
            block = []
        m = re.match(r'0x([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)', line)
        if m:
            block.append((int(m[1],16),m[2],m[3],m[4].strip()))
consume()
counts = collections.Counter()
for pc, weight in weights.items():
    candidates = matches[pc]
    category = ('not covered by translation log' if not candidates else
                next(iter(candidates))[1] if len(candidates)==1 else
                'ambiguous/reused address')
    counts[category] += weight
total = sum(weights.values())
print('Opaque exclusive samples with a single numeric PC:', total)
for category, count in counts.most_common():
    print(f'{count:7d} {100*count/total:6.2f}% {category}')
print('Coverage is selective: do not extrapolate mapped proportions to all execution.')
