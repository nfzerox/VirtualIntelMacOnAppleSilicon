#!/usr/bin/env python3
"""Stage an independent development app and its native runtime dependency closure.

Does not download, rebuild QEMU, edit the input app, or alter guest disks.
Ad-hoc signing is development-only; stock-host qualification is a separate gate.
"""
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import re
import runpy
import shutil
import subprocess
import sys
import tarfile
import tempfile
import build_freshness as freshness


def extract_host_inputs(source, destination, archive_name='apple-silicon-host-inputs.tar.gz', manifest_name='apple-silicon-SHA256SUMS'):
    """Use the checked-in donor, never an untracked research extraction."""
    if not hasattr(tarfile, 'data_filter'):
        raise RuntimeError('Python with tarfile.data_filter is required (use Python 3.12 or later). '
                           'The Xcode-bundled Python 3.9 is not supported; archive safety checks must not be bypassed.')
    archive = source/'vendor'/archive_name
    manifest = source/'vendor'/manifest_name
    matches = [line.split()[0] for line in manifest.read_text().splitlines()
               if len(line.split()) == 2 and line.split()[1] == archive.name]
    if len(matches) != 1:
        raise RuntimeError('host donor checksum entry is missing or ambiguous')
    digest = hashlib.sha256()
    with archive.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    if digest.hexdigest() != matches[0]:
        raise RuntimeError('checked-in host donor archive checksum mismatch')
    with tarfile.open(archive, 'r:gz') as stream:
        # data filter preserves safe relative framework symlinks but rejects
        # paths/links escaping the new staging directory and special devices.
        stream.extractall(destination, filter='data')
    return digest.hexdigest()


def run(*args):
    return subprocess.check_output([str(x) for x in args], text=True).strip()


def qemu_submodule_pin(qemu, path):
    """A QEMU submodule's pinned commit. The vendored tree (Source/vendor/qemu)
    records pins in QEMU_UPSTREAM beside it; a separate checkout has gitlinks."""
    manifest = Path(qemu).resolve().parent/'QEMU_UPSTREAM'
    if manifest.is_file():
        for line in manifest.read_text().splitlines():
            fields = line.split()
            if fields[:2] == ['submodule', path]:
                return fields[3]
    return run('git', '-C', qemu, 'rev-parse', 'HEAD:' + path)


def quiet_firmware(source, qemu, candidate=None):
    """Production and development use the same unbranded firmware by default.

    Preserve existing NVRAM geometry/template; quiet boot does not imply that
    the separate, still-open S3 resume problem is fixed.
    """
    production = candidate is None
    candidate = Path(candidate).resolve() if candidate else source/'prebuilt/firmware'
    receipt = plistlib.loads((candidate/'BuildReceipt.plist').read_bytes())
    if production and receipt.get('Configuration') != 'RELEASE':
        raise RuntimeError('Production quiet firmware must be a RELEASE build; rebuild with build-guest-firmware.py')
    if (receipt.get('EDKRevision') != qemu_submodule_pin(qemu, 'roms/edk2') or
            receipt.get('GuestArchitecture') != 'x86_64' or not receipt.get('QuietBoot')):
        raise RuntimeError('Quiet guest firmware does not match the pinned source')
    code = candidate/'OVMF_CODE.fd'
    if code.stat().st_size != 3653632 or hashlib.sha256(code.read_bytes()).hexdigest() != receipt['Hashes'].get(code.name):
        raise RuntimeError('Quiet guest firmware geometry/checksum mismatch')
    return code, receipt


def registration_kext(source, destination):
    """Use our already-vendored tiny driver; no KDK/download on UI builders."""
    archive=source/'prebuilt/catalina-gl/catalina-gl.tar.gz'
    checks=source/'prebuilt/catalina-gl/SHA256SUMS'
    expected=[line.split()[0] for line in checks.read_text().splitlines()
              if len(line.split())==2 and line.split()[1]==archive.name]
    if expected!=[hashlib.sha256(archive.read_bytes()).hexdigest()]:
        raise RuntimeError('GL registration source archive failed integrity verification')
    prefix='payload/Library/Extensions/PVGOpenGLProbe.kext/'
    destination.mkdir(parents=True,exist_ok=False)
    with tarfile.open(archive,'r:gz') as stream:
        for member in stream:
            if not member.name.startswith(prefix):continue
            relative=Path(member.name[len(prefix):])
            if relative.is_absolute() or '..' in relative.parts or not(member.isdir() or member.isfile()):
                raise RuntimeError('Unsafe registration kext member')
            path=destination/relative
            if member.isdir():path.mkdir(parents=True,exist_ok=True)
            else:
                path.parent.mkdir(parents=True,exist_ok=True)
                with stream.extractfile(member) as data:path.write_bytes(data.read())
                path.chmod(member.mode&0o777)
    if not (destination/'Contents/MacOS/PVGOpenGLProbe').is_file():
        raise RuntimeError('GL registration executable is missing')
    return destination


def dependencies(path):
    result = []
    for line in run('/usr/bin/otool', '-arch', 'all', '-L', path).splitlines():
        if line.startswith('\t'):
            dependency = line.strip().split(' (compatibility')[0]
            if dependency not in result:
                result.append(dependency)
    return result


def rpaths(path):
    lines = run('/usr/bin/otool', '-arch', 'all', '-l', path).splitlines()
    result = []
    for i, line in enumerate(lines):
        if line.strip() == 'cmd LC_RPATH':
            result.append(lines[i+2].strip().split('path ', 1)[1].split(' (offset')[0])
    return result


def require_universal(path, minimum=(11, 0, 0), *, exact=False):
    architectures = set(run('/usr/bin/lipo', '-archs', path).split())
    if not {'arm64', 'x86_64'}.issubset(architectures):
        raise RuntimeError(f'Host runtime must contain arm64 and x86_64: {path} ({architectures})')
    require_macos_11(path, minimum, exact=exact)


def require_macos_11(path, minimum=(11, 0, 0), *, exact=False):
    """Reject stale host artifacts even when their build receipt looks current.

    The runtime targets macOS 11; the app itself may target a newer release."""
    commands = run('/usr/bin/otool', '-arch', 'all', '-l', path)
    versions = re.findall(r'^\s*minos\s+(\d+(?:\.\d+)*)', commands, re.MULTILINE)
    versions += re.findall(r'cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version (\d+(?:\.\d+)*)', commands)
    parsed = [tuple(map(int, (v+'.0.0').split('.')[:3])) for v in versions]
    if exact and (not parsed or any(v != minimum for v in parsed)):
        raise RuntimeError(f'Host app must target exactly macOS {minimum[0]}.{minimum[1]}: {path}: {versions}')
    if not parsed or any(v > minimum for v in parsed):
        raise RuntimeError(f'Host binary requires newer than macOS {minimum[0]}.{minimum[1]} (or lacks minimum): {path}: {versions}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path, help='new .app path, preferably external scratch')
    parser.add_argument('--qemu', required=True, type=Path)
    parser.add_argument('--runtime-bin', type=Path, required=True, help='universal runtime directory from build-app-runtime.py')
    parser.add_argument('--guest-tools', type=Path, help='compiled guest tools directory; default is checked-in fast-build binaries')
    parser.add_argument('--signing-identity', help='persistent host identity; private builds always use the shared repository certificate')
    parser.add_argument('--dependencies', type=Path, help='same static dependency directory used to build this QEMU runtime')
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Debug')
    parser.add_argument('--firmware-build', type=Path, help='explicit development candidate from build-guest-firmware.py; preserves the existing NVRAM template')
    parser.add_argument('--boot-policy-build', type=Path, help='rebuilt EFI policy components; default is verified checked-in components')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    from build_signing import resolve_identity
    args.signing_identity = resolve_identity(source, args.signing_identity)
    app_info = plistlib.loads((args.app/'Contents/Info.plist').read_bytes())
    if (app_info.get('CFBundleIconName') != 'AppIcon'
            or not (args.app/'Contents/Resources/AppIcon.icns').is_file()
            or any((args.app/'Contents/Resources'/name).exists() for name in ('VirtualIntelMac.icns','FolderIcon.icns'))):
        parser.error('Build the app with its Icon Composer icon and no legacy ICNS overrides before packaging.')
    runtime_build = plistlib.loads((args.runtime_bin/'BuildReceipt.plist').read_bytes())
    if not freshness.current(runtime_build, freshness.snapshot(source,'runtime',args.qemu),args.runtime_bin,
                              ('qemu-system-x86_64','qemu-img','apple-gfx-helper')):
        parser.error('Runtime is stale, modified, or lacks a source/toolchain fingerprint. Use build-app to rebuild it.')
    if runtime_build.get('AppDisplayMaxDimension') != 8192 or runtime_build.get('AppleANSIKeyboard') is not True:
        parser.error('Runtime predates the app display/input protocol; run build-app --rebuild-runtime once.')
    if not runtime_build.get('AudioDeviceSelection') or not runtime_build.get('DisplayScalePolicy'):
        parser.error('Runtime lacks audio-device routing or display-scale selection. Rebuild the existing runtime incrementally before packaging.')
    if not runtime_build.get('DisplayPowerEvents') or not runtime_build.get('GuestDisplayPowerEvents'):
        parser.error('Runtime lacks guest display-power events. Rebuild the universal runtime before packaging.')
    if not runtime_build.get('DirectDMG'):
        parser.error('Runtime lacks direct DMG support. Rebuild the existing runtime incrementally before packaging.')
    if not runtime_build.get('USBPassthrough'):
        parser.error('Runtime predates USB passthrough and the current audio fixes. Rebuild the universal runtime before packaging; do not pair new device UI with the old QEMU binary.')
    if not runtime_build.get('USBRedirection'):
        parser.error('Runtime lacks usb-redir for the USB-only capture helper. Rebuild the universal runtime once.')
    usb_model=subprocess.run([str(args.runtime_bin/'qemu-system-x86_64'),'-device','usb-host,help'],
                             capture_output=True,text=True,timeout=15)
    if usb_model.returncode:
        parser.error('Runtime claims USB support but does not provide usb-host. Rebuild with the pinned universal libusb dependencies.')
    guest_tools = args.guest_tools or source/'prebuilt/tools'
    if args.output.exists() or args.output.suffix != '.app':
        parser.error('output must be a new .app path; existing outputs are never overwritten')
    donor_stage = tempfile.TemporaryDirectory(prefix='vim-host-donor-')
    donor_root = Path(donor_stage.name)
    donor_hash = extract_host_inputs(source, donor_root)
    donor = donor_root/'System/Library/Frameworks/ParavirtualizedGraphics.framework'
    firmware_root = donor_root/'FirmwareInputs'
    firmware_root.mkdir()
    firmware_hash = extract_host_inputs(source, firmware_root, 'app-firmware-inputs.tar.gz', 'app-firmware-SHA256SUMS')
    inputs = {
        'GuestTools/guest-image-patch': guest_tools/'guest-image-patch',
        'GuestTools/guest-status': guest_tools/'guest-status',
        'GuestTools/guest-display': guest_tools/'guest-display',
        'GuestTools/guest-clipboard': guest_tools/'guest-clipboard',
        'GuestTools/guest-macho': guest_tools/'guest-macho',
        'GuestTools/guest-prepare': guest_tools/'guest-prepare',
        'GuestTools/guest-install': guest_tools/'guest-install',
        'GuestTools/guest-progress': guest_tools/'guest-progress',
        'GuestTools/codesign_allocate': guest_tools/'codesign_allocate',
        'bin/qemu-system-x86_64': args.runtime_bin/'qemu-system-x86_64',
        'bin/apple-gfx-helper': args.runtime_bin/'apple-gfx-helper',
        'bin/qemu-img': args.runtime_bin/'qemu-img',
        'Donor/ParavirtualizedGraphics.framework/Versions/A/ParavirtualizedGraphics': donor/'Versions/A/ParavirtualizedGraphics',
        'Donor/libLLVM.dylib': donor_root/'System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/31001/Libraries/libLLVM.dylib',
    }
    for guest in ('mojave', 'catalina', 'bigsur'):
        for leaf in ['CODE.fd','VARS.fd']:
            inputs[f'Firmware/{guest}/{leaf}'] = firmware_root/leaf
        inputs[f'Firmware/{guest}/BOOT.img'] = firmware_root/guest/'BOOT.img'
    firmware_code, firmware_candidate = quiet_firmware(source, args.qemu, args.firmware_build)
    for guest in ('mojave', 'catalina', 'bigsur'):
        inputs[f'Firmware/{guest}/CODE.fd'] = firmware_code
    for path in [args.app/'Contents/MacOS/Virtual Intel Mac', args.qemu/'pc-bios', *inputs.values()]:
        if not path.exists():
            parser.error(f'missing input: {path}')
    require_universal(args.app/'Contents/MacOS/Virtual Intel Mac', (15, 0, 0), exact=True)
    for path in [inputs['bin/qemu-system-x86_64'], inputs['bin/qemu-img']]:
        require_universal(path)
    require_macos_11(inputs['bin/apple-gfx-helper'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(args.app, args.output, symlinks=True)
    runtime = args.output/'Contents/Resources/Runtime'
    if runtime.exists():
        raise RuntimeError('input app already contains a runtime; build a clean input app')
    runtime.mkdir()
    guest_payload_recipe = runpy.run_path(str(source/'scripts/package-app-guest-payloads.py'))
    surface_helper = guest_tools/'PVGGLIOKit.dylib'
    bigsur_helpers=guest_tools if (guest_tools/'PVGBigSurMetalCompat.dylib').is_file() and (guest_tools/'PVGOpenGLMetal.dylib').is_file() else source/'prebuilt/bigsur'
    guest_payload_receipt = guest_payload_recipe['build'](
        source, runtime/'GuestPayloads', surface_helper if surface_helper.is_file() else None, bigsur_helpers,
        guest_tools/'IOSurfaceABI' if (guest_tools/'IOSurfaceABI').is_dir() else None)
    # NSBundle-based shader lookup requires the framework structure/resources,
    # not just a relocatable Mach-O image.
    shutil.copytree(donor, runtime/'Donor/ParavirtualizedGraphics.framework', symlinks=True)
    queue = []
    for relative, original in inputs.items():
        # BOOT.img is only a build input. The app boots the generated GPT ESP,
        # never this unwrapped FAT image. Keep provenance in the vendor archive,
        # not three additional 64 MiB copies in every installed app.
        if relative.startswith('Firmware/') and relative.endswith('/BOOT.img'):
            continue
        dest = runtime/relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, dest)
        dest.chmod(original.stat().st_mode & 0o777)
        if relative.startswith(('bin/', 'Donor/', 'GuestTools/')):
            queue.append((original.resolve(), dest))
    # This app always emulates an Intel PC, including on ARM hosts. Do not ship
    # firmware for SPARC, POWER, s390, RISC-V, etc., or QEMU's alternative UEFI
    # builds when our own pflash firmware is selected explicitly.
    bios = runtime/'pc-bios'
    bios.mkdir()
    bios_inputs = args.qemu/'pc-bios'
    selected = set()
    for pattern in ('*.rom', 'bios*.bin', 'vgabios*.bin', 'kvmvapic.bin',
                    'linuxboot*.bin', 'multiboot*.bin', 'pvh.bin'):
        selected.update(p for p in bios_inputs.glob(pattern) if p.is_file())
    required_bios = {'vgabios-stdvga.bin', 'kvmvapic.bin', 'efi-e1000.rom'}
    if not required_bios.issubset({p.name for p in selected}):
        raise RuntimeError('Required PC firmware is missing from QEMU')
    for original in sorted(selected):
        shutil.copyfile(original, bios/original.name)
    boot_policy=args.boot_policy_build or source/'prebuilt/firmware/BootPolicy'
    boot_policy_receipt=plistlib.loads((boot_policy/'BuildReceipt.plist').read_bytes())
    for guest in ('mojave', 'catalina', 'bigsur'):
        firmware = runtime/'Firmware'/guest
        options=['--quiet','--runtime-policy',boot_policy]
        if guest=='bigsur':options+=['--bigsur-gl-registration',boot_policy/'PVGOpenGLProbe.kext']
        run(sys.executable, source/'scripts/make-efi-disk.py',
            inputs[f'Firmware/{guest}/BOOT.img'], firmware/'EFI.img', *options)
    # Resolve the actual linked closure, not a manually curated Homebrew list.
    # Only system paths may remain external. Unresolved rpaths fail packaging.
    copied = {}
    binaries = []
    while queue:
        original, dest = queue.pop(0)
        binaries.append(dest)
        for dependency in dependencies(original):
            if dependency.startswith(('/System/Library/', '/usr/lib/')):
                continue
            expanded = dependency.replace('@loader_path', str(original.parent))
            if dependency.startswith('@rpath/'):
                candidates = [Path(p.replace('@loader_path', str(original.parent)))/dependency[7:] for p in rpaths(original)]
                expanded = next((str(p) for p in candidates if p.is_file()), '')
            dep = Path(expanded)
            if not dep.is_absolute() or not dep.is_file():
                raise RuntimeError(f'unresolved dependency {dependency} in {original}')
            dep = dep.resolve()
            target = runtime/'lib'/dep.name
            if target.name in copied and copied[target.name] != dep:
                raise RuntimeError(f'dylib name collision: {dep} and {copied[target.name]}')
            if target.name not in copied:
                copied[target.name] = dep
                target.parent.mkdir(exist_ok=True)
                shutil.copyfile(dep, target)
                target.chmod(0o755)
                queue.append((dep, target))
            replacement = '@loader_path/' + os.path.relpath(target, dest.parent)
            run('/usr/bin/install_name_tool', '-change', dependency, replacement, dest)
        if dest.parent.name == 'lib':
            run('/usr/bin/install_name_tool', '-id', '@loader_path/'+dest.name, dest)
            require_universal(dest)
    # Dylib IDs are not dependencies, but setting them also removes build paths.
    for binary in binaries:
        for attr in ('com.apple.FinderInfo', 'com.apple.ResourceFork'):
            if subprocess.run(['/usr/bin/xattr', '-p', attr, str(binary)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                run('/usr/bin/xattr', '-d', attr, binary)
        if binary == runtime/'GuestTools/codesign_allocate':
            # The guest's codesign requires an Apple-signed allocator. An ad-hoc
            # signature verifies on its own, but fails that caller requirement.
            # This tool links only libSystem: preserve its exact vendor bytes.
            if binary.read_bytes() != (guest_tools/'codesign_allocate').read_bytes():
                raise RuntimeError('The Apple guest signing allocator was modified')
            run('/usr/bin/codesign', '--verify', '--strict', binary)
        else:
            # TCC and helper trust depend on the host identity surviving every
            # rebuild. Guest executables intentionally retain guest signing.
            host_binary = binary.parent in (runtime/'bin', runtime/'lib')
            command = ['/usr/bin/codesign', '--force', '--sign', args.signing_identity if host_binary else '-']
            if binary == runtime/'bin/qemu-system-x86_64':
                command += ['--entitlements', str(args.qemu/'accel/hvf/entitlements.plist')]
            run(*command, binary)
        remaining = [p for p in dependencies(binary) if not p.startswith(('/System/Library/', '/usr/lib/', '@loader_path/'))]
        if remaining:
            raise RuntimeError(f'non-portable dependencies in {binary}: {remaining}')
    # One baseline runtime: no restricted TSO entitlement or launch-time probe.
    run('/usr/bin/codesign', '--force', '--sign', '-', runtime/'Donor/ParavirtualizedGraphics.framework')
    for guest in ('mojave', 'catalina', 'bigsur'):
        shutil.copyfile(source/'VirtualIntelMac/Assets.xcassets'/f'{guest}Icon.imageset/art.png',
                        runtime/'GuestTools'/f'{guest}-support-icon.png')
    support_media = runpy.run_path(str(source/'scripts/package-guest-support-media.py'))['build'](
        runtime/'GuestPayloads', runtime/'GuestTools')
    receipt = {'FormatVersion': 2, 'HostArchitectures': ['arm64', 'x86_64'],
               'AppConfiguration': args.configuration,
               'MinimumAppVersion': '15.0', 'MinimumRuntimeVersion': '11.0',
               'QEMUCheckoutRevision': run('git','-C',args.qemu,'rev-parse','HEAD:./'),
               'QEMUBinaryVersion': run(runtime/'bin/qemu-system-x86_64', '--version'),
               'DevelopmentAdHocSigned': args.signing_identity == '-', 'HostSigningIdentity': args.signing_identity, 'StockHostValidated': False,
               'HostDonorArchiveSHA256': donor_hash,
               'FirmwareArchiveSHA256': firmware_hash,
               'RuntimeBootPolicy':boot_policy_receipt,
               'EFIImages':{guest:hashlib.sha256((runtime/'Firmware'/guest/'EFI.img').read_bytes()).hexdigest()
                            for guest in ('mojave','catalina','bigsur')},
               'GuestPayloads': guest_payload_receipt,
               'GuestSupportMedia': support_media,
               'Libraries': {name: str(path) for name,path in copied.items()}}
    if firmware_candidate:
        receipt['FirmwareCandidate'] = firmware_candidate
    receipt['GuestTools'] = {name: {'InputSHA256': hashlib.sha256((guest_tools/name).read_bytes()).hexdigest(),
                                  'PackagedSHA256': hashlib.sha256((runtime/'GuestTools'/name).read_bytes()).hexdigest()}
                             for name in ('guest-image-patch', 'guest-status', 'guest-macho', 'guest-prepare', 'guest-install', 'guest-progress', 'guest-display', 'codesign_allocate')}
    if (guest_tools/'BuildReceipt.plist').is_file():
        receipt['GuestToolsBuild'] = plistlib.loads((guest_tools/'BuildReceipt.plist').read_bytes())
        for name, hashes in receipt['GuestTools'].items():
            if receipt['GuestToolsBuild']['Tools'][name]['BinarySHA256'] != hashes['InputSHA256']:
                raise RuntimeError(f'Guest tool does not match build receipt: {name}')
    if args.runtime_bin:
        with (args.runtime_bin/'BuildReceipt.plist').open('rb') as stream:
            receipt['RuntimeBuild'] = plistlib.load(stream)
        # Full build commands/logs remain outside the product. Receipts in the
        # shipped app must not expose the builder's login name or disk paths.
        build = receipt['RuntimeBuild']
        build.pop('BuildDirectory', None)
        build.pop('DependencyPrefixes', None)
        build['ConfigureArguments'] = {
            arch: [re.sub(r'--python=.*', '--python=${BUILD_PYTHON}', arg) for arg in arguments]
            for arch, arguments in build.get('ConfigureArguments', {}).items()}
    receipt['USBHelper'] = runpy.run_path(str(source/'scripts/build-usb-helper.py'))['build'](
        source, args.output, args.signing_identity, dependencies=args.dependencies)
    receipt['NetworkHelper'] = runpy.run_path(str(source/'scripts/build-usb-helper.py'))['build'](
        source, args.output, args.signing_identity, dependencies=args.dependencies, kind='network')
    with (runtime/'Packaging.plist').open('wb') as stream:
        plistlib.dump(receipt, stream)
    run('/usr/bin/codesign', '--force', '--sign', args.signing_identity, args.output)
    run('/usr/bin/codesign', '--verify', '--deep', '--strict', args.output)
    print(f'Packaged {args.output}; {len(copied)} linked libraries. Stock-host testing remains required.')


if __name__ == '__main__':
    main()
