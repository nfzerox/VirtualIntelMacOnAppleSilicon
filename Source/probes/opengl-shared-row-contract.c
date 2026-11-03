/* Pure contract check: does not create a renderer, access a guest, or claim
 * performance/driver qualification. Compile as C99 and C++98 as well as under
 * sanitizers; both wire endpoints include this exact helper. */
#include "../PassthroughOpenGL/direct-vram-contract.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(!VIMGLSharedRowsSupported(0,718,4608));
    assert(!VIMGLSharedRowsSupported(1152,0,4608));
    assert(!VIMGLSharedRowsSupported(8193,1,32768));
    assert(!VIMGLSharedRowsSupported(1,8193,32));
    assert(!VIMGLSharedRowsSupported(1152,718,4576));
    assert(!VIMGLSharedRowsSupported(1148,718,4592));
    assert(!VIMGLSharedRowsSupported(8192,8192,32800));
    assert(!VIMGLSharedRowsSupported(UINT64_MAX,1,UINT64_MAX));
    assert(!VIMGLSharedRowsSupported(1,1,UINT64_MAX));
    assert(VIMGLSharedRowsSupported(1152,718,4608));
    assert(VIMGLSharedRowsSupported(1148,718,4608));
    assert(VIMGLSharedRowsSupported(1,1,32));
    assert(VIMGLSharedRowsSupported(8192,8192,32768));
    puts("PASS shared-row eligibility; exact odd display widths keep their fallback");
    return 0;
}
