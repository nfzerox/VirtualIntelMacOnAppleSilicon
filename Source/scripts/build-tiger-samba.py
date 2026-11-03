#!/usr/bin/env python3
"""Build/reuse Tiger's native SMB compatibility component without installing it.

Ordinary builds use the self-contained prebuilt. --rebuild uses the genuine
10.4u SDK and period Intel compiler/linker; no guest Xcode or downloaded source.
--from-build publishes an already qualified native build without recompiling.
"""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

FILES=('tiger-smbd','tiger-opendirectory.so','Samba-COPYING.txt')

def verify(directory,source=None):
    record=plistlib.loads((directory/'TigerSMB.plist').read_bytes())
    if (record.get('FormatVersion')!=1 or record.get('Guest')!='tiger' or
        record.get('PolicyVersion')!=2 or set(record.get('Files',{}))!=set(FILES)):
        raise ValueError('Tiger native file-sharing payload is incomplete')
    for name in FILES:
        path=directory/name
        if not path.is_file() or not path.stat().st_size:
            raise ValueError('Missing Tiger file-sharing component: '+name)
        if name.endswith('.txt'):continue
        if subprocess.check_output(['lipo','-archs',str(path)],text=True).split()!=['i386']:
            raise ValueError('Tiger file sharing requires the native i386 ABI: '+name)
        subprocess.run(['codesign','--verify','--strict',str(path)],check=True)
    return record

def publish(build,output,source):
    output.mkdir(parents=True,exist_ok=False)
    for name,origin in (('tiger-smbd','smbd'),('tiger-opendirectory.so','opendirectory.so')):
        path=output/name;shutil.copyfile(build/'bin'/origin,path);path.chmod(0o755)
        subprocess.run(['strip','-S',str(path)],check=True)
        subprocess.run(['codesign','--force','--sign','-','--digest-algorithm=sha1,sha256',str(path)],check=True)
    shutil.copyfile(source/'vendor/source/samba-100.9/samba/COPYING',output/'Samba-COPYING.txt')
    record={'FormatVersion':1,'Guest':'tiger','PolicyVersion':2,
            'UpstreamCommit':'55b54999e108fc287b6dfd5e8e5a06a60d4bc6c0',
            'Files':{name:True for name in FILES}}
    (output/'TigerSMB.plist').write_bytes(plistlib.dumps(record))
    verify(output,source)

def main():
    source=Path(__file__).resolve().parents[1]
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','cc','build','from-build','verify'):p.add_argument('--'+name,type=Path)
    p.add_argument('--output',type=Path,default=source/'Build/TigerSMB')
    p.add_argument('--rebuild',action='store_true')
    p.add_argument('--jobs',type=int,default=4)
    a=p.parse_args()
    if a.verify:verify(a.verify,source);print('Tiger native SMB payload verified');return
    if a.from_build:
        if a.rebuild:p.error('Choose --rebuild or --from-build')
        publish(a.from_build.resolve(),a.output.resolve(),source)
    elif a.rebuild:
        if not all((a.sdk,a.cc,a.build)):p.error('--rebuild requires --sdk, --cc and a new --build')
        subprocess.run([sys.executable,str(source/'scripts/tiger_samba_build.py'),
            '--sdk',str(a.sdk.resolve()),'--cc',str(a.cc.resolve()),
            '--output',str(a.build.resolve()),'--jobs',str(a.jobs),'--auth-module'],check=True)
        publish(a.build.resolve(),a.output.resolve(),source)
    else:
        prebuilt=source/'prebuilt/TigerSMB';verify(prebuilt,source)
        if a.output.resolve()!=prebuilt.resolve():shutil.copytree(prebuilt,a.output)
    print(a.output)

if __name__=='__main__':main()
