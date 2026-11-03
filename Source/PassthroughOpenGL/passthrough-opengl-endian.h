/* Rosetta1 keeps typeless buffer bytes in PowerPC order. Convert only the
 * interpreted scalar components at draw time, never padding or raw storage.
 * Mirrors period GLEngine's proc_native-gated array/index conversion seam. */
#ifndef VIM_PASSTHROUGH_OPENGL_ENDIAN_H
#define VIM_PASSTHROUGH_OPENGL_ENDIAN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static void DGSwapScalar(void *destination,const void *source,unsigned bytes)
{
    // memcpy handles unaligned/overlapping scalar addresses and aliasing.
    if(bytes==2){uint16_t v;memcpy(&v,source,2);v=__builtin_bswap16(v);memcpy(destination,&v,2);}
    else if(bytes==4){uint32_t v;memcpy(&v,source,4);v=__builtin_bswap32(v);memcpy(destination,&v,4);}
    else if(bytes==8){uint64_t v;memcpy(&v,source,8);v=__builtin_bswap64(v);memcpy(destination,&v,8);}
    else if(bytes==1)memmove(destination,source,1);
}
static void DGSwapVertexComponents(void *destination,const void *source,uint64_t count,
    unsigned components,unsigned bytes,size_t source_stride)
{
    size_t element=(size_t)components*bytes;
    if(!source_stride)source_stride=element;
    for(uint64_t i=0;i<count;++i)for(unsigned j=0;j<components;++j)
        DGSwapScalar((char *)destination+(size_t)i*element+j*bytes,
            (const char *)source+(size_t)i*source_stride+j*bytes,bytes);
}
static uint64_t DGBigEndianMaxIndex(const void *indices,uint32_t count,unsigned bytes)
{
    uint64_t maximum=0;
    for(uint32_t i=0;i<count;++i){
        const unsigned char *p=(const unsigned char *)indices+(size_t)i*bytes;
        uint32_t value=p[0];for(unsigned j=1;j<bytes;++j)value=(value<<8)|p[j];
        if((uint64_t)value+1>maximum)maximum=(uint64_t)value+1;
    }
    return maximum;
}
#endif
