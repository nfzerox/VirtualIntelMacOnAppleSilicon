#!/usr/bin/env python3
"""Build a genuinely PowerPC-only Rosetta1 GUI demo or finite GL CLI probe.

Use Xcode3.2.6 gcc4.2.pkg and DeveloperToolsCLI.pkg expanded with pkgutil
--expand-full. Pass their Payload/usr directories, plus the genuine target SDK.
The compiler/assembler/linker have x86_64 host slices (Rosetta2 on ARM hosts).
No compiler installation, guest developer tools, network or signing required.
"""
import argparse
import os
import plistlib
from pathlib import Path
import subprocess
import struct


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gcc-root',type=Path,required=True)
    p.add_argument('--tools-root',type=Path,required=True)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--minimum-version',choices=('10.4','10.5','10.6'),default='10.6')
    probes=p.add_mutually_exclusive_group()
    probes.add_argument('--array-element-probe',action='store_true',help='Build the finite CLI VBO/CPU/display-list probe instead of the demo app')
    probes.add_argument('--signed-normalization-probe',action='store_true',help='Build the public signed-attribute pixel matrix instead of the demo app')
    probes.add_argument('--mixed-index-probe',action='store_true',help='Build the public mixed CPU/VBO/EBO draw and list fixture')
    probes.add_argument('--compiled-index-probe',action='store_true',help='Build the public multi-draw/base-vertex display-list fixture')
    p.add_argument('--output',type=Path,required=True,help='New .app path, or executable path with a probe option')
    a=p.parse_args()
    gcc=a.gcc_root.resolve()/'bin/powerpc-apple-darwin10-gcc-4.2.1'
    tools=a.tools_root.resolve();sdk=a.sdk.resolve();output=a.output.resolve()
    cli=a.array_element_probe or a.signed_normalization_probe or a.mixed_index_probe or a.compiled_index_probe
    if output.exists() or (not cli and output.suffix!='.app'):p.error('Use a new output path (.app for the demo)')
    for f in (gcc,tools/'bin/ld',tools/'libexec/gcc/darwin/ppc/as',sdk/'usr/include/stdio.h'):
        if not f.is_file():p.error('Missing period input: '+str(f))
    root=Path(__file__).resolve().parents[1]
    source=root/'probes/RosettaOpenGLDemo'
    binary=output if cli else output/'Contents/MacOS/RosettaOpenGLDemo'
    binary.parent.mkdir(parents=True,exist_ok=True)
    # The target-prefixed GCC is already PowerPC. Passing -arch through this
    # driver incorrectly forwards it to cc1obj (unlike Apple's umbrella gcc).
    command=[str(gcc),'-mmacosx-version-min='+a.minimum_version,'-mcpu=powerpc','-isysroot',str(sdk),
        '-B'+str(tools/'libexec/gcc/darwin/ppc')+'/', '-B'+str(tools/'bin')+'/',
        '-O2','-Wall','-Wextra','-Werror','-std=gnu99','-framework','OpenGL']
    if a.array_element_probe:
        command+=['-include',str(root/'probes/opengl-period-context.h'),str(root/'probes/opengl-array-element.c')]
    elif a.signed_normalization_probe:
        command+=[str(root/'probes/opengl-signed-normalization-pixels.c')]
    elif a.mixed_index_probe:
        command+=[str(root/'probes/opengl-mixed-index-arrays.c')]
    elif a.compiled_index_probe:
        command+=[str(root/'probes/opengl-compiled-index-families.c')]
    else:
        command+=['-framework','Cocoa','-framework','ApplicationServices',str(source/'main.m')]
    command+=['-o',str(binary)]
    if a.minimum_version=='10.4':
        # Tiger's stdarg/float headers deliberately include_next GCC's own
        # headers. The relocated cross driver otherwise searches for those
        # inside the SDK, where this earlier SDK does not contain them.
        builtin=Path(subprocess.check_output([str(gcc),'-print-file-name=include'],text=True,
            env={**os.environ,'MACOSX_DEPLOYMENT_TARGET':a.minimum_version}).strip()).resolve()
        if not all((builtin/name).is_file() for name in ('stdarg.h','float.h')):
            p.error('The PowerPC compiler builtin headers are incomplete')
        command[1:1]=['-isystem',str(builtin)]
    subprocess.run(command,env={**os.environ,'MACOSX_DEPLOYMENT_TARGET':a.minimum_version},check=True)
    # New Apple lipo no longer recognizes a valid thin PowerPC Mach-O. Check
    # the genuine big-endian 32-bit executable header, not tool availability.
    with binary.open('rb') as file:header=file.read(16)
    if len(header)!=16 or struct.unpack('>4I',header)[0:2]!=(0xfeedface,18) or struct.unpack('>4I',header)[3]!=2:
        raise ValueError('Refusing a non-PowerPC32/fat test executable')
    if cli:
        print(output)
        return
    info=plistlib.loads((source/'Info.plist').read_bytes())
    info['LSMinimumSystemVersion']=a.minimum_version
    (output/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    print(output)


if __name__=='__main__':main()
