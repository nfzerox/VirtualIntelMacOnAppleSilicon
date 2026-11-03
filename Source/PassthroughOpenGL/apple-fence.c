/* Context-local APPLE named fences backed by real ARB_sync GPU checkpoints.
 * Do not advertise the complete extension yet: display-list compilation and
 * object-specific TestObject/FinishObject still need their own adapters.
 * WindowServer's immediate fence operations use these six typed GLI entries. */
struct VIMAppleFence {
    GLuint name;
    GLsync sync;
    struct VIMAppleFence *next;
};
static struct VIMAppleFence *VIMFindAppleFence(struct VIMContext *c,GLuint name)
{
    for(struct VIMAppleFence *f=c->fences;f;f=f->next) if(f->name==name) return f;
    return NULL;
}
static void VIMGenAppleFences(GLIContext opaque,GLsizei n,GLuint *names)
{
    struct VIMContext *c=(void *)opaque;
    if(n<0 || (n && !names)) { c->error=GL_INVALID_VALUE;return; }
    for(GLsizei i=0;i<n;++i) {
        struct VIMAppleFence *f=calloc(1,sizeof(*f));
        if(!f) { c->error=GL_OUT_OF_MEMORY;return; }
        do { if(!++c->next_fence) ++c->next_fence; } while(VIMFindAppleFence(c,c->next_fence));
        f->name=c->next_fence;f->next=c->fences;c->fences=f;names[i]=f->name;
    }
}
static void VIMDeleteAppleFences(GLIContext opaque,GLsizei n,const GLuint *names)
{
    struct VIMContext *c=(void *)opaque;
    if(n<0 || (n && !names)) { c->error=GL_INVALID_VALUE;return; }
    if(!VIMBindGLI(opaque)) return;
    for(GLsizei i=0;i<n;++i) {
        struct VIMAppleFence **link=&c->fences;
        while(*link && (*link)->name!=names[i]) link=&(*link)->next;
        if(*link) {
            struct VIMAppleFence *f=*link;*link=f->next;
            if(f->sync) glDeleteSync(f->sync);
            free(f);
        }
    }
}
static void VIMSetAppleFence(GLIContext opaque,GLuint name)
{
    struct VIMContext *c=(void *)opaque;
    struct VIMAppleFence *f=VIMFindAppleFence(c,name);
    if(!f) { c->error=GL_INVALID_OPERATION;return; }
    if(!VIMBindGLI(opaque)) return;
    GLsync sync=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!sync) { c->error=GL_OUT_OF_MEMORY;return; }
    if(f->sync) glDeleteSync(f->sync);
    f->sync=sync;
}
static GLboolean VIMIsAppleFence(GLIContext opaque,GLuint name)
{
    struct VIMAppleFence *f=VIMFindAppleFence((void *)opaque,name);
    return f && f->sync ? GL_TRUE : GL_FALSE;
}
static GLboolean VIMWaitAppleFence(GLIContext opaque,GLuint name,int wait)
{
    struct VIMContext *c=(void *)opaque;
    struct VIMAppleFence *f=VIMFindAppleFence(c,name);
    if(!f) { c->error=GL_INVALID_OPERATION;return GL_TRUE; }
    if(!f->sync) return GL_TRUE; // Generated but not set is already completed.
    if(!VIMBindGLI(opaque)) return GL_FALSE;
    GLenum result=glClientWaitSync(f->sync,GL_SYNC_FLUSH_COMMANDS_BIT,wait ? GL_TIMEOUT_IGNORED : 0);
    if(result==GL_WAIT_FAILED) { c->error=GL_INVALID_OPERATION;return GL_TRUE; }
    return result==GL_ALREADY_SIGNALED || result==GL_CONDITION_SATISFIED;
}
static GLboolean VIMTestAppleFence(GLIContext opaque,GLuint name)
{ return VIMWaitAppleFence(opaque,name,0); }
static void VIMFinishAppleFence(GLIContext opaque,GLuint name)
{ (void)VIMWaitAppleFence(opaque,name,1); }
static void VIMInstallAppleFences(GLIFunctionDispatch *d)
{
    d->gen_fences_APPLE=VIMGenAppleFences;d->delete_fences_APPLE=VIMDeleteAppleFences;
    d->set_fence_APPLE=VIMSetAppleFence;d->is_fence_APPLE=VIMIsAppleFence;
    d->test_fence_APPLE=VIMTestAppleFence;d->finish_fence_APPLE=VIMFinishAppleFence;
}
static void VIMDestroyAppleFences(struct VIMContext *c)
{
    if(!c->fences) return;
    struct VIMSavedBinding previous=VIMSaveGLI((GLIContext)c);
    VIMBindGLI((GLIContext)c);
    while(c->fences) { GLuint name=c->fences->name;VIMDeleteAppleFences((GLIContext)c,1,&name); }
    VIMRestoreGLI(previous,(GLIContext)c);
}
