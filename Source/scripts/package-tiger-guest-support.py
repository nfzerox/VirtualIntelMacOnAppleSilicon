#!/usr/bin/env python3
"""Publish Tiger's native controls and measured graphics payload.

This preserves the working implementation in ordinary builds. No borrowed stock framework,
probe executable, developer tool, trace library or test-only override ships.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import shutil


def verify_graphics(directory, source, receipt='BuildReceipt.plist'):
    record=plistlib.loads((directory/receipt).read_bytes())
    if (record.get('DiagnosticOnly') or record.get('FormatVersion')!=1 or record.get('Architecture')!='i386' or
        record.get('MinimumOS')!='10.4' or record.get('DispatchCount')!=759 or record.get('NativePrebootInstallVersion')!=1 or
        record.get('NativeDispatchCounts')!=[686,759] or
        not isinstance(record.get('NativeEngineSHA256'),str) or len(record['NativeEngineSHA256'])!=64):
        raise ValueError('Tiger graphics must match the measured native ABI')
    expected={'GLEngine','libVIMPassthroughOpenGL.dylib','opengl-tiger-graphics',
              'VMsvga2GA.plugin/Contents/Info.plist','VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA'}
    if any(not (directory/p).is_file() for p in expected):
        raise ValueError('Tiger graphics files are incomplete')
    return {key:record[key] for key in ('FormatVersion','Architecture','MinimumOS',
        'DispatchCount','NativeDispatchCounts','NativeEngineSHA256','NativePrebootInstallVersion')},expected


def verify_audio(directory, source):
    recipe=source/'scripts/build-opengl-kernel.py'
    record=plistlib.loads((directory/'BuildReceipt.plist').read_bytes())
    expected={'VoodooHDA.kext/Contents/Info.plist','VoodooHDA.kext/Contents/MacOS/VoodooHDA'}
    if (record.get('FormatVersion')!=1 or record.get('SDKVersion')!='10.4' or
        record.get('KernelArchitecture')!='i386' or record.get('AudioOnly') is not True or
        any(not (directory/p).is_file() for p in expected)):
        raise ValueError('Rebuild Tiger audio against its genuine native SDK before packaging')
    runpy.run_path(str(recipe))['verify_period_object'](directory/'VoodooHDA.kext/Contents/MacOS/VoodooHDA','10.4')
    info=plistlib.loads((directory/'VoodooHDA.kext/Contents/Info.plist').read_bytes())
    if (info.get('CFBundleIdentifier')!='org.voodoo.driver.VoodooHDA' or
        info.get('IOKitPersonalities',{}).get('VoodooHDA',{}).get('IOPCIPrimaryMatch')!='0x26688086'):
        raise ValueError('Tiger audio must match only the virtual Intel HDA controller')
    return {'FormatVersion':1,'SDKVersion':'10.4','KernelArchitecture':'i386','AudioOnly':True,
            'Files':{name:True for name in expected}}


def verify(directory, source, packaged=False):
    manifest=plistlib.loads((directory/'Support.plist').read_bytes())
    if (manifest.get('FormatVersion')!=1 or manifest.get('Guest')!='tiger' or
        manifest.get('RequiresGuestDeveloperTools') is not False):
        raise ValueError('Tiger support has an invalid native payload contract')
    controls=runpy.run_path(str(source/'scripts/build-tiger-guest-tools.py'))
    tools=directory if packaged else directory/'GuestTools'
    controls['verify'](tools,source)
    verify_graphics(tools,source,'Graphics.plist')
    if not packaged:
        runpy.run_path(str(source/'scripts/build-tiger-installer-firmware.py'))['verify_graphics_kernel'](directory/'Kernel',source)
    if manifest.get('HasNativeAudioPayload'):
        verify_audio(directory/'Audio',source)
    return manifest


def publish_runtime(directory, output, source):
    manifest=verify(directory,source)
    shutil.copytree(directory/'GuestTools',output,ignore=shutil.ignore_patterns('.DS_Store'))
    shutil.copyfile(directory/'Support.plist',output/'Support.plist')
    if manifest.get('HasNativeAudioPayload'):
        shutil.copytree(directory/'Audio',output/'Audio',ignore=shutil.ignore_patterns('.DS_Store'))
    verify(output,source,packaged=True)
    return manifest


def build(controls, graphics, kernel, output, source, audio=None):
    control_recipe=runpy.run_path(str(source/'scripts/build-tiger-guest-tools.py'))
    control_recipe['verify'](controls,source)
    graphic_record,names=verify_graphics(graphics,source)
    kernel_record=runpy.run_path(str(source/'scripts/build-tiger-installer-firmware.py'))['verify_graphics_kernel'](kernel,source)
    if audio:verify_audio(audio,source)
    output.mkdir(parents=True,exist_ok=False)
    shutil.copytree(controls,output/'GuestTools',ignore=shutil.ignore_patterns('.DS_Store','guest-macho'))
    control_record={'FormatVersion':1,'Architecture':'i386','MinimumOS':'10.4',
                    'RequiresGuestDeveloperTools':False,'Files':plistlib.loads((controls/'BuildReceipt.plist').read_bytes())['Files']}
    (output/'GuestTools/BuildReceipt.plist').write_bytes(plistlib.dumps(control_record))
    for name in sorted(names):
        path=output/'GuestTools'/name;path.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(graphics/name,path)
        path.chmod(0o644 if name.endswith('.plist') else 0o755)
    # Remove the diagnostic probe from the distributable receipt as well as
    # its files. The native-engine value is only a digest, not a donated file.
    graphic_record['Files']={name:True for name in sorted(names)}
    (output/'GuestTools/Graphics.plist').write_bytes(plistlib.dumps(graphic_record))
    for name in kernel_record['Files']:
        path=output/'Kernel'/name;path.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(kernel/name,path);path.chmod(0o644 if name.endswith('.plist') else 0o755)
    (output/'Kernel/BuildReceipt.plist').write_bytes(plistlib.dumps(kernel_record))
    if audio:
        for name in (*sorted(verify_audio(audio,source)['Files']),'BuildReceipt.plist'):
            path=output/'Audio'/name;path.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(audio/name,path);path.chmod(0o644 if name.endswith('.plist') else 0o755)
        (output/'Audio/BuildReceipt.plist').write_bytes(plistlib.dumps(verify_audio(audio,source)))
    manifest={'FormatVersion':1,'Guest':'tiger','Architecture':'i386','MinimumOS':'10.4',
              'RequiresGuestDeveloperTools':False,'HasNativeAudioPayload':audio is not None}
    (output/'Support.plist').write_bytes(plistlib.dumps(manifest))
    verify(output,source);return output


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('controls','graphics','kernel','audio','output','verify'):
        p.add_argument('--'+name,type=Path)
    a=p.parse_args();source=Path(__file__).resolve().parents[1]
    if a.verify:
        verify(a.verify,source);print('Tiger native support ABI and required files checked');return
    if not all((a.controls,a.graphics,a.kernel,a.output)):
        p.error('Supply explicit --controls, --graphics, --kernel and a new --output')
    print(build(a.controls,a.graphics,a.kernel,a.output,source,a.audio))


if __name__=='__main__':
    main()
