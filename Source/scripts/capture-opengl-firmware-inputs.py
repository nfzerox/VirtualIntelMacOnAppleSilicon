#!/usr/bin/env python3
"""Maintenance capture of qualified legacy EFI files, never mutable VARS.

Captures only active boot components, strips machine identities and diagnostic
arguments, and adds the already-vendored audio driver. The resulting archive is
a durable build input; normal builds do not read the research VM or use mtools.
"""
import argparse
import hashlib
import io
from pathlib import Path, PurePosixPath
import plistlib
import subprocess
import tarfile
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qualified-fat',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():parser.error('Use a new output archive')
    source=Path(__file__).resolve().parents[1]
    def read(path):
        return subprocess.check_output(['mtype','-i',str(args.qualified_fat),'::'+path])
    config=plistlib.loads(read('EFI/OC/config.plist'))
    # Explicit virtual identity; never capture a research machine's serial,
    # board serial, ROM, UUID, writable NVRAM, saved boot target or credentials.
    generic=config['PlatformInfo']['Generic']
    for key in ('MLB','SystemSerialNumber','SystemUUID'):generic[key]=''
    generic['ROM']=bytes(6);generic['SystemProductName']='MacPro5,1'
    config['Booter']['Quirks']['AllowRelocationBlock']=True
    nvram=config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']
    nvram['boot-args']='';config['Misc']['Debug']['Target']=0
    config['Misc']['Debug']['DisplayLevel']=0
    entries={}
    def add(path):
        if path not in entries:entries[path]=read(path)
    for path in ('EFI/BOOT/BOOTx64.efi','EFI/OC/OpenCore.efi'):add(path)
    for section,folder in (('ACPI','ACPI'),):
        config[section]['Add']=[e for e in config[section]['Add'] if e['Enabled']]
        for entry in config[section]['Add']:add('EFI/OC/'+folder+'/'+entry['Path'])
    config['UEFI']['Drivers']=[e for e in config['UEFI']['Drivers'] if e['Enabled'] and e['Path'] not in ('OpenCanopy.efi','ResetNvramEntry.efi','ToggleSipEntry.efi')]
    for entry in config['UEFI']['Drivers']:
        path='EFI/OC/Drivers/'+entry['Path']
        if entry['Path']=='OpenPartitionDxe.efi':
            # The old installed-system research image referenced this driver
            # without containing it. Fresh Apple APM recovery needs the real
            # driver; do not inherit that latent research configuration bug.
            entries[path]=(source/'prebuilt/PassthroughOpenGL/common/OpenPartitionDxe.efi').read_bytes()
        else:add(path)
    # Keep only the classic family's live components. Plugins are copied with
    # their parent bundle, not separately synthesized or linked to host files.
    config['Kernel']['Add']=[e for e in config['Kernel']['Add'] if e['Enabled'] and
        int((e.get('MinKernel') or '0').split('.')[0])<=13 and
        int((e.get('MaxKernel') or '99').split('.')[0])>=11]
    parents=sorted({e['BundlePath'].split('/')[0] for e in config['Kernel']['Add']})
    with tempfile.TemporaryDirectory(prefix='vim-capture-opengl-') as scratch:
        for name in parents:
            subprocess.run(['mcopy','-s','-i',str(args.qualified_fat),'::EFI/OC/Kexts/'+name,scratch+'/'],check=True)
            root=Path(scratch)/name
            for file in sorted(root.rglob('*')):
                if file.is_symlink():raise ValueError('Unexpected kext symlink')
                if file.is_file():entries['EFI/OC/Kexts/'+name+'/'+file.relative_to(root).as_posix()]=file.read_bytes()
    audio=source/'prebuilt/audio/VoodooHDA.kext'
    for file in sorted(audio.rglob('*')):
        if file.is_symlink():raise ValueError('Unexpected audio driver symlink')
        if file.is_file():entries['EFI/OC/Kexts/VoodooHDA.kext/'+file.relative_to(audio).as_posix()]=file.read_bytes()
    config['Kernel']['Add'].append(dict(Arch='x86_64',BundlePath='VoodooHDA.kext',
        Comment='Virtual Intel Mac legacy HDA input and output',Enabled=True,
        ExecutablePath='Contents/MacOS/VoodooHDA',PlistPath='Contents/Info.plist',MinKernel='11.0.0',MaxKernel='13.99.99'))
    for entry in config['Kernel']['Add']:
        if entry['BundlePath'].split('/')[0] in ('VMsvga2.kext','VMsvga2Accel.kext'):
            # Do not claim Lion/Mountain Lion driver ABI qualification yet.
            entry['Comment']='Virtual Intel Mac Mavericks PassthroughOpenGL surfaces'
    entries['EFI/OC/config.plist']=plistlib.dumps(config,sort_keys=False)
    manifest={'FormatVersion':1,'QualifiedGuest':'mavericks','ContainsMutableNVRAM':False,
        'ContainsMachineIdentity':False,'Files':{name:hashlib.sha256(data).hexdigest() for name,data in entries.items()}}
    entries['Manifest.plist']=plistlib.dumps(manifest)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with tarfile.open(args.output,'x:xz',preset=6) as archive:
        for name,data in sorted(entries.items()):
            if PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts:raise ValueError('Invalid path')
            entry=tarfile.TarInfo(name);entry.size=len(data);entry.mode=0o644;entry.mtime=0
            archive.addfile(entry,io.BytesIO(data))
    print(hashlib.sha256(args.output.read_bytes()).hexdigest(),args.output.name)

if __name__=='__main__':main()
