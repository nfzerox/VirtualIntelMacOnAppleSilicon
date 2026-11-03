/* Explicit instrumented-executor A/B, not a shipped configuration API.
 * No context/window/surface is created. Normal executors do not expose the
 * capability or control. Counts are cumulative ATTEMPTS, not GPU/FPS.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include "../PassthroughOpenGL/direct-command-profile.h"
int main(int argc,char **argv)
{
    uint32_t flags=UINT32_MAX;
    int recycling=argc==2 && !strncmp(argv[1],"recycle-",8);
    if(argc!=2){fputs("Use status, off, source, fbo, on, recycle-status, recycle-off, or recycle-on\n",stderr);return 2;}
    if(!strcmp(argv[1],"off"))flags=0;
    else if(!strcmp(argv[1],"source"))flags=1;
    else if(!strcmp(argv[1],"fbo"))flags=2;
    else if(!strcmp(argv[1],"on"))flags=3;
    else if(!strcmp(argv[1],"recycle-off"))flags=0;
    else if(!strcmp(argv[1],"recycle-on"))flags=1;
    else if(!strcmp(argv[1],"recycle-status"))flags=UINT32_MAX;
    else if(strcmp(argv[1],"status"))return 2;
    struct DGConnection *connection=calloc(1,sizeof(*connection));if(!connection)return 2;
    connection->refs=1;connection->fd=-1;
    pthread_mutex_init(&connection->lock,NULL);pthread_mutex_init(&connection->map_lock,NULL);
    pthread_mutex_init(&connection->backing_lock,NULL);
    struct DGContext c={.connection=connection};int good=!DGConnect(connection);
    if(good){
        struct DGPacket p=DGPacketNew(&c,DG_HELLO);
        p.message.args[0]=DG_MAGIC;p.message.args[1]=DG_VERSION;
        DGPacketPointer(&p,2,DG_LAYOUT,sizeof(DG_LAYOUT),0);
        struct DGReply r=DGSubmit(&c,&p,1);
        good=p.valid_reply && !r.error && !!(r.result&(recycling?DG_CAP_SNAPSHOT_RECYCLE_POLICY:DG_CAP_SNAPSHOT_IMPORT_POLICY));DGPacketFree(&p);
    }
    if(good && recycling){
        struct DGSnapshotRecyclePolicyReport report={0};
        struct DGPacket p=DGPacketNew(&c,DG_COMMAND_PROFILE);p.message.flags=DG_OUTPUT_ONLY;
        DGPacketPointer(&p,0,&report,sizeof(report),1);p.message.args[0]=5;p.message.args[1]=flags;
        struct DGReply r=DGSubmit(&c,&p,1);good=p.valid_reply && !r.error;
        if(good){
            memcpy(&report,p.data,sizeof(report));good=report.version==1 && report.flags<=1 &&
                (flags==UINT32_MAX || report.flags==flags);
            if(good)printf("snapshot_recycle=%u allocations=%llu reuses=%llu consumer_fences=%llu reuse_waits=%llu foreign_refusals=%llu tracking_refusals=%llu\n",
                report.flags,(unsigned long long)report.allocations,(unsigned long long)report.reuses,
                (unsigned long long)report.consumer_fences,(unsigned long long)report.reuse_waits,
                (unsigned long long)report.foreign_refusals,(unsigned long long)report.tracking_refusals);
        }
        DGPacketFree(&p);
    }else if(good){
        struct DGSnapshotImportPolicyReport report={0};
        struct DGPacket p=DGPacketNew(&c,DG_COMMAND_PROFILE);p.message.flags=DG_OUTPUT_ONLY;
        DGPacketPointer(&p,0,&report,sizeof(report),1);p.message.args[0]=4;p.message.args[1]=flags;
        struct DGReply r=DGSubmit(&c,&p,1);good=p.valid_reply && !r.error;
        if(good){
            memcpy(&report,p.data,sizeof(report));good=report.version==1 && report.flags<=3 &&
                (flags==UINT32_MAX || report.flags==flags);
            if(good)printf("snapshot_policy=%u source_reuse_attempts=%llu source_import_attempts=%llu framebuffer_allocations=%llu\n",
                report.flags,(unsigned long long)report.source_reuse_attempts,
                (unsigned long long)report.source_import_attempts,(unsigned long long)report.framebuffer_allocations);
        }
        DGPacketFree(&p);
    }
    if(!good)fputs("Snapshot research policy unavailable, profiler active, or request rejected.\n",stderr);
    DGConnectionRelease(connection);return !good;
}
