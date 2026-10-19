#!/usr/bin/env python3
"""Describe the stopped VM's active Parallels disk as a read-only QEMU graph.

Never merges or modifies source images. Refuses ambiguous snapshot selection,
split/encrypted disks and paths escaping the disk bundle. Output is accepted by
qemu-img as a JSON pseudo-filename, including every active ancestor explicitly.
"""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ZERO = '{00000000-0000-0000-0000-000000000000}'


def active_graph(bundle, disk):
    bundle, disk = Path(bundle).resolve(), Path(disk).resolve()
    config = ET.parse(bundle / 'config.pvs').getroot()
    uuid = config.findtext('.//VmUuid').strip('{}').lower()
    states = json.loads(subprocess.check_output(['prlctl', 'list', '-a', '-j']))
    match = [v for v in states if v['uuid'].strip('{}').lower() == uuid]
    if len(match) != 1 or match[0]['status'] != 'stopped':
        raise ValueError('Source VM must be registered and stopped')
    root = ET.parse(disk / 'DiskDescriptor.xml').getroot()
    if root.findtext('./Disk_Parameters/Encryption/Engine', ZERO) != ZERO:
        raise ValueError('Encrypted source is unsupported')
    storage = root.findall('./StorageData/Storage')
    if len(storage) != 1 or storage[0].findtext('Start') != '0':
        raise ValueError('Split/nonzero-origin disk is unsupported')
    images = {}
    for image in storage[0].findall('Image'):
        guid = image.findtext('GUID')
        if guid in images or image.findtext('Type') != 'Compressed':
            raise ValueError('Duplicate GUID or non-Parallels image')
        path = (disk / image.findtext('File')).resolve()
        if path.parent != disk or not path.is_file():
            raise ValueError('Missing image or path outside disk bundle')
        images[guid] = path
    parents = {}
    for shot in root.findall('./Snapshots/Shot'):
        guid = shot.findtext('GUID')
        if guid in parents:
            raise ValueError('Duplicate snapshot GUID')
        parents[guid] = shot.findtext('ParentGUID')
    if set(parents) != set(images):
        raise ValueError('Image and snapshot nodes disagree')
    snapshots = bundle / 'Snapshots.xml'
    saved = ET.parse(snapshots).getroot() if snapshots.exists() else None
    current = [] if saved is None else [s.get('guid') for s in saved.iter('SavedStateItem')
                                       if s.get('current') == 'yes']
    if len(current) > 1:
        raise ValueError('Multiple current snapshots')
    saved_guids = set() if saved is None else {s.get('guid') for s in saved.iter('SavedStateItem')}
    parent = current[0] if current else ZERO
    candidates = [g for g, p in parents.items() if p == parent and g not in saved_guids]
    if len(candidates) != 1:
        raise ValueError('Cannot uniquely identify writable current child')
    chain = []
    guid = candidates[0]
    while guid != ZERO:
        if guid in chain or guid not in images:
            raise ValueError('Cyclic or missing snapshot ancestor')
        chain.append(guid)
        guid = parents[guid]
    graph = None
    for guid in reversed(chain):
        graph = {'driver': 'parallels', 'read-only': True,
                 'file': {'driver': 'file', 'filename': str(images[guid]), 'read-only': True},
                 'backing': graph}
    return graph


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vm_bundle')
    parser.add_argument('disk_bundle')
    args = parser.parse_args()
    try:
        print('json:' + json.dumps(active_graph(args.vm_bundle, args.disk_bundle), separators=(',', ':')))
    except (ValueError, OSError, ET.ParseError) as error:
        parser.exit(1, str(error) + '\n')
