/* Actual host executor routines, Intel GPU, pitched backing + cropped views.
 * Successful ownership checks never read back the shared FBO. Only the
 * post-failure diagnostic inspects it, without changing the failed verdict. */
/* Compare nominal GPU completion with actual CPU-visible ownership. These
 * switches exist only in this own-context diagnostic, not the executor.
 * A signalled fence or matching pointer alone is NOT a coherence pass. */
#define VIM_VRAM_COMPLETION_EXPERIMENT 1
#define VIM_VRAM_STAGED_RESTORE_EXPERIMENT 1
#define VIM_VRAM_COPYTEX_EXPERIMENT 1
static int vim_vram_completion_fence=1;
static int vim_vram_finish_object;
static int vim_vram_iosurface;
static int vim_vram_flip_destination;
static int vim_vram_draw_source;
static int vim_vram_cache_flush;
static int vim_vram_copy_draw;
#include <libkern/OSCacheControl.h>
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
static const char *completionName(void)
{return vim_vram_completion_fence==2?"apple-fence":vim_vram_completion_fence?"fence":"finish";}
static void DGVRAMOwnedContextDrawCopy(struct DGHostContext *c,const struct DGMessage *m,unsigned count)
{
    assert(DGVRAMCopyDrawable(c,m,count,NULL));
}
static void DGVRAMExperimentDrawCopy(struct DGHostContext *c,const struct DGMessage *m,unsigned count)
{
    if(vim_vram_copy_draw==2){DGVRAMOwnedContextDrawCopy(c,m,count);return;}
    int publish=m->op==DG_VRAM_PUBLISH_DRAWABLE,flip=(int)m->args[9];
    int x=(int)m->args[5],y=(int)m->args[6],w=(int)m->args[7],h=(int)m->args[8];
    int dw=publish?(int)(c->vram->pitch/4):w,dh=publish?(int)c->vram->height:h;
    GLuint texture=publish?c->color:c->vram->texture;
    GLint program=0,matrix=0,active=0,units=0,min=0,mag=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_MATRIX_MODE,&matrix);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_MAX_TEXTURE_UNITS,&units);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glUseProgram(0);
    glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_FOG);glDisable(GL_DITHER);glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);glDisable(GL_LIGHTING);
    const char *ext=(const char *)glGetString(GL_EXTENSIONS);
    if(strstr(ext,"GL_ARB_vertex_program"))glDisable(GL_VERTEX_PROGRAM_ARB);
    if(strstr(ext,"GL_ARB_fragment_program"))glDisable(GL_FRAGMENT_PROGRAM_ARB);
    for(int i=0;i<6;++i)glDisable(GL_CLIP_PLANE0+i);
    for(int i=0;i<units;++i){
        glActiveTexture(GL_TEXTURE0+i);glDisable(GL_TEXTURE_1D);glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_3D);glDisable(GL_TEXTURE_CUBE_MAP);glDisable(GL_TEXTURE_RECTANGLE_ARB);
    }
    glActiveTexture(GL_TEXTURE0);glEnable(GL_TEXTURE_RECTANGLE_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glGetTexParameteriv(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,&min);
    glGetTexParameteriv(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,&mag);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glViewport(0,0,dw,dh);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,dw,0,dh,-1,1);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();
    const struct DGVRAMRect *rects=(const void *)(m+1);
    glBegin(GL_QUADS);
    for(unsigned i=0;i<(publish?count:1);++i){
        int sx0=x,sx1=x+w,sy0=y,sy1=y+h,dx0=0,dx1=w,dy0=flip?h:0,dy1=flip?0:h;
        if(publish){
            const struct DGVRAMRect *r=rects+i;sx0=r->x;sx1=r->x+r->width;
            sy0=flip?h-r->y:r->y;sy1=flip?h-r->y-r->height:r->y+r->height;
            dx0=x+r->x;dx1=dx0+r->width;dy0=y+r->y;dy1=dy0+r->height;
        }
        glTexCoord2i(sx0,sy0);glVertex2i(dx0,dy0);glTexCoord2i(sx1,sy0);glVertex2i(dx1,dy0);
        glTexCoord2i(sx1,sy1);glVertex2i(dx1,dy1);glTexCoord2i(sx0,sy1);glVertex2i(dx0,dy1);
    }
    glEnd();glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();
    glMatrixMode(matrix);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,min);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,mag);
    glPopAttrib();glActiveTexture(active);glUseProgram(program);
}
static uint32_t pattern(unsigned x,unsigned y)
{return 0xff000000u|((x*13u+y*3u)&255)|(((x*7u+y*17u)&255)<<8)|(((x+y*11u)&255)<<16);}
static GLuint sentinelProgram(void);
static uint32_t patchPattern(unsigned x,unsigned y,unsigned round)
{return (pattern(x+round*7,y+round*11)&0x00ffffffu)|(((x*3+y*5+round*19)&255)<<24);}
static void exercisePatch(int flip,int ram)
{
    enum {Width=640,Height=480,X=3,Y=7,W=251,H=197,Offset=65536};
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,W,H));
    struct VIMVRAMMemoryMapping cpuAlias={0};uint64_t pages[300]={0};
    if(ram){
        for(unsigned i=0;i<300;++i)pages[i]=0x10000000u+(299-i)*4096;
        assert(!VIMSharedRAMMapPages(&dg_ram,pages,300,&cpuAlias));
    }
    uint32_t *pixels=ram?(void *)(uintptr_t)cpuAlias.address:(void *)(uintptr_t)(dg_vram.address+Offset);
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)pixels[y*Width+x]=pattern(x,y);
    struct {struct DGMessage m;union {struct DGVRAMRect rects[3];uint64_t pages[300];};} p={0};
    uint64_t *a=p.m.args;a[0]=ram?0:Offset;a[1]=Width*Height*4;a[2]=Width;a[3]=Height;a[4]=Width*4;
    a[5]=X;a[6]=Y;a[7]=W;a[8]=H;a[9]=flip;a[10]=173;a[11]=4;a[12]=1;a[13]=ram;
    p.m.op=DG_VRAM_IMPORT_DRAWABLE;
    if(ram){a[14]=300;p.m.bytes=sizeof(p.pages);memcpy(p.pages,pages,sizeof(pages));}
    struct DGReply reply={0};assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
    uint32_t *view=malloc(W*H*4);assert(view);
    glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,view);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        assert(view[y*W+x]==pattern(X+x,Y+(flip?H-1-y:y)));
    // Earlier, unflushed GPU drawing outside the CPU update must survive.
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    GLuint sentinel=sentinelProgram();glUseProgram(sentinel);
    uint32_t expected[W*H];for(unsigned i=0;i<W*H;++i)expected[i]=0xffff00ff;
    p.m.op=DG_VRAM_PATCH_DRAWABLE;p.m.bytes=3*sizeof(struct DGVRAMRect);a[14]=0;
    p.rects[0]=(struct DGVRAMRect){11,13,101,67};p.rects[1]=(struct DGVRAMRect){131,77,41,39};
    p.rects[2]=(struct DGVRAMRect){53,170,51,23};
    for(unsigned round=1;round<=12;++round){
        // Rewrite the CPU source AFTER its earlier GPU read, without
        // reissuing glTexImage. This is the actual proposed coherence seam.
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)
            pixels[y*Width+x]=patchPattern(x,y,round);
        ++a[12];glEnable(GL_SCISSOR_TEST);glScissor(1,2,3,4);
        glEnable(GL_FRAMEBUFFER_SRGB_EXT);glViewport(7,9,123,117);
        glEnable(0xffffffffu); // Private transfer must preserve pending error.
        memset(&reply,0,sizeof(reply));
        assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
        assert(c.error==GL_INVALID_ENUM && !glGetError());c.error=0;
        GLint viewport[4]={0},program=0;glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        assert((GLuint)program==sentinel);
        assert(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT));
        assert(viewport[0]==7 && viewport[1]==9 && viewport[2]==123 && viewport[3]==117);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned sy=flip?H-1-y:y;
            for(unsigned r=0;r<3;++r)if(x>=(unsigned)p.rects[r].x && x<(unsigned)(p.rects[r].x+p.rects[r].width) &&
                sy>=(unsigned)p.rects[r].y && sy<(unsigned)(p.rects[r].y+p.rects[r].height))
                expected[y*W+x]=patchPattern(X+x,Y+sy,round);
        }
        glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,view);
        for(unsigned i=0;i<W*H;++i)if(view[i]!=expected[i]){
            fprintf(stderr,"PATCH mismatch flip=%d RAM=%d round=%u x=%u y=%u got=%08x expected=%08x\n",
                flip,ram,round,i%W,i/W,view[i],expected[i]);abort();
        }
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)
            assert(((volatile uint32_t *)pixels)[y*Width+x]==patchPattern(x,y,round));
    }
    // A stale backing must not create a new texture or import the full view.
    struct DGVRAMDrawableBacking *identity=c.vram;
    ++a[11];memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && !reply.result && c.vram==identity);--a[11];
    --a[12];memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && !reply.result);++a[12];
    p.rects[0].x=-1;assert(DGVRAMTransfer(&c,&p.m,&reply)==-1);p.rects[0].x=11;
    p.m.bytes=0;assert(DGVRAMTransfer(&c,&p.m,&reply)==-1);p.m.bytes=3*sizeof(struct DGVRAMRect);
    a[14]=1;assert(DGVRAMTransfer(&c,&p.m,&reply)==-1);a[14]=0;
    glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,view);
    assert(!memcmp(view,expected,sizeof(expected)) && !glGetError());
    printf("PASS shared patch CPU rewrites, sparse holes, prior GPU contents, state/error preservation, stale identity/epoch and bounds flip=%d RAM=%d\n",flip,ram);
    glUseProgram(0);glDeleteProgram(sentinel);
    free(view);DGDestroyDrawables(&c);DGDestroyAliases(&c);VIMVRAMMemoryUnmap(&cpuAlias);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
}
static void fillSource(float r,float g,float b)
{
    if(vim_vram_draw_source){glColor4f(r,g,b,1);glRectf(-1,-1,1,1);}
    else {glClearColor(r,g,b,1);glClear(GL_COLOR_BUFFER_BIT);}
}
static GLuint sentinelProgram(void)
{
    const char *source="#version 120\nvoid main(){gl_FragColor=vec4(1.,0.,1.,1.);}";
    GLuint shader=glCreateShader(GL_FRAGMENT_SHADER);assert(shader);
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint good=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&good);assert(good);
    GLuint program=glCreateProgram();assert(program);glAttachShader(program,shader);glLinkProgram(program);
    glGetProgramiv(program,GL_LINK_STATUS,&good);assert(good);glDeleteShader(shader);return program;
}
static void benchmark(int width,int height,int compareCopy,int compareWait)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,width,height));
    uint32_t *pixels=(void *)(uintptr_t)dg_vram.address;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[y*width+x]=pattern(x,y);
    struct {struct DGMessage m;struct DGVRAMRect rect;} p={0};
    uint64_t *a=p.m.args;a[1]=(uint64_t)width*height*4;a[2]=width;a[3]=height;a[4]=width*4;
    a[7]=width;a[8]=height;a[10]=17;a[11]=3;a[12]=1;
    p.rect=(struct DGVRAMRect){0,0,width,height};
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    /* A/B/B/A order shares exactly the same context, backing and GPU work.
     * Read the CPU alias immediately after each handoff, never readback the
     * shared FBO (which could hide an incomplete ownership transfer). */
    for(unsigned run=0;run<4;++run){
        vim_vram_completion_fence=(compareCopy || compareWait)?1:run==0 || run==3;
        if(compareWait)dg_vram_direct_copy_wait=run==1 || run==2;
        if(compareCopy)vim_vram_copy_draw=(run==1 || run==2)?compareCopy:0;
        uint64_t importTicks=0,publishTicks=0,pairedImport[2]={0},pairedPublish[2]={0};
        unsigned pairedCount[2]={0};
        for(unsigned i=0;i<40;++i){
            // Pair adjacent old/new handoffs at the same warmed GPU clock,
            // not only cold-to-warm outer blocks. Preserve the historical
            // completion/copy comparison modes when compareWait is false.
            if(compareWait)dg_vram_direct_copy_wait=(i%4==1 || i%4==2);
            p.m.op=DG_VRAM_IMPORT_DRAWABLE;p.m.bytes=0;a[12]++;
            struct DGReply reply={0};uint64_t begin=mach_absolute_time();
            assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
            uint64_t elapsed=mach_absolute_time()-begin;
            if(i>=8){importTicks+=elapsed;pairedImport[dg_vram_direct_copy_wait]+=elapsed;}
            glClearColor((i&1)?1:0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
            p.m.op=DG_VRAM_PUBLISH_DRAWABLE;p.m.bytes=sizeof(p.rect);a[12]++;
            memset(&reply,0,sizeof(reply));begin=mach_absolute_time();
            assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
            elapsed=mach_absolute_time()-begin;
            if(i>=8){publishTicks+=elapsed;pairedPublish[dg_vram_direct_copy_wait]+=elapsed;++pairedCount[dg_vram_direct_copy_wait];}
            uint32_t expected=(i&1)?0xffffff00u:0xff00ff00u;
            assert(((volatile uint32_t *)pixels)[0]==expected && ((volatile uint32_t *)pixels)[width*height-1]==expected);
        }
        double scale=(double)clock.numer/clock.denom/1000/32;
        if(compareWait){
            for(unsigned route=0;route<2;++route){
                double pairedScale=(double)clock.numer/clock.denom/1000/pairedCount[route];
                printf("paired production direct-copy-wait=%u size=%dx%d run=%u samples=%u import-us=%.3f publish-us=%.3f\n",
                    route,width,height,run,pairedCount[route],pairedImport[route]*pairedScale,pairedPublish[route]*pairedScale);
            }
            continue;
        }
        printf("completion=%s copy=%s direct-copy-wait=%d size=%dx%d run=%u import-us=%.3f publish-us=%.3f\n",
            completionName(),dg_vram_draw_publish?"production-textured":vim_vram_copy_draw==2?"owned-draw":vim_vram_copy_draw?"draw":"blit",
            dg_vram_direct_copy_wait,width,height,run,importTicks*scale,publishTicks*scale);
    }
    assert(!glGetError());DGDestroyDrawables(&c);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
}
static void exercise(int flip,int ram)
{
    enum {Width=640,Height=480,X=3,Y=7,W=251,H=197,Offset=65536};
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);
    printf("renderer=%s flip=%d RAM=%d\n",glGetString(GL_RENDERER),flip,ram);
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,W,H));
    struct VIMVRAMMemoryMapping cpuAlias={0};uint64_t pages[300]={0};
    if(ram){
        for(unsigned i=0;i<300;++i)pages[i]=0x10000000u+(299-i)*4096;
        assert(!VIMSharedRAMMapPages(&dg_ram,pages,300,&cpuAlias));
    }
    uint32_t *pixels=ram?(void *)(uintptr_t)cpuAlias.address:(void *)(uintptr_t)(dg_vram.address+Offset);
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)pixels[y*Width+x]=pattern(x,y);
    struct {struct DGMessage m;union {struct DGVRAMRect rects[3];uint64_t pages[512];};} p={0};
    p.m.op=DG_VRAM_IMPORT_DRAWABLE;
    uint64_t *a=p.m.args;a[0]=ram?0:Offset;a[1]=Width*Height*4;a[2]=Width;a[3]=Height;a[4]=Width*4;
    a[5]=X;a[6]=Y;a[7]=W;a[8]=H;a[9]=flip;a[10]=17;a[11]=3;a[12]=1;
    a[13]=ram;
    glEnable(GL_SCISSOR_TEST);glScissor(1,2,3,4);glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    glPixelTransferf(GL_ALPHA_SCALE,.25);glPixelStorei(GL_UNPACK_ROW_LENGTH,13);
    struct DGReply reply={0};
    if(ram){
        assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==2 && !c.vram);
        a[14]=300;p.m.bytes=300*sizeof(uint64_t);memcpy(p.pages,pages,p.m.bytes);memset(&reply,0,sizeof(reply));
    }
    assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
    if(dg_vram_staged_restore){
        // Reuse source bytes BEFORE any GL query/readback/finish can make an
        // asynchronous borrowed read look safe. Both orientations snapshot.
        for(unsigned i=0;i<Width*Height;++i)pixels[i]=0xffabcdef;
    }
    GLfloat alpha=0;GLint row=0;glGetFloatv(GL_ALPHA_SCALE,&alpha);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
    assert(alpha==.25f && row==13 && glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT));
    glPixelTransferf(GL_ALPHA_SCALE,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    uint32_t *view=malloc(W*H*4);assert(view);
    glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,view);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        assert(view[y*W+x]==pattern(X+x,Y+(flip?H-1-y:y)));
    if(dg_vram_staged_restore)
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)pixels[y*Width+x]=pattern(x,y);
    /* CPU changes made after import, outside the native clip, must survive
     * publication of a newer view (including a hole INSIDE its bounds). */
    pixels[(Y+2)*Width+X+2]=0xff123456;pixels[Width*Height-1]=0xff654321;
    glViewport(0,0,W,H);fillSource(0,1,0);
    glEnable(GL_SCISSOR_TEST);glScissor(0,5,W,25);fillSource(1,0,0);glDisable(GL_SCISSOR_TEST);
    GLuint sentinel=sentinelProgram();glUseProgram(sentinel);
    glViewport(7,9,123,117);glActiveTexture(GL_TEXTURE1);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glMatrixMode(GL_TEXTURE);glPushMatrix();glTranslatef(.25f,.5f,0);
    p.m.op=DG_VRAM_PUBLISH_DRAWABLE;p.m.bytes=sizeof(p.rects);a[12]=2;a[14]=0;
    p.rects[0]=(struct DGVRAMRect){11,13,101,67};p.rects[1]=(struct DGVRAMRect){131,77,41,39};
    p.rects[2]=(struct DGVRAMRect){53,170,51,23};
    memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
    if(vim_vram_finish_object)glFinishObjectAPPLE(GL_TEXTURE,c.vram->texture);
    for(int y=0;y<Height;++y)for(int x=0;x<Width;++x){
        uint32_t expected=pattern(x,y);int vx=x-X,vy=y-Y,covered=0;
        for(unsigned i=0;i<3;++i){const struct DGVRAMRect *r=&p.rects[i];
            if(vx>=r->x && vx<r->x+r->width && vy>=r->y && vy<r->y+r->height)covered=1;}
        if(covered){int gy=flip?H-1-vy:vy;expected=gy>=5 && gy<30?0xffff0000:0xff00ff00;}
        if(x==X+2 && y==Y+2)expected=0xff123456;
        if(x==Width-1 && y==Height-1)expected=0xff654321;
        /* Snapshot once: a second read must not conceal a late GPU write. */
        uint32_t observed=((volatile uint32_t *)pixels)[y*Width+x];
        if(observed!=expected){
            uint32_t immediate=((volatile uint32_t *)pixels)[y*Width+x];
            usleep(1000);uint32_t delayed=((volatile uint32_t *)pixels)[y*Width+x],gpu=0;
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c.vram->framebuffer);
            glReadPixels(x,y,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&gpu);
            uint32_t afterRead=((volatile uint32_t *)pixels)[y*Width+x];
            uint32_t *target=malloc(Width*Height*4),*source=malloc(W*H*4);
            assert(target && source);
            glReadPixels(0,0,Width,Height,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,target);
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c.framebuffer);
            glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,source);
            uint32_t src=vx>=0 && vx<W && vy>=0 && vy<H?source[(flip?H-1-vy:vy)*W+vx]:0;
            fprintf(stderr,"mismatch %d,%d flip=%d RAM=%d completion=%s first=%08x immediate=%08x delayed=%08x GPU=%08x after-read=%08x expected=%08x\n",
                x,y,flip,ram,completionName(),observed,immediate,delayed,gpu,afterRead,expected);
            fprintf(stderr,"whole-image target=%08x source=%08x GLerror=%x\n",target[y*Width+x],src,glGetError());
            free(target);free(source);
        }
        assert(observed==expected);
    }
    GLint programAfter=0,activeAfter=0,matrixAfter=0,viewportAfter[4]={0};GLfloat textureAfter[16]={0};
    glGetIntegerv(GL_CURRENT_PROGRAM,&programAfter);glGetIntegerv(GL_ACTIVE_TEXTURE,&activeAfter);
    glGetIntegerv(GL_MATRIX_MODE,&matrixAfter);glGetIntegerv(GL_VIEWPORT,viewportAfter);
    glGetFloatv(GL_TEXTURE_MATRIX,textureAfter);
    assert((GLuint)programAfter==sentinel && activeAfter==GL_TEXTURE1 && matrixAfter==GL_TEXTURE);
    assert(viewportAfter[0]==7 && viewportAfter[1]==9 && viewportAfter[2]==123 && viewportAfter[3]==117);
    assert(glIsEnabled(GL_TEXTURE_RECTANGLE_ARB) && textureAfter[12]==.25f && textureAfter[13]==.5f);
    glPopMatrix();glMatrixMode(GL_MODELVIEW);glActiveTexture(GL_TEXTURE0);glUseProgram(0);glDeleteProgram(sentinel);
    a[11]++;memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && !reply.result);
    a[11]--;a[12]=1;memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && !reply.result);
    a[12]=2;p.rects[0].x=-1;assert(DGVRAMTransfer(&c,&p.m,&reply)==-1);
    if(ram){
        p.m.op=DG_VRAM_IMPORT_DRAWABLE;p.m.bytes=0;a[12]=3;
        assert(DGSelectDrawable(&c,2,W-11,H-7));a[7]=W-11;a[8]=H-7;
        mach_vm_address_t alias=c.vram->alias.address;memset(&reply,0,sizeof(reply));
        assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && c.vram->alias.address==alias);
    }
    if(dg_vram_draw_publish) {
        /* Exercise real backing replacement, not just a different view into
         * one backing. The private CGL queue/FBO must survive, while the
         * obsolete pinned-page mapping and texture are actually discarded.
         * Verify CPU-visible writes without a diagnostic pixel readback.
         */
        CGLContextObj copy=c.vram_copy_context;
        assert((copy && c.vram_copy_framebuffer) || (dg_vram_copytex && !flip && !copy));
        assert(DGSelectDrawable(&c,1,W,H));a[7]=W;a[8]=H;
        for(unsigned round=0;round<3;++round) {
            DGVRAMDelete(&c);assert(!c.vram && c.vram_copy_context==copy);
            for(unsigned i=0;i<Width*Height;++i)pixels[i]=0xff112233;
            p.m.op=DG_VRAM_IMPORT_DRAWABLE;a[10]=100+round;a[11]=1;a[12]=1;
            a[14]=ram?300:0;p.m.bytes=ram?300*sizeof(uint64_t):0;
            if(ram)memcpy(p.pages,pages,p.m.bytes);
            memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
            glDisable(GL_SCISSOR_TEST);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
            p.m.op=DG_VRAM_PUBLISH_DRAWABLE;a[12]=2;a[14]=0;
            p.m.bytes=sizeof(struct DGVRAMRect);p.rects[0]=(struct DGVRAMRect){3,4,16,12};
            memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
            assert(c.vram_copy_context==copy);
            for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x) {
                int covered=x>=X+3 && x<X+19 && y>=Y+4 && y<Y+16;
                assert(((volatile uint32_t *)pixels)[y*Width+x]==(covered?0xff0000ff:0xff112233));
            }
        }
        printf("PASS publication queue survives three backing replacements flip=%d RAM=%d copytex=%d\n",flip,ram,dg_vram_copytex && !flip);
    }
    assert(!glGetError());free(view);DGDestroyDrawables(&c);assert(!c.vram_copy_context);
    DGDestroyAliases(&c);VIMVRAMMemoryUnmap(&cpuAlias);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
}
static void exerciseBackingReuse(int ram)
{
    enum {W=64,H=64,Bytes=W*H*4,Count=6,Offset=65536};
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,W,H));
    struct {struct DGMessage m;union {uint64_t pages[4];struct DGVRAMRect rect;};} p[Count];
    memset(p,0,sizeof(p));struct DGVRAMDrawableBacking *saved[3]={0};
    uint32_t *pixels[Count];uint64_t epochs[Count]={0};
    for(unsigned slot=0;slot<Count;++slot){
        uint64_t *a=p[slot].m.args;
        pixels[slot]=(void *)(uintptr_t)((ram?dg_ram.address:dg_vram.address+Offset)+slot*Bytes);
        for(unsigned i=0;i<W*H;++i)pixels[slot][i]=0xff223344;
        a[0]=ram?0:Offset+slot*Bytes;a[1]=Bytes;a[2]=W;a[3]=H;a[4]=W*4;
        a[7]=W;a[8]=H;a[10]=1000+slot;a[11]=7;a[12]=epochs[slot]=1;a[13]=ram;
        p[slot].m.op=DG_VRAM_IMPORT_DRAWABLE;
        if(ram){a[14]=4;p[slot].m.bytes=sizeof(p[slot].pages);
            for(unsigned page=0;page<4;++page)p[slot].pages[page]=0x10000000u+slot*Bytes+page*4096;}
        if(slot>=3)continue;
        struct DGReply reply={0};assert(!DGVRAMTransfer(&c,&p[slot].m,&reply) && reply.result==1);
        saved[slot]=c.vram;
    }
    uint64_t creates=c.vram_backing_creates;assert(creates==3);
    uint32_t readback[W*H];
    for(unsigned round=0;round<12;++round){
        unsigned slot=round%3;uint64_t *a=p[slot].m.args;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
            pixels[slot][y*W+x]=patchPattern(x,y,round);
        a[12]=++epochs[slot];a[14]=0;p[slot].m.bytes=0;p[slot].m.op=DG_VRAM_IMPORT_DRAWABLE;
        struct DGReply reply={0};
        assert(!DGVRAMTransfer(&c,&p[slot].m,&reply) && reply.result==1 && !reply.error);
        assert(c.vram==saved[slot] && c.vram_backing_creates==creates);
        glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,readback);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
            assert(readback[y*W+x]==patchPattern(x,y,round));
        glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
        p[slot].m.op=DG_VRAM_PUBLISH_DRAWABLE;a[12]=++epochs[slot];
        p[slot].m.bytes=sizeof(p[slot].rect);p[slot].rect=(struct DGVRAMRect){8,9,16,17};
        memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p[slot].m,&reply) && reply.result==1);
        // CPU observation comes FIRST: no diagnostic readback/sleep may
        // make an incomplete GPU publication look coherent.
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
            assert(((volatile uint32_t *)pixels[slot])[y*W+x]==
                (x>=8&&x<24&&y>=9&&y<26?0xff00ff00:patchPattern(x,y,round)));
        unsigned stale=(slot+2)%3;struct DGMessage old=p[stale].m;
        old.op=DG_VRAM_IMPORT_DRAWABLE;old.bytes=0;old.args[14]=0;old.args[12]=epochs[stale]-1;
        struct DGVRAMDrawableBacking *active=c.vram;memset(&reply,0,sizeof(reply));
        assert(!DGVRAMTransfer(&c,&old,&reply) && !reply.result && c.vram==active);
        assert(c.vram_spare_bytes<=DG_VRAM_SPARE_BYTES && !glGetError());
    }
    assert(c.vram_backing_hits==12 && c.vram_backing_creates==3);
    // A newer incarnation must not resurrect its retired old registration.
    uint64_t *a=p[1].m.args;a[11]++;a[12]=1;p[1].m.op=DG_VRAM_IMPORT_DRAWABLE;
    p[1].m.bytes=ram?sizeof(p[1].pages):0;a[14]=ram?4:0;
    if(ram)for(unsigned page=0;page<4;++page)p[1].pages[page]=0x10000000u+Bytes+page*4096;
    struct DGReply reply={0};assert(!DGVRAMTransfer(&c,&p[1].m,&reply) && reply.result==1);
    struct DGMessage old=p[1].m;old.bytes=0;old.args[14]=0;old.args[11]--;old.args[12]=100;
    struct DGVRAMDrawableBacking *active=c.vram;memset(&reply,0,sizeof(reply));
    assert(!DGVRAMTransfer(&c,&old,&reply) && !reply.result && c.vram==active);
    for(unsigned slot=3;slot<Count;++slot){
        memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p[slot].m,&reply) && reply.result==1);
        assert(c.vram_spare_bytes<=DG_VRAM_SPARE_BYTES);
    }
    for(unsigned i=0;i<3;++i)assert(!c.vram_spares[i] || c.vram_spares[i]->object!=1000);
    DGDestroyDrawables(&c);assert(!c.vram && !c.vram_spare_bytes && !c.vram_copy_context);
    for(unsigned i=0;i<3;++i)assert(!c.vram_spares[i]);
    // Exercise the byte bound separately with inert registrations (no GPU
    // allocation): two 20-MiB spares must not fit the 32-MiB spare budget.
    for(unsigned i=0;i<3;++i){
        struct DGVRAMDrawableBacking *b=calloc(1,sizeof(*b));assert(b);
        b->length=20*1024*1024;DGVRAMRememberBacking(&c,b);
        assert(c.vram_spare_bytes==b->length && c.vram_spares[0]==b && !c.vram_spares[1]);
    }
    DGVRAMDelete(&c);assert(!c.vram_spare_bytes);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    printf("PASS bounded backing reuse: 12 alternate imports, CPU-newer data, completed sparse GPU writes, stale epoch/incarnation, count/byte eviction and teardown RAM=%d\n",ram);
}
static void benchmarkStagedRestore(int width,int height,int priority)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint formats=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&formats) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,width,height));
    unsigned pitch=((unsigned)width+7u)&~7u;
    size_t length=((size_t)pitch*height*4+4095u)&~(size_t)4095u;
    struct {struct DGMessage m;struct DGVRAMRect rect;} p={0};
    uint64_t *a=p.m.args;a[1]=length;a[2]=width;a[3]=height;a[4]=pitch*4;
    a[7]=width;a[8]=height;a[10]=97;a[11]=1;a[12]=1;
    p.rect=(struct DGVRAMRect){0,0,width,height};
    uint32_t *pixels=(void *)(uintptr_t)dg_vram.address;
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    for(unsigned run=0;run<4;++run){
        uint64_t imports[2]={0},publishes[2]={0};unsigned counts[2]={0};
        for(unsigned i=0;i<40;++i){
            unsigned route=i%4==1 || i%4==2;dg_vram_staged_restore=priority?0:(int)route;
            if(priority){
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
                // Explicit public native request, outside the timed region.
                // The real transfer/completion/CPU checks below are unchanged.
                GLint request=route?kCGLCPContextPriorityRequestHigh:kCGLCPContextPriorityRequestNormal;
                assert(!CGLSetParameter(c.cgl,kCGLCPContextPriorityRequest,&request));
                if(c.vram_copy_context)assert(!CGLSetParameter(c.vram_copy_context,kCGLCPContextPriorityRequest,&request));
#else
                fputs("Context priority experiment needs a 10.10+ host SDK/runtime\n",stderr);exit(2);
#endif
            }
            uint32_t color=0xff000000u|((run*40+i+1)*UINT32_C(0x010307)&0xffffffu);
            for(size_t n=0;n<length/4;++n)pixels[n]=color;
            p.m.op=DG_VRAM_IMPORT_DRAWABLE;p.m.bytes=0;++a[12];struct DGReply reply={0};
            uint64_t start=mach_absolute_time();
            assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
            uint64_t imported=mach_absolute_time()-start;
            // Do not clear away all imported pixels: the measured GPU work
            // must actually consume the snapshot, not a dead overwritten copy.
            glEnable(GL_SCISSOR_TEST);glScissor(1,1,8,8);glClearColor(0,1,0,1);
            glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
            p.m.op=DG_VRAM_PUBLISH_DRAWABLE;p.m.bytes=sizeof(p.rect);++a[12];
            memset(&reply,0,sizeof(reply));start=mach_absolute_time();
            assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1 && !reply.error);
            uint64_t published=mach_absolute_time()-start;
            assert(((volatile uint32_t *)pixels)[0]==color);
            assert(((volatile uint32_t *)pixels)[(height-1)*pitch+width-1]==color);
            assert(((volatile uint32_t *)pixels)[2*pitch+2]==0xff00ff00);
            if(i>=8){imports[route]+=imported;publishes[route]+=published;++counts[route];}
        }
        for(unsigned route=0;route<2;++route){
            double scale=(double)clock.numer/clock.denom/1000/counts[route];
            printf("paired %s=%u size=%dx%d run=%u samples=%u import-us=%.3f publish-us=%.3f pair-us=%.3f\n",
                priority?"context-priority-high":"staged-restore",route,width,height,run,counts[route],imports[route]*scale,publishes[route]*scale,
                (imports[route]+publishes[route])*scale);
        }
    }
    assert(!glGetError());DGDestroyDrawables(&c);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
}
int main(int argc,char **argv)
{
    // Preserve the historical comparison modes; explicitly exercise the
    // production publish-only path rather than silently changing --fence.
    dg_vram_draw_publish=0;
    int selected=1,compare=0,compareCopy=0,compareWait=0,patchOnly=argc==2 && !strcmp(argv[1],"--patch-only");
    if(patchOnly){}
    else if(argc==2 && !strcmp(argv[1],"--copytex")){
        dg_vram_copytex=1;dg_vram_draw_publish=1;dg_vram_direct_copy_wait=1;
    }
    else if(argc==2 && !strcmp(argv[1],"--staged-restore")){
        dg_vram_staged_restore=1;dg_vram_draw_publish=1;dg_vram_direct_copy_wait=1;
    }
    else if(argc==2 && !strcmp(argv[1],"--compare-staged-restore")){
        dg_vram_staged_restore=1;dg_vram_draw_publish=1;dg_vram_direct_copy_wait=1;
    }
    else if(argc==2 && !strcmp(argv[1],"--compare-window-staged-restore")){
        dg_vram_staged_restore=1;dg_vram_draw_publish=1;dg_vram_direct_copy_wait=1;
    }
    else if(argc==2 && !strcmp(argv[1],"--compare-priority")){
        dg_vram_draw_publish=1;dg_vram_direct_copy_wait=1;
    }
    else if(argc==2 && !strcmp(argv[1],"--production-publish"))dg_vram_draw_publish=1;
    else if(argc==2 && !strcmp(argv[1],"--production-direct-wait")){dg_vram_draw_publish=1;dg_vram_direct_copy_wait=1;}
    else if(argc==2 && !strcmp(argv[1],"--compare-production-wait")){dg_vram_draw_publish=1;dg_vram_direct_copy_wait=1;compare=compareWait=1;}
    else if(argc==2 && !strcmp(argv[1],"--finish"))selected=0;
    else if(argc==2 && !strcmp(argv[1],"--iosurface"))vim_vram_iosurface=1;
    else if(argc==2 && !strcmp(argv[1],"--iosurface-flip-dest")){vim_vram_iosurface=1;vim_vram_flip_destination=1;}
    else if(argc==2 && !strcmp(argv[1],"--draw-source"))vim_vram_draw_source=1;
    else if(argc==2 && !strcmp(argv[1],"--iosurface-draw-source")){vim_vram_iosurface=1;vim_vram_draw_source=1;}
    else if(argc==2 && !strcmp(argv[1],"--cache-flush"))vim_vram_cache_flush=1;
    else if(argc==2 && !strcmp(argv[1],"--iosurface-draw-copy")){vim_vram_iosurface=1;vim_vram_copy_draw=1;}
    else if(argc==2 && !strcmp(argv[1],"--draw-copy"))vim_vram_copy_draw=1;
    else if(argc==2 && !strcmp(argv[1],"--compare-copy")){vim_vram_copy_draw=1;compare=compareCopy=1;}
    else if(argc==2 && !strcmp(argv[1],"--owned-copy"))vim_vram_copy_draw=2;
    else if(argc==2 && !strcmp(argv[1],"--compare-owned")){vim_vram_copy_draw=2;compare=1;compareCopy=2;}
    else if(argc==2 && !strcmp(argv[1],"--apple-fence"))selected=2;
    else if(argc==2 && !strcmp(argv[1],"--finish-object"))vim_vram_finish_object=1;
    else if(argc==2 && !strcmp(argv[1],"--compare")){selected=-1;compare=1;}
    else if(argc>2 || (argc==2 && strcmp(argv[1],"--fence"))){
        fprintf(stderr,"Usage: %s [--compare-priority|--copytex|--staged-restore|--compare-staged-restore|--production-publish|--production-direct-wait|--compare-production-wait|--patch-only|--fence|--apple-fence|--finish|--finish-object|--compare|--cache-flush|--iosurface|--iosurface-flip-dest|--draw-source|--iosurface-draw-source|--draw-copy|--iosurface-draw-copy|--compare-copy|--owned-copy|--compare-owned]\n",argv[0]);return 2;
    }
    assert(!mach_vm_allocate(mach_task_self(),&dg_vram.address,20*1024*1024,VM_FLAGS_ANYWHERE));
    dg_vram.bytes=20*1024*1024;
    assert(!mach_vm_allocate(mach_task_self(),&dg_ram.address,2*1024*1024,VM_FLAGS_ANYWHERE));
    dg_ram.bytes=2*1024*1024;dg_ram.region_count=1;
    dg_ram.regions[0]=(struct VIMSharedRAMRange){0x10000000,0,2*1024*1024};
    if(patchOnly){
        exercisePatch(0,0);exercisePatch(1,0);exercisePatch(0,1);exercisePatch(1,1);
        VIMVRAMMemoryUnmap(&dg_vram);VIMVRAMMemoryUnmap(&dg_ram);return 0;
    }
    for(unsigned mode=0;mode<3;++mode){
        if(selected>=0 && (int)mode!=selected)continue;
        vim_vram_completion_fence=mode;
        exercise(0,0);exercise(1,0);exercise(0,1);exercise(1,1);
    }
    if(dg_vram_draw_publish){exerciseBackingReuse(0);exerciseBackingReuse(1);}
    if(compare){benchmark(1280,800,compareCopy,compareWait);benchmark(2560,1600,compareCopy,compareWait);}
    if(argc==2 && !strcmp(argv[1],"--compare-staged-restore")){
        benchmarkStagedRestore(2468,82,0);benchmarkStagedRestore(2560,44,0);
    }
    if(argc==2 && !strcmp(argv[1],"--compare-window-staged-restore")){
        benchmarkStagedRestore(784,440,0);benchmarkStagedRestore(1148,718,0);
    }
    if(argc==2 && !strcmp(argv[1],"--compare-priority")){
        benchmarkStagedRestore(2468,82,1);benchmarkStagedRestore(2560,44,1);
    }
    VIMVRAMMemoryUnmap(&dg_vram);VIMVRAMMemoryUnmap(&dg_ram);
    puts("PASS selected VRAM/RAM transfer: crop import/publish, flip, sparse clip holes, CPU-newer preservation, GL state, stale identity/epoch rejection and cross-view RAM cache reuse");return 0;
}
