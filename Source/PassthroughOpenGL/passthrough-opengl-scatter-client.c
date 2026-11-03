/* Prepare validated task ranges before submitting the native read. No guest
 * image is allocated, and no caller address crosses the host GL protocol. */
int DGSupportsScatterReads(void)
{
    return dg_current && dg_current->connection->device && dg_current->connection->scatter_supported &&
        (dg_current->connection->capabilities&DG_CAP_DRAWABLE_READ_RECTS);
}
int DGSupportsRAMScatterReads(void)
{ return DGSupportsScatterReads() && dg_current->connection->ram_scatter_supported; }
int DGSupportsMaskedScatterReads(void)
{ return DGSupportsRAMScatterReads() && dg_current->connection->masked_scatter_supported; }
static int DGReadDrawableRectPrepared(const void *prepare,size_t size,int x,int y,int width,int height)
{
    struct DGContext *c=dg_current;
    uint64_t bytes=(uint64_t)width*height*4;
    struct DGConnection *connection=c->connection;pthread_mutex_lock(&connection->lock);
#if VIM_GL_SHARED_ONLY
    // The native Tiger Rosetta namespace lacks the optional Mach-clock
    // diagnostics. Keep real transport-error reporting, not clock imports
    // which prevent a renderer from loading even with tracing disabled.
    const int timing=0;
#else
    int timing=getenv("VIM_PASSTHROUGH_OPENGL_TRANSPORT_DIAGNOSTICS")!=NULL;
#endif
    uint64_t started=timing?mach_absolute_time():0,prepared=0,submitted=0,received=0;
    struct VIMGLScatterResult result={0};size_t outsize=sizeof(result);
    IOReturn error=IOConnectCallStructMethod(connection->device,kIOVM2DDirectGLScatter,prepare,size,&result,&outsize);
    if(timing)prepared=mach_absolute_time();
    if(error || outsize!=sizeof(result) || result.count || !result.token) {
        if(getenv("VIM_PASSTHROUGH_OPENGL_TRANSPORT_DIAGNOSTICS"))VIMGLStderr("scatter prepare error=%x input=%lu output=%lu count=%d token-null=%d\n",
            error,(unsigned long)size,(unsigned long)outsize,result.count,!result.token);
        int fallback=error==kIOReturnUnsupported || error==kIOReturnNoMemory || error==kIOReturnNoResources;
        if(!fallback)connection->dead=1;
        pthread_mutex_unlock(&connection->lock);
        if(fallback)return -1;
        c->error=GL_INVALID_OPERATION;return 0;
    }
    uint32_t token=result.token;
    struct DGMessage command={0};command.op=DG_READ_DRAWABLE_RECT;command.context=c->id;
    command.flags=DG_REPLY|DG_OUTPUT_ONLY;command.bytes=(uint32_t)bytes;
    command.args[0]=width;command.args[1]=height;command.args[2]=x;command.args[3]=y;
    struct DGReply reply={0};
    int failed=connection->dead || DGDrain(connection) || DGConnectionTransfer(connection,&command,sizeof(command),1) ||
        DGConnectionTransfer(connection,&reply,sizeof(reply),0) || reply.bytes!=bytes;
    if(timing)submitted=mach_absolute_time();
    if(!failed && reply.error)failed=DGDiscardReply(connection,reply.bytes);
    else if(!failed) {
        struct VIMGLScatterRequest receive={1,0,token,(uint32_t)bytes};outsize=sizeof(result);
        error=IOConnectCallStructMethod(connection->device,kIOVM2DDirectGLScatter,&receive,sizeof(receive),&result,&outsize);
        if(error && getenv("VIM_PASSTHROUGH_OPENGL_TRANSPORT_DIAGNOSTICS"))VIMGLStderr("scatter receive error=%x output=%lu count=%d\n",error,(unsigned long)outsize,result.count);
        failed=error || outsize!=sizeof(result) || result.count!=(int32_t)bytes || result.token!=token;
        if(!failed)connection->direct_read_bytes+=bytes;
    }
    if(timing)received=mach_absolute_time();
    struct VIMGLScatterRequest release={2,0,token,0};outsize=sizeof(result);
    error=IOConnectCallStructMethod(connection->device,kIOVM2DDirectGLScatter,&release,sizeof(release),&result,&outsize);
    if(error || outsize!=sizeof(result) || result.count || result.token)failed=1;
    if(timing) {
        static unsigned traces;
        if(__sync_fetch_and_add(&traces,1)<8) {
            mach_timebase_info_data_t clock;mach_timebase_info(&clock);
            double ms=(double)clock.numer/clock.denom/1e6;
            VIMGLStderr("scatter timing bytes=%llu prepare=%.3f command=%.3f receive=%.3f release=%.3f ms failed=%d\n",
                (unsigned long long)bytes,(prepared-started)*ms,(submitted-prepared)*ms,(received-submitted)*ms,
                (mach_absolute_time()-received)*ms,failed);
        }
    }
    if(failed) { connection->dead=1;c->error=GL_INVALID_OPERATION; }
    else if(reply.error)c->error=reply.error;
    pthread_mutex_unlock(&connection->lock);return !failed && !reply.error;
}
static int DGValidReadRows(void *pixels,size_t stride,int x,int y,int width,int height,unsigned maximum_rows)
{
    struct DGContext *c=dg_current;
    uint64_t row=width>0?(uint64_t)width*4:0,bytes=row*(height>0?height:0);
    if(!c || !pixels || x<0 || y<0 || width<1 || height<1 || height>(int)maximum_rows ||
       (int64_t)x+width>c->width || (int64_t)y+height>c->height || bytes>DG_MAX_BYTES || stride<row ||
       (height>1 && stride>(SIZE_MAX-row)/(height-1)))return 0;
    size_t span=(size_t)(height-1)*stride+(size_t)row;
    return span<=VIM_GL_SCATTER_MAX_BYTES && (uintptr_t)pixels<=UINTPTR_MAX-span;
}
int DGReadDrawableRectRows(void *pixels,size_t stride,int reverse,int x,int y,int width,int height)
{
    unsigned maximum_rows=DGSupportsRAMScatterReads()?VIM_GL_RAM_SCATTER_MAX_ROWS:VIM_GL_SCATTER_MAX_RANGES;
    if(!DGSupportsScatterReads() || !DGValidReadRows(pixels,stride,x,y,width,height,maximum_rows))return -1;
    struct { struct VIMGLScatterRequest header;struct VIMGLScatterRows rows; } prepare={
        {0,(uint32_t)height,0,(uint32_t)width*height*4},{(uintptr_t)pixels,stride,(uint32_t)width*4,!!reverse}};
    _Static_assert(sizeof(prepare)==40,"fixed in-band row preparation in both client widths");
    return DGReadDrawableRectPrepared(&prepare,sizeof(prepare),x,y,width,height);
}
int DGReadDrawableRectMasked(void *pixels,size_t stride,int reverse,int x,int y,int width,int height,
    const struct DGReadSpan *spans,unsigned count)
{
    if(!DGSupportsMaskedScatterReads() || !spans || !count || count>VIM_GL_MASK_MAX_SPANS ||
       !DGValidReadRows(pixels,stride,x,y,width,height,VIM_GL_RAM_SCATTER_MAX_ROWS))return -1;
    uint32_t bytes=(uint32_t)width*height*4,end=0;
    for(unsigned i=0;i<count;++i) {
        if(!spans[i].bytes || (spans[i].offset&3) || (spans[i].bytes&3) || spans[i].offset<end ||
           spans[i].offset>bytes || spans[i].bytes>bytes-spans[i].offset)return -1;
        end=spans[i].offset+spans[i].bytes;
    }
    struct { struct VIMGLScatterRequest header;struct VIMGLScatterMask mask; } prepare={
        {3,count,0,bytes},{{(uintptr_t)pixels,stride,(uint32_t)width*4,!!reverse},(uint32_t)height,0,(uintptr_t)spans}};
    _Static_assert(sizeof(prepare)==56,"fixed in-band masked preparation in both client widths");
    _Static_assert(sizeof(struct DGReadSpan)==sizeof(struct VIMGLScatterSpan),"identical fixed span ABI");
    return DGReadDrawableRectPrepared(&prepare,sizeof(prepare),x,y,width,height);
}
/* Tight canonical rows, gathered natively from a pinned pitched allocation.
 * A single GL import is ordered after the existing command batch. The caller
 * may unlock/reuse its pixels as soon as this synchronous stream write ends. */
static int DGUploadMessageRows(struct DGContext *c,const struct DGMessage *command,
    const void *pixels,unsigned row_bytes,unsigned height,size_t stride,int reverse)
{
    if(!c || !c->connection->device || !c->connection->row_upload_supported ||
       !pixels || !row_bytes || !height || height>VIM_GL_RAM_SCATTER_MAX_ROWS || stride<row_bytes ||
       (uint64_t)row_bytes*height>VIM_GL_SCATTER_MAX_BYTES ||
       (height>1 && stride>(SIZE_MAX-row_bytes)/(height-1)))return -1;
    size_t span=(height-1)*stride+row_bytes;
    if(span>VIM_GL_SCATTER_MAX_BYTES || (uintptr_t)pixels>UINTPTR_MAX-span)return -1;
    uint32_t bytes=row_bytes*height;
    if(command->bytes!=bytes || command->flags)return -1;
    struct { struct VIMGLScatterRequest header;struct VIMGLScatterRows rows; } prepare={
        {4,height,0,bytes},{(uintptr_t)pixels,stride,row_bytes,!!reverse}};
    struct DGConnection *connection=c->connection;pthread_mutex_lock(&connection->lock);
    if(connection->dead) { pthread_mutex_unlock(&connection->lock);c->error=GL_INVALID_OPERATION;return 0; }
    struct VIMGLScatterResult result={0};size_t size=sizeof(result);
    if(connection->row_upload_transaction_supported) {
        struct { struct VIMGLScatterRequest header;struct VIMGLScatterUpload upload;struct DGMessage command; } transaction={
            {6,height,0,bytes},{{(uintptr_t)pixels,stride,row_bytes,!!reverse},sizeof(*command),0},*command};
        _Static_assert(sizeof(transaction)==sizeof(struct VIMGLScatterRequest)+sizeof(struct VIMGLScatterUpload)+sizeof(struct DGMessage),
            "identical in-band upload transaction in both client widths");
        if(DGDrain(connection)) {
            connection->dead=1;c->error=GL_INVALID_OPERATION;pthread_mutex_unlock(&connection->lock);return 0;
        }
        IOReturn error=IOConnectCallStructMethod(connection->device,kIOVM2DDirectGLScatter,
            &transaction,sizeof(transaction),&result,&size);
        if(error || size!=sizeof(result) || result.count!=(int32_t)bytes || result.token) {
            // The driver returns these only before submitting the prefix.
            int fallback=error==kIOReturnUnsupported || error==kIOReturnNoMemory || error==kIOReturnNoResources;
            if(!fallback){connection->dead=1;c->error=GL_INVALID_OPERATION;}
            pthread_mutex_unlock(&connection->lock);return fallback?-1:0;
        }
        connection->direct_write_bytes+=bytes;connection->row_upload_bytes+=bytes;
        connection->row_upload_source_bytes+=span;
        pthread_mutex_unlock(&connection->lock);return 1;
    }
    IOReturn error=IOConnectCallStructMethod(connection->device,kIOVM2DDirectGLScatter,&prepare,sizeof(prepare),&result,&size);
    if(error || size!=sizeof(result) || result.count || !result.token) {
        int fallback=error==kIOReturnUnsupported || error==kIOReturnNoMemory || error==kIOReturnNoResources;
        if(!fallback) { connection->dead=1;c->error=GL_INVALID_OPERATION; }
        pthread_mutex_unlock(&connection->lock);return fallback?-1:0;
    }
    uint32_t token=result.token;
    int failed=DGDrain(connection) || DGConnectionTransfer(connection,(void *)command,sizeof(*command),1);
    if(!failed) {
        struct VIMGLScatterRequest send={5,0,token,bytes};size=sizeof(result);
        error=IOConnectCallStructMethod(connection->device,kIOVM2DDirectGLScatter,&send,sizeof(send),&result,&size);
        failed=error || size!=sizeof(result) || result.count!=(int32_t)bytes || result.token!=token;
        if(!failed) {
            connection->direct_write_bytes+=bytes;connection->row_upload_bytes+=bytes;
            connection->row_upload_source_bytes+=span;
        }
    }
    struct VIMGLScatterRequest release={2,0,token,0};size=sizeof(result);
    error=IOConnectCallStructMethod(connection->device,kIOVM2DDirectGLScatter,&release,sizeof(release),&result,&size);
    if(error || size!=sizeof(result) || result.count || result.token)failed=1;
    if(failed) { connection->dead=1;c->error=GL_INVALID_OPERATION; }
    pthread_mutex_unlock(&connection->lock);return !failed;
}
static int DGUploadDrawableRows(const void *pixels,int x,int y,int width,int height,size_t stride,int top_down,uint32_t operation)
{
    struct DGContext *c=dg_current;
    if(!DGValidReadRows((void *)pixels,stride,x,y,width,height,VIM_GL_RAM_SCATTER_MAX_ROWS))return -1;
    struct DGMessage command={0};command.op=operation;command.context=c->id;command.bytes=(uint32_t)width*height*4;
    command.args[0]=width;command.args[1]=height;command.args[2]=x;command.args[3]=y;
    return DGUploadMessageRows(c,&command,pixels,(unsigned)width*4,(unsigned)height,stride,top_down);
}
static int DGImportDrawableRows(const void *pixels,int width,int height,size_t stride,int top_down)
{ return DGUploadDrawableRows(pixels,0,0,width,height,stride,top_down,DG_IMPORT_DRAWABLE); }
static int DGImportDrawablePatchRows(const void *pixels,int x,int y,int width,int height,size_t stride,int top_down)
{ return DGUploadDrawableRows(pixels,x,y,width,height,stride,top_down,DG_IMPORT_DRAWABLE_PATCH); }
