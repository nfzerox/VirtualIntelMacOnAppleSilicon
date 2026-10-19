"""Content-based build receipts, independent of checkout path and timestamps.

Receipts are cache invalidation, not a security boundary. Missing/legacy receipts
are stale. Hash the actual working-tree inputs and final outputs, not just HEAD.
"""
import functools
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import subprocess
import sys

SCHEMA = 1
DEPENDENCY_RECIPES = ('build-universal-dependencies.py', 'usb_dependency_state.py',
                      'deployment_target.py', 'build_freshness.py')
RUNTIME_RECIPES = ('build-app-runtime.py', 'build-pvg-oop-helper', 'build_signing.py')
FIRMWARE_RECIPES = ('build-guest-firmware.py', 'firmware_source.py', 'build_freshness.py')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def files(root):
    """Tracked + nonignored untracked inputs, including additions/deletions.

    Git enumerates paths, but bytes come from disk. Build products ignored by
    the source project are never mistaken for new source files.
    """
    root = Path(root)
    if not root.is_dir():
        return [root]
    result = subprocess.run(['git', '-C', str(root), 'ls-files', '-cz', '--others',
                             '--exclude-standard', '--', '.'], capture_output=True, check=True)
    return sorted({root / os.fsdecode(p) for p in result.stdout.split(b'\0') if p})


def manifest(groups):
    result = {}
    for label, root in groups:
        root = Path(root)
        for path in files(root):
            name = label if path == root else label + '/' + path.relative_to(root).as_posix()
            if path.is_symlink():
                result[name] = {'Link': os.readlink(path)}
            elif path.is_file():
                result[name] = {'SHA256': digest(path), 'Executable': bool(path.stat().st_mode & 0o111)}
            else:
                result[name] = {'Missing': True}
    return result


@functools.lru_cache(maxsize=None)
def command_version(*command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()


def toolchain(component):
    result = {'Xcode': command_version('xcodebuild', '-version'),
              'SDK': command_version('xcrun', '--show-sdk-version'),
              'SDKBuild': command_version('xcrun', '--show-sdk-build-version'),
              'Clang': '\n'.join(line for line in command_version('xcrun', 'clang', '--version').splitlines()
                                 if not line.startswith('InstalledDir:')),
              'Python': platform.python_version(), 'BuilderArchitecture': platform.machine()}
    if component != 'firmware':
        for name in ('meson', 'ninja', 'make', 'pkg-config'):
            try:
                result[name] = command_version(name, '--version')
            except (OSError, subprocess.CalledProcessError):
                result[name] = 'unavailable'
    # These influence compiler/tool discovery and configure probes. Do not
    # serialize environment values (which can contain private builder paths).
    keys = ('CFLAGS', 'CXXFLAGS', 'OBJCFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CC', 'CXX',
            'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET', 'PKG_CONFIG',
            'PVG_OOP_SHADOW_SOURCE', 'PVG_DEVELOPER_DIR')
    result['BuildEnvironmentSHA256'] = identity({key: os.environ.get(key, '') for key in keys})
    return result


def snapshot(source, component, qemu=None):
    source = Path(source)
    if component in ('dependencies', 'runtime'):
        paths = ['scripts/' + p for p in DEPENDENCY_RECIPES]
        paths += ['vendor/qemu-dependency-sources.tar.gz', 'vendor/qemu-dependency-sources.sha256',
                  'vendor/usbredir', 'vendor/patches']
        if component == 'runtime':
            paths += ['scripts/' + p for p in RUNTIME_RECIPES]
            paths += ['compat/PVGHostAIRRepair.m', 'compat/PVGAIRMetadata.h']
        groups = [(p, source / p) for p in paths]
        if component == 'runtime':
            groups.append(('qemu', Path(qemu or source/'vendor/qemu')))
            if os.environ.get('PVG_OOP_SHADOW_SOURCE'):
                groups.append(('HelperShadowOverride', Path(os.environ['PVG_OOP_SHADOW_SOURCE'])))
    elif component == 'firmware':
        paths = ['scripts/' + p for p in FIRMWARE_RECIPES]
        paths += ['vendor/edk2-source.tar.gz', 'vendor/edk2-source.sha256']
        groups = [(p, source / p) for p in paths]
    else:
        raise ValueError('Unknown cached build component: ' + component)
    inputs = manifest(groups)
    tools = toolchain(component)
    return {'Schema': SCHEMA, 'Component': component,
            'InputsSHA256': identity(inputs), 'Toolchain': tools,
            'Fingerprint': identity({'Schema': SCHEMA, 'Component': component,
                                     'Inputs': inputs, 'Toolchain': tools})}


def output_hashes(root, paths):
    root = Path(root)
    return {str(p.relative_to(root)): {'SHA256':digest(p), 'Executable':bool(p.stat().st_mode & 0o111)}
            for p in sorted(set(map(Path, paths))) if p.is_file()}


def seal(before, after, root, paths):
    if before != after:
        raise RuntimeError('Build inputs changed during ' + before['Component'] + ' compilation; rerun')
    return {**before, 'Outputs': output_hashes(root, paths)}


def current(receipt, expected, root, required=()):
    if not isinstance(receipt, dict):
        return False
    state = receipt.get('Freshness', {})
    if (not isinstance(state, dict) or state.get('Fingerprint') != expected['Fingerprint']
            or state.get('Schema') != SCHEMA):
        return False
    outputs = state.get('Outputs', {})
    if not isinstance(outputs, dict) or not outputs or not set(required).issubset(outputs):
        return False
    try:
        for name, checksum in outputs.items():
            relative = Path(name)
            path = Path(root)/relative
            if relative.is_absolute() or '..' in relative.parts:
                return False
            if {'SHA256':digest(path), 'Executable':bool(path.stat().st_mode & 0o111)} != checksum:
                return False
    except OSError:
        return False
    return True


def load(path):
    try:
        value = plistlib.loads(Path(path).read_bytes())
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError, plistlib.InvalidFileException):
        return {}


def ensure_dependencies(source, directory, jobs=4):
    """Never reconfigure unknown/stale static objects into a fresh receipt."""
    source, directory = Path(source), Path(directory).resolve()
    expected = snapshot(source, 'dependencies')
    required = tuple(arch + '/lib/' + name for arch in ('arm64', 'x86_64')
                     for name in ('libSDL2.a', 'libusb-1.0.a', 'libusbredirhost.a', 'libusbredirparser.a'))
    if current(load(directory/'BuildReceipt.plist'), expected, directory, required):
        return directory
    if directory.exists():
        directory = directory.with_name(directory.name + '-' + expected['Fingerprint'][:16])
    if not current(load(directory/'BuildReceipt.plist'), expected, directory, required):
        # Interrupted derived output is retained, never erased. A new directory
        # also prevents old configure results surviving a toolchain change.
        if directory.exists():
            import tempfile
            directory = Path(tempfile.mkdtemp(prefix=directory.name+'-', dir=directory.parent))
        subprocess.run([sys.executable, str(source/'scripts/build-universal-dependencies.py'),
                        '--output', str(directory), '--jobs', str(jobs)], check=True)
    if not current(load(directory/'BuildReceipt.plist'), expected, directory, required):
        raise RuntimeError('Dependency build did not produce a current, intact receipt')
    return directory
