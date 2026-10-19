#!/usr/bin/env python3
"""Build a universal USB-only SMJobBless helper and reciprocal signing policy.

No host install, authorization or security changes happen during a build.
usbredir/libusb are statically linked from the pinned universal dependencies.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from usb_dependency_state import usb_inputs_hash
import build_freshness as freshness

LABEL = 'com.mac.virtualintelmac.usb-helper'


def static_archive_hash(path):
    """Hash every archive member's name/code, not ar's build-host metadata.

    Darwin ranlib refreshes the symbol-table timestamp during make install.
    Hashing that timestamp caused a needless admin-helper upgrade even when
    the linked code had not changed. Member payloads remain fully covered.
    """
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        if stream.read(8) != b'!<arch>\n':
            raise RuntimeError(f'Expected a thin static archive: {path}')
        while header := stream.read(60):
            if len(header) != 60 or header[58:] != b'`\n':
                raise RuntimeError(f'Invalid static archive header: {path}')
            try:
                length = int(header[48:58])
            except ValueError as error:
                raise RuntimeError(f'Invalid static archive member size: {path}') from error
            if length < 0:
                raise RuntimeError(f'Invalid static archive member size: {path}')
            payload = stream.read(length)
            if len(payload) != length or (length % 2 and len(stream.read(1)) != 1):
                raise RuntimeError(f'Truncated static archive: {path}')
            digest.update(header[:16])
            digest.update(length.to_bytes(8, 'big'))
            digest.update(payload)
    return digest.digest()


def build(source, app, identity, dependencies=None, kind='usb'):
    from build_signing import resolve_identity
    identity = resolve_identity(source, identity)
    if kind not in ('usb', 'network'):
        raise ValueError('Unknown privileged service')
    label = LABEL if kind == 'usb' else 'com.mac.virtualintelmac.network-helper'
    folder = source/('USBHelper' if kind == 'usb' else 'NetworkHelper')
    token = kind.upper()
    if identity == '-' or not identity:
        raise RuntimeError('USB helper requires a persistent signing identity, not an ad-hoc signature.')
    fingerprint = identity
    if not re.fullmatch(r'[a-fA-F0-9]{40}', fingerprint):
        identities = subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
        matches = re.findall(r'\b([A-Fa-f0-9]{40})\s+"'+re.escape(identity)+'"', identities)
        if len(matches) != 1:
            raise RuntimeError('Select one unambiguous codesigning identity for the USB helper.')
        fingerprint = matches[0]
    fingerprint = fingerprint.upper()
    app_id = plistlib.loads((app/'Contents/Info.plist').read_bytes())['CFBundleIdentifier']
    if not re.fullmatch(r'[A-Za-z0-9.-]+', app_id):
        raise RuntimeError('Invalid application identifier')
    client_requirement = f'identifier "{app_id}" and certificate leaf = H"{fingerprint}"'
    helper_requirement = f'identifier "{label}" and certificate leaf = H"{fingerprint}"'
    deps = Path(dependencies or source/'Build/UniversalDependencies').resolve()
    if kind == 'usb':
        deps = freshness.ensure_dependencies(source, deps)
    for arch in (('arm64', 'x86_64') if kind == 'usb' else ()):
        for name in ('libusb-1.0.a', 'libusbredirhost.a', 'libusbredirparser.a'):
            if not (deps/arch/'lib'/name).is_file():
                raise RuntimeError('Build universal dependencies (including usbredir) before packaging.')
    inputs = [folder/'main.m', folder/f'VIM{kind.title()}Protocol.h', Path(__file__).resolve()] if kind == 'network' else [folder/'main.m', folder/'VIMUSBProtocol.h', folder/'VIMUSBDevicePolicy.h', folder/'VIMUSBPolicyProbe.h', Path(__file__).resolve()]
    digest = hashlib.sha256()
    for path in inputs:
        digest.update(path.read_bytes())
    if kind == 'usb':
        # The installed service statically contains these backends. A source-
        # only handshake otherwise incorrectly accepts an old installed helper
        # after a usbredir/libusb fix, even though the bundled service changed.
        for arch in ('arm64', 'x86_64'):
            for name in ('libusb-1.0.a', 'libusbredirhost.a', 'libusbredirparser.a'):
                digest.update((arch+'/'+name).encode())
                digest.update(static_archive_hash(deps/arch/'lib'/name))
    digest.update(client_requirement.encode())
    source_hash = digest.hexdigest()
    epoch = os.environ.get('SOURCE_DATE_EPOCH') or subprocess.check_output(
        ['git', '-C', str(source), 'log', '-1', '--format=%ct'], text=True).strip()
    # The source hash, not this display version, is the compatibility handshake.
    version = str(int(epoch))
    destination = app/'Contents/Library/LaunchServices'/label
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vim-usb-build-') as work:
        work = Path(work)
        (work/f'VIM{token if kind == "usb" else "Network"}Trust.h').write_text(
            f'#define VIM_{token}_CLIENT_REQUIREMENT @'+json.dumps(client_requirement)+'\n'
            f'#define VIM_{token}_SOURCE_HASH @'+json.dumps(source_hash)+'\n')
        helper_info = {
            'CFBundleIdentifier': label, 'CFBundleName': 'Virtual Intel Mac '+kind.title()+' Helper',
            'CFBundleVersion': version, 'CFBundleShortVersionString': '1.0',
            'SMAuthorizedClients': [client_requirement],
        }
        if kind == 'usb':
            helper_info['NSRemovableVolumesUsageDescription'] = 'Connect the USB drive you selected to your virtual Mac.'
        (work/'Info.plist').write_bytes(plistlib.dumps(helper_info))
        (work/'Launchd.plist').write_bytes(plistlib.dumps({
            'Label': label, 'MachServices': {label: True}, 'UserName': 'root',
            'ProgramArguments': ['/Library/PrivilegedHelperTools/'+label],
            'AssociatedBundleIdentifiers': [app_id],
        }))
        slices = []
        for arch in ('arm64', 'x86_64'):
            prefix = deps/arch
            output = work/arch
            libraries = [str(prefix/'lib/libusbredirhost.a'), str(prefix/'lib/libusbredirparser.a'), str(prefix/'lib/libusb-1.0.a')] if kind == 'usb' else ['-framework', 'vmnet']
            subprocess.run(['xcrun', 'clang', '-arch', arch, '-mmacosx-version-min=11.0',
                '-O2', '-fobjc-arc', '-fmodules', '-Wall', '-Wextra',
                '-I'+str(work), '-I'+str(prefix/'include/libusb-1.0'), '-I'+str(prefix/'include'),
                str(folder/'main.m'), *libraries,
                '-framework', 'Foundation', '-framework', 'Security', '-framework', 'IOKit',
                '-framework', 'CoreFoundation', *(['-lbsm'] if kind == 'usb' else []),
                '-Wl,-sectcreate,__TEXT,__info_plist,'+str(work/'Info.plist'),
                '-Wl,-sectcreate,__TEXT,__launchd_plist,'+str(work/'Launchd.plist'),
                '-o', str(output)], check=True)
            slices.append(output)
        subprocess.run(['lipo', '-create', *map(str, slices), '-output', str(destination)], check=True)
    destination.chmod(0o755)
    subprocess.run(['codesign', '--force', '--sign', identity, '--identifier', label, '--timestamp=none', str(destination)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', '-R', '='+helper_requirement, str(destination)], check=True)
    info_path = app/'Contents/Info.plist'
    info = plistlib.loads(info_path.read_bytes())
    info.setdefault('SMPrivilegedExecutables', {})[label] = helper_requirement
    info['VIM'+('USB' if kind == 'usb' else 'Network')+'HelperSourceHash'] = source_hash
    info_path.write_bytes(plistlib.dumps(info))
    notices = app/'Contents/Resources/Licenses'
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source/'vendor/usbredir/COPYING.LIB', notices/'usbredir-LGPL-2.1.txt')
    result = {'Protocol': 1, 'SourceHash': source_hash, 'SigningRequirement': helper_requirement, 'Version': version}
    receipt_path = app/'Contents/Resources/Runtime/Packaging.plist'
    if receipt_path.is_file():
        receipt = plistlib.loads(receipt_path.read_bytes())
        receipt['USBHelper' if kind == 'usb' else 'NetworkHelper'] = result
        receipt_path.write_bytes(plistlib.dumps(receipt))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--dependencies', type=Path)
    args = parser.parse_args()
    print(build(Path(__file__).resolve().parents[1], args.app, args.identity, args.dependencies))
