#include <string>
#include <stdint.h>

extern "C" void PVGDownlevelModuleTarget(void *module)
{
    if (!module)
        return;

    /* Apple LLVM 7's Module::TargetTriple is at +0xd8.  This is independently
     * confirmed by GPUCompiler::getTargetTriple adding 0xd8 before invoking
     * llvm::Triple(StringRef).  The front end and its bundled PCH must retain
     * their internally consistent 11.7 target; only downlevel the completed
     * LLVM module before the PVG back end serializes it for Catalina Metal. */
    std::string *target = reinterpret_cast<std::string *>(
        static_cast<char *>(module) + 0xd8);
    *target = "air64-apple-macosx10.15.0";
}

extern "C" bool MTLDowngradeAIRModule(void *module, uint64_t version,
                                       uint32_t build);

extern "C" bool PVGDowngradeAIRModule(void *module, uint64_t version,
                                       uint32_t build)
{
    bool result = MTLDowngradeAIRModule(module, version, build);
    PVGDownlevelModuleTarget(module);
    return result;
}
