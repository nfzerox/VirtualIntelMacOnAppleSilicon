/* Native and wire ARB shader-object pointer-width result ABI. */
#ifdef VIM_ATTACHED_OBJECTS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
int main(void)
{
#ifdef VIM_ATTACHED_OBJECTS_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *ctx=DGCreateContext(attrs,NULL);
    if(!ctx || !DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    /* A 2.0 client echoes all negotiated bits, including unknown ones.
     * It still must not see extensions that require newer guest code. */
    char old_extensions[4096]={0};
    struct DGPacket old=DGPacketNew(ctx,DG_STRING);
    old.message.args[0]=GL_EXTENSIONS;
    old.message.args[3]=2;
    old.message.args[4]=ctx->connection->features;
    old.message.args[5]=UINT32_MAX;
    DGPacketPointer(&old,1,old_extensions,sizeof(old_extensions),1);
    DGSubmit(ctx,&old,1);DGPacketCopy(&old,1,old_extensions,sizeof(old_extensions));
    CHECK(old.valid_reply && old_extensions[0] &&
        strstr(old_extensions,"GL_ARB_shader_objects") &&
        !strstr(old_extensions,"GL_EXT_bindable_uniform") &&
        !strstr(old_extensions,"GL_ARB_vertex_blend") &&
        !strstr(old_extensions,"GL_APPLE_vertex_program_evaluators") &&
        !strstr(old_extensions,"GL_APPLE_vertex_point_size") &&
        !strstr(old_extensions,"GL_APPLE_object_purgeable") &&
        !strstr(old_extensions,"GL_APPLE_element_array") &&
        !strstr(old_extensions,"GL_EXT_debug_label"));
    DGPacketFree(&old);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    const char *ext=(const char *)glGetString(GL_EXTENSIONS);
    printf("attached renderer=%s handle=%zu\n",glGetString(GL_RENDERER),sizeof(GLhandleARB));
    if(!ext || !strstr(ext,"GL_ARB_shader_objects"))return 77;
    GLhandleARB program=glCreateProgramObjectARB();
    GLhandleARB vertex=glCreateShaderObjectARB(GL_VERTEX_SHADER),fragment=glCreateShaderObjectARB(GL_FRAGMENT_SHADER);
    glAttachObjectARB(program,vertex);glAttachObjectARB(program,fragment);
    const GLhandleARB sentinel=(GLhandleARB)(uintptr_t)0xfeedface;
    GLhandleARB objects[]={sentinel,sentinel,sentinel};
    GLsizei count=-1;
    glGetAttachedObjectsARB(program,3,&count,objects);
    CHECK(glGetError()==GL_NO_ERROR && count==2 && objects[2]==sentinel);
    CHECK((objects[0]==vertex && objects[1]==fragment) || (objects[0]==fragment && objects[1]==vertex));
    GLuint core[3]={0};GLsizei nc=-1;
    glGetAttachedShaders((GLuint)(uintptr_t)program,3,&nc,core);
    int matches=nc==count && (uintptr_t)objects[0]==core[0] && (uintptr_t)objects[1]==core[1];
    GLenum core_error=glGetError();
    printf("attached core names match=%d error=%04x\n",matches,core_error);
    CHECK(core_error==GL_NO_ERROR && matches);
    const GLhandleARB first=objects[0];
    objects[0]=objects[1]=sentinel;
    glGetAttachedObjectsARB(program,1,NULL,objects);
    CHECK(glGetError()==GL_NO_ERROR && objects[0]==first && objects[1]==sentinel);
    count=-1;objects[0]=sentinel;
    glGetAttachedObjectsARB(program,0,&count,objects);
    CHECK(glGetError()==GL_NO_ERROR && count==0 && objects[0]==sentinel);
    count=-1;glGetAttachedObjectsARB(program,0,&count,NULL);
    CHECK(glGetError()==GL_NO_ERROR && count==0);
    count=-1;glGetAttachedObjectsARB(program,-1,&count,objects);
    CHECK(glGetError()==GL_INVALID_VALUE && count==-1 && objects[0]==sentinel);
    count=-1;glGetAttachedObjectsARB(vertex,1,&count,objects);
    GLenum wrong_type=glGetError();
    printf("attached wrong object type=%04x count=%d\n",wrong_type,count);
    CHECK(wrong_type==GL_INVALID_OPERATION && count==-1 && objects[0]==sentinel);
    glGetAttachedObjectsARB((GLhandleARB)0,1,&count,objects);
    CHECK(glGetError()==GL_INVALID_VALUE && count==-1 && objects[0]==sentinel);
    glDeleteObjectARB(vertex);
    glGetAttachedObjectsARB(program,3,&count,objects);
    CHECK(glGetError()==GL_NO_ERROR && count==2 && (objects[0]==vertex || objects[1]==vertex));
    return failures!=0;
}
