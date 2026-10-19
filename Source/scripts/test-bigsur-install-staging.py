#!/usr/bin/env python3
"""Bounded Big Sur preparation/publication fixture; never targets a live root."""
import json
import os
from pathlib import Path
import plistlib
import shutil
import runpy
import struct
import subprocess
import sys
import tempfile

prepare, install, tools = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
def dependency(name):
    name=name.encode();size=(24+len(name)+8)&~7
    return struct.pack('<6I',12,size,24,0,0,0)+name+bytes(size-24-len(name))
commands=b''.join(dependency(p) for p in (
    '/System/Library/Frameworks/Metal.framework/Versions/A/Metal',
    '/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface'))
image=struct.pack('<8I',0xfeedfacf,0x1000007,3,8,2,len(commands),0,0)+commands+b'native fixture bytes'
with tempfile.TemporaryDirectory(prefix='vim-bigsur-install-') as temporary:
    base=Path(temporary).resolve();root=base/'root'
    source=Path(__file__).resolve().parents[1];support=base/'Support';support.mkdir()
    record=runpy.run_path(str(source/'scripts/package-app-guest-payloads.py'))['build_bigsur'](source,support,tools)
    extracted=json.loads(subprocess.check_output([prepare,'--extract',support/record['Archive'],base/'Extracted']))
    assert extracted['Extracted'] and extracted['EntryCount']==record['EntryCount']
    payload=Path(extracted['PayloadDirectory'])
    version=root/'System/Library/CoreServices/SystemVersion.plist';version.parent.mkdir(parents=True)
    version.write_bytes(plistlib.dumps({'ProductVersion':'11.0.1','ProductBuildVersion':'20B9999'}))
    originals={}
    for name in ('AppleParavirtGPUMetal','AppleMetalOpenGLRenderer'):
        bundle=root/'System/Library/Extensions'/f'{name}.bundle';binary=bundle/'Contents/MacOS'/name
        binary.parent.mkdir(parents=True);binary.write_bytes(image);originals[binary]=image
        (bundle/'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':name}))
    receipt=json.loads(subprocess.check_output([prepare,root,payload,tools],
        env=dict(os.environ,VIM_TEST_SIGNER='/usr/bin/true')))
    assert receipt['Guest']=='bigsur' and receipt['Prepared'] and not receipt['Installed']
    assert all(p.read_bytes()==data for p,data in originals.items())
    journal=base/'journal.plist'
    plan=json.loads(subprocess.check_output([install,'--plan',root,payload,journal]))
    assert plan['TargetCount']==2 and not plan['Installed']
    state=plistlib.loads(journal.read_bytes())
    assert state['Metadata']['BootCaches']['Policy']=='library-only'
    assert not state['Metadata']['BootCaches']['Caches']
    assert all(e['RelativePath'].startswith('Library/GPUBundles/') for e in state['Entries'])
    # If the legacy kernel-cache builder is accidentally invoked this fails.
    result=json.loads(subprocess.check_output([install,'--apply',journal],
        env=dict(os.environ,VIM_TEST_CACHE_BUILDER='/usr/bin/false')))
    assert result['FilesPublished'] and result['Cache']['CacheRequired'] is False
    assert result['RebootRequired'] and not result['Ready']
    assert all(p.read_bytes()==data for p,data in originals.items())
    assert not (root/'System/Library/PrelinkedKernels').exists()
    subprocess.run([install,'--rollback',journal],check=True,stdout=subprocess.DEVNULL)
    assert all(not (root/e['RelativePath']).exists() for e in state['Entries'])
    assert all(p.read_bytes()==data for p,data in originals.items())
print('PASS Big Sur: native-derived bundles, fixed library-only targets, no boot caches, publication and rollback')
