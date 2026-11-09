/* Tiger's assert.h calls the compiler-runtime helper, which is not in its
 * link SDK's shared libgcc stub. Keep diagnostics and real abort semantics;
 * this fixture-only helper never changes a GL call or assertion condition. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
void __eprintf(const char *format,const char *file,unsigned line,const char *expression)
{
    (void)format;
    fprintf(stderr,"%s:%u: failed assertion '%s'\n",file,line,expression);
    abort();
}
