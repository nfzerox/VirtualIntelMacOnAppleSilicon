/* Research only. Sample one caller-selected display pixel after actual
 * draws in the native executor. Readback can change ordering: compare to
 * the uninstrumented live display and never use for performance claims. */
static void DGTraceBannerAtlas(struct DGHostContext *c,GLint draw,const GLint viewport[4],uint32_t op)
{
    static unsigned atlas_samples,banner_samples,mask_samples;
    int width=viewport[2],height=viewport[3];
    int banner=c->drawable->width==648&&c->drawable->height==126&&(GLuint)draw==c->framebuffer;
    int mask=(width==342||width==398)&&height==64;
    if(banner){width=648;height=126;}
    else if(viewport[0]||viewport[1]||(!mask&&
        !((width==584&&height==69)||(width==716&&height==194))))return;
    unsigned *samples=banner?&banner_samples:mask?&mask_samples:&atlas_samples;
    if(*samples>=(banner||mask?24u:40u)||!draw)return;
    const char *folder=getenv("VIM_PASSTHROUGH_OPENGL_TRACE_DIR");
    if(!folder||!getenv("VIM_GL_BANNER_ATLAS"))return;
    GLint object=0,program=0;
    glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&object);
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    if(!object)return;
    size_t stride=(size_t)width*4;
    unsigned char *rgba=malloc(stride*(size_t)height);if(!rgba)return;
    GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;
    GLint read=0,buffer=0,pack=0,values[6];
    const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,
        GL_PACK_SKIP_ROWS,GL_PACK_ROW_BYTES_APPLE,GL_PACK_SWAP_BYTES};
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i){glGetIntegerv(keys[i],values+i);glPixelStorei(keys[i],i?0:1);}
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)draw);
    glGetIntegerv(GL_READ_BUFFER,&buffer);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    GLenum issue=glGetError();
    glReadBuffer((GLenum)buffer);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)glPixelStorei(keys[i],values[i]);
    DGRestoreColorTransfer(&transfer);
    char path[PATH_MAX];
    if(!issue&&snprintf(path,sizeof(path),"%s/%s-%02u-%dx%d-d%u-f%d-p%d-op%x-t%d.pam",
        folder,banner?"banner":mask?"mask":"atlas",(*samples)++,width,height,c->drawable->name,draw,program,op,object)<(int)sizeof(path)){
        FILE *file=fopen(path,"wb");
        if(file){
            fprintf(file,"P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",width,height);
            for(int row=height-1;row>=0;--row)fwrite(rgba+stride*row,1,stride,file);
            fclose(file);
        }
        if(banner) {
            GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            fprintf(stderr,"Banner output %u drawable=%u fbo=%d program=%d op=%x blend=%u context=%p",
                *samples-1,c->drawable->name,draw,program,op,glIsEnabled(GL_BLEND),(void *)c);
            for(GLint unit=0;unit<4&&unit<c->texture_units;++unit){
                GLint rect=0,normal=0;glActiveTexture(GL_TEXTURE0+unit);
                glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&rect);glGetIntegerv(GL_TEXTURE_BINDING_2D,&normal);
                fprintf(stderr," unit%d=%d/%d",unit,rect,normal);
            }
            glActiveTexture((GLenum)active);fputc('\n',stderr);
        }
    }
    free(rgba);
}

static void DGTraceDrawWitnessLocked(struct DGHostContext *c,struct DGMessage *m)
{
    static int initialized,width,height,x,y,armed;
    static unsigned remaining=384,changed,candidates;
    static char trigger[PATH_MAX];
    static uint32_t last;
    static GLuint programs[16];
    static unsigned program_count;
    if(!remaining)return;
    if(!initialized){
        initialized=1;
        const char *region=getenv("VIM_GL_DRAW_WITNESS");
        const char *folder=getenv("VIM_PASSTHROUGH_OPENGL_TRACE_DIR");
        if(!region||!folder||sscanf(region,"%d,%d,%d,%d",&width,&height,&x,&y)!=4||
           x<0||y<0||x>=width||y>=height||
           snprintf(trigger,sizeof(trigger),"%s/arm",folder)>=(int)sizeof(trigger))
            remaining=0;
    }
    if(!remaining)return;
    if(!armed){
        if(access(trigger,F_OK))return;
        armed=1;
        fprintf(stderr,"Draw witness armed: %dx%d at %d,%d\n",width,height,x,y);
    }
    GLint draw=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    GLint viewport[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);
    DGTraceBannerAtlas(c,draw,viewport,m->op);
    if(m->op==DG_FLUSH_DRAWABLE_STATE||m->op==DG_FLUSH_IOSURFACE_STATE)return;
    if(candidates<12){
        ++candidates;
        fprintf(stderr,"Draw witness candidate %u drawable=%u size=%dx%d viewport=%d,%d,%d,%d fbo=%d/%u op=%u\n",
            candidates,c->drawable->name,c->drawable->width,c->drawable->height,
            viewport[0],viewport[1],viewport[2],viewport[3],draw,c->framebuffer,m->op);
    }
    if(c->drawable->width!=width||c->drawable->height!=height)return;
    if((GLuint)draw!=c->framebuffer)return;
    GLenum oldError=glGetError();if(oldError&&!c->error)c->error=oldError;
    GLint read=0,buffer=0,pack=0,values[6];
    const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,
        GL_PACK_ROW_BYTES_APPLE,GL_PACK_SWAP_BYTES};
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i){glGetIntegerv(keys[i],values+i);glPixelStorei(keys[i],i?0:1);}
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)draw);glGetIntegerv(GL_READ_BUFFER,&buffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    uint32_t pixel=0;
    glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&pixel);
    GLenum issue=glGetError();
    glReadBuffer((GLenum)buffer);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)glPixelStorei(keys[i],values[i]);
    DGRestoreColorTransfer(&transfer);
    if(issue){fprintf(stderr,"Draw witness read failed %x\n",issue);remaining=0;return;}
    if(remaining==384||pixel!=last){
        GLint program=0;
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        GLint src=0,dst=0,mode=0;
        glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&mode);
        fprintf(stderr,"Draw witness %u context=%p drawable=%u op=%u program=%d rgba=%08x blend=%u:%x:%x:%x\n",
            384-remaining,(void *)c,c->drawable->name,m->op,program,pixel,
            glIsEnabled(GL_BLEND),src,dst,mode);
        unsigned i=0;
        while(i<program_count&&programs[i]!=(GLuint)program)++i;
        if(program && i==program_count && program_count<16) {
            programs[program_count++]=(GLuint)program;
            GLuint shaders[4];GLsizei count=0;
            glGetAttachedShaders((GLuint)program,4,&count,shaders);
            for(GLsizei j=0;j<count;++j){
                GLint type=0;char source[8192]={0};
                glGetShaderiv(shaders[j],GL_SHADER_TYPE,&type);
                glGetShaderSource(shaders[j],sizeof(source),NULL,source);
                fprintf(stderr,"Draw witness shader %d:%x\n%s\n",program,type,source);
            }
        }
        last=pixel;++changed;
    }
    if(!--remaining)fprintf(stderr,"Draw witness done: %u changes\n",changed);
}
static void DGTraceDrawWitness(struct DGHostContext *c,struct DGMessage *m)
{
    static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    // The in-process executor can serve distinct graphics contexts on
    // different connections. Keep the diagnostic state serialized.
    if(!c||!c->drawable||c->inside_begin||c->profile!=kCGLOGLPVersion_Legacy||
       (m->op!=DG_DRAW&&m->op!=DG_DRAW_INSTANCED_ARRAYS&&m->op!=74&&
        ((c->drawable->width!=648||c->drawable->height!=126)||
         (m->op!=DG_FLUSH_DRAWABLE_STATE&&m->op!=DG_FLUSH_IOSURFACE_STATE))))return;
    pthread_mutex_lock(&lock);
    DGTraceDrawWitnessLocked(c,m);
    pthread_mutex_unlock(&lock);
}
