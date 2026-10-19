#include <dlfcn.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s image-binary [required-class ...]\n",
                argv[0]);
        return 64;
    }

    int mode = RTLD_NOW | RTLD_LOCAL;
    if (getenv("PVG_PROBE_NODELETE")) {
        mode |= RTLD_NODELETE;
    }
    void *image = dlopen(argv[1], mode);
    if (!image) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    int missing = 0;
    for (int index = 2; index < argc; index++) {
        Class candidate = objc_getClass(argv[index]);
        printf("%s: %s\n", argv[index], candidate ? "present" : "MISSING");
        missing += candidate == Nil;
    }

    dlclose(image);
    return missing ? 2 : 0;
}
