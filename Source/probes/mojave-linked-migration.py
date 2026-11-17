#!/usr/bin/env python3
"""Prepare a disposable existing-VM migration fixture, not a fresh install.

Keep the verified base disk and its guest journal unchanged. The real app's
--repair-vm operation must perform and verify the subsequent installation.
The identifier is deliberately preserved: this is a snapshot of that Mac,
opened in its own test library, not a newly provisioned guest.
"""
import argparse
import fcntl
from pathlib import Path
import plistlib
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('base', type=Path)
p.add_argument('library', type=Path)
p.add_argument('qemu_image', type=Path)
p.add_argument('--name', default='Mojave Production Migration')
p.add_argument('--ssh-port', type=int, required=True)
p.add_argument('--sharing-port', type=int, required=True)
a = p.parse_args()
base = a.base.resolve()
configuration = plistlib.loads((base/'Machine.plist').read_bytes())
record = plistlib.loads((base/'State/GuestInstallation.plist').read_bytes())
if (configuration.get('Guest') != 'mojave' or record.get('Phase') != 'graphics-verified'
        or configuration.get('Disk') != 'Disks/System.qcow2'):
    p.error('Choose the stopped, previously verified Mojave base, not the edited research overlay.')
if not a.name or Path(a.name).name != a.name or not all(1024 <= n <= 65535 for n in (a.ssh_port,a.sharing_port)):
    p.error('Use a single bundle name and unprivileged test ports.')
destination = a.library.resolve()/'Machines'/(a.name+'.vintelmac')
if destination.exists():
    p.error('The destination already exists; it will not be replaced.')
disk = (base/configuration['Disk']).resolve()
with (base/'State/Session.lock').open('rb') as lock:
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    destination.mkdir(parents=True)
    (destination/'Disks').mkdir()
    (destination/'State').mkdir()
    shutil.copytree(base/'Firmware',destination/'Firmware')
    # Keep the base's real transaction evidence. Never manufacture a new
    # completed transaction or change the guest's journal/hash records.
    for state in (base/'State').glob('*.plist'):
        shutil.copyfile(state,destination/'State'/state.name)
    configuration['SSHPort'] = a.ssh_port
    configuration['FileSharingPort'] = a.sharing_port
    (destination/'Machine.plist').write_bytes(plistlib.dumps(configuration))
    subprocess.run([str(a.qemu_image.resolve()),'create','-f','qcow2','-F','qcow2',
                    '-b',str(disk),str(destination/configuration['Disk'])],check=True)
print(destination)
