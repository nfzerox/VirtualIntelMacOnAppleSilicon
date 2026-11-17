from hashlib import sha256
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from uuid import UUID

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_apfs_records import BLOCK, block_checksum
from host_install_bigsur_keybags import decoded_keybags, decrypt_keybag
from host_install_bigsur_snapshot import verify_detached


class BigSurKeybagTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "darwin", "Uses macOS CommonCrypto")
    def test_xts_uses_little_endian_sector_numbers_and_resets_each_tweak(self):
        data = bytes((i * 29 + i // 512) & 255 for i in range(BLOCK))
        plain = decrypt_keybag(data, 0x148066, bytes(range(16)))
        # Independently calculated with a standard AES-XTS implementation.
        self.assertEqual(sha256(plain).hexdigest(),
                         "3655db5299737b724e3d4cf56e53ef3f53b2df8baaf547a971acf71e1a56f6fe")

    def container(self):
        volume = UUID("12345678-1234-5678-9abc-def012345678")
        nx, data, bag, records = (bytearray(BLOCK) for _ in range(4))
        nx[72:88] = bytes(range(16))
        struct.pack_into("<QQ", nx, 1296, 3, 1)
        struct.pack_into("<Q", data, 264, 1)
        struct.pack_into("<QIIHHI", bag, 16, 2, 0x6B657973, 0, 2, 1, 64)
        struct.pack_into("<16sHH4xQQ", bag, 48, volume.bytes, 3, 16, 4, 1)
        struct.pack_into("<QIIHHI", records, 16, 2, 0x72656373, 0, 2, 0, 16)
        for value in (bag, records):
            struct.pack_into("<Q", value, 0, block_checksum(value))
        objects = {3: bag, 4: records}
        return SimpleNamespace(superblock=nx, blocks=16, xid=3,
            volumes=lambda: [SimpleNamespace(superblock=data, uuid=volume)], read=objects.__getitem__), objects, data

    @patch("host_install_bigsur_keybags.decrypt_keybag", side_effect=lambda data, *_: bytes(data))
    def test_checks_decoded_volume_reference_and_both_object_checksums(self, _):
        disk, objects, _ = self.container()
        self.assertEqual(decoded_keybags(disk), objects)
        objects[4][512] ^= 1
        with self.assertRaisesRegex(ValueError, "Invalid Big Sur APFS keybag at block 4"):
            decoded_keybags(disk)

    def test_refuses_encrypted_volumes_before_decoding_or_calling_checker(self):
        disk, _, data = self.container()
        struct.pack_into("<Q", data, 264, 8)
        with self.assertRaisesRegex(ValueError, "requires unencrypted APFS volumes"):
            decoded_keybags(disk)

    def test_audit_transform_cannot_be_used_to_qualify_a_publish_plan(self):
        disk = SimpleNamespace(image=None)
        with self.assertRaisesRegex(ValueError, "already published disk audit"):
            verify_detached(disk, {0: bytes(BLOCK)}, software_keybags=True)
