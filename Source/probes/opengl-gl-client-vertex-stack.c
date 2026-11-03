/* Physical Intel and real typed-client oracle for client vertex state.
 * Pixel checks expose stale logical pointers even when native scalar queries
 * happen to look correct. No guest address may be sent as a host GL pointer. */
#ifdef VIM_VERTEX_STACK_DIRECT
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLRenderers.h>
#include "opengl-reference-renderer.h"
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static int failures;
static const GLfloat left[]={-.9f,-.8f,-.1f,-.8f,-.1f,.8f,-.9f,.8f};
static const GLfloat right[]={.1f,-.8f,.9f,-.8f,.9f,.8f,.1f,.8f};
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);if(!good)++failures;}
static void paint(int on_left,const char *name)
{
    unsigned char a[4]={0},b[4]={0};
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glColor4f(1,0,0,1);
    glDrawArrays(GL_QUADS,0,4);glReadPixels(3,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,a);
    glReadPixels(12,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,b);GLenum error=glGetError();
    printf("pixels left=%u,%u,%u,%u right=%u,%u,%u,%u error=%x\n",
        a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3],error);
    check(!error&&a[0]==(on_left?255:0)&&b[0]==(on_left?0:255)&&
        !a[1]&&!a[2]&&!b[1]&&!b[2]&&a[3]==255&&b[3]==255,name);
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc<2||argc>3||strcmp(argv[1],"--require-intel")||
        (argc==3&&strcmp(argv[2],"--intel-hd5000")&&strcmp(argv[2],"--old-peer")))return 2;
#ifdef VIM_VERTEX_STACK_DIRECT
    if(argc==3&&strcmp(argv[2],"--old-peer"))return 2;
    unsigned char shadow[16*16*4]={0};struct DGContext *context=DGCreateContext(NULL,NULL);
    if(!context||!DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    if(argc==3&&!strcmp(argv[2],"--old-peer"))return 2;
    GLint intel=argc==3?VIMReferenceIntelHD5000():0;if(argc==3&&!intel)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAAllowOfflineRenderers,
        kCGLPFASupportsAutomaticGraphicsSwitching,argc==3?kCGLPFARendererID:0,(CGLPixelFormatAttribute)intel,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&context)||!context)return 2;
    CGLSetCurrentContext(context);
#endif
    const char *renderer=(const char *)glGetString(GL_RENDERER);printf("renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
#ifdef VIM_VERTEX_STACK_DIRECT
    if(argc==3) {
        check(!(context->connection->capabilities&DG_CAP_CLIENT_VERTEX_STACK),"actual older executor lacks vertex-stack metadata capability");
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        check(glGetError()==GL_INVALID_OPERATION&&!context->client_attrib_stack,"old peer rejects vertex push locally without adding a frame");
        glPopClientAttrib();check(glGetError()==GL_STACK_UNDERFLOW,"old peer native stack remains unchanged");
        unsigned char pixel[4]={0};glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(!glGetError()&&pixel[0]==255&&!pixel[1]&&!pixel[2]&&pixel[3]==255&&
            !context->connection->dead,"old peer retains real red pixels and live transport");
        DGDestroyContext(context);printf("old-peer vertex-stack failures=%d\n",failures);return failures?1:0;
    }
#endif
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);glPopClientAttrib();
    check(!glGetError(),"untouched default client state can be pushed and popped");
    const GLenum default_types[]={GL_VERTEX_ARRAY_TYPE,GL_NORMAL_ARRAY_TYPE,GL_COLOR_ARRAY_TYPE,
        GL_SECONDARY_COLOR_ARRAY_TYPE,GL_FOG_COORD_ARRAY_TYPE,GL_TEXTURE_COORD_ARRAY_TYPE};
    const GLenum default_strides[]={GL_VERTEX_ARRAY_STRIDE,GL_NORMAL_ARRAY_STRIDE,GL_COLOR_ARRAY_STRIDE,
        GL_SECONDARY_COLOR_ARRAY_STRIDE,GL_FOG_COORD_ARRAY_STRIDE,GL_TEXTURE_COORD_ARRAY_STRIDE};
    const GLint native_default_strides[]={16,12,16,12,4,16}; // Measured actual 13F1911 Intel, not spec-default stride zero.
    int defaults_ok=1;
    for(unsigned i=0;i<6;++i) { GLint type=-1,stride=-1;glGetIntegerv(default_types[i],&type);
        glGetIntegerv(default_strides[i],&stride);printf("default array=%u type=%x stride=%d\n",i,type,stride);
        defaults_ok&=type==GL_FLOAT&&stride==native_default_strides[i]; }
    check(defaults_ok&&!glGetError(),"all six legacy array families retain native default types and strides");
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"real framebuffer");
    glViewport(0,0,16,16);glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(2,GL_FLOAT,0,left);
    GLint initial_size=0,initial_type=0,initial_stride=0;
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE,&initial_size);glGetIntegerv(GL_VERTEX_ARRAY_TYPE,&initial_type);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE,&initial_stride);
    printf("logical vertex observation size=%d type=%x stride=%d enabled=%u error=%x\n",
        initial_size,initial_type,initial_stride,glIsEnabled(GL_VERTEX_ARRAY),glGetError());
    check(initial_size==2&&initial_type==GL_FLOAT&&initial_stride==8&&glIsEnabled(GL_VERTEX_ARRAY)&&!glGetError(),
        "logical vertex state is visible before the first draw");
    GLfloat stride_f=0;GLdouble stride_d=0;GLboolean enabled_b=0;
    glGetFloatv(GL_VERTEX_ARRAY_STRIDE,&stride_f);glGetDoublev(GL_VERTEX_ARRAY_STRIDE,&stride_d);
    glGetBooleanv(GL_VERTEX_ARRAY,&enabled_b);
    check(stride_f==8&&stride_d==8&&enabled_b==GL_TRUE&&!glGetError(),"logical array scalar conversions match native GL");
    paint(1,"initial client array draws left");
    glClientActiveTexture(GL_TEXTURE3);glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glVertexPointer(2,GL_FLOAT,0,right);glClientActiveTexture(GL_TEXTURE1);
    paint(0,"pushed state draws changed right array");
    glDisableClientState(GL_VERTEX_ARRAY);glPopClientAttrib();
    GLint active=0;glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&active);
    check(active==GL_TEXTURE3&&!glGetError(),"pop restores client active texture");
    paint(1,"pop restores client pointer and enable state");
    glEnableClientState(GL_VERTEX_ARRAY); // Keep later cases independent of the first verdict.
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glVertexPointer(2,GL_FLOAT,0,right);glPopClientAttrib();
    paint(0,"pixel-only stack does not restore vertex state");
    glVertexPointer(2,GL_FLOAT,0,left);glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glVertexPointer(2,GL_FLOAT,0,right);glPushClientAttrib(0);glVertexPointer(2,GL_FLOAT,0,left);glPopClientAttrib();
    paint(1,"zero mask occupies stack without restoring changed vertex state");
    glPopClientAttrib();paint(1,"nested mask pairing restores original vertex state");
    glVertexAttribPointerARB(5,2,GL_FLOAT,GL_FALSE,0,left);glEnableVertexAttribArrayARB(5);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);glVertexAttribPointerARB(5,3,GL_FLOAT,GL_FALSE,0,right);
    glDisableVertexAttribArrayARB(5);glPopClientAttrib();GLint generic_size=0,generic_enabled=0;
    glGetVertexAttribivARB(5,GL_VERTEX_ATTRIB_ARRAY_SIZE,&generic_size);
    glGetVertexAttribivARB(5,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&generic_enabled);
    check(generic_size==2&&generic_enabled&&!glGetError(),"generic array metadata participates in native client stack");
    glDisableVertexAttribArrayARB(5);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,5);glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,11);glVertexPointer(2,GL_FLOAT,0,right);glPopClientAttrib();
    GLint row=0;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
    check(row==5&&!glGetError(),"all-client mask restores pixel state too");
    paint(1,"all-client mask restores vertex state too");
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);glPixelStorei(GL_UNPACK_ROW_LENGTH,13);glPopClientAttrib();
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
    check(row==13&&!glGetError(),"vertex-only mask does not restore pixel state");glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    GLuint buffers[2]={0};glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferData(GL_ARRAY_BUFFER,sizeof(left),left,GL_STATIC_DRAW);
    glVertexPointer(2,GL_FLOAT,0,NULL);glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ARRAY_BUFFER,sizeof(right),right,GL_STATIC_DRAW);
    glVertexPointer(2,GL_FLOAT,0,NULL);paint(0,"changed VBO array draws right");
    glPopClientAttrib();GLint binding=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);
    check(binding==(GLint)buffers[0]&&!glGetError(),"pop restores array buffer binding");
    paint(1,"pop restores vertex buffer offset and captured buffer");
    glVertexPointer(2,GL_FLOAT,0,NULL);paint(1,"new pointer captures restored current array buffer");
    GLuint vaos[2]={0};glGenVertexArraysAPPLE(2,vaos);
    glBindVertexArrayAPPLE(vaos[0]);glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
    glVertexPointer(2,GL_FLOAT,0,NULL);glEnableClientState(GL_VERTEX_ARRAY);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);glBindVertexArrayAPPLE(vaos[1]);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[1]);glVertexPointer(2,GL_FLOAT,0,NULL);glEnableClientState(GL_VERTEX_ARRAY);
    glPopClientAttrib();GLint vao=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING_APPLE,&vao);
    printf("VAO observation after pop=%u pushed=%u changed=%u error=%x\n",(GLuint)vao,vaos[0],vaos[1],glGetError());
    paint(1,"pop restores pushed VAO drawing");
    check(vao==(GLint)vaos[0],"pop restores selected VAO name");
    glBindVertexArrayAPPLE(vaos[1]);paint(0,"other VAO survives pop unchanged");
    glBindVertexArrayAPPLE(0);glDeleteVertexArraysAPPLE(2,vaos);
    glBindBuffer(GL_ARRAY_BUFFER,0);glVertexPointer(2,GL_FLOAT,0,left);glEnableClientState(GL_VERTEX_ARRAY);
    GLint limit=0;glGetIntegerv(GL_MAX_CLIENT_ATTRIB_STACK_DEPTH,&limit);
    if(limit<1||limit>64)return 2;
    for(GLint i=0;i<limit;++i)glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    check(!glGetError(),"native maximum client stack accepted");
    glVertexPointer(2,GL_FLOAT,0,right);glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    check(glGetError()==GL_STACK_OVERFLOW,"overflow rejected by native stack");
    glVertexPointer(2,GL_FLOAT,0,left);glPopClientAttrib();
    paint(1,"failed overflow did not add a logical stack frame");
    for(GLint i=1;i<limit;++i)glPopClientAttrib();
    glPopClientAttrib();check(glGetError()==GL_STACK_UNDERFLOW,"underflow rejected without logical restoration");
    paint(1,"underflow leaves vertex state intact");
    glBegin(GL_POINTS);glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);glEnd();
    check(glGetError()==GL_INVALID_OPERATION,"Begin/End push rejected by native GL");
    glPopClientAttrib();check(glGetError()==GL_STACK_UNDERFLOW,"Begin/End rejection did not add a logical frame");
    glEnable(0xdead);glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    check(glGetError()==GL_INVALID_ENUM,"earlier GL error does not reject valid push");
    glVertexPointer(2,GL_FLOAT,0,right);glPopClientAttrib();paint(1,"accepted push after earlier error restores vertex state");
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glVertexPointer(2,GL_FLOAT,0,NULL);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[1]); // Captured array differs from current binding; no draw before push.
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);glBindBuffer(GL_ARRAY_BUFFER,0);glDeleteBuffers(1,&buffers[0]);
    glPopClientAttrib();glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);
    printf("deleted-stack observation buffer=%d original=%u exists=%u error=%x\n",binding,buffers[0],glIsBuffer(buffers[0]),glGetError());
    check(binding==(GLint)buffers[1]&&glIsBuffer(buffers[0])&&!glGetError(),"native stack restores current and separately captured buffer names");
    GLint captured_buffer=0;glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING,&captured_buffer);
    printf("deleted-array observation captured_buffer=%d error=%x\n",captured_buffer,glGetError());
    check(captured_buffer==(GLint)buffers[0],"logical captured buffer matches native restored array");
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);GLint restored_bytes=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&restored_bytes);
    printf("deleted-buffer storage observation bytes=%d error=%x\n",restored_bytes,glGetError());
    check(!restored_bytes,"native deleted-buffer restoration recreates an empty store, not stale pixels");
    // Never draw using a deleted array's NULL CPU pointer. The original
    // Mavericks Intel engine faults there; it is not a valid pixel oracle.
    GLuint saved_vao=0;glGenVertexArraysAPPLE(1,&saved_vao);glBindVertexArrayAPPLE(saved_vao);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[1]);glVertexPointer(2,GL_FLOAT,0,NULL);glEnableClientState(GL_VERTEX_ARRAY);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);glBindVertexArrayAPPLE(0);glDeleteVertexArraysAPPLE(1,&saved_vao);
    glPopClientAttrib();glGetIntegerv(GL_VERTEX_ARRAY_BINDING_APPLE,&vao);
    printf("deleted-VAO observation selected=%u original=%u exists=%u error=%x\n",(GLuint)vao,saved_vao,glIsVertexArrayAPPLE(saved_vao),glGetError());
    glBindVertexArrayAPPLE(0);glDeleteVertexArraysAPPLE(1,&saved_vao);
    glBindBuffer(GL_ARRAY_BUFFER,0);glDisableClientState(GL_VERTEX_ARRAY);glDeleteBuffers(2,buffers);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);check(!glGetError(),"native cleanup");
#ifdef VIM_VERTEX_STACK_DIRECT
    check(!context->connection->dead,"client stack preserves live transport");DGDestroyContext(context);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
#endif
    printf("client-vertex-stack failures=%d\n",failures);return failures?1:0;
}
