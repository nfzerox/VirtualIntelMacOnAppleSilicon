/* Explicit control of a separately compiled research executor. Connect only
 * to the existing device/stream; no native context, window or surface is
 * created. No debugger, extra QMP owner, polling or pixel collection. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include "../PassthroughOpenGL/direct-command-profile.h"
int main(int argc,char **argv)
{
    int arm=argc==3 && !strcmp(argv[1],"start"),stop=argc==2 && !strcmp(argv[1],"stop");
    int read=argc==2 && !strcmp(argv[1],"read");char *end=NULL;
    unsigned long duration=arm?strtoul(argv[2],&end,10):0;
    if((!arm&&!stop&&!read)||(arm&&(!duration||duration>60000||!*argv[2]||*end))){
        fputs("Use start MILLISECONDS (1--60000), stop, or read\n",stderr);return 2;
    }
    struct DGConnection *connection=calloc(1,sizeof(*connection));if(!connection)return 2;
    connection->refs=1;connection->fd=-1;
    pthread_mutex_init(&connection->lock,NULL);pthread_mutex_init(&connection->map_lock,NULL);
    pthread_mutex_init(&connection->backing_lock,NULL);
    struct DGContext c={.connection=connection};int good=!DGConnect(connection);
    if(good){
        struct DGPacket p=DGPacketNew(&c,DG_HELLO);
        p.message.args[0]=DG_MAGIC;p.message.args[1]=DG_VERSION;
        DGPacketPointer(&p,2,DG_LAYOUT,sizeof(DG_LAYOUT),0);
        struct DGReply r=DGSubmit(&c,&p,1);good=p.valid_reply&&!r.error&&!!(r.result&DG_CAP_COMMAND_PROFILE);
        DGPacketFree(&p);
    }
    struct DGCommandProfileReport *report=read?calloc(1,sizeof(*report)):NULL;
    if(read&&!report)good=0;
    if(good){
        struct DGPacket p=DGPacketNew(&c,DG_COMMAND_PROFILE);p.message.args[0]=arm?1:stop?2:3;
        if(arm)p.message.args[1]=duration;
        if(read){p.message.flags=DG_OUTPUT_ONLY;DGPacketPointer(&p,0,report,sizeof(*report),1);
            /* This command's body is the fixed report, not a pointer argument. */
            p.message.args[0]=3;
        }
        struct DGReply r=DGSubmit(&c,&p,1);good=p.valid_reply&&!r.error;
        if(good&&read)memcpy(report,p.data,sizeof(*report));
        if(good&&!read)printf("profile_%s generation=%llu duration_ms=%lu\n",arm?"started":"stopped",(unsigned long long)r.result,duration);
        DGPacketFree(&p);
    }
    if(good&&read){
        good=report->version==1&&report->numer&&report->denom&&report->ended>=report->started;
        if(good){
            double us=(double)report->numer/report->denom/1000.0;
            printf("PROFILE generation=%llu duration_ms=%.3f excluded_boundary_spans=%llu\n",
                (unsigned long long)report->generation,(report->ended-report->started)*us/1000.0,
                (unsigned long long)report->excluded);
            puts("COMMAND opcode calls inclusive_us maximum_us payload_bytes");
            puts("COMPACT 10057 counts structurally accepted envelopes/encoded bytes only; time0, decoded native commands retain their own rows");
            for(unsigned i=0;i<DG_PROFILE_ROWS;++i){
                const struct DGCommandProfileRow *r=&report->commands[i];if(!r->calls)continue;
                uint32_t op=i<974?i:i==DG_PROFILE_UNKNOWN?UINT32_MAX:0x10000+i-974;
                printf("%x %llu %.3f %.3f %llu\n",op,(unsigned long long)r->calls,r->ticks*us,r->maximum*us,(unsigned long long)r->bytes);
            }
            puts("NESTED_HOST_ROWS 100f0=backing-subscribe 100f1=backing-retire 100f2=completion-wait 100f3=completion-delete; overlap commands/batches, NOT additive");
            printf("NESTED batches=%llu inclusive_us=%.3f maximum_us=%.3f input_bytes=%llu\n",
                (unsigned long long)report->batches.calls,report->batches.ticks*us,report->batches.maximum*us,(unsigned long long)report->batches.bytes);
            printf("FAST_READS calls=%llu inclusive_us=%.3f maximum_us=%.3f bytes=%llu\n",
                (unsigned long long)report->fast_reads.calls,report->fast_reads.ticks*us,report->fast_reads.maximum*us,(unsigned long long)report->fast_reads.bytes);
        }
    }
    if(!good)fputs("Explicit research command profiling unavailable, active, or rejected; no rendering policy changed.\n",stderr);
    free(report);DGConnectionRelease(connection);return good?0:1;
}
