/* Public period-CGL signed attribute conversion, observed by real draws.
 * Integer inputs and two floating-point controls use the same shader/FBO.
 * Controls are independent observations, not accepted alternate assertions:
 * their pixels identify which conversion the driver actually implements.
 * --kind and --path isolate a crashing native stream without hiding others.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *kinds[]={"color","normal","secondary","generic"};
static const char *paths[]={"immediate","arrays","elements","array-element","list","immediate-list"};
static const GLfloat positions[]={-1,-1,3,-1,-1,3};
static unsigned errors,observations;
static GLhandleARB programs[4];
static int selected_kind=-1,selected_path=-1;
static int scalar_setters;
static int compile_execute;
static int strided_arrays;
static int gpu_arrays;
#ifdef VIM_NORMALIZATION_GPU_CONTROL
#include "opengl-normalization-gpu-control.h"
#endif
static GLhandleARB program(unsigned kind)
{
    const char *value[]={"gl_Color.rgb","gl_Normal","gl_SecondaryColor.rgb","probeValue.rgb"};
    char vertex[512];snprintf(vertex,sizeof(vertex),
        "#version 110\nattribute vec4 probeValue;varying vec3 rawValue;"
        "void main(){gl_Position=gl_Vertex;rawValue=%s;}",value[kind]);
    const GLcharARB *fragment="#version 110\nvarying vec3 rawValue;uniform float gain;"
        "void main(){gl_FragColor=vec4(rawValue*gain+vec3(0.5),1.0);}";
    GLhandleARB object=glCreateProgramObjectARB();
    for(unsigned i=0;i<2;++i){
        GLhandleARB shader=glCreateShaderObjectARB(i?GL_FRAGMENT_SHADER_ARB:GL_VERTEX_SHADER_ARB);
        const GLcharARB *source=i?fragment:vertex;
        glShaderSourceARB(shader,1,&source,NULL);glCompileShaderARB(shader);
        GLint good=0;glGetObjectParameterivARB(shader,GL_OBJECT_COMPILE_STATUS_ARB,&good);
        if(!good){char log[2048];GLsizei n=0;glGetInfoLogARB(shader,sizeof(log),&n,log);
            fprintf(stderr,"%s compile failed: %.*s\n",kinds[kind],(int)n,log);return 0;}
        glAttachObjectARB(object,shader);glDeleteObjectARB(shader);
    }
    glBindAttribLocationARB(object,6,"probeValue");glLinkProgramARB(object);
    GLint good=0;glGetObjectParameterivARB(object,GL_OBJECT_LINK_STATUS_ARB,&good);
    if(!good){char log[2048];GLsizei n=0;glGetInfoLogARB(object,sizeof(log),&n,log);
        fprintf(stderr,"%s link failed: %.*s\n",kinds[kind],(int)n,log);return 0;}
    return object;
}
static void set_float(unsigned kind,GLfloat value)
{
    if(kind==0)glColor4f(value,value,value,value);
    else if(kind==1)glNormal3f(value,value,value);
    else if(kind==2)glSecondaryColor3fEXT(value,value,value);
    else glVertexAttrib4fARB(6,value,value,value,value);
}
static void set_integer(unsigned kind,GLenum type,const void *value)
{
    if(scalar_setters&&kind<3){
        if(type==GL_BYTE){const GLbyte *v=value;
            if(kind==0)glColor4b(v[0],v[1],v[2],v[3]);else if(kind==1)glNormal3b(v[0],v[1],v[2]);
            else glSecondaryColor3bEXT(v[0],v[1],v[2]);
        }else if(type==GL_SHORT){const GLshort *v=value;
            if(kind==0)glColor4s(v[0],v[1],v[2],v[3]);else if(kind==1)glNormal3s(v[0],v[1],v[2]);
            else glSecondaryColor3sEXT(v[0],v[1],v[2]);
        }else{const GLint *v=value;
            if(kind==0)glColor4i(v[0],v[1],v[2],v[3]);else if(kind==1)glNormal3i(v[0],v[1],v[2]);
            else glSecondaryColor3iEXT(v[0],v[1],v[2]);
        }
        return;
    }
    if(type==GL_BYTE){
        if(kind==0)glColor4bv(value);else if(kind==1)glNormal3bv(value);
        else if(kind==2)glSecondaryColor3bvEXT(value);else glVertexAttrib4NbvARB(6,value);
    }else if(type==GL_SHORT){
        if(kind==0)glColor4sv(value);else if(kind==1)glNormal3sv(value);
        else if(kind==2)glSecondaryColor3svEXT(value);else glVertexAttrib4NsvARB(6,value);
    }else{
        if(kind==0)glColor4iv(value);else if(kind==1)glNormal3iv(value);
        else if(kind==2)glSecondaryColor3ivEXT(value);else glVertexAttrib4NivARB(6,value);
    }
}
static void array(unsigned kind,GLenum type,const void *pointer,int enabled,GLsizei stride)
{
    GLenum cap=kind==0?GL_COLOR_ARRAY:kind==1?GL_NORMAL_ARRAY:GL_SECONDARY_COLOR_ARRAY;
    if(kind==3){
        if(enabled){glVertexAttribPointerARB(6,4,type,GL_TRUE,stride,pointer);glEnableVertexAttribArrayARB(6);}
        else glDisableVertexAttribArrayARB(6);
    }else{
        if(enabled){
            if(kind==0)glColorPointer(4,type,stride,pointer);
            else if(kind==1)glNormalPointer(type,stride,pointer);
            else glSecondaryColorPointerEXT(3,type,stride,pointer);
            glEnableClientState(cap);
        }else glDisableClientState(cap);
    }
}
static void read_pixel(GLubyte *pixel)
{
    glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();
    if(error||pixel[0]!=pixel[1]||pixel[1]!=pixel[2]||pixel[3]!=255){
        fprintf(stderr,"draw error=%x rgba=%u,%u,%u,%u\n",(unsigned)error,pixel[0],pixel[1],pixel[2],pixel[3]);++errors;
    }
}
static void control(unsigned kind,GLfloat value,GLubyte *pixel)
{
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_TRIANGLES);set_float(kind,value);
    for(unsigned i=0;i<3;++i)glVertex2fv(positions+2*i);glEnd();read_pixel(pixel);
}
static void run(unsigned kind,unsigned path,unsigned vbo,GLenum type)
{
    union {GLbyte b[12];GLshort s[12];GLint i[12];} data;
    int64_t maximum=type==GL_BYTE?127:type==GL_SHORT?32767:INT32_MAX;
    const int64_t values[]={-maximum-1,-maximum,-1,0,1,maximum/2,maximum};
    unsigned bytes=type==GL_BYTE?1:type==GL_SHORT?2:4;
    unsigned size=kind==1||kind==2?3:4;
    GLuint buffer=0;glGenBuffers(1,&buffer);glUseProgramObjectARB(programs[kind]);
    GLint location=glGetUniformLocationARB(programs[kind],"gain");
    if(location<0){fprintf(stderr,"missing gain uniform for %s\n",kinds[kind]);++errors;return;}
    glUniform1fARB(location,type==GL_BYTE?32:type==GL_SHORT?8192:536870912);
    if(glGetError()){++errors;return;}
    for(unsigned sample=0;sample<7;++sample){
        unsigned previous_errors=errors;
        int64_t integer=values[sample];
        for(unsigned i=0;i<3*size;++i){
            if(type==GL_BYTE)data.b[i]=(GLbyte)integer;
            else if(type==GL_SHORT)data.s[i]=(GLshort)integer;else data.i[i]=(GLint)integer;
        }
        unsigned char strided[128];const void *source=&data;
        GLsizei stride=0;size_t source_bytes=3*size*bytes;
        if(strided_arrays){
            stride=(GLsizei)(size*bytes+4);source_bytes=2*(size_t)stride+size*bytes;
            memset(strided,0xa5,sizeof(strided));source=strided+4;
            for(unsigned vertex=0;vertex<3;++vertex)
                memcpy(strided+4+vertex*stride,(const unsigned char *)&data+vertex*size*bytes,size*bytes);
        }
        GLfloat biased=(GLfloat)((2.0*(double)integer+1.0)/(2.0*(double)maximum+1.0));
        GLfloat symmetric=(GLfloat)((double)integer/(double)maximum);if(symmetric < -1)symmetric=-1;
        GLubyte old_pixel[4]={0},new_pixel[4]={0},actual[4]={0};
        control(kind,biased,old_pixel);control(kind,symmetric,new_pixel);
        glBindBuffer(GL_ARRAY_BUFFER,vbo?buffer:0);
        if(vbo)glBufferData(GL_ARRAY_BUFFER,source_bytes,source,GL_STREAM_DRAW);
        if(path&&path!=5){
#ifdef VIM_NORMALIZATION_GPU_CONTROL
            if(gpu_arrays&&(type==GL_BYTE||type==GL_SHORT)){
                GLuint converted=gpu_convert(type,size,vbo?buffer:0,&data);
                if(!converted)++errors;
                glUseProgramObjectARB(programs[kind]);glBindBuffer(GL_ARRAY_BUFFER,converted);
                array(kind,GL_FLOAT,NULL,1,4*sizeof(GLfloat));
            }else
#endif
            array(kind,type,vbo?NULL:source,1,stride);
        }
        glBindBuffer(GL_ARRAY_BUFFER,0);glVertexPointer(2,GL_FLOAT,0,positions);
        glEnableClientState(GL_VERTEX_ARRAY);glClear(GL_COLOR_BUFFER_BIT);
        GLuint list=path>=4?glGenLists(1):0;
        if(path>=4&&!list){fprintf(stderr,"list allocation failed\n");++errors;}
        if(list)glNewList(list,compile_execute?GL_COMPILE_AND_EXECUTE:GL_COMPILE);
        if(path==1)glDrawArrays(GL_TRIANGLES,0,3);
        else if(path==2){GLubyte indices[]={0,1,2};glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices);}
        else{
            glBegin(GL_TRIANGLES);
            if(!path||path==5){set_integer(kind,type,&data);for(unsigned i=0;i<3;++i)glVertex2fv(positions+2*i);}
            else for(unsigned i=0;i<3;++i)glArrayElement(i);
            glEnd();
        }
        if(list){glEndList();if(!glIsList(list)){fprintf(stderr,"list compilation failed\n");++errors;}
            if(compile_execute){
                GLubyte executed[4]={0};read_pixel(executed);++observations;
                printf("EXEC kind=%s path=%s vbo=%u type=%x sample=%u actual=%u biased=%u symmetric=%u errors=%u\n",
                    kinds[kind],paths[path],vbo,(unsigned)type,sample,executed[0],old_pixel[0],new_pixel[0],errors-previous_errors);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            memset(&data,0x5a,sizeof(data));memset(strided,0x5a,sizeof(strided));glCallList(list);glDeleteLists(list,1);}
        read_pixel(actual);array(kind,type,NULL,0,0);glDisableClientState(GL_VERTEX_ARRAY);
        ++observations;
        printf("PIXEL kind=%s path=%s vbo=%u type=%x sample=%u value=%lld actual=%u biased=%u symmetric=%u errors=%u\n",
            kinds[kind],paths[path],vbo,(unsigned)type,sample,(long long)integer,actual[0],old_pixel[0],new_pixel[0],errors-previous_errors);fflush(stdout);
    }
    glBindBuffer(GL_ARRAY_BUFFER,0);glDeleteBuffers(1,&buffer);
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--scalar")){scalar_setters=1;continue;}
        if(!strcmp(argv[i],"--compile-execute")){compile_execute=1;continue;}
        if(!strcmp(argv[i],"--strided")){strided_arrays=1;continue;}
#ifdef VIM_NORMALIZATION_GPU_CONTROL
        if(!strcmp(argv[i],"--gpu-arrays")){gpu_arrays=1;continue;}
#endif
        const char **choices=NULL;int *selection=NULL;unsigned count=0;
        if(!strcmp(argv[i],"--kind")){choices=kinds;selection=&selected_kind;count=4;}
        else if(!strcmp(argv[i],"--path")){choices=paths;selection=&selected_path;count=6;}
        else return 2;
        if(++i==argc)return 2;
        *selection=-1;
        for(unsigned j=0;j<count;++j)if(!strcmp(argv[i],choices[j]))*selection=(int)j;
        if(*selection<0)return 2;
    }
    if(strided_arrays&&gpu_arrays)return 2; /* Packed-only experimental converter. */
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s bits=%u scalar_setters=%d compile_execute=%d strided=%d gpu_arrays=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)sizeof(void *)*8,scalar_setters,compile_execute,strided_arrays,gpu_arrays);fflush(stdout);
    GLuint framebuffer=0,renderbuffer=0;
    glGenRenderbuffersEXT(1,&renderbuffer);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,renderbuffer);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT||glGetError())return 2;
    glViewport(0,0,16,16);glDisable(GL_DITHER);glDisable(GL_BLEND);glClearColor(1,0,0,0);
    for(unsigned kind=0;kind<4;++kind){
        if(selected_kind>=0&&(unsigned)selected_kind!=kind)continue;
        programs[kind]=program(kind);if(!programs[kind])return 2;
        for(unsigned path=0;path<6;++path){
            if(selected_path>=0&&(unsigned)selected_path!=path)continue;
            for(unsigned vbo=0;vbo<(path&&path!=5?2u:1u);++vbo){
                run(kind,path,vbo,GL_BYTE);run(kind,path,vbo,GL_SHORT);run(kind,path,vbo,GL_INT);
            }
        }
    }
    glUseProgramObjectARB(0);for(unsigned i=0;i<4;++i)if(programs[i])glDeleteObjectARB(programs[i]);
#ifdef VIM_NORMALIZATION_GPU_CONTROL
    gpu_conversion_destroy();
#endif
    glDeleteFramebuffersEXT(1,&framebuffer);glDeleteRenderbuffersEXT(1,&renderbuffer);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("signed-normalization-pixels observations=%u api_or_draw_errors=%u\n",observations,errors);return errors?1:0;
}
