#ifdef VIM_KEYNOTE_COMPATIBILITY_LIBRARY
#include "keynote-screen-compatibility.h"
__attribute__((visibility("default"))) int VIMKeynoteCompatibilityProbeApply(
    CFStringRef identifier,CFStringRef version,double appkit)
{ return VIMInstallKeynoteMissingScreenReset(identifier,version,appkit); }
#else
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static void ExistingReset(id receiver,SEL command)
{ (void)receiver;(void)command;++calls; }
int main(int argc,char **argv)
{
    @autoreleasepool {
        assert(argc==2 || argc==3);
        SEL reset=sel_registerName("_resetScreens");
        Class screen=NSScreen.class;
        int existing=argc==3&&!strcmp(argv[2],"existing");
        if(existing) {
            if(!class_getClassMethod(screen,reset))
                assert(class_addMethod(object_getClass(screen),reset,(IMP)ExistingReset,"v@:"));
        } else assert(!class_getClassMethod(screen,reset));
        void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(library);
        int (*apply)(CFStringRef,CFStringRef,double)=dlsym(library,"VIMKeynoteCompatibilityProbeApply");assert(apply);
        if(existing) {
            IMP prior=method_getImplementation(class_getClassMethod(screen,reset));
            assert(!apply(CFSTR("com.apple.iWork.Keynote"),CFSTR("5.3"),1671));
            assert(method_getImplementation(class_getClassMethod(screen,reset))==prior);
            if(prior==(IMP)ExistingReset){((void (*)(id,SEL))prior)(screen,reset);assert(calls==1);}
            dlclose(library);
            puts("PASS existing screen reset implementation is unchanged");return 0;
        }
        assert(!apply(CFSTR("com.apple.iWork.Pages"),CFSTR("4.3"),1671));
        assert(!apply(CFSTR("com.apple.iWork.Numbers"),CFSTR("2.3"),1671));
        assert(!apply(CFSTR("com.apple.iWork.Keynote"),CFSTR("6.0"),1671));
        assert(!apply(CFSTR("com.apple.iWork.Keynote"),NULL,1671));
        assert(!apply(NULL,CFSTR("5.3"),1671));
        assert(!apply(CFSTR("com.apple.iWork.Keynote"),CFSTR("5.3"),1504));
        assert(!class_getClassMethod(screen,reset));
        assert(apply(CFSTR("com.apple.iWork.Keynote"),CFSTR("5.3"),1561));
        IMP installed=method_getImplementation(class_getClassMethod(screen,reset));assert(installed);
        assert(!apply(CFSTR("com.apple.iWork.Keynote"),CFSTR("5.3"),1671));
        assert(method_getImplementation(class_getClassMethod(screen,reset))==installed);
        assert(!strcmp(method_getTypeEncoding(class_getClassMethod(screen,reset)),"v@:"));
        dlclose(library);
        // Its ordinary loader reference is gone. The installed IMP must still
        // be mapped and callable after all original GL contexts retire.
        Dl_info image={0};assert(dladdr((void *)installed,&image));
        for(unsigned i=0;i<3;i++)((void (*)(id,SEL))installed)(screen,reset);
        puts("PASS Keynote 5-only admission, older AppKit/newer apps excluded, idempotence and IMP lifetime");
    }
}
#endif
