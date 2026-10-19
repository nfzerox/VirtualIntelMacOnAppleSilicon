// Research-only Mojave final-release adapter, loaded as a normal GLD dependency.
// No global DYLD_INSERT_LIBRARIES and no on-disk system framework modification.
#define GL_SILENCE_DEPRECATION
#import <Foundation/Foundation.h>
#include <OpenGL/OpenGL.h>
#include <Block.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/i386/thread_status.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct Callback { void (^block)(CGLPBufferObj); struct Callback *next; } Callback;
static Callback *callbacks;
static pthread_mutex_t callbackLock=PTHREAD_MUTEX_INITIALIZER;
static CGLError (*nativeCleanup)(CGLPBufferObj);
static CGLError cleanup(CGLPBufferObj object) {
    pthread_mutex_lock(&callbackLock);
    // Matches Catalina's lock-held callback semantics: callbacks must not
    // register or unregister recursively. Invocation occurs only at refcount 0.
    for(Callback *c=callbacks;c;c=c->next)c->block(object);
    pthread_mutex_unlock(&callbackLock);
    return nativeCleanup(object);
}
static void absoluteJump(uint8_t *code,const void *target) {
    const uint8_t op[]={0xff,0x25,0,0,0,0};memcpy(code,op,6);memcpy(code+6,&target,8);
}
static uint8_t *releaseTarget(void *symbol,BOOL destroy) {
    const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x48,0x85,0xff,0x74,0x0c,
        0xf0,0xff,0x4f,0x20,0x75,0x06,0x5d,0xe9};
    uint8_t *code=symbol;if(!code||memcmp(code,prefix,sizeof(prefix)))return NULL;
    int32_t displacement;memcpy(&displacement,code+17,4);
    if(destroy) {
        const uint8_t end[]={0xb8,0x1e,0x27,0,0,0x5d,0xc3};
        if(memcmp(code+21,end,sizeof(end)))return NULL;
    } else if(code[21]!=0x5d||code[22]!=0xc3)return NULL;
    return code+21+displacement;
}
static BOOL installCleanupObserver(void *image) {
    uint8_t *target=releaseTarget(dlsym(image,"CGLDestroyPBuffer"),YES);
    if(!target||target!=releaseTarget(dlsym(image,"CGLReleasePBuffer"),NO))return NO;
    // Native cleanup starts with a nine-byte position-independent prologue
    // and a seven-byte RIP-relative LEA. Relocate exactly those instructions.
    const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8d,0x05};
    if(memcmp(target,prefix,sizeof(prefix)))return NO;
    uint8_t *trampoline=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(trampoline==MAP_FAILED)return NO;
    memcpy(trampoline,target,9);trampoline[9]=0x48;trampoline[10]=0xb8;
    int32_t displacement;memcpy(&displacement,target+12,4);
    void *address=target+16+displacement;memcpy(trampoline+11,&address,8);
    absoluteJump(trampoline+19,target+16);
    if(mprotect(trampoline,4096,PROT_READ|PROT_EXEC)){munmap(trampoline,4096);return NO;}
    uint8_t patch[16];memset(patch,0x90,sizeof(patch));absoluteJump(patch,cleanup);
    thread_act_array_t threads=NULL;mach_msg_type_number_t count=0;
    if(task_threads(mach_task_self(),&threads,&count)!=KERN_SUCCESS){munmap(trampoline,4096);return NO;}
    thread_t self=mach_thread_self();unsigned suspended=0;BOOL ok=YES;
    for(unsigned i=0;i<count;i++) {
        if(threads[i]==self)continue;
        if(thread_suspend(threads[i])!=KERN_SUCCESS){ok=NO;break;}
        suspended=i+1;
        x86_thread_state64_t state;mach_msg_type_number_t size=x86_THREAD_STATE64_COUNT;
        if(thread_get_state(threads[i],x86_THREAD_STATE64,(thread_state_t)&state,&size)!=KERN_SUCCESS||
            (state.__rip>=(uintptr_t)target&&state.__rip<(uintptr_t)target+16)){ok=NO;break;}
    }
    mach_vm_address_t page=(uintptr_t)target&~((mach_vm_address_t)vm_page_size-1);
    mach_vm_size_t bytes=(((uintptr_t)target+16+vm_page_size-1)&~((uintptr_t)vm_page_size-1))-page;
    if(ok)ok=mach_vm_protect(mach_task_self(),page,bytes,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)==KERN_SUCCESS;
    if(ok) {
        nativeCleanup=(void *)trampoline;memcpy(target,patch,16);
        __builtin___clear_cache((char *)target,(char *)target+16);
        // Do not resume execution with an NX text page after a partial patch.
        if(mach_vm_protect(mach_task_self(),page,bytes,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS)_exit(70);
    }
    for(unsigned i=0;i<suspended;i++)if(threads[i]!=self)thread_resume(threads[i]);
    for(unsigned i=0;i<count;i++)mach_port_deallocate(mach_task_self(),threads[i]);
    mach_port_deallocate(mach_task_self(),self);
    vm_deallocate(mach_task_self(),(vm_address_t)threads,count*sizeof(thread_t));
    if(!ok)munmap(trampoline,4096);
    return ok;
}
void *CGLRegisterDestroyPBufferCallback(void (^block)(CGLPBufferObj)) {
    static void *image;static void *(*native)(void (^)(CGLPBufferObj));static dispatch_once_t once;
    dispatch_once(&once,^{
        image=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);
        native=dlsym(image,"CGLRegisterDestroyPBufferCallback");
        if(!native&&(!getenv("PVG_GL_TEST_INTEL_PROFILE")||!installCleanupObserver(image))) {
            fprintf(stderr,"PVG GL: unsupported/unavailable Mojave final-release adapter\n");abort();
        }
    });
    if(native)return native(block);
    Callback *entry=calloc(1,sizeof(*entry));if(!entry)abort();entry->block=Block_copy(block);
    pthread_mutex_lock(&callbackLock);entry->next=callbacks;callbacks=entry;pthread_mutex_unlock(&callbackLock);
    return entry;
}
void CGLUnregisterDestroyPBufferCallback(void *token) {
    static void (*native)(void *);static dispatch_once_t once;
    dispatch_once(&once,^{void *image=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);native=dlsym(image,"CGLUnregisterDestroyPBufferCallback");});
    if(native){native(token);return;}if(!token)return;
    pthread_mutex_lock(&callbackLock);Callback **link=&callbacks;
    while(*link&&*link!=token)link=&(*link)->next;
    Callback *entry=*link;if(entry)*link=entry->next;pthread_mutex_unlock(&callbackLock);
    if(entry){Block_release(entry->block);free(entry);}
}
