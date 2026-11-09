/* Guest-only, explicit-process CTS control. NOT the production shader codec:
 * this limited control recognizes the suite's #version 400 source convention.
 * General lowering must preserve source queries, directives, macros and user
 * overloads before it can be integrated into the guest driver. No host patch. */
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static char *prelude;
static unsigned changed;
__attribute__((constructor)) static void load_prelude(void)
{
    const char *path=getenv("VIM_TEST_GLSL_PRELUDE");if(!path)return;
    FILE *file=fopen(path,"rb");if(!file)return;
    char *text=calloc(1,65536);if(!text){fclose(file);return;}
    size_t length=fread(text,1,65535,file);int extra=fgetc(file);
    if(!ferror(file)&&extra==EOF){text[length]=0;prelude=text;}else free(text);
    fclose(file);
}
static void shader_source(GLuint shader,GLsizei count,const GLchar *const *strings,const GLint *lengths)
{
    if(!prelude||count<=0||!strings){glShaderSource(shader,count,strings,lengths);return;}
    size_t total=0;
    for(GLsizei i=0;i<count;++i){
        if(!strings[i]){glShaderSource(shader,count,strings,lengths);return;}
        size_t length=lengths&&lengths[i]>=0?(size_t)lengths[i]:strlen(strings[i]);
        if(length>(size_t)INT_MAX-total){glShaderSource(shader,count,strings,lengths);return;}total+=length;
    }
    char *source=malloc(total+1);if(!source){glShaderSource(shader,count,strings,lengths);return;}
    size_t offset=0;
    for(GLsizei i=0;i<count;++i){
        size_t length=lengths&&lengths[i]>=0?(size_t)lengths[i]:strlen(strings[i]);
        memcpy(source+offset,strings[i],length);offset+=length;
    }
    source[total]=0;char *line=strchr(source,'\n');
    if(strncmp(source,"#version 400",12)||!line||!strstr(source,"roundEven")){
        free(source);glShaderSource(shader,count,strings,lengths);return;
    }
    size_t prefix=(size_t)(line+1-source),extra=strlen(prelude),bytes=total+extra+11;
    char *lowered=malloc(bytes+1);
    if(!lowered||bytes>INT_MAX){free(lowered);free(source);glShaderSource(shader,count,strings,lengths);return;}
    memcpy(lowered,source,prefix);memcpy(lowered+prefix,prelude,extra);
    memcpy(lowered+prefix+extra,"\n#line 2\n",9);
    memcpy(lowered+prefix+extra+9,source+prefix,total-prefix);
    size_t actual=total+extra+9;lowered[actual]=0;
    GLint length=(GLint)actual;const GLchar *text=lowered;
    glShaderSource(shader,1,&text,&length);++changed;free(lowered);free(source);
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} control={
    (const void *)&shader_source,(const void *)&glShaderSource};
__attribute__((destructor)) static void report_control(void)
{fprintf(stderr,"GUEST SOURCE CONTROL lowered=%u\n",changed);free(prelude);}
