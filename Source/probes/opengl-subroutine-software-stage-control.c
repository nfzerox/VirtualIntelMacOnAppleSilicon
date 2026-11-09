/* Native diagnostic ONLY, never an app/runtime dependency.
 * Reproduce the debugger control for the observed stale software-vertex
 * subroutine-table copy. Ghidra and live GLD state show that this source is
 * null when the original vertex program executes in GLEngine's CPU path.
 * Omitting that single four-byte copy lets the unchanged all-stage Khronos
 * pixel case pass. This global interposer is intentionally NOT a production
 * fix: it does not yet prove every backend layout/stage/lifetime predicate.
 * Use DYLD_INSERT_LIBRARIES only on explicitly owned native test processes.
 */
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void *copy_control(void *destination,const void *source,size_t bytes)
{
    if(!source&&bytes==4){
        Dl_info caller={0};void *address=__builtin_return_address(0);
        if(dladdr(address,&caller)&&caller.dli_fname&&caller.dli_sname&&
           strstr(caller.dli_fname,"/AppleMetalOpenGLRenderer.bundle/")&&
           (!strcmp(caller.dli_sname,"_ZN13GLDContextRec24setRenderProgramUniformsEv")||
            !strcmp(caller.dli_sname,"__ZN13GLDContextRec24setRenderProgramUniformsEv"))){
            fprintf(stderr,"SW-STAGE CONTROL omitted stale four-byte subroutine-table copy in %s\n",caller.dli_sname);
            return destination;
        }
    }
    return memcpy(destination,source,bytes);
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} control={
    (const void *)(uintptr_t)&copy_control,(const void *)(uintptr_t)&memcpy};
