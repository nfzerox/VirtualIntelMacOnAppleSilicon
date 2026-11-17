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
    // Same alignment defect as the inherited Mojave SkyLight: seven bytes
    // between streams were excluded from their declared sizes. The old signer
    // sums sizes but copies the full span, shifting subsequent binding bytes.
    NSMutableData *gapped=Fixture(YES);
    struct mach_header_64 *gh=gapped.mutableBytes;
    NSUInteger gc=sizeof(*gh)+gh->sizeofcmds;
    struct dyld_info_command info={.cmd=LC_DYLD_INFO,.cmdsize=sizeof(info),
        .rebase_off=0x1000,.rebase_size=7,.bind_off=0x1008,.bind_size=10,
        .export_off=0x1018,.export_size=8};
    memcpy((uint8_t *)gapped.mutableBytes+gc,&info,sizeof(info));gh->ncmds++;gh->sizeofcmds+=sizeof(info);
    struct symtab_command *gs=(void *)((uint8_t *)gapped.mutableBytes+sizeof(*gh)+2*sizeof(struct segment_command_64));
    gs->symoff=gs->stroff=0x1020;gs->strsize=0x20;
    uint8_t *gb=gapped.mutableBytes;memset(gb+0x1000,0,0x20);memset(gb+0x1020,0x53,0x20);
    const uint8_t rebase[]={0x11,0x22,0,0x51,0,0,0};memcpy(gb+0x1000,rebase,sizeof(rebase));
    const uint8_t bind[]={0x11,0x40,'_','x',0,0x51,0x72,0,0x90,0};memcpy(gb+0x1008,bind,sizeof(bind));
    NSData *normalized=CompactLinkedit(gapped);
    struct dyld_info_command fixed;memcpy(&fixed,(const uint8_t *)normalized.bytes+gc,sizeof(fixed));
    Check(fixed.rebase_off==info.rebase_off&&fixed.rebase_size==8&&fixed.bind_off==info.bind_off&&fixed.bind_size==16&&
        fixed.export_off==info.export_off&&fixed.export_size==info.export_size,@"Dyld padding was not represented without moving bytes");
    Check(memcmp((const uint8_t *)normalized.bytes+0x1000,gb+0x1000,0x40)==0,@"Live LINKEDIT bytes changed");
    Check([CompactLinkedit(normalized) isEqual:normalized],@"Dyld normalization is not idempotent");
    NSMutableData *alreadyTrimmed=[gapped mutableCopy];uint8_t *trimmed=alreadyTrimmed.mutableBytes;
    struct linkedit_data_command *ts=(void *)(trimmed+sizeof(*gh)+2*sizeof(struct segment_command_64)+sizeof(struct symtab_command));
    ts->dataoff=0x1040;memcpy(trimmed+0x1040,trimmed+0x1100,16);
    struct segment_command_64 *tl=(void *)(trimmed+sizeof(*gh)+sizeof(struct segment_command_64));tl->filesize=0x50;
    alreadyTrimmed.length=0x1050;
    NSData *sameLength=CompactLinkedit(alreadyTrimmed);memcpy(&fixed,(const uint8_t *)sameLength.bytes+gc,sizeof(fixed));
    Check(sameLength.length==alreadyTrimmed.length&&fixed.rebase_size==8&&fixed.bind_size==16,
        @"Already-trimmed LINKEDIT lost its padding-only normalization");
    gb[0x1007]=1;BOOL gapRejected=NO;
    @try{CompactLinkedit(gapped);}@catch(NSException *e){gapRejected=YES;}
    Check(gapRejected,@"Unrecognized nonzero stream gap was discarded");
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
    puts("PASS: signed/unsigned LINKEDIT compaction, dyld padding with/without resizing, canonical repeat, nonzero/unknown rejection, i386 preservation");return 0;
}}
