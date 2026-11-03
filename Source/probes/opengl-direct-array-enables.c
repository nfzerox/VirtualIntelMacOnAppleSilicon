/* Compare actual native GL state against the old full-reset algorithm.
 * No guessed Intel alias map: every enable is queried after both paths.
 * The optional renderer ID selects the physical Intel reference explicitly.
 */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
#include <mach/mach_time.h>

static uint32_t state(struct DGHostContext *c)
{
    uint32_t result=0;
    if(c->profile==kCGLOGLPVersion_Legacy){
        const GLenum caps[]={GL_VERTEX_ARRAY,GL_NORMAL_ARRAY,GL_COLOR_ARRAY,
            GL_SECONDARY_COLOR_ARRAY,GL_FOG_COORD_ARRAY};
        for(unsigned i=0;i<5;++i)if(glIsEnabled(caps[i]))result|=UINT32_C(1)<<i;
        GLint active=0;glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&active);
        for(unsigned i=0;i<8;++i){glClientActiveTexture(GL_TEXTURE0+i);
            if(glIsEnabled(GL_TEXTURE_COORD_ARRAY))result|=UINT32_C(1)<<(8+i);}
        glClientActiveTexture((GLenum)active);
    }
    for(unsigned i=0;i<16;++i){GLint enabled=0;glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
        if(enabled)result|=UINT32_C(1)<<(16+i);}
    assert(!glGetError());return result;
}
static void baseline(struct DGHostContext *c,uint32_t mask)
{dg_array_enable_reuse=0;DGHostArrayEnables(c,mask);assert(!c->array_enable_valid);}
static void compare(struct DGHostContext *c,uint32_t old,uint32_t next)
{
    baseline(c,old);dg_array_enable_reuse=1;DGHostArrayEnables(c,old);
    DGHostArrayEnables(c,next);uint32_t actual=state(c);
    baseline(c,old);baseline(c,next);uint32_t expected=state(c);
    if(actual!=expected){fprintf(stderr,"enable mismatch profile=%x old=%08x next=%08x actual=%08x expected=%08x\n",
        c->profile,old,next,actual,expected);abort();}
}
static void pixels(struct DGHostContext *c)
{
    assert(c->profile==kCGLOGLPVersion_Legacy && DGCreateAliases(c,NULL));
    assert(DGSelectDrawable(c,1,32,32));glViewport(0,0,32,32);
    struct {struct DGMessage m;struct DGVertexArray a;GLfloat vertices[6];} p={0};
    p.m.op=DG_DRAW;p.m.bytes=sizeof(p)-sizeof(p.m);p.m.args[0]=GL_TRIANGLES;
    p.m.args[2]=3;p.m.args[6]=1;p.m.args[8]=1;
    p.a=(struct DGVertexArray){.slot=0,.size=2,.type=GL_FLOAT,
        .bytes=sizeof(p.vertices),.pointer=sizeof(p.a)+1};
    const GLfloat v[]={-1,-1,1,-1,0,1};memcpy(p.vertices,v,sizeof(v));
    for(unsigned i=0;i<32;++i){
        dg_array_enable_reuse=1;glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glColor4f(0,1,0,1);assert(!DGHostDraw(c,&p.m));
        unsigned char pixel[4]={0};glReadPixels(16,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        assert(!glGetError() && !pixel[0] && pixel[1]==255 && !pixel[2] && pixel[3]==255);
    }
    mach_timebase_info_data_t timebase;assert(!mach_timebase_info(&timebase));
    for(unsigned phase=0;phase<4;++phase){
        dg_array_enable_reuse=phase==1 || phase==2;
        glFinish();uint64_t began=mach_absolute_time();
        for(unsigned i=0;i<4096;++i){glColor4f(0,(i&1)?1:0,(i&1)?0:1,1);assert(!DGHostDraw(c,&p.m));}
        glFinish();uint64_t elapsed=mach_absolute_time()-began;
        unsigned char pixel[4]={0};glReadPixels(16,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        assert(!glGetError() && !pixel[0] && pixel[1]==255 && !pixel[2] && pixel[3]==255);
        printf("actual_draws=4096 reuse=%d completed_wall_us=%.3f (not desktop FPS)\n",
            dg_array_enable_reuse,elapsed*(double)timebase.numer/timebase.denom/1000.0);
    }
    DGDestroyDrawables(c);DGDestroyAliases(c);
}
static void qualify(unsigned profile,GLint renderer)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0,0,0};
    if(renderer){attrs[5]=kCGLPFARendererID;attrs[6]=(CGLPixelFormatAttribute)renderer;attrs[7]=0;}
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={.profile=profile,.draw_mode=GL_FRONT,.read_mode=GL_FRONT};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));
    const char *name=(const char *)glGetString(GL_RENDERER);printf("profile=%x renderer=%s\n",profile,name);
    assert(name && strstr(name,"Intel"));
    pthread_once(&dg_core_once,DGLoadCoreAPI);
    GLuint vao=0;DGGenHostVertexArrays(&c,1,&vao);assert(vao);DGBindHostVertexArray(&c,vao);
    uint32_t allowed=profile==kCGLOGLPVersion_Legacy?UINT32_C(0xffffff1f):UINT32_C(0xffff0000);
    // Exhaust every single-bit transition and combinations around attribute0.
    for(unsigned i=0;i<32;++i)for(unsigned j=0;j<32;++j){
        uint32_t a=(UINT32_C(1)<<i)&allowed,b=(UINT32_C(1)<<j)&allowed;
        compare(&c,a,b);compare(&c,a|b,a);compare(&c,a|b,b);
    }
    uint32_t random=0x7384b91u;
    for(unsigned i=0;i<1024;++i){random=random*1664525u+1013904223u;uint32_t old=random&allowed;
        random=random*1664525u+1013904223u;compare(&c,old,random&allowed);}
    dg_array_enable_reuse=1;DGHostArrayEnables(&c,allowed);assert(c.array_enable_valid);
    DGBindHostVertexArray(&c,vao);assert(!c.array_enable_valid);
    DGHostArrayEnables(&c,0);assert(state(&c)==0);
    glEnable(0xffffffffu);DGHostArrayEnables(&c,0);
    assert(glGetError()==GL_INVALID_ENUM && !glGetError());
    if(profile==kCGLOGLPVersion_Legacy){
        struct DGReply reply={0};struct DGMessage push={.op=209,.args={GL_CLIENT_VERTEX_ARRAY_BIT}},pop={.op=204};
        assert(!DGExecute(&c,&push,&reply) && !c.array_enable_valid);
        DGHostArrayEnables(&c,allowed);assert(c.array_enable_valid);
        assert(!DGExecute(&c,&pop,&reply) && !c.array_enable_valid);
        DGHostArrayEnables(&c,0);assert(state(&c)==0);pixels(&c);
    }
    DGDeleteHostVertexArrays(&c,1,&vao);assert(!c.array_enable_valid && !glGetError());
    assert(!CGLSetCurrentContext(NULL));CGLDestroyContext(c.cgl);
    puts("PASS4096 native mask transitions, aliasing, VAO/stack invalidation, pending native error and legacy pixels");
}
int main(int argc,char **argv)
{
    assert(argc<=2);GLint renderer=argc==2?(GLint)strtoul(argv[1],NULL,0):0;
    qualify(kCGLOGLPVersion_Legacy,renderer);qualify(kCGLOGLPVersion_3_2_Core,renderer);return 0;
}
