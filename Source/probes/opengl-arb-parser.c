/* Isolated donor-parser experiment: no GL context or system replacement.
 * API order follows Catalina GLEngine's second glePPParse pass. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
    if(argc!=2)return 64;
    setbuf(stdout,NULL);
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"%s\n",dlerror());return 2;}
#define RESOLVE(type,name) type name=(type)dlsym(library,#name); if(!name)return 3
    typedef void *(*Create)(unsigned);
    typedef void (*One)(void *);
    typedef void (*String)(void *,const char *,unsigned);
    typedef void (*Attach)(void *,void **,void **,void **);
    typedef int (*Parse)(void *);
    typedef const char *(*Error)(void *);
    typedef void (*Limits)(void *,const unsigned *);
    RESOLVE(Create,PPParserCreate);
    RESOLVE(One,PPParserInit);
    RESOLVE(One,PPParserFree);
    RESOLVE(String,PPParserAttachString);
    RESOLVE(Attach,PPParserAttachLLVMIR);
    RESOLVE(Parse,PPParserParse);
    RESOLVE(Error,PPParserGetErrorString);
    RESOLVE(Limits,PPParserSetLimits);
    const char *sources[]={
        "!!ARBvp1.0\nMOV result.position, vertex.position;\nMOV result.color, vertex.color;\nEND\n",
        "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n"
    };
    int failed=0;
    for(unsigned stage=0;stage<2;++stage) {
        void *parser=PPParserCreate(stage?2:0),*ir=NULL,*extra=NULL,*reflection=NULL;
        if(!parser)return 4;
        PPParserInit(parser);
        /* Catalina glePPParse's 64-byte vertex/fragment limits, verified
         * against its instructions and __const data, not parser defaults. */
        const unsigned limits[2][16]={
            {256,1024,8,262144,65535,1024,32,2,8,128,4,1,6,8,16,0},
            {128,1024,8,262144,65535,1024,64,0,8,128,4,1,6,8,0,8}
        };
        PPParserSetLimits(parser,limits[stage]);
        PPParserAttachString(parser,sources[stage],1);
        PPParserAttachLLVMIR(parser,&ir,&extra,&reflection);
        int result=PPParserParse(parser);
        printf("stage=%u parse=%d ir=%p extra=%p reflection=%p error=%s\n",
               stage,result,ir,extra,reflection,PPParserGetErrorString(parser));
        if(ir){unsigned char *p=ir;printf("IR header=%02x %02x %02x %02x\n",p[0],p[1],p[2],p[3]);}
        failed|=result!=0||!ir||!reflection;
        PPParserAttachLLVMIR(parser,NULL,NULL,NULL);
        PPParserFree(parser);
        free(ir);free(extra);free(reflection);
    }
    dlclose(library);
    return failed?1:0;
}
