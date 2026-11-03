/* Actual typed client/interpreter native hardware/software transitions.
 * Hidden/export-main-only: never interpose our client GL on native host GL. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint compiled=0;glShaderSource(s,1,&source,NULL);glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&compiled);assert(compiled);return s;
}
int main(void)
{
    enum {W=8,H=8};unsigned char shadow[W*H*4]={0},input[W*H*4]={0},output[W*H*4]={0};
    struct DGContext *c=DGCreateContext(NULL,NULL);assert(c&&DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));
    struct DGNativeRendererInfo info={0};assert(DGQueryNativeRenderer(&info)==1);
    assert((info.flags&(DG_NATIVE_HARDWARE|DG_NATIVE_SOFTWARE))==(DG_NATIVE_HARDWARE|DG_NATIVE_SOFTWARE));
    assert(!(info.flags&DG_NATIVE_CURRENT_SOFTWARE));
    const char *hardware=(const char *)glGetString(GL_RENDERER);assert(hardware&&strstr(hardware,"Intel"));
    char retained[512];snprintf(retained,sizeof(retained),"%s",hardware);
    printf("initial renderer=%s transport=%s native=%x flags=%x\n",hardware,DGTransportName(),info.renderer,info.flags);
    for(unsigned i=0;i<W*H;++i){input[i*4+2]=255;input[i*4+3]=255;}
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    assert(DGImportIOSurfaceTexture(GL_RGBA8,W,H,input,W*4));
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    GLuint vertex=shader(GL_VERTEX_SHADER,"#version 120\nvoid main(){gl_Position=gl_Vertex;}");
    GLuint fragment=shader(GL_FRAGMENT_SHADER,"#version 120\nuniform vec4 tint;void main(){gl_FragColor=tint;}");
    GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);glUseProgram(program);
    GLhandleARB object=(GLhandleARB)(uintptr_t)program;
    GLint tint=glGetUniformLocationARB(object,"tint");assert(tint>=0);glUniform4fARB(tint,1,0,0,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(.25f,.5f,0);glViewport(0,0,W,H);
    const unsigned kinds[]={0,1,0};const unsigned char colors[][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255}};
    GLfloat expected_uniform[4]={1,0,0,1};
    for(unsigned step=0;step<3;++step){
        assert(DGSelectNativeRenderer(kinds[step],&info)==1);
        const char *renderer=(const char *)glGetString(GL_RENDERER);
        printf("step=%u renderer=%s native=%x flags=%x\n",step,renderer,info.renderer,info.flags);
        assert(!!(info.flags&DG_NATIVE_CURRENT_SOFTWARE)==kinds[step]);
        assert(strstr(renderer,kinds[step]?"Software":"Intel")&&!strcmp(hardware,retained));
        if(!kinds[step])assert(renderer==hardware);
        assert(glIsTexture(texture)&&glIsFramebufferEXT(fbo)&&glIsProgram(program));
        GLint bound=-1,active=0;GLfloat matrix[16]={0},uniform[4]={0};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&bound);glGetIntegerv(GL_CURRENT_PROGRAM,&active);
        glGetFloatv(GL_MODELVIEW_MATRIX,matrix);glGetUniformfvARB(object,tint,uniform);
        assert(bound==(GLint)fbo&&active==(GLint)program&&matrix[12]==.25f&&matrix[13]==.5f&&
            !memcmp(uniform,expected_uniform,sizeof(uniform))&&!glGetError());
        for(unsigned i=0;i<4;++i)expected_uniform[i]=colors[step][i]/255.0f;
        glUniform4fvARB(tint,1,expected_uniform);
        glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
        unsigned char pixel[4]={0};glReadPixels(3,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        assert(!memcmp(pixel,colors[step],4)&&!glGetError());
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
        struct DGIosurfaceWrite write={0};unsigned count=0;uint32_t draw=UINT32_MAX;
        assert(DGFlushIOSurfaceState(0,&draw,&write,1,&count)==1&&count==1&&!draw&&write.texture==texture);
        assert(DGReadIOSurfaceTexture(texture,write.serial,W,H,output,W*4));
        for(unsigned i=0;i<W*H;++i){const unsigned char *p=output+i*4;
            assert(p[0]==colors[step][2]&&p[1]==colors[step][1]&&p[2]==colors[step][0]&&p[3]==255);}
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        puts("PASS retained objects/state/string lifetime, shader pixels and real IOSurface publication");
    }
    struct DGNativeRendererInfo untouched={0xa5a5a5a5,0xa5a5a5a5};
    assert(!DGSelectNativeRenderer(2,&untouched)&&untouched.flags==0xa5a5a5a5&&untouched.renderer==0xa5a5a5a5&&!glGetError());
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    assert(!glGetError()&&!c->connection->dead);DGDestroyContext(c);
    puts("PASS actual typed native hardware/software/hardware recovery; no GLI advertisement or guest installation");return 0;
}
