/* Cold native query-shape audit. Compile unchanged against public CGL, then
 * run on physical Intel and the guest. Boolean state must be written as 0/1,
 * so an 0xa5 output seed measures width without guessing a state's value.
 * The generated registry list excludes variable-length results. This is not
 * a claim that every recognized query or native extension is implemented.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include "../PassthroughOpenGL/passthrough-opengl-wire.h"
#include <stdio.h>
static const struct { GLenum key; const char *name; } selectors[]={
#include "state-query-selectors.inc"
};
int main(int argc,char **argv)
{
    int core=argc==2&&!strcmp(argv[1],"--core");
    if(argc!=1&&!core)return 64;
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s core=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),core);
    unsigned valid=0,mismatch=0;
    for(unsigned i=0;i<sizeof(selectors)/sizeof(*selectors);++i){
        /* All selected fixed-size glGet state is at most a 4x4 matrix.
         * Guard bytes detect an unexpected registry/API contract as a hard
         * diagnostic failure instead of recording a bogus inferred width. */
        struct {unsigned char before[16];GLboolean values[16];unsigned char after[16];} output;
        memset(&output,0xa5,sizeof(output));
        glGetBooleanv(selectors[i].key,output.values);GLenum error=glGetError();
        unsigned actual=0;
        for(unsigned j=0;j<16;++j){
            if(output.before[j]!=0xa5||output.after[j]!=0xa5){
                fprintf(stderr,"Unexpected variable-width query %s\n",selectors[i].name);return 2;
            }
            if(output.values[j]==0||output.values[j]==1)actual=j+1;
            else if(output.values[j]!=0xa5){
                fprintf(stderr,"Non-boolean native output %s\n",selectors[i].name);return 2;
            }
        }
        if(error){
            if(actual){fprintf(stderr,"Rejected query overwrote output %s\n",selectors[i].name);return 2;}
            continue;
        }
        ++valid;size_t wire=DGGetCount(selectors[i].key);int same=wire==actual;
        for(unsigned type=0;type<3;++type){
            union {GLint integers[16];GLfloat floats[16];GLdouble doubles[16];} typed;
            memset(&typed,0xa5,sizeof(typed));
            if(type==0)glGetIntegerv(selectors[i].key,typed.integers);
            else if(type==1)glGetFloatv(selectors[i].key,typed.floats);
            else glGetDoublev(selectors[i].key,typed.doubles);
            if(glGetError()){
                fprintf(stderr,"Typed query rejected a boolean-valid selector %s type=%u\n",selectors[i].name,type);return 2;
            }
            size_t bytes=actual*(type==2?sizeof(GLdouble):sizeof(GLint));
            for(size_t j=bytes;j<sizeof(typed);++j)if(((unsigned char *)&typed)[j]!=0xa5){
                fprintf(stderr,"Typed query exceeded its measured shape %s type=%u\n",selectors[i].name,type);return 2;
            }
        }
        mismatch+=!same;
        printf("%s key=%x native=%u wire=%zu %s\n",same?"MATCH":"AUDIT",selectors[i].key,actual,wire,selectors[i].name);
    }
    printf("valid=%u shape-audits=%u\n",valid,mismatch);
    /* Defined mutations distinguish a real native reply from untouched
     * defaults or a transport fabricated zero. All are core GL state. */
    GLint integer=-1;GLfloat floating=-1;GLdouble double_value=-1;GLboolean boolean=2;
    glDisable(GL_DITHER);glGetIntegerv(GL_DITHER,&integer);glGetFloatv(GL_DITHER,&floating);
    glGetDoublev(GL_DITHER,&double_value);glGetBooleanv(GL_DITHER,&boolean);
    if(glGetError()||integer!=0||floating!=0||double_value!=0||boolean!=0)return 1;
    glEnable(GL_DITHER);glGetIntegerv(GL_DITHER,&integer);glGetFloatv(GL_DITHER,&floating);
    glGetDoublev(GL_DITHER,&double_value);glGetBooleanv(GL_DITHER,&boolean);
    if(glGetError()||integer!=1||floating!=1||double_value!=1||boolean!=1)return 1;
    glHint(GL_TEXTURE_COMPRESSION_HINT,GL_NICEST);glGetIntegerv(GL_TEXTURE_COMPRESSION_HINT,&integer);
    if(glGetError()||integer!=GL_NICEST)return 1;
    puts("PASS changed native enable state and hint in typed replies");
    GLint formats=-1;glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS,&formats);
    if(glGetError()||formats<0||(size_t)formats>DG_MAX_BYTES/sizeof(GLdouble))return 1;
    GLint *native_formats=calloc((size_t)formats+1,sizeof(*native_formats));
    if(!native_formats)return 2;
    for(unsigned type=0;type<4;++type){
        size_t element=type==0?sizeof(GLint):type==1?sizeof(GLfloat):type==2?sizeof(GLdouble):sizeof(GLboolean);
        size_t bytes=(size_t)formats*element;unsigned char *allocation=malloc(bytes+16);
        if(!allocation)return 2;memset(allocation,0xa5,bytes+16);void *values=allocation+8;
        if(type==0)glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS,values);
        else if(type==1)glGetFloatv(GL_COMPRESSED_TEXTURE_FORMATS,values);
        else if(type==2)glGetDoublev(GL_COMPRESSED_TEXTURE_FORMATS,values);
        else glGetBooleanv(GL_COMPRESSED_TEXTURE_FORMATS,values);
        if(glGetError())return 1;
        for(unsigned i=0;i<8;++i)if(allocation[i]!=0xa5||allocation[8+bytes+i]!=0xa5)return 1;
        for(GLint i=0;i<formats;++i){
            if(type==0)native_formats[i]=((GLint *)values)[i];
            else if(type==1&&((GLfloat *)values)[i]!=(GLfloat)native_formats[i])return 1;
            else if(type==2&&((GLdouble *)values)[i]!=(GLdouble)native_formats[i])return 1;
            else if(type==3&&((GLboolean *)values)[i]!=!!native_formats[i])return 1;
        }
        free(allocation);
    }
    free(native_formats);printf("PASS counted native compressed formats=%d, four typed lists and guards\n",formats);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    /* Some state is intentionally local/client-side. AUDIT entries need
     * inspection, not an automatic replacement of the transport table. */
    return 0;
}
