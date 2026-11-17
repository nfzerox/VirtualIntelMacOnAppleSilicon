"""Native media-driven NIC selection, without changing existing VM defaults."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class GuestNetworkPolicyTests(unittest.TestCase):
    def test_original_leopard_uses_its_supplied_driver_and_keeps_the_profile(self):
        source = Path(__file__).resolve().parents[1]
        program = r'''
#import "VIMGuestFamilies.h"
#import "VIMHostInstallSourcePolicy.h"
int main(void) { @autoreleasepool {
    NSDictionary *intel=@{@"CFBundleIdentifier":@"com.apple.driver.AppleIntel8254XEthernet",
        @"CFBundleVersion":@"2.0.1a2"};
    NSDictionary *rtl=@{@"CFBundleIdentifier":@"com.apple.driver.AppleRTL8139Ethernet",
        @"IOKitPersonalities":@{@"RTL8139 PCI Adapter":@{@"IOPCIMatch":@"0x813910ec 0x13001186 0x12111113"}}};
    if(![VIMHostInstallNetworkAdapter(@"leopard",intel,rtl) isEqual:@"rtl8139"])return 1;
    NSMutableDictionary *updated=[intel mutableCopy];updated[@"CFBundleVersion"]=@"2.0.5";
    if(VIMHostInstallNetworkAdapter(@"leopard",updated,rtl))return 2;
    if(VIMHostInstallNetworkAdapter(@"leopard",intel,@{}))return 3;
    if(VIMHostInstallNetworkAdapter(@"snowleopard",intel,rtl))return 4;
    if(![VIMNetworkDeviceModel(@"leopard",@{@"NetworkAdapter":@"rtl8139"}) isEqual:@"rtl8139"])return 5;
    for(NSString *guest in VIMSupportedGuests()) {
        NSString *original=VIMUsesVirtIONetwork(guest)?@"virtio-net-pci":@"e1000-82545em";
        if(![VIMNetworkDeviceModel(guest,@{}) isEqual:original])return 6;
        if(![VIMNetworkDeviceModel(guest,@{@"NetworkAdapter":@"unknown"}) isEqual:original])return 7;
        if(![guest isEqual:@"leopard"]&&
           ![VIMNetworkDeviceModel(guest,@{@"NetworkAdapter":@"rtl8139"}) isEqual:original])return 8;
    }
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix="vim-network-policy-") as directory:
            root = Path(directory)
            (root / "main.m").write_text(program)
            subprocess.run(("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                            "-arch", "arm64", "-arch", "x86_64", "-mmacosx-version-min=11.0",
                            "-I", source / "VirtualIntelMac", "-I", source / "compat",
                            root / "main.m", "-framework", "Foundation", "-o", root / "probe"), check=True)
            subprocess.run((root / "probe",), check=True)


if __name__ == "__main__":
    unittest.main()
