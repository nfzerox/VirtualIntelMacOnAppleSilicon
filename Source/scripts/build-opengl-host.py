#!/usr/bin/env python3
"""Build the universal PassthroughOpenGL executor, without probes, a VM or an old SDK.

The generated wire declarations are captured from the same period SDK as the
guest pair. Their generator fingerprint must match; do not silently regenerate
an incompatible protocol from the builder's newer SDK.
"""
import argparse
import hashlib
import io
import os
from pathlib import Path
import plistlib
import subprocess
import tarfile
import tempfile

SOURCE=Path(__file__).resolve().parents[1]
ARCHIVE=SOURCE/'prebuilt/PassthroughOpenGL/common/host-wire.tar.xz'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def generators():return {p:sha(SOURCE/'scripts'/p) for p in ('generate-passthrough-opengl.py','generate-opengl-gli.py')}

def capture(directory):
    names=('direct-opcodes.h','direct-layout.h','direct-declarations.inc','direct-get-counts.inc',
           'direct-host-generated.inc','direct-backing-read-only.inc')
    entries={name:(directory/name).read_bytes() for name in names}
    entries['Manifest.plist']=plistlib.dumps({'FormatVersion':1,'Generators':generators(),
        'SDK':'MacOSX10.9.sdk','Files':{name:hashlib.sha256(data).hexdigest() for name,data in entries.items()}})
    ARCHIVE.parent.mkdir(parents=True,exist_ok=True)
    with tarfile.open(ARCHIVE,'w:xz') as archive:
        for name,data in sorted(entries.items()):
            info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o644
            archive.addfile(info,io.BytesIO(data))

def build(output):
    inputs={p.relative_to(SOURCE).as_posix():sha(p) for p in sorted((SOURCE/'PassthroughOpenGL').iterdir()) if p.is_file() and p.name!='.DS_Store'}
    for p in (Path(__file__).resolve(),ARCHIVE,SOURCE/'vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h',
              SOURCE/'vendor/source/VMsvga2/vminclude/vim_gl_pixel_formats.h',
              SOURCE/'vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h',
              SOURCE/'scripts/generate-passthrough-opengl.py',SOURCE/'scripts/generate-opengl-gli.py'):
        inputs[p.relative_to(SOURCE).as_posix()]=sha(p)
    tools=subprocess.check_output(['xcrun','clang','--version'],text=True).split('InstalledDir:')[0]
    sdk_build=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-build-version'],text=True).strip()
    fingerprint=hashlib.sha256(plistlib.dumps({'Inputs':inputs,'Compiler':tools,'SDKBuild':sdk_build},sort_keys=True)).hexdigest()
    receipt=output/'BuildReceipt.plist';binary=output/'libVIMPassthroughOpenGLHost.dylib'
    if receipt.is_file() and binary.is_file():
        old=plistlib.loads(receipt.read_bytes())
        if old.get('InputSHA256')==fingerprint and old.get('BinarySHA256')==sha(binary):return binary
    with tarfile.open(ARCHIVE) as archive:
        entries={}
        for info in archive.getmembers():
            if not info.isfile() or info.name in entries or Path(info.name).name!=info.name or info.size>2*1024*1024:
                raise ValueError('Unexpected generated wire entry')
            entries[info.name]=archive.extractfile(info).read()
    manifest=plistlib.loads(entries.pop('Manifest.plist'))
    if manifest.get('Generators')!=generators() or manifest.get('Files')!={name:hashlib.sha256(data).hexdigest() for name,data in entries.items()}:
        raise ValueError('Wire sources changed. Regenerate with the same period SDK as the guest GL pair.')
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='opengl-host-',dir=output) as scratch:
        scratch=Path(scratch)
        for name,data in entries.items():(scratch/name).write_bytes(data)
        candidate=scratch/binary.name
        command=['xcrun','clang','-O2','-g','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror',
            '-Wno-deprecated-declarations','-Wno-misleading-indentation','-Wno-unused-function',
            '-arch','arm64','-arch','x86_64','-mmacosx-version-min=11.0','-fvisibility=hidden',
            '-DVIM_PASSTHROUGH_OPENGL_INPROCESS=1','-dynamiclib','-I',str(scratch),
            str(SOURCE/'PassthroughOpenGL/passthrough-opengl-host.c'),'-framework','OpenGL','-framework','IOSurface',
            '-framework','CoreFoundation','-lbsm','-Wl,-install_name,@rpath/libVIMPassthroughOpenGLHost.dylib','-o',str(candidate)]
        # Explicit build flags only: no inherited research experiment macros.
        subprocess.run(command,check=True,env={k:v for k,v in os.environ.items() if k not in ('CFLAGS','CPPFLAGS','LDFLAGS','MACOSX_DEPLOYMENT_TARGET')})
        if set(subprocess.check_output(['lipo','-archs',str(candidate)],text=True).split())!={'arm64','x86_64'}:
            raise ValueError('Executor is not universal')
        subprocess.run(['codesign','--force','--sign','-',str(candidate)],check=True)
        candidate.replace(binary)
    receipt.write_bytes(plistlib.dumps({'FormatVersion':1,'InputSHA256':fingerprint,'BinarySHA256':sha(binary),
        'HostArchitectures':['arm64','x86_64'],'MinimumHostVersion':'11.0','CommandProfilerCompiled':False,'Inputs':inputs}))
    return binary

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture-generated',type=Path,help='maintenance only: capture a matched guest wire generation')
    p.add_argument('--output',type=Path,default=SOURCE/'Build/PassthroughOpenGLHostRuntime')
    a=p.parse_args()
    if a.capture_generated:capture(a.capture_generated)
    else:print(build(a.output))
