#!/usr/bin/env python3
"""Build universal QEMU/qemu-img and the Intel Rosetta helper, without booting.

Uses per-architecture static dependencies from build-universal-dependencies.py.
Both QEMU slices come from the same clean checkout; no TSO-entitled variants.
"""
import argparse
from pathlib import Path
import os
import plistlib
import platform
import subprocess
import sys
import shutil

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deployment_target
from usb_dependency_state import usb_inputs_hash
import build_freshness as freshness

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--qemu', type=Path, default=Path(__file__).resolve().parents[1]/'vendor/qemu')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--jobs', type=int, default=4)
p.add_argument('--resume', action='store_true', help='Resume an interrupted build in its existing output/build directories')
p.add_argument('--build-directory', type=Path)
p.add_argument('--dependencies', type=Path)
p.add_argument('--pkg-config', default=os.environ.get('PKG_CONFIG', 'pkg-config'))
a = p.parse_args()
source = Path(__file__).resolve().parents[1]
from build_signing import resolve_identity
signing_identity = resolve_identity(source)
# Keep all build-tool discovery in the selected Python environment, not just
# configure's interpreter. In particular Finder/Xcode omit a venv's Ninja.
os.environ['PATH']=str(Path(sys.executable).parent)+os.pathsep+os.environ.get('PATH','')
if sys.platform != 'darwin' or (a.output.exists() and not a.resume) or not 1 <= a.jobs <= 64:
    p.error('requires macOS, a new output directory (or --resume), and 1–64 jobs')
a.qemu = a.qemu.resolve()
a.output = a.output.resolve()
runtime_inputs = freshness.snapshot(source, 'runtime', a.qemu)
deps = freshness.ensure_dependencies(source, a.dependencies or source/'Build/UniversalDependencies', a.jobs)
if not shutil.which(a.pkg_config):
    a.pkg_config = str(deps/'Tools/bin/pkgconf')
if not (a.qemu/'configure').is_file():
    p.error(f'QEMU source not found at {a.qemu}; pass --qemu with a QEMU source folder')
# Identify QEMU by the committed tree of its source folder (Source/vendor/qemu
# or a separate checkout), and require that folder to have no uncommitted edits.
def qemu_state():
    revision = subprocess.check_output(['git', '-C', str(a.qemu), 'rev-parse', 'HEAD:./'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(a.qemu), 'status', '--porcelain', '--untracked-files=no', '--', '.']).strip()
    return revision, dirty
revision, dirty = qemu_state()
if dirty:
    p.error('Commit QEMU source changes before a reproducible runtime build')
a.output.mkdir(parents=True, exist_ok=a.resume)
build_root = (a.build_directory or a.output/'Build').resolve()
configurations = {}
for arch in ('x86_64', 'arm64'):
    build = build_root/arch
    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PKG_CONFIG=a.pkg_config,
               PKG_CONFIG_LIBDIR=str(deps/arch/'lib/pkgconfig'), PKG_CONFIG_PATH='',
               MACOSX_DEPLOYMENT_TARGET='11.0')
    # An explicitly chosen Python/venv also owns Meson and Ninja. Calling its
    # interpreter by absolute path does not activate that environment's PATH.
    env['PATH']=str(Path(sys.executable).parent)+os.pathsep+env.get('PATH','')
    flags = f'-arch {arch} -mmacosx-version-min=11.0'
    command = [str(a.qemu/'configure'), '--target-list=x86_64-softmmu',
        '--without-default-features', '--enable-tcg',
        '--enable-hvf' if arch == 'x86_64' else '--disable-hvf',
        '--enable-pvg', '--enable-pixman', '--enable-slirp', '--enable-sdl', '--enable-libusb', '--enable-usb-redir',
        '--enable-dmg', '--enable-bzip2', '--enable-lzfse',
        # Only used through our existing QMP owner for fixed read-only
        # developer reports; the app still launches with -monitor none.
        '--enable-tools', '--enable-hmp', '--disable-docs', '--disable-guest-agent',
        '--disable-werror', '--disable-rust', '--disable-plugins', '--static',
        '--python='+sys.executable, '--cc=clang '+flags, '--cxx=clang++ '+flags,
        '--objcc=clang '+flags, '--extra-cflags='+flags,
        '--extra-objcflags='+flags, '--extra-ldflags='+flags]
    if arch != platform.machine():
        # QEMU requires an explicit cross-build even with clang -arch; otherwise
        # Meson attempts to execute ARM configure probes on an Intel builder.
        command += ['--cross-prefix=', '--cpu='+('aarch64' if arch == 'arm64' else arch)]
    configurations[arch] = command[1:]
    log_path = a.output/(arch+'.log')
    print(f'Building {arch}: {log_path}', flush=True)
    with log_path.open('w') as log:
        subprocess.run(command, cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        # Regenerate now if needed, so a later regeneration cannot rewrite config-host.h.
        subprocess.run(['ninja', 'build.ninja'], cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        for name in deployment_target.undefine_unavailable(build/'config-host.h', 'CONFIG_', arch):
            print(f'{arch}: {name} is newer than macOS 11.0; using the fallback', file=log, flush=True)
        subprocess.run(['ninja', '-j', str(a.jobs), 'qemu-system-x86_64', 'qemu-img'],
                       cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
if qemu_state() != (revision, b''):
    p.error('QEMU source changed during the build; rerun with the same build directory before packaging')
for name in ('qemu-system-x86_64', 'qemu-img'):
    subprocess.run(['lipo', '-create', str(build_root/'x86_64'/name), str(build_root/'arm64'/name),
                    '-output', str(a.output/name)], check=True)
    for arch in ('x86_64', 'arm64'):  # current lipo verifies one architecture per call
        subprocess.run(['lipo', str(a.output/name), '-verify_arch', arch], check=True)
    (a.output/name).chmod(0o755)
    deployment_target.check_weak_libsystem_imports(a.output/name)
    sign = ['codesign', '--force', '--sign', signing_identity]
    if name == 'qemu-system-x86_64':
        sign += ['--entitlements', str(a.qemu/'accel/hvf/entitlements.plist')]
    subprocess.run(sign+[str(a.output/name)], check=True)
env = dict(os.environ, PVG_QEMU_SOURCE=str(a.qemu), PVG_OOP_HELPER_OUTPUT=str(a.output/'apple-gfx-helper'))
subprocess.run(['zsh', str(source/'scripts/build-pvg-oop-helper')], env=env, check=True)
subprocess.run(['codesign', '--force', '--sign', signing_identity, str(a.output/'apple-gfx-helper')], check=True)
deployment_target.check_weak_libsystem_imports(a.output/'apple-gfx-helper')
(a.output/'BuildReceipt.plist').write_bytes(plistlib.dumps({
    'Freshness': freshness.seal(runtime_inputs, freshness.snapshot(source, 'runtime', a.qemu),
                                a.output, [a.output/name for name in
                                ('qemu-system-x86_64', 'qemu-img', 'apple-gfx-helper')]),
    'QEMURevision':revision, 'HostArchitectures':['x86_64','arm64'],
    'MinimumHostVersion':'11.0', 'ConfigureArguments':configurations,
    'BuildDirectory':str(build_root), 'DependencyPrefixes':str(deps),
    'RuntimeValidated':False, 'HardwareTSO':False, 'AppDisplayMaxDimension':8192,
    'AppleANSIKeyboard':True, 'AudioDeviceSelection':True, 'DisplayScalePolicy':True, 'DisplayPowerEvents':True, 'GuestDisplayPowerEvents':True,
    'DirectDMG':True, 'DeploymentTargetChecked':True, 'USBPassthrough':True,
    'DiagnosticRegisters':True, 'USBRedirection':True}))
print('Universal runtime:', a.output)
