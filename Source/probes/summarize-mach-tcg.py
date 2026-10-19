"""Conservative instruction attribution for tcg-mach-sample JSONL.
Requires capstone==5.0.6. Percentages refer only to selected JIT-range samples.
"""
import collections
import json
import re
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
counts = collections.Counter()
ops = collections.Counter()
for line in open(sys.argv[1]):
    row = json.loads(line)
    raw = bytes.fromhex(row['bytes'])
    pc, start = int(row['pc'],16), int(row['start'],16)
    # Decode each word independently: preceding bytes may be a literal pool.
    instructions = {}
    for offset in range(0,len(raw)-3,4):
        decoded = list(decoder.disasm(raw[offset:offset+4],start+offset))
        if decoded:
            instructions[start+offset] = decoded[0]
    current = instructions.get(pc)
    category = 'unclassified generated instruction'
    for address, ins in instructions.items():
        if ins.mnemonic != 'ldp' or not re.fullmatch(r'x16, x17, \[x19, #-0x[0-9a-f]+\]',ins.op_str):
            continue
        seq = [instructions.get(address+4*i) for i in range(12)]
        if not seq[1] or seq[1].mnemonic!='and' or not seq[2] or seq[2].op_str!='x17, x17, x16':
            continue
        for j in range(6,12):
            if seq[j] and seq[j].mnemonic=='b.ne':
                if address<=pc<=address+4*j:
                    category='inline software-TLB check'
                break
    if current:
        text = current.mnemonic+' '+current.op_str
        ops[text]+=1
        if category == 'unclassified generated instruction':
            if text=='bti j' or text.startswith('ldur w20, [x19, #-0x14]') or text=='sturb wzr, [x19, #-0x10]':
                category='block entry bookkeeping'
            elif current.mnemonic in ('ldr','str','ldp','stp','ldrb','strb','ldrh','strh') and '[x19' in current.op_str:
                category='CPU state load/store'
            elif current.mnemonic in ('ldr','str') and re.search(r'\[x17, x\d+\]',current.op_str):
                category='translated memory access (recognized form)'
    counts[category]+=1
total=sum(counts.values())
for name, count in counts.most_common():
    print(f'{count:6d} {count/total:7.2%} {name}')
print('selected JIT samples:',total)
print('Top instruction forms:')
for name,count in ops.most_common(15):
    print(count,name)
print('Pattern-based lower bounds; unclassified is NOT equivalent to useful guest computation.')
