#!/usr/bin/env python3
"""Build one quiet, app-owned GPT/ESP OpenGL guest EFI from checked-in inputs.

No VM, writable NVRAM, admin access, network or mtools dependency is used.
Each period driver is built against its own SDK; qualification is separate.
"""
import argparse
import hashlib
from pathlib import Path
import plistlib
import runpy
import tarfile
import tempfile


def set_framebuffer_model(fat, guest):
    # Match the period profile advertised by the accelerated GL renderer.
    # VMsvga2 publishes this name as the PCI device's `model` property; its
    # actual PCI IDs, driver match and framebuffer capabilities stay intact.
    model = {
        'tiger': 'Intel GMA 950', 'leopard': 'Intel GMA X3100',
        'snowleopard': 'Intel HD Graphics 3000', 'lion': 'Intel HD Graphics 3000',
        'mountainlion': 'Intel HD Graphics 4000', 'mavericks': 'Intel HD Graphics 4000',
        'yosemite': 'Intel Iris Pro', 'elcapitan': 'Intel Iris Pro',
        'sierra': 'Intel HD Graphics 630', 'highsierra': 'Intel HD Graphics 630',
    }[guest]
    path = 'EFI/OC/Kexts/VMsvga2.kext/Contents/Info.plist'
    info = plistlib.loads(fat.read(path))
    info['IOKitPersonalities']['VMsvga2']['IOHardwareModel'] = model
    fat.write(path, plistlib.dumps(info), replace=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True,help='New output directory')
    parser.add_argument('--code',type=Path,help='Qualified quiet RELEASE OVMF CODE for this app build')
    parser.add_argument('--guest',choices=('highsierra','sierra','elcapitan','yosemite','mavericks','mountainlion','lion','snowleopard','leopard'),default='mavericks')
    args=parser.parse_args();source=Path(__file__).resolve().parents[1]
    if args.output.exists():parser.error('Use a new output directory')
    archive=source/'prebuilt/PassthroughOpenGL/common/firmware-inputs.tar.xz'
    with tarfile.open(archive) as package:
        entries={}
        for item in package.getmembers():
            if not item.isfile() or item.name in entries or item.size>16*1024*1024:raise ValueError('Invalid firmware input')
            entries[item.name]=package.extractfile(item).read()
    manifest=plistlib.loads(entries.pop('Manifest.plist'))
    if manifest.get('Files')!={name:hashlib.sha256(data).hexdigest() for name,data in entries.items()}:
        raise ValueError('Legacy firmware manifest mismatch')
    FAT32=runpy.run_path(str(source/'scripts/efi-fat32.py'))['FAT32']
    with tarfile.open(source/'vendor/app-firmware-inputs.tar.gz') as base:
        data=bytearray(base.extractfile('mojave/BOOT.img').read())
        variables=base.extractfile('VARS.fd').read()
    fat=FAT32(data)
    period=args.guest in ('leopard','snowleopard','lion','mountainlion')
    native_audio=period or args.guest in ('yosemite','elcapitan','sierra','highsierra')
    kernel={'leopard':9,'snowleopard':10,'lion':11,'mountainlion':12,'mavericks':13,'yosemite':14,'elcapitan':15,'sierra':16,'highsierra':17}[args.guest]
    sdk={'leopard':'10.5','snowleopard':'10.6','lion':'10.7','mountainlion':'10.8','mavericks':'10.9','yosemite':'10.10','elcapitan':'10.11','sierra':'10.12','highsierra':'10.13'}[args.guest]
    display_name={'leopard':'Leopard','snowleopard':'Snow Leopard','lion':'Lion','mountainlion':'Mountain Lion','mavericks':'Mavericks','yosemite':'Yosemite','elcapitan':'El Capitan','sierra':'Sierra','highsierra':'High Sierra'}[args.guest]
    architecture='i386' if args.guest=='leopard' else 'x86_64'
    for name,content in sorted(entries.items()):
        if native_audio and name.startswith('EFI/OC/Kexts/VoodooHDA.kext/'):
            continue # Use the complete period-built bundle, not stale signature files.
        parts=fat.parts(name);existing=fat.find(fat.directory(parts[:-1],create=True),parts[-1])
        fat.write(name,content,replace=existing is not None)
    config=plistlib.loads(fat.read('EFI/OC/config.plist'))
    # OpenCore regenerates Apple SMBIOS type 131. Its auto-detected Core 2
    # Duo code has no four-core name in AppleSystemInfo. The Penryn Xeon
    # family has period labels for one, two and four cores, including our
    # default four. This is metadata only, not a CPUID or topology change.
    # Tiger inherits this same Penryn processor type from the Leopard profile.
    config['PlatformInfo']['Generic']['ProcessorType']=0x0402
    if args.guest=='leopard':
        # Actual Darwin 9 reference and native SDK are a 32-bit kernel with
        # both userspace ABIs. These period-built kexts are MH_OBJECT/i386,
        # not Snow's x64 MH_KEXT_BUNDLE. QEMU supplies the Apple SMC already;
        # do not attempt to load unrelated later x64 Lilu/VirtualSMC images.
        config['Kernel']['Scheme']['KernelArch']='i386'
        # The actual 9J3050 cpu_check accepts MBP5,2/5,3/5,4/5,5. Present a
        # period-correct virtual Mac rather than editing Apple's model check.
        # Serial/UUID remain unset; no physical host identity is imported.
        config['PlatformInfo']['Generic']['SystemProductName']='MacBookPro5,3'
        for item in config['Kernel']['Add']:
            if item['BundlePath'] in ('Lilu.kext','VirtualSMC.kext'):
                item['Enabled']=False
        # Actual9J3050/9L31a native XNU hard-codes a250ms lock/TLB deadline;
        # unlike Snow it has no slto_us boot argument. Real cold-boot traces
        # show TLB-shootdown -> pmap-lock panics at that deadline. A bounded
        # one-second research EFI reaches the desktop with the actual live
        # LockTimeOut=1e9 and LockTimeOutTSC=1,996,800,000. Retain all locks,
        # acknowledgements, four CPUs and panic behavior, not disabled checks.
        # Match complete instructions ONLY inside the named initializer;
        # never apply a numeric file offset or edit the on-disk Apple kernel.
        config['Kernel']['Patch'].append(dict(Arch='i386',Base='_ml_init_lock_timeout',
            Comment='Bounded virtual CPU lock and TLB deadline (one second)',
            Count=1,Enabled=True,Find=bytes.fromhex('c7042480b2e60ec744240400000000'),
            Replace=bytes.fromhex('c7042400ca9a3bc744240400000000'),
            Identifier='kernel',Limit=48,Mask=b'',ReplaceMask=b'',
            MinKernel='9.0.0',MaxKernel='9.99.99',Skip=0))
    if args.guest in ('leopard','snowleopard'):
        # Darwin 9/10's below-first_avail coalescing mistakes a single-page EFI
        # BootServices descriptor for free memory. On our real DVD boot this
        # recycles kernel __const as pmap_phys_attributes and destroys vtables.
        # Normalize adjacent usable descriptors in the loader, before XNU sees
        # them; retain runtime regions and their actual memory attributes.
        config['Booter']['Quirks']['RebuildAppleMemoryMap']=True
    if args.guest=='snowleopard':
        # XNU 1504's native TLB/lock deadline is only 250 ms. A synchronous
        # virtual graphics I/O exit can exceed that during first host GL
        # initialization, while the other CPUs still run and send shootdowns.
        # Use XNU's own bounded one-second policy, not disabled watchdogs or
        # fewer CPUs. 1e6 us also stays within its uint32 absolute/TSC budgets
        # at the virtual platform's measured 1.9968-GHz TSC.
        apple=config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']
        arguments=[part for part in apple.get('boot-args','').split() if not part.startswith('slto_us=')]
        apple['boot-args']=' '.join(arguments+['slto_us=1000000'])
    if args.guest=='yosemite':
        # Darwin14 introduced mandatory kext signing. Use its documented guest
        # development boot policy for our source-built extensions; no host
        # security changes or borrowed Apple driver signatures are required.
        apple=config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']
        arguments=[part for part in apple.get('boot-args','').split() if not part.startswith('kext-dev-mode=')]
        apple['boot-args']=' '.join(arguments+['kext-dev-mode=1'])
    if kernel>=15:
        # SIP is a guest policy. Permit the source-built drivers and guest
        # framework installation without changing the host's security state.
        apple=config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']
        apple['csr-active-config']=(0x67).to_bytes(4,'little')
        # The actual15G31 AppleMobileFileIntegrity image exposes this policy;
        # keep the same app-owned guest framework policy as the newer guests.
        arguments=[part for part in apple.get('boot-args','').split() if not part.startswith('amfi_get_out_of_my_way=')]
        apple['boot-args']=' '.join(arguments+['amfi_get_out_of_my_way=0x1'])
    # One source-built quiet loader recognizes the actual Apple installer
    # root on each boot. Recovery keeps configured SMC/decryption providers,
    # but no graphics/audio Add/Force injections; the installed OS gets the full stack.
    # No second EFI, app-state switch, timed handoff, or persistent boot flag.
    loader=source/'prebuilt/PassthroughOpenGL/Firmware/OpenCore'
    loader_receipt=runpy.run_path(str(source/'scripts/build-opengl-opencore.py'))['verify'](loader)
    fat.write('EFI/OC/OpenCore.efi',(loader/'OpenCore.efi').read_bytes(),replace=True)
    if period:
        # Native 10.8 DSP symbols also require the source-matched loader's
        # linkedit file-bounds correction on the installed-system path.
        if args.guest=='lion':
            # Apple's Lion recovery cache uses 45 disk fragments. The older
            # bundled OpenHfsPlus stops at the first eight (4.78 MB), before
            # the x64 FAT slice. Read its native extent overflow index instead
            # of thinning/rewriting Apple's cache or changing CPU identity.
            fat.write('EFI/OC/Drivers/OpenHfsPlus.efi',(loader/'OpenHfsPlus.efi').read_bytes(),replace=True)
    # Independently compiled against each guest's actual kernel headers.
    # Mavericks also uses an explicit source/byte receipt, not the historical
    # kernel snapshot still present in the base archive. Never silently ship
    # an old accelerator when its paired userspace gains a new transaction.
    families=[('Kernel','VMsvga2',('VMsvga2','VMsvga2Accel'))]
    if native_audio:families.append(('Audio','VoodooHDA/tranc',('VoodooHDA',)))
    for family,origin,names in families:
        kernel_source=source/'prebuilt/PassthroughOpenGL'/args.guest/family
        receipt=plistlib.loads((kernel_source/'BuildReceipt.plist').read_bytes())
        if (receipt.get('FormatVersion')!=1 or receipt.get('SDKVersion')!=sdk or
            receipt.get('AudioOnly')!=(family=='Audio')):
            raise ValueError(display_name+' '+family+' targets the wrong guest ABI')
        for name in names:
            if not (kernel_source/(name+'.kext/Contents/MacOS/'+name)).is_file():
                raise ValueError('Missing guest kernel driver: '+name)
            for file in sorted((kernel_source/(name+'.kext')).rglob('*')):
                if file.is_symlink():raise ValueError('Unexpected kernel payload symlink')
                if file.is_file():
                    path='EFI/OC/Kexts/'+str(file.relative_to(kernel_source));parts=fat.parts(path)
                    exists=fat.find(fat.directory(parts[:-1],create=True),parts[-1]) is not None
                    fat.write(path,file.read_bytes(),replace=exists)
    set_framebuffer_model(fat,args.guest)
    for item in config['Kernel']['Add']:
        if native_audio and item['BundlePath']=='VoodooHDA.kext':
            item.update(Arch=architecture,MinKernel=f'{kernel}.0.0',MaxKernel=f'{kernel}.99.99')
        if item['BundlePath'] in ('VMsvga2.kext','VMsvga2Accel.kext'):
            item.update(Arch=architecture,MinKernel=f'{kernel}.0.0',MaxKernel=f'{kernel}.99.99',Enabled=True,
                Comment='Virtual Intel Mac '+display_name+' PassthroughOpenGL surfaces')
    # The period kernel build intentionally starts with the upstream bundle
    # metadata, not a copy of the Mavericks payload. Apply the paired renderer
    # personality here on BOTH releases. Without it applications can create a
    # hardware CGL context, but CoreGraphics excludes the display compositor
    # before loading any GL engine (AccelCaps bit 0). Mountain Lion also
    # requires bit 1 (QuartzGL backing stores) for native menu filters, and
    # bit 3 admits the ordinary mipmaps implemented by the paired renderer.
    # The physical Intel Gen6 accelerator reports 11, but this virtual driver
    # also implements native page-flip primary/companion ownership (mask 4).
    # Advertise it so CoreGraphics negotiates GPU presentation instead of the
    # completed CPU-primary publication path. Lion's measured Retina Finder
    # pacing rises from ~30 to ~58/s; Mountain Lion also admits GPU root 1.
    # Never reclassify an ordinary primary behind the compositor's back.
    # Keep Mavericks' qualified mask unchanged. These are not SVGA3D caps.
    accelerator_path='EFI/OC/Kexts/VMsvga2Accel.kext/Contents/Info.plist'
    accelerator=plistlib.loads(fat.read(accelerator_path))
    personality=accelerator['IOKitPersonalities']['VMsvga2Accel']
    personality.update(AccelCaps=15 if period else 5,
        VIMDirectGLCompositor=True,VIMDirectGLGPUPresentation=True)
    fat.write(accelerator_path,plistlib.dumps(accelerator),replace=True)
    # Actual 13F34 recovery prelink inventory excludes these native families.
    # Read them from the OS being booted, never transplant donor kernel code.
    # IOGraphicsFamily uses a flat bundle on this release; the others do not.
    native=[('com.apple.iokit.IOGraphicsFamily','IOGraphicsFamily.kext','IOGraphicsFamily','Info.plist'),
        ('com.apple.iokit.IOSerialFamily','IOSerialFamily.kext','Contents/MacOS/IOSerialFamily','Contents/Info.plist'),
        ('com.apple.driver.Apple16X50Serial','Apple16X50Serial.kext','Contents/MacOS/Apple16X50Serial','Contents/Info.plist'),
        ('com.apple.driver.Apple16X50ACPI','Apple16X50Serial.kext/Contents/PlugIns/Apple16X50ACPI.kext','Contents/MacOS/Apple16X50ACPI','Contents/Info.plist'),
        ('com.apple.kext.OSvKernDSPLib','OSvKernDSPLib.kext','Contents/MacOS/OSvKernDSPLib','Contents/Info.plist'),
        ('com.apple.iokit.IOAudioFamily','IOAudioFamily.kext','Contents/MacOS/IOAudioFamily','Contents/Info.plist')]
    if kernel>=15:
        # The Air's stock 15G31 cache also omits Libm. OpenCore then cannot
        # link vecLib -> IOAudioFamily -> VoodooHDA, even though native
        # kextd can load those dependencies after boot. Force the full
        # native chain in order; already cached bundles are not duplicated.
        native[-1:-1]=[
            ('com.apple.kec.Libm','Libm.kext','Contents/MacOS/Libm','Contents/Info.plist'),
            ('com.apple.vecLib.kext','vecLib.kext','Contents/MacOS/vecLib','Contents/Info.plist')]
    force=config['Kernel'].setdefault('Force',[])
    identifiers={item[0] for item in native};force[:]=[e for e in force if e['Identifier'] not in identifiers]
    for identifier,bundle,executable,info in native:
        force.append(dict(Arch=architecture,BundlePath='System/Library/Extensions/'+bundle,
            Comment='Native OS X recovery dependency',Enabled=True,ExecutablePath=executable,
            Identifier=identifier,MaxKernel=f'{kernel}.99.99',MinKernel=f'{kernel}.0.0',PlistPath=info))
    fat.write('EFI/OC/config.plist',plistlib.dumps(config,sort_keys=False),replace=True)
    args.output.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix='vim-opengl-efi-') as scratch:
        boot=Path(scratch)/'BOOT.img';boot.write_bytes(data)
        runpy.run_path(str(source/'scripts/make-efi-disk.py'))['create'](boot,args.output/'EFI.img',quiet=True)
    # Pristine variable template only. Existing VMs keep their own mutable copy.
    (args.output/'VARS.fd').write_bytes(variables)
    (args.output/'CODE.fd').write_bytes((args.code or source/'prebuilt/firmware/OVMF_CODE.fd').read_bytes())
    receipt={'FormatVersion':1,'Guest':args.guest,'Quiet':True,'AppOwnedReadOnlyEFI':True,
        'InstallerGraphicsPolicy':'selected-apple-installer-root',
        'AcceleratorQualified':args.guest=='mavericks',
        'SourceArchiveSHA256':hashlib.sha256(archive.read_bytes()).hexdigest(),
        'Files':{name:hashlib.sha256((args.output/name).read_bytes()).hexdigest() for name in ('EFI.img','VARS.fd','CODE.fd')}}
    if loader_receipt is not None:receipt['OpenCoreBuild']=loader_receipt
    (args.output/'BuildReceipt.plist').write_bytes(plistlib.dumps(receipt))
    print(args.output)

if __name__=='__main__':main()
