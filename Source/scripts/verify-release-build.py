#!/usr/bin/env python3
"""Fail closed on incomplete runtime packaging or inconsistent host signing."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import runpy
import subprocess


def run(*command):
    return subprocess.check_output(list(map(str, command)), text=True).strip()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_linkage(path):
    # otool -L includes a dylib's own LC_ID_DYLIB. It is not an imported
    # dependency, and need not be rewritten from its @rpath install name.
    own_ids = {line.strip() for line in run('otool', '-arch', 'all', '-D', path).splitlines()
               if line.strip() and not line.rstrip().endswith(':')}
    for line in run('otool', '-arch', 'all', '-L', path).splitlines():
        if not line.startswith('\t'):
            continue
        dependency = line.strip().split(' (compatibility', 1)[0]
        if dependency not in own_ids and not dependency.startswith(('/System/Library/', '/usr/lib/', '@loader_path/')):
            raise RuntimeError('Unbundled dependency in '+str(path)+': '+dependency)


def verify_unified_firmware(runtime, source, receipt):
    recipe = runpy.run_path(str(source/'scripts/build-unified-efi.py'))
    profiles = recipe['PROFILES']
    record = receipt.get('UnifiedEFI', {})
    image = runtime/'Firmware/Common/EFI.img'
    if (record.get('RuntimeProfileSelection') is not True or record.get('Profiles') != list(profiles)
            or not image.is_file() or record.get('ImageBytes') != image.stat().st_size):
        raise RuntimeError('Release requires one runtime-selected EFI image with all thirteen OS profiles')
    fat = recipe['FAT32'](recipe['volume'](image))
    selector = fat.read('EFI/BOOT/BOOTx64.efi')
    if not selector.startswith(b'MZ') or selector != fat.read('EFI/OC/OpenCore.efi'):
        raise RuntimeError('Shared EFI boot paths do not contain the same selector')
    if fat.read('.contentVisibility') != b'Disabled\n':
        raise RuntimeError('Shared EFI must exclude its own launcher from the guest OS picker')
    total = 0
    for guest in profiles:
        directory = runtime/'Firmware'/guest
        if (directory/'EFI.img').exists() or (directory/'EFI.img').is_symlink():
            raise RuntimeError('Guest firmware must use Common directly, not an EFI copy/alias: '+guest)
        if (directory/'BuildReceipt.plist').exists():
            raise RuntimeError('Build-only firmware receipt in runtime profile: '+guest)
        entries = recipe['files'](fat, fat.directory(['EFI', 'VIM', guest]))
        config = plistlib.loads(entries['config.plist'])
        if not entries['OpenCore.efi'].startswith(b'MZ') or config['Misc']['Boot']['ShowPicker']:
            raise RuntimeError('A shared EFI profile is missing its quiet native loader: '+guest)
        total += sum(map(len, entries.values()))
    if total != record.get('ProfileBytes'):
        raise RuntimeError('Shared EFI profile inventory differs from the packaging output')


def verify_opengl_runtime(runtime, source, receipt):
    """Verify the installed-guest inputs, not a boot/conformance claim."""
    guests = {'leopard': (10, 5, 0), 'snowleopard': (10, 6, 0), 'lion': (10, 7, 0),
              'mountainlion': (10, 8, 0), 'mavericks': (10, 9, 0), 'yosemite': (10, 10, 0),
              'elcapitan': (10, 11, 0), 'sierra': (10, 12, 0), 'highsierra': (10, 13, 0)}
    records = receipt.get('OpenGLGuests', {})
    if set(records) != set(guests):
        raise RuntimeError('Release requires OpenGL guest support for every version from Leopard through High Sierra')
    verify = runpy.run_path(str(source/'scripts/package-opengl-guest-support.py'))['verify']
    check_minimum = runpy.run_path(str(source/'scripts/package-app-runtime.py'))['require_macos_11']
    executor = runtime/'bin/libVIMPassthroughOpenGLHost.dylib'
    executor_hash = digest(executor)
    quiet_code_hash = digest(runtime/'Firmware/Common/CODE.fd')
    dual = {'GLEngine', 'libVIMPassthroughOpenGL.dylib', 'VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA'}
    magics = {b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf', b'\xcf\xfa\xed\xfe',
              b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xfe\xed\xfa\xce'}
    for guest, minimum in guests.items():
        record = records[guest]
        support = runtime/'OpenGLGuests'/guest
        manifest = verify(support, source)
        if manifest.get('RequiresGuestDeveloperTools') is not False:
            raise RuntimeError('Guest support receipt mismatch: '+guest)
        if record.get('HostExecutorPackagedSHA256') != executor_hash:
            raise RuntimeError('OpenGL host executor checksum mismatch: '+guest)
        directory = runtime/'Firmware'/guest
        firmware = record.get('Firmware', {})
        if (firmware.get('Guest') != guest or firmware.get('Quiet') is not True
                or firmware.get('AppOwnedReadOnlyEFI') is not True
                or firmware.get('InstallerGraphicsPolicy') != 'selected-apple-installer-root'):
            raise RuntimeError('Guest requires one quiet installed-system-aware EFI: '+guest)
        actual = {'EFI.img':digest(runtime/'Firmware/Common/EFI.img'),
                  'CODE.fd':digest(directory/'CODE.fd' if (directory/'CODE.fd').exists() else runtime/'Firmware/Common/CODE.fd'),
                  'VARS.fd':digest(directory/'VARS.fd')}
        expected_files = firmware.get('Files', {})
        files_match = (all(expected_files.get(name) == actual[name] for name in ('CODE.fd', 'VARS.fd'))
                       if receipt.get('UnifiedEFI') else expected_files == actual)
        if (not files_match or record.get('EFIImageSHA256') != actual['EFI.img']
                or actual['CODE.fd'] != quiet_code_hash):
            raise RuntimeError('Guest firmware checksum or quiet CODE mismatch: '+guest)
        for name in manifest['Files']:
            path = support/name
            if name == 'GLEngine' and b'VIMRosettaCodeUpdateProbe' in path.read_bytes():
                raise RuntimeError('Research-only Rosetta code-cache fixture in packaged guest renderer: '+guest)
            if path.is_symlink():
                raise RuntimeError('Unexpected guest support symlink: '+guest+'/'+name)
            with path.open('rb') as stream:
                executable = stream.read(4) in magics
            if not executable:
                continue
            if guest == 'leopard' and name == 'leopard-smbfs':
                # verify(support) above already checks the genuine i386
                # MH_OBJECT, period ABI and SMB bundle contract. Darwin9's
                # kernel object has no userspace deployment load command or
                # modern signature; requiring either would reject valid code.
                continue
            pair = name in dual or (guest == 'leopard' and name == 'opengl-installer-automation.dylib')
            # Both period Samba pairs are 32-bit userspace programs. Snow's
            # x86_64 kernel still runs them, and smbd loads its matching i386
            # Directory Services bundle in-process.
            native_smb = guest in ('leopard', 'snowleopard') and name in (
                guest+'-smbd', guest+'-odsam.dylib')
            expected = {'i386'} if native_smb else {'x86_64', 'i386'} if pair else {'x86_64'}
            if set(run('lipo', '-archs', path).split()) != expected:
                raise RuntimeError('Wrong guest architectures: '+guest+'/'+name)
            check_minimum(path, minimum)
            signature = ['codesign', '--verify', '--strict']
            if name == 'VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA':
                # The bootstrap assembles this plugin from two independently
                # manifest-sealed inputs (Mach-O and Info.plist), not the old
                # build directory's resource envelope. Verify the unchanged
                # executable signature/entitlements without that envelope.
                signature.append('--ignore-resources')
            run(*signature, path)
            verify_linkage(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--configuration', choices=('Release', 'Debug'), required=True)
    args = parser.parse_args()
    app = args.app
    runtime = app/'Contents/Resources/Runtime'
    if (runtime/'ResearchRuntime.plist').exists():
        raise RuntimeError('A research-only app cannot be packaged as a release')
    receipt = plistlib.loads((runtime/'Packaging.plist').read_bytes())
    if receipt.get('AppConfiguration') != args.configuration:
        raise RuntimeError('Packaged app configuration does not match the requested build')
    identity = receipt['HostSigningIdentity'].upper()
    if receipt.get('DevelopmentAdHocSigned') or len(identity) != 40:
        raise RuntimeError('Release host executables must have a persistent signing identity')
    shared = args.source/'Private/DevelopmentSigning/Identity.plist'
    if shared.exists() and plistlib.loads(shared.read_bytes())['Identity'].upper() != identity:
        raise RuntimeError('Release does not use the shared private-repository identity')
    run('codesign', '--verify', '--deep', '--strict', app)
    hosts = [app/'Contents/MacOS/Virtual Intel Mac', runtime/'bin/qemu-system-x86_64',
             runtime/'bin/qemu-img', runtime/'bin/apple-gfx-helper',
             runtime/'bin/libVIMPassthroughOpenGLHost.dylib',
             app/'Contents/Library/LaunchServices/com.mac.virtualintelmac.usb-helper',
             app/'Contents/Library/LaunchServices/com.mac.virtualintelmac.network-helper']
    for path in hosts:
        architectures = set(run('lipo', '-archs', path).split())
        expected = {'x86_64'} if path.name == 'apple-gfx-helper' else {'arm64', 'x86_64'}
        if architectures != expected:
            raise RuntimeError(f'Wrong architectures: {path.name}: {architectures}')
        run('codesign', '--verify', '--strict', '-R', '=certificate leaf = H"'+identity+'"', path)
        verify_linkage(path)
        entitlements = subprocess.run(['codesign', '-d', '--entitlements', ':-', str(path)],
                                      stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout
        if b'cross-architecture-support' in entitlements or b'get-task-allow' in entitlements:
            raise RuntimeError(f'Development/restricted entitlement in shipped host executable: {path.name}')
    firmware = receipt.get('FirmwareCandidate')
    if not firmware or firmware.get('Configuration') != 'RELEASE' or not firmware.get('QuietBoot'):
        raise RuntimeError('Release requires the quiet RELEASE firmware')
    if receipt.get('RuntimeBootPolicy',{}).get('RuntimePolicyProtocol')!=1:
        raise RuntimeError('Release requires runtime-configurable EFI policy')
    for guest in ('mojave', 'catalina', 'bigsur'):
        directory=runtime/'Firmware'/guest
        if any(directory.glob('EFI*.img')):
            raise RuntimeError('Guest must load the single common EFI image directly: '+guest)
        if hashlib.sha256((runtime/'Firmware/Common/EFI.img').read_bytes()).hexdigest()!=receipt['EFIImages'][guest]:
            raise RuntimeError('EFI image checksum mismatch: '+guest)
        payload = receipt['GuestPayloads']['Guests'][guest]
        media = receipt['GuestSupportMedia'][guest]
        image = runtime/'GuestPayloads'/media['Image']
        if not image.is_file() or not image.stat().st_size:
            raise RuntimeError('Guest support media is missing: '+guest)
        entries = media['Files']
        name = 'payload.tar.xz' if payload['Archive'].endswith('.xz') else 'payload.tar.gz'
        if not entries.get(name,{}).get('Size'):
            raise RuntimeError('Embedded guest payload is missing: '+guest)
    if list((runtime/'GuestPayloads').glob('*.plist')):
        raise RuntimeError('Redundant outer guest support manifests in runtime')
    verify_unified_firmware(runtime, args.source, receipt)
    verify_opengl_runtime(runtime, args.source, receipt)
    tiger=runpy.run_path(str(args.source/'scripts/package-tiger-guest-support.py'))['verify'](runtime/'OpenGLGuests/tiger',args.source,packaged=True)
    if receipt.get('TigerGuestSupport')!=tiger:
        raise RuntimeError('Tiger native support packaging receipt mismatch')
    # A build must not accidentally copy the private key or local receipts into
    # the app. The helper policy contains only a public certificate fingerprint.
    for path in app.rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.p12', '.pfx', '.keychain', '.keychain-db'):
            raise RuntimeError('Private credential file in app: '+path.name)
    print('Verified: universal app/QEMU/helpers/OpenGL executor, shared signing, portable linkage, one EFI with thirteen OS profiles, native Tiger support and period guest architectures/deployment targets.')


if __name__ == '__main__':
    main()
