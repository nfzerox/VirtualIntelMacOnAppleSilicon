/* Offline experiment: assemble textual AIR using the matching Apple LLVM. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "../compat/PVGAIRMetadata.h"

int main(int argc, char **argv) {
    if (argc != 3 || !getenv("PVG_LLVM_IMAGE")) return 64;
    void *image = dlopen(getenv("PVG_LLVM_IMAGE"), RTLD_NOW | RTLD_LOCAL);
    if (!image) { fprintf(stderr, "%s\n", dlerror()); return 1; }
#define LOAD(name, ret, args) ret (*name) args = dlsym(image, #name); if (!name) return 2
    LOAD(LLVMContextCreate, void *, (void));
    LOAD(LLVMCreateMemoryBufferWithContentsOfFile, int, (const char *, void **, char **));
    LOAD(LLVMParseIRInContext, int, (void *, void *, void **, char **));
    LOAD(LLVMWriteBitcodeToMemoryBuffer, void *, (void *));
    LOAD(LLVMGetBufferStart, const char *, (void *));
    LOAD(LLVMGetBufferSize, size_t, (void *));
    void *input = NULL, *module = NULL;
    char *error = NULL;
    void *context = LLVMContextCreate();
    if (LLVMCreateMemoryBufferWithContentsOfFile(argv[1], &input, &error) ||
        LLVMParseIRInContext(context, input, &module, &error)) {
        fprintf(stderr, "%s\n", error ? error : "parse failed"); return 3;
    }
    if (getenv("PVG_REPAIR_FRAGMENT_LOCATIONS")) {
        int repaired = PVGRepairAIRFragmentLocations(image, module, context);
        fprintf(stderr, "fragment locations repaired=%d\n", repaired);
        if (repaired < 0) return 6;
    }
    void *output = LLVMWriteBitcodeToMemoryBuffer(module);
    FILE *file = fopen(argv[2], "wb");
    if (!file) return 4;
    size_t size = LLVMGetBufferSize(output);
    int ok = fwrite(LLVMGetBufferStart(output), 1, size, file) == size;
    return fclose(file) || !ok ? 5 : 0;
}
