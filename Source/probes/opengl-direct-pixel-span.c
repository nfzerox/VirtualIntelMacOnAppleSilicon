/* Pure shape/bounds checks, including products larger than uint64_t.
 * No VM, GL driver or live desktop access is required. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include "../PassthroughOpenGL/passthrough-opengl-wire.h"
#include <limits.h>
#include <stdio.h>
static int failed;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);failed=1;}}while(0)
int main(void)
{
    CHECK(DGPixelSpan(5,3,0x1908,0x1401,8,8,1,2,0)==124);
    CHECK(DGPixelSize(0x85b9,0x85ba)==2);
    CHECK(DGPixelSize(0x85b9,0x85bb)==2);
    CHECK(DGPixelSize(0x1908,0x85ba)==0);
    CHECK(DGPixelSpan(4,2,0x85b9,0x85ba,8,4,1,2,0)==44);
    CHECK(DGPixelSpan(4,2,0x85b9,0x85bb,0,8,0,0,16)==24);
    CHECK(DGPixelSpan(INT_MAX,INT_MAX,0x85b9,0x85ba,INT_MAX,8,0,0,0)==SIZE_MAX);
    CHECK(DGPixelSpan3D(5,3,3,0x1908,0x1401,8,8,1,2,0,5,2)==764);
    CHECK(DGPixelSpan3D(5,3,3,0x1908,0x1401,8,8,1,2,0,1,2)==252);
    CHECK(DGPixelSpan3D(5,3,0,0x1908,0x1401,8,8,1,2,0,5,2)==0);
    CHECK(DGPixelSpan3D(5,3,-1,0x1908,0x1401,8,8,1,2,0,5,2)==SIZE_MAX);
    CHECK(DGPixelSpan3D(5,3,3,0x1908,0x1401,8,8,1,2,0,INT_MAX,INT_MAX)==SIZE_MAX);
    CHECK(DGPixelSpan(INT_MAX,INT_MAX,0x1908,0x1406,INT_MAX,8,INT_MAX,INT_MAX,0)==SIZE_MAX);
    CHECK(DGPixelSpan3D(INT_MAX,INT_MAX,INT_MAX,0x1908,0x1406,INT_MAX,8,INT_MAX,INT_MAX,0,INT_MAX,INT_MAX)==SIZE_MAX);
    CHECK(DGPixelSpanWide(4096,4097,GL_RGBA,GL_UNSIGNED_BYTE,0,1,0,0,0)==UINT64_C(67125248));
    CHECK(DGPixelSpan(4096,4097,GL_RGBA,GL_UNSIGNED_BYTE,0,1,0,0,0)==SIZE_MAX);
    CHECK(DGPixelSpan3DWide(257,257,65,GL_RGBA,GL_FLOAT,260,1,1,1,0,260,1)==UINT64_C(71377248));
    CHECK(DGPixelSpan3D(257,257,65,GL_RGBA,GL_FLOAT,260,1,1,1,0,260,1)==SIZE_MAX);
    CHECK(DGPixelSpanWide(16,1,GL_RGBA,GL_UNSIGNED_BYTE,0,1,0,16777216,0)==UINT64_C(67108928));
    CHECK(DGPixelSpan3DWide(INT_MAX,INT_MAX,INT_MAX,GL_RGBA,GL_FLOAT,INT_MAX,8,INT_MAX,INT_MAX,0,INT_MAX,INT_MAX)==UINT64_MAX);
    struct DGRasterLayout raster=DGRasterLayout(10,3,1,17,8,2,536870915,5);
    CHECK(raster.span==UINT64_C(67108886) && raster.offset==UINT64_C(67108874));
    CHECK(raster.stride==5 && raster.row_bytes==2 && raster.rows==3 && raster.bit_offset==3);
    CHECK(DGPixelSpanWide(10,3,GL_STENCIL_INDEX,GL_BITMAP,17,8,2,536870915,5)==raster.span);
    CHECK(DGRasterLayout(16,1,32,0,1,0,16777216,0).offset==UINT64_C(67108864));
    puts(failed?"FAIL pixel span bounds":"PASS pixel span bounds");return failed;
}
