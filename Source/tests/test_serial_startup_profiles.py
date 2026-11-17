"""Only the native UART publication path is serialized on affected kernels."""
from pathlib import Path
import plistlib
import runpy
import unittest


SOURCE = Path(__file__).resolve().parents[1]
entries = runpy.run_path(str(SOURCE / 'scripts/build-snowleopard-serial-efi.py'))['serial_profile_entries']


class SerialStartupProfileTests(unittest.TestCase):
    def test_profile_kernel_ranges_and_native_provider(self):
        for guest, limits in (('snowleopard', ('10.7.3', '10.8.0')),
                              ('lion', ('11.0.0', '11.99.99'))):
            with self.subTest(guest=guest):
                original = {'Kernel': {'Add': [{'BundlePath': 'Other.kext', 'Enabled': True}]},
                            'ACPI': {'Add': []}, 'NVRAM': {'Delete': {}}}
                result = entries(plistlib.dumps(original), b'executable', guest)
                config = plistlib.loads(result['config.plist'])
                self.assertEqual(config['ACPI'], original['ACPI'])
                self.assertEqual(config['NVRAM'], original['NVRAM'])
                self.assertEqual(config['Kernel']['Add'][0], original['Kernel']['Add'][0])
                extension = config['Kernel']['Add'][1]
                self.assertEqual((extension['MinKernel'], extension['MaxKernel']), limits)
                self.assertEqual(extension['Arch'], 'x86_64')
                info = plistlib.loads(result['Kexts/VIMSerialStartup.kext/Contents/Info.plist'])
                self.assertEqual(info['IOKitPersonalities']['Virtual PC UART']['IOProviderClass'],
                                 'com_apple_driver_16X50UARTSync')
                self.assertEqual(result['Kexts/VIMSerialStartup.kext/Contents/MacOS/VIMSerialStartup'],
                                 b'executable')
                # Updating an existing profile must not load the extension twice.
                repeated = entries(result['config.plist'], b'executable', guest)
                self.assertEqual(plistlib.loads(repeated['config.plist']), config)

    def test_unaffected_release_is_not_admitted(self):
        with self.assertRaises(KeyError):
            entries(plistlib.dumps({'Kernel': {'Add': []}}), b'executable', 'mountainlion')


if __name__ == '__main__':
    unittest.main()
