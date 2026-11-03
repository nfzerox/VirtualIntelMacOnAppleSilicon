/* Actual encoder bytes + native/device GPU controls. No system renderer
 * replacement, weakened GL error observation or desktop-FPS inference. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include "direct-opcodes.h"
#include <assert.h>
static size_t expand(const unsigned char *in,size_t bytes,unsigned char *out,size_t capacity)
{
    size_t at=0,used=0;
    while(at<bytes){
        assert(sizeof(struct DGMessage)<=bytes-at);struct DGMessage m;memcpy(&m,in+at,sizeof(m));at+=sizeof(m);
        assert(m.bytes<=bytes-at);
        if(m.op==DG_COMPACT_SCALARS){
            size_t end=at+m.bytes;assert(!m.context && !m.flags && m.args[0]==1 && m.args[1]>=2);
            for(uint64_t i=0;i<m.args[1];++i){
                assert(sizeof(struct DGCompactScalar)<=end-at);struct DGCompactScalar r;memcpy(&r,in+at,sizeof(r));at+=sizeof(r);
                assert(r.op<974 && r.words<=16 && r.context && r.words*8<=end-at);
                struct DGMessage plain={.op=r.op,.context=r.context};
                // Independent reference expansion, including every omitted
                // trailing word, not a call to the production decoder.
                for(unsigned word=0;word<r.words;++word)memcpy(plain.args+word,in+at+word*8,8);
                at+=r.words*8;assert(sizeof(plain)<=capacity-used);memcpy(out+used,&plain,sizeof(plain));used+=sizeof(plain);
            }
            assert(at==end);
        }else{
            assert(sizeof(m)+m.bytes<=capacity-used);memcpy(out+used,&m,sizeof(m));used+=sizeof(m);
            memcpy(out+used,in+at,m.bytes);used+=m.bytes;at+=m.bytes;
        }
    }
    assert(at==bytes);return used;
}
static void builder(void)
{
    unsigned char reference[32000],decoded[32000];size_t reference_bytes=0;
    struct DGConnection *c=calloc(1,sizeof(*c));assert(c);c->fd=-1;c->capabilities=DG_CAP_COMPACT_SCALARS;
    pthread_mutex_init(&c->lock,NULL);struct DGContext context={.connection=c,.id=7};
    for(unsigned test=0;test<3;++test){
        c->used=c->scalar_start=c->scalar_compact=0;reference_bytes=0;
        c->compact_commands=c->compact_runs=c->compact_saved_bytes=0;
        c->capabilities=test==2?0:DG_CAP_COMPACT_SCALARS;
        for(unsigned i=0;i<120;++i){
            dg_compact_scalar_override=test==1?(i<20 || i>=40):1;
            struct DGPacket p=DGPacketNew(&context,DG_OP_tex_coord2f);p.message.context=i%2+1;
            p.message.args[0]=i;p.message.args[1]=UINT64_C(0x80000000); // -0 float bits
            if(i%9==0)p.message.args[3]=UINT64_C(0x7ff8000012345678); // NaN payload, no canonicalization
            if(i%13==0)for(unsigned n=0;n<16;++n)p.message.args[n]=UINT64_C(0xfedcba9876543210)+n;
            if(i%7==0){p.message.op=DG_ARRAY_DIVISOR;p.message.args[3]=0;} // raw private boundary
            if(i%11==0){p.message.op=DG_OP_tex_image2D;p.message.bytes=3;p.data=malloc(3);p.capacity=3;memcpy(p.data,"abc",3);}
            assert(sizeof(p.message)+p.message.bytes<=sizeof(reference)-reference_bytes);
            memcpy(reference+reference_bytes,&p.message,sizeof(p.message));reference_bytes+=sizeof(p.message);
            if(p.message.bytes){memcpy(reference+reference_bytes,p.data,p.message.bytes);reference_bytes+=p.message.bytes;}
            DGSubmit(&context,&p,0);assert(!c->dead);DGPacketFree(&p);
        }
        size_t count=expand(c->batch,c->used,decoded,sizeof(decoded));
        assert(count==reference_bytes && !memcmp(reference,decoded,count));
        assert(c->used<=reference_bytes);
        if(test==2)assert(c->used==reference_bytes && !c->compact_commands && !memcmp(reference,c->batch,reference_bytes));
        else assert(c->compact_commands && c->compact_runs && c->compact_saved_bytes==reference_bytes-c->used);
        printf("PASS encoder exact bits/order, mixed odd payload alignment, contexts, wide args, toggle/old-peer test=%u bytes=%lu/%lu\n",
            test,(unsigned long)c->used,(unsigned long)reference_bytes);
    }
    dg_compact_scalar_override=1;pthread_mutex_destroy(&c->lock);free(c);
}
static void gpu(void)
{
    unsigned char pixels[64*64*4]={0};struct DGContext *c=DGCreateContext(NULL,NULL);assert(c);
    assert(DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,64,64));
    const char *renderer=(const char *)glGetString(GL_RENDERER);assert(renderer && strstr(renderer,"Intel"));
    int negotiated=!!(c->connection->capabilities&DG_CAP_COMPACT_SCALARS);
    printf("renderer=%s transport=%s compact_negotiated=%d\n",renderer,DGTransportName(),negotiated);
    dg_common_state_reuse=0; // Native observations, not another local cache proving itself.
    for(unsigned phase=0;phase<4;++phase){
        dg_compact_scalar_override=phase==1 || phase==2;
        uint64_t commands=c->connection->compact_commands,saved=c->connection->compact_saved_bytes;
        for(unsigned i=0;i<24;++i){
            glViewport(0,0,64,64);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glColor4f(1,0,0,1);
            glBegin(GL_QUADS);
            glTexCoord2f(0,-0.f);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
            glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
            unsigned char pixel[4]={0};glReadPixels(31,30,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            assert(pixel[0]==255 && !pixel[1] && !pixel[2] && pixel[3]==255 && !glGetError());
            glEnable(0xffffffffu);assert(glGetError()==GL_INVALID_ENUM && !glGetError());
            glBegin(GL_POINTS);glClear(GL_COLOR_BUFFER_BIT);glEnd();
            assert(glGetError()==GL_INVALID_OPERATION && !glGetError());
        }
        int used=negotiated && dg_compact_scalar_override;
        assert(used?(c->connection->compact_commands>commands && c->connection->compact_saved_bytes>saved):
            (c->connection->compact_commands==commands && c->connection->compact_saved_bytes==saved));
        printf("PASS native pixels/Begin-End/ordered errors phase=%u enabled=%d packed=%llu saved=%llu\n",phase,used,
            (unsigned long long)(c->connection->compact_commands-commands),(unsigned long long)(c->connection->compact_saved_bytes-saved));
    }
    // Force real batch-capacity drains. Both modes finish the actual native
    // queue, not merely their encoder; final state is checked after timing.
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    for(unsigned trial=0;trial<4;++trial){
        dg_compact_scalar_override=trial&1;
        uint64_t commands=c->connection->compact_commands,saved=c->connection->compact_saved_bytes,begin=mach_absolute_time();
        for(unsigned i=0;i<16000;++i){glTexCoord2f((GLfloat)i,1);glColor4f(.25f,.5f,.75f,1);}
        assert(!glGetError());uint64_t end=mach_absolute_time();
        GLfloat color[4]={0};glGetFloatv(GL_CURRENT_COLOR,color);assert(!memcmp(color,(GLfloat[]){.25f,.5f,.75f,1},sizeof(color)) && !glGetError());
        printf("TIMING scalar mode=%d negotiated=%d commands=32000 submit_through_native_observation_ms=%.3f packed=%llu saved=%llu (NOT desktop FPS)\n",
            dg_compact_scalar_override,negotiated,(end-begin)*(double)clock.numer/clock.denom/1e6,
            (unsigned long long)(c->connection->compact_commands-commands),(unsigned long long)(c->connection->compact_saved_bytes-saved));
    }
    dg_compact_scalar_override=1;DGDestroyContext(c);
}
static void core(void)
{
    const int attrs[]={0x56494d02,0x3200,0};unsigned char pixels[32*32*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);assert(c && DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,32,32));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    for(unsigned phase=0;phase<2;++phase){
        dg_compact_scalar_override=phase;uint64_t before=c->connection->compact_commands;
        for(unsigned i=0;i<40;++i){
            glViewport(0,0,32,32);glClearColor(.25,.5,.75,1);glClear(GL_COLOR_BUFFER_BIT);
            unsigned char p[4]={0};glReadPixels(15,15,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
            assert(p[0]>=63 && p[0]<=64 && p[1]>=127 && p[1]<=128 && p[2]>=191 && p[2]<=192 && p[3]==255);
            glEnable(0xffffffffu);assert(glGetError()==GL_INVALID_ENUM && !glGetError());
        }
        int packed=phase && !!(c->connection->capabilities&DG_CAP_COMPACT_SCALARS);
        assert(packed?c->connection->compact_commands>before:c->connection->compact_commands==before);
        printf("PASS actual core-profile pixels/native errors mode=%u packed=%d\n",phase,packed);
    }
    dg_compact_scalar_override=1;DGDestroyContext(c);
}
int main(int argc,char **argv)
{
    builder();if(argc==2 && !strcmp(argv[1],"--builder-only"))return 0;
    assert(argc==1);gpu();core();return 0;
}
