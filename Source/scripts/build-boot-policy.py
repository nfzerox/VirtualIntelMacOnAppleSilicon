#!/usr/bin/env python3
"""Build the small guest EFI policy driver and boot-argument-gated GL registrar."""
import argparse
import hashlib
import os
import platform
from pathlib import Path
import plistlib
import runpy
import shutil
import subprocess
import sys
import build_freshness as freshness

SOURCE=Path(__file__).resolve().parents[1]
def run(*args,**kwargs):
    print('+',' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),check=True,**kwargs)
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def inputs():
    paths=[*sorted((SOURCE/'compat/BootPolicy').glob('*')),
        SOURCE/'compat/PVGOpenGLProbe/PVGOpenGLProbe.cpp',SOURCE/'compat/PVGOpenGLProbe/kmod_info.c',
        SOURCE/'compat/PVGOpenGLProbe/Info.plist',Path(__file__).resolve()]
    return {str(p.relative_to(SOURCE)):digest(p) for p in paths if p.is_file()}
def fingerprint():
    return freshness.identity({'Sources':inputs(), 'FirmwareRecipe':freshness.snapshot(SOURCE,'firmware'),
                               'KernelSDK':freshness.manifest([('MacKernelSDK',SOURCE/'vendor/source/MacKernelSDK')])})
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,default=SOURCE/'Build/BootPolicy')
    parser.add_argument('--tools',type=Path,default=SOURCE/'Build/FirmwareTools')
    parser.add_argument('--bootstrap-tools',action='store_true')
    parser.add_argument('--jobs',type=int,default=4)
    args=parser.parse_args()
    initial_fingerprint=fingerprint()
    if not 1<=args.jobs<=64:parser.error('--jobs must be between 1 and 64')
    from firmware_source import extract
    args.work=args.work.resolve();args.work.mkdir(parents=True,exist_ok=True)
    root=args.work/'edk2';edk=extract(SOURCE,root)
    package=root/'VIMBootPolicy';shutil.copytree(SOURCE/'compat/BootPolicy',package,dirs_exist_ok=True)
    builder=runpy.run_path(str(SOURCE/'scripts/build-guest-firmware.py'))
    builder['patch_sources'](root)
    llvm,nasm,iasl=builder['prepare_tools'](args.tools.resolve(),args.jobs,args.bootstrap_tools)
    env=dict(os.environ,PYTHON_COMMAND=sys.executable,PYTHONHASHSEED='1',WORKSPACE=str(root),
        CLANG_BIN=str(llvm/'bin')+'/',NASM_PREFIX=str(nasm.parent)+'/',IASL_PREFIX=str(iasl.parent)+'/')
    env['PATH']=str(Path(sys.executable).parent)+os.pathsep+env.get('PATH','')
    run('make','-C',root/'BaseTools/Source/C',f'-j{args.jobs}',
        'CC=/usr/bin/xcrun --sdk macosx clang','CXX=/usr/bin/xcrun --sdk macosx clang++',env=env)
    run('/bin/bash','-c','build_args=("$@"); set --; . ./edksetup.sh && build "${build_args[@]}"',
        'build-boot-policy','-a','X64','-b','RELEASE','-t','CLANGPDB','-n',args.jobs,
        '-p','VIMBootPolicy/BootPolicy.dsc',cwd=root,env=env)
    headers=root/'MdePkg/Include'
    fixture=args.work/'test-policy'
    run('xcrun','clang','-O2','-Wall','-Wextra','-fshort-wchar','-Wno-macro-redefined',
        '-I'+str(headers),'-I'+str(headers/('AArch64' if platform.machine()=='arm64' else 'X64')),
        SOURCE/'probes/efi-boot-policy.c','-o',fixture)
    run(fixture)
    output=args.work/'Output';output.mkdir(exist_ok=True)
    shutil.copyfile(root/'Build/VIMBootPolicy/RELEASE_CLANGPDB/X64/VIMBootPolicy.efi',output/'VIMBootPolicy.efi')
    # Current Xcode plus vendored kernel ABI headers; no target-era SDK needed.
    sdk=SOURCE/'vendor/source/MacKernelSDK'
    flags=['-arch','x86_64','-mmacosx-version-min=11.0','-nostdinc','-I'+str(sdk/'Headers'),
        '-DKERNEL','-DKERNEL_PRIVATE','-DDRIVER_PRIVATE','-DAPPLE','-DNeXT',
        '-DPVG_RUNTIME_BOOT_POLICY=1','-mkernel','-fno-builtin','-fno-common','-O2']
    run('xcrun','clang++',*flags,'-std=gnu++14','-fno-exceptions','-fno-rtti','-fapple-kext',
        '-c',SOURCE/'compat/PVGOpenGLProbe/PVGOpenGLProbe.cpp','-o',args.work/'driver.o')
    run('xcrun','clang',*flags,'-c',SOURCE/'compat/PVGOpenGLProbe/kmod_info.c','-o',args.work/'info.o')
    product=output/'PVGOpenGLProbe.kext/Contents';(product/'MacOS').mkdir(parents=True,exist_ok=True)
    run('xcrun','ld','-arch','x86_64','-kext','-static','-macosx_version_min','11.0',
        '-o',product/'MacOS/PVGOpenGLProbe',args.work/'driver.o',args.work/'info.o','-L'+str(sdk/'Library/x86_64'),'-lkmod')
    info=plistlib.loads((SOURCE/'compat/PVGOpenGLProbe/Info.plist').read_bytes())
    info['OSBundleLibraries']['com.apple.kpi.unsupported']='11.0.0'
    (product/'Info.plist').write_bytes(plistlib.dumps(info))
    run('codesign','--force','--sign','-',product.parent)
    run('codesign','--verify','--deep','--strict',product.parent)
    hashes={str(p.relative_to(output)):digest(p) for p in sorted(output.rglob('*')) if p.is_file() and p.name!='BuildReceipt.plist'}
    if fingerprint()!=initial_fingerprint:raise RuntimeError('EFI policy inputs changed during build; rerun')
    (output/'BuildReceipt.plist').write_bytes(plistlib.dumps({'FormatVersion':1,'Configuration':'RELEASE',
        'BuildFingerprint':initial_fingerprint,
        'RuntimePolicyProtocol':1,'SourceFiles':inputs(),'EDKSource':edk,'Hashes':hashes}))
    print('Built boot-time policy components:',output)
if __name__=='__main__':main()
