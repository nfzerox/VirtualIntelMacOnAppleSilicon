import io
import os
from pathlib import Path
import plistlib
import subprocess
import tarfile
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "tools/HostInstall"
PROBE = '''#import "VIMApplePeriodUpdate.h"
int main(int argc,char **argv) {@autoreleasepool {
    if(argc!=6)return 2;
    NSError *error=nil;
    NSData *key=[NSData dataWithContentsOfFile:@(argv[3])];
    BOOL ok=VIMStageApplePeriodUpdate([NSURL fileURLWithPath:@(argv[1])],@(argv[2]),key,
        400ULL<<20,[NSURL fileURLWithPath:@(argv[4])],^BOOL{return !strcmp(argv[5],"cancel");},&error);
    if(!ok)fprintf(stderr,"%s\\n",error.localizedDescription.UTF8String);
    return ok?0:1;
}}
'''


class TigerUpdateSignatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="vim-tiger-update-test-")
        cls.addClassCleanup(cls.work.cleanup)
        root = Path(cls.work.name)
        source = root / "probe.m"
        cls.probe = root / "verify"
        source.write_text(PROBE)
        subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Werror", "-I", SOURCE,
                        source, SOURCE / "VIMApplePeriodUpdate.m", "-framework", "Foundation",
                        "-framework", "Security", "-larchive", "-o", cls.probe], check=True)
        cls.private = root / "test-only.pem"
        cls.key = root / "test-only.der"
        subprocess.run(["/usr/bin/openssl", "genrsa", "-out", cls.private, "2048"],
                       check=True, capture_output=True)
        subprocess.run(["/usr/bin/openssl", "rsa", "-in", cls.private, "-RSAPublicKey_out", "-outform", "DER",
                        "-out", cls.key], check=True, capture_output=True)

    def test_signed_payload_and_rejection_paths(self):
        member = "TigerTest.pkg.tar"
        payload = b"fixture payload" * 32768
        signature = subprocess.check_output(["/usr/bin/openssl", "dgst", "-sha1", "-sign", self.private],
                                            input=payload).hex().encode()
        record = member.encode() + b"\n" + signature + b"\n"
        cases = {
            "correct": [("signature", record), (member, payload)],
            "reversed": [(member, payload), ("signature", record)],
            "tampered": [("signature", record), (member, payload[:-1] + b"!")],
            "wrong-signed-name": [("signature", b"Other.pkg.tar\n" + signature), (member, payload)],
            "duplicate": [("signature", record), (member, payload), (member, payload)],
            "extra": [("signature", record), (member, payload), ("../other", b"no")],
            "cancel": [("signature", record), (member, payload)],
        }
        for case, members in cases.items():
            with self.subTest(case=case), tempfile.TemporaryDirectory(dir=self.work.name) as folder:
                archive, out = (Path(folder) / name for name in ("outer.tar", "staged.tar"))
                with tarfile.open(archive, "w") as tar:
                    for name, body in members:
                        entry = tarfile.TarInfo(name)
                        entry.size = len(body)
                        tar.addfile(entry, io.BytesIO(body))
                reply = subprocess.run([self.probe, archive, member, self.key, out, case], capture_output=True)
                expected = case in ("correct", "reversed")
                self.assertEqual(reply.returncode, 0 if expected else 1, reply.stderr.decode())
                self.assertEqual(out.exists(), expected)
                if expected:
                    self.assertEqual(out.read_bytes(), payload)

    @unittest.skipUnless(os.environ.get("VIM_TIGER_UPDATE_FIXTURES"), "Original Apple packages not configured")
    def test_original_apple_updates(self):
        root = Path(os.environ["VIM_TIGER_UPDATE_FIXTURES"])
        key = Path(self.work.name) / "AppleTiger.der"
        key.write_bytes(plistlib.loads((root / "AppleTigerPublicKey.plist").read_bytes())["ApplePkgKeyData"])
        for name in ("MacOSXUpdCombo10.4.11Intel", "SecUpd2009-005Intel"):
            with self.subTest(package=name), tempfile.TemporaryDirectory(prefix="verified-", dir=root) as folder:
                subprocess.run([self.probe, root / (name + ".tar"), name + ".pkg.tar", key,
                                Path(folder) / "staged.tar", "verify"], check=True)


if __name__ == "__main__":
    unittest.main()
