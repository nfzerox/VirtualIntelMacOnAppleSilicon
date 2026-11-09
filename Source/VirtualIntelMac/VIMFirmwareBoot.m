#import "VIMFirmwareBoot.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// EDK II 2970e5699ba6267f3384ffab20f96647578aebc8:
// MdeModulePkg/Include/Guid/VariableFormat.h and RuntimeDxe/VariableParsing.c.
// These are serialized-format offsets, not guest-build addresses or patches.
static uint16_t LE16(const uint8_t *p) {return p[0]|((uint16_t)p[1]<<8);}
static uint32_t LE32(const uint8_t *p) {return LE16(p)|((uint32_t)LE16(p+2)<<16);}
static uint64_t LE64(const uint8_t *p) {return LE32(p)|((uint64_t)LE32(p+4)<<32);}

static VIMFirmwareBootTarget TargetFromPath(NSString *path) {
    path=[path.lowercaseString stringByReplacingOccurrencesOfString:@"/" withString:@"\\"];
    if(![path hasSuffix:@"\\boot.efi"])return VIMFirmwareBootUnknown;
    if([path hasSuffix:@"\\com.apple.installer\\boot.efi"]||[path containsString:@"\\macos install data\\"]||
       [path containsString:@"\\os x install data\\"])return VIMFirmwareBootInstaller;
    if([path hasSuffix:@"\\system\\library\\coreservices\\boot.efi"])return VIMFirmwareBootInstalledSystem;
    return VIMFirmwareBootUnknown;
}

static VIMFirmwareBootTarget TargetFromPlist(NSData *value) {
    const uint8_t *bytes=value.bytes;NSUInteger length=value.length;
    while(length&&bytes[length-1]==0)length--;
    id plist=[NSPropertyListSerialization propertyListWithData:[value subdataWithRange:NSMakeRange(0,length)] options:NSPropertyListImmutable format:NULL error:NULL];
    if(![plist isKindOfClass:NSArray.class])return VIMFirmwareBootUnknown;
    for(id entry in plist) {
        if(![entry isKindOfClass:NSDictionary.class]||![entry[@"IOEFIDevicePathType"] isEqual:@"MediaFilePath"]||![entry[@"Path"] isKindOfClass:NSString.class])continue;
        VIMFirmwareBootTarget target=TargetFromPath(entry[@"Path"]);
        if(target!=VIMFirmwareBootUnknown)return target;
    }
    return VIMFirmwareBootUnknown;
}

static VIMFirmwareBootTarget TargetFromDevicePath(NSData *value) {
    const uint8_t *b=value.bytes;NSUInteger end=value.length;
    NSMutableString *path=[NSMutableString string];
    for(NSUInteger offset=0;end-offset>=4;) {
        const uint8_t *node=b+offset;
        NSUInteger length=LE16(node+2);
        if(length<4||length>end-offset)return VIMFirmwareBootUnknown;
        if(node[0]==0x7f) {
            // A single path must end with End Entire, not End Instance.
            return node[1]==0xff&&length==4&&length==end-offset?
                TargetFromPath(path):VIMFirmwareBootUnknown;
        }
        if(node[0]==4&&node[1]==4) { // MEDIA_FILEPATH_DP
            if(length<8||length%2||LE16(node+length-2))return VIMFirmwareBootUnknown;
            for(NSUInteger i=4;i<length-2;i+=2)if(!LE16(node+i))return VIMFirmwareBootUnknown;
            NSString *part=[[NSString alloc] initWithBytes:node+4 length:length-6 encoding:NSUTF16LittleEndianStringEncoding];
            if(!part)return VIMFirmwareBootUnknown;
            [path appendString:part];
        } else if(path.length)return VIMFirmwareBootUnknown;
        offset+=length;
    }
    return VIMFirmwareBootUnknown;
}

static VIMFirmwareBootTarget TargetFromSelection(NSData *plist,NSData *devicePath) {
    VIMFirmwareBootTarget readable=plist?TargetFromPlist(plist):VIMFirmwareBootUnknown;
    VIMFirmwareBootTarget binary=devicePath?TargetFromDevicePath(devicePath):VIMFirmwareBootUnknown;
    if(readable!=VIMFirmwareBootUnknown&&binary!=VIMFirmwareBootUnknown&&readable!=binary)
        return VIMFirmwareBootUnknown;
    return readable!=VIMFirmwareBootUnknown?readable:binary;
}

VIMFirmwareBootTarget VIMFirmwareBootTargetFromData(NSData *data) {
    const uint8_t *b=data.bytes;NSUInteger n=data.length;
    if(n<100||n>4*1024*1024||memcmp(b+40,"_FVH",4)||b[55]!=2)return VIMFirmwareBootUnknown;
    uint64_t volume=LE64(b+32);NSUInteger header=LE16(b+48);
    if(volume>n||header<56||header>volume||header%2)return VIMFirmwareBootUnknown;
    uint16_t sum=0;for(NSUInteger i=0;i<header;i+=2)sum+=LE16(b+i);
    if(sum||volume-header<28)return VIMFirmwareBootUnknown;
    const uint8_t normal[]={0x16,0x36,0xcf,0xdd,0x75,0x32,0x64,0x41,0x98,0xb6,0xfe,0x85,0x70,0x7f,0xfe,0x7d};
    const uint8_t authenticated[]={0x78,0x2c,0xf3,0xaa,0x7b,0x94,0x9a,0x43,0xa1,0x80,0x2e,0x14,0x4e,0xc3,0x77,0x92};
    BOOL auth=!memcmp(b+header,authenticated,16);
    if(!auth&&memcmp(b+header,normal,16))return VIMFirmwareBootUnknown;
    NSUInteger store=LE32(b+header+16);
    if(store<28||store>volume-header||b[header+20]!=0x5a||b[header+21]!=0xfe)return VIMFirmwareBootUnknown;
    NSUInteger end=header+store,offset=(header+28+3)&~(NSUInteger)3;
    NSUInteger size=auth?60:32,nameOffset=auth?36:8,dataOffset=auth?40:12,guidOffset=auth?44:16;
    const uint8_t apple[]={0x10,0x61,0x43,0x7c,0x2a,0xab,0xbb,0x4b,0xa8,0x80,0xfe,0x41,0x99,0x5c,0x9f,0x82};
    NSArray *names=@[@"efi-boot-next",@"efi-boot-next-data",@"efi-boot-device",@"efi-boot-device-data"];
    NSData *active[4]={nil},*transition[4]={nil};
    while(offset<=end&&end-offset>=size) {
        const uint8_t *v=b+offset;
        if(LE16(v)!=0x55aa)break;
        NSUInteger nameSize=LE32(v+nameOffset),dataSize=LE32(v+dataOffset);
        if(nameSize<2||nameSize%2||nameSize>16384||nameSize>end-offset-size||dataSize>end-offset-size-nameSize)return VIMFirmwareBootUnknown;
        if((v[2]==0x3f||v[2]==0x3e)&&!memcmp(v+guidOffset,apple,16)&&LE16(v+size+nameSize-2)==0) {
            NSString *name=[[NSString alloc] initWithBytes:v+size length:nameSize-2 encoding:NSUTF16LittleEndianStringEncoding];
            NSUInteger index=name?[names indexOfObject:name]:NSNotFound;
            if(index!=NSNotFound) {
                NSData *value=[NSData dataWithBytes:v+size+nameSize length:dataSize];
                if(v[2]==0x3f) {
                    if(!active[index])active[index]=value;
                } else transition[index]=value; // Only used if there is no added value.
            }
        }
        offset=(offset+size+nameSize+dataSize+3)&~(NSUInteger)3;
    }
    for(NSUInteger i=0;i<4;i++)if(!active[i])active[i]=transition[i];
    // Apple's one-shot choice wins even if it is not a loader we recognize.
    // Never infer an update from a backup GUID or an arbitrary BootOrder entry.
    if(active[0]||active[1])return TargetFromSelection(active[0],active[1]);
    return TargetFromSelection(active[2],active[3]);
}

VIMFirmwareBootTarget VIMFirmwareBootTargetAtURL(NSURL *url) {
    int fd=url.isFileURL?open(url.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC):-1;
    struct stat st;
    if(fd<0)return VIMFirmwareBootUnknown;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<100||st.st_size>4*1024*1024){close(fd);return VIMFirmwareBootUnknown;}
    NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)st.st_size];NSUInteger total=0;
    while(total<data.length) {
        ssize_t got=pread(fd,(uint8_t *)data.mutableBytes+total,data.length-total,total);
        if(got<=0){close(fd);return VIMFirmwareBootUnknown;}total+=(NSUInteger)got;
    }
    close(fd);return VIMFirmwareBootTargetFromData(data);
}

// Same EDK II store walk as VIMFirmwareBootTargetFromData, for one name.
static BOOL AppleVariablePresent(NSData *data,NSString *wanted) {
    const uint8_t *b=data.bytes;NSUInteger n=data.length;
    if(n<100||n>4*1024*1024||memcmp(b+40,"_FVH",4)||b[55]!=2)return NO;
    uint64_t volume=LE64(b+32);NSUInteger header=LE16(b+48);
    if(volume>n||header<56||header>volume||header%2)return NO;
    const uint8_t authenticated[]={0x78,0x2c,0xf3,0xaa,0x7b,0x94,0x9a,0x43,0xa1,0x80,0x2e,0x14,0x4e,0xc3,0x77,0x92};
    BOOL auth=!memcmp(b+header,authenticated,16);
    NSUInteger store=LE32(b+header+16);
    if(store<28||store>volume-header)return NO;
    NSUInteger end=header+store,offset=(header+28+3)&~(NSUInteger)3;
    NSUInteger size=auth?60:32,nameOffset=auth?36:8,dataOffset=auth?40:12,guidOffset=auth?44:16;
    // Apple boot (7C436110-...) and Apple vendor (4D1EDE05-...) GUIDs.
    const uint8_t apple[]={0x10,0x61,0x43,0x7c,0x2a,0xab,0xbb,0x4b,0xa8,0x80,0xfe,0x41,0x99,0x5c,0x9f,0x82};
    const uint8_t vendor[]={0x05,0xde,0x1e,0x4d,0xc7,0x38,0x6a,0x4a,0x9c,0xc6,0x4b,0xcc,0xa8,0xb3,0x8c,0x14};
    while(offset<=end&&end-offset>=size) {
        const uint8_t *v=b+offset;
        if(LE16(v)!=0x55aa)break;
        NSUInteger nameSize=LE32(v+nameOffset),dataSize=LE32(v+dataOffset);
        if(nameSize<2||nameSize%2||nameSize>16384||nameSize>end-offset-size||dataSize>end-offset-size-nameSize)return NO;
        if(v[2]==0x3f&&(!memcmp(v+guidOffset,apple,16)||!memcmp(v+guidOffset,vendor,16))&&dataSize>0) {
            NSString *name=[[NSString alloc] initWithBytes:v+size length:nameSize-2 encoding:NSUTF16LittleEndianStringEncoding];
            if([name isEqualToString:wanted])return YES;
        }
        offset=(offset+size+nameSize+dataSize+3)&~(NSUInteger)3;
    }
    return NO;
}

BOOL VIMFirmwareHasStagedInstallAtURL(NSURL *url) {
    NSData *data=url.isFileURL?[NSData dataWithContentsOfURL:url options:NSDataReadingUncached error:NULL]:nil;
    // 10.14/10.15 startosinstall writes install-product-url; Big Sur's
    // InstallAssistant records its staged phases (IASCurrentInstallPhase)
    // and boots the staged installer from a Preboot folder instead.
    return data.length<=4*1024*1024&&(AppleVariablePresent(data,@"install-product-url")||
        AppleVariablePresent(data,@"IASCurrentInstallPhase"));
}
