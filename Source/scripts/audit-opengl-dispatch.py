#!/usr/bin/env python3
"""Inventory the actual generated GLI codec, not non-NULL dispatch pointers.

Run after generate-passthrough-opengl.py. Host export presence is deliberately
not called feature support: compatibility/core profiles and driver limits
still decide whether a native invocation is valid.
"""
import argparse
from collections import defaultdict
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
symbols={normalize(s):s for s in sorted(set(re.findall(r'\b(?:extern|GLAPI)\s+[^;{}]*?\b(gl\w+)\s*\(',public)))}
# Public aliases such as glVertexArrayRangeAPPLE cannot be recovered by
# normalizing the GLI field (vertex_array_range_EXT). The SDK's CGLMacro
# spells out the actual dispatch binding. Keep that evidence separate from
# name-only matches and from our generated host symbol choices.
macro_symbols=defaultdict(set)
macro_header=headers/'CGLMacro.h'
if macro_header.is_file():
    macros=macro_header.read_text().replace('\\\n',' ')
    macros=re.sub(r'/\*.*?\*/',' ',macros,flags=re.S)
    for name,body in re.findall(r'^#define[ \t]+(gl\w+)\([^\n]*?\)[ \t]+([^\n]+)',macros,re.M):
        for field in re.findall(r'\bdisp\.(\w+)\s*\)',body):
            macro_symbols[field].add(name)
library=ctypes.CDLL('/System/Library/Frameworks/OpenGL.framework/OpenGL') if a.host_exports else None
lines=['# PassthroughOpenGL dispatch inventory','',
    'Generated from the canonical 974-slot SDK table. `Codec` means a typed wire implementation, not GL conformance.',
    'Host exports are addresses only: they do not establish support in a particular renderer/profile.',
    'GLI overrides and private CGL contracts are implemented separately from the generated codec.','',
    '| Slot | GLI field | Codec host symbol | SDK public entry points (evidence) | Codec | Host export | Remaining seam |',
    '| --- | --- | --- | --- | --- | --- | --- |']
counts={}
gli_overrides={
    'get_vertex_attrib_pointerv_ARB':'explicit GLI adapter returns retained guest VAO pointer/VBO offset, never host packet storage',
    'label_object_with_responsible_process_APPLE':'guest-local accounting for native object namespaces; never sends guest PIDs to host',
}
known_limits={
    'generate_mipmap_EXT':'public GL color-policy and R/RG/RGB/RGBA8/16/16F range corrections, including immutable clipped tails; typed precision, arrays and caller-list isolation qualified; implicit generation and other compressed/depth-stencil coverage remain',
    'get_tex_image':'native readback; large CPU downloads use bounded staging and chunked replies',
    'get_compressed_tex_image':'native compressed readback; large CPU downloads use bounded staging and chunked replies',
    'buffer_data':'native definition; large CPU inputs stage bounded packets before one native call',
    'buffer_sub_data':'native update; large CPU inputs stage bounded packets before one native call',
    'get_buffer_sub_data':'one native snapshot; large CPU readbacks stream bounded replies',
    'map_buffer':'leased guest mapping; no packet-sized map ceiling, aggregate byte budget remains',
    'map_buffer_range':'leased guest mapping; no packet-sized map ceiling, aggregate byte budget remains',
    'draw_pixels':'native compatibility draw; large CPU layouts stage only touched bytes through a PBO',
    'read_pixels':'large CPU output streams one native snapshot; bitmap CPU/PBO packing corrected with retained buffer lifetime',
    'bitmap':'native compatibility bitmap; large CPU layouts stage touched rows through a PBO',
    'polygon_stipple':'native stipple; large CPU layouts stage touched rows through a PBO',
    'get_polygon_stipple':'CPU/PBO readback scatters native stipple state with exact packing and retained buffer lifetime',
    'get_stringi':'native per-profile extension enumeration intersected with qualified wire families',
    'get_pointerv':'guest-local array/offset, selection/feedback and Apple element/range pointers; other extension selectors remain',
    'array_element':'typed CPU elements and native x86 VBO offsets; negotiated PowerPC VBO snapshots qualified on Tiger/Leopard/Snow Rosetta1; large-VBO cost and normalization contracts remain',
    'vertex_array_range_EXT':'guest-local range state; coherent draw-time snapshots instead of native retained mappings',
    'flush_vertex_array_range_EXT':'draw-time snapshots are already coherent; no retained native range to flush',
    'sample_pass':'historical glSamplePass/glSamplePassARB; native compatibility entry, not modern sample coverage',
}
for field in ('tex_image1D','tex_image2D','tex_image3D','tex_sub_image1D','tex_sub_image2D','tex_sub_image3D',
              'compressed_tex_image1D','compressed_tex_image2D','compressed_tex_image3D',
              'compressed_tex_sub_image1D','compressed_tex_sub_image2D','compressed_tex_sub_image3D'):
    known_limits[field]='native image/update; large CPU inputs stream through a negotiated temporary PBO'
for field in ('tex_image1D','tex_image2D','tex_image3D','tex_sub_image1D','tex_sub_image2D','tex_sub_image3D'):
    known_limits[field]='native image/update with compact CPU addressing; actual large inputs retain bounded PBO streaming'
for field in ('color_table','color_sub_table','get_color_table','convolution_filter1D','convolution_filter2D',
              'get_convolution_filter','separable_filter2D','get_separable_filter','get_histogram','get_minmax'):
    known_limits[field]='native imaging with compact touched CPU rows and aliased vectors; actual packet byte budget remains; native PBO path preserved'
for row,signature in zip(coverage,mapping):
    assert row['slot']==signature['index'] and row['field']==signature['field']
    explicit=sorted(macro_symbols.get(row['field'],()))
    inferred=symbols.get(normalize(row['field']))
    candidates=explicit or ([inferred] if inferred else [])
    public=', '.join(f'`{name}`' for name in candidates)
    if public:
        public+= ' (CGLMacro)' if explicit else ' (spelling only)'
    probed=[row['symbol']] if row['symbol'] else candidates
    implemented=bool(row['supported']) or row['field'] in gli_overrides
    if row['field'] in gli_overrides:seam=gli_overrides[row['field']]
    elif row['supported']:seam=known_limits.get(row['field'],'implemented')
    elif not row['symbol']:
        seam=('Apple SDK dispatch binding; codec missing' if explicit else
              'possible public symbol; confirm dispatch binding' if inferred else
              'unclassified: public alias or private GLI hook')
    elif row['field'].startswith(('multi_draw','draw_elements','draw_range_elements')) or row['field'] in ('interleaved_arrays','array_element'):
        seam='vertex/index snapshot codec'
    elif 'pointer' in row['field'] or row['field'] in ('select_buffer','feedback_buffer'):
        seam='retained guest pointer/state contract'
    elif 'compressed_tex' in row['field']:seam='compressed payload/PBO/readback codec'
    elif '*' in signature['parameters']:seam='bounded pointer/result shape'
    else:seam='dedicated contract'
    counts[seam]=counts.get(seam,0)+1
    present='not measured' if library is None else ', '.join(
        f'`{name}`' for name in probed if getattr(library,name,None)) or 'none'
    lines.append(f"| {row['slot']} | `{row['field']}` | `{row['symbol'] or '—'}` | {public or '—'} | {'yes' if implemented else 'no'} | {present} | {seam} |")
lines+=['','## Totals','']+[f'- {name}: {count}' for name,count in sorted(counts.items())]
a.output.write_text('\n'.join(lines)+'\n')
print('; '.join(f'{name}={count}' for name,count in sorted(counts.items())))
