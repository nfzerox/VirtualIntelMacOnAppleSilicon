/* Default framebuffer semantics at the ordered GL dispatch boundary.
 * Ordinary draws/clears do not allocate or enter the accumulation engine.
 * Display-list accumulation is deliberately not executed at compile time;
 * retaining/replaying those operations is still a separate interface gap.
 */
static void DGHostAccumulation(struct DGHostContext *c,GLenum operation,GLfloat value,int clearing)
{
    if(c->inside_begin || c->list_mode || !c->accumulation_bits || !c->drawable){
        if(!c->error)c->error=GL_INVALID_OPERATION;return;
    }
    if(operation!=GL_LOAD && operation!=GL_ACCUM && operation!=GL_MULT &&
       operation!=GL_ADD && operation!=GL_RETURN && !(clearing && operation==DG_ACCUMULATION_CLEAR)){
        if(!c->error)c->error=GL_INVALID_ENUM;return;
    }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint draw=0,read=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    if((GLuint)draw!=c->framebuffer){if(!c->error)c->error=GL_INVALID_OPERATION;return;}
    // EXT_framebuffer_blit4.2.4 requires identical READ/DRAW bindings for
    // every Accum operation, not only the operations which read color.
    if(!clearing){glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
        if(read!=draw){if(!c->error)c->error=GL_INVALID_OPERATION;return;}}
    GLuint source=0;
    if(operation==GL_LOAD || operation==GL_ACCUM){
        GLint attachment=0;glGetIntegerv(GL_READ_BUFFER,&attachment);
        if(attachment==GL_COLOR_ATTACHMENT0_EXT)source=c->color;
        else if(attachment==GL_COLOR_ATTACHMENT1_EXT && c->double_buffer)source=c->back;
        if(!source){if(!c->error)c->error=GL_INVALID_OPERATION;return;}
    }
    struct DGAccumulationTarget targets[2];unsigned target_count=0;
    if(operation==GL_RETURN){
        if(!DGDrawableWritable(c))return;
        if(!c->max_draw_buffers)glGetIntegerv(GL_MAX_DRAW_BUFFERS_ARB,&c->max_draw_buffers);
        for(GLint i=0;i<c->max_draw_buffers;++i){
            GLint attachment=0;glGetIntegerv(GL_DRAW_BUFFER0_ARB+i,&attachment);
            if(attachment==GL_NONE)continue;
            GLuint texture=attachment==GL_COLOR_ATTACHMENT0_EXT?c->color:
                attachment==GL_COLOR_ATTACHMENT1_EXT?c->back:0;
            if(!texture || target_count==2){if(!c->error)c->error=GL_INVALID_OPERATION;return;}
            targets[target_count].texture=texture;
            glGetBooleanIndexedvEXT(GL_COLOR_WRITEMASK,(GLuint)i,targets[target_count].mask);++target_count;
        }
    }
    GLint clip[4];GLint *scissor=NULL;GLfloat clear[4]={0};
    if(glIsEnabled(GL_SCISSOR_TEST)){glGetIntegerv(GL_SCISSOR_BOX,clip);scissor=clip;}
    if(operation==DG_ACCUMULATION_CLEAR)glGetFloatv(GL_ACCUM_CLEAR_VALUE,clear);
    GLboolean dither=glIsEnabled(GL_DITHER);
    GLenum failed=DGAccumulationExecute(&c->accumulation,&c->drawable->accumulation,
        c->drawable->width,c->drawable->height,operation,value,clear,scissor,source,targets,target_count,dither);
    if(failed && !c->error)c->error=failed;
    if(operation==GL_RETURN && !failed){c->iosurface_draw_dirty=1;DGMarkDrawableDamage(c);}
}
static void DGHostClear(struct DGHostContext *c,GLbitfield mask)
{
    if(mask&GL_ACCUM_BUFFER_BIT && !c->inside_begin &&
       !(mask&~(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT|GL_ACCUM_BUFFER_BIT))){
        GLint draw=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
        if(c->framebuffer && (GLuint)draw==c->framebuffer){
            if(c->accumulation_bits)DGHostAccumulation(c,DG_ACCUMULATION_CLEAR,0,1);
            mask&=~GL_ACCUM_BUFFER_BIT;
        }
    }
    glClear(mask);
    if(mask&GL_COLOR_BUFFER_BIT)DGMarkDrawableDamage(c);
}
