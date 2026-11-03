/* Public native Intel / guest comparison for texture-environment queries.
 * Exact scalar/vector bounds and output preservation, not a fabricated state
 * cache. Build x64/i386 against the period SDK and run the same executable.
 * Contract: Khronos OpenGL-Refpages gl2.1/glGetTexEnv.xml.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLRenderers.h>
#include "opengl-reference-renderer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct Output { uint32_t before; union {GLint integers[4];GLfloat floats[4];} value;uint32_t after; };
struct Query {GLenum pname;unsigned count;const char *name;};
static const struct Query queries[]={
#define DG_SERVER_QUERY(name,count) {name,count,#name},
#include "../PassthroughOpenGL/server-state-query-counts.inc"
#undef DG_SERVER_QUERY
};
static void seed(struct Output *output){memset(output,0xa5,sizeof(*output));}
static void guards(const struct Output *output)
{assert(output->before==0xa5a5a5a5u&&output->after==0xa5a5a5a5u);}
static void tail(const struct Output *output)
{for(unsigned i=1;i<4;++i)assert((uint32_t)output->value.integers[i]==0xa5a5a5a5u);}
int main(int argc,char **argv)
{
    if(argc>2||(argc==2&&strcmp(argv[1],"--intel-hd5000")))return 64;
    GLint intel=argc==2?VIMReferenceIntelHD5000():0;
    if(argc==2&&!intel)return 77;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAAllowOfflineRenderers,kCGLPFASupportsAutomaticGraphicsSwitching,
        argc==2?kCGLPFARendererID:0,(CGLPixelFormatAttribute)intel,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attributes,&format,&count)&&format);
    assert(!CGLCreateContext(format,NULL,&context)&&context);
    CGLDestroyPixelFormat(format);assert(!CGLSetCurrentContext(context));
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"none");assert(renderer&&strstr(renderer,"Intel"));
    struct Output output;GLfloat color[]={0.25f,0.5f,0.75f,1.0f};
    for(unsigned q=0;q<sizeof(queries)/sizeof(queries[0]);++q){
        const struct Query *query=&queries[q];
        for(unsigned floating=0;floating<2;++floating){
            seed(&output);
            if(floating)glGetFloatv(query->pname,output.value.floats);
            else glGetIntegerv(query->pname,output.value.integers);
            GLenum error=glGetError();
            if(error)fprintf(stderr,"query=%s floating=%u error=%x\n",query->name,floating,error);
            assert(!error);guards(&output);
            for(unsigned i=query->count;i<4;++i)assert((uint32_t)output.value.integers[i]==0xa5a5a5a5u);
        }
    }
    printf("audited fixed-size server queries=%lu (integer and float)\n",(unsigned long)(sizeof(queries)/sizeof(queries[0])));
    glFrontFace(GL_CW);glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL,GL_SEPARATE_SPECULAR_COLOR);
    seed(&output);glGetIntegerv(GL_FRONT_FACE,output.value.integers);
    assert(!glGetError()&&output.value.integers[0]==GL_CW);guards(&output);tail(&output);
    seed(&output);glGetIntegerv(GL_LIGHT_MODEL_COLOR_CONTROL,output.value.integers);
    assert(!glGetError()&&output.value.integers[0]==GL_SEPARATE_SPECULAR_COLOR);guards(&output);tail(&output);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_ADD);
    glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,color);assert(!glGetError());
    seed(&output);glGetTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,output.value.integers);
    assert(!glGetError()&&output.value.integers[0]==GL_COMBINE);guards(&output);tail(&output);
    seed(&output);glGetTexEnvfv(GL_TEXTURE_ENV,GL_COMBINE_RGB,output.value.floats);
    assert(!glGetError()&&output.value.floats[0]==(GLfloat)GL_ADD);guards(&output);tail(&output);
    seed(&output);glGetTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,output.value.floats);
    assert(!glGetError()&&!memcmp(output.value.floats,color,sizeof(color)));guards(&output);
    seed(&output);glGetTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,output.value.integers);
    assert(!glGetError()&&output.value.integers[0]>0&&
        output.value.integers[0]<output.value.integers[1]&&
        output.value.integers[1]<output.value.integers[2]&&
        output.value.integers[3]==INT32_MAX);guards(&output);
    struct Output original;seed(&original);output=original;
    glGetTexEnviv(0xdead,GL_TEXTURE_ENV_COLOR,output.value.integers);
    assert(glGetError()==GL_INVALID_ENUM&&!memcmp(&output,&original,sizeof(output)));
    glGetTexEnvfv(GL_TEXTURE_ENV,0xdead,output.value.floats);
    assert(glGetError()==GL_INVALID_ENUM&&!memcmp(&output,&original,sizeof(output)));
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    puts("PASS QC scalar state and native scalar/vector texture-environment values, integer conversion, guards and failed-query output preservation");
    return 0;
}
