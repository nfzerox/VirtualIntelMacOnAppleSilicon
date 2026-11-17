/* Explicit, process-local comparison fixture. Never installed in a guest
 * payload or host application. Compile either as the temporary .app launcher
 * or its interposer; the ORIGINAL game executable and data remain unchanged.
 * Run the ordinary public pixel fixtures first to control observation effects.
 */
#ifdef VIM_GAME_STATE_LAUNCHER
#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
static int setting(CFBundleRef bundle,CFStringRef key,char *out)
{
    CFTypeRef value=CFBundleGetValueForInfoDictionaryKey(bundle,key);
    return value&&CFGetTypeID(value)==CFStringGetTypeID()&&
        CFStringGetCString(value,out,PATH_MAX,kCFStringEncodingUTF8)&&out[0]=='/';
}
int main(void)
{
    CFBundleRef bundle=CFBundleGetMainBundle();
    char executable[PATH_MAX],library[PATH_MAX],output[PATH_MAX];
    if(!bundle||!setting(bundle,CFSTR("VIMOriginalExecutable"),executable))return 64;
    CFURLRef module=CFBundleCopyResourceURL(bundle,CFSTR("state"),CFSTR("dylib"),NULL);
    CFURLRef app=CFBundleCopyBundleURL(bundle);
    CFURLRef directory=app?CFURLCreateCopyDeletingLastPathComponent(NULL,app):NULL;
    CFURLRef log=directory?CFURLCreateCopyAppendingPathComponent(NULL,directory,CFSTR("NativeState.log"),false):NULL;
    if(!module||!log||!CFURLGetFileSystemRepresentation(module,true,(UInt8 *)library,sizeof(library))||
       !CFURLGetFileSystemRepresentation(log,true,(UInt8 *)output,sizeof(output)))return 64;
    CFRelease(module);CFRelease(log);CFRelease(directory);CFRelease(app);
    if(setenv("DYLD_INSERT_LIBRARIES",library,1)||setenv("VIM_NATIVE_GAME_STATE",output,1))return 71;
    char *arguments[]={executable,NULL};
    execv(executable,arguments);perror("original game exec");return 71;
}
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static FILE *output;
static unsigned draws;
static uint64_t seen[24];
static void vector(GLenum key,unsigned count)
{
    GLfloat values[16]={0};glGetFloatv(key,values);fprintf(output,"VECTOR %x",key);
    for(unsigned i=0;i<count;++i)fprintf(output," %g",values[i]);fputc('\n',output);
}
static void array(GLenum enabled,GLenum size_key,GLenum type_key,GLenum stride_key,
                  GLenum buffer_key,GLenum pointer_key)
{
    if(!glIsEnabled(enabled))return;
    GLint size=3,type=0,stride=0,buffer=0,binding=0;void *pointer=NULL;
    if(size_key)glGetIntegerv(size_key,&size);
    glGetIntegerv(type_key,&type);glGetIntegerv(stride_key,&stride);
    glGetIntegerv(buffer_key,&buffer);glGetPointerv(pointer_key,&pointer);
    fprintf(output,"ARRAY %x size=%d type=%x stride=%d buffer=%d offset=%llu\n",
        enabled,size,type,stride,buffer,(unsigned long long)(uintptr_t)pointer);
    if(type!=GL_FLOAT||size<1||size>4||stride<0)return;
    size_t step=stride?(size_t)stride:(size_t)size*sizeof(GLfloat);
    if(step>256/3)return;
    size_t bytes=3*step+size*sizeof(GLfloat);
    if(bytes>256)return;
    // A CPU array has no public allocation length. Record its descriptor but
    // never speculate that four vertices are readable from an arbitrary pointer.
    if(!buffer)return;
    unsigned char captured[256];const unsigned char *data=pointer;
    if(buffer){
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);glBindBuffer(GL_ARRAY_BUFFER,(GLuint)buffer);
        GLint length=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&length);
        if(length<0||(uintptr_t)pointer>(uintptr_t)length||bytes>(uintptr_t)length-(uintptr_t)pointer){
            glBindBuffer(GL_ARRAY_BUFFER,(GLuint)binding);return;
        }
        glGetBufferSubData(GL_ARRAY_BUFFER,(GLintptr)(uintptr_t)pointer,(GLsizeiptr)bytes,captured);
        glBindBuffer(GL_ARRAY_BUFFER,(GLuint)binding);data=captured;
    }
    if(!data)return;
    for(unsigned v=0;v<4;++v){
        fprintf(output,"  V%u",v);
        for(GLint i=0;i<size;++i){GLfloat f;memcpy(&f,data+v*step+i*sizeof(f),sizeof(f));fprintf(output," %g",f);}
        fputc('\n',output);
    }
}
static void trace(GLenum mode,GLsizei count,GLenum type)
{
    /* Meshes, not the splash text. No work at all unless explicitly armed.
     * This observer can synchronize GPU buffer reads; NEVER use it for FPS. */
    if(!CGLGetCurrentContext()||!getenv("VIM_NATIVE_GAME_STATE")||
       (count<100&&!getenv("VIM_NATIVE_GAME_STATE_ALL")))return;
    pthread_mutex_lock(&lock);
    if(draws>=24){pthread_mutex_unlock(&lock);return;}
    GLint element=0,vertex=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING,&vertex);
    uint64_t key=((uint64_t)mode<<56)^((uint64_t)type<<32)^(unsigned)count^
        ((uint64_t)(unsigned)element<<16)^((uint64_t)(unsigned)vertex<<40);
    for(unsigned i=0;i<draws;++i)if(seen[i]==key){pthread_mutex_unlock(&lock);return;}
    if(!output)output=fopen(getenv("VIM_NATIVE_GAME_STATE"),"a");
    if(!output){pthread_mutex_unlock(&lock);return;}
    seen[draws++]=key;
    fprintf(output,"DRAW %u pid=%d context=%p mode=%x count=%d type=%x renderer=%s version=%s\n",
        draws,getpid(),(void *)CGLGetCurrentContext(),mode,count,type,glGetString(GL_RENDERER),glGetString(GL_VERSION));
    const GLenum caps[]={GL_CULL_FACE,GL_DEPTH_TEST,GL_LIGHTING,GL_FOG,GL_BLEND,GL_ALPHA_TEST};
    for(unsigned i=0;i<sizeof(caps)/sizeof(caps[0]);++i)fprintf(output,"CAP %x=%u\n",caps[i],glIsEnabled(caps[i]));
    const GLenum keys[]={GL_FRONT_FACE,GL_CULL_FACE_MODE,GL_DEPTH_FUNC,GL_DEPTH_WRITEMASK,
        GL_DRAW_BUFFER,GL_READ_BUFFER,GL_ELEMENT_ARRAY_BUFFER_BINDING};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){GLint value=0;glGetIntegerv(keys[i],&value);fprintf(output,"STATE %x=%x\n",keys[i],value);}
    vector(GL_MODELVIEW_MATRIX,16);vector(GL_PROJECTION_MATRIX,16);vector(GL_DEPTH_RANGE,2);
    vector(GL_DEPTH_CLEAR_VALUE,1);vector(GL_CURRENT_COLOR,4);vector(GL_LIGHT_MODEL_AMBIENT,4);
    array(GL_VERTEX_ARRAY,GL_VERTEX_ARRAY_SIZE,GL_VERTEX_ARRAY_TYPE,GL_VERTEX_ARRAY_STRIDE,
        GL_VERTEX_ARRAY_BUFFER_BINDING,GL_VERTEX_ARRAY_POINTER);
    array(GL_NORMAL_ARRAY,0,GL_NORMAL_ARRAY_TYPE,GL_NORMAL_ARRAY_STRIDE,
        GL_NORMAL_ARRAY_BUFFER_BINDING,GL_NORMAL_ARRAY_POINTER);
    GLint client=0;glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&client);
    for(unsigned unit=0;unit<2;++unit){
        glClientActiveTexture(GL_TEXTURE0+unit);fprintf(output,"COORD_UNIT %u\n",unit);
        array(GL_TEXTURE_COORD_ARRAY,GL_TEXTURE_COORD_ARRAY_SIZE,GL_TEXTURE_COORD_ARRAY_TYPE,
            GL_TEXTURE_COORD_ARRAY_STRIDE,GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING,GL_TEXTURE_COORD_ARRAY_POINTER);
    }
    glClientActiveTexture((GLenum)client);
    fflush(output);pthread_mutex_unlock(&lock);
}
static void range(GLenum mode,GLuint first,GLuint last,GLsizei count,GLenum type,const void *indices)
{trace(mode,count,type);glDrawRangeElements(mode,first,last,count,type,indices);}
static void elements(GLenum mode,GLsizei count,GLenum type,const void *indices)
{trace(mode,count,type);glDrawElements(mode,count,type,indices);}
/* dyld does not interpose the replacement library's own original references. */
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} interpose[]={
    {(const void *)range,(const void *)glDrawRangeElements},
    {(const void *)elements,(const void *)glDrawElements}
};
#endif
