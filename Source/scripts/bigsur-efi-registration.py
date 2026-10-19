"""Add the tiny GL registration kext to a generated guest EFI.

Production images use the runtime-gated driver, which checks vim_gl before
advertising GL. Legacy, ungated driver experiments remain prepared-guest-only.
"""
from pathlib import Path
import plistlib
import runpy

def add_registration(data, kext):
    kext=Path(kext)
    info=plistlib.loads((kext/'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier')!='local.pvg.OpenGLProbe' or info.get('CFBundleExecutable')!='PVGOpenGLProbe':
        raise ValueError('unexpected GL registration kext identity')
    fat=runpy.run_path(str(Path(__file__).with_name('efi-fat32.py')))['FAT32'](data)
    config=plistlib.loads(fat.read('EFI/OC/config.plist'))
    entries=config['Kernel']['Add']
    if any(e.get('BundlePath')=='PVGOpenGLProbe.kext' for e in entries):
        raise ValueError('GL registration already exists in EFI template')
    for file in sorted(kext.rglob('*')):
        if file.is_symlink():raise ValueError('unexpected link in registration kext')
        if file.is_file():fat.write('EFI/OC/Kexts/PVGOpenGLProbe.kext/'+file.relative_to(kext).as_posix(),file.read_bytes())
    entries.append({'Arch':'x86_64','BundlePath':'PVGOpenGLProbe.kext',
        'Comment':'Virtual Intel Mac prepared Big Sur OpenGL registration',
        'Enabled':True,'ExecutablePath':'Contents/MacOS/PVGOpenGLProbe',
        'PlistPath':'Contents/Info.plist','MinKernel':'20.0.0','MaxKernel':'20.99.99'})
    fat.write('EFI/OC/config.plist',plistlib.dumps(config,sort_keys=False),replace=True)
    # Read through a new parser, not retained directory offsets.
    checked=runpy.run_path(str(Path(__file__).with_name('efi-fat32.py')))['FAT32'](data)
    for file in kext.rglob('*'):
        if file.is_file() and checked.read('EFI/OC/Kexts/PVGOpenGLProbe.kext/'+file.relative_to(kext).as_posix())!=file.read_bytes():
            raise ValueError('registration kext failed generated EFI readback')
    if plistlib.loads(checked.read('EFI/OC/config.plist'))!=config:
        raise ValueError('registration config failed generated EFI readback')
