/* Typed state reads share one ordered native snapshot. No local
 * matrix math, speculative setter shadowing, application names or GLI offsets.
 * Audited mutations invalidate only affected fields; unknown/private packets
 * invalidate the complete snapshot. No setter result is predicted locally.
 * Unrelated values survive without suppressing any GL call/error. Begin/end remain blocked
 * conservatively to preserve native query error behavior.
 */
static volatile int dg_common_state_reuse=1; // Owned-process diagnostic toggle.
static volatile uint64_t dg_common_state_hits,dg_common_state_fills;
static volatile int dg_error_state_prefetch=1; // Owned-process A/B only.
static volatile uint64_t dg_common_state_prefetches;
static int DGCommonStateGet(struct DGContext *c,GLenum key,void *out,int floating)
{
    if(!c || !out || !dg_common_state_reuse || c->connection->dead ||
       c->common_state_blocked || (c->profile!=0x1000 && !(c->connection->features&DG_FEATURE_CORE_COMMON_STATE)) ||
       !(c->connection->capabilities&DG_CAP_COMMON_STATE))return 0;
    size_t offset=0,bytes=0;unsigned field=0;
    if(floating) {
        if(c->profile!=0x1000)return 0; // Removed matrix queries retain native errors.
        if(key==GL_MODELVIEW_MATRIX){offset=offsetof(struct DGCommonState,modelview);bytes=16*sizeof(GLfloat);field=DG_COMMON_MODELVIEW;}
        else if(key==GL_PROJECTION_MATRIX){offset=offsetof(struct DGCommonState,projection);bytes=16*sizeof(GLfloat);field=DG_COMMON_PROJECTION;}
    } else {
        if(key==GL_VIEWPORT){offset=offsetof(struct DGCommonState,viewport);bytes=4*sizeof(GLint);field=DG_COMMON_VIEWPORT;}
        else if(key==GL_DRAW_FRAMEBUFFER_BINDING_EXT){offset=offsetof(struct DGCommonState,draw_framebuffer);bytes=sizeof(GLint);field=DG_COMMON_FRAMEBUFFER;}
        else if(key==GL_DRAW_BUFFER){offset=offsetof(struct DGCommonState,draw_buffer);bytes=sizeof(GLint);field=DG_COMMON_DRAW_BUFFER;}
        else if(key==GL_TEXTURE_BINDING_RECTANGLE_ARB){offset=offsetof(struct DGCommonState,rectangle_binding);bytes=sizeof(GLint);field=DG_COMMON_RECTANGLE;}
    }
    if(!bytes)return 0;
    if(!(c->common_state_valid&field)){
        struct DGPacket p=DGPacketNew(c,DG_COMMON_STATE);p.message.flags=DG_OUTPUT_ONLY;
        DGPacketPointer(&p,0,&c->common_state,sizeof(c->common_state),1);
        DGSubmit(c,&p,1);
        if(p.valid_reply){DGPacketCopy(&p,0,&c->common_state,sizeof(c->common_state));
            c->common_state_valid=c->common_state.version==1 ? (c->profile==0x1000?DG_COMMON_ALL:DG_COMMON_CORE) : 0;
            if(!c->common_state_valid)c->error=GL_INVALID_OPERATION;}
        DGPacketFree(&p);
        // The negotiated snapshot had a real error. Do not submit a second
        // query or overwrite the caller's buffer with unused reply bytes.
        if(!c->common_state_valid)return 1;
        __sync_fetch_and_add(&dg_common_state_fills,1);
    } else __sync_fetch_and_add(&dg_common_state_hits,1);
    memcpy(out,(const char *)&c->common_state+offset,bytes);return 1;
}
