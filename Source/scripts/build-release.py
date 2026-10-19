#!/usr/bin/env python3
"""Build a complete universal release candidate from a clean committed checkout.

No existing compiled output, dependency prefix or DerivedData is reused. Apple
donors and qualified guest payloads are checked-in inputs, not re-extracted from
an installed OS. Nothing is installed in a VM or uploaded. Xcode and Python 3.12+
are prerequisites; small pinned build tools are bootstrapped in a private venv.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve().parents[1]
TOOLS = ('meson==1.12.0', 'ninja==1.13.2')


def run(*command, **kwargs):
    print('+', ' '.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def output(*command):
    return subprocess.check_output(list(map(str, command)), text=True).strip()


def digest(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def finalize(app, destination, receipt, work):
    # ExFAT reports every file as mode 700. Create the distributable on APFS
    # with real Unix modes, so installing it for other users remains possible.
    stage = work/'Distribution'
    stage.mkdir(exist_ok=False)
    release_folder = stage/'Virtual Intel Mac'
    release_folder.mkdir(mode=0o755)
    staged_app = release_folder/app.name
    run('/usr/bin/ditto', '--noextattr', '--noacl', app, staged_app)
    magics = (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf', b'\xcf\xfa\xed\xfe',
              b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xfe\xed\xfa\xce')
    for path in [staged_app, *staged_app.rglob('*')]:
        if path.is_symlink():
            continue
        if path.is_dir():
            path.chmod(0o755)
        elif path.is_file():
            with path.open('rb') as file:
                header = file.read(4)
            path.chmod(0o755 if header in magics or header.startswith(b'#!') else 0o644)
    run('codesign', '--verify', '--deep', '--strict', staged_app)
    # Keep the opening instructions alongside the app, not hidden inside it.
    instructions = SOURCE/'support/How to Open Virtual Intel Mac.webloc'
    if not instructions.is_file():
        raise RuntimeError('The release opening-instructions link is missing')
    staged_instructions = release_folder/instructions.name
    shutil.copyfile(instructions, staged_instructions)
    staged_instructions.chmod(0o644)
    archive = destination/'VirtualIntelMac.zip'
    if archive.exists():
        raise RuntimeError('Refusing to overwrite an existing release ZIP')
    run('/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', stage, archive)
    run('/usr/bin/unzip', '-tq', archive)
    import zipfile
    with zipfile.ZipFile(archive) as packaged:
        names = packaged.namelist()
        prefix = release_folder.name+'/'
        if (prefix+instructions.name not in names or prefix+app.name+'/Contents/Info.plist' not in names
                or any(not name.startswith((prefix,'__MACOSX/')) for name in names)):
            raise RuntimeError('The ZIP must contain one Virtual Intel Mac folder with the app and opening instructions side by side')
    receipt['ArchiveRootDirectory'] = release_folder.name
    receipt['ArchiveSHA256'] = digest(archive)
    (destination/'ReleaseBuild.plist').write_bytes(plistlib.dumps(receipt))
    (destination/'SHA256SUMS').write_text(receipt['ArchiveSHA256']+'  '+archive.name+'\n')
    print(f'\nComplete {receipt["Configuration"]} candidate: {app}\nZIP: {archive}\nBuild logs/work: {work}\n'
          'Build checks passed. Runtime qualification and distribution approval are separate gates.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='new output folder; defaults to a folder chooser (or VIM_BUILD_SCRATCH)')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument('--configuration', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--use-prebuilt-firmware', action='store_true',
                        help='use the hash-verified quiet RELEASE firmware instead of rebuilding EDK II')
    args = parser.parse_args()
    if sys.platform != 'darwin' or platform.machine() not in ('arm64', 'x86_64'):
        parser.error('Build on an Intel or Apple silicon Mac with full Xcode installed.')
    if not 1 <= args.jobs <= 64:
        parser.error('--jobs must be between 1 and 64')
    if output('git', '-C', SOURCE, 'status', '--porcelain', '--untracked-files=normal'):
        parser.error('Commit or stash source changes first. Release builds use committed files only.')
    revision = output('git', '-C', SOURCE, 'rev-parse', 'HEAD')
    from build_signing import resolve_identity
    # Resolve in the persistent checkout, not its disposable clean clone.
    # Otherwise public-source clean builds would generate a new local key on
    # every run. Private checkouts always restore their shared archive instead.
    signing_identity = resolve_identity(SOURCE)
    epoch = output('git', '-C', SOURCE, 'show', '-s', '--format=%ct', revision)
    xcode = output('xcodebuild', '-version')
    sdk = output('xcrun', '--show-sdk-version')
    if args.output is None:
        parent = os.environ.get('VIM_BUILD_SCRATCH')
        if not parent:
            # Native save-panel helper, not AppleScript; usable by double-click
            # without assuming any builder's user name or external drive name.
            chooser = SOURCE/'Build/choose-release-directory'
            chooser.parent.mkdir(parents=True, exist_ok=True)
            run('xcrun', 'clang', '-fobjc-arc', SOURCE/'scripts/choose-release-directory.m',
                '-framework', 'AppKit', '-o', chooser)
            parent = output(chooser)
            if not parent:
                raise SystemExit('Build cancelled. No source or output was changed.')
        Path(parent).mkdir(parents=True, exist_ok=True)
        args.output = Path(tempfile.mkdtemp(prefix='VirtualIntelMac-'+revision[:8]+'-', dir=parent))
    elif args.output.exists():
        parser.error('--output must be a new folder; previous releases are never overwritten')
    else:
        args.output.mkdir(parents=True)
    args.output = args.output.resolve()
    # QEMU cannot configure in a path containing spaces. Use macOS's internal
    # temporary volume, independent of the name/location of the user's clone.
    # A fresh clone excludes ignored developer files and all build caches.
    work = Path(tempfile.mkdtemp(prefix='vim-clean-build-', dir='/private/var/tmp'))
    checkout = work/'Checkout'
    run('git', 'clone', '--local', '--no-hardlinks', '--quiet', SOURCE.parent, checkout)
    run('git', '-C', checkout, 'checkout', '--detach', revision)
    source = checkout/'Source'
    environment = dict(os.environ)
    for key in ('CFLAGS', 'CXXFLAGS', 'OBJCFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CC', 'CXX',
                'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET', 'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR',
                'PYTHONPATH', 'PYTHONHOME', 'VIM_CODESIGN_IDENTITY', 'CCACHE_DIR'):
        environment.pop(key, None)
    environment.update(SOURCE_DATE_EPOCH=epoch, PYTHONHASHSEED='0', CCACHE_DISABLE='1',
                       VIM_CODESIGN_IDENTITY=signing_identity)
    venv = work/'Tools'
    run(sys.executable, '-m', 'venv', venv, env=environment)
    python = venv/'bin/python3'
    run(python, '-m', 'pip', 'install', '--disable-pip-version-check', *TOOLS, env=environment)
    environment['PATH'] = str(venv/'bin')+os.pathsep+environment.get('PATH', '')
    firmware = []
    if not args.use_prebuilt_firmware:
        run(python, source/'scripts/build-guest-firmware.py', '--bootstrap-tools',
            '--configuration', 'RELEASE', '--jobs', args.jobs, env=environment)
        firmware = ['--firmware-build', source/'Build/GuestFirmware/Output-RELEASE']
    app = args.output/'Virtual Intel Mac.app'
    # Full builds recompile guest tools and both IOSurface ABI kexts as well as
    # both QEMU slices, static dependencies and the Intel graphics helper.
    run(python, source/'scripts/build-app', '--configuration', args.configuration,
        '--rebuild-runtime', '--dependencies', source/'Build/UniversalDependencies',
        '--jobs', args.jobs, '--output', app, *firmware, env=environment)
    run(python, source/'scripts/verify-release-build.py', '--app', app,
        '--source', source, '--configuration', args.configuration, env=environment)
    packaging = plistlib.loads((app/'Contents/Resources/Runtime/Packaging.plist').read_bytes())
    receipt = {
        'FormatVersion': 2, 'SourceCommit': revision, 'SourceTree': output('git', '-C', checkout, 'rev-parse', 'HEAD^{tree}'),
        'Configuration': args.configuration, 'CleanCheckout': True, 'ReusedBuildOutputs': False,
        'BuilderArchitecture': platform.machine(), 'Xcode': xcode, 'SDKVersion': sdk,
        'PythonVersion': output(python, '--version'), 'BuildTools': list(TOOLS),
        'HostSigningIdentity': packaging['HostSigningIdentity'],
        'FirmwareRebuilt': bool(firmware) or packaging['FirmwareCandidate'].get('Freshness') !=
            plistlib.loads((source/'prebuilt/firmware/BuildReceipt.plist').read_bytes()).get('Freshness'),
        'RuntimeRebuilt': True, 'GuestToolsRebuilt': True, 'GuestDriversRebuilt': True,
        'FreshnessVerified': True, 'RuntimeFingerprint': packaging['RuntimeBuild']['Freshness']['Fingerprint'],
        'CheckedInInputs': 'Apple donor extractions, qualified guest payload seeds, OpenCore, PC ROMs and Apple codesign_allocate',
        'BuildVerificationPassed': True, 'RuntimeQualificationPassed': False, 'Notarized': False,
    }
    finalize(app, args.output, receipt, work)


if __name__ == '__main__':
    main()
