#!/usr/bin/env python3
"""Stage an experimental EFI kernel payload and OpenCore config fragment.

Does not change firmware or guests. Merge only after reviewing external
dependencies and excluding duplicate drivers in the actual boot configuration.
"""
import argparse
import plistlib
import tarfile
from pathlib import Path, PurePosixPath


def stage(archive, destination, release):
    targets = Path(__file__).resolve().parents[1] / 'support' / (
        'install-targets-mojave.txt' if release == 'mojave' else 'install-targets.txt')
    roots = ['payload' + p.strip() for p in targets.read_text().splitlines()
             if p.strip().endswith('.kext')]
    drivers = {}
    files = []
    with tarfile.open(archive, 'r:gz') as source:
        members = source.getmembers()
        names = [m.name.rstrip('/') for m in members]
        if len(names) != len(set(names)):
            raise ValueError('duplicate archive paths')
        for root in roots:
            infos = [m for m in members if m.name in
                     (root + '/Info.plist', root + '/Contents/Info.plist')]
            if len(infos) != 1 or not infos[0].isfile():
                raise ValueError('missing or ambiguous kext metadata: ' + root)
            info = plistlib.load(source.extractfile(infos[0]))
            identifier = info['CFBundleIdentifier']
            if identifier in drivers:
                raise ValueError('duplicate bundle identifier: ' + identifier)
            layout = 'Contents/' if '/Contents/' in infos[0].name else ''
            executable = info.get('CFBundleExecutable', '')
            binary = layout + ('MacOS/' if layout else '') + executable if executable else ''
            if binary and root + '/' + binary not in names:
                raise ValueError('missing executable: ' + root)
            drivers[identifier] = dict(root=root, name=PurePosixPath(root).name,
                plist=layout + 'Info.plist', binary=binary,
                dependencies=info.get('OSBundleLibraries', {}))
            for member in members:
                if not member.name.startswith(root + '/'):
                    continue
                relative = PurePosixPath(member.name).relative_to(root)
                if '..' in relative.parts or not (member.isdir() or member.isfile()):
                    raise ValueError('unsupported kernel payload entry: ' + member.name)
                files.append((PurePosixPath(root).name, relative, member))
        ordered = []
        pending = dict(drivers)
        while pending:
            ready = sorted(k for k, v in pending.items()
                           if not set(v['dependencies']).intersection(pending))
            if not ready:
                raise ValueError('cyclic injected kernel dependencies')
            ordered.extend(ready)
            for key in ready:
                del pending[key]
        kernel = '18' if release == 'mojave' else '19'
        bounds = dict(Arch='x86_64', MinKernel=kernel+'.0.0', MaxKernel=kernel+'.99.99')
        adds = [dict(bounds, BundlePath=drivers[k]['name'], Enabled=True,
                     ExecutablePath=drivers[k]['binary'], PlistPath=drivers[k]['plist'],
                     Comment='Virtual Intel Mac: '+k) for k in ordered]
        # Exclude every injected identifier: both stock Apple replacements and
        # previously installed copies must not compete with the EFI payload.
        blocks = [dict(bounds, Identifier=k, Enabled=True, Strategy='Exclude',
                       Comment='Replaced by matching EFI payload') for k in ordered]
        report = dict(Release=release, RuntimeValidated=False, InjectionOrder=ordered,
                      ExternalDependencies={k: sorted(set(drivers[k]['dependencies'])-drivers.keys())
                                            for k in ordered})
        destination.mkdir(parents=True, exist_ok=False)
        for bundle, relative, member in files:
            output = destination / 'Kexts' / bundle / str(relative)
            if member.isdir():
                output.mkdir(parents=True, exist_ok=True)
            else:
                output.parent.mkdir(parents=True, exist_ok=True)
                with output.open('xb') as stream:
                    stream.write(source.extractfile(member).read())
                output.chmod(0o755 if member.mode & 0o111 else 0o644)
        for name, value in [('KernelFragment.plist', dict(Add=adds, Block=blocks)),
                            ('Report.plist', report)]:
            with (destination / name).open('xb') as stream:
                plistlib.dump(value, stream)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', choices=['mojave', 'catalina'], required=True)
    parser.add_argument('archive', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    result = stage(args.archive, args.destination, args.release)
    print('\n'.join(result['InjectionOrder']))
    print('Staged only; firmware unchanged; runtime validation required.')
