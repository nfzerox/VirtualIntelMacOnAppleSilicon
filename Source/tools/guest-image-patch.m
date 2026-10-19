// Native equivalent of scripts/patch-guest-image.pl. No cache extraction,
// fixed build addresses, in-place system mutation, or guest developer tools.
#import <Foundation/Foundation.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void Require(BOOL condition, NSString *message) {
    if(!condition)@throw [NSException exceptionWithName:@"PatchRejected" reason:message userInfo:nil];
}
static BOOL Bounds(uint64_t offset,uint64_t size,NSUInteger total) {return offset<=total&&size<=total-offset;}
static NSData *Text(NSString *text) {return [text dataUsingEncoding:NSUTF8StringEncoding];}
static NSData *Hex(NSString *hex) {
    NSMutableData *data=[NSMutableData data];
    for(NSUInteger i=0;i<hex.length;i+=2){unsigned x=0;[[NSScanner scannerWithString:[hex substringWithRange:NSMakeRange(i,2)]] scanHexInt:&x];uint8_t b=x;[data appendBytes:&b length:1];}
    return data;
}
#include "guest-mojave-core.h"
#include "guest-mojave-sky.h"
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=4){fprintf(stderr,"usage: guest-image-patch coredisplay|mojave-coredisplay|mojave-skylight|compiler|service INPUT NEW_OUTPUT\n");return 64;}
    @try {
        NSString *kind=@(argv[1]);NSError *error=nil;
        NSMutableData *data=[[NSData dataWithContentsOfFile:@(argv[2]) options:0 error:&error] mutableCopy];
        Require(data!=nil,error.localizedDescription?:@"Cannot read input");
        if([kind isEqual:@"mojave-coredisplay"])data=PatchMojaveCore(data);
        else if([kind isEqual:@"mojave-skylight"])data=PatchMojaveSky(data);
        else {
        struct mach_header_64 header;
        Require(data.length>=sizeof(header),@"Truncated Mach-O header");memcpy(&header,data.bytes,sizeof(header));
        Require(header.magic==MH_MAGIC_64&&header.cputype==CPU_TYPE_X86_64,@"Expected thin x86_64 Mach-O");
        Require(Bounds(sizeof(header),header.sizeofcmds,data.length),@"Invalid load command bounds");
        const uint8_t *bytes=data.bytes;NSUInteger cursor=sizeof(header),limit=cursor+header.sizeofcmds;
        struct section_64 text={0};NSUInteger textCount=0;struct symtab_command sym={0};
        for(uint32_t i=0;i<header.ncmds;i++) {
            struct load_command lc;
            Require(Bounds(cursor,sizeof(lc),limit),@"Truncated command");memcpy(&lc,bytes+cursor,sizeof(lc));
            Require(lc.cmdsize>=sizeof(lc)&&Bounds(cursor,lc.cmdsize,limit),@"Invalid command");
            if(lc.cmd==LC_SYMTAB){Require(lc.cmdsize>=sizeof(sym),@"Short symbol command");memcpy(&sym,bytes+cursor,sizeof(sym));}
            if(lc.cmd==LC_SEGMENT_64) {
                struct segment_command_64 segment;
                Require(lc.cmdsize>=sizeof(segment),@"Short segment");memcpy(&segment,bytes+cursor,sizeof(segment));
                Require(Bounds(sizeof(segment),(uint64_t)segment.nsects*sizeof(text),lc.cmdsize),@"Invalid section count");
                for(uint32_t s=0;s<segment.nsects;s++) {
                    struct section_64 section;memcpy(&section,bytes+cursor+sizeof(segment)+s*sizeof(section),sizeof(section));
                    if(!strncmp(section.sectname,"__text",16)&&!strncmp(section.segname,"__TEXT",16)) {text=section;textCount++;}
                }
            }
            cursor+=lc.cmdsize;
        }
        Require(textCount==1&&Bounds(text.offset,text.size,data.length)&&text.addr<=UINT64_MAX-text.size,@"Missing or invalid unique text section");
        NSMutableDictionary<NSString *,NSNumber *> *symbols=[NSMutableDictionary dictionary];
        if(sym.cmd) {
            Require(Bounds(sym.symoff,(uint64_t)sym.nsyms*sizeof(struct nlist_64),data.length)&&Bounds(sym.stroff,sym.strsize,data.length),@"Invalid symbol table");
            for(uint32_t i=0;i<sym.nsyms;i++) {
                struct nlist_64 symbol;memcpy(&symbol,bytes+sym.symoff+(uint64_t)i*sizeof(symbol),sizeof(symbol));
                if((symbol.n_type&N_TYPE)!=N_SECT||(symbol.n_type&N_STAB)||symbol.n_un.n_strx>=sym.strsize||symbol.n_value<text.addr||symbol.n_value>=text.addr+text.size)continue;
                const char *name=(const char *)bytes+sym.stroff+symbol.n_un.n_strx;
                const char *end=memchr(name,0,sym.strsize-symbol.n_un.n_strx);Require(end!=NULL,@"Unterminated symbol");
                NSString *key=[[NSString alloc] initWithBytes:name length:end-name encoding:NSUTF8StringEncoding];
                Require(key!=nil,@"Invalid symbol encoding");symbols[key]=@(symbol.n_value);
            }
        }
        void (^patch)(NSData *,NSData *,NSString *)=^(NSData *old,NSData *replacement,NSString *function) {
            Require(old.length==replacement.length,@"Patch changes file length");
            NSRange scope=NSMakeRange(0,data.length);
            if(function) {
                Require(symbols[function]!=nil,[@"Missing native symbol " stringByAppendingString:function]);
                uint64_t address=[symbols[function] unsignedLongLongValue],end=text.addr+text.size;
                for(NSNumber *value in symbols.allValues)if(value.unsignedLongLongValue>address&&value.unsignedLongLongValue<end)end=value.unsignedLongLongValue;
                scope=NSMakeRange(text.offset+address-text.addr,end-address);
            }
            NSUInteger matches=0,offset=0;
            for(NSData *pattern in @[old,replacement]) {
                NSRange remaining=scope;
                while(remaining.length>=pattern.length) {
                    NSRange found=[data rangeOfData:pattern options:0 range:remaining];if(found.location==NSNotFound)break;
                    offset=found.location;matches++;NSUInteger next=found.location+1;
                    remaining=NSMakeRange(next,NSMaxRange(scope)-next);
                }
            }
            Require(matches==1,[NSString stringWithFormat:@"Unknown or ambiguous %@ patch (%lu matches)",kind,matches]);
            [data replaceBytesInRange:NSMakeRange(offset,replacement.length) withBytes:replacement.bytes];
            printf("%s: matched patch at file offset 0x%lx\n",kind.UTF8String,offset);
        };
        if([kind isEqual:@"coredisplay"]) {
            patch(Hex(@"0f44d089d083c804380d"),Hex(@"0f44d089d083c814380d"),@"__ZN11CoreDisplay16UseIOPresentmentEv");
            patch(Hex(@"4883f8ffb801000000bb080000000f44d8"),Hex(@"4883f8ffb810000000bb080000000f44d8"),@"_GetGPUVendorForFramebufferService");
        } else if([kind isEqual:@"compiler"]) {
            NSData *old=Text(@"AppleIntelHD4000GraphicsMTLDriver.bundle/Contents/MacOS/libMTLIntelCompilerPlugin.dylib");
            NSMutableData *replacement=[Text(@"AppleParavirtGPUMetal.bundle/Contents/Resources/libAppleParavirtCompilerPlugin.dylib") mutableCopy];
            Require(replacement.length<=old.length,@"Compiler path too long");replacement.length=old.length;
            patch(old,replacement,nil);
            BOOL known=NO;
            for(NSString *version in @[@"3802",@"3902"])
                known|=[data rangeOfData:Text([NSString stringWithFormat:@"/GPUCompiler.framework/Versions/%@/Libraries/libLLVM.dylib",version]) options:0 range:NSMakeRange(0,data.length)].location!=NSNotFound;
            Require(known,@"Unsupported native compiler family");
        } else if([kind isEqual:@"service"]) {
            NSString *prefix=@"/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/";
            NSString *from,*to;
            BOOL (^hasVersion)(NSString *)=^BOOL(NSString *version) {
                return [data rangeOfData:Text([NSString stringWithFormat:@"%@%@/MTLCompiler",prefix,version]) options:0 range:NSMakeRange(0,data.length)].location!=NSNotFound;
            };
            // Supported Mojave/Catalina use 3902. Some services also contain
            // the legacy 3802 path: do not select that merely because it exists,
            // or change selection after an already-patched input is supplied.
            if(hasVersion(@"3902")||hasVersion(@"3903")){from=@"3902";to=@"3903";}
            else if(hasVersion(@"3802")||hasVersion(@"3803")){from=@"3802";to=@"3803";}
            else {from=@"%d";to=@"P3";}
            patch(Text([NSString stringWithFormat:@"%@%@/MTLCompiler",prefix,from]),Text([NSString stringWithFormat:@"%@%@/MTLCompiler",prefix,to]),nil);
        } else Require(NO,@"Unknown patch kind");
        }
        // The caller owns staging/transaction/signing. Never overwrite an input
        // or an existing output, even if the caller accidentally reuses a path.
        int fd=open(argv[3],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0755);Require(fd>=0,@"Cannot create new output");
        const uint8_t *p=data.bytes;NSUInteger left=data.length;BOOL complete=YES;
        while(left){ssize_t n=write(fd,p,left);if(n<0&&errno==EINTR)continue;if(n<=0){complete=NO;break;}p+=n;left-=n;}
        BOOL synced=complete&&fsync(fd)==0;int closed=close(fd);
        Require(synced&&closed==0,@"Failed to write output (partial staging file retained)");
        return 0;
    } @catch(NSException *exception) {fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
}}
