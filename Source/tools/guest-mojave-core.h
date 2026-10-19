// Guarded, address-independent Mojave CoreDisplay patch. Included by the native
// image tool; Require/Bounds/Hex are shared with the Catalina implementation.
#include <libkern/OSByteOrder.h>

@interface PVGCoreImage : NSObject
@property NSMutableData *data;
@property NSMutableArray<NSValue *> *segments;
@property struct section_64 text;
@property NSUInteger commandEnd;
@property uint64_t firstSection;
@property struct segment_command_64 textSegment;
@property NSUInteger sliceOffset;
@property NSMutableDictionary<NSString *,NSMutableSet<NSNumber *> *> *symbols;
- (NSUInteger)offset:(uint64_t)address size:(NSUInteger)size;
- (NSData *)read:(uint64_t)address size:(NSUInteger)size;
- (uint64_t)relative:(uint64_t)address displacement:(NSUInteger)displacement length:(NSUInteger)length;
- (NSArray<NSNumber *> *)matches:(NSString *)pattern start:(uint64_t)start end:(uint64_t)end;
- (void)writable:(uint64_t)address;
@end
@implementation PVGCoreImage
- (NSUInteger)offset:(uint64_t)address size:(NSUInteger)size {
    NSUInteger count=0,offset=0;
    for(NSValue *value in self.segments) {
        struct segment_command_64 s;[value getValue:&s size:sizeof(s)];
        if(address>=s.vmaddr&&Bounds(address-s.vmaddr,size,s.filesize)) {count++;offset=s.fileoff+address-s.vmaddr;}
    }
    Require(count==1&&Bounds(offset,size,self.data.length),@"Address is not uniquely file backed");return offset;
}
- (NSData *)read:(uint64_t)address size:(NSUInteger)size {return [self.data subdataWithRange:NSMakeRange([self offset:address size:size],size)];}
- (uint64_t)relative:(uint64_t)address displacement:(NSUInteger)displacement length:(NSUInteger)length {
    NSData *d=[self read:address+displacement size:4];int32_t delta=(int32_t)OSReadLittleInt32(d.bytes,0);
    __int128 target=(__int128)address+length+delta;
    Require(target>=0&&target<=UINT64_MAX,@"Relative address overflow");return (uint64_t)target;
}
- (NSArray<NSNumber *> *)matches:(NSString *)pattern start:(uint64_t)start end:(uint64_t)end {
    Require(end>=start,@"Invalid search bounds");NSData *region=[self read:start size:end-start];
    NSArray *tokens=[pattern componentsSeparatedByString:@" "];
    NSMutableData *needle=[NSMutableData data],*mask=[NSMutableData data];
    for(NSString *token in tokens)if(token.length) {
        uint8_t bit=[token isEqual:@"??"]?0:255,b=0;
        if(bit){NSData *d=Hex(token);Require(d.length==1,@"Invalid byte pattern");b=*(const uint8_t *)d.bytes;}
        [needle appendBytes:&b length:1];[mask appendBytes:&bit length:1];
    }
    Require(needle.length>0,@"Empty pattern");NSMutableArray *result=[NSMutableArray array];
    const uint8_t *bytes=region.bytes,*n=needle.bytes,*m=mask.bytes;
    for(NSUInteger i=0;Bounds(i,needle.length,region.length);i++) {
        BOOL match=YES;for(NSUInteger j=0;j<needle.length;j++)if(m[j]&&bytes[i+j]!=n[j]){match=NO;break;}
        if(match){[result addObject:@(start+i)];i+=needle.length-1;}
    }
    return result;
}
- (void)writable:(uint64_t)address {
    BOOL found=NO;for(NSValue *value in self.segments){struct segment_command_64 s;[value getValue:&s size:sizeof(s)];found|=address>=s.vmaddr&&address-s.vmaddr<s.vmsize&&(s.initprot&VM_PROT_WRITE);}
    Require(found,@"Expected writable GPU state");
}
@end

static uint64_t CoreOne(NSArray<NSNumber *> *values,NSString *label) {
    Require(values.count==1,[@"Unknown or ambiguous " stringByAppendingString:label]);return values[0].unsignedLongLongValue;
}
static void CoreRelative(NSMutableData *code,NSString *op,uint64_t cave,uint64_t target,NSData *suffix) {
    NSData *opcode=Hex(op);__int128 delta=(__int128)target-cave-code.length-opcode.length-4-suffix.length;
    Require(delta>=INT32_MIN&&delta<=INT32_MAX,@"Trampoline displacement out of range");
    int32_t value=(int32_t)delta;[code appendData:opcode];[code appendBytes:&value length:4];if(suffix)[code appendData:suffix];
}
static PVGCoreImage *CoreParse(NSData *original) {
    NSUInteger sliceOffset=0,sliceSize=original.length;
    Require(original.length>=4,@"Truncated image");uint32_t magic=OSReadBigInt32(original.bytes,0);
    if(magic==0xcafebabe||magic==0xcafebabf) {
        Require(original.length>=8,@"Truncated universal header");BOOL wide=magic==0xcafebabf;
        NSUInteger count=OSReadBigInt32(original.bytes,4),stride=wide?32:20,selected=0;
        Require(count>0&&count<=64&&Bounds(8,count*stride,original.length),@"Invalid universal header");
        NSMutableArray *ranges=[NSMutableArray array];
        for(NSUInteger i=0;i<count;i++) {
            NSUInteger p=8+i*stride;uint32_t cpu=OSReadBigInt32(original.bytes,p);
            uint64_t off=wide?OSReadBigInt64(original.bytes,p+8):OSReadBigInt32(original.bytes,p+8);
            uint64_t size=wide?OSReadBigInt64(original.bytes,p+16):OSReadBigInt32(original.bytes,p+12);
            Require(off>=8+count*stride&&size>0&&Bounds(off,size,original.length),@"Invalid universal slice");
            for(NSValue *v in ranges){NSRange r=v.rangeValue;Require(off>=NSMaxRange(r)||r.location>=off+size,@"Overlapping universal slices");}
            [ranges addObject:[NSValue valueWithRange:NSMakeRange(off,size)]];
            if(cpu==CPU_TYPE_X86_64){sliceOffset=off;sliceSize=size;selected++;}
        }
        Require(selected==1,@"Expected unique x86_64 slice");
    }
    PVGCoreImage *image=[PVGCoreImage new];image.data=[[original subdataWithRange:NSMakeRange(sliceOffset,sliceSize)] mutableCopy];
    image.sliceOffset=sliceOffset;image.symbols=[NSMutableDictionary dictionary];
    image.segments=[NSMutableArray array];image.firstSection=UINT64_MAX;
    struct mach_header_64 h;Require(image.data.length>=sizeof(h),@"Truncated Mach-O");memcpy(&h,image.data.bytes,sizeof(h));
    Require(h.magic==MH_MAGIC_64&&h.cputype==CPU_TYPE_X86_64,@"Expected x86_64 Mach-O");
    Require(Bounds(sizeof(h),h.sizeofcmds,image.data.length),@"Invalid load commands");image.commandEnd=sizeof(h)+h.sizeofcmds;
    NSUInteger p=sizeof(h),textCount=0;struct segment_command_64 textSegment={0};NSUInteger segmentCount=0;
    const uint8_t *bytes=image.data.bytes;
    struct symtab_command sym={0};NSUInteger symCount=0;
    for(uint32_t i=0;i<h.ncmds;i++) {
        struct load_command lc;Require(Bounds(p,sizeof(lc),image.commandEnd),@"Truncated command");memcpy(&lc,bytes+p,sizeof(lc));
        Require(lc.cmdsize>=sizeof(lc)&&Bounds(p,lc.cmdsize,image.commandEnd),@"Invalid command");
        if(lc.cmd==LC_SYMTAB){Require(lc.cmdsize>=sizeof(sym),@"Truncated symbol command");memcpy(&sym,bytes+p,sizeof(sym));symCount++;}
        if(lc.cmd==LC_SEGMENT_64) {
            struct segment_command_64 s;Require(lc.cmdsize>=sizeof(s),@"Truncated segment");memcpy(&s,bytes+p,sizeof(s));
            Require(Bounds(s.fileoff,s.filesize,image.data.length)&&Bounds(sizeof(s),(uint64_t)s.nsects*sizeof(struct section_64),lc.cmdsize),@"Invalid segment bounds");
            Require(s.vmaddr<=UINT64_MAX-s.vmsize&&s.vmaddr<=UINT64_MAX-s.filesize,@"Segment address overflow");
            [image.segments addObject:[NSValue valueWithBytes:&s objCType:@encode(struct segment_command_64)]];
            BOOL text=!strncmp(s.segname,"__TEXT",16);if(text){textSegment=s;segmentCount++;}
            for(uint32_t j=0;j<s.nsects;j++) {
                struct section_64 section;memcpy(&section,bytes+p+sizeof(s)+j*sizeof(section),sizeof(section));
                if(text&&section.offset)image.firstSection=MIN(image.firstSection,section.offset);
                if(text&&!strncmp(section.sectname,"__text",16)){image.text=section;textCount++;}
            }
        }
        p+=lc.cmdsize;
    }
    Require(p==image.commandEnd&&textCount==1&&segmentCount==1&&image.firstSection!=UINT64_MAX,@"Missing unique text layout");
    struct section_64 text=image.text;Require(text.addr<=UINT64_MAX-text.size,@"Text address overflow");
    Require(symCount<=1,@"Ambiguous symbol table");
    if(symCount) {
        Require(Bounds(sym.symoff,(uint64_t)sym.nsyms*sizeof(struct nlist_64),image.data.length)&&Bounds(sym.stroff,sym.strsize,image.data.length),@"Invalid symbol table bounds");
        for(uint32_t i=0;i<sym.nsyms;i++) {
            struct nlist_64 symbol;memcpy(&symbol,bytes+sym.symoff+(uint64_t)i*sizeof(symbol),sizeof(symbol));
            if((symbol.n_type&N_STAB)||(symbol.n_type&N_TYPE)!=N_SECT)continue;
            Require(symbol.n_un.n_strx<sym.strsize,@"Invalid symbol string offset");
            const char *name=(const char *)bytes+sym.stroff+symbol.n_un.n_strx;
            const char *end=memchr(name,0,sym.strsize-symbol.n_un.n_strx);Require(end!=NULL,@"Unterminated symbol");
            NSString *key=[[NSString alloc] initWithBytes:name length:end-name encoding:NSUTF8StringEncoding];
            Require(key!=nil,@"Invalid symbol encoding");
            if(!image.symbols[key])image.symbols[key]=[NSMutableSet set];
            [image.symbols[key] addObject:@(symbol.n_value)];
        }
    }
    image.textSegment=textSegment;return image;
}
static uint64_t CoreSymbol(PVGCoreImage *image,NSString *name) {
    return CoreOne(image.symbols[name].allObjects,[@"symbol " stringByAppendingString:name]);
}
static NSRange CoreFunction(PVGCoreImage *image,NSString *name) {
    uint64_t start=CoreSymbol(image,name),end=image.text.addr+image.text.size;
    Require(start>=image.text.addr&&start<end,@"Symbol outside executable text");
    for(NSSet *set in image.symbols.allValues)for(NSNumber *value in set)
        if(value.unsignedLongLongValue>start&&value.unsignedLongLongValue<end)end=value.unsignedLongLongValue;
    return NSMakeRange(start,end-start);
}
static NSMutableData *CorePreservingSlices(NSData *original,PVGCoreImage *image) {
    NSMutableData *result=[original mutableCopy];
    [result replaceBytesInRange:NSMakeRange(image.sliceOffset,image.data.length) withBytes:image.data.bytes];return result;
}
#include "guest-mojave-splitseg.h"
static NSMutableData *PatchMojaveCore(NSData *original) {
    PVGCoreImage *image=CoreParse(original);struct section_64 text=image.text;
    struct segment_command_64 textSegment=image.textSegment;const uint8_t *bytes=image.data.bytes;
    NSString *predicate=@"80 3d ?? ?? ?? ?? 00 74 51 48 83 3d ?? ?? ?? ?? ff 75 4a 0f b6 05 ?? ?? ?? ?? 89 c2 83 ca 02 31 c9 38 0d ?? ?? ?? ?? 0f 44 d0 89 d0 83 c8 04 38 0d ?? ?? ?? ?? 8b 0d ?? ?? ?? ?? 0f 44 c2 85 c9 74 11 21 c8 39 c8 75 0b 80 3d ?? ?? ?? ?? 00 0f 95 c0 c3";
    NSArray *gates=[image matches:predicate start:text.addr end:text.addr+text.size];
    NSString *block=@"48 8d 3d ?? ?? ?? ?? e8 ?? ?? ?? ?? 84 c0 74 07 80 0d ?? ?? ?? ??";
    NSString *detector=[NSString stringWithFormat:@"55 48 89 e5 %@ 01 %@ 02 %@ 04",block,block,block];
    NSArray *(^correlate)(NSArray *)=^NSArray *(NSArray *sites) {
        NSMutableArray *result=[NSMutableArray array];
        for(NSNumber *site in sites) {
            NSUInteger count=0;uint64_t gpu=[image relative:site.unsignedLongLongValue+20 displacement:2 length:7];
            for(NSNumber *gate in gates)count+=[image relative:gate.unsignedLongLongValue+53 displacement:2 length:6]==gpu;
            if(count==2)[result addObject:site];
        }return result;
    };
    NSArray *sites=correlate([image matches:[detector stringByAppendingString:@" 5d c3"] start:text.addr end:text.addr+text.size]);
    BOOL old=sites.count==0;
    if(old)sites=correlate([image matches:detector start:textSegment.vmaddr+image.commandEnd end:textSegment.vmaddr+image.firstSection]);
    uint64_t start=CoreOne(sites,@"GPU detector"),helper=0,gpu=0;
    NSMutableArray *strings=[NSMutableArray array];NSArray *names=@[@"AppleIntelFramebuffer",@"NVDA",@"AMDFramebuffer"];
    for(NSUInteger n=0;n<3;n++) {
        uint64_t at=start+4+23*n,string=[image relative:at displacement:3 length:7];
        NSData *expected=[names[n] dataUsingEncoding:NSUTF8StringEncoding];NSMutableData *terminated=[expected mutableCopy];uint8_t zero=0;[terminated appendBytes:&zero length:1];
        Require([[image read:string size:terminated.length] isEqual:terminated],@"Vendor string mismatch");[strings addObject:@(string)];
        uint64_t nextHelper=[image relative:at+7 displacement:1 length:5],nextGPU=[image relative:at+16 displacement:2 length:7];
        Require(!n||(helper==nextHelper&&gpu==nextGPU),@"Inconsistent detector references");helper=nextHelper;gpu=nextGPU;
    }
    Require(helper>=text.addr&&helper-text.addr<text.size,@"Helper outside text");[image writable:gpu];
    NSMutableArray *masks=[NSMutableArray array];
    for(NSNumber *gate in gates)if([image relative:gate.unsignedLongLongValue+53 displacement:2 length:6]==gpu)
        [masks addObject:@([image relative:gate.unsignedLongLongValue+19 displacement:3 length:7])];
    Require(masks.count==2&&![masks[0] isEqual:masks[1]],@"Expected two unique capability masks");[masks sortUsingSelector:@selector(compare:)];
    for(NSNumber *mask in masks)[image writable:mask.unsignedLongLongValue];
    uint64_t cave=start;
    if(!old) {
        Require(textSegment.fileoff==0&&(textSegment.initprot&VM_PROT_EXECUTE),@"No executable header padding");
        NSUInteger offset=(image.commandEnd+256+15)&~(NSUInteger)15;
        Require(Bounds(offset,256,image.firstSection)&&Bounds(offset,256,image.data.length),@"Insufficient header padding");
        for(NSUInteger i=offset;i<offset+256;i++)Require(bytes[i]==0,@"Nonzero header padding");
        cave=textSegment.vmaddr+offset;
    }
    NSMutableArray *references=[NSMutableArray array];
    NSMutableData *code=[Hex(@"554889e5") mutableCopy];
    for(NSUInteger n=0;n<4;n++) {
        NSArray *targets=n==3?[@[@(gpu)] arrayByAddingObjectsFromArray:masks]:@[@(gpu)];
        uint64_t string=n==3?cave+192:[strings[n] unsignedLongLongValue];
        [references addObject:@[@(cave+code.length+3),@(string)]];
        CoreRelative(code,@"488d3d",cave,string,nil);
        [references addObject:@[@(cave+code.length+1),@(helper)]];
        CoreRelative(code,@"e8",cave,helper,nil);[code appendData:Hex(@"84c074")];uint8_t skip=7*targets.count;[code appendBytes:&skip length:1];
        uint8_t bit=n==3?16:1<<n;NSData *suffix=[NSData dataWithBytes:&bit length:1];
        for(NSNumber *target in targets){[references addObject:@[@(cave+code.length+2),target]];CoreRelative(code,@"800d",cave,target.unsignedLongLongValue,suffix);}
    }
    [code appendData:Hex(@"5dc3")];Require(code.length<192,@"Trampoline too large");
    const char label[]="AppleParavirtAccelerator";NSData *labelData=[NSData dataWithBytes:label length:sizeof(label)];
    if(old) {
        Require([[image read:cave size:code.length] isEqual:code]&&[[image read:cave+192 size:labelData.length] isEqual:labelData],@"Unknown existing detector trampoline");
        NSMutableArray *entries=[NSMutableArray array];
        for(NSNumber *entry in [image matches:@"e9 ?? ?? ?? ?? 90 90 90 90 90 90" start:text.addr end:text.addr+text.size])
            if([image relative:entry.unsignedLongLongValue displacement:1 length:5]==cave)[entries addObject:entry];
        start=CoreOne(entries,@"patched detector entry");
    } else {
        [image.data replaceBytesInRange:NSMakeRange([image offset:cave size:code.length],code.length) withBytes:code.bytes];
        [image.data replaceBytesInRange:NSMakeRange([image offset:cave+192 size:labelData.length],labelData.length) withBytes:labelData.bytes];
        NSMutableData *jump=[NSMutableData data];CoreRelative(jump,@"e9",start,cave,nil);[jump appendData:Hex(@"909090909090")];
        [image.data replaceBytesInRange:NSMakeRange([image offset:start size:jump.length],jump.length) withBytes:jump.bytes];
    }
    [references addObject:@[@(start+1),@(cave)]];
    CoreFixSplitSeg(image,references,@[[NSValue valueWithRange:NSMakeRange(start,11)]]);
    return CorePreservingSlices(original,image);
}
