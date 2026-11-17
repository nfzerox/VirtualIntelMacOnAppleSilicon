from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE=Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform=='darwin','Native Foundation policy')
class DiskImagesPolicyTests(unittest.TestCase):
    def test_arguments_and_device_normalization(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'probe.m';binary=Path(directory)/'probe'
            source.write_text(r'''
#import "VIMDiskImagesPolicy.h"
int main(void) {@autoreleasepool {
    NSString *image=@"/Library/Test/Staging.sparsebundle";
    NSArray *args=VIMModernWritableImageArguments(@[@"-nomount",image]);
    if(![args isEqual:@[@"image",@"attach",@"--plist",@"--nobrowse",@"--noMount",image]])return 1;
    args=VIMModernWritableImageArguments(@[@"-owners",@"off",@"-mountpoint",@"/private/var/tmp/Target",image]);
    if(![args containsObject:@"noowners"]||![args containsObject:@"--mountPoint"]||![args.lastObject isEqual:image])return 2;
    if(VIMModernWritableImageArguments(@[@"-readonly",image])||
       VIMModernWritableImageArguments(@[@"-imagekey",@"diskimage-class=CRawDiskImage",image])||
       VIMModernWritableImageArguments(@[@"/installer.dmg"]))return 3;
    NSArray *rows=VIMNormalizeDiskImageEntities(@[@{@"dev-entry":@"disk4",@"content-hint":@"GUID_partition_scheme"},
        @{@"dev-entry":@"/dev/disk5",@"mount-point":@"/private/var/tmp/Target"}]);
    if(![rows[0][@"dev-entry"] isEqual:@"/dev/disk4"]||![rows[1][@"dev-entry"] isEqual:@"/dev/disk5"]||
       ![rows[1][@"mount-point"] isEqual:@"/private/var/tmp/Target"])return 4;
    return 0;
}}
''')
            subprocess.run(['xcrun','clang','-fobjc-arc','-fblocks','-Wall','-Wextra','-Werror',
                '-framework','Foundation','-I',str(SOURCE/'compat'),str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
