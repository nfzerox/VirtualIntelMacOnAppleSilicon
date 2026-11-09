/* Compare native register-combiner exports with actual per-context support. */
#ifdef VIM_NV_COMBINERS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <string.h>

/* Apple's SDK hides both extensions even though the exports are present. */
#ifndef VIM_NV_COMBINERS_WIRE
extern void glGetCombinerInputParameterfvNV(GLenum,GLenum,GLenum,GLenum,GLfloat *);
extern void glGetCombinerInputParameterivNV(GLenum,GLenum,GLenum,GLenum,GLint *);
extern void glGetCombinerOutputParameterfvNV(GLenum,GLenum,GLenum,GLfloat *);
extern void glGetCombinerOutputParameterivNV(GLenum,GLenum,GLenum,GLint *);
extern void glGetFinalCombinerInputParameterfvNV(GLenum,GLenum,GLfloat *);
extern void glGetFinalCombinerInputParameterivNV(GLenum,GLenum,GLint *);
extern void glCombinerStageParameterfvNV(GLenum,GLenum,const GLfloat *);
extern void glGetCombinerStageParameterfvNV(GLenum,GLenum,GLfloat *);
extern void glSamplePass(GLenum);
extern void glSamplePassARB(GLenum);
#endif
enum { variable_a=0x8523, constant_color0=0x852a, combiner_input=0x8542,
       combiner_ab_output=0x854a, combiner0=0x8550 };

#define QUERY(name,...) do { \
    float f[]={-91,-92,-93,-94,-95};int i[]={-91,-92,-93,-94,-95}; \
    glGet##name##fvNV(__VA_ARGS__,f); GLenum ef=glGetError(); \
    glGet##name##ivNV(__VA_ARGS__,i); GLenum ei=glGetError(); \
    printf(#name " errors=%04x/%04x results=%g,%g/%d,%d\n",ef,ei,(double)f[0],(double)f[1],i[0],i[1]); \
} while(0)

int main(int argc,char **argv)
{
    int core=argc>1 && !strcmp(argv[1],"core");
#ifdef VIM_NV_COMBINERS_WIRE
    const int attrs[]={0x56494d02,core?0x3200:0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *ctx=DGCreateContext(attrs,NULL);
    if(!ctx || !DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    const char *extensions=core?NULL:(const char *)glGetString(GL_EXTENSIONS);
    printf("combiner renderer=%s core=%d extensions=%d/%d\n",glGetString(GL_RENDERER),core,
        extensions && strstr(extensions,"GL_NV_register_combiners ")!=NULL,
        extensions && strstr(extensions,"GL_NV_register_combiners2")!=NULL);
    QUERY(CombinerInputParameter,combiner0,GL_RGB,variable_a,combiner_input);
    QUERY(CombinerOutputParameter,combiner0,GL_RGB,combiner_ab_output);
    QUERY(FinalCombinerInputParameter,variable_a,combiner_input);
    const GLfloat color[]={0.25f,0.5f,0.75f,1};
    GLfloat observed[]={-91,-92,-93,-94,-95};
    glCombinerStageParameterfvNV(combiner0,constant_color0,color);
    GLenum set=glGetError();
    glGetCombinerStageParameterfvNV(combiner0,constant_color0,observed);
    GLenum read=glGetError();
    printf("combiner stage errors=%04x/%04x data=%g,%g,%g,%g tail=%g\n",set,read,
        (double)observed[0],(double)observed[1],(double)observed[2],(double)observed[3],(double)observed[4]);
    QUERY(CombinerInputParameter,combiner0,GL_RGB,variable_a,0);
    /* 1PASS, 2PASS_0, and an invalid token. SampleCoverage is a separate
     * interface; selecting a legacy pass must not change its coverage. */
    const GLenum modes[]={0x80a1,0x80a2,0};
    glSampleCoverage(0.5f,GL_TRUE);
    GLenum coverage=glGetError();
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i){
        glSamplePass(modes[i]);GLenum pass=glGetError();
        GLfloat result=-1;glGetFloatv(GL_SAMPLE_COVERAGE_VALUE,&result);
        GLenum query=glGetError();
        printf("sample pass mode=%04x errors=%04x/%04x/%04x coverage=%g\n",
            modes[i],coverage,pass,query,(double)result);
#ifndef VIM_NV_COMBINERS_WIRE
        glSamplePassARB(modes[i]);
        if(glGetError()!=pass)return 3;
#endif
    }
#ifdef VIM_NV_COMBINERS_WIRE
    DGDestroyContext(ctx);
#else
    if(argc>1 && !strcmp(argv[argc-1],"gli")){
        for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i){
            if(!ctx->disp.sample_pass)return 4;
            ctx->disp.sample_pass(ctx->rend,modes[i]);
            printf("GLI sample pass mode=%04x error=%04x\n",modes[i],glGetError());
        }
#define SLOT(field,ctype,...) do { \
        ctype out[]={-91,-92,-93,-94,-95}; \
        ctx->disp.field(ctx->rend,__VA_ARGS__,out); \
        printf("GLI " #field " error=%04x data=%g,%g,%g,%g,%g\n",glGetError(), \
            (double)out[0],(double)out[1],(double)out[2],(double)out[3],(double)out[4]); \
    } while(0)
        SLOT(get_combiner_input_parameterfv_NV,GLfloat,combiner0,GL_RGB,variable_a,combiner_input);
        SLOT(get_combiner_input_parameteriv_NV,GLint,combiner0,GL_RGB,variable_a,combiner_input);
        SLOT(get_combiner_output_parameterfv_NV,GLfloat,combiner0,GL_RGB,combiner_ab_output);
        SLOT(get_combiner_output_parameteriv_NV,GLint,combiner0,GL_RGB,combiner_ab_output);
        SLOT(get_final_combiner_input_parameterfv_NV,GLfloat,variable_a,combiner_input);
        SLOT(get_final_combiner_input_parameteriv_NV,GLint,variable_a,combiner_input);
        ctx->disp.combiner_stage_parameterfv_NV(ctx->rend,combiner0,constant_color0,color);
        printf("GLI combiner_stage_parameterfv_NV error=%04x\n",glGetError());
        SLOT(get_combiner_stage_parameterfv_NV,GLfloat,combiner0,constant_color0);
#undef SLOT
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
#endif
    return 0;
}
