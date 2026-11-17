import sys
from pathlib import Path
import tempfile
import unittest
from uuid import UUID

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_tiger_netinfo import Record, provision_account, read_store, shadow_hash, string, write_store


def fixture():
    def node(i, parent, children, name, **attributes):
        return Record(i, 1, i, parent, children,
                      [(string(k), [string(v)]) for k, v in {"name": name, **attributes}.items()], [])
    return {r.identifier: r for r in (
        node(0, 0, [1, 2], "/"), node(1, 0, [4], "users"),
        node(2, 0, [3], "groups"), node(3, 2, [], "admin", users="root"),
        node(4, 1, [], "root", uid="0"),
    )}


class TigerNetInfoTests(unittest.TestCase):
    def test_offline_account_preserves_existing_users(self):
        records = fixture()
        root_user = records[4].encode()
        guid = UUID(int=42)
        self.assertEqual(provision_account(records, "tiger", "Tiger Test", guid), 501)
        self.assertEqual(records[4].encode(), root_user)
        self.assertEqual(records[1].children, [4, 5])
        self.assertEqual(records[3].get("users"), list(map(string, ("root", "tiger"))))
        self.assertEqual(records[3].get("groupmembers"), [string(str(guid).upper())])
        self.assertEqual(records[5].get("authentication_authority"), [string(";ShadowHash;")])
        self.assertEqual(records[5].get("passwd"), [string("********")])
        with self.assertRaisesRegex(ValueError, "replace"):
            provision_account(records, "tiger", "Not a replacement", UUID(int=43))
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "local.nidb"
            write_store(path, records)
            self.assertEqual(read_store(path), records)
            self.assertEqual((path / "Clean").read_bytes(), b"\x1b\0\0\0")

    def test_shadow_hash_matches_tiger_uppercase_format(self):
        encoded = shadow_hash(b"fixture", salt=bytes(range(1, 5)))
        self.assertEqual(len(encoded), 1240)
        self.assertEqual(encoded, encoded.upper())
        # Independent openssl dgst -sha1 over these four bytes and "fixture".
        self.assertEqual(encoded[168:216], b"01020304C53EBA32B081A7FCAC92ACEB7495C012EE47846A")
        self.assertEqual(encoded[:168] + encoded[216:], b"0" * 1192)

    @unittest.skipUnless(sys.platform == "darwin", "Uses the system CommonCrypto NT digest")
    def test_smb_opt_in_populates_only_the_native_nt_slot(self):
        salt = bytes(range(1, 5))
        without = shadow_hash(b"password", salt=salt)
        with_smb = shadow_hash(b"password", salt=salt, smb=True)
        self.assertEqual(with_smb[:32], b"8846F7EAEE8FB117AD06BDD830B7586C")
        self.assertEqual(with_smb[32:], without[32:])
        records = fixture()
        provision_account(records, "sharing", "Share Test", UUID(int=44), smb=True)
        self.assertEqual(records[5].get("authentication_authority"),
                         [string(";ShadowHash;HASHLIST:<SALTED-SHA1,SMB-NT>")])


if __name__ == "__main__":
    unittest.main()
