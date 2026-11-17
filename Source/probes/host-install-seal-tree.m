#import "../tools/HostInstall/VIMBigSurTree.h"
#ifdef VIM_SEAL_DIGEST_PROBE
#import "../tools/HostInstall/VIMBigSurSealTree.h"
#endif

// Check tree packing separately from filesystem reconstruction. The input
// is an ordered diagnostic stream: LE16 key and value lengths, then bytes.
// With a separate original digest database, input omits digest records.
// Neither replay is an input to the installer.
int main(int argc,char **argv) { @autoreleasepool {
    if(argc<2||argc>3||strlen(argv[1])!=64)return 64;
#ifdef VIM_SEAL_DIGEST_PROBE
    NSURL *database=argc==3?[NSURL fileURLWithPath:@(argv[2])]:nil;
    NSMutableArray<NSArray<NSData *> *> *metadata=database?[NSMutableArray array]:nil;
#else
    if(argc!=2)return 64;
#endif
    uint8_t digest[32];
    for(size_t i=0;i<sizeof(digest);i++) {
        char pair[3]={argv[1][2*i],argv[1][2*i+1],0},*end=NULL;
        long value=strtol(pair,&end,16);
        if(end!=pair+2||value<0||value>255)return 64;
        digest[i]=(uint8_t)value;
    }
    NSData *expected=[NSData dataWithBytes:digest length:sizeof(digest)];
    uint64_t count=0;
    BOOL (^discard)(uint64_t,NSData *)=^BOOL(__unused uint64_t relative,__unused NSData *block){return YES;};
    VIMBigSurTreeBuilder *builder=[[VIMBigSurTreeBuilder alloc] initWithSink:discard];
    for(;;) @autoreleasepool {
        uint8_t lengths[4],key[4096],value[4096];
        size_t bytes=fread(lengths,1,sizeof(lengths),stdin);
        if(!bytes&&feof(stdin))break;
        if(bytes!=sizeof(lengths))return 65;
        size_t k=lengths[0]|lengths[1]<<8,v=lengths[2]|lengths[3]<<8;
        if(!k||!v||k>sizeof(key)||v>sizeof(value)||fread(key,1,k,stdin)!=k||fread(value,1,v,stdin)!=v)
            return 65;
        NSData *keyData=[NSData dataWithBytes:key length:k],*valueData=[NSData dataWithBytes:value length:v];
#ifdef VIM_SEAL_DIGEST_PROBE
        if(metadata) {
            if(k<8||(key[7]>>4)==13)return 65;
            [metadata addObject:@[keyData,valueData]];
            count++;continue;
        }
#endif
        if(![builder appendKey:keyData value:valueData]) {
            fprintf(stderr,"Could not append ordered record %llu.\n",count);return 1;
        }
        count++;
    }
    NSError *error=nil;
    NSDictionary *verified=nil;
#ifdef VIM_SEAL_DIGEST_PROBE
    if(database)verified=VIMBuildBigSurTreeWithDigests(metadata,database,expected,discard,nil,&error);
    else
#endif
        verified=[builder finishWithRootHash:expected error:&error];
    if(!verified){fprintf(stderr,"%s (after %llu records)\n",error.localizedDescription.UTF8String,count);return 1;}
    printf("Matched original root: %llu records, %llu nodes.\n",
        [verified[@"RecordCount"] unsignedLongLongValue],[verified[@"NodeCount"] unsignedLongLongValue]);
    return 0;
}}
