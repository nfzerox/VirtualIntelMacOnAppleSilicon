#!/usr/bin/env python3
"""Cross-build the owned framebuffer/accelerator with a period SDK.

Uses the vendored Xcode project's actual source phases and period configuration
definitions. No Xcode app launch, reference-Mac mutation or kext installation.
The caller supplies a compatible Apple compiler and matching kernel SDK.
"""
import argparse
import json
from pathlib import Path
import plistlib
import shlex
import struct
import subprocess


def verify_period_object(path,version):
    """Darwin8/9 consume native32-bit relocatables, not later kext bundles."""
    data=path.read_bytes()
    if len(data)<28:raise ValueError('Truncated Leopard kernel object')
    magic,cpu,subtype,kind,count,size,flags=struct.unpack_from('<7I',data)
    if magic!=0xfeedface or cpu!=7 or kind!=1 or not 1<=count<=16 or 28+size>len(data):
        raise ValueError('Leopard requires an i386 MH_OBJECT extension')
    cursor=28;commands=[]
    for _ in range(count):
        if cursor+8>28+size:raise ValueError('Truncated Leopard load command')
        command,length=struct.unpack_from('<II',data,cursor)
        if length<8 or length%4 or cursor+length>28+size:raise ValueError('Invalid Leopard load command')
        if command not in ((1,2) if version=='10.4' else (1,2,0x1b)):
            raise ValueError('Unsupported Darwin9 kld load command '+hex(command)+'; use the period assembler/linker via --binutils')
        if command==1:
            if length<56:raise ValueError('Truncated Leopard segment')
            sections=struct.unpack_from('<I',data,cursor+48)[0]
            if 56+68*sections!=length:raise ValueError('Invalid Leopard section table')
            for index in range(sections):
                section=cursor+56+68*index
                if data[section:section+16].rstrip(b'\0')!=b'__const':continue
                offset,entries=struct.unpack_from('<II',data,section+48)
                if offset+8*entries>len(data):raise ValueError('Truncated Leopard relocations')
                for entry in range(entries):
                    address,bits=struct.unpack_from('<II',data,offset+8*entry)
                    if address&0x80000000 or bits>>28!=0 or (bits>>25)&3!=2 or (not bits&0x08000000 and not bits&0x00ffffff):
                        raise ValueError('Darwin9 C++ vtable linker cannot restore skipped __const relocations; compile without switch jump tables')
        commands.append(command);cursor+=length
    if cursor!=28+size or 1 not in commands or commands.count(2)!=1:
        raise ValueError('Incomplete Leopard kernel object')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--cc',type=Path,required=True)
    p.add_argument('--binutils',type=Path,help='Explicit assembler/linker directory for the period GCC driver')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--audio-only',action='store_true',help='Build the vendored VoodooHDA against the same explicit period SDK')
    a=p.parse_args()
    a.sdk=a.sdk.resolve();a.cc=a.cc.resolve();a.output=a.output.resolve()
    if a.output.exists():p.error('Use a new output directory')
    settings=plistlib.loads((a.sdk/'SDKSettings.plist').read_bytes())
    version=settings['Version']
    if version not in ('10.4','10.5','10.6','10.7','10.8','10.9','10.10','10.11','10.12','10.13'):p.error('Explicitly supported kernel SDKs: 10.4 through 10.13')
    relocatable=version in ('10.4','10.5')
    architecture='i386' if relocatable else 'x86_64'
    compiler=subprocess.check_output([str(a.cc),'--version'],text=True)
    gcc='(GCC)' in compiler and 'clang' not in compiler
    if relocatable and not gcc:
        p.error('Darwin8/9 i386 C++ kernels require the period Apple GCC toolchain; clang rejects that kernel ABI')
    architecture_flags=['-m32' if architecture=='i386' else '-m64'] if gcc else ['-arch',architecture]
    tool_flags=[]
    if a.binutils:
        a.binutils=a.binutils.resolve()
        if not all((a.binutils/name).is_file() for name in ('as','ld')):p.error('--binutils must contain as and ld')
        tool_flags=['-B'+str(a.binutils)+'/']
    compiler_runtime=None
    if relocatable:
        if not a.binutils:p.error('Tiger/Leopard require an explicit period assembler/linker via --binutils')
        # Darwin9 does not export GCC's 64-bit division/modulus helpers.
        # Use the compiler's own static runtime, selecting only needed archive
        # members; never link libSystem or invent a newer kernel dependency.
        compiler_runtime=Path(subprocess.check_output([str(a.cc),'-m32','-mmacosx-version-min='+version,'-print-file-name=libgcc_static.a'],text=True).strip())
        if not compiler_runtime.is_file():p.error('The period compiler static runtime is missing')
    source=Path(__file__).resolve().parents[1]/('vendor/source/VoodooHDA/tranc' if a.audio_only else 'vendor/source/VMsvga2')
    project=source/('VoodooHDA.xcodeproj/project.pbxproj' if a.audio_only else 'VMsvga2.xcodeproj/project.pbxproj')
    objects=json.loads(subprocess.check_output(['plutil','-convert','json','-o','-',str(project)]))['objects']
    parents={child:key for key,item in objects.items() if item.get('isa')=='PBXGroup' for child in item.get('children',[])}
    def path(key):
        item=objects[key]
        if item.get('sourceTree') not in ('<group>',None):raise ValueError('Unsupported source tree')
        return (path(parents[key]) if key in parents else source)/item.get('path','')
    a.output.mkdir(parents=True)
    kernel=a.sdk/'System/Library/Frameworks/Kernel.framework'
    common=[*architecture_flags,*tool_flags,'-isysroot',str(a.sdk),'-mmacosx-version-min='+version,
        '-nostdinc','-fno-builtin','-fno-exceptions','-fno-rtti','-msoft-float','-Os',
        '-fno-common','-mkernel','-fapple-kext','-fasm-blocks','-fstrict-aliasing',
        '-DKERNEL','-DKERNEL_PRIVATE','-DDRIVER_PRIVATE','-DAPPLE','-DNeXT','-D__STDC_CONSTANT_MACROS']
    if relocatable:
        # Native Darwin9's C++ vtable linker pseudo-relocates __const, then
        # restores every relocation in that section. It skips scattered
        # switch-table records on the first pass but dereferences their null
        # cache on the second (actual 9J3050 IOKit prepare_for_link+0xcd).
        # Keep equivalent switch branches in code, not address-addend tables
        # beside the vtables. Do not patch Apple's loader or remove records.
        common+=['-fno-jump-tables']
    if version=='10.4':
        # The genuine SDK's IOFramebuffer declaration spells its in-class
        # probe as IOFramebuffer::probe. GCC4.0 accepted that; GCC4.2 requires
        # permissive parsing. Do not edit or borrow a later kernel header.
        common+=['-fpermissive']
        if a.audio_only:
            # Use the vendored driver's explicit native Tiger implementation,
            # not its Darwin9+ dependencies or a graphics-only compatibility
            # header. This is an explicit build, not runtime qualification.
            common+=['-DTIGER']
        else:
            common+=['-include',str(source/'vminclude/vim_tiger_kernel_compat.h')]
    for directory in (kernel/'PrivateHeaders',kernel/'Headers',source/'FB',source/'AC',source/'AC/UC',source/'vmutil',source/'vminclude'):
        common+=['-I',str(directory)]
    def run(command):
        print(shlex.join(map(str,command)),flush=True)
        subprocess.run(list(map(str,command)),check=True)
    for name in (('VoodooHDA',) if a.audio_only else ('VMsvga2','VMsvga2Accel')):
        target=next(item for item in objects.values() if item.get('isa')=='PBXNativeTarget' and item['name']==name)
        configs=objects[target['buildConfigurationList']]['buildConfigurations']
        configuration='Release' if a.audio_only else 'ReleaseLeo' if relocatable else 'ReleaseSnowLeo'
        config=next(objects[key]['buildSettings'] for key in configs if objects[key]['name']==configuration)
        flags=common+(config['OTHER_CFLAGS'] if a.audio_only else ['-D'+value for value in config['GCC_PREPROCESSOR_DEFINITIONS']])
        work=a.output/'Objects'/name;work.mkdir(parents=True)
        inputs=[]
        for phase in target['buildPhases']:
            phase=objects[phase]
            if phase['isa']!='PBXSourcesBuildPhase':continue
            for key in phase['files']:
                entry=objects[key];unit=path(entry['fileRef']);output=work/(unit.stem+'.o')
                extra=shlex.split(entry.get('settings',{}).get('COMPILER_FLAGS',''))
                if gcc:
                    # Apple GCC 4.2 does not implement this later diagnostic.
                    # Remove only its suppression, not any kernel ABI flags.
                    extra=[flag for flag in extra if flag!='-Wno-array-bounds']
                run([a.cc,*flags,*extra,'-c',unit,'-o',output]);inputs.append(output)
        identifier=config['MODULE_NAME'];module=work/'module.c';module_version=config.get('CURRENT_PROJECT_VERSION','1.2.5')
        module.write_text(('#include <mach/vm_types.h>\n' if relocatable else '')+'#include <mach/kmod.h>\n'
            'extern kern_return_t _start(kmod_info_t *,void *);\n'
            'extern kern_return_t _stop(kmod_info_t *,void *);\n'
            f'KMOD_EXPLICIT_DECL({identifier},"{module_version}",_start,_stop)\n'
            '__private_extern__ kmod_start_func_t *_realmain = 0;\n'
            '__private_extern__ kmod_stop_func_t *_antimain = 0;\n'
            '__private_extern__ int _kext_apple_cc = __APPLE_CC__;\n')
        metadata=work/'module.o';run([a.cc,*flags,'-c',module,'-o',metadata])
        bundle=a.output/(name+'.kext')/'Contents';(bundle/'MacOS').mkdir(parents=True)
        # The supplied 9L30 IOGraphicsFamily is MH_OBJECT/i386. Darwin9
        # consumes relocatable extensions, not Snow's MH_KEXT_BUNDLE format;
        # its SDK also has no libcc_kext. Do not fake a newer bundle type.
        run([a.cc,*architecture_flags,*tool_flags,'-isysroot',a.sdk,'-mmacosx-version-min='+version,
            '-Xlinker','-r' if relocatable else '-kext',
            *(['-Xlinker','-no_uuid'] if version=='10.4' else []),'-nostdlib',*inputs,'-lkmodc++',metadata,'-lkmod',
            *([compiler_runtime] if relocatable else ['-lcc_kext']),
            '-o',bundle/'MacOS'/name])
        if relocatable:verify_period_object(bundle/'MacOS'/name,version)
        substitutions={'EXECUTABLE_NAME':name,'PRODUCT_NAME':name,'MODULE_NAME':identifier,
            'MODULE_VERSION':module_version,'DYLIB_COMPATIBILITY_VERSION':module_version,
            'PRODUCT_BUNDLE_IDENTIFIER':identifier,'MARKETING_VERSION':module_version,'CURRENT_PROJECT_VERSION':module_version,
            'COPYRIGHT_NOTICE':'Copyright 2009-2013 Zenith432'}
        text=(source/config['INFOPLIST_FILE']).read_text()
        for key,value in substitutions.items():text=text.replace('${'+key+'}',value).replace('$('+key+')',value)
        if '${' in text or '$(' in text:raise ValueError('Unexpanded kext metadata')
        info=plistlib.loads(text.encode())
        if a.audio_only:
            # The app presents QEMU's ICH6 HDA controller (8086:2668).
            # 10.8 AppleHDAController scores 1 and otherwise claims this PCI
            # device before VoodooHDA, then fails on QEMU's generic codec.
            # Prefer our period-built driver only on that virtual controller;
            # do not disable or patch Apple's entire native HDA stack.
            personality=info['IOKitPersonalities']['VoodooHDA']
            personality['IOPCIPrimaryMatch']='0x26688086'
            personality['IOProbeScore']=1000
            if version=='10.4':
                for key in tuple(info['OSBundleLibraries']):
                    if key.startswith('com.apple.kpi.'):
                        info['OSBundleLibraries'][key]='8.0.0'
        (bundle/'Info.plist').write_bytes(plistlib.dumps(info))
        run(['lipo',bundle/'MacOS'/name,'-verify_arch',architecture])
    receipt={'FormatVersion':1,'SDKVersion':version,'AudioOnly':a.audio_only,
        'KernelArchitecture':architecture,
        'Files':{str(f.relative_to(a.output)):True
            for f in sorted(a.output.glob('*.kext/Contents/**/*')) if f.is_file()}}
    (a.output/'BuildReceipt.plist').write_bytes(plistlib.dumps(receipt))
    print(a.output)


if __name__=='__main__':main()
