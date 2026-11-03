/* Native public-CGL recovery oracle. One real context must retain its objects,
 * bindings, matrices and pixels while actually selecting hardware/software/
 * hardware. No renderer spoof, object recreation or private structure layout. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
static int failures;
static void number(CFMutableDictionaryRef properties,CFStringRef key,int value)
{
    CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&value);
    CFDictionarySetValue(properties,key,n);CFRelease(n);
}
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);if(!good)++failures;}
static GLuint shader(GLenum kind,const char *source)
{
    GLuint object=glCreateShader(kind);GLint compiled=0;
    glShaderSource(object,1,&source,NULL);glCompileShader(object);
    glGetShaderiv(object,GL_COMPILE_STATUS,&compiled);
    check(compiled,"shader compiled before migration");return object;
}
static void pixel(const unsigned char expected[4],const char *name)
{
    unsigned char actual[4]={0};glReadPixels(3,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    GLenum error=glGetError();
    printf("pixel=%u,%u,%u,%u expected=%u,%u,%u,%u error=%x\n",actual[0],actual[1],actual[2],actual[3],
        expected[0],expected[1],expected[2],expected[3],error);
    check(!error&&!memcmp(actual,expected,4),name);
}
int main(int argc,char **argv)
{
    if(argc<2||argc>6||strcmp(argv[1],"--require-intel"))return 2;
    int executor=0,iosurface=0,direct=0,no_switching=0;
    for(int i=2;i<argc;++i){
        if(!strcmp(argv[i],"--executor-format")&&!executor)executor=1;
        else if(!strcmp(argv[i],"--iosurface")&&!iosurface)iosurface=1;
        else if(!strcmp(argv[i],"--direct-engine")&&!direct)direct=1;
        else if(!strcmp(argv[i],"--no-switching")&&!no_switching)no_switching=1;
        else return 2;
    }
    CGLPixelFormatAttribute attributes[]={kCGLPFADoubleBuffer,kCGLPFAWindow,
        kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,kCGLPFADepthSize,16,kCGLPFAStencilSize,1,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatAttribute executor_attributes[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,
        kCGLPFADepthSize,24,kCGLPFAStencilSize,8,0};
    if(no_switching)attributes[10]=0;
    CGLPixelFormatAttribute direct_attributes[]={kCGLPFARendererID,(CGLPixelFormatAttribute)0x01036000,
        kCGLPFADoubleBuffer,kCGLPFAWindow,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,
        kCGLPFADepthSize,16,kCGLPFAStencilSize,1,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0,hardware=-1,software=-1;
    if(CGLChoosePixelFormat(direct?direct_attributes:executor?executor_attributes:attributes,&format,&count)||!format)return 2;
    for(GLint i=0;i<count;++i){
        GLint accelerated=0,id=0;
        CGLDescribePixelFormat(format,i,kCGLPFAAccelerated,&accelerated);
        CGLDescribePixelFormat(format,i,kCGLPFARendererID,&id);
        printf("screen=%d renderer=%x accelerated=%d\n",i,id,accelerated);
        if(accelerated&&hardware<0)hardware=i;else if(!accelerated&&software<0)software=i;
    }
    if(hardware<0||software<0)return 2;
    CGLError created=CGLCreateContext(format,NULL,&context);
    printf("context create=%d created=%d\n",created,context!=NULL);fflush(stdout);
    if(created||!context)return 2;
    if(CGLSetCurrentContext(context)||CGLSetVirtualScreen(context,hardware))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("initial renderer=%s\n",renderer?renderer:"none");fflush(stdout);
    if(!renderer||!strstr(renderer,"Intel")){
        CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);return 2;
    }
    IOSurfaceRef surface=NULL;GLenum target=iosurface?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D;
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(target,texture);
    if(iosurface){
        CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        number(properties,kIOSurfaceWidth,8);number(properties,kIOSurfaceHeight,8);
        number(properties,kIOSurfaceBytesPerElement,4);number(properties,kIOSurfaceBytesPerRow,32);
        number(properties,kIOSurfacePixelFormat,0x42475241);surface=IOSurfaceCreate(properties);CFRelease(properties);
        if(!surface||CGLTexImageIOSurface2D(context,target,GL_RGBA8,8,8,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0)){
            fputs("FAIL initial native IOSurface binding\n",stderr);return 1;
        }
    }else glTexImage2D(target,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,target,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"retained framebuffer complete");
    GLuint vertex=shader(GL_VERTEX_SHADER,"#version 120\nvoid main(){gl_Position=gl_Vertex;}");
    GLuint fragment=shader(GL_FRAGMENT_SHADER,"#version 120\nuniform vec4 tint;void main(){gl_FragColor=tint;}");
    GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked,"program linked before migration");
    glUseProgram(program);GLint tint=glGetUniformLocation(program,"tint");check(tint>=0,"uniform exists before migration");
    glUniform4f(tint,1,0,0,1);glViewport(0,0,8,8);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(.25f,.5f,0);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    unsigned char previous[4]={255,0,0,255};pixel(previous,"initial retained texture pixels");
    const GLint screens[]={hardware,software,hardware};
    const unsigned char colors[][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255}};
    GLfloat prior_uniform[4]={1,0,0,1};
    for(unsigned step=0;step<3;++step){
        CGLError switched=CGLSetVirtualScreen(context,screens[step]);GLint actual=-1,id=0,expected=0;
        CGLGetVirtualScreen(context,&actual);CGLGetParameter(context,kCGLCPCurrentRendererID,&id);
        CGLDescribePixelFormat(format,screens[step],kCGLPFARendererID,&expected);
        renderer=(const char *)glGetString(GL_RENDERER);
        printf("step=%u requested=%d actual=%d renderer_id=%x expected=%x error=%d renderer=%s\n",
            step,screens[step],actual,id,expected,switched,renderer?renderer:"none");
        check(!switched&&actual==screens[step]&&id==expected&&renderer&&
            strstr(renderer,step==1?"Software":"Intel"),"actual hardware/software renderer selection");
        check(glIsTexture(texture)&&glIsFramebufferEXT(fbo)&&glIsProgram(program),"retained object identities");
        GLint bound=0,active=0;GLfloat matrix[16]={0},uniform[4]={0};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&bound);glGetIntegerv(GL_CURRENT_PROGRAM,&active);
        glGetFloatv(GL_MODELVIEW_MATRIX,matrix);glGetUniformfv(program,tint,uniform);
        check(bound==(GLint)fbo&&active==(GLint)program&&matrix[12]==.25f&&matrix[13]==.5f&&
            !memcmp(uniform,prior_uniform,sizeof(uniform))&&!glGetError(),"bindings, matrix and uniform preserved");
        pixel(previous,"texture contents survive renderer migration");
        for(unsigned channel=0;channel<4;++channel)prior_uniform[channel]=colors[step][channel]/255.0f;
        glUniform4fv(tint,1,prior_uniform);
        glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
        pixel(colors[step],"retained shader renders on selected renderer");memcpy(previous,colors[step],4);
        if(surface){
            glFlush();int locked=!IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
            const unsigned char *p=locked?(const unsigned char *)IOSurfaceGetBaseAddress(surface)+
                4*IOSurfaceGetBytesPerRow(surface)+3*4:NULL;
            check(p&&p[0]==colors[step][2]&&p[1]==colors[step][1]&&p[2]==colors[step][0]&&p[3]==255,
                "actual IOSurface CPU pixels after producer Flush");
            if(locked)IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
        }
    }
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    check(!glGetError(),"cleanup");CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
    if(surface)CFRelease(surface);
    printf("recovery object failures=%d\n",failures);return failures?1:0;
}
