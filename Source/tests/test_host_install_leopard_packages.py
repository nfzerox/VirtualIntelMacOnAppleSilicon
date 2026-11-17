import lzma
from pathlib import Path, PurePosixPath
import sqlite3
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_install_leopard_packages import ACL_PACKAGE, DENY_DELETE, Receipts, unpack_flat_package
from host_install_hfs import STATE, Target, stage_efi_media_probe


class LeopardReceiptsTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "darwin", "requires the native macOS worker")
    def test_native_receipts_accept_mountpoint_alias_and_keep_final_nofollow(self):
        tools = Path(__file__).resolve().parents[1] / "tools" / "HostInstall"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            canonical = root / "volume"
            canonical.mkdir()
            alias = root / "mountpoint"
            alias.symlink_to(canonical, target_is_directory=True)
            probe = root / "probe.m"
            probe.write_text(r'''
#import "VIMFlatReceipts.h"
int main(int argc,const char *argv[]) {
    @autoreleasepool {
        if(argc!=2)return 2;
        @try {
            NSURL *path=[NSURL fileURLWithFileSystemRepresentation:argv[1] isDirectory:NO relativeToURL:nil];
            VIMWriteLeopardReceipts(path,
                @[@{@"Identifier":@"com.apple.pkg.BaseSystem",@"Version":@"1",@"Groups":@[@"shared"],
                    @"Metadata":@{@"Applications":@[@0,@80,@0040775],@"usr/bin/sudo":@[@0,@0,@0104511]}},
                  @{@"Identifier":@"com.apple.pkg.ACL",@"Version":@"1",@"Groups":@[@"shared"],@"Metadata":@{}}],
                @[@"Applications",@"Library"],@"deny");
            VIMAppendLeopardReceipts(path,
                @[@{@"Identifier":@"com.apple.pkg.update.os.10.5.8.combo",@"Version":@"2",@"Groups":@[@"shared"],
                    @"Metadata":@{@"Applications":@[@0,@80,@0040775],@"usr/lib/updated.dylib":@[@0,@0,@0100644]}}]);
            return 0;
        } @catch(NSException *e){fprintf(stderr,"%s\n",e.reason.UTF8String);return 1;}
    }
}
''')
            subprocess.run(("/usr/bin/xcrun", "clang", "-fobjc-arc", "-fobjc-exceptions",
                            "-framework", "Foundation", "-lsqlite3", "-I", tools, probe,
                            tools / "VIMFlatReceipts.m", "-o", root / "probe"), check=True)
            subprocess.run((root / "probe", alias / "a.receiptdb"), check=True)
            db = sqlite3.connect(canonical / "a.receiptdb")
            try:
                self.assertEqual(db.execute("PRAGMA integrity_check").fetchone(), ("ok",))
                self.assertEqual(db.execute("PRAGMA user_version").fetchone(), (17,))
                self.assertEqual(db.execute("SELECT COUNT(*) FROM groups").fetchone(), (1,))
                self.assertEqual(db.execute("SELECT COUNT(*) FROM pkgs").fetchone(), (3,))
                self.assertEqual(db.execute("SELECT COUNT(*) FROM paths WHERE path='Applications'").fetchone(), (1,))
                self.assertEqual(db.execute("SELECT COUNT(*) FROM pkgs_paths JOIN paths USING(path_key) WHERE path='Applications'").fetchone(), (2,))
                self.assertEqual(db.execute("SELECT vers FROM pkgs WHERE pkgid='com.apple.pkg.update.os.10.5.8.combo'").fetchone(), ("2",))
                self.assertEqual(db.execute("SELECT path,acl FROM paths JOIN acls USING(path_key) ORDER BY path").fetchall(),
                                 [("Applications", "deny"), ("Library", "deny")])
                self.assertEqual(db.execute("SELECT uid,gid,perms FROM paths JOIN pkgs_paths USING(path_key) WHERE path=?",
                                            ("usr/bin/sudo",)).fetchone(), (0, 0, 0o104511))
            finally:
                db.close()
            outside = root / "untouched"
            outside.write_bytes(b"original")
            (canonical / "redirected").symlink_to(outside)
            failure = subprocess.run((root / "probe", alias / "redirected"), capture_output=True)
            self.assertEqual(failure.returncode, 1)
            self.assertEqual(outside.read_bytes(), b"original")

    def test_flat_package_streams_mixed_pbzx_chunks_into_native_pax(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, members, target = (root / directory for directory in ("source", "members", "target"))
            for directory in (source, members, target):
                directory.mkdir()
            content = b"Yosemite package resource\n"
            (source / "resource").write_bytes(content)
            subprocess.run(("/usr/bin/mkbom", source, members / "Bom"), check=True)
            cpio = subprocess.check_output(("/bin/pax", "-w", "-x", "cpio", "resource"), cwd=source)
            first, last = cpio[:81], cpio[81:]
            stored = lzma.compress(first)
            (members / "Payload").write_bytes(b"pbzx" + struct.pack(">Q", len(cpio)) +
                struct.pack(">QQ", len(first), len(stored)) + stored +
                struct.pack(">QQ", len(last), len(last)) + last)
            (members / "PackageInfo").write_text(
                '<pkg-info identifier="com.apple.pkg.Sample" version="1" install-location="/"/>')
            package = root / "Sample.pkg"
            subprocess.run(("/usr/bin/xar", "-cf", package, "Bom", "PackageInfo", "Payload"),
                           cwd=members, check=True)
            info, _, metadata = unpack_flat_package(package, root / "unpacked", target)
            self.assertEqual((target / "resource").read_bytes(), content)
            self.assertEqual(info.get("identifier"), "com.apple.pkg.Sample")
            self.assertIn(Path("resource"), metadata)
            direct = root / "direct"
            direct.mkdir()
            stage = root / "streamed"
            _, _, streamed = unpack_flat_package(package, stage, direct, stream_pbzx=True)
            self.assertEqual((direct / "resource").read_bytes(), content)
            self.assertEqual(streamed, metadata)
            self.assertFalse((stage / "Payload").exists())

    def test_registers_native_metadata_with_shared_paths(self):
        base = {PurePosixPath("Applications"): (0, 80, 0o40775),
                PurePosixPath("usr/bin/sudo"): (0, 0, 0o104511)}
        group = "com.apple.repair-permissions.pkg-group"
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "db/a.receiptdb"
            with Receipts(file) as db:
                db.add("com.apple.pkg.BaseSystem", "1", [group], base, 123)
                db.add(ACL_PACKAGE, "1", [group], {}, 123,
                       acl_paths=[PurePosixPath("Applications"), PurePosixPath("Library")])
            with sqlite3.connect(file) as db:
                self.assertEqual(db.execute("PRAGMA integrity_check").fetchone(), ("ok",))
                self.assertEqual(db.execute("PRAGMA user_version").fetchone(), (17,))
                self.assertEqual(db.execute("SELECT COUNT(*) FROM groups").fetchone(), (1,))
                self.assertEqual(db.execute("SELECT path FROM paths ORDER BY path_key").fetchall(),
                                 [("Applications",), ("usr/bin/sudo",), ("Library",)])
                self.assertEqual(db.execute("SELECT pkg_key, path_key, acl FROM acls ORDER BY path_key").fetchall(),
                                 [(2, 1, DENY_DELETE), (2, 3, DENY_DELETE)])
                self.assertEqual(db.execute("SELECT uid,gid,perms FROM pkgs_paths WHERE path_key=2").fetchone(),
                                 (0, 0, 0o104511))

    def test_installed_support_modes_do_not_inherit_exfat_modes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tools = Path("Tools")
            (root / tools).mkdir(mode=0o700)
            for name in ("opengl-display-agent", "policy.plist"):
                (root / tools / name).touch(mode=0o700)
            target = Target(root, {})
            target.remember_tree(tools, 0, 0, support=True)
            self.assertEqual(target.metadata[tools], (0, 0, 0o40755))
            self.assertEqual(target.metadata[tools / "opengl-display-agent"], (0, 0, 0o100755))
            self.assertEqual(target.metadata[tools / "policy.plist"], (0, 0, 0o100644))

    def test_efi_probe_preserves_the_original_fat32_probe(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = Target(root, {})
            path = Path("System/Library/Filesystems/msdos.fs/Contents/Info.plist")
            fat = {"FSMediaProperties": {"Content Hint": "DOS_FAT_32", "Leaf": True},
                   "FSProbeArguments": "-p", "FSProbeOrder": 1, "FSProbeExecutable": "msdos.util"}
            original = {"FSMediaTypes": {"DOS_FAT_32": fat}}
            target.write_plist(path, original)
            stage_efi_media_probe(target, "9J3050", STATE)
            updated = plistlib.loads((root / path).read_bytes())
            identifier = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
            self.assertEqual(updated["FSMediaTypes"]["DOS_FAT_32"], fat)
            self.assertEqual(updated["FSMediaTypes"][identifier]["FSMediaProperties"]["Content Hint"], identifier)
            self.assertIs(updated["FSMediaTypes"][identifier]["autodiskmount"], False)
            backup = STATE / "OriginalFilesystemMetadata/9J3050/msdos-Info.plist"
            self.assertEqual(plistlib.loads((root / backup).read_bytes()), original)
            self.assertEqual(target.metadata[backup], (0, 0, 0o100600))


if __name__ == "__main__":
    unittest.main()
