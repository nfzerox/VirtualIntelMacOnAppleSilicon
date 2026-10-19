// Keep injected RIP-relative instructions valid when update_dyld_shared_cache
// separates __TEXT and __DATA. Format follows Apple's mach_o/SplitSeg.cpp and
// mach_o_writer/SplitSegWriter.cpp, vendored under vendor/source/dyld.
static uint64_t CoreULEB(const uint8_t **cursor,const uint8_t *end) {
    uint64_t value=0;unsigned shift=0;
    for(;;){Require(*cursor<end&&shift<64,@"Malformed split-segment ULEB");uint8_t b=*(*cursor)++;
        Require(shift!=63||(b&0x7e)==0,@"Split-segment ULEB overflow");value|=(uint64_t)(b&127)<<shift;
        if(!(b&128))return value;shift+=7;}
}
static void CorePutULEB(NSMutableData *data,uint64_t value) {
    do{uint8_t b=value&127;value>>=7;if(value)b|=128;[data appendBytes:&b length:1];}while(value);
}
static void CoreDropSignature(PVGCoreImage *image,NSUInteger command) {
    struct mach_header_64 *h=image.data.mutableBytes;
    NSUInteger end=sizeof(*h)+h->sizeofcmds,size=sizeof(struct linkedit_data_command);
    Require(command>=sizeof(*h)&&Bounds(command,size,end),@"Invalid signature command");
    uint8_t *bytes=image.data.mutableBytes;memmove(bytes+command,bytes+command+size,end-command-size);
    memset(bytes+end-size,0,size);h->ncmds--;h->sizeofcmds-=size;
}
static void CorePublishSplitSeg(PVGCoreImage *image,NSData *encoded,NSUInteger splitCommand,struct linkedit_data_command split,NSUInteger signatureCommand,struct linkedit_data_command signature) {
    NSUInteger allocation=(encoded.length+7)&~(NSUInteger)7,oldEnd=split.dataoff+split.datasize;
    NSUInteger growth=allocation>split.datasize?allocation-split.datasize:0;
    Require(signature.dataoff>=oldEnd&&Bounds(signature.dataoff,signature.datasize,image.data.length)&&growth<signature.datasize,@"Insufficient LINKEDIT allocation");
    uint8_t *bytes=image.data.mutableBytes;
    memmove(bytes+oldEnd+growth,bytes+oldEnd,signature.dataoff-oldEnd);
    memset(bytes+signature.dataoff+growth,0,image.data.length-signature.dataoff-growth);
    memset(bytes+split.dataoff,0,split.datasize+growth);memcpy(bytes+split.dataoff,encoded.bytes,encoded.length);
    // Older dyld requires LINKEDIT's canonical order. Grow the existing table
    // in place and relocate following tables, rather than appending it.
    void (^adjust)(uint32_t *)=^(uint32_t *offset){if(*offset>=oldEnd&&*offset<signature.dataoff)*offset+=(uint32_t)growth;};
    struct mach_header_64 *h=image.data.mutableBytes;NSUInteger p=sizeof(*h);
    for(uint32_t i=0;i<h->ncmds;i++) {
        struct load_command *lc=(void *)(bytes+p);
        if(lc->cmd==LC_SYMTAB){struct symtab_command *s=(void *)lc;adjust(&s->symoff);adjust(&s->stroff);}
        else if(lc->cmd==LC_DYSYMTAB){struct dysymtab_command *s=(void *)lc;adjust(&s->tocoff);adjust(&s->modtaboff);adjust(&s->extrefsymoff);adjust(&s->indirectsymoff);adjust(&s->extreloff);adjust(&s->locreloff);}
        else if(lc->cmd==LC_DYLD_INFO||lc->cmd==LC_DYLD_INFO_ONLY){struct dyld_info_command *s=(void *)lc;adjust(&s->rebase_off);adjust(&s->bind_off);adjust(&s->weak_bind_off);adjust(&s->lazy_bind_off);adjust(&s->export_off);}
        else if(lc->cmd==LC_FUNCTION_STARTS||lc->cmd==LC_DATA_IN_CODE||lc->cmd==LC_DYLIB_CODE_SIGN_DRS){struct linkedit_data_command *s=(void *)lc;adjust(&s->dataoff);}
        p+=lc->cmdsize;
    }
    split.datasize=(uint32_t)(split.datasize+growth);memcpy(bytes+splitCommand,&split,sizeof(split));CoreDropSignature(image,signatureCommand);
}
static void CoreFixSplitSeg(PVGCoreImage *image,NSArray<NSArray<NSNumber *> *> *references,NSArray<NSValue *> *replacedRanges) {
    BOOL (^removed)(uint64_t)=^BOOL(uint64_t address){for(NSValue *value in replacedRanges)if(NSLocationInRange(address,value.rangeValue))return YES;return NO;};
    NSMutableArray *sections=[NSMutableArray arrayWithObject:@[@(image.textSegment.vmaddr),@(image.firstSection)]];
    struct mach_header_64 h;memcpy(&h,image.data.bytes,sizeof(h));
    NSUInteger p=sizeof(h),splitCommand=0,signatureCommand=0;
    struct linkedit_data_command split={0},signature={0};
    for(uint32_t i=0;i<h.ncmds;i++) {
        struct load_command lc;memcpy(&lc,(const uint8_t *)image.data.bytes+p,sizeof(lc));
        if(lc.cmd==LC_SEGMENT_64){struct segment_command_64 seg;memcpy(&seg,(const uint8_t *)image.data.bytes+p,sizeof(seg));
            for(uint32_t j=0;j<seg.nsects;j++){struct section_64 s;memcpy(&s,(const uint8_t *)image.data.bytes+p+sizeof(seg)+j*sizeof(s),sizeof(s));[sections addObject:@[@(s.addr),@(s.size)]];}}
        if(lc.cmd==LC_SEGMENT_SPLIT_INFO){Require(!splitCommand,@"Duplicate split-segment information");splitCommand=p;memcpy(&split,(const uint8_t *)image.data.bytes+p,sizeof(split));}
        if(lc.cmd==LC_CODE_SIGNATURE){Require(!signatureCommand,@"Duplicate signature");signatureCommand=p;memcpy(&signature,(const uint8_t *)image.data.bytes+p,sizeof(signature));}
        p+=lc.cmdsize;
    }
    Require(splitCommand&&signatureCommand&&Bounds(split.dataoff,split.datasize,image.data.length)&&split.datasize>1,@"Missing split-segment metadata");
    const uint8_t *cursor=(const uint8_t *)image.data.bytes+split.dataoff,*end=cursor+split.datasize;
    if(*cursor!=0x7f) {
        // Mojave's x86_64 linker uses v1: kind followed by sorted offsets
        // relative to __TEXT, terminated by zero. Kind 1 adjusts RIP-relative
        // references from text to data by the cache's code-to-data delta.
        NSMutableDictionary<NSNumber *,NSMutableSet<NSNumber *> *> *kinds=[NSMutableDictionary dictionary];BOOL changed=NO;
        while(cursor<end&&*cursor) {
            uint8_t kind=*cursor++;Require(kind==1||kind==2,@"Unsupported x86_64 split-segment kind");
            if(!kinds[@(kind)])kinds[@(kind)]=[NSMutableSet set];uint64_t offset=0;
            for(;;){uint64_t delta=CoreULEB(&cursor,end);if(!delta)break;Require(offset<=UINT64_MAX-delta,@"Split-segment overflow");offset+=delta;
                uint64_t address=image.textSegment.vmaddr+offset;
                if(!removed(address))[kinds[@(kind)] addObject:@(offset)];else changed=YES;}
        }
        Require(cursor<end,@"Unterminated split-segment metadata");
        if(!kinds[@1])kinds[@1]=[NSMutableSet set];
        for(NSArray *reference in references) {
            uint64_t source=[reference[0] unsignedLongLongValue],target=[reference[1] unsignedLongLongValue];
            if(target>=image.textSegment.vmaddr&&target-image.textSegment.vmaddr<image.textSegment.vmsize)continue;
            BOOL data=NO;for(NSValue *value in image.segments){struct segment_command_64 s;[value getValue:&s size:sizeof(s)];if(!strncmp(s.segname,"__DATA",16)&&target>=s.vmaddr&&target-s.vmaddr<s.vmsize)data=YES;}
            Require(data&&source>=image.textSegment.vmaddr&&source-image.textSegment.vmaddr<image.textSegment.vmsize,@"Unsupported v1 injected cross-segment reference");
            NSNumber *offset=@(source-image.textSegment.vmaddr);if(![kinds[@1] containsObject:offset]){[kinds[@1] addObject:offset];changed=YES;}
        }
        if(!changed)return;
        NSMutableData *encoded=[NSMutableData data];
        for(NSNumber *key in [kinds.allKeys sortedArrayUsingSelector:@selector(compare:)]){uint8_t kind=key.unsignedCharValue;[encoded appendBytes:&kind length:1];uint64_t previous=0;
            for(NSNumber *location in [kinds[key].allObjects sortedArrayUsingSelector:@selector(compare:)]){uint64_t offset=location.unsignedLongLongValue;Require(offset>previous,@"Duplicate split-segment offset");CorePutULEB(encoded,offset-previous);previous=offset;}CorePutULEB(encoded,0);}
        CorePutULEB(encoded,0);
        CorePublishSplitSeg(image,encoded,splitCommand,split,signatureCommand,signature);return;
    }
    Require(*cursor++==0x7f,@"Unsupported split-segment format");
    NSMutableArray *records=[NSMutableArray array];
    uint64_t groups=CoreULEB(&cursor,end);Require(groups<=65536,@"Too many split-segment groups");
    for(uint64_t i=0;i<groups;i++) {
        uint64_t from=CoreULEB(&cursor,end),to=CoreULEB(&cursor,end),count=CoreULEB(&cursor,end),target=0;
        Require(from<sections.count&&to<sections.count&&count<=split.datasize,@"Invalid split-segment section");
        for(uint64_t j=0;j<count;j++) {
            uint64_t delta=CoreULEB(&cursor,end);Require(target<=UINT64_MAX-delta,@"Target overflow");target+=delta;
            uint64_t kinds=CoreULEB(&cursor,end);Require(kinds<=14,@"Invalid split-segment kind count");
            for(uint64_t k=0;k<kinds;k++) {
                uint64_t kind=CoreULEB(&cursor,end),locations=CoreULEB(&cursor,end),source=0;
                Require(kind<=13&&locations<=split.datasize,@"Invalid split-segment reference");
                for(uint64_t n=0;n<locations;n++) {
                    delta=CoreULEB(&cursor,end);Require(source<=UINT64_MAX-delta,@"Source overflow");source+=delta;
                    uint64_t address=[sections[from][0] unsignedLongLongValue]+source;
                    // The old detector's first LEA was replaced by the jump.
                    // Discard its now-dead relocation rather than adjusting NOPs.
                    if(removed(address))continue;
                    [records addObject:@[@(from),@(to),@(target),@(kind),@(source)]];
                }
            }
        }
    }
    for(NSArray *reference in references) {
        uint64_t source=[reference[0] unsignedLongLongValue],target=[reference[1] unsignedLongLongValue];
        NSUInteger from=NSNotFound,to=NSNotFound;
        for(NSUInteger i=0;i<sections.count;i++) {
            uint64_t base=[sections[i][0] unsignedLongLongValue],size=[sections[i][1] unsignedLongLongValue];
            if(source>=base&&source-base<size)from=i;
            if(target>=base&&target-base<size)to=i;
        }
        Require(from!=NSNotFound&&to!=NSNotFound,@"Injected reference outside sections");
        NSArray *record=@[@(from),@(to),@(target-[sections[to][0] unsignedLongLongValue]),@3,@(source-[sections[from][0] unsignedLongLongValue])];
        if(![records containsObject:record])[records addObject:record];
    }
    // Group and sort exactly as the linker does.
    [records sortUsingComparator:^NSComparisonResult(NSArray *a,NSArray *b){for(NSUInteger i=0;i<5;i++){NSComparisonResult r=[a[i] compare:b[i]];if(r!=NSOrderedSame)return r;}return NSOrderedSame;}];
    NSMutableArray *pairs=[NSMutableArray array];
    for(NSArray *r in records){NSMutableArray *pair=pairs.lastObject;NSArray *last=pair.lastObject;if(!last||![last[0] isEqual:r[0]]||![last[1] isEqual:r[1]]){pair=[NSMutableArray array];[pairs addObject:pair];}[pair addObject:r];}
    NSMutableData *encoded=[NSMutableData data];uint8_t marker=0x7f;[encoded appendBytes:&marker length:1];CorePutULEB(encoded,pairs.count);
    for(NSArray *pair in pairs){NSArray *first=pair.firstObject;CorePutULEB(encoded,[first[0] unsignedLongLongValue]);CorePutULEB(encoded,[first[1] unsignedLongLongValue]);
        NSMutableArray *targets=[NSMutableArray array];
        for(NSArray *r in pair){NSMutableArray *target=targets.lastObject;NSArray *last=target.lastObject;if(!last||![last[2] isEqual:r[2]]){target=[NSMutableArray array];[targets addObject:target];}[target addObject:r];}
        CorePutULEB(encoded,targets.count);uint64_t lastTarget=0;
        for(NSArray *target in targets){first=target.firstObject;uint64_t targetOffset=[first[2] unsignedLongLongValue];CorePutULEB(encoded,targetOffset-lastTarget);lastTarget=targetOffset;
            NSMutableArray *kinds=[NSMutableArray array];
            for(NSArray *r in target){NSMutableArray *kind=kinds.lastObject;NSArray *last=kind.lastObject;if(!last||![last[3] isEqual:r[3]]){kind=[NSMutableArray array];[kinds addObject:kind];}[kind addObject:r];}
            CorePutULEB(encoded,kinds.count);
            for(NSArray *kind in kinds){first=kind.firstObject;CorePutULEB(encoded,[first[3] unsignedLongLongValue]);CorePutULEB(encoded,kind.count);uint64_t lastSource=0;
                for(NSArray *r in kind){uint64_t source=[r[4] unsignedLongLongValue];CorePutULEB(encoded,source-lastSource);lastSource=source;}}
        }
    }
    CorePublishSplitSeg(image,encoded,splitCommand,split,signatureCommand,signature);
}
