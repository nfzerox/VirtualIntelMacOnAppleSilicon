#!/usr/bin/env python3
"""Fail closed on incomplete runtime packaging or inconsistent host signing."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import subprocess


def run(*command):
    return subprocess.check_output(list(map(str, command)), text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--configuration', choices=('Release', 'Debug'), required=True)
    args = parser.parse_args()
    app = args.app
    runtime = app/'Contents/Resources/Runtime'
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
             app/'Contents/Library/LaunchServices/com.mac.virtualintelmac.usb-helper',
             app/'Contents/Library/LaunchServices/com.mac.virtualintelmac.network-helper']
    for path in hosts:
        architectures = set(run('lipo', '-archs', path).split())
        expected = {'x86_64'} if path.name == 'apple-gfx-helper' else {'arm64', 'x86_64'}
        if architectures != expected:
            raise RuntimeError(f'Wrong architectures: {path.name}: {architectures}')
        run('codesign', '--verify', '--strict', '-R', '=certificate leaf = H"'+identity+'"', path)
        for line in run('otool', '-arch', 'all', '-L', path).splitlines():
            if line.startswith('\t') and not line.strip().startswith(('/System/Library/', '/usr/lib/', '@loader_path/')):
                raise RuntimeError(f'Unbundled dependency in {path.name}: {line}')
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
        if sorted(p.name for p in directory.glob('EFI*.img'))!=['EFI.img']:
            raise RuntimeError('Ship exactly one EFI image per guest OS: '+guest)
        if hashlib.sha256((directory/'EFI.img').read_bytes()).hexdigest()!=receipt['EFIImages'][guest]:
            raise RuntimeError('EFI image checksum mismatch: '+guest)
        payload = receipt['GuestPayloads']['Guests'][guest]
        media = receipt['GuestSupportMedia'][guest]
        image = runtime/'GuestPayloads'/media['Image']
        with image.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if media['SHA256'] != actual:
            raise RuntimeError('Guest support media checksum mismatch: '+guest)
        # Packaging verifies each input, records the ISO hash, then deletes the
        # redundant loose archive. Cross-check its sealed media manifest too.
        entries = media['Files']
        name = 'payload.tar.xz' if payload['Archive'].endswith('.xz') else 'payload.tar.gz'
        if entries[name]['SHA256'] != payload['SHA256']:
            raise RuntimeError('Embedded guest payload manifest mismatch: '+guest)
        manifest = plistlib.loads((runtime/'GuestPayloads'/media['Manifest']).read_bytes())
        if manifest['Guest'] != guest or manifest['Files'] != entries:
            raise RuntimeError('Guest media manifest mismatch: '+guest)
    # A build must not accidentally copy the private key or local receipts into
    # the app. The helper policy contains only a public certificate fingerprint.
    for path in app.rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.p12', '.pfx', '.keychain', '.keychain-db'):
            raise RuntimeError('Private credential file in app: '+path.name)
    print('Verified: universal app/QEMU/helpers, consistent shared signing, portable linkage, quiet RELEASE firmware and all guest media.')


if __name__ == '__main__':
    main()
