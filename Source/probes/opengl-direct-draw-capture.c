/* Byte-for-byte client-span controls plus real native/device pixels. Captured
 * descriptors may overlap; their contents/indices/first/divisors must not
 * change. No FPS inference from the bounded transport microbenchmark. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
#include <sys/mman.h>
static void plans(void)
{
    struct DGDrawCapturePlan p={0};
    assert(DGDrawCaptureAdd(&p,(void*)100,100));
    assert(DGDrawCaptureAdd(&p,(void*)80,30));
    assert(DGDrawCaptureAdd(&p,(void*)200,10));
    assert(DGDrawCaptureAdd(&p,(void*)120,10));
    assert(DGDrawCaptureAdd(&p,(void*)100,100));
    assert(DGDrawCaptureAdd(&p,(void*)400,10));
    DGDrawCaptureMerge(&p);assert(p.count==2 && p.ranges[0].begin==80 && p.ranges[0].end==210 &&
        p.ranges[1].begin==400 && p.ranges[1].end==410);
    p.ranges[0].wire=17;p.ranges[1].wire=153;
    assert(DGDrawCaptureOffset(&p,(void*)100,100)==37);
    assert(!DGDrawCaptureOffset(&p,(void*)209,2));assert(!DGDrawCaptureOffset(&p,(void*)300,1));
    assert(!DGDrawCaptureAdd(&p,(void*)UINTPTR_MAX,1));assert(!DGDrawCaptureAdd(&p,NULL,1));
    assert(!DGDrawCaptureAdd(&p,(void*)10,0));
    p=(struct DGDrawCapturePlan){0};
    for(unsigned i=0;i<DG_VERTEX_SLOTS;++i)assert(DGDrawCaptureAdd(&p,(void*)(uintptr_t)(1000-i*10),5));
    assert(!DGDrawCaptureAdd(&p,(void*)5000,5));DGDrawCaptureMerge(&p);assert(p.count==DG_VERTEX_SLOTS);
    puts("PASS bounded sorted unions: overlapping/nested/identical/adjacent/disjoint spans and pointer overflow");
}
static void builder(void)
{
    const size_t page=(size_t)getpagesize();unsigned char *data=mmap(NULL,page*3,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
    assert(data!=MAP_FAILED && !mprotect(data,page,PROT_READ|PROT_WRITE) &&
        !mprotect(data+page*2,page,PROT_READ|PROT_WRITE));
    for(size_t i=0;i<page;++i){data[i]=(unsigned char)(i*7);data[page*2+i]=(unsigned char)(i*11);}
    struct DGConnection *connection=calloc(1,sizeof(*connection));assert(connection);
    connection->fd=-1;connection->capabilities=DG_CAP_INSTANCED_ARRAYS;
    assert(!pthread_mutex_init(&connection->lock,NULL));
    struct DGContext context={.connection=connection,.id=1};context.vertex=DGVertexState(&context,0);assert(context.vertex);
    uint16_t indices[]={8,9,10,10,11,8};size_t baseline=0;
    for(unsigned scenario=0;scenario<4;++scenario)for(unsigned mode=0;mode<2;++mode){
        memset(context.vertex->arrays,0,sizeof(context.vertex->arrays));
        unsigned slots[]={0,8,9,10,16,17,18};
        for(unsigned i=0;i<7;++i){
            struct DGClientArray *a=&context.vertex->arrays[slots[i]];
            a->enabled=1;a->wire=(struct DGVertexArray){.slot=slots[i],.size=2,.type=GL_FLOAT,.stride=32};
            // Deliberately unsorted/interleaved, with a nested identical span.
            a->pointer=data+(i==6?0:(6-i)*4);
            if(scenario==1 && i==3)a->pointer=data+page*2; // unreadable gap must NEVER be touched
            if(scenario==2 && i==4){a->wire.buffer=9;a->pointer=(void*)32;}
            if(scenario==3 && i>=4){a->divisor=i-3;a->pointer=data+page*2+(i-4)*4;}
        }
        connection->used=0;dg_draw_capture_coalescing=mode;
        DGDrawInstances(&context,GL_TRIANGLES,scenario==0?5:0,6,scenario?GL_UNSIGNED_SHORT:0,
            scenario?indices:NULL,scenario==3?7:1,scenario==3);
        assert(!context.error && !connection->dead && connection->used>=sizeof(struct DGMessage));
        struct DGMessage m;memcpy(&m,connection->batch,sizeof(m));
        const unsigned char *body=connection->batch+sizeof(m);
        assert(m.args[1]==(scenario==0?5:0) && m.args[2]==6 && m.args[8]==7);
        assert(m.bytes+sizeof(m)==connection->used);
        if(scenario)assert(!memcmp(body+m.args[5]-1,indices,sizeof(indices)));
        for(unsigned i=0;i<7;++i){
            struct DGVertexArray a;memcpy(&a,body+m.args[6]-1+i*sizeof(a),sizeof(a));
            const struct DGClientArray *source=&context.vertex->arrays[slots[i]];
            assert(a.slot==source->wire.slot && a.stride==source->wire.stride && a.buffer==source->wire.buffer);
            if(a.buffer){assert(a.pointer==(uintptr_t)source->pointer && !a.bytes);continue;}
            uint64_t vertices=scenario==0?11:12;
            if(source->divisor)vertices=6/source->divisor+1;
            assert(a.bytes==DGVertexSpan(&source->wire,vertices) && a.pointer && a.pointer-1<=m.bytes && a.bytes<=m.bytes-(a.pointer-1));
            assert(!memcmp(body+a.pointer-1,source->pointer,(size_t)a.bytes));
            if(scenario==3){uint32_t divisor;memcpy(&divisor,body+m.args[10]-1+i*4,4);assert(divisor==source->divisor);}
        }
        if(!mode)baseline=connection->used;else assert(connection->used<baseline);
        printf("PASS exact per-attribute bytes/first/indices/mixed VBO/divisors/guarded gap scenario=%u mode=%u bytes=%lu baseline=%lu\n",
            scenario,mode,(unsigned long)connection->used,(unsigned long)baseline);
    }
    dg_draw_capture_coalescing=1;DGDestroyArrays(&context);free(context.draw_scratch);
    pthread_mutex_destroy(&connection->lock);free(connection);assert(!munmap(data,page*3));
}
static void gpu(void)
{
    unsigned char pixels[64*64*4]={0};struct DGContext *c=DGCreateContext(NULL,NULL);assert(c && DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,64,64));
    assert(strstr((const char*)glGetString(GL_RENDERER),"Intel"));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,64,64);glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    struct Vertex {GLfloat position[2],color[4],uv[5][2];} v[4]={
        {{-1,-1},{1,0,0,1},{{0,0}}},{{1,-1},{1,0,0,1},{{1,0}}},
        {{1,1},{1,0,0,1},{{1,1}}},{{-1,1},{1,0,0,1},{{0,1}}}};
    GLushort index[]={0,1,2,2,3,0};
    glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(2,GL_FLOAT,sizeof(v[0]),v[0].position);
    glEnableClientState(GL_COLOR_ARRAY);glColorPointer(4,GL_FLOAT,sizeof(v[0]),v[0].color);
    for(unsigned unit=0;unit<5;++unit){glClientActiveTexture(GL_TEXTURE0+unit);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);glTexCoordPointer(2,GL_FLOAT,sizeof(v[0]),v[0].uv[unit]);}
    for(unsigned mode=0;mode<2;++mode){
        dg_draw_capture_coalescing=mode;glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        for(unsigned i=0;i<20;++i){glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,index);glFinish();assert(!glGetError());}
        unsigned char p[4]={0};glReadPixels(31,30,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        GLenum error=glGetError();printf("native mode=%u pixel=%u,%u,%u,%u error=%x\n",mode,p[0],p[1],p[2],p[3],error);fflush(stdout);
        assert(p[0]==255 && !p[1] && !p[2] && p[3]==255 && !error);
        glDrawElements(0xffffffffu,6,GL_UNSIGNED_SHORT,index);assert(glGetError()==GL_INVALID_ENUM && !glGetError());
        printf("PASS actual native interleaved pixels, Finish and real invalid-mode GL error mode=%u\n",mode);
    }
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    for(unsigned trial=0;trial<4;++trial){
        dg_draw_capture_coalescing=trial==1 || trial==2;glFinish();
        uint64_t begin=mach_absolute_time();
        for(unsigned i=0;i<4000;++i)glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,index);
        glFinish();uint64_t end=mach_absolute_time();assert(!glGetError());
        printf("TIMING actual device draws=4000 attributes=7 coalesce=%d submit_through_Finish_ms=%.3f (NOT desktop FPS)\n",
            dg_draw_capture_coalescing,(end-begin)*(double)clock.numer/clock.denom/1e6);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    dg_draw_capture_coalescing=1;DGDestroyContext(c);
}
int main(int argc,char **argv)
{plans();builder();if(argc==2 && !strcmp(argv[1],"--builder-only"))return 0;assert(argc==1);gpu();return 0;}
