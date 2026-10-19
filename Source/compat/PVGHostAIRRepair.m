/* Local host compatibility experiment. Invoked once per library ingestion.
 * No guest ABI/object layouts, shader instruction changes, or per-draw work.
 * Currently accepts only the single-function MTLB container emitted by PVG.
 */
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <pthread.h>
#include "PVGAIRMetadata.h"

/* Process-local: LLVM is loaded once and repair rules cannot change during
 * the helper's lifetime. Key the complete container, not merely its bitcode.
 * Keep parsing outside the lock; duplicate simultaneous misses are harmless.
 * FIFO limits both result bytes and entry count, including negative results. */
static pthread_mutex_t repairCacheLock = PTHREAD_MUTEX_INITIALIZER;
static NSMutableDictionary *repairCache;
static NSMutableArray *repairOrder;
static size_t repairCacheBytes;
static id cachedRepair(NSData *key)
{
    pthread_mutex_lock(&repairCacheLock);
    id value = [[repairCache objectForKey:key] retain];
    pthread_mutex_unlock(&repairCacheLock);
    return [value autorelease];
}
static void rememberRepair(NSData *key, NSData *bytes)
{
    const size_t limit = 32 * 1024 * 1024;
    if (bytes.length > limit) return;
    pthread_mutex_lock(&repairCacheLock);
    if (!repairCache) {
        repairCache = [[NSMutableDictionary alloc] init];
        repairOrder = [[NSMutableArray alloc] init];
    }
    if (![repairCache objectForKey:key]) {
        while (repairOrder.count >= 128 || repairCacheBytes + bytes.length > limit) {
            NSData *oldKey = [repairOrder objectAtIndex:0];
            id old = [repairCache objectForKey:oldKey];
            if ([old isKindOfClass:[NSData class]]) repairCacheBytes -= [old length];
            [repairCache removeObjectForKey:oldKey];
            [repairOrder removeObjectAtIndex:0];
        }
        [repairCache setObject:bytes ?: (id)[NSNull null] forKey:key];
        [repairOrder addObject:key];
        repairCacheBytes += bytes.length;
    }
    pthread_mutex_unlock(&repairCacheLock);
}
static dispatch_data_t retainedDispatchBytes(NSData *bytes)
{
    NSData *owner = [bytes retain];
    dispatch_data_t data = dispatch_data_create(owner.bytes, owner.length,
        dispatch_get_global_queue(0, 0), ^{ [owner release]; });
    if (!data) [owner release];
    return data;
}

static uint64_t u64(const uint8_t *p) { uint64_t v; memcpy(&v,p,8); return v; }
static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint16_t u16(const uint8_t *p) { uint16_t v; memcpy(&v,p,2); return v; }
static void put64(uint8_t *p, uint64_t v) { memcpy(p,&v,8); }

/* Returns a retained replacement, or NULL to preserve the original bytes. */
dispatch_data_t PVGHostRepairLibrary(dispatch_data_t data) {
    const char *path=getenv("PVG_HOST_AIR_LLVM");
    if(!path || !*path) return NULL;
    const void *raw=NULL; size_t size=0;
    dispatch_data_t mapped=dispatch_data_create_map(data,&raw,&size);
    if(!mapped) return NULL;
    dispatch_data_t result=NULL;
    void *context=NULL,*module=NULL,*input=NULL,*output=NULL;
    NSData *cacheKey=nil;
    const uint8_t *b=raw;
    if(size<100 || size>16*1024*1024 || memcmp(b,"MTLB",4) || u64(b+16)!=size) goto done;
    uint64_t start=u64(b+72), length=u64(b+80), functions=u64(b+24);
    if(start>size || length!=size-start || length<20 || functions>start || start-functions<8) goto done;
    if(u32(b+functions)!=1 || u32(b+start)!=0x0b17c0de || u32(b+start+8)!=20) goto done;
    uint32_t bcsize=u32(b+start+12);
    if(bcsize>length-20) goto done;
    size_t cursor=functions+8, hash=0, mdsize=0;
    BOOL fragment=NO, ended=NO;
    while(cursor<=start && start-cursor>=4) {
        if(!memcmp(b+cursor,"ENDT",4)) { ended=YES; break; }
        if(start-cursor<6) goto done;
        unsigned n=u16(b+cursor+4);
        if(n>start-cursor-6) goto done;
        if(!memcmp(b+cursor,"HASH",4)) { if(hash || n!=32) goto done; hash=cursor+6; }
        if(!memcmp(b+cursor,"MDSZ",4)) { if(mdsize || n!=8) goto done; mdsize=cursor+6; }
        if(!memcmp(b+cursor,"TYPE",4)) fragment=n==1 && b[cursor+6]==1;
        cursor+=6+n;
    }
    if(!ended || !fragment || !hash || !mdsize || u64(b+mdsize)!=length) goto done;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(b+start,(CC_LONG)length,digest);
    if(memcmp(digest,b+hash,sizeof(digest))) goto done;
    const char *cacheSetting=getenv("PVG_HOST_AIR_CACHE");
    if(!cacheSetting || strcmp(cacheSetting,"0")) {
        CC_SHA256(b,(CC_LONG)size,digest);
        cacheKey=[NSData dataWithBytes:digest length:sizeof(digest)];
        id hit=cachedRepair(cacheKey);
        if(hit) {
            if(hit != [NSNull null]) result=retainedDispatchBytes(hit);
            if(result || hit == [NSNull null]) goto done;
        }
    }

    static void *image;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        image=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        if(!image) fprintf(stderr,"PVG host AIR LLVM unavailable: %s\n",dlerror());
    });
    if(!image) goto done;
#define API(name, ret, args) ret (*name) args=dlsym(image,#name); if(!name) goto done
    API(LLVMContextCreate,void *,(void));
    API(LLVMContextDispose,void,(void *));
    API(LLVMCreateMemoryBufferWithMemoryRangeCopy,void *,(const char *,size_t,const char *));
    API(LLVMParseBitcodeInContext2,int,(void *,void *,void **));
    API(LLVMDisposeMemoryBuffer,void,(void *));
    API(LLVMDisposeModule,void,(void *));
    API(LLVMWriteBitcodeToMemoryBuffer,void *,(void *));
    API(LLVMGetBufferStart,const char *,(void *));
    API(LLVMGetBufferSize,size_t,(void *));
    context=LLVMContextCreate();
    input=LLVMCreateMemoryBufferWithMemoryRangeCopy((const char *)b+start+20,bcsize,"PVG host AIR");
    if(!context || !input || LLVMParseBitcodeInContext2(context,input,&module)) goto cleanup;
    int changed=PVGRepairAIRFragmentLocations(image,module,context);
    if(changed==0 && cacheKey) rememberRepair(cacheKey,nil);
    if(changed<=0) goto cleanup;
    output=LLVMWriteBitcodeToMemoryBuffer(module);
    if(!output) goto cleanup;
    const uint8_t *bc=(const uint8_t *)LLVMGetBufferStart(output);
    size_t count=LLVMGetBufferSize(output);
    if(count>=20 && u32(bc)==0x0b17c0de) {
        uint32_t off=u32(bc+8), len=u32(bc+12);
        if(off>count || len>count-off) goto cleanup;
        bc+=off; count=len;
    }
    if(count<4 || count>16*1024*1024 || memcmp(bc,"BC\xc0\xde",4)) goto cleanup;
    size_t newLength=(20+count+15)&~(size_t)15;
    uint8_t *copy=calloc(1,start+newLength);
    if(!copy) goto cleanup;
    memcpy(copy,b,start+20);
    uint32_t newCount=(uint32_t)count;
    memcpy(copy+start+12,&newCount,4);
    memcpy(copy+start+20,bc,count);
    put64(copy+16,start+newLength); put64(copy+80,newLength); put64(copy+mdsize,newLength);
    CC_SHA256(copy+start,(CC_LONG)newLength,copy+hash);
    result=dispatch_data_create(copy,start+newLength,dispatch_get_global_queue(0,0),DISPATCH_DATA_DESTRUCTOR_FREE);
    if(!result) { free(copy); goto cleanup; }
    if(result && cacheKey) rememberRepair(cacheKey,
        [NSData dataWithBytes:copy length:start+newLength]);
    if(getenv("PVG_TRACE_HOST_AIR"))
        fprintf(stderr,"PVG host AIR repaired fragment inputs=%d\n",changed);
cleanup:
    if(output) LLVMDisposeMemoryBuffer(output);
    if(module) LLVMDisposeModule(module);
    if(input) LLVMDisposeMemoryBuffer(input);
    if(context) LLVMContextDispose(context);
done:
    dispatch_release(mapped);
    return result;
}
