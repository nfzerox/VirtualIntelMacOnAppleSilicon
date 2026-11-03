/* GLsync pointers never leave this executor. Names belong to the actual GL
 * share group and survive destruction of the original creating context. */
struct DGHostSync { uint32_t name;GLsync value;struct DGHostSync *next; };
static void DGDestroySyncs(struct DGAliasGroup *group)
{
    while(group->syncs) { struct DGHostSync *s=group->syncs;group->syncs=s->next;glDeleteSync(s->value);free(s); }
}
static int DGHostSyncCommand(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    struct DGAliasGroup *group=c->alias_group;
    if(!group || c->inside_begin || m->bytes!=(m->op==DG_SYNC_GET?sizeof(struct DGSyncQuery):0))return -1;
    struct DGHostSync **link=&group->syncs;
    while(*link && (*link)->name!=m->args[0])link=&(*link)->next;
    struct DGHostSync *sync=*link;
    if(m->op!=DG_SYNC_FENCE && !sync) {
        if(m->op!=DG_SYNC_IS)reply->error=GL_INVALID_VALUE;return 0;
    }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    switch(m->op) {
    case DG_SYNC_FENCE: {
        if(group->next_sync==UINT32_MAX) { reply->error=GL_OUT_OF_MEMORY;return 0; }
        struct DGHostSync *s=calloc(1,sizeof(*s));
        if(!s) { reply->error=GL_OUT_OF_MEMORY;return 0; }
        s->value=glFenceSync((GLenum)m->args[0],(GLbitfield)m->args[1]);
        if(!s->value) { free(s);reply->error=glGetError();if(!reply->error)reply->error=GL_OUT_OF_MEMORY;return 0; }
        /* Publish the producer checkpoint before returning its remote name.
         * A guest may wait from another shared context with flags=0. Its
         * flush cannot submit this context's host command queue, and an
         * unsubmitted fence must not hold the emulator's device lock forever.
         * Flush only submits work; it does not claim GPU completion. */
        glFlush();
        s->name=++group->next_sync;s->next=group->syncs;group->syncs=s;reply->result=s->name;break;
    }
    case DG_SYNC_IS:reply->result=glIsSync(sync->value);break;
    case DG_SYNC_DELETE:glDeleteSync(sync->value);*link=sync->next;free(sync);break;
    case DG_SYNC_CLIENT_WAIT:reply->result=glClientWaitSync(sync->value,(GLbitfield)m->args[1],m->args[2]);break;
    case DG_SYNC_WAIT:glWaitSync(sync->value,(GLbitfield)m->args[1],m->args[2]);break;
    case DG_SYNC_GET: {
        if(m->args[2]>1)return -1;
        struct DGSyncQuery *out=(void *)(m+1);
        glGetSynciv(sync->value,(GLenum)m->args[1],(GLsizei)m->args[2],&out->length,m->args[2]?&out->value:NULL);break;
    }
    default:return -1;
    }
    reply->error=glGetError();return 0;
}
