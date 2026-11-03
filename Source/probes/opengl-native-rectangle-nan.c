/* Compare the legacy CoreAnimation gradient input on native Intel and M4. */
#define main feedback_probe_main
#include "opengl-native-fbo-feedback.c"
#undef main
#include <CoreGraphics/CoreGraphics.h>
#include <math.h>

int main(int argc,char **argv)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,0,0,0};
    if(argc==2){
        attributes[2]=kCGLPFARendererID;attributes[3]=(CGLPixelFormatAttribute)strtoul(argv[1],NULL,0);
        if(!strcmp(argv[1],"display")){
            attributes[1]=kCGLPFASupportsAutomaticGraphicsSwitching;
            attributes[2]=kCGLPFADisplayMask;
            attributes[3]=(CGLPixelFormatAttribute)CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID());
        }
    }
    CGLPixelFormatObj pixel=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError selection=CGLChoosePixelFormat(attributes,&pixel,&count);
    if(selection||!pixel||CGLCreateContext(pixel,NULL,&context)||CGLSetCurrentContext(context)){
        fprintf(stderr,"No requested CGL context: selection=%d count=%d\n",selection,count);
        CGLRendererInfoObj renderers=NULL;
        if(!CGLQueryRendererInfo((GLuint)-1,&renderers,&count)){
            for(GLint i=0;i<count;++i){GLint id=0;CGLDescribeRenderer(renderers,i,kCGLRPRendererID,&id);fprintf(stderr,"CGL renderer=%#x\n",id);}
            CGLDestroyRendererInfo(renderers);
        }
        return 2;
    }
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint p=glCreateProgram(),input,output,fbo;
    glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}"));
    glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 120\n#extension GL_ARB_texture_rectangle : enable\nvarying vec2 uv;uniform sampler2DRect image;void main(){gl_FragColor=texture2DRect(image,uv);}"));
    glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&count);if(!count)return 2;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,output);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glGenTextures(1,&input);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,input);
    unsigned char ramp[512][4];
    for(unsigned i=0;i<512;++i){ramp[i][0]=i>>1;ramp[i][1]=255-(i>>1);ramp[i][2]=48;ramp[i][3]=i>>1;}
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,512,1,0,GL_RGBA,GL_UNSIGNED_BYTE,ramp);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glUseProgram(p);glUniform1i(glGetUniformLocation(p,"image"),0);glViewport(0,0,8,8);
    const char *labels[]={"left","right","mid","+NaN","-NaN","+inf","-inf"};
    const float coordinates[]={.5f,511.5f,255.5f,NAN,-NAN,INFINITY,-INFINITY};
    int good=1;
    for(unsigned i=0;i<sizeof(coordinates)/sizeof(*coordinates);++i){
        float x=coordinates[i];
        glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(x,.5f);glVertex2f(-1,-1);
        glTexCoord2f(x,.5f);glVertex2f(1,-1);
        glTexCoord2f(x,.5f);glVertex2f(1,1);
        glTexCoord2f(x,.5f);glVertex2f(-1,1);
        glEnd();
        unsigned char c[4];glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,c);
        GLenum error=glGetError();
        good&=!error && (i>2 || (c[0]==(i==0?0:i==1?255:127)&&c[2]==48&&c[3]==c[0]));
        printf("%s RGBA=%u,%u,%u,%u GL=%x\n",labels[i],c[0],c[1],c[2],c[3],error);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(pixel);return !good;
}
