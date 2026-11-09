/* Distinguish an SDK symbol from a period renderer's advertised contract. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const char *const names[]={
    "GL_ARB_imaging","GL_EXT_color_table","GL_EXT_convolution","GL_EXT_histogram",
    "GL_ARB_vertex_blend","GL_APPLE_vertex_program_evaluators","GL_APPLE_aux_depth_stencil",
    "GL_APPLE_element_array","GL_APPLE_vertex_array_range","GL_APPLE_vertex_array_object",
    "GL_NV_register_combiners","GL_NV_register_combiners2",
    "GL_ARB_shading_language_include","GL_ARB_gpu_shader_fp64","GL_EXT_debug_label",
    "GL_ARB_shader_objects","GL_APPLE_object_purgeable","GL_EXT_bindable_uniform",
    "GL_APPLE_vertex_point_size",
};
static int token(const char *haystack,const char *name)
{
    if(!haystack)return 0;
    size_t length=strlen(name);
    for(const char *p=haystack;(p=strstr(p,name));++p)
        if((p==haystack || p[-1]==' ') && (!p[length] || p[length]==' '))return 1;
    return 0;
}
int main(void)
{
    typedef const GLubyte *(*IndexedString)(GLenum,GLuint);
    IndexedString indexed=(IndexedString)dlsym(RTLD_DEFAULT,"glGetStringi");
    for(int core=0;core<=1;++core){
        if(core && !indexed)continue;
        const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
            kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
            (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_GL4_Core:kCGLOGLPVersion_Legacy),0};
        CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
        if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf){if(core)continue;return 77;}
        if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
        CGLDestroyPixelFormat(pf);
        printf("profile=%s renderer=%s GL=%s\n",core?"core":"compatibility",
               glGetString(GL_RENDERER),glGetString(GL_VERSION));
        const char *compat=core?NULL:(const char *)glGetString(GL_EXTENSIONS);
        if(core)glGetIntegerv(0x821d,&n); /* NUM_EXTENSIONS */
        for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i){
            int present=token(compat,names[i]);
            for(GLint j=0;core && !present && j<n;++j){
                const char *entry=(const char *)indexed(GL_EXTENSIONS,(GLuint)j);
                present=entry && !strcmp(entry,names[i]);
            }
            printf("  %s %s\n",present?"yes":" no",names[i]);
        }
        if(glGetError()!=GL_NO_ERROR)return 3;
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    }
    return 0;
}
