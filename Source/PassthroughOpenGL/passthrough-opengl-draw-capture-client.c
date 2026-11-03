/* Per-draw union of client-memory spans. Only overlapping/adjacent intervals
 * merge: never read a gap, retain an application pointer, rebase an index or
 * change gl_VertexID. Each descriptor still exposes exactly its old span.
 * Existing host packet bounds already permit overlapping descriptor ranges.
 */
struct DGDrawCaptureRange {uintptr_t begin,end;uint64_t wire;};
struct DGDrawCapturePlan {struct DGDrawCaptureRange ranges[DG_VERTEX_SLOTS];unsigned count;};
static volatile int dg_draw_capture_coalescing=1;
static int DGDrawCaptureAdd(struct DGDrawCapturePlan *plan,const void *pointer,size_t bytes)
{
    uintptr_t begin=(uintptr_t)pointer;
    if(!pointer || !bytes || bytes>UINTPTR_MAX-begin || plan->count>=DG_VERTEX_SLOTS)return 0;
    unsigned i=plan->count++;
    while(i && plan->ranges[i-1].begin>begin){plan->ranges[i]=plan->ranges[i-1];--i;}
    plan->ranges[i]=(struct DGDrawCaptureRange){begin,begin+bytes,0};return 1;
}
static void DGDrawCaptureMerge(struct DGDrawCapturePlan *plan)
{
    unsigned used=0;
    for(unsigned i=0;i<plan->count;++i){
        struct DGDrawCaptureRange next=plan->ranges[i];
        if(used && next.begin<=plan->ranges[used-1].end){
            if(next.end>plan->ranges[used-1].end)plan->ranges[used-1].end=next.end;
        }else plan->ranges[used++]=next;
    }
    plan->count=used;
}
static uint64_t DGDrawCaptureOffset(const struct DGDrawCapturePlan *plan,const void *pointer,size_t bytes)
{
    uintptr_t begin=(uintptr_t)pointer;
    for(unsigned i=0;i<plan->count;++i){
        const struct DGDrawCaptureRange *range=&plan->ranges[i];
        if(range->wire && begin>=range->begin && begin<=range->end && bytes<=range->end-begin)
            return range->wire+(begin-range->begin);
    }
    return 0;
}
