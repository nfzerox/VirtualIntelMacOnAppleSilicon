/* Pure compile-time normalizer checks; no renderer, VM or native mutation. */
#include "../PassthroughOpenGL/arb-program-source.h"
#include <assert.h>
#include <stdio.h>
static void unchanged(const char *text,size_t length)
{
    size_t output=123;char *result=DGARBDefineTemporaries(text,length,&output);
    assert(!result && !output);
}
int main(void)
{
    const char input[]="!!ARBfp1.0\n# TEMP ignored;\nPARAM p = {0,0,0,0};\nTEMP r0, # comment\n r1;\nALIAS z=r0;\nMOV r1.x,p;\nEND\n";
    const char inserted[]="MOV r0, {0.0,0.0,0.0,0.0};\nMOV r1, {0.0,0.0,0.0,0.0};\n";
    size_t size=0;char *result=DGARBDefineTemporaries(input,sizeof(input)-1,&size);assert(result);
    const char *body=strstr(input,"MOV r1.x");size_t prefix=(size_t)(body-input);
    assert(size==sizeof(input)-1+sizeof(inserted)-1);
    assert(!memcmp(result,input,prefix));assert(!memcmp(result+prefix,inserted,sizeof(inserted)-1));
    assert(!memcmp(result+prefix+sizeof(inserted)-1,body,sizeof(input)-1-prefix));free(result);
    unchanged(NULL,10);unchanged(input,0);unchanged("!!ARBvp1.0\nTEMP r;\nMOV result.position,r;\nEND",48);
    const char *bad[]={"!!ARBfp1.0\nTEMP", "!!ARBfp1.0\nTEMP r", "!!ARBfp1.0\nTEMP r[2];\nEND", "!!ARBfp1.0\nEND"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)unchanged(bad[i],strlen(bad[i]));
    // Every truncated prefix must remain within the supplied, non-NUL span.
    for(size_t n=0;n<sizeof(input);++n){result=DGARBDefineTemporaries(input,n,&size);free(result);}
    const char cookie[]="0123456789abcdef0123456789abcdef";
    int counts[DG_ARB_SOURCE_COUNTS]={11,2,1,1,11,0,0},decoded[DG_ARB_SOURCE_COUNTS];
    char *wrapped=DGARBWrapSource(input,sizeof(input)-1,cookie,counts,&size);assert(wrapped);
    size_t wrapped_size=size,original_size=0;char *copy=malloc(size);assert(copy);memcpy(copy,wrapped,size);
    assert(DGARBUnwrapSource(copy,size,cookie,decoded,&original_size));
    assert(original_size==sizeof(input)-1 && !memcmp(copy,input,original_size));
    assert(!memcmp(counts,decoded,sizeof(counts)));free(copy);
    // Query rejection is nonmutating, including wrong cookies and corruption.
    copy=malloc(size);assert(copy);memcpy(copy,wrapped,size);
    assert(!DGARBUnwrapSource(copy,size,"fedcba9876543210fedcba9876543210",decoded,&original_size));
    assert(!memcmp(copy,wrapped,size));
    copy[size-2]^=1;assert(!DGARBUnwrapSource(copy,size,cookie,decoded,&original_size));free(copy);
    for(size_t n=0;n<wrapped_size;++n){copy=malloc(n?n:1);assert(copy);memcpy(copy,wrapped,n);
        assert(!DGARBUnwrapSource(copy,n,cookie,decoded,&original_size));free(copy);}
    // Even an application-provided identical marker survives one upload/query.
    size_t nested_size=0;char *nested=DGARBWrapSource(wrapped,wrapped_size,cookie,counts,&nested_size);assert(nested);
    assert(DGARBUnwrapSource(nested,nested_size,cookie,decoded,&original_size));
    assert(original_size==wrapped_size && !memcmp(nested,wrapped,wrapped_size));free(nested);free(wrapped);
    puts("ARB temporary initialization bounds/comments/declarations: PASS");return 0;
}
