#!/usr/bin/env python3
"""Maintainer-only capture of pinned OpenCore build sources (no build/fetch).

Ordinary/full builds use the resulting self-contained archive, not these Git
checkouts. The upstream OpenCore tag is captured without study instrumentation;
our separately tracked link-bounds patch is applied by the builder.
"""
import argparse
import hashlib
import io
from pathlib import Path
import plistlib
import subprocess
import tarfile

OC_REVISION='e8437f737708c7151b243d967f9ceca54193d97e'
EDK_REVISION='f57172652dc48efac882d0cde416676e4fe4f05a'
PACKAGES=('BaseTools','MdePkg','MdeModulePkg','UefiCpuPkg','NetworkPkg',
          'ShellPkg','OvmfPkg','CryptoPkg','SecurityPkg','EmbeddedPkg','ArmPkg','UefiPayloadPkg')
CHILDREN=('BaseTools/Source/C/BrotliCompress/brotli',
          'MdeModulePkg/Library/BrotliCustomDecompressLib/brotli')
METADATA={
    'MdePkg/Library/MipiSysTLib/mipisyst':'https://github.com/MIPI-Alliance/public-mipi-sys-t.git',
    'MdePkg/Library/BaseFdtLib/libfdt':'https://github.com/devicetree-org/pylibfdt.git'}

def git(root,*args):
    return subprocess.check_output(['git','-C',str(root),*args])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--opencore',type=Path,required=True)
    p.add_argument('--edk',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--metadata-only',action='store_true',help='Capture the additional package-header dependencies separately')
    a=p.parse_args()
    if a.output.exists():p.error('Use a new archive path')
    oc=git(a.opencore,'rev-parse','1.0.5^{commit}').decode().strip()
    edk=git(a.edk,'rev-parse','HEAD').decode().strip()
    if oc!=OC_REVISION or edk!=EDK_REVISION:
        p.error('Unexpected source revision')
    receipt={'FormatVersion':1,'OpenCore':{'URL':'https://github.com/acidanthera/OpenCorePkg.git','Revision':oc,'Tag':'1.0.5'},
             'EDK':{'URL':'https://github.com/acidanthera/audk.git','Revision':edk,'Packages':list(PACKAGES)},'Dependencies':{}}
    archives=[] if a.metadata_only else [('OpenCorePkg',git(a.opencore,'archive','--format=tar','1.0.5')),
              ('',git(a.edk,'archive','--format=tar','HEAD',*PACKAGES,'edksetup.sh','License.txt','License-History.txt'))]
    for path in (METADATA if a.metadata_only else CHILDREN):
        expected=git(a.edk,'ls-tree','HEAD',path).decode().split()
        child=a.edk/path
        if len(expected)!=4 or expected[0]!='160000' or git(child,'rev-parse','HEAD').decode().strip()!=expected[2]:
            p.error('Materialize the pinned source dependency first: '+path)
        receipt['Dependencies'][path]={'Revision':expected[2],'URL':METADATA[path] if a.metadata_only else 'https://github.com/google/brotli.git'}
        archives.append((path,git(child,'archive','--format=tar','HEAD')))
    files={};a.output.parent.mkdir(parents=True,exist_ok=True)
    with tarfile.open(a.output,'x:xz',preset=6) as out:
        for prefix,raw in archives:
            with tarfile.open(fileobj=io.BytesIO(raw)) as package:
                for item in package:
                    if not item.isfile():continue # No Git submodule/link placeholders.
                    name=(Path(prefix)/item.name).as_posix()
                    if name in files:raise ValueError('Duplicate source input: '+name)
                    content=package.extractfile(item).read()
                    entry=tarfile.TarInfo(name);entry.size=len(content);entry.mode=item.mode&0o777;entry.mtime=0
                    out.addfile(entry,io.BytesIO(content));files[name]=hashlib.sha256(content).hexdigest()
        receipt['Files']=files
        raw=plistlib.dumps(receipt);entry=tarfile.TarInfo('VIMOpenCoreMetadataSource.plist' if a.metadata_only else 'VIMOpenCoreSource.plist');entry.size=len(raw);entry.mode=0o644
        out.addfile(entry,io.BytesIO(raw))
    print(hashlib.file_digest(a.output.open('rb'),'sha256').hexdigest(),a.output.name)

if __name__=='__main__':main()
