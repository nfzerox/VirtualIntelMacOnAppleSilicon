#define main GuestMachoMain
#include "../tools/guest-macho.m"
#undef main
static NSMutableData *Fixture(BOOL signedImage) {
    NSMutableData *data=[NSMutableData dataWithLength:0x1110];
    struct mach_header_64 h={.magic=MH_MAGIC_64,.cputype=CPU_TYPE_X86_64,.cpusubtype=3,.filetype=MH_DYLIB,
        .ncmds=signedImage?4:3,.sizeofcmds=2*sizeof(struct segment_command_64)+sizeof(struct symtab_command)+(signedImage?sizeof(struct linkedit_data_command):0)};
    memcpy(data.mutableBytes,&h,sizeof(h));NSUInteger p=sizeof(h);
    struct segment_command_64 text={.cmd=LC_SEGMENT_64,.cmdsize=sizeof(text),.fileoff=0,.filesize=0x1000,.vmsize=0x1000};strcpy(text.segname,"__TEXT");
    memcpy((uint8_t *)data.mutableBytes+p,&text,sizeof(text));p+=sizeof(text);
    struct segment_command_64 linkedit={.cmd=LC_SEGMENT_64,.cmdsize=sizeof(linkedit),.fileoff=0x1000,.filesize=0x110,.vmaddr=0x1000,.vmsize=0x1000};strcpy(linkedit.segname,"__LINKEDIT");
    memcpy((uint8_t *)data.mutableBytes+p,&linkedit,sizeof(linkedit));p+=sizeof(linkedit);
    struct symtab_command symbols={.cmd=LC_SYMTAB,.cmdsize=sizeof(symbols),.symoff=0x1000,.nsyms=0,.stroff=0x1000,.strsize=0x20};
    memcpy((uint8_t *)data.mutableBytes+p,&symbols,sizeof(symbols));p+=sizeof(symbols);
    memset((uint8_t *)data.mutableBytes+0x1000,0x53,0x20);
    if(signedImage){struct linkedit_data_command signature={.cmd=LC_CODE_SIGNATURE,.cmdsize=sizeof(signature),.dataoff=0x1100,.datasize=16};
        memcpy((uint8_t *)data.mutableBytes+p,&signature,sizeof(signature));memset((uint8_t *)data.mutableBytes+0x1100,0x43,16);}
    return data;
}
int main(void) {@autoreleasepool {
    NSData *signedImage=CompactLinkedit(Fixture(YES));Check(signedImage.length==0x1030,@"Signed padding was not removed");
    Check([CompactLinkedit(signedImage) isEqual:signedImage],@"Canonical signed image changed on repeat");
    Check(CompactLinkedit(Fixture(NO)).length==0x1020,@"Unsigned signature allocation was not removed");
    NSMutableData *bad=Fixture(YES);((uint8_t *)bad.mutableBytes)[0x1080]=1;BOOL rejected=NO;
    @try{CompactLinkedit(bad);}@catch(NSException *e){rejected=YES;}Check(rejected,@"Nonzero padding was discarded");
    bad=Fixture(YES);struct load_command unknown={.cmd=0xffff,.cmdsize=16};
    memcpy((uint8_t *)bad.mutableBytes+sizeof(struct mach_header_64)+2*sizeof(struct segment_command_64)+sizeof(struct symtab_command),&unknown,sizeof(unknown));
    rejected=NO;@try{CompactLinkedit(bad);}@catch(NSException *e){rejected=YES;}Check(rejected,@"Unknown metadata was accepted");
    NSMutableData *fat=[NSMutableData dataWithLength:0x4000+0x1000];
    OSWriteBigInt32(fat.mutableBytes,0,0xcafebabe);OSWriteBigInt32(fat.mutableBytes,4,2);
    OSWriteBigInt32(fat.mutableBytes,8,CPU_TYPE_X86_64);OSWriteBigInt32(fat.mutableBytes,12,3);OSWriteBigInt32(fat.mutableBytes,16,0x1000);OSWriteBigInt32(fat.mutableBytes,20,0x1110);OSWriteBigInt32(fat.mutableBytes,24,12);
    OSWriteBigInt32(fat.mutableBytes,28,CPU_TYPE_I386);OSWriteBigInt32(fat.mutableBytes,32,3);OSWriteBigInt32(fat.mutableBytes,36,0x4000);OSWriteBigInt32(fat.mutableBytes,40,0x1000);OSWriteBigInt32(fat.mutableBytes,44,12);
    NSData *thin=Fixture(YES);memcpy((uint8_t *)fat.mutableBytes+0x1000,thin.bytes,thin.length);
    struct mach_header native32={.magic=MH_MAGIC,.cputype=CPU_TYPE_I386,.cpusubtype=3,.filetype=MH_DYLIB};
    memset((uint8_t *)fat.mutableBytes+0x4000,0x37,0x1000);memcpy((uint8_t *)fat.mutableBytes+0x4000,&native32,sizeof(native32));
    NSData *output=CompactLinkedit(fat);BOOL preserved=NO;
    for(NSDictionary *slice in Slices(output))if([slice[@"cpu"] intValue]==CPU_TYPE_I386)
        preserved=[[output subdataWithRange:NSMakeRange([slice[@"offset"] unsignedIntegerValue],[slice[@"size"] unsignedIntegerValue])] isEqual:[fat subdataWithRange:NSMakeRange(0x4000,0x1000)]];
    Check(preserved,@"Native i386 bytes changed");
    puts("PASS: signed/unsigned LINKEDIT compaction, canonical repeat, nonzero/unknown rejection, i386 preservation");return 0;
}}
