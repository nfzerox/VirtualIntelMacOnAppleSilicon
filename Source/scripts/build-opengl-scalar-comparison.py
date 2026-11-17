#!/usr/bin/env python3
"""Build finite scalar fixtures and a generated generic-path reference.

Both use the actual current decoder/client. Only the three new fast append
calls are removed from the reference GENERATED include, never repository
source or a guest library. Neither binary installs anything. Run --builder-only
for byte/state tests, or without arguments for actual native GPU checks/timings.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

source=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--minimum',default='10.5')
a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
sdk=source/'resources/sdk/MacOSX10.9.sdk'
cc=source/'resources/xcode/XcodeMojave.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
candidate=out/'Candidate';reference=out/'Reference'
subprocess.run(['python3',str(source/'scripts/generate-passthrough-opengl.py'),str(sdk),str(candidate)],check=True)
shutil.copytree(candidate,reference)
generated=reference/'direct-client-generated.inc'
text,count=re.subn(r'    if\(DGTrySubmitImmediateScalars\(context,(267|312|353),immediate_args,[23]\)\)return;',
                  '    (void)immediate_args; /* finite generic-path reference */',generated.read_text())
if count!=3:raise SystemExit('Expected exactly the three measured scalar APIs')
generated.write_text(text)
for name,include in [('ScalarFast',candidate),('ScalarReference',reference)]:
    command=[str(cc),'-O2','-g','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations',
        '-Wno-missing-braces','-Wno-missing-field-initializers',
        '-arch','i386','-arch','x86_64','-isysroot',str(sdk),'-mmacosx-version-min='+a.minimum,
        '-I',str(include),str(source/'probes/opengl-direct-compact-scalars.c'),
        '-framework','IOKit','-framework','CoreFoundation','-o',str(out/name)]
    subprocess.run(command,check=True)
    subprocess.run(['codesign','--force','--sign','-','--digest-algorithm=sha1,sha256',str(out/name)],check=True)
print(out)
