#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <dlfcn.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dumpClass(const char *name) {
    Class cls = objc_getClass(name);
    if (!cls) {
        printf("class=%s missing\n", name);
        return;
    }
    Class superclass = class_getSuperclass(cls);
    printf("class=%s size=0x%zx super=%s\n", name,
           class_getInstanceSize(cls),
           superclass ? class_getName(superclass) : "(root)");
    // dsce correctly reconstructs the executable Objective-C class records,
    // but some cached MetalSerializer ivar type-string pointers still refer to
    // the original shared cache mapping. Do not dereference those diagnostic-
    // only strings in a relocated extraction.
    if (strncmp(name, "MTLSerializer", strlen("MTLSerializer")) == 0)
        return;
    unsigned count = 0;
    Ivar *ivars = class_copyIvarList(cls, &count);
    for (unsigned index = 0; index < count; ++index) {
        printf("  ivar=%s offset=0x%tx type=%s\n",
               ivar_getName(ivars[index]), ivar_getOffset(ivars[index]),
               ivar_getTypeEncoding(ivars[index]));
    }
    free(ivars);
}

static void dumpMethodsContaining(Class cls, const char *needle) {
    if (!cls)
        return;
    printf("selected-methods class=%s needle=%s\n", class_getName(cls),
           needle);
    unsigned count = 0;
    Method *methods = class_copyMethodList(cls, &count);
    for (unsigned index = 0; index < count; ++index) {
        const char *selector = sel_getName(method_getName(methods[index]));
        if (strcasestr(selector, needle)) {
            printf("  method=%s types=%s imp=%p\n", selector,
                   method_getTypeEncoding(methods[index]),
                   method_getImplementation(methods[index]));
        }
    }
    free(methods);
}

static int compareClassNames(const void *left, const void *right) {
    const Class leftClass = *(const Class *)left;
    const Class rightClass = *(const Class *)right;
    return strcmp(class_getName(leftClass), class_getName(rightClass));
}

static bool isRelevantClassName(const char *name) {
    return strstr(name, "AppleParavirt") || strstr(name, "MTLIOAccel") ||
           strstr(name, "MTLSerializer");
}

static void dumpRelevantClasses(void) {
    int count = objc_getClassList(NULL, 0);
    if (count <= 0) {
        printf("class-list unavailable\n");
        return;
    }

    Class *classes = (Class *)calloc((size_t)count, sizeof(*classes));
    if (!classes) {
        printf("class-list allocation failed count=%d\n", count);
        return;
    }

    count = objc_getClassList(classes, count);
    qsort(classes, (size_t)count, sizeof(*classes), compareClassNames);
    for (int index = 0; index < count; ++index) {
        const char *name = class_getName(classes[index]);
        if (isRelevantClassName(name)) {
            dumpClass(name);
        }
    }
    free(classes);
}

static void dumpMethods(Class cls) {
    for (Class current = cls; current; current = class_getSuperclass(current)) {
        printf("methods class=%s\n", class_getName(current));
        unsigned count = 0;
        Method *methods = class_copyMethodList(current, &count);
        for (unsigned index = 0; index < count; ++index) {
            const char *selector = sel_getName(method_getName(methods[index]));
            if (strcasestr(selector, "pipeline") ||
                strcasestr(selector, "library") ||
                strcasestr(selector, "function")) {
                printf("  method=%s types=%s imp=%p\n", selector,
                       method_getTypeEncoding(methods[index]),
                       method_getImplementation(methods[index]));
            }
        }
        free(methods);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    @autoreleasepool {
        void *pvgMetal = dlopen(
            "/System/Library/Extensions/AppleParavirtGPUMetal.bundle/Contents/"
            "MacOS/AppleParavirtGPUMetal", RTLD_NOW | RTLD_LOCAL);
        printf("pvgMetal=%s\n", pvgMetal ? "loaded" : dlerror());
        void *serializer = dlopen(
            "/System/Library/PrivateFrameworks/MetalSerializer.framework/"
            "Versions/A/MetalSerializer", RTLD_NOW | RTLD_LOCAL);
        printf("serializer=%s\n", serializer ? "loaded" : dlerror());
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        printf("actual device=%s\n", object_getClassName(device));
        dumpRelevantClasses();
        dumpMethodsContaining(objc_getClass("AppleParavirtTexture"), "init");
        dumpMethodsContaining(objc_getClass("MTLIOAccelTexture"), "init");
        dumpMethodsContaining(objc_getClass("MTLIOAccelResource"), "init");
        for (Class cls = objc_getClass("AppleParavirtCommandBuffer"); cls;
             cls = class_getSuperclass(cls))
            dumpMethodsContaining(cls, "blit");
        dumpMethods(device ? object_getClass(device)
                           : objc_getClass("AppleParavirtDevice"));
    }
    return 0;
}
