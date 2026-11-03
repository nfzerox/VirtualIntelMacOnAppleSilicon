/* Actual executor regression for QuickTime's short software ARB compiler test.
 * --double --unsafe-native-query is a deliberate negative control: Tahoe
 * Intel Float faults in glsFlushTexture+68 after the native query.
 * No VM, other process, application data or global setting is touched. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
struct CompileWork {struct DGHostContext *context;const char *source;size_t length;};
static void *compile_on_thread(void *opaque)
{
    struct CompileWork *w=opaque;assert(!CGLSetCurrentContext(w->context->cgl));
    DGHostARBProgramString(w->context,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,(GLsizei)w->length,w->source);
    assert(!CGLSetCurrentContext(NULL));return NULL;
}
int main(int argc,char **argv)
{
    int unsafe=0,double_buffer=0,retire_software=0,software_upload=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--unsafe-native-query"))unsafe=1;
        else if(!strcmp(argv[i],"--double"))double_buffer=1;
        else if(!strcmp(argv[i],"--retire-software"))retire_software=1;
        else if(!strcmp(argv[i],"--software-upload"))software_upload=1;
        else return 2;
    }
    setbuf(stdout,NULL);dg_storage_reuse=1;
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj share=NULL;GLint count=0;
    struct DGHostContext shared={.profile=kCGLOGLPVersion_Legacy,.draw_mode=GL_FRONT,.read_mode=GL_FRONT};
    struct DGHostContext c={.profile=kCGLOGLPVersion_Legacy,
        .draw_mode=GL_FRONT,.read_mode=GL_FRONT,.native_hardware_screen=-1};
    if(double_buffer){c.double_buffer=1;c.draw_mode=c.read_mode=GL_BACK;}
    assert(!CGLChoosePixelFormat(a,&pf,&count)&&pf);
    assert(!CGLCreateContext(pf,NULL,&share)&&!CGLSetCurrentContext(share));
    shared.cgl=share;assert(DGCreateAliases(&shared,NULL)&&DGSelectDrawable(&shared,1,64,64));
    GLuint input=1;assert(!DGPrivateTextureClaim(DGPrivateTexturesCurrentRegistry(),input));
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,input);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,640,359,0,GL_RGBA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
    assert(!CGLCreateContext(pf,share,&c.cgl));c.native_screen_count=count;
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl)&&DGCreateAliases(&c,&shared));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    assert(DGSelectDrawable(&c,1,64,64));
    GLuint program=0;
    for(unsigned i=0;i<(retire_software?2:3);++i){
        struct DGMessage select={.op=DG_SELECT_NATIVE_RENDERER,.flags=DG_REPLY,.args={i&1}};
        struct DGReply reply={0};assert(!DGNativeRendererCommand(&c,&select,&reply)&&!reply.error);
        printf("renderer=%s\n",glGetString(GL_RENDERER));
        glGetString(GL_EXTENSIONS);glGetString(GL_VENDOR);glGetString(GL_RENDERER);
        GLint coordinates=0;glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB,&coordinates);glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB,&coordinates);
        if(i==1){
            GLuint programs[6]={0};glGenProgramsARB(6,programs);program=programs[0];glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,program);
            static const char source[]="!!ARBfp1.0\nTEMP R0;\nMOV result.color, fragment.color;\nEND\n";
            char data[4096];const char *file=getenv("VIM_PROBE_ARB_SOURCE");size_t bytes=0;
            if(file){FILE *f=fopen(file,"rb");assert(f);bytes=fread(data,1,sizeof(data),f);assert(bytes&&bytes<sizeof(data));fclose(f);}
            struct CompileWork work={&c,file?data:source,file?bytes:sizeof(source)-1};
            if(getenv("VIM_PROBE_ARB_THREAD")){
                assert(!CGLSetCurrentContext(NULL));pthread_t thread;assert(!pthread_create(&thread,NULL,compile_on_thread,&work));
                assert(!pthread_join(thread,NULL)&&!CGLSetCurrentContext(c.cgl));
            }else DGHostARBProgramString(&c,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,(GLsizei)work.length,work.source);
            GLint position=-2;glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);printf("program error position=%d\n",position);
            GLint draw=0,read=0,after=0,native=-1;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
            if(unsafe)glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,&native);
            assert(!c.error&&!glGetError());
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&after);assert(after==draw);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&after);assert(after==read);
            /* Flush BEFORE any draw/read can mask the invalid native texture. */
            struct DGMessage flush={0};struct DGReply submitted={0};
            if(!unsafe)assert(!DGFlushDrawableState(&c,&flush,&submitted)&&!submitted.error);
            puts("PASS software compilation, split framebuffer bindings and immediate flush");
            if(software_upload){
                struct DGMessage upload={.op=DG_OP_tex_image2D};
                assert(DGNativeRendererPrepareCommand(&c,&upload));
                assert(!double_buffer||c.native_drawable_deferred);
                glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGB8,64,64,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
                puts("PASS software texture allocation");
            }
        }
        /* The double-buffer case models QuickTime's compiler-only recovery;
         * do not claim software rendering to two IOSurface attachments here. */
        if(i&&(!double_buffer||i!=1)){
            struct DGMessage draw={.op=DG_OP_clear};assert(DGNativeRendererPrepareCommand(&c,&draw));
            assert(!c.native_drawable_deferred);
            glClearColor(i==0,i==1,i==2,1);glClear(GL_COLOR_BUFFER_BIT);
            unsigned char pixel[4]={0};glReadPixels(3,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            assert(!glGetError()&&pixel[0]==(i==0?255:0)&&pixel[1]==(i==1?255:0)&&pixel[2]==(i==2?255:0)&&pixel[3]==255);
        }
        CGLSetCurrentContext(NULL);assert(!CGLSetCurrentContext(c.cgl));
    }
    glDeleteProgramsARB(1,&program);glFinish();DGDestroyDrawables(&c);DGDestroyAliases(&c);
    glDeleteTextures(1,&input);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    CGLSetCurrentContext(share);DGDestroyDrawables(&shared);DGDestroyAliases(&shared);
    CGLSetCurrentContext(NULL);CGLDestroyContext(share);
    puts("PASS hardware/software/hardware pixels and teardown");return 0;
}
