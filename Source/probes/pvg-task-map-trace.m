// Local diagnostic only. Link into a temporary PVG helper, never the app runtime.
// Preserves the donor's mapping and exception behavior; at most eight reports.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <execinfo.h>

static IMP originalMap;
static uint64_t scalar(id object,const char *name,size_t size) {
    Ivar ivar=class_getInstanceVariable(object_getClass(object),name);
    uint64_t result=0;
    if(ivar&&size<=sizeof(result))memcpy(&result,(char *)object+ivar_getOffset(ivar),size);
    return result;
}
static void traceMap(id task,SEL selector,uint64_t offset,uint64_t length) {
    @try {
        ((void(*)(id,SEL,uint64_t,uint64_t))originalMap)(task,selector,offset,length);
    } @catch(NSException *exception) {
        static unsigned reports;
        if(__atomic_fetch_add(&reports,1,__ATOMIC_RELAXED)<8) {
            fprintf(stderr,"PVG map trace: %s offset=0x%llx length=0x%llx taskLength=0x%llx\n",
                exception.reason.UTF8String,offset,length,scalar(task,"_length",8));
            id iterator=(id)(uintptr_t)scalar(task,"_pageTableIterator",sizeof(id));
            uint64_t root=scalar(iterator,"_root",8);
            unsigned depth=(unsigned)scalar(iterator,"_depth",4);
            fprintf(stderr,"PVG map trace: root=0x%llx depth=%u allowHoles=%llu\n",
                root,depth,scalar(iterator,"_allowHoles",1));
            void (^readMemory)(uint64_t,uint32_t,void *)=(id)(uintptr_t)scalar(iterator,"_read",sizeof(id));
            // Geometry verified from this donor's visitPagesInRange disassembly.
            uint64_t table=root&UINT64_C(0xffffffff000);
            for(unsigned level=0;readMemory&&table&&level<depth&&depth<=4;level++) {
                unsigned index=(unsigned)((offset>>(12+10*(depth-level-1)))&1023);
                uint32_t entry=0;
                readMemory(table+index*4,4,&entry);
                fprintf(stderr,"PVG map trace: level=%u table=0x%llx index=%u entry=0x%x\n",level,table,index,entry);
                table=(uint64_t)(entry&0x7fffffff)<<12;
            }
            void *stack[24];int count=backtrace(stack,24);backtrace_symbols_fd(stack,count,2);
        }
        @throw;
    }
}
__attribute__((constructor)) static void installTrace(void) {@autoreleasepool {
    if(!dlopen("/System/Library/Frameworks/ParavirtualizedGraphics.framework/ParavirtualizedGraphics",RTLD_NOW|RTLD_LOCAL))return;
    Class task=NSClassFromString(@"PGTask");
    SEL selector=sel_registerName("mapMemoryInternalAtOffset:length:");
    Method method=class_getInstanceMethod(task,selector);
    if(!method)return;
    originalMap=method_getImplementation(method);
    class_replaceMethod(task,selector,(IMP)traceMap,method_getTypeEncoding(method));
}}
