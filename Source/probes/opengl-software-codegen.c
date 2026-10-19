// Control: load the PVG dependency in a software-GL process and verify that
// native shader linking is untouched. This is NOT an injection deployment.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static GLuint shader(GLenum type,const char *source) {
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    return ok?s:0;
}

int main(void) {
    CGLPixelFormatAttribute attributes[]={kCGLPFARendererID,
        (CGLPixelFormatAttribute)kCGLRendererGenericFloatID,kCGLPFAColorSize,24,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0,accelerated=1;
    if(CGLChoosePixelFormat(attributes,&format,&n)||!format ||
       CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context)) return 2;
    CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&accelerated);
    printf("renderer=%s accelerated=%d\n",glGetString(GL_RENDERER),accelerated);
    if(accelerated) return 3;
    if(!dlopen("/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/PVGOpenGLMetal.dylib",RTLD_NOW|RTLD_LOCAL)) {
        fprintf(stderr,"compat load: %s\n",dlerror());return 4;
    }
    GLuint v=shader(GL_VERTEX_SHADER,"#version 120\nvoid main(){gl_Position=gl_Vertex;}"),
        f=shader(GL_FRAGMENT_SHADER,"#version 120\nvoid main(){gl_FragColor=vec4(1,0,0,1);}");
    if(!v||!f) return 5;
    GLuint program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 6;
    GLuint texture,buffer;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&buffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,buffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT) return 7;
    glViewport(0,0,16,16);glUseProgram(program);
    glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
    unsigned char pixels[16*16*4];memset(pixels,0,sizeof(pixels));
    glReadPixels(0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();unsigned bad=0;
    for(unsigned i=0;i<256;++i)
        if(pixels[4*i]!=255||pixels[4*i+1]||pixels[4*i+2]||pixels[4*i+3]!=255)++bad;
    printf("software shader pixels: bad=%u/256 error=0x%x\n",bad,error);
    glDeleteFramebuffersEXT(1,&buffer);glDeleteTextures(1,&texture);
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(v);glDeleteShader(f);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
    return bad||error?8:0;
}
