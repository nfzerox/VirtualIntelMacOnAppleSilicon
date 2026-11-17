import hashlib
import lzma
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


COMPAT = Path(__file__).resolve().parents[1] / "compat"
PROBE = r'''
#import "VIMEmptySourceRecoveryPatch.h"
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 4) return 2;
        NSError *error = nil;
        BOOL ok = VIMDecodeEmptySourceRecoveryPatch([NSURL fileURLWithPath:@(argv[1])],
            [NSURL fileURLWithPath:@(argv[2])], ^BOOL { return !strcmp(argv[3], "cancel"); }, &error);
        return ok ? 0 : (int)error.code;
    }
}
'''


def patch(chunks):
    result = b"".join(plain for plain, _ in chunks)
    header = bytearray(0x48)
    header[:8] = b"BXDIFF50"
    struct.pack_into("<Q", header, 0x10, len(result))
    header[0x28:0x3C] = hashlib.sha1(result).digest()
    header[0x3C:0x40] = b"pbzx"
    struct.pack_into(">Q", header, 0x40, 1 << 20)
    return bytes(header) + b"".join(struct.pack(">QQ", len(plain), len(stored)) + stored
                                     for plain, stored in chunks)


class RecoveryPatchTests(unittest.TestCase):
    def test_native_recovery_decode_and_safe_failure(self):
        with tempfile.TemporaryDirectory(prefix="vim-recovery-patch-") as temporary:
            root = Path(temporary)
            probe = root / "decode"
            subprocess.run(["xcrun", "clang", "-x", "objective-c", "-", "-fobjc-arc",
                            "-Wall", "-Wextra", "-Werror", "-I", COMPAT, "-framework", "Foundation",
                            "-lcompression", "-o", probe], input=PROBE.encode(), check=True)
            raw, compressed = b"raw data", b"native recovery contents\n" * 8192
            original = patch([(raw, raw), (compressed, lzma.compress(compressed))])
            source, out = root / "Source.patch", root / "BaseSystem.dmg"
            source.write_bytes(original)
            command = [probe, source, out, "decode"]
            subprocess.run(command, check=True)
            self.assertEqual(out.read_bytes(), raw + compressed)
            self.assertNotEqual(subprocess.run(command).returncode, 0)
            self.assertEqual(out.read_bytes(), raw + compressed)
            out.unlink()
            self.assertNotEqual(subprocess.run([probe, source, out, "cancel"]).returncode, 0)
            self.assertFalse(out.exists())
            for payload in (original[:-3], original[:0x28] + b"\0" * 20 + original[0x3C:],
                            original[:8] + b"\1" + original[9:]):
                source.write_bytes(payload)
                self.assertNotEqual(subprocess.run(command).returncode, 0)
                self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
