/* Owned-process GL execution control. --tap changes only this diagnostic's
 * SDK-published CGL generate callback, never a driver image or another process.
 * --adapted calls our existing explicit helper AFTER the upload/list returns;
 * that is a mechanism control, not an implementation of deferred semantics. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
#include "../PassthroughOpenGL/passthrough-opengl-automatic-mipmap-host.h"
static unsigned native_pairs_created,native_pairs_destroyed;
#define DG_NATIVE_MIPMAP_CREATED() (++native_pairs_created)
#define DG_NATIVE_MIPMAP_DESTROYED() (++native_pairs_destroyed)
#include "opengl-native-mipmap-list-diagnostic.h"

static void (*native_generate)(GLIContext,GLenum);
static __typeof__(((GLIFunctionDispatch *)0)->tex_sub_image2D) native_upload;
static unsigned generate_calls,upload_calls,checks,failures;
/* Bounded debugger marker: 1 immediate upload, 2 recording, 3 playback. */
unsigned VIMAutomaticExecutionPhase;
static struct DGMipmapExecution execution;
static int draw_in_list;
static void tapped_generate(GLIContext context,GLenum target)
{++generate_calls;native_generate(context,target);}
static void tapped_upload(GLIContext context,GLenum target,GLint level,GLint x,GLint y,
    GLsizei width,GLsizei height,GLenum format,GLenum type,const GLvoid *pixels)
{++upload_calls;native_upload(context,target,level,x,y,width,height,format,type,pixels);}
static void check(int ok,const char *what)
{++checks;failures+=!ok;printf("%s %s\n",ok?"PASS":"FAIL",what);fflush(stdout);}
static void fill(unsigned char *pixels,unsigned count,const unsigned char *seed)
{for(unsigned i=0;i<count*4;++i)pixels[i]=seed[i%4];}
static int near(const unsigned char *a,const unsigned char *b)
{for(unsigned i=0;i<4;++i){int delta=(int)a[i]-b[i];if(delta < -1||delta > 1)return 0;}return 1;}
static void draw(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
}
static void drawn(const unsigned char *seed,const char *what)
{
    unsigned char pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();printf("drawn=%u,%u,%u,%u error=%x\n",pixel[0],pixel[1],pixel[2],pixel[3],error);
    check(!error&&near(pixel,seed),what);
}
static void child(GLint level,const unsigned char *seed)
{
    unsigned char pixels[16]={0},gpu[4]={0};GLuint fbo=0;
    glGetTexImage(GL_TEXTURE_2D,level,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();
    printf("child level=%d GetTexImage=%u,%u,%u,%u error=%x\n",level,pixels[0],pixels[1],pixels[2],pixels[3],error);
    check(!error&&near(pixels,seed),"child texture readback has updated image");
    GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,(GLuint)texture,level);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    check(status==GL_FRAMEBUFFER_COMPLETE_EXT,"child is a complete native GPU attachment");
    if(status==GL_FRAMEBUFFER_COMPLETE_EXT)glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,gpu);
    error=glGetError();printf("child GPU ReadPixels=%u,%u,%u,%u status=%x error=%x\n",gpu[0],gpu[1],gpu[2],gpu[3],status,error);
    check(!error&&status==GL_FRAMEBUFFER_COMPLETE_EXT&&near(gpu,seed),"GPU attachment has updated child image");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
}
static void run(CGLContextObj context,int base,int list,int adapted,int tap,int apple,int tap_upload,int native_list)
{
    GLuint texture=0,commands=0,result=0,framebuffer=0;unsigned char pixels[1024];
    struct DGNativeMipmapList owner={0};
    GLenum native_error=0,list_mode=list==2?GL_COMPILE_AND_EXECUTE:GL_COMPILE;
    const unsigned char before[]={17,71,131,191},after[]={201,101,51,171};
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,base);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,base+2);
    for(int level=0;level<=base;++level){
        int size=4<<(base-level);fill(pixels,(unsigned)(size*size),before);
        glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    }
    /* Defined sentinel children make the pre-execution result unambiguous. */
    fill(pixels,4,before);glTexImage2D(GL_TEXTURE_2D,base+1,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexImage2D(GL_TEXTURE_2D,base+2,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,apple?0x8a4b:GL_GENERATE_MIPMAP,GL_TRUE);
    check(!glGetError(),"automatic execution source accepted");
    if(draw_in_list){
        glGenTextures(1,&result);glBindTexture(GL_TEXTURE_2D,result);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,result,0);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"list draw has its own complete GPU target");
        glBindTexture(GL_TEXTURE_2D,texture);glEnable(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,1);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,1);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glViewport(0,0,1,1);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    }
    generate_calls=upload_calls=0;
    if(tap){native_generate=context->disp.generate_mipmap_EXT;context->disp.generate_mipmap_EXT=tapped_generate;}
    printf("case base=%d list=%d adapted=%d tap=%d apple=%d tap_upload=%d native_list=%d\n",base,list,adapted,tap,apple,tap_upload,native_list);fflush(stdout);
    fill(pixels,16,after);
    if(list){commands=glGenLists(1);glNewList(commands,list_mode);}
    struct DGNativeMipmapPair *pair=native_list&&list?
        DGNativeMipmapBegin(DG_NATIVE_MIPMAP_SUB2,GL_TEXTURE_2D,base,1,0,list_mode,&owner):NULL;
    if(native_list&&list)check(pair!=NULL,"native execution-bound pair recorded");
    struct DGAutomaticMipmapScope immediate={0};
    if(native_list)immediate=DGBeginAutomaticMipmapExecute(GL_TEXTURE_2D,base,1,0,
        list==1?GL_COMPILE:0,&native_error,0,1,&execution);
    if(tap_upload&&!list){native_upload=context->disp.tex_sub_image2D;context->disp.tex_sub_image2D=tapped_upload;}
    VIMAutomaticExecutionPhase=list?2:1;
    glTexSubImage2D(GL_TEXTURE_2D,base,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    DGEndAutomaticMipmap(&immediate,&execution,&native_error);
    DGNativeMipmapEnd(pair);
    if(draw_in_list)draw();
    if(list){
        glEndList();
        if(draw_in_list){const unsigned char empty[4]={0};
            drawn(list==2?after:empty,"draw execution matches compile versus compile/execute mode");}
        /* Wrap the actual execution table, not its temporary compiler table. */
        if(tap_upload){native_upload=context->disp.tex_sub_image2D;context->disp.tex_sub_image2D=tapped_upload;}
        VIMAutomaticExecutionPhase=3;
        struct DGNativeMipmapPlayback playback={context,&execution,&native_error,0,1};
        struct DGNativeMipmapPlayback *previous=dg_native_mipmap_playback;
        if(native_list)dg_native_mipmap_playback=&playback;
        glCallList(commands);
        dg_native_mipmap_playback=previous;
    }
    VIMAutomaticExecutionPhase=0;
    unsigned automatic_calls=generate_calls;
    int retained=!tap||context->disp.generate_mipmap_EXT==tapped_generate;
    printf("execution automatic_callback_calls=%u published_slot_retained=%d upload_error=%x\n",
        automatic_calls,retained,glGetError());
    if(tap_upload)printf("execution upload_callback_calls=%u upload_slot_retained=%d\n",
        upload_calls,context->disp.tex_sub_image2D==tapped_upload);
    if(native_list)check(!native_error,"execution-bound mipmaps leave no adapter error");
    if(draw_in_list)drawn(after,"draw inside the list samples the updated child before returning");
    if(adapted)check(!DGGenerateSrgbMipmapExecute(GL_TEXTURE_2D,1,0,&execution),"post-execution explicit correction accepted");
    child(base+1,after);
    if(tap_upload){
        unsigned old=upload_calls;
        glTexSubImage2D(GL_TEXTURE_2D,base,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        check(context->disp.tex_sub_image2D==tapped_upload&&upload_calls==old+1,
            "ordinary upload reaches the diagnostic execution callback");
        context->disp.tex_sub_image2D=native_upload;glGetError();
    }
    if(tap){
        unsigned old=generate_calls;glGenerateMipmapEXT(GL_TEXTURE_2D);
        check(retained&&generate_calls==old+1,"explicit call reaches diagnostic published callback");
        context->disp.generate_mipmap_EXT=native_generate;
        /* Native generation may corrupt nonzero children again; no image
         * assertion is made after this positive dispatch-routing control. */
        glGetError();
    }
    if(commands)glDeleteLists(commands,1);glDeleteTextures(1,&texture);
    DGNativeMipmapFreePairs(&owner);
    if(framebuffer)glDeleteFramebuffersEXT(1,&framebuffer);if(result)glDeleteTextures(1,&result);
}
static void call(CGLContextObj context,GLuint list,int adapted,GLenum *error)
{
    struct DGNativeMipmapPlayback playback={context,&execution,error,0,1};
    struct DGNativeMipmapPlayback *previous=dg_native_mipmap_playback;
    if(adapted)dg_native_mipmap_playback=&playback;
    glCallList(list);dg_native_mipmap_playback=previous;
}
static void shared_lists(CGLContextObj consumer,int adapted)
{
    const unsigned char before[]={17,71,131,191},after[]={201,101,51,171};
    unsigned char pixels[1024];GLuint textures[2],child_list=0,parents[2];GLenum error=0;
    struct DGNativeMipmapList owner={0};
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj producer=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format){check(0,"shared list format");return;}
    CGLError result=CGLCreateContext(format,consumer,&producer);CGLDestroyPixelFormat(format);
    if(result||!producer){check(0,"shared list producer");return;}
    CGLSetCurrentContext(producer);glGenTextures(2,textures);
    for(unsigned t=0;t<2;++t){
        glBindTexture(GL_TEXTURE_2D,textures[t]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,2);
        for(int level=0;level<5;++level){int size=16>>level;fill(pixels,(unsigned)(size*size),before);
            glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);}
    }
    glBindTexture(GL_TEXTURE_2D,textures[0]);child_list=glGenLists(1);glNewList(child_list,GL_COMPILE);
    /* Force real native command-buffer growth before our retained records. */
    for(unsigned i=0;i<1024;++i)glColor4f((float)i/1024,0,0,1);
    glBindTexture(GL_TEXTURE_2D,textures[1]);fill(pixels,16,after);
    /* Grow native command storage across many helper/upload boundaries, not
     * just before the first helper. Opaque records must survive optimization. */
    for(unsigned upload=0;upload<129;++upload){
        glColor4ub((GLubyte)upload,7,19,255);
        struct DGNativeMipmapPair *pair=adapted?DGNativeMipmapBegin(DG_NATIVE_MIPMAP_SUB2,
            GL_TEXTURE_2D,2,1,0,GL_COMPILE,&owner):NULL;
        glTexSubImage2D(GL_TEXTURE_2D,2,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);DGNativeMipmapEnd(pair);
    }
    glEndList();memset(pixels,0,sizeof(pixels));
    parents[0]=glGenLists(1);glNewList(parents[0],GL_COMPILE);glCallList(child_list);glEndList();
    parents[1]=glGenLists(1);glNewList(parents[1],GL_COMPILE);glListBase(child_list);
    GLuint zero=0;glCallLists(1,GL_UNSIGNED_INT,&zero);glEndList();
    check(!glGetError(),"shared list compilation and native command growth accepted");
    CGLSetCurrentContext(consumer);CGLDestroyContext(producer);
    glBindTexture(GL_TEXTURE_2D,textures[0]);call(consumer,parents[0],adapted,&error);
    check(!error&&!glGetError(),"shared compiled list outlives its producer context");
    child(3,before); /* Generation is disabled, despite a captured upload. */
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
    call(consumer,parents[1],adapted,&error);check(!error&&!glGetError(),"nested CallLists uses live automatic state");
    child(3,after);
    glBindTexture(GL_TEXTURE_2D,textures[0]);child(3,before);
    glNewList(child_list,GL_COMPILE);glColor4f(1,1,1,1);glEndList();
    DGNativeMipmapFreePairs(&owner);
    call(consumer,parents[0],adapted,&error);
    check(!error&&!glGetError(),"parent observes a child redefinition after old helper storage is released");
    glDeleteLists(child_list,1);glDeleteLists(parents[0],1);glDeleteLists(parents[1],1);
    glListBase(0);glDeleteTextures(2,textures);check(!glGetError(),"shared/redefined list cleanup leaves no error");
}
static void rejected_list(CGLContextObj context,int adapted)
{
    const unsigned char seed[]={17,71,131,191};unsigned char pixels[64];fill(pixels,16,seed);
    GLuint texture=0,list=glGenLists(1);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    struct DGNativeMipmapList owner={0};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);glGetError();
    glNewList(list,GL_COMPILE);
    struct DGNativeMipmapPair *pair=adapted?DGNativeMipmapBegin(DG_NATIVE_MIPMAP_SUB2,
        GL_TEXTURE_2D,0,1,0,GL_COMPILE,&owner):NULL;
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,0xdead,GL_UNSIGNED_BYTE,pixels);
    DGNativeMipmapEnd(pair);glEndList();
    GLenum compile_error=glGetError();
    printf("invalid upload compile error=%x\n",compile_error);
    check(!compile_error,"invalid list upload defers its error until playback");
    GLenum error=0;call(context,list,adapted,&error);
    GLenum playback_error=glGetError();
    printf("invalid upload playback error=%x adapter error=%x\n",playback_error,error);
    check((error?error:playback_error)==GL_INVALID_ENUM,
        "invalid list upload preserves the native playback error");
    GLint width=-1;glGetTexLevelParameteriv(GL_TEXTURE_2D,1,GL_TEXTURE_WIDTH,&width);
    check(width==0&&!glGetError(),"discarded upload leaves undefined children undefined");
    glDeleteLists(list,1);glDeleteTextures(1,&texture);
    DGNativeMipmapFreePairs(&owner);
}
static void execution_error(CGLContextObj context,int adapted)
{
    const unsigned char before[]={17,71,131,191},after[]={201,101,51,171};
    unsigned char pixels[1024];GLuint textures[2],list=glGenLists(1);GLenum error=0;
    struct DGNativeMipmapList owner={0};
    glGenTextures(2,textures);
    for(unsigned t=0;t<2;++t){
        glBindTexture(GL_TEXTURE_2D,textures[t]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,2);
        for(int level=0;level<5;++level){int size=(t?16:8)>>level;if(!size)size=1;
            fill(pixels,(unsigned)(size*size),before);
            glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);}
        glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
    }
    glBindTexture(GL_TEXTURE_2D,textures[0]);fill(pixels,16,after);
    check(!glGetError(),"execution-error sources accepted");
    glNewList(list,GL_COMPILE_AND_EXECUTE);
    struct DGNativeMipmapPair *pair=adapted?DGNativeMipmapBegin(DG_NATIVE_MIPMAP_SUB2,
        GL_TEXTURE_2D,2,1,0,GL_COMPILE_AND_EXECUTE,&owner):NULL;
    struct DGAutomaticMipmapScope scope={0};
    if(adapted)scope=DGBeginAutomaticMipmapExecute(GL_TEXTURE_2D,2,1,0,0,&error,0,1,&execution);
    /* This upload is valid to record, but too wide for the current binding.
     * Initial execution failure must not discard the later valid command. */
    glTexSubImage2D(GL_TEXTURE_2D,2,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    DGEndAutomaticMipmap(&scope,&execution,&error);DGNativeMipmapEnd(pair);glEndList();
    GLenum native=glGetError();check((error?error:native)==GL_INVALID_VALUE,
        "compile/execute preserves an initial execution-only error");error=0;
    glBindTexture(GL_TEXTURE_2D,textures[1]);call(context,list,adapted,&error);
    check(!error&&!glGetError(),"the same captured upload is valid on a later binding");
    child(3,after);
    glDeleteLists(list,1);DGNativeMipmapFreePairs(&owner);glDeleteTextures(2,textures);
}
int main(int argc,char **argv)
{
    int tap=0,adapted=0,apple=0,tap_upload=0,native_list=0,compile_execute=0;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--tap"))tap=1;
        else if(!strcmp(argv[i],"--adapted"))adapted=1;
        else if(!strcmp(argv[i],"--tap-upload"))tap_upload=1;
        else if(!strcmp(argv[i],"--native-list-adapted"))native_list=1;
        else if(!strcmp(argv[i],"--compile-execute"))compile_execute=1;
        else if(!strcmp(argv[i],"--draw-in-list"))draw_in_list=1;
        else if(!strcmp(argv[i],"--apple"))apple=1;else return 2;}
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(pf);DGMipmapCaptureExecution(context,&execution);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(int base=0;base<=2;base+=2)for(int list=0;list<(compile_execute?3:2);++list)
        run(context,base,list,adapted,tap,apple,tap_upload,native_list);
    if(compile_execute){shared_lists(context,native_list);rejected_list(context,native_list);
        execution_error(context,native_list);}
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    if(native_list)check(native_pairs_created&&native_pairs_created==native_pairs_destroyed&&
        !dg_native_mipmap_stack.count&&!dg_native_mipmap_stack.overflow,
        "native list ownership releases every helper exactly once");
    printf("automatic-execution checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
