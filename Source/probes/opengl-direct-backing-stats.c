/* One explicit renderer-global metadata snapshot through our own endpoint.
 * Does not attach WindowServer, borrow its surfaces or run a polling service.
 * Independent atomics are not a transactional snapshot; use quiet before/after
 * workloads, never infer FPS or byte savings from these call counts alone. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
int main(int argc,char **argv)
{
    int canonical=argc==2 && !strcmp(argv[1],"--canonical");
    int storage=argc==2 && !strcmp(argv[1],"--storage");
    int writer=argc==2 && !strcmp(argv[1],"--writer-refusals");
    int admission=argc==2 && !strcmp(argv[1],"--writer-admission");
    int gpu_writer=argc==2 && !strcmp(argv[1],"--canonical-writer");
    int errors=argc==2 && !strcmp(argv[1],"--writer-errors");
    int error_fields=argc==2 && !strcmp(argv[1],"--writer-error-fields");
    int surface=argc==2 && !strcmp(argv[1],"--surface-registration");
    int prepared=argc==2 && !strcmp(argv[1],"--prepared-drawable");
    int cpu=argc==2 && !strcmp(argv[1],"--cpu-canonical");
    int scatter=argc==2 && !strcmp(argv[1],"--scatter");
    int gather=argc==2 && !strcmp(argv[1],"--gather");
    int coalescing=argc==2 && !strcmp(argv[1],"--coalescing");
    if(argc!=1 && !canonical && !storage && !writer && !admission && !gpu_writer && !errors && !error_fields && !surface && !prepared && !cpu && !scatter && !gather && !coalescing)return 2;
    unsigned char pixels[64]={0};
    struct DGContext *c=DGCreateContext(NULL,NULL);
    int good=c && DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,4,4) && (storage || DGSupportsBackingRenderScopes());
    uint64_t counts[4]={0};
    if(good){
        struct DGPacket p=DGPacketNew(c,DG_VRAM_STATS);p.message.flags=DG_OUTPUT_ONLY;
        p.message.args[0]=storage?DG_VRAM_STATS_DRAWABLE_STORAGE:
            coalescing?DG_VRAM_STATS_CPU_COALESCING:gather?DG_VRAM_STATS_GATHER_INITIALIZE:scatter?DG_VRAM_STATS_SCATTER_BACKING:cpu?DG_VRAM_STATS_CPU_CANONICAL:prepared?DG_VRAM_STATS_PREPARED_DRAWABLE:surface?DG_VRAM_STATS_SURFACE_REGISTRATION:errors?DG_VRAM_STATS_WRITER_ERRORS:error_fields?DG_VRAM_STATS_WRITER_ERROR_FIELDS:
            gpu_writer?DG_VRAM_STATS_CANONICAL_WRITER:
            writer?DG_VRAM_STATS_WRITER_REFUSALS:admission?DG_VRAM_STATS_WRITER_ADMISSION:
            canonical?DG_VRAM_STATS_CANONICAL_SCOPE:DG_VRAM_STATS_BACKING_SCOPE;p.message.bytes=sizeof(counts);
        p.data=(void *)counts;p.capacity=sizeof(counts);p.borrowed=1;
        struct DGReply reply=DGSubmit(c,&p,1);good=p.valid_reply && !reply.error;
        if(good)memcpy(counts,p.data,sizeof(counts));DGPacketFree(&p);
    }
    if(good)printf(coalescing?
        "{\"groups\":%llu,\"skipped_CPU_copies\":%llu,\"avoided_bytes\":%llu,\"forwarded_waits\":%llu}\n":gather?
        "{\"initialized_resources\":%llu,\"input_bytes\":%llu,\"pending_refusals\":%llu,\"native_failures\":%llu}\n":scatter?
        "{\"plans\":%llu,\"CPU_complete_copies\":%llu,\"copied_bytes\":%llu,\"admitted_scopes\":%llu}\n":cpu?
        "{\"accepted\":%llu,\"pending_rejections\":%llu,\"epoch_rejections\":%llu,\"identity_rejections\":%llu}\n":prepared?
        "{\"reserved\":%llu,\"canonical_resolves\":%llu,\"private_resolves\":%llu,\"unused_retirements\":%llu}\n":surface?
        "{\"imports\":%llu,\"shared_attachments\":%llu,\"retired_private_targets\":%llu,\"live_registrations\":%llu}\n":(errors || error_fields)?
        "{\"state\":%llu,\"missing_ticket\":%llu,\"ticket_identity\":%llu,\"native_wait\":%llu}\n":gpu_writer?
        "{\"canonical_writers\":%llu,\"live_alias_writers\":%llu,\"identity_misses\":%llu,\"epoch_misses\":%llu}\n":writer?
        "{\"attempts\":%llu,\"cache_misses\":%llu,\"foreign_groups\":%llu,\"begin_refusals\":%llu}\n":admission?
        "{\"shape\":%llu,\"ordinary_shared\":%llu,\"live_alias\":%llu,\"registration\":%llu}\n":storage?
        "{\"registrations\":%llu,\"texture_reuses\":%llu,\"texture_retentions\":%llu,\"evictions\":%llu}\n":canonical?
        "{\"attachments\":%llu,\"reuses\":%llu,\"self_publications\":%llu,\"stale_rejections\":%llu}\n":
        "{\"begins\":%llu,\"materializations\":%llu,\"read_only_ends\":%llu,\"republished_ends\":%llu}\n",
        (unsigned long long)counts[0],(unsigned long long)counts[1],
        (unsigned long long)counts[2],(unsigned long long)counts[3]);
    else fputs("Scoped backing statistics unavailable; no workload was changed\n",stderr);
    if(c)DGDestroyContext(c);return good?0:1;
}
