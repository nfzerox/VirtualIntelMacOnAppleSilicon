#!/usr/bin/env python3
"""Build a finite non-rendering Tiger Rosetta import audit, NEVER for shipping.

Derive the complete platform symbol inventory from the actual candidate pair.
Keep the exact real support binaries/receipt separately; an owned guest's
ordinary guarded graphics installer can restore that pair after the audit.
"""
import argparse
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('graphics','sdk','cc','ld','nm','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    source=Path(__file__).resolve().parents[1]
    if args.output.exists():parser.error('Supply a new isolated output directory')
    record=plistlib.loads((args.graphics/'BuildReceipt.plist').read_bytes())
    if record.get('MinimumOS')!='10.4' or record.get('Architecture')!='i386':
        parser.error('Supply the actual native Tiger candidate pair')
    symbols=set()
    for name in ('GLEngine','libVIMPassthroughOpenGL.dylib'):
        listing=subprocess.check_output([str(args.nm),'-m','-u',str(args.graphics/name)],text=True)
        for line in listing.splitlines():
            match=re.search(r'external (_\w+) \(from (\w+)\)',line)
            if match and match[2] in ('libSystem','libgcc','IOKit','CoreFoundation','ApplicationServices'):
                symbols.add((match[1][1:],match[2]))
    if not symbols or len(symbols)>256:raise ValueError('Unexpected native import inventory')
    args.output.mkdir(parents=True)
    libraries={'libSystem':'/usr/lib/libSystem.B.dylib','libgcc':'/usr/lib/libgcc_s.1.dylib',
        'IOKit':'/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit',
        'CoreFoundation':'/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation',
        'ApplicationServices':'/System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices'}
    lines=['struct VIMTigerAuditSymbol {const char *name,*present,*missing,*library,*libpresent,*libmissing;};',
           'static const struct VIMTigerAuditSymbol vim_tiger_symbols[]={']
    for symbol,library in sorted(symbols):
        values=(symbol,'VIM_AUDIT_PRESENT_'+symbol,'VIM_AUDIT_MISSING_'+symbol,
                libraries[library],'VIM_AUDIT_LIB_PRESENT_'+symbol,'VIM_AUDIT_LIB_MISSING_'+symbol)
        lines.append('    {'+','.join(json.dumps(s) for s in values)+'},')
    lines.append('};\n')
    (args.output/'tiger-rosetta-symbols-generated.inc').write_text('\n'.join(lines))
    commands=[
        [args.cc,'-arch','i386','-mmacosx-version-min=10.4','-isysroot',args.sdk,
         '-O2','-Wall','-Wextra','-Werror','-I',args.output,'-c',
         source/'probes/opengl-tiger-rosetta-symbols.c','-o',args.output/'audit.o'],
        [args.ld,'-arch','i386','-macosx_version_min','10.4','-syslibroot',args.sdk,
         '-no_uuid','-bundle',args.output/'audit.o','-framework','IOKit',
         '-framework','CoreFoundation','-framework','ApplicationServices','-lSystem',
         '-o',args.output/'GLEngine']]
    for command in commands:subprocess.run(list(map(str,command)),check=True)
    # Preserve every other real receipt member; only the explicitly identified
    # diagnostic engine changes. The release packager does NOT consume this dir.
    for name in record['Files']:
        if name=='GLEngine':continue
        target=args.output/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(args.graphics/name,target)
    record['Files']['GLEngine']=True
    record['DiagnosticOnly']='NativeRosettaSymbolAudit'
    (args.output/'BuildReceipt.plist').write_bytes(plistlib.dumps(record))
    (args.output/'ImportSymbols.json').write_text(json.dumps(sorted(symbols),indent=2)+'\n')
    print('Built isolated audit of',len(symbols),'platform imports; NEVER package as graphics support')


if __name__=='__main__':main()
