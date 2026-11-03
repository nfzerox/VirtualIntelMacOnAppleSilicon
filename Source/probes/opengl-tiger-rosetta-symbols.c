/* Finite, deliberately non-rendering native Rosetta GL-loader audit.
 * Only for an owned Tiger test VM, temporarily at its authorized GLI path.
 * BDL's measured diagnostic callback reports deliberately absent labels, so
 * this probe does not itself import printf/stdio, clocks or filesystem APIs
 * whose availability it is trying to measure. exit terminates only the
 * explicitly launched fixture; this is NEVER a distributable GL engine. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include "tiger-rosetta-symbols-generated.inc"
__attribute__((constructor)) static void AuditTigerRosettaSymbols(void)
{
    typedef uint32_t (*Lookup)(const char *);
    Lookup lookup=(Lookup)dlsym(RTLD_DEFAULT,"bdl_lookup_symbol");
    if(!lookup)exit(77);
    for(unsigned i=0;i<sizeof(vim_tiger_symbols)/sizeof(vim_tiger_symbols[0]);i++){
        const struct VIMTigerAuditSymbol *symbol=&vim_tiger_symbols[i];
        void *address=dlsym(RTLD_DEFAULT,symbol->name);
        lookup(address?symbol->present:symbol->missing);
        void *library=dlopen(symbol->library,RTLD_LAZY|RTLD_LOCAL);
        void *bound=library?dlsym(library,symbol->name):NULL;
        lookup(bound?symbol->libpresent:symbol->libmissing);
        // Keep handles only until this finite child exits. Do not introduce
        // an unaudited dlclose import merely to qualify other imports.
    }
    exit(0);
}
