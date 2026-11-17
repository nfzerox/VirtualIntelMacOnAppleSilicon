import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "tools/HostInstall"
PROBE = '''#import "VIMTigerKeychain.h"
int main(int argc,char **argv) {@autoreleasepool {
    if(argc<3)return 2;
    NSMutableArray *certs=[NSMutableArray array];
    for(int i=2;i<argc;i++) {
        NSData *certificate=[NSData dataWithContentsOfFile:@(argv[i])];
        if(!certificate)return 2;
        [certs addObject:certificate];
    }
    NSError *error=nil;
    BOOL ok=VIMCreateTigerCertificateKeychain(certs,[NSURL fileURLWithPath:@(argv[1])],&error);
    if(!ok)fprintf(stderr,"%s\\n",error.localizedDescription.UTF8String);
    return ok?0:1;
}}
'''


class TigerKeychainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="vim-tiger-keychains-test-")
        cls.addClassCleanup(cls.work.cleanup)
        root = Path(cls.work.name)
        cls.probe = root / "keychains"
        source = root / "probe.m"
        source.write_text(PROBE)
        subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Werror", "-I", SOURCE, source,
                        SOURCE / "VIMTigerKeychain.m", "-framework", "Foundation", "-framework", "Security",
                        "-o", cls.probe], check=True)

    def test_duplicate_certificate_and_host_search_list(self):
        with tempfile.TemporaryDirectory(dir=self.work.name) as folder:
            root = Path(folder)
            key, certificate = root / "fixture-key.pem", root / "fixture.der"
            subprocess.run(["/usr/bin/openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                            "-subj", "/CN=Virtual Intel Mac Test Certificate", "-keyout", key, "-outform", "DER",
                            "-out", certificate], check=True, capture_output=True)
            invalid = root / "invalid.der"
            invalid.write_bytes(b"not a certificate")
            keychain = root / "X509Anchors"
            list_command = ["/usr/bin/security", "list-keychains", "-d", "user"]
            before = subprocess.check_output(list_command)
            subprocess.run([self.probe, keychain, certificate, certificate], check=True)
            result = subprocess.check_output(["/usr/bin/security", "find-certificate", "-a", "-p", keychain])
            self.assertEqual(result.count(b"BEGIN CERTIFICATE"), 1)
            self.assertEqual(subprocess.run([self.probe, root / "X509Certificates", invalid],
                                            capture_output=True).returncode, 1)
            self.assertFalse((root / "X509Certificates").exists())
            self.assertEqual(subprocess.check_output(list_command), before)

    @unittest.skipUnless(os.environ.get("VIM_TIGER_KEYCHAIN_FIXTURES"), "Original Apple certificates not configured")
    def test_original_update_certificate_sets(self):
        root = Path(os.environ["VIM_TIGER_KEYCHAIN_FIXTURES"])
        updates = ("MacOSXUpdCombo10.4.11Intel", "SecUpd2009-005Intel")
        with tempfile.TemporaryDirectory(prefix="verified-keychains-", dir=root) as folder:
            for role in ("Anchors", "Certificates"):
                certificates = [p for update in updates
                                for p in sorted((root / update / "System/Library/Keychains" / role).iterdir())]
                self.assertGreater(len(certificates), 20)
                out = Path(folder) / ("X509" + role)
                subprocess.run([self.probe, out, *certificates], check=True)
                contents = subprocess.check_output(["/usr/bin/security", "find-certificate", "-a", "-p", out])
                self.assertGreater(contents.count(b"BEGIN CERTIFICATE"), 20)


if __name__ == "__main__":
    unittest.main()
