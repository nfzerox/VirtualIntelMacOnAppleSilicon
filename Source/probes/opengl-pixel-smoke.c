// Require hardware OpenGL and validate pixels, never accept software fallback.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GLuint shader(GLenum kind, const char *source) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader: %s\n", log); exit(2);
    }
    return s;
}

static int runContext(int argc, char **argv) {
    setbuf(stdout, NULL);
    CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, kCGLPFAAccelerated, 0};
    CGLPixelFormatObj format = NULL; GLint count = 0, accelerated = 0;
    if (CGLChoosePixelFormat(attrs, &format, &count) || !format) return 2;
    CGLDescribePixelFormat(format, 0, kCGLPFAAccelerated, &accelerated);
    if (!accelerated) return 3;
    CGLContextObj context = NULL;
    if (CGLCreateContext(format, NULL, &context) || CGLSetCurrentContext(context)) return 2;
    printf("accelerated=%d renderer=%s\n", accelerated, glGetString(GL_RENDERER));
    unsigned red = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 91;
    if (red > 255) return 2;
    const char *vs = "#version 150\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
    int attribute=0,uniform=0,texture=0,pattern=0,clearCheck=0,uniformLoop=0,textureCenter=0,sourceCheck=0,coverage=0,clearOnly=0,relink=0;
    int texelFetch=0,texture3D=0;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--attribute")) attribute=1;
        else if(!strcmp(argv[i],"--uniform")) uniform=1;
        else if(!strcmp(argv[i],"--uniform-loop")) {uniform=1;uniformLoop=1;}
        else if(!strcmp(argv[i],"--texture")) texture=1;
        else if(!strcmp(argv[i],"--texel-fetch"))texture=texelFetch=1;
        else if(!strcmp(argv[i],"--texture3d-read"))texture=texelFetch=texture3D=1;
        else if(!strcmp(argv[i],"--texture-center")) {texture=1;textureCenter=1;}
        else if(!strcmp(argv[i],"--source-check")) sourceCheck=1;
        else if(!strcmp(argv[i],"--coverage")) coverage=1;
        else if(!strcmp(argv[i],"--pattern")) {texture=1;pattern=1;}
        else if(!strcmp(argv[i],"--no-dither")) glDisable(GL_DITHER);
        else if(!strcmp(argv[i],"--clear-check")) clearCheck=1;
        else if(!strcmp(argv[i],"--clear-only")) {clearCheck=1;clearOnly=1;}
        else if(!strcmp(argv[i],"--relink")) relink=1;
        else return 64;
    }
    if((uniform && texture) || (textureCenter && pattern) || (texelFetch && (pattern || textureCenter)) ||
       (clearOnly && (attribute || uniform || texture || sourceCheck || coverage || relink)) ||
       (relink && !uniformLoop)) return 64;
    unsigned attributeIndex=0;
    if(getenv("PVG_GL_ATTRIBUTE_INDEX")) {
        char *end=NULL;
        unsigned long value=strtoul(getenv("PVG_GL_ATTRIBUTE_INDEX"),&end,10);
        if(!end || *end || value>=16) return 64;
        attributeIndex=(unsigned)value;
    }
    // Distinct Z values allow controlled cold-compiler tests without deleting
    // application caches; this target has no depth attachment.
    char vertexSource[512];
    if (attribute) {
        snprintf(vertexSource,sizeof(vertexSource),"#version 150\nin vec2 pvg_position;void main(){gl_Position=vec4(pvg_position,%.9f,1.0);}",red/255.0);
        vs=vertexSource;
    }
    char fs[512];
    snprintf(fs, sizeof(fs), "#version 150\nout vec4 c;void main(){c=vec4(%.9f,0.25,0.75,1.0);}", red/255.0);
    if(uniform) strcpy(fs,"#version 150\nuniform vec4 chosen;out vec4 c;void main(){c=chosen;}");
    if(texture) strcpy(fs,"#version 150\nuniform sampler2D source;out vec4 c;void main(){c=texture(source,gl_FragCoord.xy/64.0);}");
    if(textureCenter) strcpy(fs,"#version 150\nuniform sampler2D source;out vec4 c;void main(){c=texture(source,vec2(0.5));}");
    if(texelFetch)strcpy(fs,texture3D?
        "#version 150\nuniform sampler3D source;out vec4 c;void main(){c=texelFetch(source,textureSize(source,0)-ivec3(1),0);}":
        "#version 150\nuniform sampler2D source;out vec4 c;void main(){c=texelFetch(source,textureSize(source,0)-ivec2(1),0);}");
    GLuint v=0,f=0,p=0;
    if(!clearOnly) {
    v=shader(GL_VERTEX_SHADER,vs); f=shader(GL_FRAGMENT_SHADER,fs); p=glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,f); glBindAttribLocation(p,attributeIndex,"pvg_position"); glLinkProgram(p);
    GLint linked=0; glGetProgramiv(p,GL_LINK_STATUS,&linked); if(!linked) return 2;
    }
    GLuint tex,fb,vao;
    glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb); glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return 2;
    if(clearCheck) {
        unsigned char pixels[64*64*4]; memset(pixels,0xa5,sizeof(pixels));
        glClearColor(0,1,1,1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
        glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        GLenum e=glGetError(); unsigned bad=0;
        for(unsigned i=0;i<64*64;i++)
            if(pixels[4*i]!=0 || pixels[4*i+1]!=255 ||
               pixels[4*i+2]!=255 || pixels[4*i+3]!=255) bad++;
        printf("clear pixel=%u,%u,%u,%u bad=%u/4096 error=0x%x\n",
               pixels[0],pixels[1],pixels[2],pixels[3],bad,e);
        if(clearOnly) {
            glDeleteFramebuffers(1,&fb); glDeleteTextures(1,&tex);
            CGLSetCurrentContext(NULL); CGLDestroyContext(context); CGLDestroyPixelFormat(format);
            return e || bad ? 4 : 0;
        }
        if(e || bad) return 4;
    }
    glGenVertexArrays(1,&vao); glBindVertexArray(vao); glUseProgram(p);
    GLuint sourceTexture=0;
    if(uniform) {
        GLint location=glGetUniformLocation(p,"chosen"); if(location<0) return 2;
        glUniform4f(location,red/255.0f,0.25f,0.75f,1.0f);
    }
    if(texture) {
        unsigned char source[4*4*4];
        for(unsigned i=0;i<16;i++) {
            source[4*i]=red;source[4*i+1]=64;source[4*i+2]=191;source[4*i+3]=255;
        }
        glActiveTexture(GL_TEXTURE0);glGenTextures(1,&sourceTexture);
        GLenum target=texture3D?GL_TEXTURE_3D:GL_TEXTURE_2D;
        glBindTexture(target,sourceTexture);
        glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        if(texture3D)glTexImage3D(target,0,GL_RGBA8,4,4,1,0,GL_RGBA,GL_UNSIGNED_BYTE,source);
        else glTexImage2D(target,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,source);
        GLint location=glGetUniformLocation(p,"source"); if(location<0) return 2;
        glUniform1i(location,0);
        if(sourceCheck) {
            unsigned char copied[sizeof(source)]; memset(copied,0,sizeof(copied));
            glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,copied);
            GLenum e=glGetError(); int bad=memcmp(source,copied,sizeof(source))!=0;
            printf("source pixel=%u,%u,%u,%u mismatch=%d error=0x%x\n",
                copied[0],copied[1],copied[2],copied[3],bad,e);
            if(e || bad) return 5;
        }
    }
    GLuint buffer=0;
    if (attribute) {
        const GLfloat positions[]={-1,-1,3,-1,-1,3};
        glGenBuffers(1,&buffer); glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBufferData(GL_ARRAY_BUFFER,sizeof(positions),positions,GL_STATIC_DRAW);
        glEnableVertexAttribArray(attributeIndex); glVertexAttribPointer(attributeIndex,2,GL_FLOAT,GL_FALSE,0,NULL);
    }
    unsigned totalBad=0; GLenum error=0;
    for(unsigned frame=0;frame<((pattern||uniformLoop)?32:1);frame++) {
    if(relink && frame && !(frame%4)) {
        glLinkProgram(p);GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);
        if(!linked)return 6;
        glUseProgram(p);
        printf("program relink frame=%u\n",frame);
    }
    if(uniformLoop)
        glUniform4f(glGetUniformLocation(p,"chosen"),((red+frame*7)&255)/255.0f,0.25f,0.75f,1.0f);
    if(pattern) {
        unsigned char source[4*4*4];
        for(unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++) {
            unsigned i=4*(y*4+x);
            source[i]=(red+frame*7+x*31+y*17)&255;
            source[i+1]=x*64;source[i+2]=y*64;source[i+3]=255;
        }
        // Queue use before overwrite to exercise GL's resource synchronization.
        glDrawArrays(GL_TRIANGLES,0,3);
        glBindTexture(GL_TEXTURE_2D,sourceTexture);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,source);
    }
    GLuint coverageQuery=0;
    if(coverage) {glGenQueries(1,&coverageQuery);glBeginQuery(GL_SAMPLES_PASSED,coverageQuery);}
    glViewport(0,0,64,64); glDrawArrays(GL_TRIANGLES,0,3);
    if(coverage) glEndQuery(GL_SAMPLES_PASSED);
    glFinish();
    if(coverage) {
        GLuint samples=0;glGetQueryObjectuiv(coverageQuery,GL_QUERY_RESULT,&samples);
        printf("coverage samples=%u error=0x%x\n",samples,glGetError());
        glDeleteQueries(1,&coverageQuery);
    }
    unsigned char pixels[64*64*4]; memset(pixels,0,sizeof(pixels));
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    error=glGetError(); unsigned bad=0;
    for(unsigned i=0;i<64*64;i++) {
        const unsigned char *q=pixels+4*i;
        unsigned x=(i%64)/16,y=(i/64)/16;
        unsigned r=pattern?((red+frame*7+x*31+y*17)&255):(uniformLoop?((red+frame*7)&255):red);
        unsigned g=pattern?x*64:64,b=pattern?y*64:191;
        if(abs((int)q[0]-(int)r)>1 || abs((int)q[1]-(int)g)>1 || abs((int)q[2]-(int)b)>1 || q[3]!=255) bad++;
    }
    printf("pixel=%u,%u,%u,%u bad=%u/4096 error=0x%x\n",pixels[0],pixels[1],pixels[2],pixels[3],bad,error);
    totalBad+=bad;
    if(error || bad) break;
    }
    glDeleteFramebuffers(1,&fb); glDeleteTextures(1,&tex); glDeleteVertexArrays(1,&vao);
    if(buffer) glDeleteBuffers(1,&buffer);
    if(sourceTexture) glDeleteTextures(1,&sourceTexture);
    glDeleteProgram(p); glDeleteShader(v); glDeleteShader(f);
    CGLSetCurrentContext(NULL); CGLDestroyContext(context); CGLDestroyPixelFormat(format);
    return error || totalBad ? 1 : 0;
}

int main(int argc, char **argv) {
    unsigned contexts = 1;
    const char *requested = getenv("PVG_GL_CONTEXT_LOOPS");
    if (requested) {
        char *end = NULL;
        unsigned long n = strtoul(requested, &end, 10);
        if (!*requested || *end || n < 1 || n > 16) return 64;
        contexts = (unsigned)n;
    }
    for (unsigned i = 0; i < contexts; ++i) {
        int result = runContext(argc, argv);
        printf("context iteration=%u/%u result=%d\n", i+1, contexts, result);
        if (result) return result;
    }
    return 0;
}
