#!/usr/bin/env python3
"""Create receipted quiet Tiger firmware, native-only by default.

An explicit research graphics candidate must match its genuine10.4 build
receipt. No drivers or kernel patches are inherited from another OS. A
successful build is not display/acceleration or installation qualification.
"""
import argparse
import hashlib
from pathlib import Path
import plistlib
import runpy
import struct
import tempfile


def verify_graphics_kernel(candidate, source):
    graphics=plistlib.loads((candidate/'BuildReceipt.plist').read_bytes())
    recipe=source/'scripts/build-opengl-kernel.py'
    builder=runpy.run_path(str(recipe))
    if (graphics.get('FormatVersion')!=1 or graphics.get('SDKVersion')!='10.4' or
        graphics.get('AudioOnly') is not False or graphics.get('KernelArchitecture')!='i386'):
        raise ValueError('Rebuild the explicit genuine Tiger graphics candidate')
    for name in ('VMsvga2','VMsvga2Accel'):
        builder['verify_period_object'](candidate/(name+'.kext/Contents/MacOS/'+name),'10.4')
    return {'FormatVersion':1,'SDKVersion':'10.4','AudioOnly':False,'KernelArchitecture':'i386',
            'Files':{str(p.relative_to(candidate)):True for p in sorted(candidate.glob('*.kext/Contents/**/*')) if p.is_file()}}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware',type=Path,required=True,help='Quiet RELEASE app firmware directory')
    p.add_argument('--output',type=Path,required=True,help='New derived output directory')
    p.add_argument('--graphics-kernel',type=Path,help='Explicit genuine10.4 framebuffer/accelerator research build; never used implicitly')
    a=p.parse_args()
    if a.output.exists():p.error('Use a new output directory')
    source=Path(__file__).resolve().parents[1]
    receipt=plistlib.loads((a.firmware/'BuildReceipt.plist').read_bytes())
    if receipt.get('Quiet') is not True:raise ValueError('A quiet source profile is required')
    inputs={name:(a.firmware/name).read_bytes() for name in ('EFI.img','CODE.fd','VARS.fd')}
    if receipt.get('Files')!={name:hashlib.sha256(data).hexdigest() for name,data in inputs.items()}:
        raise ValueError('Firmware does not match its build receipt')
    disk=inputs['EFI.img']
    if disk[512:520]!=b'EFI PART':raise ValueError('Expected a GPT firmware image')
    first,last=struct.unpack_from('<QQ',disk,1024+32)
    if first!=2048 or last<first or (last+1)*512>len(disk):raise ValueError('Invalid ESP bounds')
    data=bytearray(disk[first*512:(last+1)*512])
    FAT32=runpy.run_path(str(source/'scripts/efi-fat32.py'))['FAT32']
    fat=FAT32(data);c=plistlib.loads(fat.read('EFI/OC/config.plist'))
    if c['Misc']['Boot']['ShowPicker'] or c['Misc']['Debug']['Target']:
        raise ValueError('Input configuration is not actually quiet')
    for key in ('Add','Force','Patch','Block'):c['Kernel'][key]=[]
    c['Kernel']['Scheme']['KernelArch']='i386'
    # Darwin8 machine_idle_cstate has no halt fallback without pmDispatch.
    # Do not inherit Leopard's deliberate _start-return patch for the native
    # AppleIntelCPUPowerManagement; qualify the real native driver instead.
    c['Kernel']['Emulate']['DummyPowerManagement']=False
    c['PlatformInfo']['Generic']['SystemProductName']='iMac7,1'
    # Tiger's native About This Mac first looks up cpu-type at
    # IODeviceTree:/cpus/CPU0@0. QEMU names this node C000; rename it and
    # its AML references in QEMU's CPU container so Tiger can read the
    # existing SMBIOS processor type. Leave CPUID and topology alone.
    c['ACPI']['Patch'].append(dict(Base=r'\_SB.CPUS',BaseSkip=0,
        Comment='Expose the first QEMU CPU at the path Tiger expects',Count=0,
        Enabled=True,Find=b'C000',Limit=0,Mask=b'',OemTableId=b'BXPC    ',
        Replace=b'CPU0',ReplaceMask=b'',Skip=0,TableLength=0,TableSignature=b'DSDT'))
    # No physical-machine identity is introduced into the generated VM.
    c['PlatformInfo']['Generic'].update(SystemSerialNumber='',SystemUUID='',MLB='',ROM=bytes(6))
    c['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']['boot-args']=''
    # Native Tiger EfiBoot uses UGA, not GOP, to select graphical boot.
    # Its real boot-video record otherwise arrives as FB_TEXT_MODE even
    # without -v. Publish OpenCore's existing GOP-backed UGA protocol;
    # leave the kernel, framebuffer and explicit verbose requests alone.
    c['UEFI']['Output']['UgaPassThrough']=True
    graphics=None
    if a.graphics_kernel:
        candidate=a.graphics_kernel.resolve()
        graphics=verify_graphics_kernel(candidate,source)
        for name in ('VMsvga2','VMsvga2Accel'):
            bundle=candidate/(name+'.kext')
            for file in sorted(bundle.rglob('*')):
                if file.is_symlink():raise ValueError('Unexpected candidate kernel symlink')
                if file.is_file():
                    path='EFI/OC/Kexts/'+str(file.relative_to(candidate))
                    parts=fat.parts(path);directory=fat.directory(parts[:-1],create=True)
                    fat.write(path,file.read_bytes(),replace=fat.find(directory,parts[-1]) is not None)
            c['Kernel']['Add'].append(dict(Arch='i386',BundlePath=name+'.kext',
                Comment='Genuine Tiger research '+name,Enabled=True,
                ExecutablePath='Contents/MacOS/'+name,PlistPath='Contents/Info.plist',
                MinKernel='8.0.0',MaxKernel='8.99.99'))
        runpy.run_path(str(source/'scripts/build-opengl-firmware.py'))['set_framebuffer_model'](fat,'tiger')
        # Actual8R4088 __CGXGLDisplayContextForDisplayDevice reads AccelCaps
        # as exactly four bytes and excludes the compositor if it is zero.
        # Advertise the owned driver's original QE mask3, not the newer
        # period mask15. VMsvga2Accel normalizes this to OSNumber32 at start.
        # Native public CGL already independently qualifies hardware drawing.
        # These paired surface paths are not SVGA3D hardware capabilities.
        accelerator_path='EFI/OC/Kexts/VMsvga2Accel.kext/Contents/Info.plist'
        accelerator=plistlib.loads(fat.read(accelerator_path))
        accelerator['IOKitPersonalities']['VMsvga2Accel'].update(
            AccelCaps=3,VIMDirectGLCompositor=True,VIMDirectGLGPUPresentation=True)
        fat.write(accelerator_path,plistlib.dumps(accelerator),replace=True)
        # Actual8R4088 pmap_flush_tlbs panics at its hard-coded250ms
        # LockTimeOut during a real graphics/shared-library mapping boot.
        # Read-only kernel stacks identify panic -> pmap_flush_tlbs ->
        # pmap_remove -> vm_map_submap_pmap_clean, not a GPU command stall.
        # Native _ml_init_lock_timeout has exactly this nanoseconds argument
        # sequence within its first48bytes, with no slto_us override. Match
        # the complete instruction pair by symbol and retain a bounded one
        # second deadline, all acknowledgements, four CPUs and panic checks.
        # Never edit the installed Apple kernel or disable the timeout.
        c['Kernel']['Patch'].append(dict(Arch='i386',Base='_ml_init_lock_timeout',
            Comment='Bounded virtual CPU lock and TLB deadline (one second)',
            Count=1,Enabled=True,Find=bytes.fromhex('c7042480b2e60ec744240400000000'),
            Replace=bytes.fromhex('c7042400ca9a3bc744240400000000'),
            Identifier='kernel',Limit=48,Mask=b'',ReplaceMask=b'',
            MinKernel='8.0.0',MaxKernel='8.99.99',Skip=0))
        # Native8R4088 boot reaches the personality before IOGraphicsFamily
        # enters the startup extension dictionary. Its kld then rejects both
        # owned drivers with that exact missing dependency, although native
        # kextload resolves them later. Force the SAME installed OS's flat
        # families early; never copy a reference/donor binary into the EFI.
        for identifier,name in (('com.apple.iokit.IOGraphicsFamily','IOGraphicsFamily'),
                                ('com.apple.iokit.IOPCIFamily','IOPCIFamily')):
            c['Kernel']['Force'].append(dict(Arch='i386',
                BundlePath='System/Library/Extensions/'+name+'.kext',
                Comment='Native Tiger graphics dependency',Enabled=True,
                ExecutablePath=name,Identifier=identifier,PlistPath='Info.plist',
                MinKernel='8.0.0',MaxKernel='8.99.99'))
        # The existing source-built loader recognizes the actual selected
        # DVD's System/Installation/CDIS and suppresses the COMPLETE pair
        # there. Installed Tiger has no such directory. No host-state flag,
        # installer injection or persistent guest boot argument is involved.
    fat.write('EFI/OC/config.plist',plistlib.dumps(c,sort_keys=False),replace=True)
    a.output.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix='vim-tiger-native-efi-') as scratch:
        boot=Path(scratch)/'BOOT.img';boot.write_bytes(data)
        # The source is already quiet. Re-running generic quiet_config here
        # re-adds an unrelated Darwin18+ AppleVirtualGraphics mode-table
        # patch AFTER clearing the kernel patch list above. It cannot match
        # Tiger, but violates the native-only profile and its zero-patch
        # receipt. Wrap exactly the checked configuration, without that pass.
        runpy.run_path(str(source/'scripts/make-efi-disk.py'))['create'](boot,a.output/'EFI.img')
    for name in ('CODE.fd','VARS.fd'):(a.output/name).write_bytes(inputs[name])
    output={'FormatVersion':1,'Guest':'tiger','Quiet':True,'NativeInstallerOnly':graphics is None,
        'BootValidated':False,'InjectedKernelDrivers':2 if graphics else 0,'KernelPatches':1 if graphics else 0,'NativePowerManagement':True,
        'RecipeSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'InputFiles':receipt['Files'],
        'Files':{name:hashlib.sha256((a.output/name).read_bytes()).hexdigest() for name in inputs}}
    if graphics:output['GraphicsKernelBuild']=graphics
    (a.output/'BuildReceipt.plist').write_bytes(plistlib.dumps(output))
    print(a.output)


if __name__=='__main__':main()
