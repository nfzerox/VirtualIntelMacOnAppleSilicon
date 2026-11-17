"""Exercise the real native decoder's file/ABI contract with a small library."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]
LIBRARY = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct request {
    int64_t reserved[2];const char *old_image,*output,*patch;
    uint32_t not_cryptex_cache,threads,verbose;
};
int RawImagePatch(struct request *r) {
    if(r->reserved[0]||r->reserved[1]||r->old_image[0]||r->threads!=2||r->verbose||r->not_cryptex_cache)return 13;
    if(!strcmp(r->patch,"error"))return 17;
    unsigned char bytes[36]={0};
    if(strcmp(r->patch,"invalid"))memcpy(bytes+32,"NXSB",4);
    FILE *f=fopen(r->output,"wb");if(!f)return 5;
    int ok=fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes);return fclose(f)||!ok?5:0;
}
'''


@unittest.skipUnless(sys.platform == "darwin", "Uses native dyld")
class CryptexDecoderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        cls.root = Path(temp.name)
        cls.tool, cls.library = cls.root / "decoder", cls.root / "decoder.dylib"
        subprocess.run(("xcrun", "clang", "-Wall", "-Wextra", "-Werror",
                        SOURCE / "tools/host-install-cryptex.c", "-o", cls.tool), check=True)
        code = cls.root / "library.c"
        code.write_text(LIBRARY)
        subprocess.run(("xcrun", "clang", "-dynamiclib", code, "-o", cls.library), check=True)

    def run_decode(self, patch, output):
        return subprocess.run((self.tool, "--library", self.library, patch, output), capture_output=True)

    def test_matching_export_and_request_layout(self):
        result = subprocess.run((self.tool, "--library", self.library, "--availability"), capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            output = Path(temp) / "new.dmg"
            result = self.run_decode("original-patch", output)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes()[32:], b"NXSB")

    def test_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            output = Path(temp) / "original.dmg"
            output.write_bytes(b"preserve this")
            result = self.run_decode("original-patch", output)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b"preserve this")

    def test_api_failure_is_not_success(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            output = Path(temp) / "new.dmg"
            result = self.run_decode("error", output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"RawImagePatch: 17", result.stderr)
            self.assertFalse(output.exists())

    def test_non_apfs_output_is_not_success(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            result = self.run_decode("invalid", Path(temp) / "new.dmg")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"not an APFS image", result.stderr)
