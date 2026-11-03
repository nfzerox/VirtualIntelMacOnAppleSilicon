/* Actual native shader/program queries, or the actual production wire client
 * (including its cache), not an alternate GL implementation. No UI defaults,
 * shader workarounds or timing sleeps. Run native on physical INTEL first.
 */
#ifdef VIM_LOCATION_NATIVE
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-program-locations.h"
typedef struct {CGLContextObj cgl;GLuint framebuffer,texture;} *TestContext;
static TestContext make(TestContext share)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};CGLPixelFormatObj format=NULL;
    GLint count=0;CGLContextObj context=NULL;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format)return NULL;
    CGLError error=CGLCreateContext(format,share?share->cgl:NULL,&context);CGLDestroyPixelFormat(format);
    if(error)return NULL;
    TestContext result=calloc(1,sizeof(*result));
    if(!result){CGLDestroyContext(context);return NULL;}result->cgl=context;return result;
}
static int current(TestContext context)
{
    if(CGLSetCurrentContext(context->cgl))return 0;
    if(!context->framebuffer){
        glGenTextures(1,&context->texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,context->texture);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffersEXT(1,&context->framebuffer);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,context->framebuffer);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,context->texture,0);
        return glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,context->framebuffer);return 1;
}
static void destroy(TestContext context)
{CGLSetCurrentContext(context->cgl);glDeleteFramebuffersEXT(1,&context->framebuffer);
 glDeleteTextures(1,&context->texture);CGLSetCurrentContext(NULL);CGLDestroyContext(context->cgl);free(context);}
#else
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
typedef struct DGContext *TestContext;
static unsigned char pixels[64*64*4];
static TestContext make(TestContext share){return DGCreateContext(NULL,share);}
static int current(TestContext context){return DGMakeCurrent(context,pixels,GL_UNSIGNED_BYTE,64,64);}
static void destroy(TestContext context){DGDestroyContext(context);}
#endif
static int failed;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;}}while(0)
static const char *vertex_source="#version 120\nvoid main(){gl_Position=gl_Vertex;}\n";
static const char *red_source="#version 120\nuniform vec4 tint;void main(){gl_FragColor=tint;}\n";
static const char *blue_source="#version 120\nuniform vec4 accent;void main(){gl_FragColor=accent;}\n";
static GLhandleARB shader(GLenum type,const char *source)
{
    GLhandleARB object=glCreateShaderObjectARB(type);CHECK(object!=0);
    glShaderSourceARB(object,1,&source,NULL);glCompileShaderARB(object);
    GLint compiled=0;glGetObjectParameterivARB(object,GL_OBJECT_COMPILE_STATUS_ARB,&compiled);
    if(!compiled){char message[2048]={0};glGetInfoLogARB(object,sizeof(message),NULL,message);
        fprintf(stderr,"shader: %s\n",message);failed=1;}
    return object;
}
static void link_program(GLhandleARB program,int wanted)
{
    GLint linked=0;glLinkProgramARB(program);
    glGetObjectParameterivARB(program,GL_OBJECT_LINK_STATUS_ARB,&linked);CHECK(!!linked==wanted);
}
static void verify_uniform_drawing(GLhandleARB program,GLint location)
{
    glUseProgramObjectARB(program);glUniform4fARB(location,1,0,1,1);glViewport(0,0,64,64);
#ifdef VIM_LOCATION_NATIVE
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
#else
    glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
#endif
    glBegin(GL_TRIANGLE_STRIP);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(-1,1);glVertex2f(1,1);glEnd();
    unsigned char pixel[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    CHECK(pixel[0]==255&&pixel[1]==0&&pixel[2]==255&&pixel[3]==255);CHECK(glGetError()==GL_NO_ERROR);
    glUseProgramObjectARB(0);
}
static void cache_lifetime_controls(void)
{
    struct DGProgramLocations *s=calloc(1,sizeof(*s));CHECK(s!=NULL);if(!s)return;
    uint64_t generation=0,old=0;int32_t value=0;
    CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value)&&generation==0);
    DGLocationBorn(s,7);CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value)&&generation);
    DGLocationStore(s,7,"tint",4,generation,12);
    CHECK(DGLocationLookup(s,7,"tint",4,&old,&value)&&value==12);
    DGLocationMutation(s,DG_LOCATION_LINK,7);
    DGLocationStore(s,7,"tint",4,old,99); // In-flight pre-link fill must fail.
    CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value));
    DGLocationStore(s,7,"tint",4,generation,-1);
    CHECK(DGLocationLookup(s,7,"tint",4,&generation,&value)&&value==-1);
    old=generation;DGLocationMutation(s,DG_LOCATION_DELETE,7);
    DGLocationStore(s,7,"tint",4,old,99);
    CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value)&&!generation);
    DGLocationBorn(s,7); // Actual numeric-name reuse gets a new generation.
    DGLocationStore(s,7,"tint",4,old,99);
    CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value)&&generation!=old);
    DGLocationStore(s,7,"tint",4,generation,23);
    CHECK(DGLocationLookup(s,7,"tint",4,&generation,&value)&&value==23);
    DGLocationMutation(s,DG_LOCATION_FORGET,0);
    CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value)&&!generation);
    DGLocationMutation(s,DG_LOCATION_LINK,7); // A link cannot revive forgotten eligibility.
    CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value)&&!generation);
    for(unsigned i=0;i<1000;++i)DGLocationMutation(s,DG_LOCATION_FORGET,0);
    DGLocationBorn(s,7);CHECK(DGLocationGeneration(s,7)>s->forgotten);
    s->serial=UINT64_MAX;DGLocationBorn(s,7);CHECK(s->exhausted);
    CHECK(!DGLocationLookup(s,7,"tint",4,&generation,&value)&&!generation);
    DGLocationBorn(s,7);CHECK(!DGLocationGeneration(s,7));free(s);
}
int main(int argc,char **argv)
{
    int old_peer=argc==2&&!strcmp(argv[1],"--old-peer");
    if(argc>2||(argc==2&&!old_peer))return 2;
    cache_lifetime_controls();TestContext a=make(NULL),b=NULL;CHECK(a&&current(a));if(!a)return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);printf("renderer=%s\n",renderer?renderer:"none");
    CHECK(renderer&&strstr(renderer,"Intel"));if(!renderer||!strstr(renderer,"Intel"))return 2;
#ifndef VIM_LOCATION_NATIVE
    int negotiated=!!(a->connection->capabilities&DG_CAP_VALIDATED_UNIFORM_LOCATIONS);
    CHECK(negotiated!=old_peer);printf("validated-uniform-replies=%d\n",negotiated);
#else
    if(old_peer)return 2;
#endif
    b=make(a);CHECK(b!=NULL);if(!b)return 2;
    GLhandleARB vertex=shader(GL_VERTEX_SHADER_ARB,vertex_source),red=shader(GL_FRAGMENT_SHADER_ARB,red_source);
    GLhandleARB program=glCreateProgramObjectARB();CHECK(program!=0);
    glAttachObjectARB(program,vertex);glAttachObjectARB(program,red);link_program(program,1);
    CHECK(glGetError()==GL_NO_ERROR);
    GLint tint=glGetUniformLocationARB(program,"tint");CHECK(tint>=0);
#ifndef VIM_LOCATION_NATIVE
    uint64_t hits=a->connection->program_locations.hits,fills=a->connection->program_locations.fills;
#endif
    for(unsigned i=0;i<100;++i)CHECK(glGetUniformLocationARB(program,"tint")==tint);
#ifndef VIM_LOCATION_NATIVE
    CHECK(a->connection->program_locations.hits==hits+(negotiated?100:0));
    CHECK(a->connection->program_locations.fills==fills);
#endif
    CHECK(glGetUniformLocationARB(program,"absent")==-1);
    CHECK(glGetUniformLocationARB(program,"absent")==-1);CHECK(glGetError()==GL_NO_ERROR);
    glEnable(0xffffffffu);CHECK(glGetUniformLocationARB(program,"tint")==tint);
    CHECK(glGetError()==GL_INVALID_ENUM);CHECK(glGetError()==GL_NO_ERROR);
    glBegin(GL_POINTS);GLint inside=glGetUniformLocationARB(program,"tint");glEnd();
    GLenum inside_error=glGetError();CHECK(inside_error==GL_INVALID_OPERATION);
    printf("BeginQuery result=%d error=%x\n",inside,inside_error);
    // A different shared context changes the SAME program interface.
    CHECK(current(b));CHECK(glGetUniformLocationARB(program,"tint")==tint);
    GLhandleARB blue=shader(GL_FRAGMENT_SHADER_ARB,blue_source);
    glDetachObjectARB(program,red);glAttachObjectARB(program,blue);link_program(program,1);
    CHECK(current(a));CHECK(glGetUniformLocationARB(program,"tint")==-1);
    GLint accent=glGetUniformLocationARB(program,"accent");CHECK(accent>=0);
    CHECK(glGetUniformLocationARB(program,"accent")==accent);CHECK(glGetError()==GL_NO_ERROR);
    verify_uniform_drawing(program,accent);
    // Independent connections are independent program namespaces, even when
    // native GL recycles the same numeric names in them.
    TestContext independent=make(NULL);CHECK(independent&&current(independent));
    if(!independent)return 2;
    GLhandleARB iv=shader(GL_VERTEX_SHADER_ARB,vertex_source),ir=shader(GL_FRAGMENT_SHADER_ARB,red_source);
    GLhandleARB ip=glCreateProgramObjectARB();glAttachObjectARB(ip,iv);glAttachObjectARB(ip,ir);link_program(ip,1);
    CHECK(glGetUniformLocationARB(ip,"accent")==-1);GLint it=glGetUniformLocationARB(ip,"tint");CHECK(it>=0);
    verify_uniform_drawing(ip,it);glDeleteObjectARB(ip);glDeleteObjectARB(iv);glDeleteObjectARB(ir);
    CHECK(glGetError()==GL_NO_ERROR);destroy(independent);CHECK(current(a));
    CHECK(glGetUniformLocationARB(program,"tint")==-1);
    CHECK(glGetUniformLocationARB(program,"accent")==accent);CHECK(glGetError()==GL_NO_ERROR);
    char long_name[256];memset(long_name,'x',sizeof(long_name)-1);long_name[255]=0;
#ifndef VIM_LOCATION_NATIVE
    hits=a->connection->program_locations.hits;fills=a->connection->program_locations.fills;
#endif
    CHECK(glGetUniformLocationARB(program,long_name)==-1);CHECK(glGetUniformLocationARB(program,long_name)==-1);
#ifndef VIM_LOCATION_NATIVE
    CHECK(a->connection->program_locations.hits==hits&&a->connection->program_locations.fills==fills);
#endif
    // Repeated bad links must not retain the old successful interface.
    GLhandleARB bad=shader(GL_FRAGMENT_SHADER_ARB,"#version 120\nvarying vec4 not_written;void main(){gl_FragColor=not_written;}\n");
    glDetachObjectARB(program,blue);glAttachObjectARB(program,bad);link_program(program,0);
    GLint unlinked=glGetUniformLocationARB(program,"accent");GLenum link_error=glGetError();
    CHECK(link_error==GL_INVALID_OPERATION);printf("UnlinkedQuery result=%d error=%x\n",unlinked,link_error);
    for(unsigned i=0;i<3;++i){
        CHECK(glGetUniformLocationARB(program,"accent")==-1);
        CHECK(glGetError()==GL_INVALID_OPERATION);
    }
    glDetachObjectARB(program,bad);glAttachObjectARB(program,blue);link_program(program,1);
    accent=glGetUniformLocationARB(program,"accent");CHECK(accent>=0);CHECK(glGetError()==GL_NO_ERROR);
#ifndef VIM_LOCATION_NATIVE
    // Off/on is an owned diagnostic, never a persisted opt-in. Mutations must
    // still invalidate while off, so turning it on cannot revive old values.
    dg_program_locations_reuse=0;glDetachObjectARB(program,blue);glAttachObjectARB(program,red);link_program(program,1);
    CHECK(glGetUniformLocationARB(program,"accent")==-1);dg_program_locations_reuse=1;
    CHECK(glGetUniformLocationARB(program,"accent")==-1);
    glDetachObjectARB(program,red);glAttachObjectARB(program,blue);link_program(program,1);
    accent=glGetUniformLocationARB(program,"accent");CHECK(accent>=0);CHECK(glGetError()==GL_NO_ERROR);
#endif
    // Pending-deleted programs can still be bound, but are never cached.
    glUseProgramObjectARB(program);glDeleteObjectARB(program);
#ifndef VIM_LOCATION_NATIVE
    hits=a->connection->program_locations.hits;fills=a->connection->program_locations.fills;
#endif
    GLint pending=glGetUniformLocationARB(program,"accent");GLenum pending_error=glGetError();
    CHECK(pending==accent&&pending_error==GL_NO_ERROR);
#ifndef VIM_LOCATION_NATIVE
    CHECK(a->connection->program_locations.hits==hits&&a->connection->program_locations.fills==fills);
#endif
    glUseProgramObjectARB(0);
    GLint deleted=glGetUniformLocationARB(program,"accent");GLenum deleted_error=glGetError();
    CHECK(deleted_error!=GL_NO_ERROR);printf("DeletedQuery result=%d error=%x\n",deleted,deleted_error);
    glDeleteObjectARB(vertex);glDeleteObjectARB(red);glDeleteObjectARB(blue);glDeleteObjectARB(bad);
    CHECK(glGetError()==GL_NO_ERROR);destroy(b);destroy(a);
    printf("%s actual uniform locations: repeated/missing queries, pending errors, Begin, shared-context relink, independent namespaces, rendered pixels, failed relink, long-name fallback, pending deletion, name-reuse/stale-fill/overflow controls\n",failed?"FAIL":"PASS");
    return failed;
}
