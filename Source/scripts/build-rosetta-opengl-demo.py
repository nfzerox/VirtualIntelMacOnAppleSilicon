#!/usr/bin/env python3
"""Build a genuinely PowerPC-only Rosetta1 GUI test with period Apple tools.

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


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gcc-root',type=Path,required=True)
    p.add_argument('--tools-root',type=Path,required=True)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--minimum-version',choices=('10.4','10.5','10.6'),default='10.6')
    p.add_argument('--output',type=Path,required=True,help='New .app path')
    a=p.parse_args()
    gcc=a.gcc_root.resolve()/'bin/powerpc-apple-darwin10-gcc-4.2.1'
    tools=a.tools_root.resolve();sdk=a.sdk.resolve();output=a.output.resolve()
    if output.exists() or output.suffix!='.app':p.error('Use a new .app output path')
    for f in (gcc,tools/'bin/ld',tools/'libexec/gcc/darwin/ppc/as',sdk/'usr/include/stdio.h'):
        if not f.is_file():p.error('Missing period input: '+str(f))
    source=Path(__file__).resolve().parents[1]/'probes/RosettaOpenGLDemo'
    binary=output/'Contents/MacOS/RosettaOpenGLDemo';binary.parent.mkdir(parents=True)
    # The target-prefixed GCC is already PowerPC. Passing -arch through this
    # driver incorrectly forwards it to cc1obj (unlike Apple's umbrella gcc).
    command=[str(gcc),'-mmacosx-version-min='+a.minimum_version,'-mcpu=powerpc','-isysroot',str(sdk),
        '-B'+str(tools/'libexec/gcc/darwin/ppc')+'/', '-B'+str(tools/'bin')+'/',
        '-O2','-Wall','-Wextra','-Werror','-std=gnu99','-framework','Cocoa','-framework','OpenGL','-framework','ApplicationServices',
        str(source/'main.m'),'-o',str(binary)]
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
    architectures=subprocess.check_output(['lipo','-archs',str(binary)],text=True).split()
    if architectures not in (['ppc'],['ppc7400']):raise ValueError('Refusing an Intel/fat test app: '+str(architectures))
    info=plistlib.loads((source/'Info.plist').read_bytes())
    info['LSMinimumSystemVersion']=a.minimum_version
    (output/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    print(output)


if __name__=='__main__':main()
