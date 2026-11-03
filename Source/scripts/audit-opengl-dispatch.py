#!/usr/bin/env python3
"""Inventory the actual generated GLI codec, not non-NULL dispatch pointers.

Run after generate-passthrough-opengl.py. Host export presence is deliberately
not called feature support: compatibility/core profiles and driver limits
still decide whether a native invocation is valid.
"""
import argparse
import ctypes
import json
from pathlib import Path
import re

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('generated',type=Path)
p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--host-exports',action='store_true')
a=p.parse_args()
coverage=json.loads((a.generated/'direct-coverage.json').read_text())
mapping=json.loads((a.generated/'passthrough-opengl-gli-map.json').read_text())['table']
headers=a.sdk/'System/Library/Frameworks/OpenGL.framework/Headers'
public='\n'.join((headers/n).read_text() for n in ('gl.h','glext.h','gl3.h','gl3ext.h') if (headers/n).is_file())
normalize=lambda s:s.removeprefix('gl').replace('_','').lower()
symbols={normalize(s):s for s in sorted(set(re.findall(r'extern\s+[^;{}]*?\b(gl\w+)\s*\(',public)))}
library=ctypes.CDLL('/System/Library/Frameworks/OpenGL.framework/OpenGL') if a.host_exports else None
lines=['# PassthroughOpenGL dispatch inventory','',
    'Generated from the canonical 974-slot SDK table. `Codec` means a typed wire implementation, not GL conformance.',
    'Host exports are addresses only: they do not establish support in a particular renderer/profile.',
    'GLI overrides and private CGL contracts are implemented separately from the generated codec.','',
    '| Slot | GLI field | Public entry point | Codec | Host export | Remaining seam |',
    '| --- | --- | --- | --- | --- | --- |']
counts={}
gli_overrides={
    'get_vertex_attrib_pointerv_ARB':'explicit GLI adapter returns retained guest VAO pointer/VBO offset, never host packet storage',
}
for row,signature in zip(coverage,mapping):
    assert row['slot']==signature['index'] and row['field']==signature['field']
    symbol=row['symbol'] or symbols.get(normalize(row['field']))
    implemented=bool(row['supported']) or row['field'] in gli_overrides
    if row['field'] in gli_overrides:seam=gli_overrides[row['field']]
    elif row['supported']:seam='implemented'
    elif not row['symbol']:seam='public-name/profile mapping or private GLI hook'
    elif row['field'].startswith(('multi_draw','draw_elements','draw_range_elements')) or row['field'] in ('interleaved_arrays','array_element'):
        seam='vertex/index snapshot codec'
    elif 'pointer' in row['field'] or row['field'] in ('select_buffer','feedback_buffer'):
        seam='retained guest pointer/state contract'
    elif 'compressed_tex' in row['field']:seam='compressed payload/PBO/readback codec'
    elif '*' in signature['parameters']:seam='bounded pointer/result shape'
    else:seam='dedicated contract'
    counts[seam]=counts.get(seam,0)+1
    present='not measured' if library is None else 'yes' if symbol and getattr(library,symbol,None) else 'no'
    lines.append(f"| {row['slot']} | `{row['field']}` | `{symbol or '—'}` | {'yes' if implemented else 'no'} | {present} | {seam} |")
lines+=['','## Totals','']+[f'- {name}: {count}' for name,count in sorted(counts.items())]
a.output.write_text('\n'.join(lines)+'\n')
print('; '.join(f'{name}={count}' for name,count in sorted(counts.items())))
