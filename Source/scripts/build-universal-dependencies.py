#!/usr/bin/env python3
"""Build static QEMU dependencies for both macOS architectures (minimum 11).

Sources are supplied explicitly; no Homebrew architecture assumptions. Each
architecture has an independent prefix, and host tools are never cross-run.
"""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import hashlib
import json
import shutil
import tarfile
import plistlib
import sys

# Invoking a venv's Python by absolute path does not activate its tool PATH.
os.environ['PATH'] = str(Path(sys.executable).parent) + os.pathsep + os.environ.get('PATH', '')
sys.path.insert(0, str(Path(__file__).resolve().parent))
import deployment_target
from usb_dependency_state import usb_inputs_hash, apply_libusb_patches
import build_freshness as freshness

# Ninja records the Meson launcher during setup. Pin one absolute executable
# for setup and install, whether Meson comes from this venv or Homebrew.
meson_executable = shutil.which('meson')
if not meson_executable:
    raise SystemExit('Meson is required; install it in the selected build environment.')
meson = [meson_executable]
meson_version = subprocess.check_output([*meson, '--version'], text=True).strip()


def meson_needs_reconfigure(build):
    try:
        info = json.loads((build/'meson-info/meson-info.json').read_text())
        return info['meson_version']['full'] != meson_version
    except (OSError, ValueError, KeyError, TypeError):
        return True

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sources', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--pkg-config', type=Path)
p.add_argument('--jobs', type=int, default=4)
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parents[1]
build_inputs = freshness.snapshot(source, 'dependencies')
(a.output/'BuildReceipt.plist').unlink(missing_ok=True)
source = Path(__file__).resolve().parents[1]
if not a.sources:
    archive = source/'vendor/qemu-dependency-sources.tar.gz'
    expected = (source/'vendor/qemu-dependency-sources.sha256').read_text().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        p.error('Dependency source archive checksum mismatch')
    a.sources = a.output/'Sources'
    stamp = a.sources/'.archive-sha256'
    if a.sources.exists() and (not stamp.is_file() or stamp.read_text() != expected):
        shutil.rmtree(a.sources)  # Extracted from a different archive revision.
    if not a.sources.exists():
        a.sources.mkdir()
        with tarfile.open(archive) as stream:
            stream.extractall(a.sources, filter='data')
        stamp.write_text(expected)
a.sources = a.sources.resolve()
apply_libusb_patches(source, a.sources)
if not a.pkg_config:
    found = shutil.which('pkgconf') or shutil.which('pkg-config')
    if found:
        a.pkg_config = Path(found)
    else:
        # Older dependency caches configured pkgconf in its extracted source
        # directory. Reconfigure that derived copy in place instead of failing
        # an out-of-tree build; the vendored archive remains untouched.
        build = a.sources/'pkgconf' if (a.sources/'pkgconf/config.status').is_file() else a.output/'pkgconf-host'
        prefix = a.output/'Tools'
        build.mkdir(exist_ok=True)
        with (a.output/'pkgconf.log').open('w') as log:
            for command in ([str(a.sources/'pkgconf/configure'), '--prefix='+str(prefix)],
                            ['make', '-j', str(a.jobs)], ['make', 'install']):
                subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        a.pkg_config = prefix/'bin/pkgconf'
if not shutil.which('meson') or not shutil.which('ninja'):
    p.error('Install standard build tools in this Python environment: pip install meson ninja')
sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
for arch in ('x86_64', 'arm64'):
    prefix = a.output / arch
    prefix.mkdir(exist_ok=True)
    machine = a.output / (arch + '.ini')
    flags = ['-arch', arch, '-mmacosx-version-min=11.0', '-isysroot', sdk]
    machine_text = ("[binaries]\nc = ['clang']\ncpp = ['clang++']\n"
        "objc = ['clang']\nar = 'ar'\nstrip = 'strip'\npkg-config = " + repr(str(a.pkg_config)) + "\n"
        "[host_machine]\nsystem = 'darwin'\nsubsystem = 'macos'\nkernel = 'xnu'\ncpu_family = " + repr('aarch64' if arch == 'arm64' else arch) +
        "\ncpu = " + repr(arch) + "\nendian = 'little'\n"
        "[properties]\nneeds_exe_wrapper = true\n"
        "[built-in options]\nc_args = " + repr(flags) + "\ncpp_args = " + repr(flags) +
        "\nobjc_args = " + repr(flags) + "\nobjc_link_args = " + repr(flags) +
        "\nc_link_args = " + repr(flags) + "\ncpp_link_args = " + repr(flags) +
        "\npkg_config_path = " + repr([str(prefix/'lib/pkgconfig')]) + "\n")
    # Meson treats the cross-file mtime as a configure dependency. Rewriting
    # identical bytes made every USB-only rebuild regenerate unrelated GLib
    # configuration and rebuild hundreds of objects in both architectures.
    if not machine.is_file() or machine.read_text() != machine_text:
        machine.write_text(machine_text)
    env = dict(os.environ, PKG_CONFIG=str(a.pkg_config),
               PKG_CONFIG_LIBDIR=str(prefix/'lib/pkgconfig'), PKG_CONFIG_PATH='',
               MACOSX_DEPLOYMENT_TARGET='11.0')
    with (a.output / (arch + '.log')).open('a') as log:
        def run(cmd, cwd=None):
            print(arch, ' '.join(map(str, cmd)), flush=True)
            subprocess.run(list(map(str, cmd)), cwd=cwd, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        for name, options in (
            ('glib', ['--force-fallback-for=libffi,libpcre2-8,intl', '-Dtests=false',
                      '-Dintrospection=disabled', '-Ddocumentation=false',
                      '-Dman-pages=disabled', '-Dsysprof=disabled', '-Dlibmount=disabled']),
            ('pixman', ['-Dtests=disabled', '-Ddemos=disabled']),
            ('slirp', [])):
            build = a.output / (arch + '-' + name)
            if not (build/'build.ninja').exists() or not (build/'target-11.0').exists():
                reset = ['--wipe'] if (build/'build.ninja').exists() else []
                run([*meson, 'setup', *reset, build, a.sources/name, '--cross-file', machine,
                     '--prefix', prefix, '--libdir=lib', '--default-library=static',
                     '--buildtype=release', *options])
                (build/'target-11.0').touch()
            elif meson_needs_reconfigure(build):
                run([*meson, 'setup', '--reconfigure', build, a.sources/name,
                     '--cross-file', machine, '--prefix', prefix, '--libdir=lib',
                     '--default-library=static', '--buildtype=release', *options])
            # If inputs changed, regenerate before adjusting availability;
            # a later regeneration would overwrite those config.h fallbacks.
            run(['ninja', '-C', build, 'build.ninja'])
            if (build/'config.h').is_file():
                for name in deployment_target.undefine_unavailable(build/'config.h', 'HAVE_', arch):
                    print(arch, name, 'is newer than macOS 11.0; using the fallback', flush=True)
            run(['ninja', '-C', build, '-j', a.jobs])
            if (build/'config.h').is_file() and deployment_target.undefine_unavailable(build/'config.h', 'HAVE_', arch):
                raise SystemExit(f'{build}: configuration was regenerated during the build; rerun')
            run([*meson, 'install', '-C', build, '--no-rebuild'])
        build = a.output/(arch + '-sdl')
        build.mkdir(exist_ok=True)
        env.update(CC='clang', CXX='clang++', CFLAGS=' '.join(flags),
                   CXXFLAGS=' '.join(flags), LDFLAGS=' '.join(flags))
        if not (build/'Makefile').exists() or not (build/'target-11.0').exists():
            if (build/'Makefile').exists():
                run(['make', 'clean'], build)
            run([a.sources/'sdl/configure', '--prefix='+str(prefix),
                 '--host='+('aarch64' if arch == 'arm64' else arch)+'-apple-darwin',
                 '--build='+platform.machine()+'-apple-darwin',
                 '--disable-shared', '--enable-static', '--disable-video-x11',
                 '--disable-video-vulkan', '--disable-video-opengl'], build)
            (build/'target-11.0').touch()
        sdl_config = build/'include/SDL_config.h'
        for name in (deployment_target.undefine_unavailable(sdl_config, 'HAVE_', arch) if sdl_config.is_file() else []):
            print(arch, name, 'is newer than macOS 11.0; using the fallback', flush=True)
        run(['make', '-j', a.jobs], build)
        run(['make', 'install'], build)
        # USB passthrough (QEMU usb-host) uses libusb's IOKit backend.
        build = a.output/(arch + '-libusb')
        build.mkdir(exist_ok=True)
        if not (build/'Makefile').exists() or not (build/'target-11.0').exists():
            if (build/'Makefile').exists():
                run(['make', 'clean'], build)
            run([a.sources/'libusb/configure', '--prefix='+str(prefix),
                 '--host='+('aarch64' if arch == 'arm64' else arch)+'-apple-darwin',
                 '--build='+platform.machine()+'-apple-darwin',
                 '--disable-shared', '--enable-static', '--disable-examples-build',
                 '--disable-tests-build'], build)
            (build/'target-11.0').touch()
        for name in deployment_target.undefine_unavailable(build/'config.h', 'HAVE_', arch):
            print(arch, name, 'is newer than macOS 11.0; using the fallback', flush=True)
        run(['make', '-j', a.jobs], build)
        run(['make', 'install'], build)
        # USB-only privileged capture can use QEMU's existing usb-redir device
        # over a local socket. The protocol/host sources are vendored directly,
        # not another checkout, and the VM process stays unprivileged.
        build = a.output/(arch + '-usbredir')
        if not (build/'build.ninja').exists() or meson_needs_reconfigure(build):
            reset=['--reconfigure'] if (build/'build.ninja').exists() else []
            run([*meson, 'setup', *reset, build, source/'vendor/usbredir',
                 '--cross-file', machine, '--prefix', prefix, '--libdir=lib',
                 '--default-library=static', '--buildtype=release',
                 '-Dtools=disabled', '-Dtests=disabled'])
        run(['ninja', '-C', build, '-j', a.jobs])
        run([*meson, 'install', '-C', build, '--no-rebuild'])
(a.output/'BuildReceipt.plist').write_bytes(plistlib.dumps({
    'Freshness': freshness.seal(build_inputs, freshness.snapshot(source, 'dependencies'), a.output,
        [p for arch in ('x86_64', 'arm64') for folder in ('lib', 'include')
         for p in (a.output/arch/folder).rglob('*') if p.is_file()]),
    'MinimumHostVersion':'11.0', 'DeploymentTargetChecked':True, 'LibUSB':True, 'USBRedirection':True, 'Architectures':['x86_64','arm64'],
    'SDK':sdk, 'USBInputsSHA256':usb_inputs_hash(source),
    'SourceArchiveSHA256':(source/'vendor/qemu-dependency-sources.sha256').read_text().split()[0]}))
print('Dependency prefixes ready:', a.output)
