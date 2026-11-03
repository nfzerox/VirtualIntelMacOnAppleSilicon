/* Original ARB source travels WITH the native program, not a GLuint cache.
 * Delete/rebind/name reuse and shared-context zombies retain native lifetimes.
 * The process-local cookie distinguishes our wrapper from application comments;
 * it is collision avoidance, NOT a security/authentication boundary.
 * Only compilation and explicit source/resource queries use this codec. */
#ifndef VIM_ARB_PROGRAM_SOURCE_H
#define VIM_ARB_PROGRAM_SOURCE_H
#include <stdint.h>
#include <stdio.h>
#include "arb-defined-temporaries.h"
#define DG_ARB_SOURCE_COUNTS 7
#define DG_ARB_SOURCE_LIMIT (262144u+65536u+512u)
static uint64_t DGARBSourceHashBytes(uint64_t value,const void *data,size_t length)
{
    const unsigned char *p=data;
    for(size_t i=0;i<length;++i)value=(value^p[i])*UINT64_C(1099511628211);
    return value;
}
static uint64_t DGARBSourceHash(const void *data,size_t length)
{ return DGARBSourceHashBytes(UINT64_C(14695981039346656037),data,length); }
static char *DGARBWrapSource(const void *source,size_t length,const char cookie[33],
    const int counts[DG_ARB_SOURCE_COUNTS],size_t *output_length)
{
    size_t transformed_length=0,at=0;*output_length=0;
    char *transformed=DGARBDefineTemporariesAt(source,length,&transformed_length,&at);
    if(!transformed)return NULL;
    char header[512];
    int n=snprintf(header,sizeof(header),"\n# VIM_ARB_TEMP1 %s %zu %zu %zu %016llx %d %d %d %d %d %d %d\n",
        cookie,length,at,transformed_length-length,(unsigned long long)DGARBSourceHash(source,length),
        counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6]);
    if(n<0 || n>=(int)sizeof(header)){free(transformed);return NULL;}
    char *result=malloc(transformed_length+(size_t)n);if(!result){free(transformed);return NULL;}
    memcpy(result,transformed,10);memcpy(result+10,header,(size_t)n);
    memcpy(result+10+n,transformed+10,transformed_length-10);free(transformed);
    *output_length=transformed_length+(size_t)n;return result;
}
static int DGARBUnwrapSource(void *bytes,size_t length,const char cookie[33],
    int counts[DG_ARB_SOURCE_COUNTS],size_t *output_length)
{
    char *raw=bytes;*output_length=0;
    const char prefix[]="!!ARBfp1.0\n# VIM_ARB_TEMP1 ";
    if(!raw || length<sizeof(prefix)-1 || length>DG_ARB_SOURCE_LIMIT || memcmp(raw,prefix,sizeof(prefix)-1))return 0;
    size_t available=length-11;if(available>510)available=510;
    const char *end=memchr(raw+11,'\n',available);if(!end)return 0;
    size_t header_length=(size_t)(end+1-(raw+10));
    char header[512];memcpy(header,raw+10,header_length);header[header_length]=0;
    char token[33]={0};size_t original=0,at=0,added=0;unsigned long long hash=0;int consumed=0;
    if(sscanf(header,"\n# VIM_ARB_TEMP1 %32s %zu %zu %zu %llx %d %d %d %d %d %d %d\n%n",
        token,&original,&at,&added,&hash,counts,counts+1,counts+2,counts+3,counts+4,counts+5,counts+6,&consumed)!=12 ||
       consumed!=(int)header_length || strcmp(cookie,token) || original<10 || original>262144 ||
       at<10 || at>original || !added || added>65536 || length!=original+header_length+added)return 0;
    for(unsigned i=0;i<DG_ARB_SOURCE_COUNTS;++i)if(counts[i]<0)return 0;
    uint64_t digest=DGARBSourceHash(raw,10);
    digest=DGARBSourceHashBytes(digest,raw+10+header_length,at-10);
    digest=DGARBSourceHashBytes(digest,raw+at+header_length+added,original-at);
    if(digest!=(uint64_t)hash)return 0;
    // Verify first, then compact in place. No second allocation/OOM fallback
    // can accidentally expose the augmented source instead of caller bytes.
    memmove(raw+10,raw+10+header_length,at-10);
    memmove(raw+at,raw+at+header_length+added,original-at);
    *output_length=original;return 1;
}
#endif
