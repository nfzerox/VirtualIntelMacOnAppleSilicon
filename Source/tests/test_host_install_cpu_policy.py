"""Native launch-policy regression check for original-media Intel caches."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class GuestCPUPolicyTests(unittest.TestCase):
    def test_haswell_cache_guests_do_not_inherit_penryn(self):
        source = Path(__file__).resolve().parents[1]
        program = r'''
#import "VIMGuestFamilies.h"
int main(void) { @autoreleasepool {
    NSArray *modern=@[@"ventura",@"sonoma",@"sequoia",@"tahoe"];
    for(NSString *guest in VIMSupportedGuests()) {
        BOOL cryptex=[modern containsObject:guest];
        NSString *expected=cryptex?
            @"Haswell-noTSX,stepping=4,vendor=GenuineIntel,+pcid,-tsc-deadline,-invpcid":
            @"Penryn,vendor=GenuineIntel,+sse4.2,+popcnt,+aes,+pclmulqdq,+pcid";
        if(![VIMEmulatedCPUModel(guest,NO) isEqual:expected])return 1;
        NSString *arm=[expected stringByAppendingString:cryptex?
            @",tsc-frequency=1200000000":@",tsc-frequency=1200000000,vmware-cpuid-freq=off"];
        if(![VIMEmulatedCPUModel(guest,YES) isEqual:arm])return 2;
    }
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix="vim-cpu-policy-") as directory:
            root = Path(directory)
            (root / "main.m").write_text(program)
            subprocess.run(("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                            "-arch", "arm64", "-arch", "x86_64", "-mmacosx-version-min=11.0",
                            "-I", source / "VirtualIntelMac", root / "main.m",
                            "-framework", "Foundation", "-o", root / "probe"), check=True)
            subprocess.run((root / "probe",), check=True)


if __name__ == "__main__":
    unittest.main()
