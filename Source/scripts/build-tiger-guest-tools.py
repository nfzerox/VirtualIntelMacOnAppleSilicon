#!/usr/bin/env python3
"""Build Tiger's native installed/installer controls, not diagnostic probes.

Compile with the real 10.4u SDK and explicitly selected period linker. No
borrowed later libraries, guest developer tools, VM mutations or network.
Ordinary app packaging consumes the published native payload.
"""
import argparse
from pathlib import Path
import plistlib
import runpy
import shlex
import shutil
import subprocess


UNITS = {
    'opengl-graphics-check': ('tools/opengl-tiger-graphics-check.c',),
    'opengl-guest-agent': ('tools/opengl-tiger-guest-agent.m',),
    'opengl-desktop-status': ('tools/opengl-desktop-status.m',),
    'opengl-setup-status': ('tools/opengl-setup-status.m',),
    'opengl-create-account': ('tools/opengl-create-account.m', 'tools/opengl-leopard-directory.m'),
    'opengl-tiger-install-config': ('tools/opengl-tiger-install-config.m',),
    'opengl-tiger-installer-bootstrap': ('tools/opengl-tiger-installer-bootstrap.c',),
    'opengl-audio-defaults': ('tools/opengl-audio-defaults.c',),
    'opengl-tiger-audio': ('tools/opengl-tiger-audio.m',),
    'opengl-tiger-console': ('tools/opengl-tiger-console.m',),
    'opengl-file-sharing': ('tools/opengl-file-sharing.m',),
    'guest-display': ('tools/guest-display.m',),
    'guest-clipboard': ('tools/guest-clipboard.m',),
    'guest-host-connection': ('tools/guest-host-connection.m',),
}


def verify(directory, source):
    record = plistlib.loads((directory/'BuildReceipt.plist').read_bytes())
    actual = {p.relative_to(directory).as_posix(): True for p in directory.iterdir()
              if p.is_file() and p.name != 'BuildReceipt.plist'}
    if (record.get('FormatVersion') != 1 or record.get('Architecture') != 'i386' or
            record.get('MinimumOS') != '10.4' or not set(UNITS).issubset(actual)):
        raise ValueError('Tiger control payload is incomplete')
    runpy.run_path(str(source/'scripts/build-tiger-samba.py'))['verify'](directory,source)
    runpy.run_path(str(source/'scripts/build-tiger-smb-client.py'))['verify'](directory,source)
    return record


def build(sdk, cc, linker, output, ssh=None):
    source = Path(__file__).resolve().parents[1]
    if plistlib.loads((sdk/'SDKSettings.plist').read_bytes()).get('Version') != '10.4':
        raise ValueError('Supply the genuine MacOSX10.4u SDK')
    output.mkdir(parents=True, exist_ok=False)
    objects = output/'Objects'; objects.mkdir()
    inputs = {Path(__file__).resolve()}
    flags = ['-arch', 'i386', '-mmacosx-version-min=10.4', '-isysroot', sdk,
             '-O2', '-Wall', '-Wextra', '-Werror', '-fno-builtin-memchr',
             '-Wno-deprecated-objc-isa-usage', '-Wno-deprecated-objc-pointer-introspection']
    links = [linker, '-arch', 'i386', '-macosx_version_min', '10.4',
             '-syslibroot', sdk, '-no_uuid', '-lcrt1.o']

    def run(command):
        print(shlex.join(map(str, command)), flush=True)
        subprocess.run(list(map(str, command)), check=True)

    def compile(relative):
        unit = source/relative
        obj = objects/(unit.stem+'.o'); dep = obj.with_suffix('.d')
        if obj.exists():
            return obj
        objc = ['-fno-objc-arc', '-include', source/'tools/opengl-period-foundation.h'] if unit.suffix == '.m' else []
        defines = ['-DVIM_OPENGL_GUEST=1'] if unit.name == 'guest-clipboard.m' else []
        run([cc, *flags, *objc, *defines, '-MMD', '-MF', dep, '-c', unit, '-o', obj])
        # Record the compiler's actual transitive project dependencies, not
        # a hand-maintained list that silently misses a new compatibility file.
        text = dep.read_text().replace('\\\n', '')
        for name in shlex.split(text.split(':', 1)[1]):
            path = Path(name).resolve()
            if path.is_relative_to(source):
                inputs.add(path)
        inputs.add(unit)
        return obj

    support = [compile(name) for name in ('tools/opengl-period-foundation.m',
                                         'vendor/source/JSONKit/JSONKit.m')]
    for name, units in UNITS.items():
        compiled = [compile(unit) for unit in units]
        objc = any(unit.endswith('.m') for unit in units)
        frameworks = ('Foundation', 'SystemConfiguration', 'DiskArbitration',
                      'DirectoryService', 'IOKit', 'CoreFoundation') if objc else ('IOKit', 'CoreFoundation')
        if name == 'opengl-graphics-check':
            frameworks += ('OpenGL', 'ApplicationServices')
        if name == 'opengl-audio-defaults':
            frameworks += ('CoreAudio',)
        if name == 'guest-display':
            frameworks += ('ApplicationServices',)
        if name == 'guest-clipboard':
            frameworks += ('AppKit',)
        run([*links, *compiled, *(support if objc else []),
             *(word for framework in frameworks for word in ('-framework', framework)),
             '-lgcc_s.10.4', '-lSystem', '-o', output/name])
        archs = subprocess.check_output(['lipo', '-archs', str(output/name)], text=True).split()
        if archs != ['i386']:
            raise ValueError('Tiger controls must be genuine native i386: '+name)
    record = {'FormatVersion': 1, 'Architecture': 'i386', 'MinimumOS': '10.4',
              'RequiresGuestDeveloperTools': False,
              'Files': {name: True for name in UNITS}}
    # Derived .o/.d files are not part of the distributable control directory.
    payload = output/'GuestTools'; payload.mkdir()
    for name in UNITS:
        (output/name).rename(payload/name)
    smb=source/'prebuilt/TigerSMB'
    samba=runpy.run_path(str(source/'scripts/build-tiger-samba.py'))
    samba['verify'](smb,source)
    for name in (*samba['FILES'],'TigerSMB.plist'):
        shutil.copyfile(smb/name,payload/name)
        (payload/name).chmod(0o644 if name.endswith(('.txt','.plist')) else 0o755)
        record['Files'][name]=True
    client=runpy.run_path(str(source/'scripts/build-tiger-smb-client.py'))
    smb_client=source/'prebuilt/TigerSMBClient'
    client['verify'](smb_client,source)
    for name in (*client['FILES'],'TigerSMBClient.plist'):
        shutil.copyfile(smb_client/name,payload/name)
        # Native kld object, not a modern signed executable.
        (payload/name).chmod(0o644)
        record['Files'][name]=True
    if ssh:
        if subprocess.check_output(['lipo','-archs',str(ssh)],text=True).split()!=['i386']:
            raise ValueError('Tiger SSH must be the native i386 client')
        shutil.copyfile(ssh,payload/'guest-ssh');(payload/'guest-ssh').chmod(0o755)
        record['Files']['guest-ssh']=True
    (payload/'BuildReceipt.plist').write_bytes(plistlib.dumps(record))
    verify(payload, source)
    return payload


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'cc', 'ld', 'output', 'verify', 'ssh'):
        p.add_argument('--'+name, type=Path)
    a = p.parse_args()
    if a.verify:
        verify(a.verify, Path(__file__).resolve().parents[1]); print('Tiger controls verified'); return
    if not all((a.sdk, a.cc, a.ld, a.output)):
        p.error('Supply --sdk, --cc, --ld and a new --output')
    print(build(*(p.resolve() for p in (a.sdk, a.cc, a.ld, a.output)),ssh=a.ssh.resolve() if a.ssh else None))


if __name__ == '__main__':
    main()
