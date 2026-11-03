/* Compatibility choice for undefined ARB fragment-program temporary values.
 * Tiger's native CI compiler emits DPH reading an unwritten component with a
 * zero coefficient. GMA950 eliminates that term; current Intel propagates its
 * undefined value. Initializing TEMP registers chooses zero only where the
 * specification gives no defined initial value. Defined writes are untouched.
 * Compilation only, never a per-draw conversion or pixel/CPU fallback.
 * Caller validates the ORIGINAL program with native GL first. */
#ifndef VIM_ARB_DEFINED_TEMPORARIES_H
#define VIM_ARB_DEFINED_TEMPORARIES_H
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static size_t DGARBTrivia(const char *source,size_t size,size_t p)
{
    while(p<size){
        if(isspace((unsigned char)source[p])){++p;continue;}
        if(source[p]=='#'){while(p<size && source[p]!='\n')++p;continue;}
        break;
    }
    return p;
}
static char *DGARBDefineTemporariesAt(const void *bytes,size_t size,size_t *result_size,size_t *insertion_at)
{
    const char *source=bytes;
    *result_size=0;
    if(!source || size<10 || size>262144 || memcmp(source,"!!ARBfp1.0",10))return NULL;
    size_t names[256][2],count=0,p=10,insertion=0;
    while((p=DGARBTrivia(source,size,p))<size){
        size_t begin=p;
        while(p<size && (isalnum((unsigned char)source[p]) || source[p]=='_'))++p;
        size_t length=p-begin;
        int temp=length==4 && !memcmp(source+begin,"TEMP",4);
        const char *declarations[]={"TEMP","OPTION","PARAM","ATTRIB","OUTPUT","ALIAS","ADDRESS"};
        int declaration=0;
        for(unsigned i=0;i<sizeof(declarations)/sizeof(*declarations);++i)
            if(length==strlen(declarations[i]) && !memcmp(source+begin,declarations[i],length))declaration=1;
        if(!declaration){insertion=begin;break;}
        if(temp){
            for(;;){
                p=DGARBTrivia(source,size,p);size_t name=p;
                if(p>=size || !(isalpha((unsigned char)source[p]) || source[p]=='_'))return NULL;
                while(p<size && (isalnum((unsigned char)source[p]) || source[p]=='_'))++p;
                if(count==256 || p-name>127)return NULL;
                names[count][0]=name;names[count++][1]=p-name;
                p=DGARBTrivia(source,size,p);if(p>=size)return NULL;
                if(source[p]==';'){++p;break;}
                if(source[p++]!=',')return NULL;
            }
        }else{
            while(p<size){
                p=DGARBTrivia(source,size,p);if(p>=size)return NULL;
                if(source[p++]==';')break;
            }
        }
    }
    if(!count || !insertion)return NULL;
    size_t added=0;for(size_t i=0;i<count;++i)added+=names[i][1]+sizeof("MOV , {0.0,0.0,0.0,0.0};\n")-1;
    char *result=malloc(size+added);if(!result)return NULL;
    memcpy(result,source,insertion);size_t out=insertion;
    for(size_t i=0;i<count;++i){
        memcpy(result+out,"MOV ",4);out+=4;
        memcpy(result+out,source+names[i][0],names[i][1]);out+=names[i][1];
        const char tail[]=", {0.0,0.0,0.0,0.0};\n";
        memcpy(result+out,tail,sizeof(tail)-1);out+=sizeof(tail)-1;
    }
    memcpy(result+out,source+insertion,size-insertion);*result_size=size+added;
    if(insertion_at)*insertion_at=insertion;return result;
}
static char *DGARBDefineTemporaries(const void *bytes,size_t size,size_t *result_size)
{ return DGARBDefineTemporariesAt(bytes,size,result_size,NULL); }
#endif
