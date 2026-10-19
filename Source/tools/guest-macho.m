// Guest-native replacement for pvg_macho.py's thin/replace-x86_64 operations.
// Inputs are never changed. The caller owns signing and the install transaction.
#import <Foundation/Foundation.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#include <libkern/OSByteOrder.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <CommonCrypto/CommonDigest.h>
#include "guest-support-media.h"

static void Check(BOOL condition,NSString *message) {
    if(!condition)@throw [NSException exceptionWithName:@"InvalidImage" reason:message userInfo:nil];
}
static BOOL Fits(uint64_t offset,uint64_t size,NSUInteger length){return offset<=length&&size<=length-offset;}
static uint32_t BE32(NSData *data,NSUInteger offset){Check(Fits(offset,4,data.length),@"Truncated header");return OSReadBigInt32(data.bytes,offset);}
static uint64_t BE64(NSData *data,NSUInteger offset){Check(Fits(offset,8,data.length),@"Truncated header");return OSReadBigInt64(data.bytes,offset);}
static uint32_t LE32(NSData *data,NSUInteger offset){Check(Fits(offset,4,data.length),@"Truncated Mach-O header");return OSReadLittleInt32(data.bytes,offset);}
static NSArray<NSDictionary *> *Slices(NSData *data) {
    uint32_t magic=BE32(data,0);
    if(magic!=0xcafebabe&&magic!=0xcafebabf) {
        uint32_t cpu=LE32(data,4);
        Check((cpu==CPU_TYPE_X86_64&&LE32(data,0)==MH_MAGIC_64&&data.length>=32)||
              (cpu==CPU_TYPE_I386&&LE32(data,0)==MH_MAGIC&&data.length>=28),@"Expected Intel Mach-O or universal image");
        return @[@{@"cpu":@(cpu),@"subtype":@(LE32(data,8)),@"offset":@0,@"size":@(data.length),@"align":@12}];
    }
    BOOL wide=magic==0xcafebabf;
    NSUInteger count=BE32(data,4),stride=wide?32:20,header=8+count*stride;
    Check(count>0&&count<=64&&header<=data.length,@"Invalid universal header");
    NSMutableArray *parts=[NSMutableArray array];NSMutableSet *architectures=[NSMutableSet set];
    for(NSUInteger i=0;i<count;i++) {
        NSUInteger p=8+i*stride;uint32_t cpu=BE32(data,p),subtype=BE32(data,p+4);
        uint64_t offset=wide?BE64(data,p+8):BE32(data,p+8),size=wide?BE64(data,p+16):BE32(data,p+12);
        uint32_t alignment=BE32(data,p+(wide?24:16));
        Check(alignment<=20&&offset>=header&&size>=28&&Fits(offset,size,data.length),@"Invalid universal slice bounds");
        Check(!(offset&((1ULL<<alignment)-1)),@"Misaligned universal slice");
        Check(![architectures containsObject:@(cpu)],@"Ambiguous architecture in universal image");
        [architectures addObject:@(cpu)];
        for(NSDictionary *part in parts) {
            uint64_t start=[part[@"offset"] unsignedLongLongValue],length=[part[@"size"] unsignedLongLongValue];
            Check(offset>=start+length||start>=offset+size,@"Overlapping universal slices");
        }
        if(cpu==CPU_TYPE_X86_64||cpu==CPU_TYPE_I386) {
            Check(LE32(data,offset)==(cpu==CPU_TYPE_X86_64?MH_MAGIC_64:MH_MAGIC)&&
                LE32(data,offset+4)==cpu&&LE32(data,offset+8)==subtype,@"Slice architecture disagrees with universal header");
            Check(size>=(cpu==CPU_TYPE_X86_64?32:28),@"Truncated architecture header");
        }
        [parts addObject:@{@"cpu":@(cpu),@"subtype":@(subtype),@"offset":@(offset),@"size":@(size),@"align":@(alignment)}];
    }
    return parts;
}
static NSData *Thin(NSData *data) {
    NSDictionary *selected=nil;
    for(NSDictionary *part in Slices(data))if([part[@"cpu"] unsignedIntValue]==CPU_TYPE_X86_64) {
        Check(!selected,@"Multiple x86_64 slices");selected=part;
    }
    Check(selected!=nil,@"Missing x86_64 slice");
    NSData *slice=[data subdataWithRange:NSMakeRange([selected[@"offset"] unsignedLongLongValue],[selected[@"size"] unsignedLongLongValue])];
    Check(LE32(slice,0)==MH_MAGIC_64&&LE32(slice,4)==CPU_TYPE_X86_64,@"Invalid x86_64 image");
    return slice;
}
static NSData *PrepareBigSurBundle(NSData *input,BOOL metalPlugin) {
    // The native Big Sur GPU plugin is already a loose bundle. Copy its Intel
    // slice into /Library/GPUBundles and change only these two dependencies;
    // no shared-cache extraction, fixed offsets, or sealed-volume edit.
    NSMutableData *output=[Thin(input) mutableCopy];
    Check(LE32(output,12)==MH_BUNDLE,@"Expected a native GPU plugin bundle");
    NSUInteger count=LE32(output,16),cursor=sizeof(struct mach_header_64);
    NSUInteger end=cursor+LE32(output,20);
    Check(end>=cursor&&end<=output.length,@"Invalid renderer command bounds");
    NSString *helper=metalPlugin?@"@loader_path/PVGBigSurMetalCompat.dylib":@"@loader_path/PVGOpenGLMetal.dylib";
    NSData *name=[helper dataUsingEncoding:NSUTF8StringEncoding];
    unsigned metal=0,surface=0,prepared=0;
    for(NSUInteger i=0;i<count;i++) {
        Check(Fits(cursor,sizeof(struct load_command),end),@"Truncated renderer load command");
        uint32_t command=LE32(output,cursor),length=LE32(output,cursor+4);
        Check(length>=sizeof(struct load_command)&&Fits(cursor,length,end),@"Invalid renderer load command");
        if(command==LC_LOAD_DYLIB) {
            Check(length>=sizeof(struct dylib_command),@"Short renderer dependency");
            NSUInteger offset=LE32(output,cursor+8);
            Check(offset>=sizeof(struct dylib_command)&&offset<length,@"Invalid renderer dependency name");
            const char *start=(const char *)output.bytes+cursor+offset;
            const char *terminator=memchr(start,0,length-offset);
            Check(terminator!=NULL,@"Unterminated renderer dependency");
            NSString *old=[[NSString alloc] initWithBytes:start length:terminator-start encoding:NSUTF8StringEncoding];
            Check(old!=nil,@"Invalid renderer dependency encoding");
            BOOL isMetal=[old isEqual:@"/System/Library/Frameworks/Metal.framework/Versions/A/Metal"];
            BOOL isSurface=[old isEqual:@"/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface"];
            metal+=isMetal;surface+=isSurface;prepared+=[old isEqual:helper];
            if(isMetal||isSurface) {
                Check(name.length+1<=length-offset,@"Renderer dependency has insufficient capacity");
                memset((uint8_t *)output.mutableBytes+cursor+offset,0,length-offset);
                memcpy((uint8_t *)output.mutableBytes+cursor+offset,name.bytes,name.length);
            }
        }
        cursor+=length;
    }
    Check(cursor==end&&((metal==1&&surface==1&&prepared==0)||(metal==0&&surface==0&&prepared==2)),@"Unexpected or partially prepared renderer dependencies");
    return output;
}
static NSData *Replace(NSData *original,NSData *replacement,BOOL allowNative32) {
    uint32_t magic=BE32(original,0);BOOL wide=magic==0xcafebabf;
    Check(magic==0xcafebabe||wide||allowNative32,@"Original must be universal to preserve 32-bit compatibility");
    NSMutableArray *parts=[Slices(original) mutableCopy];
    BOOL has64=NO;
    for(NSDictionary *part in parts)has64|=[part[@"cpu"] unsignedIntValue]==CPU_TYPE_X86_64;
    if(!has64) {
        Check(allowNative32,@"Missing x86_64 slice");
        [parts addObject:@{@"cpu":@(CPU_TYPE_X86_64),@"align":@12}];
    }
    if(magic!=0xcafebabf)magic=0xcafebabe;
    NSData *replacementSlice=Thin(replacement);
    NSUInteger stride=wide?32:20,cursor=8+stride*parts.count;
    NSMutableData *output=[NSMutableData dataWithLength:cursor];
    OSWriteBigInt32(output.mutableBytes,0,magic);OSWriteBigInt32(output.mutableBytes,4,(uint32_t)parts.count);
    BOOL i386=NO;NSUInteger index=0;
    for(NSDictionary *part in parts) {
        uint32_t cpu=[part[@"cpu"] unsignedIntValue],alignment=[part[@"align"] unsignedIntValue];
        BOOL replacing=cpu==CPU_TYPE_X86_64;i386|=cpu==CPU_TYPE_I386;
        NSData *content=replacing?replacementSlice:[original subdataWithRange:NSMakeRange(
            [part[@"offset"] unsignedLongLongValue],[part[@"size"] unsignedLongLongValue])];
        uint32_t subtype=replacing?LE32(content,8):[part[@"subtype"] unsignedIntValue];
        uint64_t aligned=((uint64_t)cursor+(1ULL<<alignment)-1)&~((1ULL<<alignment)-1);
        Check(aligned<=NSUIntegerMax&&content.length<=NSUIntegerMax-aligned,@"Combined image is too large");
        Check(wide||(aligned<=UINT32_MAX&&content.length<=UINT32_MAX),@"Image exceeds 32-bit universal bounds");
        output.length=(NSUInteger)aligned;[output appendData:content];
        NSUInteger p=8+index++*stride;
        OSWriteBigInt32(output.mutableBytes,p,cpu);OSWriteBigInt32(output.mutableBytes,p+4,subtype);
        if(wide) {
            OSWriteBigInt64(output.mutableBytes,p+8,aligned);OSWriteBigInt64(output.mutableBytes,p+16,content.length);
            OSWriteBigInt32(output.mutableBytes,p+24,alignment);
        } else {
            OSWriteBigInt32(output.mutableBytes,p+8,(uint32_t)aligned);OSWriteBigInt32(output.mutableBytes,p+12,(uint32_t)content.length);
            OSWriteBigInt32(output.mutableBytes,p+16,alignment);
        }
        cursor=output.length;
    }
    Check(i386,@"Original i386 slice is missing; restore a stock backup first");
    (void)Slices(output);return output;
}
// Restore the native i386 entry's install name without moving any sections.
// Legacy routing facades are deliberately rejected: callers must supply the
// preserved native backup, never silently treat a forwarding stub as a driver.
static NSData *NativeOpenGL32(NSData *input) {
    NSData *data=input;
    if(BE32(input,0)==0xcafebabe||BE32(input,0)==0xcafebabf) {
        NSDictionary *selected=nil;
        for(NSDictionary *part in Slices(input))if([part[@"cpu"] unsignedIntValue]==CPU_TYPE_I386)selected=part;
        Check(selected!=nil,@"Missing native i386 slice");
        data=[input subdataWithRange:NSMakeRange([selected[@"offset"] unsignedLongLongValue],[selected[@"size"] unsignedLongLongValue])];
    }
    Check(data.length>=28&&LE32(data,0)==MH_MAGIC&&LE32(data,4)==CPU_TYPE_I386,@"Expected native i386 Mach-O");
    NSUInteger count=LE32(data,16),size=LE32(data,20),cursor=28,firstSection=data.length,identities=0;
    Check(Fits(28,size,data.length)&&count<=size/8,@"Invalid load command range");
    NSMutableData *table=[NSMutableData data];
    NSString *stock=@"/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL";
    NSArray *privateNames=@[@"/System/Library/Frameworks/PVGGL32.dylib",@"/Library/PVG/OpenGL32.dylib"];
    for(NSUInteger i=0;i<count;i++) {
        Check(Fits(cursor,8,28+size),@"Truncated load command");
        uint32_t command=LE32(data,cursor),length=LE32(data,cursor+4);
        Check(length>=8&&!(length&3)&&Fits(cursor,length,28+size),@"Invalid load command length");
        NSData *rewritten=[data subdataWithRange:NSMakeRange(cursor,length)];
        if(command==LC_SEGMENT) {
            Check(length>=56,@"Truncated segment");
            NSUInteger sections=LE32(data,cursor+48);
            Check(sections<=(length-56)/68,@"Truncated section table");
            for(NSUInteger j=0;j<sections;j++) {
                uint32_t offset=LE32(data,cursor+56+68*j+40);
                if(offset)firstSection=MIN(firstSection,offset);
            }
        }
        if(command==LC_ID_DYLIB||command==LC_REEXPORT_DYLIB) {
            Check(length>=24,@"Invalid dylib command");
            NSUInteger start=LE32(data,cursor+8);
            Check(start>=24&&start<length,@"Invalid dylib name offset");
            const char *name=(const char *)data.bytes+cursor+start;
            const char *end=memchr(name,0,length-start);
            Check(end!=NULL,@"Unterminated dylib name");
            NSString *identity=[[NSString alloc] initWithBytes:name length:end-name encoding:NSUTF8StringEncoding];
            Check(identity!=nil,@"Invalid dylib identity");
            if(command==LC_REEXPORT_DYLIB)Check(![privateNames containsObject:identity],@"Routing facade is not a native backup");
            else {
                Check([identity isEqual:stock]||[privateNames containsObject:identity],@"Unrecognized OpenGL identity");
                NSData *newName=[stock dataUsingEncoding:NSUTF8StringEncoding];
                NSUInteger newLength=MAX(length,(start+newName.length+1+3)&~(NSUInteger)3);
                NSMutableData *newCommand=[[data subdataWithRange:NSMakeRange(cursor,start)] mutableCopy];
                newCommand.length=newLength;
                OSWriteLittleInt32(newCommand.mutableBytes,4,(uint32_t)newLength);
                memcpy((uint8_t *)newCommand.mutableBytes+start,newName.bytes,newName.length);
                rewritten=newCommand;identities++;
            }
        }
        [table appendData:rewritten];cursor+=length;
    }
    Check(cursor==28+size&&identities==1,@"Invalid command table or identity");
    Check(table.length<=UINT32_MAX&&Fits(28,table.length,data.length)&&28+table.length<=firstSection,@"Insufficient Mach-O header padding");
    const uint8_t *bytes=data.bytes;
    for(NSUInteger i=28+size;i<28+table.length;i++)Check(bytes[i]==0,@"Nonzero Mach-O header padding");
    NSMutableData *result=[data mutableCopy];
    memcpy((uint8_t *)result.mutableBytes+28,table.bytes,table.length);
    OSWriteLittleInt32(result.mutableBytes,20,(uint32_t)table.length);
    return result;
}
static NSData *ReadImage(NSString *path) {
    NSError *error=nil;
    NSData *data=[NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:&error];
    Check(data!=nil,error.localizedDescription?:@"Cannot read image");return data;
}
static BOOL Inside(NSString *path,NSString *root) {
    return [path hasPrefix:[root stringByAppendingString:@"/"]];
}
static void StageOpenGL(NSString *guest,NSString *staged,NSArray<NSString *> *backups) {
    NSFileManager *fm=NSFileManager.defaultManager;
    guest=guest.stringByResolvingSymlinksInPath.stringByStandardizingPath;
    staged=staged.stringByResolvingSymlinksInPath.stringByStandardizingPath;
    Check(guest.isAbsolutePath&&staged.isAbsolutePath&&![guest isEqual:staged]&&
          !Inside(guest,staged)&&!Inside(staged,guest),@"Native and staging frameworks must be separate directories");
    BOOL directory=NO;
    Check([fm fileExistsAtPath:guest isDirectory:&directory]&&directory,@"Missing native framework");
    Check([fm fileExistsAtPath:staged isDirectory:&directory]&&directory,@"Missing staging framework");
    NSString *manifest=[staged stringByAppendingPathComponent:@"Versions/A/Resources/PVGDonorFiles.txt"];
    Check(Inside(manifest.stringByResolvingSymlinksInPath,staged),@"Manifest escapes staging framework");
    NSString *text=[[NSString alloc] initWithData:ReadImage(manifest) encoding:NSUTF8StringEncoding];
    Check(text!=nil,@"Invalid donor manifest");
    NSMutableSet *donors=[NSMutableSet set];
    for(NSString *line in [text componentsSeparatedByString:@"\n"]) {
        if(!line.length)continue;
        Check(!line.isAbsolutePath&&![line.pathComponents containsObject:@".."]&&
              [line rangeOfCharacterFromSet:NSCharacterSet.controlCharacterSet].location==NSNotFound,@"Unsafe donor manifest path");
        [donors addObject:line];
    }
    Check([donors containsObject:@"Versions/A/OpenGL"],@"Donor manifest lacks OpenGL entry");
    NSMutableArray *changes=[NSMutableArray array];BOOL entry=NO;
    NSDirectoryEnumerator *enumerator=[fm enumeratorAtPath:staged];
    Check(enumerator!=nil,@"Cannot enumerate staging framework");
    for(NSString *relative in enumerator) {
        NSDictionary *attributes=enumerator.fileAttributes;
        if(![attributes[NSFileType] isEqual:NSFileTypeRegular])continue;
        Check([relative rangeOfCharacterFromSet:NSCharacterSet.controlCharacterSet].location==NSNotFound,@"Unsafe staging path");
        NSString *path=[staged stringByAppendingPathComponent:relative];
        Check(Inside(path.stringByResolvingSymlinksInPath,staged),@"Image escapes staging framework");
        NSData *donor=ReadImage(path);
        if(donor.length<4)continue;
        uint32_t magic=BE32(donor,0);
        if(magic!=0xcffaedfe&&magic!=0xcafebabe&&magic!=0xcafebabf)continue;
        NSString *original=[guest stringByAppendingPathComponent:relative];
        if(![fm fileExistsAtPath:original])continue;
        Check(Inside(original.stringByResolvingSymlinksInPath,guest),@"Native image escapes framework");
        NSData *source=ReadImage(original),*output=nil;
        if(![donors containsObject:relative])output=source;
        else {
            BOOL isEntry=[relative isEqual:@"Versions/A/OpenGL"];
            if(isEntry) {
                @try {source=NativeOpenGL32(source);}
                @catch(NSException *exception) {
                    if(![exception.reason isEqual:@"Routing facade is not a native backup"])@throw;
                    NSString *backup=nil;
                    for(NSString *candidate in backups)if([fm fileExistsAtPath:candidate]){backup=candidate;break;}
                    Check(backup!=nil,@"Old facade requires preserved native OpenGL backup");
                    source=NativeOpenGL32(ReadImage(backup));
                }
            }
            BOOL has32=NO;
            for(NSDictionary *part in Slices(source))has32|=[part[@"cpu"] unsignedIntValue]==CPU_TYPE_I386;
            if(!has32)continue;
            output=Replace(source,donor,YES);entry|=isEntry;
        }
        [changes addObject:@{@"path":path,@"data":output,@"permissions":attributes[NSFilePosixPermissions]?:@0755}];
    }
    // Validate the complete framework before publishing any staged replacement.
    // This is staging, not a system transaction; caller signs only on success.
    Check(entry,@"Native i386 OpenGL entry was not preserved");
    for(NSDictionary *change in changes) {
        NSError *error=nil;NSString *path=change[@"path"];
        Check([change[@"data"] writeToFile:path options:NSDataWritingAtomic error:&error],error.localizedDescription?:@"Cannot write staged image");
        Check([fm setAttributes:@{NSFilePosixPermissions:change[@"permissions"]} ofItemAtPath:path error:&error],error.localizedDescription?:@"Cannot preserve staged permissions");
        printf("%s\n",path.fileSystemRepresentation);
    }
}
// Older codesign_allocate requires the signature immediately after the final
// string table. Earlier split-segment patching dropped LC_CODE_SIGNATURE but
// retained its zeroed allocation, so a later signer appended a second slot.
// Canonicalize only checked x86_64 LINKEDIT padding, preserving other slices.
static NSData *CompactLinkedit(NSData *original) {
    NSMutableData *image=[Thin(original) mutableCopy];struct mach_header_64 h;
    memcpy(&h,image.bytes,sizeof(h));Check(Fits(sizeof(h),h.sizeofcmds,image.length),@"Invalid load commands");
    struct symtab_command symbols={0};struct linkedit_data_command signature={0};
    struct segment_command_64 linkedit={0};NSUInteger symbolCount=0,linkCount=0,signatureCount=0,signatureCommand=0,linkCommand=0;
    NSMutableArray *ranges=[NSMutableArray array];NSUInteger p=sizeof(h),end=p+h.sizeofcmds;
    void (^range)(uint64_t,uint64_t)=^(uint64_t offset,uint64_t size){if(size){Check(Fits(offset,size,image.length),@"LINKEDIT range outside image");[ranges addObject:@[@(offset),@(size)]];}};
    for(uint32_t i=0;i<h.ncmds;i++) {
        struct load_command lc;Check(Fits(p,sizeof(lc),end),@"Truncated load command");memcpy(&lc,(const uint8_t *)image.bytes+p,sizeof(lc));
        Check(lc.cmdsize>=sizeof(lc)&&Fits(p,lc.cmdsize,end),@"Invalid load command size");
        if(lc.cmd==LC_SEGMENT_64) {
            Check(lc.cmdsize>=sizeof(linkedit),@"Short segment command");struct segment_command_64 segment;memcpy(&segment,(const uint8_t *)image.bytes+p,sizeof(segment));
            if(!strncmp(segment.segname,"__LINKEDIT",16)){Check(segment.nsects==0,@"Unexpected LINKEDIT sections");linkedit=segment;linkCount++;linkCommand=p;}
        } else if(lc.cmd==LC_SYMTAB) {
            Check(lc.cmdsize>=sizeof(symbols),@"Short symbol command");memcpy(&symbols,(const uint8_t *)image.bytes+p,sizeof(symbols));symbolCount++;
            range(symbols.symoff,(uint64_t)symbols.nsyms*sizeof(struct nlist_64));range(symbols.stroff,symbols.strsize);
        } else if(lc.cmd==LC_DYSYMTAB) {
            struct dysymtab_command s;Check(lc.cmdsize>=sizeof(s),@"Short dynamic symbol command");memcpy(&s,(const uint8_t *)image.bytes+p,sizeof(s));
            range(s.tocoff,(uint64_t)s.ntoc*sizeof(struct dylib_table_of_contents));range(s.modtaboff,(uint64_t)s.nmodtab*sizeof(struct dylib_module_64));range(s.extrefsymoff,(uint64_t)s.nextrefsyms*sizeof(struct dylib_reference));
            range(s.indirectsymoff,(uint64_t)s.nindirectsyms*sizeof(uint32_t));range(s.extreloff,(uint64_t)s.nextrel*sizeof(struct relocation_info));range(s.locreloff,(uint64_t)s.nlocrel*sizeof(struct relocation_info));
        } else if(lc.cmd==LC_DYLD_INFO||lc.cmd==LC_DYLD_INFO_ONLY) {
            struct dyld_info_command s;Check(lc.cmdsize>=sizeof(s),@"Short dyld info command");memcpy(&s,(const uint8_t *)image.bytes+p,sizeof(s));
            range(s.rebase_off,s.rebase_size);range(s.bind_off,s.bind_size);range(s.weak_bind_off,s.weak_bind_size);range(s.lazy_bind_off,s.lazy_bind_size);range(s.export_off,s.export_size);
        } else if(lc.cmd==LC_CODE_SIGNATURE||lc.cmd==LC_SEGMENT_SPLIT_INFO||lc.cmd==LC_FUNCTION_STARTS||lc.cmd==LC_DATA_IN_CODE||
                  lc.cmd==LC_DYLIB_CODE_SIGN_DRS||lc.cmd==LC_LINKER_OPTIMIZATION_HINT||lc.cmd==LC_DYLD_EXPORTS_TRIE||lc.cmd==LC_DYLD_CHAINED_FIXUPS) {
            struct linkedit_data_command s;Check(lc.cmdsize>=sizeof(s),@"Short LINKEDIT command");memcpy(&s,(const uint8_t *)image.bytes+p,sizeof(s));
            if(lc.cmd==LC_CODE_SIGNATURE){signature=s;signatureCommand=p;signatureCount++;}else range(s.dataoff,s.datasize);
        } else if(lc.cmd==LC_TWOLEVEL_HINTS) {
            struct twolevel_hints_command s;Check(lc.cmdsize>=sizeof(s),@"Short hints command");memcpy(&s,(const uint8_t *)image.bytes+p,sizeof(s));range(s.offset,(uint64_t)s.nhints*4);
        } else {
            const uint32_t harmless[]={LC_ID_DYLIB,LC_LOAD_DYLIB,LC_LOAD_WEAK_DYLIB,LC_REEXPORT_DYLIB,LC_LOAD_UPWARD_DYLIB,
                LC_RPATH,LC_UUID,LC_SOURCE_VERSION,LC_VERSION_MIN_MACOSX,LC_BUILD_VERSION,LC_MAIN,LC_ROUTINES_64,
                LC_SUB_FRAMEWORK,LC_SUB_UMBRELLA,LC_SUB_CLIENT,LC_SUB_LIBRARY,LC_LOAD_DYLINKER,LC_ID_DYLINKER,LC_DYLD_ENVIRONMENT};
            BOOL known=NO;for(NSUInteger j=0;j<sizeof(harmless)/sizeof(*harmless);j++)known|=lc.cmd==harmless[j];
            Check(known,@"Unrecognized load command during legacy signature preparation");
        }
        p+=lc.cmdsize;
    }
    Check(p==end&&symbolCount==1&&linkCount==1&&signatureCount<=1&&symbols.strsize,@"Unsupported LINKEDIT layout");
    uint64_t stringEnd=(uint64_t)symbols.stroff+symbols.strsize;
    Check(linkedit.fileoff<=symbols.stroff&&Fits(linkedit.fileoff,linkedit.filesize,image.length)&&linkedit.fileoff+linkedit.filesize==image.length,@"LINKEDIT is not the final file segment");
    for(NSArray *r in ranges)Check([r[0] unsignedLongLongValue]>=linkedit.fileoff&&[r[0] unsignedLongLongValue]+[r[1] unsignedLongLongValue]<=stringEnd,@"Live LINKEDIT data follows the string table");
    uint64_t boundary=signatureCount?(stringEnd+15)&~15ULL:stringEnd;
    uint64_t oldBoundary=signatureCount?signature.dataoff:image.length;
    Check(boundary<=oldBoundary&&Fits(oldBoundary,signatureCount?signature.datasize:0,image.length)&&
          (!signatureCount||oldBoundary+signature.datasize==image.length),@"Invalid signature allocation");
    const uint8_t *bytes=image.bytes;
    for(uint64_t i=stringEnd;i<oldBoundary;i++)Check(bytes[i]==0,@"Nonzero data in obsolete signature padding");
    uint64_t length=boundary+(signatureCount?signature.datasize:0);
    if(length==image.length)return original;
    Check(length>=linkedit.fileoff&&length<=NSUIntegerMax,@"Invalid compacted image size");
    if(signatureCount){uint8_t *mutable=image.mutableBytes;Check(boundary<=UINT32_MAX,@"Signature offset exceeds Mach-O limits");memmove(mutable+boundary,mutable+oldBoundary,signature.datasize);signature.dataoff=(uint32_t)boundary;memcpy(mutable+signatureCommand,&signature,sizeof(signature));}
    linkedit.filesize=length-linkedit.fileoff;linkedit.vmsize=(linkedit.filesize+4095)&~4095ULL;
    memcpy((uint8_t *)image.mutableBytes+linkCommand,&linkedit,sizeof(linkedit));image.length=(NSUInteger)length;
    uint32_t magic=BE32(original,0);
    return magic==0xcafebabe||magic==0xcafebabf?Replace(original,image,YES):image;
}
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc==4&&!strcmp(argv[1],"stage-media")) {
        @try {
            VIMMediaCheck(geteuid()==0,@"Support staging requires the privileged guest agent.");
            VIMMediaCheck([@(argv[2]) isEqual:[@(argv[3]) stringByAppendingPathComponent:@"support-manifest.plist"]],@"Invalid trusted support manifest path.");
            NSDictionary *receipt=VIMStageSupportMedia(@"support-manifest.plist",@(argv[3]),@"/Volumes",YES);
            NSData *json=[NSJSONSerialization dataWithJSONObject:receipt options:0 error:NULL];
            fwrite(json.bytes,1,json.length,stdout);puts("");return 0;
        } @catch(NSException *exception) {fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
    }
    if(argc==3&&!strcmp(argv[1],"sha256")) {
        int fd=open(argv[2],O_RDONLY|O_NOFOLLOW);struct stat st;
        if(fd<0)return 66;
        if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return 65;}
        CC_SHA256_CTX context;CC_SHA256_Init(&context);uint8_t buffer[65536],digest[CC_SHA256_DIGEST_LENGTH];
        for(;;){ssize_t n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;
            if(n<0){close(fd);return 74;}if(!n)break;CC_SHA256_Update(&context,buffer,(CC_LONG)n);}
        if(close(fd))return 74;CC_SHA256_Final(digest,&context);
        for(NSUInteger i=0;i<sizeof(digest);i++)printf("%02x",digest[i]);printf("\n");return 0;
    }
    BOOL thin=argc==4&&!strcmp(argv[1],"thin"),replace=argc==5&&!strcmp(argv[1],"replace-x86_64");
    BOOL compact=argc==4&&!strcmp(argv[1],"compact-linkedit");
    BOOL bigsurGL=argc==4&&!strcmp(argv[1],"prepare-bigsur-gl");
    BOOL bigsurMetal=argc==4&&!strcmp(argv[1],"prepare-bigsur-metal");
    BOOL merge=argc==5&&!strcmp(argv[1],"merge-x86_64");
    BOOL native32=argc==4&&!strcmp(argv[1],"opengl-native32");
    BOOL stage=argc>=4&&!strcmp(argv[1],"stage-opengl");
    if(!thin&&!replace&&!native32&&!merge&&!stage&&!compact&&!bigsurGL&&!bigsurMetal){fprintf(stderr,"usage: guest-macho thin INPUT NEW_OUTPUT | prepare-bigsur-gl INPUT NEW_OUTPUT | prepare-bigsur-metal INPUT NEW_OUTPUT | compact-linkedit INPUT NEW_OUTPUT | opengl-native32 INPUT NEW_OUTPUT | replace-x86_64 ORIGINAL REPLACEMENT NEW_OUTPUT | merge-x86_64 NATIVE REPLACEMENT NEW_OUTPUT | stage-opengl GUEST_FRAMEWORK STAGED_FRAMEWORK [NATIVE_BACKUPS...]\n");return 64;}
    @try {
        if(stage) {
            NSMutableArray *backups=[NSMutableArray array];
            for(int i=4;i<argc;i++)[backups addObject:@(argv[i])];
            StageOpenGL(@(argv[2]),@(argv[3]),backups);return 0;
        }
        NSError *error=nil;NSData *input=[NSData dataWithContentsOfFile:@(argv[2]) options:NSDataReadingMappedIfSafe error:&error];
        Check(input!=nil,error.localizedDescription?:@"Cannot read input");
        NSData *output=nil;
        if(bigsurGL||bigsurMetal)output=PrepareBigSurBundle(input,bigsurMetal);
        else if(compact)output=CompactLinkedit(input);
        else if(native32)output=NativeOpenGL32(input);
        else if(thin)output=Thin(input);
        else {
            NSData *replacement=[NSData dataWithContentsOfFile:@(argv[3]) options:NSDataReadingMappedIfSafe error:&error];
            Check(replacement!=nil,error.localizedDescription?:@"Cannot read replacement");output=Replace(input,replacement,merge);
        }
        int fd=open(argv[argc-1],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0755);
        Check(fd>=0,@"Cannot create new output (existing files are never overwritten)");
        const uint8_t *p=output.bytes;NSUInteger left=output.length;BOOL ok=YES;
        while(left){ssize_t n=write(fd,p,left);if(n<0&&errno==EINTR)continue;if(n<=0){ok=NO;break;}p+=n;left-=n;}
        BOOL synced=ok&&fsync(fd)==0;int closed=close(fd);
        Check(synced&&closed==0,@"Output write failed; partial staging file retained");return 0;
    } @catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
}}
