/* Two subroutine locations in every active stage, with exact integer pixels.
 * Qualifies native software-stage fallback as well as ordinary Metal stages;
 * does not use renderer-private structures or depend on subroutine ordering. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;}} while(0)
static const GLenum stages[]={GL_VERTEX_SHADER,GL_TESS_CONTROL_SHADER,
    GL_TESS_EVALUATION_SHADER,GL_GEOMETRY_SHADER,GL_FRAGMENT_SHADER};
static const char *bodies[]={
    "flat out int vertexCode;void main(){"
    "gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);"
    "vertexCode=pick[0]()+2*pick[1]();}",
    "layout(vertices=3) out;flat in int vertexCode[];flat out int controlCode[];void main(){"
    "gl_out[gl_InvocationID].gl_Position=gl_in[gl_InvocationID].gl_Position;"
    "controlCode[gl_InvocationID]=vertexCode[gl_InvocationID]+4*(pick[0]()+2*pick[1]());"
    "if(gl_InvocationID==0){gl_TessLevelOuter[0]=1;gl_TessLevelOuter[1]=1;"
    "gl_TessLevelOuter[2]=1;gl_TessLevelInner[0]=1;}}",
    "layout(triangles,equal_spacing,ccw) in;flat in int controlCode[];flat out int evaluationCode;"
    "void main(){gl_Position=gl_TessCoord.x*gl_in[0].gl_Position+"
    "gl_TessCoord.y*gl_in[1].gl_Position+gl_TessCoord.z*gl_in[2].gl_Position;"
    "evaluationCode=controlCode[0]+16*(pick[0]()+2*pick[1]());}",
    NULL, /* Its input interface depends on tessellation. */
    NULL  /* Its input interface depends on the last active stage. */
};
static GLuint compile(GLenum type,const char *body)
{
    char text[4096];snprintf(text,sizeof(text),"#version 410\n"
        "subroutine int Choice();subroutine(Choice) int zero(){return 0;}"
        "subroutine(Choice) int one(){return 1;}subroutine uniform Choice pick[2];%s",body);
    const char *source=text;GLuint shader=glCreateShader(type);
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    CHECK(ok);return shader;
}
static void verify(int tessellation,int geometry)
{
    GLuint program=glCreateProgram();unsigned active=1u|16u;
    if(tessellation)active|=6u;if(geometry)active|=8u;
    char geometryBody[2048],fragmentBody[2048];
    snprintf(geometryBody,sizeof(geometryBody),
        "layout(triangles) in;layout(triangle_strip,max_vertices=3) out;"
        "flat in int %s[];flat out int geometryCode;void main(){"
        "for(int i=0;i<3;i++){gl_Position=gl_in[i].gl_Position;"
        "geometryCode=%s[i]+64*(pick[0]()+2*pick[1]());EmitVertex();}EndPrimitive();}",
        tessellation?"evaluationCode":"vertexCode",tessellation?"evaluationCode":"vertexCode");
    snprintf(fragmentBody,sizeof(fragmentBody),
        "flat in int %s;layout(location=0) out uvec4 color;void main(){"
        "uint code=uint(%s+256*(pick[0]()+2*pick[1]()));"
        "color=uvec4(code,code^341u,1023u-code,1u);}",
        geometry?"geometryCode":tessellation?"evaluationCode":"vertexCode",
        geometry?"geometryCode":tessellation?"evaluationCode":"vertexCode");
    for(unsigned i=0;i<5;++i)if(active&(1u<<i)){
        GLuint shader=compile(stages[i],i==3?geometryBody:i==4?fragmentBody:bodies[i]);
        glAttachShader(program,shader);glDeleteShader(shader);
    }
    glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){char log[4096]={0};glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    CHECK(linked);if(!linked){glDeleteProgram(program);return;}
    glUseProgram(program);
    GLint locations[5][2]={{0}};GLuint choices[5][2]={{0}};
    for(unsigned i=0;i<5;++i)if(active&(1u<<i)){
        GLint count=0;glGetProgramStageiv(program,stages[i],GL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS,&count);
        CHECK(count==2);
        locations[i][0]=glGetSubroutineUniformLocation(program,stages[i],"pick[0]");
        locations[i][1]=glGetSubroutineUniformLocation(program,stages[i],"pick[1]");
        choices[i][0]=glGetSubroutineIndex(program,stages[i],"zero");
        choices[i][1]=glGetSubroutineIndex(program,stages[i],"one");
        if(count!=2||locations[i][0]<0||locations[i][0]>1||locations[i][1]<0||
           locations[i][1]>1||locations[i][0]==locations[i][1]||
           choices[i][0]==GL_INVALID_INDEX||choices[i][1]==GL_INVALID_INDEX){
            CHECK(0);glDeleteProgram(program);return;
        }
    }
    for(unsigned iteration=0;iteration<16;++iteration){
        GLuint expected=0;
        for(unsigned i=0;i<5;++i)if(active&(1u<<i)){
            unsigned bits=((iteration>>(i%3))+i)&3;GLuint selection[2];
            selection[locations[i][0]]=choices[i][bits&1];
            selection[locations[i][1]]=choices[i][bits>>1];
            glUniformSubroutinesuiv(stages[i],2,selection);
            for(unsigned j=0;j<2;++j){GLuint current=GL_INVALID_INDEX;
                glGetUniformSubroutineuiv(stages[i],locations[i][j],&current);
                CHECK(current==choices[i][(bits>>j)&1]);}
            expected|=bits<<(i*2);
        }
        const GLuint clear[4]={0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu};
        glClearBufferuiv(GL_COLOR,0,clear);
        glDrawArrays(tessellation?GL_PATCHES:GL_TRIANGLES,0,3);
        GLuint pixels[4*4*4]={0};glReadPixels(0,0,4,4,GL_RGBA_INTEGER,GL_UNSIGNED_INT,pixels);
        CHECK(glGetError()==GL_NO_ERROR);
        for(unsigned pixel=0;pixel<16;++pixel){
            GLuint *p=pixels+pixel*4;
            if(p[0]!=expected||p[1]!=(expected^341u)||p[2]!=1023u-expected||p[3]!=1){
                fprintf(stderr,"pixel tess=%d geom=%d iteration=%u pixel=%u: %u,%u,%u,%u expected=%u\n",
                    tessellation,geometry,iteration,pixel,p[0],p[1],p[2],p[3],expected);
                CHECK(0);break;
            }
        }
    }
    printf("multistage tess=%d geometry=%d completed 16 draws\n",tessellation,geometry);
    glUseProgram(0);glDeleteProgram(program);
}
int main(void)
{
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);printf("multistage renderer=%s\n",glGetString(GL_RENDERER));
    GLuint vao=0,texture=0,framebuffer=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32UI,4,4,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,NULL);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,4,4);glPatchParameteri(GL_PATCH_VERTICES,3);
    for(int tess=0;tess<2;++tess)for(int geom=0;geom<2;++geom)verify(tess,geom);
    glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);glDeleteVertexArrays(1,&vao);
    CHECK(glGetError()==GL_NO_ERROR);CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("multistage failures=%d\n",failures);return failures?1:0;
}
