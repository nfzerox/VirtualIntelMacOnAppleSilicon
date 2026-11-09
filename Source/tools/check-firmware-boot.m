#import "VIMFirmwareBoot.h"
#include <assert.h>

static void Put16(NSMutableData *data,NSUInteger offset,uint16_t value) {
    uint8_t *b=data.mutableBytes;b[offset]=value;b[offset+1]=value>>8;
}
static void Put32(NSMutableData *data,NSUInteger offset,uint32_t value) {
    Put16(data,offset,value);Put16(data,offset+2,value>>16);
}
static NSString *const Installer=@"/UUID/com.apple.installer/boot.efi";
static NSString *const Installed=@"/UUID/System/Library/CoreServices/boot.efi";

static NSData *PathPlist(NSString *path) {
    return [NSPropertyListSerialization dataWithPropertyList:@[@{@"IOEFIDevicePathType":@"MediaFilePath",@"Path":path}]
        format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
}

static NSMutableData *DevicePath(NSArray<NSString *> *parts) {
    const uint8_t disk[]={4,1,4,0},end[]={0x7f,0xff,4,0};
    NSMutableData *data=[NSMutableData dataWithBytes:disk length:sizeof(disk)];
    for(NSString *part in parts) {
        NSData *path=[[part stringByAppendingString:@"\0"] dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
        NSUInteger offset=data.length;
        [data increaseLengthBy:4];
        uint8_t *b=data.mutableBytes;b[offset]=4;b[offset+1]=4;
        Put16(data,offset+2,(uint16_t)(4+path.length));
        [data appendData:path];
    }
    [data appendBytes:end length:sizeof(end)];
    return data;
}

static NSMutableData *Store(BOOL auth) {
    NSMutableData *data=[NSMutableData dataWithLength:4096];uint8_t *b=data.mutableBytes;
    memcpy(b+40,"_FVH",4);b[55]=2;Put32(data,32,4096);Put16(data,48,72);
    const uint8_t normal[]={0x16,0x36,0xcf,0xdd,0x75,0x32,0x64,0x41,0x98,0xb6,0xfe,0x85,0x70,0x7f,0xfe,0x7d};
    const uint8_t authenticated[]={0x78,0x2c,0xf3,0xaa,0x7b,0x94,0x9a,0x43,0xa1,0x80,0x2e,0x14,0x4e,0xc3,0x77,0x92};
    memcpy(b+72,auth?authenticated:normal,16);Put32(data,88,4096-72);b[92]=0x5a;b[93]=0xfe;
    uint16_t sum=0;for(NSUInteger i=0;i<72;i+=2)sum+=(uint16_t)b[i]|((uint16_t)b[i+1]<<8);
    Put16(data,50,(uint16_t)-sum);return data;
}

static NSUInteger Add(NSMutableData *data,NSUInteger v,BOOL auth,NSString *key,NSData *value,uint8_t state,BOOL apple) {
    uint8_t *b=data.mutableBytes;NSUInteger size=auth?60:32;
    NSData *name=[[key stringByAppendingString:@"\0"] dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
    assert(v+size+name.length+value.length<=data.length);
    Put16(data,v,0x55aa);b[v+2]=state;Put32(data,v+4,7);
    Put32(data,v+(auth?36:8),(uint32_t)name.length);Put32(data,v+(auth?40:12),(uint32_t)value.length);
    const uint8_t guid[]={0x10,0x61,0x43,0x7c,0x2a,0xab,0xbb,0x4b,0xa8,0x80,0xfe,0x41,0x99,0x5c,0x9f,0x82};
    if(apple)memcpy(b+v+(auth?44:16),guid,16);
    memcpy(b+v+size,name.bytes,name.length);memcpy(b+v+size+name.length,value.bytes,value.length);
    return (v+size+name.length+value.length+3)&~(NSUInteger)3;
}

static NSMutableData *Fixture(BOOL auth,BOOL installer,uint8_t state,BOOL apple) {
    NSMutableData *data=Store(auth);
    Add(data,100,auth,@"efi-boot-device",PathPlist(installer?Installer:Installed),state,apple);
    return data;
}
int main(void) {@autoreleasepool {
    for(NSNumber *variant in @[@NO,@YES]) {
        BOOL auth=variant.boolValue;
        NSData *installer=Fixture(auth,YES,0x3f,YES);
        assert(VIMFirmwareBootTargetFromData(installer)==VIMFirmwareBootInstaller);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth,NO,0x3f,YES))==VIMFirmwareBootInstalledSystem);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth,YES,0x3e,YES))==VIMFirmwareBootInstaller);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth,YES,0x3d,YES))==VIMFirmwareBootUnknown);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth,YES,0x3f,NO))==VIMFirmwareBootUnknown);
        for(NSUInteger n=0;n<installer.length;n+=31)
            assert(VIMFirmwareBootTargetFromData([installer subdataWithRange:NSMakeRange(0,n)])==VIMFirmwareBootUnknown);
        NSMutableData *bad=[installer mutableCopy];Put32(bad,100+(auth?36:8),UINT32_MAX);
        assert(VIMFirmwareBootTargetFromData(bad)==VIMFirmwareBootUnknown);

        NSMutableData *next=Store(auth);
        NSUInteger v=Add(next,100,auth,@"efi-boot-device",PathPlist(Installed),0x3f,YES);
        v=Add(next,v,auth,@"efi-boot-next",PathPlist(Installed),0x3e,YES);
        v=Add(next,v,auth,@"efi-boot-next",PathPlist(Installer),0x3f,YES);
        assert(VIMFirmwareBootTargetFromData(next)==VIMFirmwareBootInstaller);
        Add(next,v,auth,@"efi-boot-next-data",DevicePath(@[Installed]),0x3f,YES);
        assert(VIMFirmwareBootTargetFromData(next)==VIMFirmwareBootUnknown);

        NSMutableData *binary=Store(auth);
        v=Add(binary,100,auth,@"efi-boot-device-data",DevicePath(@[Installed]),0x3f,YES);
        Add(binary,v,auth,@"efi-boot-next-data",DevicePath(@[@"/macOS Install Data",@"/Locked Files/Boot Files/boot.efi"]),0x3d,YES);
        assert(VIMFirmwareBootTargetFromData(binary)==VIMFirmwareBootInstalledSystem);
        Add(binary,v,auth,@"efi-boot-next-data",DevicePath(@[@"/macOS Install Data",@"/Locked Files/Boot Files/boot.efi"]),0x3f,YES);
        assert(VIMFirmwareBootTargetFromData(binary)==VIMFirmwareBootInstaller);

        NSMutableData *device=DevicePath(@[Installer]);
        NSUInteger last=device.length-4;
        ((uint8_t *)device.mutableBytes)[last+1]=1; // End Instance, not End Entire.
        Add(binary,v,auth,@"efi-boot-next-data",device,0x3f,YES);
        assert(VIMFirmwareBootTargetFromData(binary)==VIMFirmwareBootUnknown);
        Add(binary,v,auth,@"efi-boot-next-data",DevicePath(@[@"/macOS Install Data/firmware.efi"]),0x3f,YES);
        assert(VIMFirmwareBootTargetFromData(binary)==VIMFirmwareBootUnknown);
    }
    puts("Firmware boot format, one-shot precedence, state, GUID and truncation checks passed.");return 0;
}}
