"""Validate the two native i386 Tiger adapters' special Rosetta1 namespace.

Only the two exact Tiger adapter filenames in its runtime root select this policy in build-app.
Host dependency availability checks remain unchanged. The listed APIs exist
in native Tiger, but its Rosetta1 GL namespace deliberately omits some of them.
tiger-rosetta-platform.h checks pointers/provides fallbacks; making these
imports strong would break PPC clients. Unknown optional APIs still fail.
"""
import re
import subprocess
from deployment_target import ALLOWED_WEAK

TIGER_ROSETTA_WEAK = {'_strlen', '_setenv', '_syslog', '_dladdr', '_mprotect',
                     '_nlist', '_vm_region', '_vm_read_overwrite', '_vm_protect',
                     '__dyld_get_image_vmaddr_slide'}

def check_tiger_rosetta_imports(binary):
    if subprocess.check_output(['lipo','-archs',str(binary)],text=True).split()!=['i386']:
        raise RuntimeError('Tiger Rosetta optional imports require the native i386 adapter')
    imports=subprocess.check_output(['xcrun','dyld_info','-arch','i386','-imports',str(binary)],text=True)
    allowed={s.lstrip('_') for s in ALLOWED_WEAK|TIGER_ROSETTA_WEAK}
    weak=re.findall(r'^\s+(\S+) \[weak-import\] \(from libSystem\)',imports,re.M)
    missing=[name for name in weak if name.lstrip('_') not in allowed]
    if missing:raise RuntimeError(f'{binary} has unaudited Tiger optional imports: '+', '.join(missing))
