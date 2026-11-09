/* Owned-process native list investigation, never included in the runtime.
 * Read actual loaded-image symbols, not a build address or instruction patch. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLContext.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uintptr_t allocator;
struct Marker {uint32_t bytes,value;};
static unsigned marker_calls,marker_value;
static GLfloat marker_color;
static unsigned marker(GLIContext context,const void *data)
{
    struct Marker record;memcpy(&record,data,sizeof(record));GLfloat color[4]={0};
    glGetFloatv(GL_CURRENT_COLOR,color);++marker_calls;marker_value=record.value;marker_color=color[0];
    printf("native callback marker=%u current-context=%d color=%g error=%x\n",
        record.value,CGLGetCurrentContext()->rend==context,color[0],glGetError());
    return record.bytes;
}
static int append_control(CGLContextObj context)
{
    if(!allocator){puts("No loaded native list allocator symbol; no command inserted.");return 77;}
    typedef void *(*AddCommand)(GLIContext,uintptr_t,size_t);
    uintptr_t callback=(uintptr_t)&marker;
#if defined(__arm64__)
    /* Observed native compiler uses PACIZA; gleCallList uses BLRAAZ. Sign
     * only our callback value, never a system import or executable page. */
    uintptr_t zero=0;__asm__ volatile("pacia %0, %1":"+r"(callback):"r"(zero));
#endif
    GLuint list=glGenLists(1);glColor4f(.125f,0,0,1);
    glNewList(list,GL_COMPILE);glColor4f(.25f,0,0,1);
    struct Marker record={sizeof(record),7};
    void *data=((AddCommand)allocator)(context->rend,callback,sizeof(record));
    if(!data)return 2;memcpy(data,&record,sizeof(record));
    glColor4f(.75f,0,0,1);glEndList();
    GLint index=-1;glGetIntegerv(GL_LIST_INDEX,&index);
    GLfloat color[4]={0};glGetFloatv(GL_CURRENT_COLOR,color);
    int failures=marker_calls||index||color[0]!=.125f||glGetError();
    glCallList(list);glGetFloatv(GL_CURRENT_COLOR,color);
    failures+=marker_calls!=1||marker_value!=7||marker_color!=.25f||color[0]!=.75f||glGetError();
    glDeleteLists(list,1);
    printf("native-list-append callbacks=%u final-color=%g failures=%d\n",marker_calls,color[0],failures);
    return failures?1:0;
}
static void inspect_engine(const struct mach_header_64 *header,intptr_t slide)
{
    const struct segment_command_64 *link=NULL;
    const struct symtab_command *symbols=NULL;
    const struct load_command *command=(const void *)(header+1);
    for(unsigned i=0;i<header->ncmds;++i){
        if(command->cmd==LC_SEGMENT_64){
            const struct segment_command_64 *segment=(const void *)command;
            if(!strcmp(segment->segname,SEG_LINKEDIT))link=segment;
        }else if(command->cmd==LC_SYMTAB)symbols=(const void *)command;
        command=(const void *)((const char *)command+command->cmdsize);
    }
    if(!link||!symbols)return;
    uintptr_t base=slide+link->vmaddr-link->fileoff;
    const struct nlist_64 *table=(const void *)(base+symbols->symoff);
    const char *strings=(const void *)(base+symbols->stroff);
    unsigned found=0;
    printf("loaded engine symbols=%u\n",symbols->nsyms);
    for(unsigned i=0;i<symbols->nsyms;++i){
        uint32_t index=table[i].n_un.n_strx;
        if(index>=symbols->strsize||!memchr(strings+index,0,symbols->strsize-index))continue;
        if((table[i].n_type&N_TYPE)!=N_SECT)continue;
        const char *name=strings+index;
        if(!strcmp(name,"_gleAddCommand"))allocator=slide+table[i].n_value;
        if(!strcmp(name,"_gleAddCommand")||!strcmp(name,"_gleAddListItemWithFree")||
           !strcmp(name,"_gliCreateContext")||
           (strstr(name,"CompressedTex")&&strstr(name,"_ListExec"))){
            printf("symbol=%s address=%p external=%d\n",name,
                (void *)(slide+table[i].n_value),!!(table[i].n_type&N_EXT));++found;
        }
    }
    printf("matched list/control symbols=%u\n",found);
}
int main(int argc,char **argv)
{
    int append=argc==2&&!strcmp(argv[1],"--append");
    if(argc!=1&&!append)return 64;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    for(unsigned i=0;i<_dyld_image_count();++i){
        const char *name=_dyld_get_image_name(i);
        if(!name||!strstr(name,"/GLEngine.bundle/GLEngine"))continue;
        const struct mach_header *header=_dyld_get_image_header(i);
        if(header->magic==MH_MAGIC_64)inspect_engine((const void *)header,_dyld_get_image_vmaddr_slide(i));
        void *library=dlopen(name,RTLD_LAZY|RTLD_NOLOAD);
        if(library){
            printf("dynamic allocator=%p control=%p\n",dlsym(library,"gleAddCommand"),dlsym(library,"gliCreateContext"));
            dlclose(library);
        }
    }
    int result=append?append_control(context):0;
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);return result;
}
