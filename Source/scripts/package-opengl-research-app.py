#!/usr/bin/env python3
"""Package an explicitly non-release PassthroughOpenGL experiment.

Uses an existing app's runtime/artwork, a newly compiled universal UI, both
new QEMU slices, and the same-interpreter in-process GL library. Does not
manufacture a production freshness receipt or modify any input app/VM.
The project selects the compatible Icon Composer source for each Xcode version.
--use-ui-artwork retains that compiled icon and asset catalog instead of taking
artwork from the reference app.
"""
import argparse
import hashlib
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import runpy


def run(*args):
    return subprocess.check_output([str(x) for x in args], text=True).strip()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for option in ('reference-app', 'ui-app', 'qemu-x86', 'qemu-arm', 'executor', 'output'):
        p.add_argument('--' + option, type=Path, required=True)
    p.add_argument('--signing-identity', required=True)
    p.add_argument('--use-ui-artwork', action='store_true',
                   help='retain the newly compiled UI asset catalog and Icon Composer icon')
    a = p.parse_args()
    if a.output.exists() or a.output.suffix != '.app':
        p.error('--output must be a NEW .app path')
    source = Path(__file__).resolve().parents[1]
    for path, expected in ((a.qemu_x86, {'x86_64'}), (a.qemu_arm, {'arm64'}),
                           (a.executor, {'x86_64', 'arm64'}),
                           (a.ui_app/'Contents/MacOS/Virtual Intel Mac', {'x86_64', 'arm64'})):
        if set(run('lipo', '-archs', path).split()) != expected:
            p.error('Unexpected architectures: ' + str(path))
        for line in run('otool', '-arch', 'all', '-L', path).splitlines():
            if line.startswith('\t'):
                dep = line.strip().split(' (compatibility')[0]
                if path == a.executor and dep == '@rpath/libVIMPassthroughOpenGLHost.dylib':
                    continue  # own install name, not an external dependency
                if not dep.startswith(('/System/Library/', '/usr/lib/')):
                    p.error('Unpackaged dependency: ' + dep)
    runtime = a.reference_app/'Contents/Resources/Runtime'
    if not runtime.is_dir():
        p.error('The reference app has no bundled runtime')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    # A previously packaged research app is also a valid unchanged-UI input.
    # Its runtime is replaced explicitly below, not copied twice or allowed
    # to collide with copytree's destination. Never omit unrelated UI assets.
    def omit_old_runtime(directory, names):
        return ['Runtime'] if Path(directory) == a.ui_app/'Contents/Resources' and 'Runtime' in names else []
    shutil.copytree(a.ui_app, a.output, symlinks=True, ignore=omit_old_runtime)
    resources = a.output/'Contents/Resources'
    shutil.copytree(runtime, resources/'Runtime', symlinks=True)
    # A shared helper can change during pre-Metal bring-up too. Do not silently
    # retain the reference app's older copy (notably guest-clipboard) while
    # claiming to exercise the current source. Keep unrelated reference assets.
    tools = source/'prebuilt/tools'
    published_tools = []
    for file in sorted(tools.iterdir()):
        name = file.name
        target = resources/'Runtime/GuestTools'/name
        if not file.is_file() or name.endswith('.plist') or not target.is_file():
            continue
        shutil.copy2(tools/name, target)
        published_tools.append(name)
    for guest in ('mavericks','mountainlion','lion','snowleopard','leopard'):
        payload=source/'prebuilt/PassthroughOpenGL'/guest/'GuestTools'
        runpy.run_path(str(source/'scripts/package-opengl-guest-support.py'))['verify'](payload,source)
        support=resources/'Runtime/OpenGLGuests'/guest
        if support.exists():shutil.rmtree(support)
        shutil.copytree(payload,support,ignore=shutil.ignore_patterns('.DS_Store'))
        firmware=resources/'Runtime/Firmware'/guest
        if firmware.exists():shutil.rmtree(firmware)
        run(sys.executable,source/'scripts/build-opengl-firmware.py','--guest',guest,'--output',firmware)
    tiger=source/'prebuilt/PassthroughOpenGL/tiger'
    runpy.run_path(str(source/'scripts/package-tiger-guest-support.py'))['verify'](tiger,source)
    tiger_support=resources/'Runtime/OpenGLGuests/tiger'
    # Older reference apps used a top-level Tiger directory. This is a new
    # output app: do not retain a second, unreachable payload from that input.
    old_tiger=resources/'Runtime/TigerSupport'
    if old_tiger.exists():shutil.rmtree(old_tiger)
    if tiger_support.exists():shutil.rmtree(tiger_support)
    runpy.run_path(str(source/'scripts/package-tiger-guest-support.py'))['publish_runtime'](tiger,tiger_support,source)
    tiger_firmware=resources/'Runtime/Firmware/tiger'
    if tiger_firmware.exists():shutil.rmtree(tiger_firmware)
    run(sys.executable,source/'scripts/build-tiger-installer-firmware.py',
        '--firmware',resources/'Runtime/Firmware/leopard','--graphics-kernel',tiger/'Kernel',
        '--output',tiger_firmware)
    info_path = a.output/'Contents/Info.plist'
    info = plistlib.loads(info_path.read_bytes())
    if not a.use_ui_artwork:
        reference_info = plistlib.loads((a.reference_app/'Contents/Info.plist').read_bytes())
        icon = reference_info.get('CFBundleIconFile', 'AppIcon')
        if not isinstance(icon, str) or Path(icon).name != icon:
            p.error('The reference app has no safe compiled icon filename')
        if not icon.endswith('.icns'):
            icon += '.icns'
        for name in ('Assets.car', icon):
            shutil.copy2(a.reference_app/'Contents/Resources'/name, resources/name)
        for key in ('CFBundleIconFile', 'CFBundleIconName'):
            if key in reference_info:
                info[key] = reference_info[key]
    info_path.write_bytes(plistlib.dumps(info))
    binary = resources/'Runtime/bin/qemu-system-x86_64'
    binary.unlink()
    run('lipo', '-create', a.qemu_x86, a.qemu_arm, '-output', binary)
    binary.chmod(0o755)
    executor = binary.with_name('libVIMPassthroughOpenGLHost.dylib')
    shutil.copy2(a.executor, executor)
    for target in (binary, executor):
        command = ['codesign', '--force', '--sign', a.signing_identity]
        if target == binary:
            command += ['--entitlements', str(source/'vendor/qemu/accel/hvf/entitlements.plist')]
        run(*command, target)
    receipt = {
        'ResearchOnly': True, 'ProductionQualification': False,
        'SourceCommit': run('git', '-C', source, 'rev-parse', 'HEAD'),
        'WorkingTreeDiffSHA256': hashlib.sha256(subprocess.check_output(
            ['git', '-C', str(source), 'diff', 'HEAD'])).hexdigest(),
        'Inputs': {str(path.resolve()): digest(path) for path in (
            a.qemu_x86, a.qemu_arm, a.executor,
            a.ui_app/'Contents/MacOS/Virtual Intel Mac')},
        'ReferenceRuntime': str(runtime.resolve()),
        'ReferenceArtwork': str(a.reference_app.resolve()),
        'UsesCompiledUIAssets': a.use_ui_artwork,
        'CurrentGuestTools': published_tools,
    }
    (resources/'Runtime/ResearchRuntime.plist').write_bytes(plistlib.dumps(receipt))
    run('codesign', '--force', '--sign', a.signing_identity, a.output)
    run('codesign', '--verify', '--deep', '--strict', a.output)
    print(a.output)


if __name__ == '__main__':
    main()
