"""The banded finalizer must behave like raw bytes without allocating capacity."""
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest

SOURCE=Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform=='darwin','Native Objective-C disk backend')
class DiskImageIOTests(unittest.TestCase):
    def test_missing_short_cross_band_eviction_and_sync(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);image=root/'Image.sparsebundle';(image/'bands').mkdir(parents=True)
            (image/'Info.plist').write_bytes(plistlib.dumps({'diskimage-bundle-type':'com.apple.diskimage.sparsebundle',
                'size':32*8192,'band-size':8192}))
            (image/'bands/0').write_bytes(b'a'*4000)
            code=root/'probe.m';binary=root/'probe'
            code.write_text(r'''
#import "VIMDiskImageIO.h"
int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=2)return 64;
    NSURL *url=[NSURL fileURLWithPath:@(argv[1])];NSError *error=nil;
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:url writable:YES error:&error];
    if(!image)return 1;
    uint8_t bytes[64];
    if(![image readAt:3990 bytes:bytes length:sizeof(bytes)])return 2;
    for(unsigned i=0;i<64;i++)if(bytes[i]!=(i<10?'a':0))return 3;
    if(![image writeAt:8190 bytes:"cross-band" length:10])return 4;
    // More dirty bands than cache slots: flush must include evicted handles.
    for(uint32_t i=2;i<24;i++)if(![image writeAt:(uint64_t)i*8192 bytes:&i length:4])return 5;
    if(![image synchronize])return 6;
    VIMDiskImageIO *read=[[VIMDiskImageIO alloc] initWithURL:url writable:NO error:&error];
    if(!read||![read readAt:8190 bytes:bytes length:10]||memcmp(bytes,"cross-band",10))return 7;
    for(uint32_t i=2;i<24;i++) {uint32_t value=0;
        if(![read readAt:(uint64_t)i*8192 bytes:&value length:4]||value!=i)return 8;}
    if(![read readAt:30*8192 bytes:bytes length:64])return 9;
    for(unsigned i=0;i<64;i++)if(bytes[i])return 10;
    if([image writeAt:image.size-1 bytes:bytes length:2])return 11;
    if([read writeAt:0 bytes:bytes length:1])return 12;
    return 0;
}}
''')
            subprocess.run(['xcrun','clang','-fobjc-arc','-O2','-Wall','-Wextra','-Werror','-framework','Foundation',
                '-I',str(SOURCE/'tools/HostInstall'),str(code),str(SOURCE/'tools/HostInstall/VIMDiskImageIO.m'),'-o',str(binary)],check=True)
            subprocess.run([str(binary),str(image)],check=True)
            self.assertLess(sum(p.stat().st_blocks*512 for p in (image/'bands').iterdir()),1024*1024)

if __name__=='__main__':unittest.main()
