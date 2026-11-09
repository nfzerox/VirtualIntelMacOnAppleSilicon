/* Independent per-bit oracle for every shift and fragmented source rows.
 * Run under ASan/UBSan; no CGL context or GPU is required. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-wire.h"
#include "../PassthroughOpenGL/passthrough-opengl-raster-copy.h"
int main(void)
{
    unsigned cases=0;unsigned char source[512],expected[512],actual[512];
    for(unsigned i=0;i<sizeof(source);++i)source[i]=(unsigned char)(i*97+43);
    for(unsigned width=1;width<=33;++width)for(unsigned rows=1;rows<=4;++rows)for(int lsb=0;lsb<2;++lsb){
        memset(expected,0,sizeof(expected));memcpy(actual,source,sizeof(actual));size_t stride=(width+7)/8;
        for(unsigned row=0;row<rows;++row)for(unsigned x=0;x<width;++x)
            expected[row*stride+x/8]|=(unsigned char)((source[row*width+x]&1u)<<(lsb?x%8:7-x%8));
        size_t n=DGPackRasterBitmap(actual,width,rows,lsb);
        if(n!=stride*rows || memcmp(actual,expected,n)){fprintf(stderr,"FAIL bitmap pack %u,%u,%d\n",width,rows,lsb);return 1;}
        ++cases;
    }
    for(unsigned width=1;width<=33;++width)for(unsigned rows=1;rows<=4;++rows)
        for(unsigned shift=0;shift<8;++shift)for(int lsb=0;lsb<2;++lsb){
            struct DGRasterLayout l=DGRasterLayout((int)width,(int)rows,1,0,1,1,(int)shift,11);
            size_t stride=(width+7)/8,total=stride*rows;memset(expected,0xa5,sizeof(expected));
            for(unsigned row=0;row<rows;++row)for(unsigned x=0;x<width;++x){
                unsigned in_bit=lsb?x%8:7-x%8;unsigned bit=(source[row*stride+x/8]>>in_bit)&1;
                unsigned out_bit=shift+x;unsigned mask=1u<<(lsb?out_bit%8:7-out_bit%8);
                size_t offset=(size_t)l.offset+row*(size_t)l.stride+out_bit/8;
                expected[offset]=(unsigned char)((expected[offset]&~mask)|(bit?mask:0));
            }
            for(size_t chunk=1;chunk<=17;++chunk){
                memset(actual,0xa5,sizeof(actual));
                for(size_t at=0;at<total;){size_t n=total-at<chunk?total-at:chunk;
                    DGCopyRasterRead(actual,source+at,n,at,&l,width,1,lsb);at+=n;}
                if(memcmp(actual,expected,sizeof(actual))){
                    fprintf(stderr,"FAIL width=%u rows=%u shift=%u lsb=%d chunk=%zu\n",width,rows,shift,lsb,chunk);return 1;}
                ++cases;
            }
        }
    struct DGRasterLayout l=DGRasterLayout(17,3,24,23,8,1,2,83);
    memset(expected,0xa5,sizeof(expected));
    for(unsigned row=0;row<3;++row)memcpy(expected+l.offset+row*l.stride,source+row*51,51);
    for(size_t chunk=1;chunk<=153;++chunk){
        memset(actual,0xa5,sizeof(actual));
        for(size_t at=0;at<153;){size_t n=153-at<chunk?153-at:chunk;
            DGCopyRasterRead(actual,source+at,n,at,&l,17,0,0);at+=n;}
        if(memcmp(actual,expected,sizeof(actual))){fprintf(stderr,"FAIL byte chunk=%zu\n",chunk);return 1;}++cases;
    }
    printf("PASS raster copy cases=%u\n",cases);return 0;
}
