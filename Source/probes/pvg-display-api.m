// Read-only runtime metadata survey; creates no GPU device or guest.
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <dlfcn.h>

int main(void) {@autoreleasepool {
    if(!dlopen("/System/Library/Frameworks/ParavirtualizedGraphics.framework/ParavirtualizedGraphics",RTLD_NOW|RTLD_LOCAL))return 1;
    for(NSString *name in @[@"PGDisplayDescriptor",@"PGDisplayMode"]) {
        Class cls=NSClassFromString(name);printf("%s\n",name.UTF8String);
        unsigned count=0;Method *methods=class_copyMethodList(cls,&count);
        for(unsigned i=0;i<count;i++)printf("  %s %s\n",sel_getName(method_getName(methods[i])),method_getTypeEncoding(methods[i]));
        free(methods);
    }
    return 0;
}}
