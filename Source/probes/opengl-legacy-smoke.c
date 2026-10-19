// Exercise the compatibility profile's fixed-function emulation, not GLSL.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    int noDither=0,arrays=0,vbo=0,elements=0,list=0,stateCycle=0,fogBlend=0,frames=1,mix=0,alphaTexture=0;
    int arbVertex=0,arbFragment=0;
    GLenum indexType=GL_UNSIGNED_SHORT;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--no-dither"))noDither=1;
        else if(!strcmp(argv[i],"--arb-vertex"))arbVertex=1;
        else if(!strcmp(argv[i],"--arb-fragment"))arbFragment=1;
        else if(!strcmp(argv[i],"--alpha-texture"))alphaTexture=1;
        else if(!strcmp(argv[i],"--arrays"))arrays=1;
        else if(!strcmp(argv[i],"--vbo"))arrays=vbo=1;
        else if(!strcmp(argv[i],"--elements"))arrays=elements=1;
        else if(!strcmp(argv[i],"--list"))list=1;
        else if(!strcmp(argv[i],"--state-cycle"))stateCycle=1;
        else if(!strcmp(argv[i],"--fog-blend"))stateCycle=fogBlend=1;
        else if(!strcmp(argv[i],"--mix"))mix=1;
        else if(!strcmp(argv[i],"--index-byte")){arrays=elements=1;indexType=GL_UNSIGNED_BYTE;}
        else if(!strcmp(argv[i],"--index-uint")){arrays=elements=1;indexType=GL_UNSIGNED_INT;}
        else if(!strcmp(argv[i],"--frames") && i+1<argc) {
            frames=atoi(argv[++i]);if(frames<1||frames>1000)return 64;
        }
        else return 64;
    }
    setbuf(stdout,NULL);
    /* Keep the expected color deterministic: these explicit programs bypass
     * fixed-function texturing and fog, which have separate tests above. */
    if((arbVertex||arbFragment)&&(alphaTexture||stateCycle))return 64;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0,accelerated=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 2;
    CGLDescribePixelFormat(pf,0,kCGLPFAAccelerated,&accelerated);if(!accelerated)return 3;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    printf("accelerated=%d renderer=%s version=%s\n",accelerated,glGetString(GL_RENDERER),glGetString(GL_VERSION));
    if(noDither)glDisable(GL_DITHER);
    GLuint fb,tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,tex,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glViewport(0,0,64,64);glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glColor4ub(51,102,153,255);
    GLuint arbPrograms[2]={0};
    const char *arbSources[]={
        "!!ARBvp1.0\nMOV result.position, vertex.position;\nMOV result.color, vertex.color;\nEND\n",
        "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n"
    };
    for(unsigned stage=0;stage<2;++stage) {
        if(!(stage?arbFragment:arbVertex))continue;
        GLenum target=stage?GL_FRAGMENT_PROGRAM_ARB:GL_VERTEX_PROGRAM_ARB;
        glGenProgramsARB(1,&arbPrograms[stage]);glBindProgramARB(target,arbPrograms[stage]);
        glProgramStringARB(target,GL_PROGRAM_FORMAT_ASCII_ARB,
                           (GLsizei)strlen(arbSources[stage]),arbSources[stage]);
        GLint position=-1;glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
        GLenum error=glGetError();
        if(error||position!=-1) {
            fprintf(stderr,"ARB stage=%u error=0x%x position=%d: %s\n",stage,error,
                    position,glGetString(GL_PROGRAM_ERROR_STRING_ARB));return 4;
        }
        glEnable(target);
    }
    GLuint sampleTexture=0;
    if(alphaTexture) {
        GLubyte alpha=128;glGenTextures(1,&sampleTexture);glBindTexture(GL_TEXTURE_2D,sampleTexture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_ALPHA8,1,1,0,GL_ALPHA,GL_UNSIGNED_BYTE,&alpha);
        glEnable(GL_TEXTURE_2D);glTexCoord2f(0.5f,0.5f);
    }
    GLuint buffer=0,displayList=0;
    const GLfloat vertices[]={-1,-1,3,-1,-1,3};
    const GLushort indices[]={0,1,2};
    const GLubyte byteIndices[]={0,1,2};
    const GLuint uintIndices[]={0,1,2};
    unsigned totalBad=0;GLenum totalError=0;
    for(int frame=0;frame<frames;++frame) {
    if(mix) {
        unsigned path=frame%7;
        arrays=path>=2;vbo=path==3;list=path==1;elements=path>=4;
        indexType=path==4?GL_UNSIGNED_BYTE:path==6?GL_UNSIGNED_INT:GL_UNSIGNED_SHORT;
    }
    printf("frame=%d path=%s index=0x%x\n",frame,list?"list":vbo?"vbo":elements?"elements":arrays?"arrays":"immediate",elements?indexType:0);
    unsigned expected[]={51,102,153,255};
    if(alphaTexture)expected[3]=128;
    if(stateCycle) {
        /* Exercise generated-program changes and subsequent cache reuse.
         * The triangle is at z=0; fog starts well beyond it, leaving RGB exact. */
        glFogi(GL_FOG_MODE,GL_LINEAR);glFogf(GL_FOG_START,fogBlend?-1:100);glFogf(GL_FOG_END,fogBlend?1:101);
        if(fogBlend) {
            unsigned offset=(frame&2)?4:0;
            GLfloat fogColor[]={(1+offset)/255.0f,(2+offset)/255.0f,(3+offset)/255.0f,0};
            glFogfv(GL_FOG_COLOR,fogColor);
            if(frame&1){expected[0]=26+offset/2;expected[1]=52+offset/2;expected[2]=78+offset/2;}
        }
        if(frame&1)glEnable(GL_FOG);else glDisable(GL_FOG);
    }
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    if(arrays) {
        if(vbo) {
            glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
            glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
        }
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2,GL_FLOAT,0,vbo?NULL:vertices);
        if(list) {displayList=glGenLists(1);glNewList(displayList,GL_COMPILE);}
        if(elements)glDrawElements(GL_TRIANGLES,3,indexType,indexType==GL_UNSIGNED_BYTE?(const void *)byteIndices:indexType==GL_UNSIGNED_INT?(const void *)uintIndices:indices);
        else glDrawArrays(GL_TRIANGLES,0,3);
        if(list) {glEndList();glCallList(displayList);}
    } else {
        if(list) {displayList=glGenLists(1);glNewList(displayList,GL_COMPILE);}
        glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
        if(list) {glEndList();glCallList(displayList);}
    }
    unsigned char pixels[64*64*4]={0};glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();unsigned bad=0;
    for(unsigned i=0;i<4096;i++) {unsigned char *p=pixels+4*i;if(p[0]!=expected[0]||p[1]!=expected[1]||p[2]!=expected[2]||p[3]!=expected[3])bad++;}
    printf("bad=%u/4096 error=0x%x pixel=%u,%u,%u,%u\n",bad,error,pixels[0],pixels[1],pixels[2],pixels[3]);
    totalBad+=bad;totalError|=error;
    if(arrays)glDisableClientState(GL_VERTEX_ARRAY);
    if(buffer){glDeleteBuffers(1,&buffer);buffer=0;}
    if(displayList){glDeleteLists(displayList,1);displayList=0;}
    if(bad||error)break;
    }
    glDeleteFramebuffersEXT(1,&fb);glDeleteTextures(1,&tex);
    if(sampleTexture)glDeleteTextures(1,&sampleTexture);
    for(unsigned stage=0;stage<2;++stage)if(arbPrograms[stage]) {
        glDisable(stage?GL_FRAGMENT_PROGRAM_ARB:GL_VERTEX_PROGRAM_ARB);
        glDeleteProgramsARB(1,&arbPrograms[stage]);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);
    return totalBad||totalError?1:0;
}
