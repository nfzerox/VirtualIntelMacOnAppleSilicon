/* Measured Apple GL storage contracts used by CoreAnimation/WindowServer.
 * The transport is an indirect renderer: client texture bytes are decoded/copied,
 * not aliased across hosts. Storage/purgeability hints never fabricate loss.
 * APPLE row bytes are translated at each transfer, preserving ordinary GL
 * row length/alignment. Non-pixel-aligned pitches remain explicit errors. */
struct VIMAppleObject {
    GLenum type;GLuint name;int purgeable,responsible_pid;
    GLint hint,length;const void *pointer;
    int mipmap_sgis,mipmap_apple;
    struct VIMAppleObject *next;
};
static struct VIMAppleObject *VIMAppleObject(struct VIMShareGroup *g,GLenum type,GLuint name,int create)
{
    for(struct VIMAppleObject *o=g->objects;o;o=o->next) if(o->type==type && o->name==name) return o;
    if(!create) return NULL;
    struct VIMAppleObject *o=calloc(1,sizeof(*o));
    if(o) { o->type=type;o->name=name;o->hint=GL_STORAGE_PRIVATE_APPLE;o->next=g->objects;g->objects=o; }
    return o;
}
static void VIMDestroyAppleObjects(struct VIMShareGroup *g)
{
    while(g->objects) { struct VIMAppleObject *o=g->objects;g->objects=o->next;free(o); }
}
static void VIMForgetAppleObjects(GLIContext opaque,GLenum type,GLsizei n,const GLuint *names)
{
    if(!opaque || n<1 || !names) return;
    struct VIMShareGroup *g=((struct VIMContext *)opaque)->group;
    pthread_mutex_lock(&g->lock);
    struct VIMAppleObject **p=&g->objects;
    while(*p) {
        int remove=0;
        if((*p)->type==type) for(GLsizei i=0;i<n;++i) if((*p)->name==names[i]) { remove=1;break; }
        if(remove) { struct VIMAppleObject *o=*p;*p=o->next;free(o); } else p=&(*p)->next;
    }
    pthread_mutex_unlock(&g->lock);
}
static GLint *VIMAppleState(struct VIMContext *c,GLenum key)
{
    switch(key) {
    case GL_PACK_ROW_BYTES_APPLE:return &c->pack_row_bytes;
    case GL_UNPACK_ROW_BYTES_APPLE:return &c->unpack_row_bytes;
    case GL_UNPACK_CLIENT_STORAGE_APPLE:return &c->unpack_client_storage;
    case GL_TRANSFORM_HINT_APPLE:return &c->transform_hint;
    default:return NULL;
    }
}
static void VIMApplePixelStorei(GLIContext ctx,GLenum key,GLint value)
{
    struct VIMContext *c=(void *)ctx;
    GLint *state=VIMAppleState(c,key);
    if(state && key!=GL_TRANSFORM_HINT_APPLE) {
        if(key==GL_UNPACK_CLIENT_STORAGE_APPLE) *state=!!value;
        else if(value<0) c->error=GL_INVALID_VALUE;
        else *state=value;
        return;
    }
    VIMDispatch_pixel_storei(ctx,key,value);
}
static void VIMApplePixelStoref(GLIContext ctx,GLenum key,GLfloat value)
{
    if(VIMAppleState((void *)ctx,key) && key!=GL_TRANSFORM_HINT_APPLE)
        VIMApplePixelStorei(ctx,key,(GLint)roundf(value));
    else VIMDispatch_pixel_storef(ctx,key,value);
}
#define VIM_APPLE_GET(suffix,Type) \
static void VIMAppleGet##suffix(GLIContext ctx,GLenum key,Type *value) { \
    GLint *state=VIMAppleState((void *)ctx,key); \
    if(state) { if(value) { GLint actual=key==GL_TRANSFORM_HINT_APPLE && !*state ? GL_DONT_CARE : *state; \
        *value=sizeof(Type)==sizeof(GLboolean) ? (Type)!!actual : (Type)actual; } } \
    else VIMDispatch_get_##suffix(ctx,key,value); }
VIM_APPLE_GET(integerv,GLint)
VIM_APPLE_GET(floatv,GLfloat)
VIM_APPLE_GET(doublev,GLdouble)
VIM_APPLE_GET(booleanv,GLboolean)
#undef VIM_APPLE_GET
static void VIMAppleHint(GLIContext ctx,GLenum target,GLenum mode)
{
    if(target==GL_TRANSFORM_HINT_APPLE) {
        if(mode!=GL_DONT_CARE && mode!=GL_FASTEST && mode!=GL_NICEST)
            ((struct VIMContext *)ctx)->error=GL_INVALID_ENUM;
        else ((struct VIMContext *)ctx)->transform_hint=mode;
        return; // Keep accurate transformations even for FASTEST.
    }
    VIMDispatch_hint(ctx,target,mode);
}
static GLuint VIMAppleTexture(GLIContext ctx,GLenum target)
{
    GLenum binding=0;GLint name=0;
    switch(target) {
    case GL_TEXTURE_1D:binding=GL_TEXTURE_BINDING_1D;break;
    case GL_TEXTURE_2D:binding=GL_TEXTURE_BINDING_2D;break;
    case GL_TEXTURE_3D:binding=GL_TEXTURE_BINDING_3D;break;
    case GL_TEXTURE_RECTANGLE_ARB:binding=GL_TEXTURE_BINDING_RECTANGLE_ARB;break;
    case GL_TEXTURE_CUBE_MAP:binding=GL_TEXTURE_BINDING_CUBE_MAP;break;
    default:((struct VIMContext *)ctx)->error=GL_INVALID_ENUM;return 0;
    }
    if(VIMBindGLI(ctx)) glGetIntegerv(binding,&name);
    return (GLuint)name;
}
static int VIMAppleTexParameter(GLIContext ctx,GLenum target,GLenum key,GLint value)
{
    /* Stock 10.9 glTexParameterI_Exec stores 0x8a4b as a boolean mutually
     * exclusive with GENERATE_MIPMAP, and glTexImage2D_Exec invokes
     * gleGenMipmaps for either bit. CoreGraphics enables it for tiled window
     * textures with mipmapped filters. The backend must generate the chain, not
     * merely ignore the private flag (which leaves the texture incomplete). */
    const GLenum apple_mipmap=0x8a4b;
    if(key!=GL_TEXTURE_STORAGE_HINT_APPLE && key!=apple_mipmap && key!=GL_GENERATE_MIPMAP) return 0;
    struct VIMContext *c=(void *)ctx;
    GLuint name=VIMAppleTexture(ctx,target);
    if(!name) { c->error=GL_INVALID_OPERATION;return 1; }
    if(key==GL_TEXTURE_STORAGE_HINT_APPLE &&
       value!=GL_STORAGE_PRIVATE_APPLE && value!=GL_STORAGE_CACHED_APPLE && value!=GL_STORAGE_SHARED_APPLE)
        { c->error=GL_INVALID_ENUM;return 1; }
    if(key!=GL_TEXTURE_STORAGE_HINT_APPLE && value!=0 && value!=1)
        { c->error=GL_INVALID_VALUE;return 1; }
    pthread_mutex_lock(&c->group->lock);
    struct VIMAppleObject *o=VIMAppleObject(c->group,GL_TEXTURE,name,1);
    int generate=-1;
    if(!o) c->error=GL_OUT_OF_MEMORY;
    else if(key==GL_TEXTURE_STORAGE_HINT_APPLE) o->hint=value;
    else if(value && (key==apple_mipmap ? o->mipmap_sgis : o->mipmap_apple)) c->error=GL_INVALID_OPERATION;
    else {
        if(key==apple_mipmap) o->mipmap_apple=value;else o->mipmap_sgis=value;
        generate=o->mipmap_sgis || o->mipmap_apple;
    }
    pthread_mutex_unlock(&c->group->lock);
    if(generate>=0) VIMDispatch_tex_parameteri(ctx,target,GL_GENERATE_MIPMAP,generate);
    return 1;
}
static void VIMAppleTexParameteri(GLIContext c,GLenum t,GLenum p,GLint v)
{ if(!VIMAppleTexParameter(c,t,p,v)) VIMDispatch_tex_parameteri(c,t,p,v); }
static void VIMAppleTexParameterf(GLIContext c,GLenum t,GLenum p,GLfloat v)
{ if(!VIMAppleTexParameter(c,t,p,(GLint)v)) VIMDispatch_tex_parameterf(c,t,p,v); }
static void VIMAppleTexParameteriv(GLIContext c,GLenum t,GLenum p,const GLint *v)
{ if(!v || !VIMAppleTexParameter(c,t,p,*v)) VIMDispatch_tex_parameteriv(c,t,p,v); }
static void VIMAppleTexParameterfv(GLIContext c,GLenum t,GLenum p,const GLfloat *v)
{ if(!v || !VIMAppleTexParameter(c,t,p,(GLint)*v)) VIMDispatch_tex_parameterfv(c,t,p,v); }
static int VIMAppleTexQuery(GLIContext ctx,GLenum target,GLenum key,GLint *value)
{
    if(key!=0x8a4b && key!=GL_GENERATE_MIPMAP && key!=GL_TEXTURE_STORAGE_HINT_APPLE) return 0;
    struct VIMContext *c=(void *)ctx;
    GLuint name=VIMAppleTexture(ctx,target);
    pthread_mutex_lock(&c->group->lock);
    struct VIMAppleObject *o=VIMAppleObject(c->group,GL_TEXTURE,name,0);
    if(value) *value=key==GL_TEXTURE_STORAGE_HINT_APPLE ? (o ? o->hint : GL_STORAGE_PRIVATE_APPLE) :
        o ? (key==0x8a4b ? o->mipmap_apple : o->mipmap_sgis) : 0;
    pthread_mutex_unlock(&c->group->lock);
    return 1;
}
static void VIMAppleGetTexParameteriv(GLIContext c,GLenum t,GLenum p,GLint *v)
{ if(!VIMAppleTexQuery(c,t,p,v)) VIMDispatch_get_tex_parameteriv(c,t,p,v); }
static void VIMAppleGetTexParameterfv(GLIContext c,GLenum t,GLenum p,GLfloat *v)
{ GLint i;if(VIMAppleTexQuery(c,t,p,&i)) { if(v) *v=(GLfloat)i; } else VIMDispatch_get_tex_parameterfv(c,t,p,v); }
static int VIMPixelBytes(GLenum format,GLenum type)
{
    if(type==GL_UNSIGNED_SHORT_8_8_APPLE || type==GL_UNSIGNED_SHORT_8_8_REV_APPLE)
        return format==GL_YCBCR_422_APPLE ? 2 : 0;
    switch(type) {
    case GL_UNSIGNED_INT_8_8_8_8:case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_24_8:return 4;
    case GL_UNSIGNED_SHORT_5_6_5:case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:case GL_UNSIGNED_SHORT_1_5_5_5_REV:return 2;
    }
    int size=0,components=0;
    switch(type) {
    case GL_BYTE:case GL_UNSIGNED_BYTE:size=1;break;
    case GL_SHORT:case GL_UNSIGNED_SHORT:case GL_HALF_FLOAT_ARB:size=2;break;
    case GL_INT:case GL_UNSIGNED_INT:case GL_FLOAT:size=4;break;
    default:return 0;
    }
    switch(format) {
    case GL_RED:case GL_GREEN:case GL_BLUE:case GL_ALPHA:case GL_LUMINANCE:
    case GL_DEPTH_COMPONENT:case GL_STENCIL_INDEX:components=1;break;
    case GL_LUMINANCE_ALPHA:case GL_RG:components=2;break;
    case GL_RGB:case GL_BGR:components=3;break;
    case GL_RGBA:case GL_BGRA:components=4;break;
    default:return 0;
    }
    return size*components;
}
static int VIMPreparePixelTransfer(GLIContext ctx,GLenum format,GLenum type,int pack,struct VIMPixelTransfer *s)
{
    struct VIMContext *c=(void *)ctx;
    memset(s,0,sizeof(*s));s->pack=pack;
    GLint bytes=pack ? c->pack_row_bytes : c->unpack_row_bytes;
    if(!bytes) return 1;
    int pixel=VIMPixelBytes(format,type);
    if(!pixel || bytes%pixel) { c->error=GL_INVALID_OPERATION;return 0; }
    glGetIntegerv(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,&s->length);
    glGetIntegerv(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,&s->alignment);
    glPixelStorei(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,bytes/pixel);
    glPixelStorei(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,1);
    s->changed=1;return 1;
}
static void VIMRestorePixelTransfer(const struct VIMPixelTransfer *s)
{
    if(!s->changed) return;
    glPixelStorei(s->pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,s->length);
    glPixelStorei(s->pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,s->alignment);
}
static GLenum VIMApplePurge(GLIContext ctx,GLenum type,GLuint name,GLenum option,int purge)
{
    struct VIMContext *c=(void *)ctx;
    if(!VIMBindGLI(ctx)) return 0;
    if(!name) { c->error=GL_INVALID_VALUE;return 0; }
    GLboolean valid;
    switch(type) {
    case GL_TEXTURE:valid=glIsTexture(name);break;
    case GL_BUFFER_OBJECT_APPLE:valid=glIsBuffer(name);break;
    case GL_RENDERBUFFER_EXT:valid=glIsRenderbufferEXT(name);break;
    default:c->error=GL_INVALID_ENUM;return 0;
    }
    if(!valid) { c->error=GL_INVALID_OPERATION;return 0; }
    if((purge && option!=GL_VOLATILE_APPLE && option!=GL_RELEASED_APPLE) ||
       (!purge && option!=GL_RETAINED_APPLE && option!=GL_UNDEFINED_APPLE))
        { c->error=GL_INVALID_ENUM;return 0; }
    pthread_mutex_lock(&c->group->lock);
    struct VIMAppleObject *o=VIMAppleObject(c->group,type,name,1);
    GLenum result=0;
    if(!o) c->error=GL_OUT_OF_MEMORY;
    else if(o->purgeable==purge) c->error=GL_INVALID_OPERATION;
    else { o->purgeable=purge;result=purge ? GL_VOLATILE_APPLE : option; }
    pthread_mutex_unlock(&c->group->lock);
    return result; // Retain storage: never report RELEASED/undefined data loss.
}
static GLenum VIMApplePurgeable(GLIContext c,GLenum t,GLuint n,GLenum o)
{ return VIMApplePurge(c,t,n,o,1); }
static GLenum VIMAppleUnpurgeable(GLIContext c,GLenum t,GLuint n,GLenum o)
{ return VIMApplePurge(c,t,n,o,0); }
static void VIMAppleResponsible(GLIContext ctx,GLenum type,GLuint name,GLint pid)
{
    // The private ABI labels ownership for accounting, not rendering. Retain
    // that label with the share-group object; it grants no task access and
    // does not imply host GPU memory is charged to an emulated guest PID.
    struct VIMContext *c=(void *)ctx;
    if(!VIMBindGLI(ctx)) return;
    GLboolean valid=GL_FALSE;
    switch(type) {
    case GL_TEXTURE:valid=glIsTexture(name);break;
    // The label ABI uses EXT's object namespace, not APPLE purgeability's
    // GL_BUFFER_OBJECT_APPLE. Native gleGetHashMachineFromType confirms it.
    case GL_BUFFER_OBJECT_EXT:valid=glIsBuffer(name);type=GL_BUFFER_OBJECT_APPLE;break;
    case GL_FRAMEBUFFER_EXT:valid=glIsFramebufferEXT(name);break;
    case GL_RENDERBUFFER_EXT:valid=glIsRenderbufferEXT(name);break;
    case GL_SHADER_OBJECT_EXT:valid=glIsShader(name);break;
    case GL_PROGRAM_OBJECT_EXT:valid=glIsProgram(name);break;
    case GL_QUERY_OBJECT_EXT:valid=glIsQuery(name);break;
    case GL_VERTEX_ARRAY_OBJECT_EXT:valid=glIsVertexArrayAPPLE(name);break;
    default:if(!c->error) c->error=GL_INVALID_ENUM;return;
    }
    // Live 13F1911 NVIDIA control: generated-but-unbound, missing and deleted
    // names are INVALID_VALUE (501), not INVALID_OPERATION (502).
    if(!valid) { if(!c->error) c->error=GL_INVALID_VALUE;return; }
    pthread_mutex_lock(&c->group->lock);
    struct VIMAppleObject *o=VIMAppleObject(c->group,type,name,1);
    if(o) o->responsible_pid=pid;else c->error=GL_OUT_OF_MEMORY;
    pthread_mutex_unlock(&c->group->lock);
}
static void VIMInstallAppleTextureParameters(GLIFunctionDispatch *d)
{
    /* The private 10.9 automatic-mipmap bit is not a portable host-driver
     * contract. Preserve its state and request real SGIS mipmap generation. */
    d->tex_parameteri=VIMAppleTexParameteri;d->tex_parameterf=VIMAppleTexParameterf;
    d->tex_parameteriv=VIMAppleTexParameteriv;d->tex_parameterfv=VIMAppleTexParameterfv;
    d->get_tex_parameteriv=VIMAppleGetTexParameteriv;d->get_tex_parameterfv=VIMAppleGetTexParameterfv;
}
static void VIMInstallAppleStorage(GLIFunctionDispatch *d)
{
    d->pixel_storei=VIMApplePixelStorei;d->pixel_storef=VIMApplePixelStoref;
    d->get_integerv=VIMAppleGetintegerv;d->get_booleanv=VIMAppleGetbooleanv;
    d->get_floatv=VIMAppleGetfloatv;d->get_doublev=VIMAppleGetdoublev;
    d->hint=VIMAppleHint;
    VIMInstallAppleTextureParameters(d);
#if VIM_GLI_DISPATCH_COUNT >= 757
    /* Native Tiger's measured 686-slot table predates these entry points. */
    d->object_purgeable_APPLE=VIMApplePurgeable;d->object_unpurgeable_APPLE=VIMAppleUnpurgeable;
#endif
#if VIM_GLI_HAS_RESPONSIBLE_PROCESS
    d->label_object_with_responsible_process_APPLE=VIMAppleResponsible;
#endif
}
