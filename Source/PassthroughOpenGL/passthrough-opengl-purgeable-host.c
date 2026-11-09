/* Native object storage, not guest bookkeeping. Never pass a name of an
 * executor-owned drawable attachment to a guest purgeability call. */
static int DGPurgeablePrivateName(struct DGHostContext *c,GLenum type,GLuint name)
{
    if(!name || !c->alias_group)return 0;
    if(type==GL_TEXTURE)return DGPrivateTextureContains(c->alias_group->private_textures,name);
    if(type!=GL_RENDERBUFFER_EXT)return 0;
    for(struct DGHostContext *member=c->alias_group->members;member;member=member->alias_member_next){
        if(member->depth==name)return 1;
        for(struct DGHostDrawable *d=member->drawables;d;d=d->next)
            if(d->depth==name)return 1;
    }
    return 0;
}
/* The Metal-backed implementation can abort inside gldReclaimBuffer
 * even though some explicit calls succeed. Only use advertised profiles. */
static int DGHostPurgeableSupported(struct DGHostContext *c)
{
    if(c->inside_begin)return 0;
    if(!c->purgeability){
        int advertised=c->profile==kCGLOGLPVersion_Legacy?
            DGHostHasExtension((const char *)glGetString(GL_EXTENSIONS),"GL_APPLE_object_purgeable"):
            !!DGHostCoreExtensionMask(DG_CORE_EXT_OBJECT_PURGEABLE);
        c->purgeability=advertised?1:-1;
    }
    return c->purgeability>0;
}
