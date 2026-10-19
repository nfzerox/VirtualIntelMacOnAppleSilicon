#include <stdbool.h>
#include <stdint.h>
#include "../PVGAIRTarget.h"

typedef void *LLVMModuleRef;

extern bool MTLDowngradeAIRModule(LLVMModuleRef module, uint64_t version,
                                  uint32_t build);
extern void LLVMSetTarget(LLVMModuleRef module, const char *triple);

/* The shipping PVG plugin asks GPUCompiler to downgrade AIR, but that helper
 * stamps the host generation's deployment target onto render functions. Keep
 * the official downgrade and then restore the Catalina target through LLVM's
 * stable C API. The plugin's bind is renamed to this equal-length symbol. */
bool PVGDowngradeAIRModule(LLVMModuleRef module, uint64_t version,
                           uint32_t build) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
    // Mojave's native Metal compiler accepts AIR through 2.1 (not 2.2).
    // The donor GL frontend is upgraded before reaching us. Use Apple's
    // actual downgrade, not a header-only version relabel.
    if (version > UINT64_C(0x100000002)) {
        version = UINT64_C(0x100000002);
        build = 0;
    }
#endif
    bool result = MTLDowngradeAIRModule(module, version, build);
    if (module)
        LLVMSetTarget(module, PVG_AIR_TARGET);
    return result;
}
