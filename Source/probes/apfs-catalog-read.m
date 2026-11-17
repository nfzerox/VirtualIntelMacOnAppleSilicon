// Read-only component diagnostic for detached host-install images.
// Include the production reader so stage failures can be isolated without
// mounting, re-extracting an installer, or making any filesystem changes.
#import "../tools/HostInstall/VIMAPFSFirmlinks.m"
#import "../tools/HostInstall/VIMHFSCatalog.h"

@interface FixtureImage : NSObject
@property NSMutableData *bytes;
@end
@implementation FixtureImage
- (BOOL)readAt:(uint64_t)offset bytes:(void *)out length:(size_t)length {
    if(offset>self.bytes.length||length>self.bytes.length-offset)return NO;
    memcpy(out,(const uint8_t *)self.bytes.bytes+offset,length);return YES;
}
@end
static void Fixture16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void FixtureChecksum(uint8_t *p){Set64(p,Checksum(p));}
static int SelfTest(void) {
    uint8_t attribute[52]={1};uint16_t form;size_t length;
    for(unsigned declared=0;declared<=UINT16_MAX;declared+=UINT16_MAX) {
        Fixture16(attribute+2,(uint16_t)declared);
        NSCAssert(AttributeLayout(attribute,sizeof(attribute),&form,&length)&&form==1&&length==48,
            @"Stream attributes must ignore xdata_len");
    }
    NSCAssert(!AttributeLayout(attribute,51,&form,&length),@"A stream descriptor must be complete");
    attribute[0]=2;Fixture16(attribute+2,16);
    NSCAssert(AttributeLayout(attribute,20,&form,&length)&&form==2&&length==16,@"Embedded attributes retain their own length");
    NSCAssert(!AttributeLayout(attribute,19,&form,&length),@"Truncated embedded data must fail");
    attribute[0]=3;
    NSCAssert(!AttributeLayout(attribute,20,&form,&length),@"Mutually exclusive storage forms cannot be combined");

    FixtureImage *image=[FixtureImage new];image.bytes=[NSMutableData dataWithLength:4*Block];
    uint8_t *bytes=image.bytes.mutableBytes,*node=bytes+Block,*header=bytes+2*Block;
    Set64(node+8,1);Set64(node+16,1);Set32(node+24,0x40000002);Set32(node+28,14);
    Fixture16(node+32,3);Set32(node+36,1);Fixture16(node+42,8);
    Fixture16(node+58,16);Fixture16(node+60,24);Fixture16(node+62,24);
    Set64(node+64,(UINT64_C(8)<<60)|9);
    uint8_t *extent=node+Block-40-24;
    Set64(extent,Block);Set64(extent+8,2);FixtureChecksum(node);
    memcpy(header,"fpmc",4);Set32(header+4,3);
    Container c={(VIMDiskImageIO *)image,0,4};
    NSDictionary *owners=@{@9:@[@2,@3]};NSMutableDictionary *types=[NSMutableDictionary dictionary];
    NSCAssert(ReadCompressionHeaders(c,1,nil,owners,types)&&[types[@2] isEqual:@3]&&[types[@3] isEqual:@3],
        @"One shared compression stream supplies both file headers");
    header[0]='x';
    NSCAssert(!ReadCompressionHeaders(c,1,nil,owners,types),@"A bad compression header must fail");
    header[0]='f';Set64(extent+8,4);FixtureChecksum(node);
    NSCAssert(!ReadCompressionHeaders(c,1,nil,owners,types),@"Out-of-container extents must fail");
    Set64(extent+8,2);Set64(node+72,Block);FixtureChecksum(node);
    NSCAssert(!ReadCompressionHeaders(c,1,nil,owners,types),@"A nonzero file offset cannot supply the header");
    puts("PASS: embedded/stream xattrs, ignored stream length, shared header, missing/truncated/invalid data");
    return 0;
}

int main(int argc,char **argv) {@autoreleasepool {
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc==2&&!strcmp(argv[1],"--self-test"))return SelfTest();
    if(argc!=2)return 64;
    NSURL *raw=[NSURL fileURLWithPath:@(argv[1])];NSError *error=nil;
    uint64_t offset=0,size=0;
    if(!VIMGetAPFSPartition(raw,&offset,&size,&error)) {
        fprintf(stderr,"partition: %s\n",error.localizedDescription.UTF8String);return 1;
    }
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:NO error:&error];
    printf("image=%llu partition=%llu+%llu\n",image.size,offset,size);
    if(!image)return 1;
    Container c={image,offset,size/Block};uint8_t nx[Block];
    if(!ReadActiveSuperblock(c,nx)){fputs("active container: FAILED\n",stderr);return 1;}
    printf("active container xid=%llu blocks=%llu omap=%llu\n",U64(nx+16),U64(nx+40),U64(nx+160));
    ResolveObject resolve=ReadObjectMap(c,U64(nx+160),U64(nx+16));
    if(!resolve){fputs("container object map: FAILED\n",stderr);return 1;}
    unsigned systems=0;
    for(unsigned i=0;i<100;i++) {
        uint64_t oid=U64(nx+184+i*8);if(!oid)continue;
        uint8_t volume[Block];uint64_t address=resolve(oid);
        if(!ReadObject(c,address,13,volume)){fprintf(stderr,"volume[%u] oid=%llu address=%llu: FAILED\n",i,oid,address);return 1;}
        NSUUID *uuid=[[NSUUID alloc] initWithUUIDBytes:volume+240];
        unsigned role=U16(volume+0x3c4);
        printf("volume[%u] %s role=0x%x xid=%llu flags=0x%llx fs_flags=0x%llx root=%llu omap=%llu\n",
            i,uuid.UUIDString.UTF8String,role,U64(volume+16),U64(volume+56),U64(volume+216),U64(volume+136),U64(volume+128));
        if(role!=VIMAPFSSystemRole)continue;
        systems++;
        ResolveObject objects=ReadUnsealedVolume(c,uuid,role,volume);
        if(!objects){fputs("unsealed System object map: FAILED\n",stderr);return 1;}
        __block unsigned streams=0,mismatches=0;
        BOOL walked=WalkTree(c,U64(volume+136),objects,14,NO,
            ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v) {
                if(!leaf||k<8||(U64(key)>>60)!=Xattr||v<4)return YES;
                uint16_t form=U16(value)&3,length=U16(value+2);
                if(v!=4U+length)mismatches++;
                if(form==1&&streams++<5) {
                    const char decmpfs[]="com.apple.decmpfs";
                    BOOL compressed=k==10+sizeof(decmpfs)&&!memcmp(key+10,decmpfs,sizeof(decmpfs));
                    printf("stream xattr compressed=%d flags=0x%x declared=%u actual=%zu oid=%llu logical=%llu\n",
                        compressed,U16(value),length,v,v>=12?U64(value+4):0,v>=20?U64(value+12):0);
                }
                return YES;
            },nil);
        printf("raw tree walked=%d stream attributes=%u length mismatches=%u\n",walked,streams,mismatches);
        NSDictionary *catalog=BigSurCatalog(c,uuid,NO);
        if(!catalog){fputs("System catalog: FAILED\n",stderr);return 1;}
        printf("System catalog: %lu paths\n",(unsigned long)[catalog[@"Paths"] count]);
        NSDictionary *canonical=BigSurCatalog(c,uuid,YES);
        if(!canonical){fputs("Canonical System catalog: FAILED\n",stderr);return 1;}
        printf("Canonical System catalog: %lu paths, %lu files with external attributes\n",
            (unsigned long)[canonical[@"Paths"] count],(unsigned long)[canonical[@"Streams"] count]);
    }
    return systems?0:1;
}}
