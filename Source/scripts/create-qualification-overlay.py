#!/usr/bin/env python3
"""Create a disposable test VM without copying or changing its installed disk.

The source VM must remain stopped and unchanged while this overlay is in use.
Boot the result through the real app with --development-defer-guest-setup.
Only immutable backing reads are shared; NVRAM and all test writes are private.
"""
import argparse
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import uuid


def member(bundle, value):
    relative = Path(value)
    if relative.is_absolute():
        raise ValueError('Expected a bundle-relative disk or NVRAM path')
    result = (bundle / relative).resolve()
    result.relative_to(bundle)
    if not result.is_file():
        raise ValueError('The source disk or NVRAM is missing')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--app', required=True, type=Path,
                        help='The real app whose bundled qemu-img will create the overlay')
    parser.add_argument('--ssh-port', required=True, type=int)
    parser.add_argument('--smb-port', required=True, type=int)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.absolute()
    if output.exists() or output.suffix != '.vintelmac':
        parser.error('Choose a new .vintelmac bundle under the scratch disk')
    if not output.parent.is_dir():
        parser.error('Create the scratch parent directory first')
    if any(not 1 <= port <= 65535 for port in (args.ssh_port, args.smb_port)):
        parser.error('Choose valid, unused local forwarding ports')
    settings = plistlib.loads((source / 'Machine.plist').read_bytes())
    if settings.get('InstallState') != 'installed':
        parser.error('Use an already installed VM as the source')
    disk, nvram = (member(source, settings[key]) for key in ('Disk', 'NVRAM'))
    image_tool = args.app.resolve() / 'Contents/Resources/Runtime/bin/qemu-img'
    # Do not force-share a disk that another QEMU process has open for writing.
    info = json.loads(subprocess.check_output([image_tool, 'info', '--output=json', disk]))
    settings.update(Identifier=str(uuid.uuid4()).upper(), Disk='Disks/System.qcow2',
                    NVRAM='Firmware/NVRAM.fd', FirmwareSource='application',
                    Firmware='Firmware/CODE.fd', BootMedia='Firmware/EFI.img',
                    BootMediaKind='efi-disk', OpticalMedia='', AdditionalMedia=[],
                    SSHEnabled=True, SSHPort=args.ssh_port,
                    FileSharingEnabled=True, FileSharingPort=args.smb_port)
    settings.pop('Name', None)
    output.mkdir(mode=0o700)
    for name in ('Disks', 'Firmware', 'State', 'Logs', 'Installation'):
        (output / name).mkdir(mode=0o700)
    shutil.copy2(nvram, output / settings['NVRAM'])
    subprocess.run([image_tool, 'create', '-f', 'qcow2', '-F', info['format'],
                    '-b', disk, output / settings['Disk']], check=True)
    (output / 'Machine.plist').write_bytes(plistlib.dumps(settings))
    print(output)
    print('Keep the original VM stopped and its backing disk unchanged.')


if __name__ == '__main__':
    main()
