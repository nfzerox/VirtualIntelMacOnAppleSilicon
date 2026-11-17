import copy
from pathlib import Path
import runpy
import unittest

configure = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                              'scripts/build-opengl-firmware.py'))['configure_boot_graphics']
GUID = '4D1EDE05-38C7-4A6A-9CC6-4BCCA8B38C14'


class MavericksBootGraphics(unittest.TestCase):
    def test_clear_only_mavericks_black_mode(self):
        original = {'NVRAM': {'Add': {GUID: {'DefaultBackgroundColor': bytes(4),
                                            'unrelated': b'keep'}},
                              'Delete': {GUID: ['existing']}}}
        for guest in ('tiger', 'leopard', 'snowleopard', 'lion', 'mountainlion',
                      'yosemite', 'elcapitan', 'sierra', 'highsierra'):
            config = copy.deepcopy(original)
            configure(config, guest)
            self.assertEqual(config, original)
        config = copy.deepcopy(original)
        configure(config, 'mavericks')
        self.assertEqual(config['NVRAM']['Add'][GUID], {'unrelated': b'keep'})
        self.assertEqual(config['NVRAM']['Delete'][GUID],
                         ['existing', 'DefaultBackgroundColor'])
        expected = copy.deepcopy(config)
        configure(config, 'mavericks')
        self.assertEqual(config, expected)

    def test_custom_template_without_the_variable(self):
        config = {'NVRAM': {'Add': {}}}
        configure(config, 'mavericks')
        self.assertEqual(config['NVRAM']['Add'], {})
        self.assertEqual(config['NVRAM']['Delete'][GUID], ['DefaultBackgroundColor'])


if __name__ == '__main__':
    unittest.main()
