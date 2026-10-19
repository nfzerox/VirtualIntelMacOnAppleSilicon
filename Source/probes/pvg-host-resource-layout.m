// Read-only host runtime metadata; does not create a device or alter methods.
#define main PVGGuestLayoutMain
#include "metal-class-layout.m"
#undef main
int main(void) {
    @autoreleasepool {
        const char *path=getenv("PVG_PROBE_FRAMEWORK");
        if(!path)path="/System/Library/Frameworks/ParavirtualizedGraphics.framework/ParavirtualizedGraphics";
        if(!dlopen(path,RTLD_NOW|RTLD_LOCAL)) {
            fprintf(stderr,"%s\n",dlerror());return 1;
        }
        Dl_info origin;
        IMP anchor = class_getMethodImplementation(NSClassFromString(@"PGDeviceDescriptor"),
                                                   sel_registerName("init"));
        if (dladdr((void *)anchor, &origin))
            printf("image-base=%p path=%s\n", origin.dli_fbase, origin.dli_fname);
        const char *names[]={"PGDeviceDescriptor", "PGDisplayDescriptor", "_PGDisplay", "_PGDisplayNub", "PGMemoryMapDescriptor",
                            "PGMemoryMap", "PGTask", "_PGDevice",
                            "PGLocalTask", "PGSharedBufferResource", "PGBufferResource",
                            "PGBackingResource", "PGResource","PGTextureResource","PGSinglePlaneTextureResource",
                            "PGPrivateBufferResource", "PGSinglePlaneSharedTextureBackingResource",
                            "PGDualPlaneSharedTextureBackingResource",
                            "PGPageTableIterator", "PGBaseTask", "PGFIFO", "PGChildFIFO"};
        for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
            dumpClass(names[i]);
            dumpMethodsContaining(objc_getClass(names[i]),"");
        }
        unsigned count=0;Class *classes=objc_copyClassList(&count);
        for(unsigned i=0;i<count;i++) {
            const char *name=class_getName(classes[i]);
            if(strncmp(name,"PG",2))continue;
            printf("available-class=%s\n", name);
            unsigned methodsCount=0;Method *methods=class_copyMethodList(classes[i],&methodsCount);
            for(unsigned j=0;j<methodsCount;j++) {
                const char *selector=sel_getName(method_getName(methods[j]));
                if(strcasestr(selector,"download")||strcasestr(selector,"synchronize")||
                   strcasestr(selector,"createResource")||strcasestr(selector,"newTexture")||
                   strcasestr(selector,"createTexture")||strcasestr(selector,"registerResource")||
                   strcasestr(selector,"prepareResource")||strcasestr(selector,"executeCommand"))
                    printf("transfer class=%s method=%s types=%s imp=%p\n",name,selector,
                           method_getTypeEncoding(methods[j]),method_getImplementation(methods[j]));
            }
            free(methods);
        }
        free(classes);
    }
    return 0;
}
