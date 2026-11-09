/* Native Mavericks private surface imports order queued geometry before an
 * in-place destination read. CGLIOSurface on Metal requires a producer flush
 * before consumer bind, even when both texture names belong to one context.
 * Track only aliases we created, not arbitrary application textures. No
 * readback, glFinish, guest round trip or global per-draw flush is needed.
 * Contexts sharing GL names share metadata; connections execute serially.
 */
struct DGAlias { GLuint texture,storage,source_storage;GLenum internal;
    int width,height;uint64_t copied_serial;
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
    uint64_t binding_generation;
#endif
    IOSurfaceRef owned_storage,producer_storage;uint64_t iosurface_write_serial,iosurface_submitted_serial;
    struct DGHostContext *iosurface_writer;
    unsigned iosurface_import;struct DGAlias *next; };
struct DGUniformShape {GLuint program;GLint location;uint32_t components;};
struct DGAliasSamplers {GLuint program;GLint *locations;unsigned count;int valid,fragment_stage;};
struct DGAtlasProgram {GLuint name;GLint sampler;struct DGAtlasProgram *next;};
struct DGAliasGroup { unsigned refs;uint64_t generation;struct DGAlias *aliases;
    unsigned nonzero_mipmap_base_possible;
    struct DGPrivateTextures *private_textures;
    struct DGAtlasProgram *atlas_programs;
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    uint64_t backing_write_serial; // all potentially pixel-mutating packets across the share group
#endif
    struct DGHostContext *members; // Serialized endpoint-owned contexts, never asynchronous jobs.
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
    uint64_t binding_generation;
#endif
    struct DGUniformShape uniform_shapes[256];
    struct DGAliasSamplers sampler_layouts[256];
    size_t owned_surface_bytes;unsigned owned_surface_count;
    unsigned iosurface_import_count;uint64_t iosurface_write_clock;
    uint32_t next_sync;struct DGHostSync *syncs;
    uint32_t next_map,map_count;size_t map_bytes;struct DGHostMapping *maps; };
static int DGNonzeroMipmapBasePossible(const struct DGHostContext *c)
{
    /* A conservative shared-group possibility, not a texture-state cache.
     * Native GL remains authoritative after binds, lists, stacks and sharing.
     * Owned decoder controls without a resource group take the query path. */
    return !c->alias_group || c->alias_group->nonzero_mipmap_base_possible;
}
static int DGCreateAliases(struct DGHostContext *c,struct DGHostContext *shared)
{
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&c->texture_units);
    if(c->texture_units<1 || c->texture_units>1024)return 0;
    c->sampled_storage=calloc((size_t)c->texture_units*3,sizeof(*c->sampled_storage));
    if(!c->sampled_storage)return 0;
    c->sampled_textures=c->sampled_storage+c->texture_units;
    c->rectangle_bindings=c->sampled_textures+c->texture_units;
    c->alias_group=shared?shared->alias_group:calloc(1,sizeof(*c->alias_group));
    if(!c->alias_group) {
        free(c->sampled_storage);c->sampled_storage=c->sampled_textures=c->rectangle_bindings=NULL;return 0;
    }
    if(!shared){
        c->alias_group->private_textures=DGPrivateTexturesCreate();
        if(!c->alias_group->private_textures){free(c->alias_group);c->alias_group=NULL;
            free(c->sampled_storage);c->sampled_storage=c->sampled_textures=c->rectangle_bindings=NULL;return 0;}
    }
    ++c->alias_group->refs;c->alias_bindings_dirty=1;
    c->alias_member_next=c->alias_group->members;c->alias_group->members=c;
    return 1;
}
static void DGDestroyAliases(struct DGHostContext *c)
{
    if(c->alias_framebuffer)glDeleteFramebuffersEXT(1,&c->alias_framebuffer);
    free(c->sampled_storage);c->sampled_storage=c->sampled_textures=c->rectangle_bindings=NULL;
    struct DGAliasGroup *g=c->alias_group;c->alias_group=NULL;
    if(!g)return;
    struct DGHostContext **member=&g->members;
    while(*member && *member!=c)member=&(*member)->alias_member_next;
    if(*member)*member=c->alias_member_next;c->alias_member_next=NULL;
    for(struct DGAlias *p=g->aliases;p;p=p->next)
        if(p->iosurface_writer==c)p->iosurface_writer=NULL;
    if(--g->refs)return;
    DGDestroySyncs(g);
    DGDestroyMappings(g);
    for(unsigned i=0;i<256;++i)free(g->sampler_layouts[i].locations);
    while(g->atlas_programs){struct DGAtlasProgram *p=g->atlas_programs;g->atlas_programs=p->next;free(p);}
    while(g->aliases) { struct DGAlias *p=g->aliases;g->aliases=p->next;
        if(p->owned_storage)CFRelease(p->owned_storage);
        if(p->producer_storage)CFRelease(p->producer_storage);free(p); }
    DGPrivateTexturesRelease(g->private_textures);free(g);
}
static int DGRegisterAlias(struct DGHostContext *c,GLuint texture,IOSurfaceRef storage)
{
    struct DGAliasGroup *g=c->alias_group;if(!g || !texture || !storage)return 0;
    struct DGAlias *p=g->aliases;for(;p;p=p->next)if(p->texture==texture)break;
    if(!p) { p=calloc(1,sizeof(*p));if(!p)return 0;p->texture=texture;p->next=g->aliases;g->aliases=p; }
    // Numeric IDs do not keep retired surfaces alive. Successful public
    // redefinition/delete and later private imports remove/replace metadata.
    if(p->owned_storage){
        g->owned_surface_bytes-=IOSurfaceGetAllocSize(p->owned_storage);--g->owned_surface_count;
        CFRelease(p->owned_storage);p->owned_storage=NULL;
    }
    if(p->producer_storage){CFRelease(p->producer_storage);p->producer_storage=NULL;}
    if(p->iosurface_import)--g->iosurface_import_count;
    p->iosurface_import=0;p->iosurface_write_serial=p->iosurface_submitted_serial=0;p->iosurface_writer=NULL;
    p->storage=IOSurfaceGetID(storage);p->source_storage=0;
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
    p->width=p->height=0;p->internal=0;p->binding_generation=UINT64_MAX;
#endif
    p->copied_serial=0;++g->generation;return 1;
}
/* Display lists can redefine a native texture without passing through our
 * individual TexImage wrappers. Keep existing alias lifetime metadata, but
 * require a real CGL import before reusing any direct storage binding again.
 * Saturation permanently disables reuse rather than accepting a wrapped age.
 */
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
static void DGInvalidateAliasStorage(struct DGHostContext *c)
{
    if(c->alias_group && c->alias_group->binding_generation!=UINT64_MAX)
        ++c->alias_group->binding_generation;
}
#endif
static void DGForgetAliases(struct DGHostContext *c,GLsizei count,const GLuint *textures)
{
    if(c->inside_begin || count<1 || !textures || !c->alias_group)return;
    struct DGAliasGroup *g=c->alias_group;struct DGAlias **link=&g->aliases;
    while(*link) {
        int remove=0;for(GLsizei i=0;i<count;++i)if(textures[i]==(*link)->texture) { remove=1;break; }
        if(remove) { struct DGAlias *p=*link;*link=p->next;
            if(p->iosurface_import)--g->iosurface_import_count;
            if(p->owned_storage){g->owned_surface_bytes-=IOSurfaceGetAllocSize(p->owned_storage);
                --g->owned_surface_count;CFRelease(p->owned_storage);}
            if(p->producer_storage)CFRelease(p->producer_storage);
            free(p);++g->generation; }
        else link=&(*link)->next;
    }
    c->alias_bindings_dirty=1;
}
static GLuint DGBeginAliasRedefinition(struct DGHostContext *c,GLenum target,GLint level)
{
    if(c->inside_begin || target!=GL_TEXTURE_RECTANGLE_ARB || level || !c->alias_group || !c->alias_group->aliases)return 0;
    GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    for(struct DGAlias *p=c->alias_group->aliases;p;p=p->next)if(p->texture==(GLuint)texture) {
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;return p->texture;
    }
    return 0;
}
static void DGEndAliasRedefinition(struct DGHostContext *c,GLuint texture)
{
    if(!texture)return;
    GLenum error=glGetError();if(error) { if(!c->error)c->error=error; }
    else DGForgetAliases(c,1,&texture);
}
/* Binding one rectangle texture does not invalidate every other texture
 * unit. Read the actual native binding, so an invalid target/name operation
 * cannot corrupt our cache or require consuming the application's GL error.
 * Full rescan remains mandatory after PopAttrib and unknown display lists.
 * Contexts with no aliases add no GL queries to ordinary texture binding.
 */
static void DGRecordAliasBinding(struct DGHostContext *c,GLenum target)
{
    if(target!=GL_TEXTURE_RECTANGLE_ARB || c->inside_begin)return;
    if(!c->alias_group || !c->alias_group->aliases){c->alias_bindings_dirty=1;return;}
    if(c->alias_bindings_dirty)return;
    GLint active=0,name=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    if(active<GL_TEXTURE0 || active-GL_TEXTURE0>=c->texture_units){c->alias_bindings_dirty=1;return;}
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&name);
    c->rectangle_bindings[active-GL_TEXTURE0]=(GLuint)name;c->alias_mappings_dirty=1;
}
static void DGForgetAliasSamplers(struct DGHostContext *c,GLuint program)
{
    if(!c->alias_group)return;
    for(unsigned i=0;i<256;++i){struct DGAliasSamplers *p=c->alias_group->sampler_layouts+i;
        if(!program||p->program==program){free(p->locations);*p=(struct DGAliasSamplers){0};}
    }
    // Display lists invalidate live draw state, not the linked program's
    // uniform interface. Only a particular program's relink/delete does.
    if(program){
        struct DGAtlasProgram **link=&c->alias_group->atlas_programs;
        for(;*link;link=&(*link)->next)if((*link)->name==program){
            struct DGAtlasProgram *p=*link;*link=p->next;free(p);break;
        }
    }
}
static struct DGAliasSamplers *DGAliasSamplerLayout(struct DGHostContext *,GLuint);
static void DGHostLinkProgram(struct DGHostContext *c,GLuint program)
{
    DGForgetAliasSamplers(c,program);
    glLinkProgram(program);
    if(!c->alias_group||!glIsProgram(program))return;
    GLint linked=0,count=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked)return;
    glGetProgramiv(program,GL_ATTACHED_SHADERS,&count);
    if(count<1||count>4096)return;
    GLuint *shaders=malloc((size_t)count*sizeof(*shaders));if(!shaders)return;
    GLsizei actual=0;glGetAttachedShaders(program,count,&actual,shaders);int fragment=0,box_downsample=0;
    for(GLsizei i=0;i<actual;++i){GLint type=0;glGetShaderiv(shaders[i],GL_SHADER_TYPE,&type);
        if(type==GL_FRAGMENT_SHADER){
            fragment=1;
            if(c->profile!=kCGLOGLPVersion_Legacy)continue;
            GLint length=0;glGetShaderiv(shaders[i],GL_SHADER_SOURCE_LENGTH,&length);
            if(length<1||length>4096)continue;
            char source[4096];glGetShaderSource(shaders[i],sizeof(source),NULL,source);
            box_downsample=
                strstr(source,"texture2DRect(tex, gl_TexCoord[0].xy + vec2(-0.5, -0.5))")&&
                strstr(source,"texture2DRect(tex, gl_TexCoord[0].xy + vec2(-0.5, 0.5))")&&
                strstr(source,"texture2DRect(tex, gl_TexCoord[0].xy + vec2( 0.5, -0.5))")&&
                strstr(source,"texture2DRect(tex, gl_TexCoord[0].xy + vec2( 0.5, 0.5))")&&
                strstr(source,"gl_FragColor *= 0.25");
        }
    }
    free(shaders);
    struct DGAliasSamplers *p=c->alias_group->sampler_layouts+(program%256);
    free(p->locations);*p=(struct DGAliasSamplers){.program=program,.fragment_stage=fragment};
    if(!fragment || c->profile!=kCGLOGLPVersion_Legacy)return;
    // These measured compositor downsample/blur programs reuse an atlas. Cache
    // the real linked rectangle sampler, not a program ID or shader offset.
    const char *name=NULL;
    if(glGetUniformLocation(program,"desktop_offset")>=0 && glGetUniformLocation(program,"xy_maximum")>=0)
        name="desktop_tex";
    else if(box_downsample||glGetUniformLocation(program,"skipDivideByAlpha")>=0)name="tex";
    if(!name)return;
    GLint sampler=glGetUniformLocation(program,name);if(sampler<0)return;
    struct DGAliasSamplers *layout=DGAliasSamplerLayout(c,program);
    if(!layout)return;
    for(unsigned i=0;i<layout->count;++i)if(layout->locations[i]==sampler){
        struct DGAtlasProgram *entry=malloc(sizeof(*entry));
        if(entry){
            *entry=(struct DGAtlasProgram){program,sampler,c->alias_group->atlas_programs};
            c->alias_group->atlas_programs=entry;
        }
        break;
    }
}
/* A bound rectangle is not necessarily sampled. In particular a compositor
 * can leave its destination alias on unit2 while drawing many ordinary
 * source-texture quads on unit0. Submitting the GPU for EACH such quad is not
 * a producer/consumer dependency. Reflect actual linked native programs;
 * cache only locations, never uniform values or speculative setter results.
 * Existing relink/delete/binary/display-list invalidation owns this cache too.
 * Unknown layouts and assembly programs retain conservative ordering. */
static struct DGAliasSamplers *DGAliasSamplerLayout(struct DGHostContext *c,GLuint program)
{
    struct DGAliasSamplers *p=c->alias_group->sampler_layouts+(program%256);
    // Vertex-only compatibility programs still use fixed-function (or ARB)
    // fragment texturing. Observe stages at LINK, not a later attached-shader
    // list which applications can mutate without changing the executable.
    if(p->program!=program||!p->fragment_stage)return NULL;
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    // A failed relink can leave an older executable current. Its reflection
    // is not a reliable description of that executable: do not narrow it.
    if(!linked)return NULL;
    if(p->valid)return p;
    GLint active=0,maximum=0;glGetProgramiv(program,GL_ACTIVE_UNIFORMS,&active);
    glGetProgramiv(program,GL_ACTIVE_UNIFORM_MAX_LENGTH,&maximum);
    if(active<0||active>4096||maximum<0||maximum>65536)return NULL;
    if(!active){p->valid=1;return p;}
    if(!maximum)return NULL;
    char *name=malloc((size_t)maximum),*element=malloc((size_t)maximum+16);
    GLint *locations=malloc(4096*sizeof(*locations));unsigned count=0;int good=0;
    if(!name||!element||!locations)goto done;
    for(GLuint i=0;i<(GLuint)active;++i){
        GLsizei length=0;GLint size=0;GLenum type=0;
        glGetActiveUniform(program,i,maximum,&length,&size,&type,name);
        if(length<0||length>=maximum||size<1||size>4096)goto done;
        if(type!=GL_SAMPLER_2D_RECT_ARB&&type!=GL_SAMPLER_2D_RECT_SHADOW_ARB&&
           type!=GL_INT_SAMPLER_2D_RECT_EXT&&type!=GL_UNSIGNED_INT_SAMPLER_2D_RECT_EXT)continue;
        if((unsigned)size>4096-count)goto done;
        name[length]=0;
        // Simple sampler arrays are specified with a trailing [0]. More
        // complex aggregate naming remains conservative, not guessed.
        char *index=strstr(name,"[0]");
        if(size>1&&(!index||index+3!=name+length))goto done;
        for(GLint j=0;j<size;++j){
            const char *query=name;
            if(j){snprintf(element,(size_t)maximum+16,"%.*s[%d]",(int)(index-name),name,j);query=element;}
            GLint location=glGetUniformLocation(program,query);if(location<0)goto done;
            locations[count++]=location;
        }
    }
    good=1;
done:
    free(name);free(element);
    if(!good){free(locations);return NULL;}
    if(count){
        GLint *compact=realloc(locations,(size_t)count*sizeof(*locations));
        p->locations=compact?compact:locations;p->count=count;
    }else free(locations);
    p->valid=1;return p;
}
static int DGAliasSampledUnits(struct DGHostContext *c,unsigned char used[1024])
{
    GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    if(!program)return 0; // Fixed function/ARB programs: conservative fallback.
    struct DGAliasSamplers *layout=DGAliasSamplerLayout(c,(GLuint)program);
    if(!layout)return 0;
    memset(used,0,(size_t)c->texture_units);
    for(unsigned i=0;i<layout->count;++i){
        GLint unit=-1;glGetUniformiv((GLuint)program,layout->locations[i],&unit);
        if(unit<0||unit>=c->texture_units)return 0;
        used[unit]=1;
    }
    return 1;
}
static int DGBackdropWritesSampledAtlas(struct DGHostContext *c)
{
    if(!c->alias_group || !c->alias_group->atlas_programs)return 0;
    GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    struct DGAtlasProgram *p=c->alias_group->atlas_programs;
    for(;p && p->name!=(GLuint)program;p=p->next){}
    if(!p)return 0;
    GLint framebuffer=0,kind=0,target=0,level=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&framebuffer);
    if(!framebuffer || (GLuint)framebuffer==c->framebuffer)return 0;
    glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&kind);
    if(kind!=GL_TEXTURE)return 0;
    glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT,&level);
    if(level)return 0;
    glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&target);
    GLint unit=0,active=0,binding=0;
    glGetUniformiv(p->name,p->sampler,&unit);
    if(unit<0 || unit>=c->texture_units)return 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glActiveTexture(GL_TEXTURE0+unit);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&binding);
    glActiveTexture((GLenum)active);
    return binding && binding==target;
}
static void DGPrepareDrawableAliases(struct DGHostContext *c,int unknown_list_bindings)
{
    struct DGAliasGroup *g=c->alias_group;
    if(c->inside_begin || !c->drawable || !c->drawable->alias_dirty || !c->color_storage || !g || !g->aliases)return;
    GLuint target=IOSurfaceGetID(c->color_storage),back=c->back_storage?IOSurfaceGetID(c->back_storage):0;
    int feedback=unknown_list_bindings;
    {
        if(c->alias_bindings_dirty) {
            GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            for(GLint unit=0;unit<c->texture_units;++unit) {
                GLint name=0;glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&name);
                c->rectangle_bindings[unit]=(GLuint)name;
            }
            glActiveTexture((GLenum)active);c->alias_bindings_dirty=0;c->alias_mappings_dirty=1;
        }
        if(c->alias_mappings_dirty || c->alias_generation!=g->generation) {
            memset(c->sampled_storage,0,(size_t)c->texture_units*2*sizeof(*c->sampled_storage));
            for(GLint unit=0;unit<c->texture_units;++unit)
                for(struct DGAlias *p=g->aliases;p;p=p->next)if(p->texture==c->rectangle_bindings[unit]) {
                    c->sampled_storage[unit]=p->source_storage?p->source_storage:p->storage;
                    c->sampled_textures[unit]=p->texture;break;
                }
            c->alias_mappings_dirty=0;c->alias_generation=g->generation;
        }
        for(GLint unit=0;unit<c->texture_units;++unit)
            if(c->sampled_storage[unit]==target || (back && c->sampled_storage[unit]==back)) { feedback=1;break; }
    }
    if(!feedback)return;
    unsigned char used[1024];int known=!unknown_list_bindings&&DGAliasSampledUnits(c,used);
    if(known){
        feedback=0;
        for(GLint unit=0;unit<c->texture_units;++unit)
            if(used[unit]&&(c->sampled_storage[unit]==target||(back&&c->sampled_storage[unit]==back))){feedback=1;break;}
        // Keep dirty: a later draw may actually sample this producer.
        if(!feedback)return;
    }
    DGRefreshFlippedAliases(c,!unknown_list_bindings);
    // Visibility belongs to the sampled producer storage, not the draw
    // destination. A separate FBO (for example a blur atlas) can consume the
    // same dirty drawable IOSurface just as a draw to the drawable can.
    /* A flipped view was copied into the SAME native texture that the next
     * draw samples. Ordinary GL command ordering already orders that blit;
     * it is not a second texture object importing the producer IOSurface.
     * Keep the explicit IOSurface Flush/rebind only for actual direct aliases
     * (or unknown display-list bindings). Otherwise each text quad submits
     * the whole producer queue for no cross-object visibility requirement. */
    int direct=unknown_list_bindings;
    for(GLint unit=0;!direct && unit<c->texture_units;++unit)
        if((!known||used[unit])&&(c->sampled_storage[unit]==target || (back && c->sampled_storage[unit]==back)))
            for(struct DGAlias *p=g->aliases;p;p=p->next)
                if(p->texture==c->sampled_textures[unit] && !p->owned_storage){direct=1;break;}
    // A known shader may use only an owned mirror while leaving a direct
    // alias for a later draw. Refreshing the mirror does not submit that
    // direct consumer's producer; retain its pending dependency.
    if(!direct){if(!known)c->drawable->alias_dirty=0;return;}
    glFlush();
    // CGLIOSurface.h explicitly requires consumer bind AFTER producer flush.
    // It is not enough that the alias happened to be bound before the writes.
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(GLint unit=0;unit<c->texture_units;++unit)
      if(c->sampled_storage[unit]==target || (back && c->sampled_storage[unit]==back)) {
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,c->sampled_textures[unit]);
    }
    glActiveTexture((GLenum)active);c->drawable->alias_dirty=0;
}
