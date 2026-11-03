/* Opt-in, bounded renderer-state evidence. Never enabled in timing runs.
 * Pixel captures require a separate explicit diagnostics flag. */
static void DGTraceImmediateResult(struct DGHostContext *c)
{
    if(!dg_diagnostics || c->inside_begin || c->traced_immediate_program<=0 ||
       !getenv("VIM_PASSTHROUGH_OPENGL_TRACE_PIXELS") || !c->drawable)return;
    int program=c->traced_immediate_program;c->traced_immediate_program=-program;
    const char *folder=getenv("VIM_PASSTHROUGH_OPENGL_TRACE_DIR");if(!folder)return;
    GLint width=c->drawable->width,height=c->drawable->height;
    if(width<1 || height<1 || (uint64_t)width*height>4*1024*1024)return;
    unsigned char *pixels=malloc((size_t)width*height*4);if(!pixels)return;
    GLint read=0,buffer=0,pack=0,draw=0;
    const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,
        GL_PACK_ROW_BYTES_APPLE,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};
    GLint values[sizeof(keys)/sizeof(*keys)];
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i){glGetIntegerv(keys[i],values+i);glPixelStorei(keys[i],i?0:1);}
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)draw);glGetIntegerv(GL_READ_BUFFER,&buffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();
    if(!error){
        char path[2048];
        if(snprintf(path,sizeof(path),"%s/after-%d-%p-%d.ppm",folder,getpid(),(void *)c,program)<(int)sizeof(path)){
            FILE *file=fopen(path,"wb");
            if(file){fprintf(file,"P6\n%d %d\n255\n",width,height);
                for(int row=height-1;row>=0;--row)for(int x=0;x<width;++x)
                    fwrite(pixels+4*((size_t)row*width+x),1,3,file);
                fclose(file);
            }
        }
    }
    glReadBuffer((GLenum)buffer);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)glPixelStorei(keys[i],values[i]);
    DGRestoreColorTransfer(&transfer);free(pixels);
}
static void DGTraceAssemblyTexture(struct DGHostContext *c,GLenum target,GLint texture,
    GLint width,GLint height,unsigned unit,FILE *file)
{
    if(width<1 || height<1 || (uint64_t)width*height>4*1024*1024)return;
    unsigned char *pixels=malloc((size_t)width*height*4);if(!pixels)return;
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint read=0,pack=0;GLuint probe=0;
    const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,
        GL_PACK_ROW_BYTES_APPLE,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};
    GLint values[sizeof(keys)/sizeof(*keys)];
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i){glGetIntegerv(keys[i],values+i);glPixelStorei(keys[i],i?0:1);}
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGenFramebuffersEXT(1,&probe);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,probe);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,target,(GLuint)texture,0);
    GLenum status=glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT),error=GL_NO_ERROR;
    if(status==GL_FRAMEBUFFER_COMPLETE_EXT){
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);error=glGetError();
        if(!error){
            unsigned minimum=255,maximum=0,zero=0,opaque=0,unassociated=0;
            for(size_t i=0;i<(size_t)width*height;++i){
                const unsigned char *p=pixels+4*i;unsigned a=p[3];
                if(a<minimum)minimum=a;if(a>maximum)maximum=a;zero+=!a;opaque+=a==255;
                unassociated+=p[0]>a || p[1]>a || p[2]>a;
            }
            fprintf(file,"ASSEMBLY_TEXTURE unit=%u name=%d alpha_min=%u alpha_max=%u zero=%u opaque=%u rgb_above_alpha=%u count=%zu\n",
                unit,texture,minimum,maximum,zero,opaque,unassociated,(size_t)width*height);
            for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){
                int sx=(width-1)*x/2,sy=(height-1)*y/2;
                const unsigned char *p=pixels+4*((size_t)sy*width+sx);
                fprintf(file,"ASSEMBLY_TEXEL unit=%u xy=%d,%d rgba=%u,%u,%u,%u\n",unit,sx,sy,p[0],p[1],p[2],p[3]);
            }
        }
    }
    if(status!=GL_FRAMEBUFFER_COMPLETE_EXT || error)
        fprintf(file,"ASSEMBLY_TEXTURE unit=%u unavailable status=%x error=%x\n",unit,status,error);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glDeleteFramebuffersEXT(1,&probe);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)glPixelStorei(keys[i],values[i]);
    DGRestoreColorTransfer(&transfer);free(pixels);
}
static void DGTraceDestinationAlpha(struct DGHostContext *c,struct DGMessage *m,
    const struct DGVertexArray *arrays,unsigned count,GLint program,FILE *file)
{
    // Numeric alpha statistics, never color pixels or an image. Restrict
    // this to the measured four-vertex CA client-array/matrix seam so the
    // diagnostic describes the actual impending draw's destination region.
    if(!arrays || m->args[1] || m->args[2]!=4 || !c->drawable)return;
    if(m->args[3]) {
        if(m->args[3]!=GL_UNSIGNED_SHORT || m->args[4])return;
        const GLushort *indices=DGHostPointer(m,5,4*sizeof(*indices));if(!indices)return;
        unsigned used=0;for(unsigned i=0;i<4;++i) { if(indices[i]>=4)return;used|=1u<<indices[i]; }
        if(used!=15)return;
    }
    GLint draw=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    if((GLuint)draw!=c->framebuffer)return;
    const struct DGVertexArray *position=NULL;
    for(unsigned i=0;i<count;++i)if(arrays[i].slot==16)position=&arrays[i];
    if(!position || position->buffer || position->type!=GL_FLOAT || position->size!=4 ||
       !position->pointer || position->pointer-1>m->bytes || position->bytes>m->bytes-(position->pointer-1))return;
    size_t stride=position->stride?position->stride:4*sizeof(GLfloat);
    if(stride>SIZE_MAX/3 || 3*stride+4*sizeof(GLfloat)>position->bytes)return;
    GLint location=glGetUniformLocation((GLuint)program,"vertex_matrix");if(location<0)return;
    GLfloat matrix[16]={0};glGetUniformfv((GLuint)program,location,matrix);
    GLint viewport[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);
    double left=c->drawable->width,bottom=c->drawable->height,right=0,top=0;
    const unsigned char *data=(const unsigned char *)(m+1)+position->pointer-1;
    for(unsigned i=0;i<4;++i) {
        GLfloat v[4];memcpy(v,data+i*stride,sizeof(v));double clip[4]={0};
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)clip[row]+=matrix[col*4+row]*v[col];
        if(!isfinite(clip[0]) || !isfinite(clip[1]) || !isfinite(clip[3]) || clip[3]<=0)return;
        double x=viewport[0]+(.5+.5*clip[0]/clip[3])*viewport[2];
        double y=viewport[1]+(.5+.5*clip[1]/clip[3])*viewport[3];
        if(x<left)left=x;if(x>right)right=x;if(y<bottom)bottom=y;if(y>top)top=y;
    }
    if(left<0)left=0;if(bottom<0)bottom=0;
    if(right>c->drawable->width)right=c->drawable->width;if(top>c->drawable->height)top=c->drawable->height;
    int x=(int)ceil(left),y=(int)ceil(bottom),width=(int)floor(right)-x,height=(int)floor(top)-y;
    if(width<1 || height<1 || (uint64_t)width*height>4*1024*1024)return;
    unsigned char *alpha=malloc((size_t)width*height*4);if(!alpha)return;
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    GLint read=0,buffer=0,pack=0;
    const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,
        GL_PACK_ROW_BYTES_APPLE,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};
    GLint values[sizeof(keys)/sizeof(*keys)];
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i) { glGetIntegerv(keys[i],&values[i]);glPixelStorei(keys[i],i?0:1); }
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c->framebuffer);
    glGetIntegerv(GL_READ_BUFFER,&buffer);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(x,y,width,height,GL_RGBA,GL_UNSIGNED_BYTE,alpha);
    GLenum read_error=glGetError();
    unsigned minimum=255,maximum=0,zero=0,opaque=0;
    for(size_t i=0;!read_error && i<(size_t)width*height;++i) {
        unsigned a=alpha[i*4+3];
        if(a<minimum)minimum=a;if(a>maximum)maximum=a;zero+=a==0;opaque+=a==255;
    }
    if(read_error)fprintf(file,"DESTINATION_ALPHA unavailable error=%x\n",read_error);
    else fprintf(file,"DESTINATION_ALPHA before_draw rect=%d,%d %dx%d min=%u max=%u zero=%u opaque=%u count=%zu\n",
        x,y,width,height,minimum,maximum,zero,opaque,(size_t)width*height);
    GLint sampler=glGetUniformLocation((GLuint)program,"texture1"),unit=0,active=0;
    if(sampler>=0) {
        glGetUniformiv((GLuint)program,sampler,&unit);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        if(unit>=0 && unit<16) {
            glActiveTexture(GL_TEXTURE0+unit);GLint tw=0,th=0,name=0;
            glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&name);
            glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&tw);
            glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&th);
            if(name && tw>0 && th>0 && (uint64_t)tw*th<=4*1024*1024 && x+width<=tw && y+height<=th) {
                size_t expected=(size_t)tw*th,capacity=(((size_t)tw*4+63)&~(size_t)63)*th;
                unsigned char *sampled=malloc(capacity);
                if(sampled) {
                    memset(sampled,0xa5,capacity);
                    // Native Intel GetTexImage on an IOSurface rectangle
                    // collapses rows when APPLE pitch is explicitly zero.
                    // The paired owned-surface control verifies this exact
                    // pitch versus FBO sampling; do not infer missing pixels
                    // from that diagnostic-only packing artifact.
                    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,tw);
                    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_ALPHA,GL_UNSIGNED_BYTE,sampled);
                    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,0);
                    GLenum texture_error=glGetError();
                    minimum=255;maximum=zero=opaque=0;
                    for(int row=y;!texture_error && row<y+height;++row)for(int col=x;col<x+width;++col) {
                        unsigned a=sampled[(size_t)row*tw+col];
                        if(a<minimum)minimum=a;if(a>maximum)maximum=a;zero+=!a;opaque+=a==255;
                    }
                    unsigned guard_changed=0;for(size_t i=expected;i<capacity;++i)guard_changed+=sampled[i]!=0xa5;
                    if(texture_error)fprintf(file,"DESTINATION_TEXTURE_ALPHA unavailable error=%x\n",texture_error);
                    else fprintf(file,"DESTINATION_TEXTURE_ALPHA name=%d size=%d,%d rect=%d,%d %dx%d min=%u max=%u zero=%u opaque=%u\n",
                        name,tw,th,x,y,width,height,minimum,maximum,zero,opaque);
                    fprintf(file,"DESTINATION_TEXTURE_TRANSFER expected=%zu guard_changed=%u\n",expected,guard_changed);free(sampled);
                }
                // Compare the actual imported texture through a distinct,
                // complete READ FBO, not only glGetTexImage's CPU format path.
                // No draw state or pixels are modified; log alpha counts only.
                GLuint probe=0;glGenFramebuffersEXT(1,&probe);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,probe);
                glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,(GLuint)name,0);
                GLenum status=glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT);
                if(status==GL_FRAMEBUFFER_COMPLETE_EXT) {
                    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadPixels(x,y,width,height,GL_RGBA,GL_UNSIGNED_BYTE,alpha);
                    GLenum alias_error=glGetError();minimum=255;maximum=zero=opaque=0;
                    for(size_t i=0;!alias_error && i<(size_t)width*height;++i) {
                        unsigned a=alpha[i*4+3];if(a<minimum)minimum=a;if(a>maximum)maximum=a;zero+=!a;opaque+=a==255;
                    }
                    if(alias_error)fprintf(file,"DESTINATION_ALIAS_FBO_ALPHA unavailable error=%x\n",alias_error);
                    else fprintf(file,"DESTINATION_ALIAS_FBO_ALPHA name=%d rect=%d,%d %dx%d min=%u max=%u zero=%u opaque=%u\n",
                        name,x,y,width,height,minimum,maximum,zero,opaque);
                } else fprintf(file,"DESTINATION_ALIAS_FBO_ALPHA unavailable status=%x\n",status);
                glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c->framebuffer);glDeleteFramebuffersEXT(1,&probe);
            }
        }
        glActiveTexture((GLenum)active);
    }
    glReadBuffer((GLenum)buffer);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)glPixelStorei(keys[i],values[i]);
    DGRestoreColorTransfer(&transfer);free(alpha);
}
static void DGTraceProgram(struct DGHostContext *c,struct DGMessage *m,
    const struct DGVertexArray *arrays,unsigned count)
{
    if(!dg_diagnostics) return;
    const char *folder=getenv("VIM_PASSTHROUGH_OPENGL_TRACE_DIR");
    int geometry=getenv("VIM_PASSTHROUGH_OPENGL_TRACE_GEOMETRY")!=NULL;
    if(!folder || c->program_debug_count>=(geometry?256u:32u)) return;
    GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    int assembly=!program && glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);
    if(assembly)glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_BINDING_ARB,&program);
    GLint viewport[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);
    if(!program) return;
    // Window-size changes must not exhaust the budget before menu/effect
    // shaders appear. Capture each actual shader program once per context.
    uint64_t key=(uint32_t)program | (assembly?UINT64_C(1)<<63:0);
    // Explicit correctness observation: the same CA program can draw an
    // ordinary background and, later, a selection. A program-only key hides
    // the latter. The bounded key hashes draw state/vertex bytes without new
    // readback or mutation. The existing diagnostic below MAY read pixels;
    // keep all of it opt-in and use no diagnostic work for timing acceptance.
    if(geometry && arrays && count) {
        GLint state[4]={0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,state);
        glGetIntegerv(GL_BLEND_SRC,state+1);glGetIntegerv(GL_BLEND_DST,state+2);
        glGetIntegerv(GL_BLEND_EQUATION,state+3);
        const unsigned char *bytes=(const unsigned char *)state;
        for(size_t j=0;j<sizeof(state);++j)key=(key^bytes[j])*UINT64_C(1099511628211);
        for(unsigned i=0;i<count;++i) {
            const struct DGVertexArray *a=arrays+i;
            if(a->buffer || !a->pointer || a->pointer-1>m->bytes || a->bytes>m->bytes-(a->pointer-1))continue;
            const unsigned char *data=(const unsigned char *)(m+1)+a->pointer-1;
            size_t n=a->bytes<32?(size_t)a->bytes:32;
            for(size_t j=0;j<n;++j)key=(key^data[j])*UINT64_C(1099511628211);
        }
    }
    for(unsigned i=0;i<c->program_debug_count;++i) if(c->traced_program_states[i]==key) return;
    c->traced_program_states[c->program_debug_count++]=key;
    char path[2048];
    if(snprintf(path,sizeof(path),"%s/program-%d-%p-%d-%u.txt",folder,getpid(),(void *)c,program,c->program_debug_count)>=(int)sizeof(path)) return;
    FILE *file=fopen(path,"w");if(!file) return;
    if(!arrays && !count) { c->traced_immediate_program=program;c->immediate_vertex_budget=4; }
    GLint framebuffer=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&framebuffer);
    fprintf(file,"program=%d framebuffer=%d default=%u drawable=%u viewport=%d,%d,%d,%d color=%u storage=%p\n",program,framebuffer,c->framebuffer,
        c->drawable ? c->drawable->name : 0,viewport[0],viewport[1],viewport[2],viewport[3],c->color,(void *)c->color_storage);
    if(framebuffer) {
        GLint kind=0,name=0,level=0;
        glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&kind);
        glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&name);
        if(kind==GL_TEXTURE)glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT,&level);
        fprintf(file,"ACTUAL_COLOR_ATTACHMENT type=%x name=%d level=%d\n",kind,name,level);
    }
    GLenum caps[]={GL_BLEND,GL_ALPHA_TEST,GL_SCISSOR_TEST,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_POLYGON_STIPPLE,
        GL_CLIP_PLANE0,GL_CLIP_PLANE1,GL_CLIP_PLANE2,GL_CLIP_PLANE3,GL_CLIP_PLANE4,GL_CLIP_PLANE5};
    for(unsigned i=0;i<sizeof(caps)/sizeof(caps[0]);++i) fprintf(file,"CAP %x enabled=%d\n",caps[i],glIsEnabled(caps[i]));
    fprintf(file,"DRAW op=%x mode=%llx first=%llu count=%llu\n",m->op,m->args[0],m->args[1],m->args[2]);
    GLenum keys[]={GL_DRAW_BUFFER,GL_READ_BUFFER,GL_STENCIL_FUNC,GL_STENCIL_REF,GL_STENCIL_VALUE_MASK,
        GL_STENCIL_FAIL,GL_STENCIL_PASS_DEPTH_FAIL,GL_STENCIL_PASS_DEPTH_PASS,GL_DEPTH_FUNC,
        GL_BLEND_SRC,GL_BLEND_DST,GL_BLEND_EQUATION,GL_ALPHA_TEST_FUNC};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i) { GLint value=0;glGetIntegerv(keys[i],&value);fprintf(file,"STATE %x = %x\n",keys[i],value); }
    GLint scissor[4];GLboolean mask[4];GLfloat reference;
    glGetIntegerv(GL_SCISSOR_BOX,scissor);glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetFloatv(GL_ALPHA_TEST_REF,&reference);
    fprintf(file,"SCISSOR %d,%d,%d,%d COLOR_MASK %u,%u,%u,%u ALPHA_REF %g\n",
        scissor[0],scissor[1],scissor[2],scissor[3],mask[0],mask[1],mask[2],mask[3],reference);
    GLenum transfer[]={GL_RED_SCALE,GL_GREEN_SCALE,GL_BLUE_SCALE,GL_ALPHA_SCALE,
        GL_RED_BIAS,GL_GREEN_BIAS,GL_BLUE_BIAS,GL_ALPHA_BIAS,GL_MAP_COLOR};
    for(unsigned i=0;i<sizeof(transfer)/sizeof(*transfer);++i) {
        GLfloat value=0;glGetFloatv(transfer[i],&value);fprintf(file,"COLOR_TRANSFER %x = %g\n",transfer[i],value);
    }
    GLenum matrices[]={GL_MODELVIEW_MATRIX,GL_PROJECTION_MATRIX};
    for(unsigned i=0;i<sizeof(matrices)/sizeof(*matrices);++i) {
        GLfloat values[16];glGetFloatv(matrices[i],values);fprintf(file,"MATRIX %x",matrices[i]);
        for(unsigned j=0;j<16;++j) fprintf(file," %g",values[j]);fputc('\n',file);
    }
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(unsigned unit=0;unit<3;++unit) {
        glActiveTexture(GL_TEXTURE0+unit);
        GLfloat texture_matrix[16];glGetFloatv(GL_TEXTURE_MATRIX,texture_matrix);
        fprintf(file,"TEXTURE_MATRIX unit=%u",unit);
        for(unsigned j=0;j<16;++j)fprintf(file," %g",texture_matrix[j]);fputc('\n',file);
        GLenum targets[]={GL_TEXTURE_2D,GL_TEXTURE_RECTANGLE_ARB};
        GLenum bindings[]={GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_RECTANGLE_ARB};
        for(unsigned j=0;j<2;++j) {
            GLint texture=0,width=0,height=0,min=0,mag=0,wraps=0,wrapt=0;
            glGetIntegerv(bindings[j],&texture);if(!texture) continue;
            glGetTexLevelParameteriv(targets[j],0,GL_TEXTURE_WIDTH,&width);
            glGetTexLevelParameteriv(targets[j],0,GL_TEXTURE_HEIGHT,&height);
            glGetTexParameteriv(targets[j],GL_TEXTURE_MIN_FILTER,&min);glGetTexParameteriv(targets[j],GL_TEXTURE_MAG_FILTER,&mag);
            glGetTexParameteriv(targets[j],GL_TEXTURE_WRAP_S,&wraps);glGetTexParameteriv(targets[j],GL_TEXTURE_WRAP_T,&wrapt);
            fprintf(file,"TEXTURE unit=%u target=%x name=%d size=%d,%d filter=%x,%x wrap=%x,%x\n",unit,targets[j],texture,width,height,min,mag,wraps,wrapt);
            if(assembly)DGTraceAssemblyTexture(c,targets[j],texture,width,height,unit,file);
            if(unit==0 && targets[j]==GL_TEXTURE_RECTANGLE_ARB &&
               ((width==256 && height==256) || (width>0 && width<=8192 && height>0 && height<=64))) {
                GLint pack=0,row=0,alignment=0,skiprows=0,skippixels=0,rowbytes=0;
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);
                glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);glGetIntegerv(GL_PACK_SKIP_ROWS,&skiprows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skippixels);
                glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&rowbytes);
                unsigned char *image=calloc((size_t)width*height,4);
                if(image) {
                    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_ALIGNMENT,4);
                    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
                    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,width*4);
                    glGetTexImage(targets[j],0,GL_RGBA,GL_UNSIGNED_BYTE,image);
                    unsigned step=height>64?16:1;
                    for(unsigned y=0;y<(unsigned)height;y+=step){
                        unsigned char *left=image+(size_t)y*width*4,*middle=left+(width/2)*4;
                        fprintf(file,"TEXTURE ROW %u left=%u,%u,%u,%u middle=%u,%u,%u,%u\n",y,
                            left[0],left[1],left[2],left[3],middle[0],middle[1],middle[2],middle[3]);
                    }
                    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);glPixelStorei(GL_PACK_ROW_LENGTH,row);glPixelStorei(GL_PACK_ALIGNMENT,alignment);
                    glPixelStorei(GL_PACK_SKIP_ROWS,skiprows);glPixelStorei(GL_PACK_SKIP_PIXELS,skippixels);free(image);
                    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,rowbytes);
                }
            }
        }
    }
    glActiveTexture((GLenum)active);
    if(assembly){
        fprintf(file,"ARB_VERTEX_ENABLED %u\n",glIsEnabled(GL_VERTEX_PROGRAM_ARB));
        GLint vertex=0;glGetProgramivARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_BINDING_ARB,&vertex);
        fprintf(file,"ARB_VERTEX_BINDING %d\n",vertex);
        if(vertex){
            GLint size=0;glGetProgramivARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&size);
            if(size>0 && size<=65536){
                char *source=calloc((size_t)size+1,1);
                if(source){glGetProgramStringARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_STRING_ARB,source);
                    fprintf(file,"ARB_VERTEX_PROGRAM\n%s\n",source);free(source);}
            }
            for(unsigned i=0;i<16;++i){
                GLfloat local[4]={0};glGetProgramLocalParameterfvARB(GL_VERTEX_PROGRAM_ARB,i,local);
                fprintf(file,"ARB_VERTEX_LOCAL %u = %g,%g,%g,%g\n",i,local[0],local[1],local[2],local[3]);
            }
        }
        GLint length=0;glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&length);
        if(length>0 && length<=65536){
            char *text=calloc((size_t)length+1,1);
            if(text){glGetProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_STRING_ARB,text);
                fprintf(file,"ARB_FRAGMENT_PROGRAM\n%s\n",text);free(text);}
        }
        // Native Core Image's ripple uses locals through 10. Capturing only
        // the first four hides both the texture transforms and wave bounds.
        for(unsigned i=0;i<16;++i){
            GLfloat local[4]={0};glGetProgramLocalParameterfvARB(GL_FRAGMENT_PROGRAM_ARB,i,local);
            fprintf(file,"ARB_LOCAL %u = %g,%g,%g,%g\n",i,local[0],local[1],local[2],local[3]);
        }
    }else{
    GLuint shaders[8]={0};GLsizei n=0;glGetAttachedShaders((GLuint)program,8,&n,shaders);
    for(int i=0;i<n;++i) {
        GLint length=0,type=0;glGetShaderiv(shaders[i],GL_SHADER_SOURCE_LENGTH,&length);glGetShaderiv(shaders[i],GL_SHADER_TYPE,&type);
        if(length<1 || length>65536) continue;
        char *source=calloc(1,(size_t)length);if(!source) continue;
        glGetShaderSource(shaders[i],length,NULL,source);fprintf(file,"SHADER type=%x\n%s\n",type,source);free(source);
    }
    GLint uniforms=0;glGetProgramiv((GLuint)program,GL_ACTIVE_UNIFORMS,&uniforms);
    for(GLint i=0;i<uniforms && i<64;++i) {
        char name[512]={0};GLint size=0;GLenum type=0;
        glGetActiveUniform((GLuint)program,(GLuint)i,sizeof(name),NULL,&size,&type,name);
        GLint location=glGetUniformLocation((GLuint)program,name);
        fprintf(file,"UNIFORM %s location=%d type=%x count=%d",name,location,type,size);
        // Fixed-function built-ins can be active but have location -1.
        // Querying them with glGetUniformfv creates GL_INVALID_OPERATION
        // and would contaminate the application's own error state.
        if(location>=0 && size>0 && size<=256) {
            GLfloat *values=calloc((size_t)size*16,sizeof(*values));
            if(values) {
                glGetUniformfv((GLuint)program,location,values);
                for(unsigned j=0;j<16;++j) fprintf(file," %g",values[j]);free(values);
            }
        }
        fputc('\n',file);
    }
    }
    for(unsigned i=0;i<count;++i) {
        const struct DGVertexArray *a=&arrays[i];
        fprintf(file,"ARRAY slot=%u size=%u type=%x stride=%u normalized=%u buffer=%u bytes=%llu\n",
            a->slot,a->size,a->type,a->stride,a->normalized,a->buffer,a->bytes);
        if(a->buffer || (a->type!=GL_FLOAT && a->type!=GL_UNSIGNED_BYTE) ||
           !a->pointer || a->pointer-1>m->bytes || a->bytes>m->bytes-(a->pointer-1)) continue;
        const unsigned char *base=(const unsigned char *)(m+1)+a->pointer-1;
        size_t element=a->type==GL_FLOAT?sizeof(GLfloat):1;
        size_t stride=a->stride ? a->stride : a->size*element;
        for(unsigned v=0;v<4 && (size_t)v*stride+a->size*element<=a->bytes;++v) {
            fprintf(file,"  vertex %u:",v);
            for(unsigned j=0;j<a->size;++j) {
                if(a->type==GL_UNSIGNED_BYTE)fprintf(file," %u",base[(size_t)v*stride+j]);
                else { GLfloat value;memcpy(&value,base+(size_t)v*stride+j*sizeof(value),sizeof(value));fprintf(file," %g",value); }
            }
            fputc('\n',file);
        }
    }
    if(!assembly)DGTraceDestinationAlpha(c,m,arrays,count,program,file);
    fclose(file);fprintf(stderr,"direct draw state captured: %s\n",path);
}
