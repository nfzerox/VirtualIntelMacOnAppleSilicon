#!/usr/bin/env python3
"""Synthetic snapshot graph tests; never opens real VM disks."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('source', Path(__file__).with_name('qemu-parallels-source.py'))
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class GraphTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.vm = Path(self.temp.name)
        self.disk = self.vm / 'disk.hdd'
        self.disk.mkdir()
        (self.vm / 'config.pvs').write_text('<VM><VmUuid>{vm}</VmUuid></VM>')
        (self.vm / 'Snapshots.xml').write_text(
            '<States><SavedStateItem guid="base" current="yes"/>'
            '<SavedStateItem guid="old"/></States>')
        self.nodes = [('base', source.ZERO), ('old', 'base'), ('live', 'base')]
        self.mock = patch.object(source.subprocess, 'check_output', return_value=json.dumps(
            [{'uuid': 'vm', 'status': 'stopped'}]).encode())
        self.mock.start()
        self.addCleanup(self.mock.stop)

    def graph(self):
        images, shots = [], []
        for guid, parent in self.nodes:
            (self.disk / (guid + '.hds')).touch()
            images.append(f'<Image><GUID>{guid}</GUID><Type>Compressed</Type>'
                          f'<File>{guid}.hds</File></Image>')
            shots.append(f'<Shot><GUID>{guid}</GUID><ParentGUID>{parent}</ParentGUID></Shot>')
        (self.disk / 'DiskDescriptor.xml').write_text(
            '<Disk><StorageData><Storage><Start>0</Start>' + ''.join(images) +
            '</Storage></StorageData><Snapshots>' + ''.join(shots) + '</Snapshots></Disk>')
        return source.active_graph(self.vm, self.disk)

    def test_active_branch_and_read_only(self):
        graph = self.graph()
        self.assertTrue(graph['file']['filename'].endswith('live.hds'))
        self.assertTrue(graph['backing']['file']['filename'].endswith('base.hds'))
        self.assertIsNone(graph['backing']['backing'])
        self.assertTrue(graph['read-only'])
        self.assertTrue(graph['file']['read-only'])

    def test_ambiguous_child_refused(self):
        self.nodes.append(('other', 'base'))
        with self.assertRaisesRegex(ValueError, 'uniquely'):
            self.graph()

    def test_cycle_refused(self):
        self.nodes[0] = ('base', 'live')
        with self.assertRaisesRegex(ValueError, 'Cyclic'):
            self.graph()

    def test_running_refused(self):
        source.subprocess.check_output.return_value = b'[{"uuid":"vm","status":"running"}]'
        with self.assertRaisesRegex(ValueError, 'stopped'):
            self.graph()

    def test_missing_ancestor_refused(self):
        self.nodes[0] = ('base', 'missing')
        with self.assertRaisesRegex(ValueError, 'missing'):
            self.graph()


if __name__ == '__main__':
    unittest.main()
