/* Pure byte-order checks; real Rosetta/native GPU acceptance is separate. */
#include "../PassthroughOpenGL/passthrough-opengl-endian.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const unsigned char source[]={0x12,0x34,0x56,0x78,0xaa,0xbb,0xcc,0xdd,
        0x9a,0xbc,0xde,0xf0,0xee,0xff,0x11,0x22};
    unsigned char output[16]={0};
    DGSwapVertexComponents(output,source,2,2,2,8);
    const unsigned char short_expected[]={0x34,0x12,0x78,0x56,0xbc,0x9a,0xf0,0xde};
    assert(!memcmp(output,short_expected,8));assert(source[4]==0xaa && source[12]==0xee);
    DGSwapVertexComponents(output,source,2,1,4,8);
    const unsigned char word_expected[]={0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a};
    assert(!memcmp(output,word_expected,8));
    DGSwapVertexComponents(output,source,1,1,8,0);
    const unsigned char double_expected[]={0xdd,0xcc,0xbb,0xaa,0x78,0x56,0x34,0x12};
    assert(!memcmp(output,double_expected,8));
    DGSwapVertexComponents(output,source,2,4,1,0);assert(!memcmp(output,source,8));
    memcpy(output,source,16);DGSwapVertexComponents(output,output,4,1,4,0);
    assert(!memcmp(output,word_expected,4) && output[4]==0xdd && output[7]==0xaa);
    const unsigned char indices16[]={0,1,0,3,1,0},indices32[]={0,0,0,2,0,1,0,0};
    assert(DGBigEndianMaxIndex(indices16,3,2)==257);
    assert(DGBigEndianMaxIndex(indices32,2,4)==65537);
    assert(DGBigEndianMaxIndex(indices16,0,2)==0);
    puts("PASS typed big-endian components, strides, raw padding, in-place indices and ranges");return 0;
}
