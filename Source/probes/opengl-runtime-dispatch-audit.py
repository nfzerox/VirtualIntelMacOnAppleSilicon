#!/usr/bin/env python3
"""Compare native libGL dispatch stubs with surveyed field positions.

Run on a loose native libGL.dylib copied from the reference OS. This does not
execute it or load host libraries. Native CGLGetParameter(224), GLI record
analysis and live rendering remain separate evidence requirements.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('map',type=Path)
p.add_argument('library',type=Path)
p.add_argument('--count',type=int,choices=(686,757,759,764,968,972,974),required=True)
p.add_argument('--arch',choices=('x86_64','i386'),default='x86_64')
p.add_argument('--period-sdk',type=Path,
               help='Actual Leopard SDK: audit its named layout rather than a canonical prefix')
p.add_argument('--tls-offset',choices=('0xf0','0x150'),default='0xf0',
               help='Observed native libGL TLS slot; Snow Leopard uses 0x150')
a=p.parse_args()
rows=json.loads(a.map.read_text())['table']
if len(rows)!=974:raise SystemExit('Expected the canonical 974-slot map')
if a.period_sdk:
    if a.count not in (686,757,759):raise SystemExit('Named-layout mode requires measured Tiger/Leopard runtime')
    header=a.period_sdk/'System/Library/Frameworks/OpenGL.framework/Headers/gliDispatch.h'
    source=re.sub(r'/\*.*?\*/','',header.read_text(),flags=re.S)
    fields=re.findall(r'[^;{}]+?\(\s*\*\s*(\w+)\)\s*\(.*?\)\s*;',source,re.S)
    tiger=a.count==686 and a.arch=='i386' and len(fields)==686
    leopard=a.count==757 and a.arch=='x86_64' and len(fields)==759 and fields[-2:]==['depth_rangef','clear_depthf']
    late_tiger=a.count==759 and a.arch=='i386' and len(fields)==759 and fields[-2:]==['depth_rangef','clear_depthf']
    if not (tiger or leopard or late_tiger) or len(set(fields))!=len(fields):
        raise SystemExit('Unexpected period SDK layout; no ABI assumption accepted')
    rows=[dict(index=index,field=field) for index,field in enumerate(fields[:a.count])]
elif a.count in (686,757,759):
    raise SystemExit('Tiger/Leopard are not canonical prefixes; supply the actual --period-sdk')
if a.arch=='i386' and a.count not in (686,759):raise SystemExit('Only surveyed Tiger i386 dispatches are accepted here')
text=subprocess.check_output(['otool','-arch',a.arch,'-tvV',str(a.library)],text=True)
parts=re.split(r'^_(gl\w+):\s*$',text,flags=re.M)
def dispatch_offset(body):
    if a.arch=='i386':
        # 8R3032 libGL's stack-page TLS cache is in context+0xac0. Its
        # pthread_getspecific fallback restores that same context register;
        # the native stub passes *(context) as the GLIContext argument and
        # dispatches through (slot+1)*4. Never accept an arbitrary indirect
        # call from another register, later function, or unchecked TLS shape.
        body=re.split(r'^[^\s:]+:\s*$',body,flags=re.M)[0]
        # The actual 8R4088 restore build grows the table to759 entries;
        # its same stack-page cache moves to context+0xbe4. Do not accept
        # that offset for the earlier686-entry implementation.
        cache='0xbe4' if a.count==759 else '0xac0'
        bases=set(re.findall(r'xorl\s+'+cache+r'\((%e\w+)\),',body))
        if len(bases)!=1 or '_pthread_getspecific' not in body:return None
        base=re.escape(next(iter(bases)))
        offsets=re.findall(r'(?:calll|jmpl)\s+\*(0x[0-9a-f]+)\('+base+r'\)',body)
        return int(offsets[0],16) if len(offsets)==1 else None
    # Leopard uses r9/r10 for larger public signatures and may CALL the GLI
    # entry rather than tail-jump. Follow its actual TLS base, not an rax
    # assumption or an arbitrary memory operand elsewhere in the function.
    loads=list(re.finditer(r'movq\s+%gs:'+re.escape(a.tls_offset)+r',\s*(%r\w+)\s*$',body,re.M))
    if len(loads)!=1:return None
    base=loads[0].group(1);offsets=[]
    for line in body[loads[0].end():].splitlines():
        instruction=re.match(r'^\w+\s+(\w+)\s+([^#]+)',line.strip())
        if not instruction:continue
        mnemonic,operands=instruction.groups();operands=operands.strip()
        slot=re.match(r'\*?(0x[0-9a-f]+)\('+re.escape(base)+r'\)(?:,|$)',operands)
        if slot and mnemonic in ('movq','callq','jmpq'):offsets.append(int(slot.group(1),16))
        if mnemonic.startswith(('call','jmp','ret')):break
        # Do not interpret later uses after the TLS pointer was overwritten.
        if mnemonic not in ('cmpq','testq','pushq') and re.search(r'(?:,\s*|^)'+re.escape(base)+r'$',operands):break
    return offsets[0] if len(offsets)==1 else None

functions={name:dispatch_offset(body) for name,body in zip(parts[1::2],parts[2::2])}
normalize=lambda name:name.removeprefix('gl').replace('_','').lower()
# The supplied 10.5 header spells the unsigned query Iiuv, while its native
# exported stub is Iuiv. This affects name matching only, never slot order.
aliases={'get_tex_parameterIiuv_EXT':'get_tex_parameterIuiv_EXT'} if a.count in (757,759) else {}
checked=[]
mismatches=[]
for row in rows[:a.count]:
    # Use the native function name, not bridge aliases such as an ARB vertex
    # array entry routed to the older APPLE operation for the host backend.
    for name,offset in functions.items():
        if normalize(name)!=normalize(aliases.get(row['field'],row['field'])) or offset is None:continue
        checked.append(row['field'])
        expected=(row['index']+1)*(4 if a.arch=='i386' else 8) # leading GLIContext pointer
        if offset!=expected:
            mismatches.append(dict(field=row['field'],index=row['index'],
                expected=expected,observed=offset))
print(json.dumps(dict(RuntimeSlots=a.count,CheckedStubs=len(checked),
    UncheckedFields=a.count-len(set(checked)),Mismatches=mismatches,
    Layout=('Tiger named SDK/runtime' if a.count in (686,759) else 'Leopard named SDK/runtime') if a.period_sdk else 'canonical prefix'),indent=2))
if len(checked)<600 or mismatches:raise SystemExit(1)
