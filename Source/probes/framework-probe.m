#include <dlfcn.h>
#include <objc/runtime.h>
#include <stdio.h>

static const char *required_classes[] = {
    "MTLSerializer",
    "MTLSerializerBlitCommandEncoder",
    "MTLSerializerCommandEncoder",
    "MTLSerializerComputeCommandEncoder",
    "MTLSerializerParallelRenderCommandEncoder",
    "MTLSerializerRenderCommandEncoder",
};

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s framework-binary\n", argv[0]);
        return 64;
    }

    void *image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!image) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    int missing = 0;
    for (unsigned int index = 0;
         index < sizeof(required_classes) / sizeof(required_classes[0]);
         index++) {
        Class candidate = objc_getClass(required_classes[index]);
        printf("%s: %s\n", required_classes[index], candidate ? "present" : "MISSING");
        missing += candidate == Nil;
    }

    dlclose(image);
    return missing ? 2 : 0;
}
