/* The paired renderer promises process-local "0" diagnostic overrides.
 * Default normal builds still get their production "1" values at GLI init. */
#include "../PassthroughOpenGL/tiger-rosetta-platform.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const char *name="VIM_TEST_PRODUCT_FEATURE";
    assert(!unsetenv(name));assert(!VIMGLProductFeature(name));
    assert(!setenv(name,"1",1));assert(VIMGLProductFeature(name));
    assert(!setenv(name,"0",1));assert(!VIMGLProductFeature(name));
    assert(!setenv(name,"enabled",1));assert(VIMGLProductFeature(name));
    assert(!unsetenv(name));
    puts("PASS absent, enabled and explicit-zero product feature semantics");return 0;
}
