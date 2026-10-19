#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <os/log.h>

// Mirrors donor setWritability's argument.type/index/access semantics, but
// never asks Catalina to decode an AppleParavirtFunction's newer reflection.
// Native AIR <= 2.2 is handled by Catalina's native reflection reader.
typedef struct { uint32_t buffers[8], textures[8]; } PVGAccessMasks;
static char accessKey;

static NSData *accessMasks(id function) {
    NSData *cached = objc_getAssociatedObject(function, &accessKey);
    if (cached) return cached.length == sizeof(PVGAccessMasks) ? cached : nil;
    if (object_getClass(function) != objc_getClass("_MTLFunctionInternal")) return nil;
    SEL dataSelector = sel_registerName("functionData");
    if (![function respondsToSelector:dataSelector]) return nil;
    const uint8_t *data = ((void *(*)(id,SEL))objc_msgSend)(function,dataSelector);
    if (!data || *(const uint16_t *)(data+0x28)>2 ||
        (*(const uint16_t *)(data+0x28)==2 && *(const uint16_t *)(data+0x2a)>2)) return nil;
    PVGAccessMasks masks = {0};
    BOOL valid = NO;
    @try {
        SEL selector=sel_registerName("reflectionWithOptions:");
        if ([function respondsToSelector:selector]) {
            id reflection=((id(*)(id,SEL,NSUInteger))objc_msgSend)(function,selector,0);
            id arguments = [reflection respondsToSelector:@selector(arguments)]
                ? ((id(*)(id,SEL))objc_msgSend)(reflection,@selector(arguments)) : nil;
            valid = [arguments isKindOfClass:[NSArray class]];
            for (MTLArgument *argument in arguments) {
                NSUInteger type=argument.type, index=argument.index, access=argument.access;
                if (type!=MTLArgumentTypeBuffer && type!=MTLArgumentTypeTexture) continue;
                if (index>=256 || access>MTLArgumentAccessWriteOnly) {valid=NO;break;}
                if (access!=MTLArgumentAccessReadOnly) {
                    uint32_t *bits=type==MTLArgumentTypeBuffer ? masks.buffers : masks.textures;
                    bits[index/32] |= UINT32_C(1)<<(index%32);
                }
            }
        }
    } @catch (NSException *exception) { valid=NO; }
    NSData *result=[NSData dataWithBytes:&masks length:valid ? sizeof(masks) : 0];
    objc_setAssociatedObject(function,&accessKey,result,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return valid ? result : nil;
}

void PVGSetWritability(id function,uint32_t *buffers,unsigned bufferCount,
                       uint32_t *textures,unsigned textureCount) {
    @autoreleasepool {
        NSData *data = getenv("PVG_CONSERVATIVE_WRITABILITY") ? nil : accessMasks(function);
        const PVGAccessMasks *masks=data.bytes;
        BOOL precise=masks && bufferCount<=256 && textureCount<=256;
        for(unsigned i=0;buffers && i<(bufferCount+31u)/32u;i++) {
            uint32_t bits=precise ? masks->buffers[i] : UINT32_MAX;
            unsigned remaining=bufferCount-i*32;
            if(precise && remaining<32)bits &= (UINT32_C(1)<<remaining)-1;
            buffers[i] |= bits;
        }
        for(unsigned i=0;textures && i<(textureCount+31u)/32u;i++) {
            uint32_t bits=precise ? masks->textures[i] : UINT32_MAX;
            unsigned remaining=textureCount-i*32;
            if(precise && remaining<32)bits &= (UINT32_C(1)<<remaining)-1;
            textures[i] |= bits;
        }
        if(getenv("PVG_TRACE_WRITABILITY"))
            fprintf(stderr,"PVG access name=%s precise=%d buffers=%08x textures=%08x\n",
                    [function respondsToSelector:@selector(name)] ? [[function name] UTF8String] : "(unknown)",precise,buffers && bufferCount?buffers[0]:0,textures && textureCount?textures[0]:0);
    }
}

void PVGInstallWritability(void) {
    static BOOL installed;
    if(installed || getenv("PVG_DISABLE_PRECISE_WRITABILITY")) return;
    Method method=class_getInstanceMethod(objc_getClass("AppleParavirtDevice"),sel_registerName("llvmVersion"));
    Dl_info info;
    if(!method || !dladdr((void *)method_getImplementation(method),&info) ||
       !info.dli_fname || !strstr(info.dli_fname,"AppleParavirtGPUMetal"))return;
    uint8_t *site=(uint8_t *)info.dli_fbase+0x34ec;
    // Exact conservative helper emitted by PVGSetWritability.s. Unknown donor
    // bytes keep the existing conservative behavior, rather than being patched.
    const uint8_t expected[]={0x48,0x85,0xf6,0x74,0x1d,0x89,0xd0,0x83,0xc0,0x1f,0xc1,0xe8,0x05,0x74,0x13,0x41};
    if(memcmp(site,expected,sizeof(expected))) {
        os_log_error(OS_LOG_DEFAULT,"PVG precise access: unknown helper; retaining conservative masks");
        return;
    }
    uint8_t jump[12]={0x48,0xb8};
    uintptr_t target=(uintptr_t)&PVGSetWritability;
    memcpy(jump+2,&target,8);jump[10]=0xff;jump[11]=0xe0;
    mach_vm_address_t page=(uintptr_t)site & ~((uintptr_t)vm_page_size-1);
    if(mach_vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) {
        os_log_error(OS_LOG_DEFAULT,"PVG precise access: COW unavailable; retaining conservative masks");
        return;
    }
    memcpy(site,jump,sizeof(jump));
    __builtin___clear_cache((char *)site,(char *)site+sizeof(jump));
    if(mach_vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS)abort();
    installed=YES;
    os_log(OS_LOG_DEFAULT,"PVG precise access installed");
}
