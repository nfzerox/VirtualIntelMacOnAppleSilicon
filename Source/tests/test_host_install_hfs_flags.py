from pathlib import Path
import plistlib
import stat
import struct
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "darwin", "Uses the native detached HFS writer")
class DetachedHFSFlagsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.work.cleanup)
        root = Path(cls.work.name)
        tools = Path(__file__).resolve().parents[1] / "tools/HostInstall"
        source = root / "probe.m"
        source.write_text(r'''
#import "VIMHFSCatalog.h"
int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=2)return 64;
    NSError *error=nil;
    BOOL ok=VIMFinalizeHFSCatalog([NSURL fileURLWithPath:@(argv[1])],0,65536,
        @{@9:@[@0,@0,@040755,@0x10],@10:@[@501,@20,@0100644]},9,10,&error);
    if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
    return ok?0:1;
}}
''')
        cls.probe = root / "probe"
        subprocess.run(("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                        "-framework", "Foundation", "-lz", "-I", tools, source,
                        tools / "VIMHFSCatalog.m", "-o", cls.probe), check=True)

    def test_only_requested_admin_bits_change_and_second_finalize_is_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary)
            (image / "bands").mkdir()
            (image / "Info.plist").write_bytes(plistlib.dumps({"diskimage-bundle-type": "com.apple.diskimage.sparsebundle",
                                                             "band-size": 65536, "size": 65536}))
            data = bytearray(65536)
            header, tree, leaf = 1024, 8192, 12288
            data[header:header + 4] = b"H+\0\4"
            struct.pack_into(">I", data, header + 4, 0x100)
            struct.pack_into(">II", data, header + 40, 4096, 16)
            struct.pack_into(">Q", data, header + 272, 8192)
            struct.pack_into(">I", data, header + 284, 2)
            struct.pack_into(">II", data, header + 288, 2, 2)
            data[-1024:-1020] = data[header:header + 4]
            data[tree + 8] = 1
            struct.pack_into(">I", data, tree + 24, 1)
            struct.pack_into(">H", data, tree + 32, 4096)
            data[leaf + 8] = 255
            struct.pack_into(">H", data, leaf + 10, 2)
            records = []
            start = 14
            for index, (cnid, kind, mode, size) in enumerate(((9, 1, stat.S_IFDIR | 0o755, 88),
                                                           (10, 2, stat.S_IFREG | 0o755, 248))):
                struct.pack_into(">HIHH", data, leaf + start, 8, 2, 1, ord("a") + index)
                record = leaf + start + 10
                records.append(record)
                struct.pack_into(">H", data, record, kind)
                struct.pack_into(">I", data, record + 8, cnid)
                struct.pack_into(">II", data, record + 32, 77, 88)
                data[record + 40:record + 42] = b"\x02\x40"
                struct.pack_into(">H", data, record + 42, mode)
                struct.pack_into(">I", data, record + 44, 7)
                data[record + 48:record + 80] = b"F" * 32
                struct.pack_into(">H", data, leaf + 4096 - 2 * (index + 1), start)
                start += 10 + size
            struct.pack_into(">H", data, leaf + 4096 - 6, start)
            band = image / "bands/0"
            band.write_bytes(data)
            result = subprocess.run((self.probe, image), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            after = band.read_bytes()
            self.assertEqual(after[records[0] + 40], 0x12)
            self.assertEqual(after[records[1] + 40], 0x02)
            for record in records:
                self.assertEqual(after[record + 41], 0x40)
                self.assertEqual(after[record + 44:record + 80], data[record + 44:record + 80])
            self.assertEqual(struct.unpack_from(">II", after, records[0] + 32), (0, 0))
            self.assertEqual(struct.unpack_from(">II", after, records[1] + 32), (501, 20))
            self.assertEqual(struct.unpack_from(">H", after, records[1] + 42)[0], stat.S_IFREG | 0o644)
            result = subprocess.run((self.probe, image), capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(band.read_bytes(), after)
