#!/usr/bin/env python3
"""Rebuild quiet RELEASE OpenCore 1.0.5 with the native linkedit-bound fix.

Self-contained pinned source input, shared standard firmware build tools, no
Git checkout/download or administrator access. App/UI iterations use the
checked-in qualified result instead of rebuilding a bootloader.
"""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import plistlib
import runpy
import shutil
import subprocess
import sys
import tarfile
from build_signing import resolve_identity

SOURCE=Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def inputs():
    names=('vendor/opengl-opencore-source.tar.xz','vendor/opengl-opencore-source.sha256',
           'vendor/opengl-opencore-metadata-source.tar.xz','vendor/opengl-opencore-metadata-source.sha256',
           'vendor/patches/opencore-linkedit-file-bounds.patch','scripts/build-opengl-opencore.py',
           'vendor/patches/opencore-hfs-overflow-extents.patch',
           'vendor/patches/opencore-installer-graphics-isolation.patch',
           'vendor/patches/opencore-memory-map-tail-count.patch',
           'compat/PassthroughOpenGLBootPolicy.h',
           'scripts/build-guest-firmware.py','scripts/firmware-host-compiler.py','scripts/build_signing.py')
    return {name:digest(SOURCE/name) for name in names}

def verify(output):
    receipt=plistlib.loads((output/'BuildReceipt.plist').read_bytes())
    if (receipt.get('FormatVersion')!=1 or receipt.get('Configuration')!='RELEASE' or
        receipt.get('OpenCoreVersion')!='1.0.5' or receipt.get('SourceFiles')!=inputs() or
        receipt.get('Files')!={name:digest(output/name) for name in ('OpenCore.efi','OpenHfsPlus.efi')}):
        raise ValueError('Rebuild the source-matched RELEASE OpenCore payload')
    return receipt

def run(*args,**kwargs):
    print('+',' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),check=True,**kwargs)

def platform_text(original):
    # Use the upstream application's library/configuration contract, without
    # building unrelated drivers, networking, a shell or third-party tools.
    # This does not change OcMainLib or any application's boot policy.
    before,components=original.split('[Components]',1)
    _,after=components.split('[LibraryClasses]',1)
    text=before+'[Components]\n  OpenCorePkg/Application/OpenCore/OpenCore.inf\n  OpenCorePkg/Staging/OpenHfsPlus/OpenHfsPlus.inf\n\n[LibraryClasses]'+after
    lines=[]
    for line in text.splitlines():
        if 'FLASH_DEFINITION' in line:continue
        if line.strip().startswith('!include ') and ('NetworkPkg/' in line or 'Ext4Pkg/' in line):continue
        if line.strip().startswith('DEFINE NETWORK_'):
            line=line.split('=')[0]+'= FALSE'
        if line.strip().startswith('CLANGPDB:RELEASE_*_*_CC_FLAGS'):
            # New Clang diagnoses upstream counters whose only reads are
            # DEBUG expressions removed by RELEASE. Keep other -Werror checks.
            line+=' -Wno-unused-but-set-global'
        lines.append(line)
    lines.insert(lines.index('[Defines]')+1,'  OUTPUT_DIRECTORY = Build/VIMOpenCore')
    return '\n'.join(lines)+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,default=SOURCE/'Build/OpenGL-OpenCore')
    p.add_argument('--tools',type=Path,default=SOURCE/'Build/FirmwareTools')
    p.add_argument('--bootstrap-tools',action='store_true')
    p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--verify',type=Path)
    a=p.parse_args()
    if a.verify:verify(a.verify);print('RELEASE OpenCore source and bytes verified');return
    if not 1<=a.jobs<=64:p.error('--jobs must be between 1 and 64')
    initial=inputs();archive=SOURCE/'vendor/opengl-opencore-source.tar.xz'
    fields=(SOURCE/'vendor/opengl-opencore-source.sha256').read_text().split()
    if len(fields)!=2 or fields[1]!=archive.name or fields[0]!=digest(archive):
        p.error('Vendored OpenCore source checksum mismatch')
    work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    lock=(work/'Build.lock').open('a')
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    root=work/'Sources';stamp=root/'.VIMOpenCoreSourceSHA256'
    if root.exists():
        if not stamp.is_file() or stamp.read_text()!=fields[0]:raise ValueError('Use a new --work directory')
    else:
        root.mkdir()
        with tarfile.open(archive,'r:xz') as package:package.extractall(root,filter='data')
        receipt=plistlib.loads((root/'VIMOpenCoreSource.plist').read_bytes())
        if receipt.get('FormatVersion')!=1 or receipt['OpenCore']['Tag']!='1.0.5':raise ValueError('Unexpected source receipt')
        for name,expected in receipt['Files'].items():
            if digest(root/name)!=expected:raise ValueError('Extracted source mismatch: '+name)
        stamp.write_text(fields[0])
    metadata=SOURCE/'vendor/opengl-opencore-metadata-source.tar.xz'
    metadata_fields=(SOURCE/'vendor/opengl-opencore-metadata-source.sha256').read_text().split()
    if len(metadata_fields)!=2 or metadata_fields[1]!=metadata.name or metadata_fields[0]!=digest(metadata):
        raise ValueError('Vendored EDK metadata dependency checksum mismatch')
    metadata_stamp=root/'.VIMOpenCoreMetadataSHA256'
    if metadata_stamp.exists():
        if metadata_stamp.read_text()!=metadata_fields[0]:raise ValueError('Metadata inputs changed; use a new --work directory')
    else:
        with tarfile.open(metadata,'r:xz') as package:
            info=plistlib.loads(package.extractfile('VIMOpenCoreMetadataSource.plist').read())
            base_info=plistlib.loads((root/'VIMOpenCoreSource.plist').read_bytes())
            if info.get('EDK')!=base_info.get('EDK'):raise ValueError('EDK and metadata snapshots must match')
            for name,expected in info['Files'].items():
                if not name.startswith(('MdePkg/Library/MipiSysTLib/mipisyst/','MdePkg/Library/BaseFdtLib/libfdt/')):
                    raise ValueError('Unexpected metadata dependency path')
                if (root/name).exists():raise ValueError('Metadata source would overwrite an existing input')
            package.extractall(root,filter='data')
        for name,expected in info['Files'].items():
            if digest(root/name)!=expected:raise ValueError('Metadata source mismatch: '+name)
        metadata_stamp.write_text(metadata_fields[0])
    patch=SOURCE/'vendor/patches/opencore-linkedit-file-bounds.patch'
    patched=root/'.VIMOpenCorePatchSHA256'
    if patched.exists():
        if patched.read_text()!=digest(patch):raise ValueError('Patch changed; use a new --work directory')
    else:
        run('/usr/bin/patch','-p1','--batch','--forward','-i',patch,cwd=root/'OpenCorePkg')
        patched.write_text(digest(patch))
    hfs_patch=SOURCE/'vendor/patches/opencore-hfs-overflow-extents.patch'
    hfs_stamp=root/'.VIMHfsOverflowPatchSHA256'
    if hfs_stamp.exists():
        if hfs_stamp.read_text()!=digest(hfs_patch):raise ValueError('HFS patch changed; use a new --work directory')
    else:
        run('/usr/bin/patch','-p1','--batch','--forward','-i',hfs_patch,cwd=root/'OpenCorePkg')
        hfs_stamp.write_text(digest(hfs_patch))
    isolation_patch=SOURCE/'vendor/patches/opencore-installer-graphics-isolation.patch'
    isolation_stamp=root/'.VIMInstallerIsolationPatchSHA256'
    if isolation_stamp.exists():
        if isolation_stamp.read_text()!=digest(isolation_patch):raise ValueError('Installer isolation patch changed; use a new --work directory')
    else:
        run('/usr/bin/patch','-p1','--batch','--forward','-i',isolation_patch,cwd=root/'OpenCorePkg')
        isolation_stamp.write_text(digest(isolation_patch))
    shutil.copyfile(SOURCE/'compat/PassthroughOpenGLBootPolicy.h',root/'OpenCorePkg/Library/OcMainLib/VIMOpenGLBootPolicy.h')
    if (root/'.VIMAppleMemoryMapPatchSHA256').exists():
        raise ValueError('Use a new --work directory to retire the superseded memory-map cleanup prototype')
    memory_patch=SOURCE/'vendor/patches/opencore-memory-map-tail-count.patch'
    memory_stamp=root/'.VIMMemoryMapTailCountPatchSHA256'
    if memory_stamp.exists():
        if memory_stamp.read_text()!=digest(memory_patch):raise ValueError('Memory map patch changed; use a new --work directory')
    else:
        run('/usr/bin/patch','-p1','--batch','--forward','-i',memory_patch,cwd=root/'OpenCorePkg')
        memory_stamp.write_text(digest(memory_patch))
    builder=runpy.run_path(str(SOURCE/'scripts/build-guest-firmware.py'))
    replace=builder['replace_once']
    replace(root,'BaseTools/Source/C/VfrCompile/GNUmakefile','APPNAME = VfrCompile','.NOTPARALLEL:\n\nAPPNAME = VfrCompile')
    (root/'OpenCorePkg/VIMOpenCore.dsc').write_text(platform_text((root/'OpenCorePkg/OpenCorePkg.dsc').read_text()))
    (root/'Conf').mkdir(exist_ok=True)
    llvm,nasm,iasl=builder['prepare_tools'](a.tools.resolve(),a.jobs,a.bootstrap_tools)
    env=dict(os.environ,PYTHON_COMMAND=sys.executable,PYTHONHASHSEED='1',WORKSPACE=str(root),
        CLANG_BIN=str(llvm/'bin')+'/',NASM_PREFIX=str(nasm.parent)+'/',IASL_PREFIX=str(iasl.parent)+'/',SOURCE_DATE_EPOCH='1')
    env['PATH']=str(Path(sys.executable).parent)+os.pathsep+env.get('PATH','')
    env['VIM_FIRMWARE_TOOL_IDENTITY']=resolve_identity(SOURCE)
    import shlex
    compiler=shlex.join((sys.executable,str(SOURCE/'scripts/firmware-host-compiler.py')))
    run('make','-C',root/'BaseTools/Source/C',f'-j{a.jobs}',
        'CC='+compiler+' clang','CXX='+compiler+' clang++',env=env)
    # Acidanthera's PE image converter is separate from stock BaseTools C.
    # Its compiler feature probes quote CC as one executable, so provide a
    # generated launcher rather than a multi-word compiler command.
    host_cc=work/'HostClang'
    host_cc.write_text('#!/bin/sh\nexec '+compiler+' clang "$@"\n')
    host_cc.chmod(0o755)
    run('make','-C',root/'BaseTools/ImageTool',f'-j{a.jobs}','CC='+str(host_cc),env=env)
    # The upstream native makefile strips after linking; sign the final file.
    run('/usr/bin/codesign','--force','--sign',env['VIM_FIRMWARE_TOOL_IDENTITY'],root/'BaseTools/ImageTool/ImageTool')
    run('/usr/bin/codesign','--verify','--strict',root/'BaseTools/ImageTool/ImageTool')
    run('/bin/bash','-c','build_args=("$@"); set --; . ./edksetup.sh && build "${build_args[@]}"',
        'build-opengl-opencore','-a','X64','-b','RELEASE','-t','CLANGPDB','-n',a.jobs,
        '-p','OpenCorePkg/VIMOpenCore.dsc',cwd=root,env=env)
    output=work/'Output';output.mkdir(exist_ok=True)
    shutil.copyfile(root/'Build/VIMOpenCore/RELEASE_CLANGPDB/X64/OpenCore.efi',output/'OpenCore.efi')
    shutil.copyfile(root/'Build/VIMOpenCore/RELEASE_CLANGPDB/X64/OpenHfsPlus.efi',output/'OpenHfsPlus.efi')
    source=plistlib.loads((root/'VIMOpenCoreSource.plist').read_bytes())
    metadata_source=plistlib.loads((root/'VIMOpenCoreMetadataSource.plist').read_bytes())
    if initial!=inputs():raise ValueError('Inputs changed during the build; rerun')
    (output/'BuildReceipt.plist').write_bytes(plistlib.dumps({'FormatVersion':1,'Configuration':'RELEASE',
        'OpenCoreVersion':'1.0.5','SourceFiles':initial,'Upstream':{key:source[key] for key in ('OpenCore','EDK','Dependencies')},
        'MetadataDependencies':metadata_source['Dependencies'],
        'Files':{name:digest(output/name) for name in ('OpenCore.efi','OpenHfsPlus.efi')}}))
    verify(output);print(output)

if __name__=='__main__':main()
