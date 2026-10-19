#import "VIMFirmwareBoot.h"
#include <assert.h>

static void Put16(NSMutableData *data,NSUInteger offset,uint16_t value) {
    uint8_t *b=data.mutableBytes;b[offset]=value;b[offset+1]=value>>8;
}
static void Put32(NSMutableData *data,NSUInteger offset,uint32_t value) {
    Put16(data,offset,value);Put16(data,offset+2,value>>16);
}
static NSMutableData *Fixture(BOOL auth,BOOL installer,uint8_t state,BOOL apple) {
    NSMutableData *data=[NSMutableData dataWithLength:4096];uint8_t *b=data.mutableBytes;
    memcpy(b+40,"_FVH",4);b[55]=2;Put32(data,32,4096);Put16(data,48,72);
    const uint8_t normal[]={0x16,0x36,0xcf,0xdd,0x75,0x32,0x64,0x41,0x98,0xb6,0xfe,0x85,0x70,0x7f,0xfe,0x7d};
    const uint8_t authenticated[]={0x78,0x2c,0xf3,0xaa,0x7b,0x94,0x9a,0x43,0xa1,0x80,0x2e,0x14,0x4e,0xc3,0x77,0x92};
    memcpy(b+72,auth?authenticated:normal,16);Put32(data,88,4096-72);b[92]=0x5a;b[93]=0xfe;
    NSUInteger v=100,size=auth?60:32;
    NSData *name=[@"efi-boot-device\0" dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
    NSData *value=[NSPropertyListSerialization dataWithPropertyList:@[@{@"IOEFIDevicePathType":@"MediaFilePath",@"Path":installer?@"\\UUID\\com.apple.installer\\boot.efi":@"\\UUID\\System\\Library\\CoreServices\\boot.efi"}] format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    Put16(data,v,0x55aa);b[v+2]=state;Put32(data,v+4,7);
    Put32(data,v+(auth?36:8),(uint32_t)name.length);Put32(data,v+(auth?40:12),(uint32_t)value.length);
    const uint8_t guid[]={0x10,0x61,0x43,0x7c,0x2a,0xab,0xbb,0x4b,0xa8,0x80,0xfe,0x41,0x99,0x5c,0x9f,0x82};
    if(apple)memcpy(b+v+(auth?44:16),guid,16);
    memcpy(b+v+size,name.bytes,name.length);memcpy(b+v+size+name.length,value.bytes,value.length);
    uint16_t sum=0;for(NSUInteger i=0;i<72;i+=2)sum+=(uint16_t)b[i]|((uint16_t)b[i+1]<<8);
    Put16(data,50,(uint16_t)-sum);return data;
}
int main(void) {@autoreleasepool {
    for(NSNumber *auth in @[@NO,@YES]) {
        NSData *installer=Fixture(auth.boolValue,YES,0x3f,YES);
        assert(VIMFirmwareBootTargetFromData(installer)==VIMFirmwareBootInstaller);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth.boolValue,NO,0x3f,YES))==VIMFirmwareBootInstalledSystem);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth.boolValue,YES,0x3e,YES))==VIMFirmwareBootInstaller);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth.boolValue,YES,0x3d,YES))==VIMFirmwareBootUnknown);
        assert(VIMFirmwareBootTargetFromData(Fixture(auth.boolValue,YES,0x3f,NO))==VIMFirmwareBootUnknown);
        for(NSUInteger n=0;n<installer.length;n+=31)
            assert(VIMFirmwareBootTargetFromData([installer subdataWithRange:NSMakeRange(0,n)])==VIMFirmwareBootUnknown);
        NSMutableData *bad=[installer mutableCopy];Put32(bad,100+(auth.boolValue?36:8),UINT32_MAX);
        assert(VIMFirmwareBootTargetFromData(bad)==VIMFirmwareBootUnknown);
    }
    puts("Firmware boot format, state, GUID and truncation checks passed.");return 0;
}}
