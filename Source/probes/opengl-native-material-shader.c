/* Replay the recorded final GLSL with known pixels, without any guest/IOSurface. */
#define main native_feedback_main
#include "opengl-native-fbo-feedback.c"
#undef main
#include <string.h>
#include <math.h>

static GLuint recorded_program(const char *path){
    FILE *f=fopen(path,"r");if(!f)return 0;
    char line[4096],source[2][32768]={{0}};int stage=-1;
    while(fgets(line,sizeof(line),f)){
        if(!strncmp(line,"SHADER type=8b31",16)){stage=0;continue;}
        if(!strncmp(line,"SHADER type=8b30",16)){stage=1;continue;}
        if(!strncmp(line,"UNIFORM ",8))break;
        if(stage>=0){size_t n=strlen(source[stage]);if(n+strlen(line)>=sizeof(source[stage]))exit(2);strcat(source[stage],line);}
    }
    fclose(f);if(!source[0][0]||!source[1][0])return 0;
    GLuint p=glCreateProgram();glAttachShader(p,shader(GL_VERTEX_SHADER,source[0]));glAttachShader(p,shader(GL_FRAGMENT_SHADER,source[1]));
    glLinkProgram(p);GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);return linked?p:0;
}
static void attach(GLuint texture,const float *data){
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA32F_ARB,128,16,0,GL_RGBA,GL_FLOAT,data);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    printf("renderer=%s\n",glGetString(GL_RENDERER));int good=1;
    for(int y=0;y<=1;++y){
        GLuint p=recorded_program(argv[1+y]);if(!p)return 2;
        GLuint t[3],fbo;glGenTextures(3,t);glGenFramebuffersEXT(1,&fbo);
        float input[16][128][4],second[16][128][4];
        for(int r=0;r<16;++r)for(int x=0;x<128;++x){
            const float f[4]={.25f,.125f,0,.25f},b[4]={.25f,.25f,.5f,1},mask[4]={1,1,1,.5f};
            const float *a=y&&x>=64?b:f,*s=y?mask:b;
            memcpy(input[r][x],a,sizeof(f));memcpy(second[r][x],s,sizeof(f));
        }
        glActiveTexture(GL_TEXTURE1);attach(t[1],&second[0][0][0]);
        glActiveTexture(GL_TEXTURE0);attach(t[2],NULL);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,t[2],0);
        if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
        attach(t[0],&input[0][0][0]);glUseProgram(p);
        const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
        glUniformMatrix3x4fv(glGetUniformLocation(p,y?"saturation_matrix":"saturation_fill_matrix"),1,GL_FALSE,identity);
        glUniform1i(glGetUniformLocation(p,y?"tex":"content"),0);
        glUniform1i(glGetUniformLocation(p,y?"mask":"blur"),1);
        glUniform4f(glGetUniformLocation(p,"tint"),1,1,1,1);
        glUniform1f(glGetUniformLocation(p,"fade"),1);
        glUniform4f(glGetUniformLocation(p,"fill"),0,0,0,1);
        glUniform3f(glGetUniformLocation(p,"offset_and_friends"),0,0,64);
        glUniform1f(glGetUniformLocation(p,"hdr_scaling_factor"),1);
        glViewport(0,0,128,16);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,128,0,16,-1,1);
        glMatrixMode(GL_MODELVIEW);glLoadIdentity();glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);glTexCoord2f(.5f,.5f);glVertex2f(0,0);glVertex2f(63,0);glVertex2f(63,16);glVertex2f(0,16);glEnd();
        float rgba[4];glReadPixels(20,8,1,1,GL_RGBA,GL_FLOAT,rgba);
        const float expect[]={.25f,.125f,.25f,1};GLenum error=glGetError();int pass=!error;
        for(unsigned i=0;i<4;++i)pass&=fabsf(rgba[i]-expect[i]*(y?.5f:1))<.0001f;
        printf("%s recorded %s shader: %g %g %g %g GL=%x\n",pass?"PASS":"FAIL",y?"Yosemite":"El Capitan",rgba[0],rgba[1],rgba[2],rgba[3],error);
        good&=pass;glUseProgram(0);glDeleteProgram(p);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(3,t);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);return !good;
}
