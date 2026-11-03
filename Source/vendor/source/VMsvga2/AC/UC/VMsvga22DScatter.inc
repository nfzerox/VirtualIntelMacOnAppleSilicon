/* Private research method. Never infer physical ownership from a task
 * address: prepare the owning task's complete descriptor, then verify every
 * physical segment. Legacy peers require actual device VRAM; RAM peers also
 * independently validate physical RAM and pin its writable QEMU DMA maps. */
void CLASS::clear_direct_scatter()
{
    if (m_scatter_memory) { m_scatter_memory->complete();m_scatter_memory->release();m_scatter_memory=0; }
    if (m_scatter_segments) { IOFree(m_scatter_segments,m_scatter_capacity*sizeof(VIMGLScatterMemorySegment));m_scatter_segments=0; }
    m_scatter_count=m_scatter_bytes=m_scatter_token=m_scatter_capacity=0;m_scatter_masked=m_scatter_upload=false;
    if(m_scatter_access){if(m_provider)m_provider->returnBackingAccess(&m_scatter_memory);m_scatter_access=false;}
}
static bool VIMAppendMaskedSegment(VIMGLScatterMemorySegment* segments,uint32_t* count,
    uint64_t address,uint32_t bytes,uint32_t kind)
{
    if (!bytes)return true;
    if (*count && segments[*count-1].kind==kind &&
        (kind==2 || segments[*count-1].address+segments[*count-1].bytes==address))
        segments[*count-1].bytes+=bytes;
    else if (*count<VIM_GL_RAM_SCATTER_MAX_RANGES) {
        VIMGLScatterMemorySegment& segment=segments[(*count)++];
        segment.address=address;segment.bytes=bytes;segment.kind=kind;
    } else return false;
    return true;
}
IOReturn CLASS::prepare_direct_mask(void const* input,size_t input_size)
{
    if (!m_direct_masked_scatter_supported)return kIOReturnUnsupported;
    if (m_scatter_memory)return kIOReturnExclusiveAccess;
    VIMGLScatterRequest request;VIMGLScatterMask mask;
    if (input_size<sizeof(request)+sizeof(mask))return kIOReturnBadArgument;
    memcpy(&request,input,sizeof(request));
    memcpy(&mask,static_cast<const char*>(input)+sizeof(request),sizeof(mask));
    const VIMGLScatterRows& rows=mask.rows;
    if (!request.count || request.count>VIM_GL_MASK_MAX_SPANS || request.token ||
        !request.bytes || request.bytes>VIM_GL_SCATTER_MAX_BYTES ||
        input_size!=sizeof(request)+sizeof(mask) || !mask.spans_address ||
        mask.spans_address>UINT64_MAX-request.count*sizeof(VIMGLScatterSpan) ||
        !rows.address || !rows.row_bytes || (rows.row_bytes&3) || rows.reverse>1 ||
        !mask.height || mask.height>VIM_GL_RAM_SCATTER_MAX_ROWS || mask.reserved ||
        rows.stride<rows.row_bytes || (uint64_t)rows.row_bytes*mask.height!=request.bytes ||
        (mask.height>1 && rows.stride>(UINT64_MAX-rows.row_bytes)/(mask.height-1)))return kIOReturnBadArgument;
    uint64_t span=(mask.height-1)*rows.stride+rows.row_bytes;
    if (span>VIM_GL_SCATTER_MAX_BYTES || rows.address>UINT64_MAX-span)return kIOReturnBadArgument;
    const size_t table_bytes=request.count*sizeof(VIMGLScatterSpan);
    IOMemoryDescriptor* table=IOMemoryDescriptor::withAddressRange(mask.spans_address,table_bytes,kIODirectionOut,m_owning_task);
    if (!table)return kIOReturnNoMemory;
    IOReturn table_error=table->prepare();bool table_prepared=!table_error;
    char* entries=!table_error?static_cast<char*>(IOMalloc(table_bytes)):0;
    if (!table_error && !entries)table_error=kIOReturnNoMemory;
    if (!table_error && table->readBytes(0,entries,table_bytes)!=table_bytes)table_error=kIOReturnVMError;
    if (table_prepared)table->complete();table->release();
    if (table_error) { if (entries)IOFree(entries,table_bytes);return table_error; }
    uint32_t end=0;
    // Validate every stream span before pinning or submitting a read. Metadata
    // is the owning task's snapshot, not a second mutable task mapping.
    for (uint32_t i=0;i<request.count;++i) {
        VIMGLScatterSpan entry;memcpy(&entry,entries+i*sizeof(entry),sizeof(entry));
        if (!entry.bytes || (entry.offset&3) || (entry.bytes&3) || entry.offset<end ||
            entry.offset>request.bytes || entry.bytes>request.bytes-entry.offset) { IOFree(entries,table_bytes);return kIOReturnBadArgument; }
        end=entry.offset+entry.bytes;
    }
    IOMemoryDescriptor* memory=IOMemoryDescriptor::withAddressRange(rows.address,span,kIODirectionIn,m_owning_task);
    if (!memory) { IOFree(entries,table_bytes);return kIOReturnNoMemory; }
    IOReturn error=memory->prepare();bool prepared=!error;
    const uint32_t capacity=VIM_GL_RAM_SCATTER_MAX_RANGES;
    VIMGLScatterMemorySegment* segments=!error?static_cast<VIMGLScatterMemorySegment*>(IOMalloc(capacity*sizeof(VIMGLScatterMemorySegment))):0;
    if (!error && !segments)error=kIOReturnNoMemory;
    uint64_t base=0,vram=0;uint32_t count=0,stream=0;
    if (!error && !m_provider->directGLVRAMRange(&base,&vram))error=kIOReturnUnsupported;
    for (uint32_t i=0;!error && i<request.count;++i) {
        VIMGLScatterSpan entry;memcpy(&entry,entries+i*sizeof(entry),sizeof(entry));
        if (!VIMAppendMaskedSegment(segments,&count,0,entry.offset-stream,2)) { error=kIOReturnUnsupported;break; }
        stream=entry.offset;
        uint32_t remaining=entry.bytes;
        while (!error && remaining) {
            uint32_t row=stream/rows.row_bytes,column=stream%rows.row_bytes;
            uint64_t position=(rows.reverse?mask.height-1-row:row)*rows.stride+column;
            IOByteCount length=0;addr64_t address=VIMPhysicalSegment(memory,position,&length);
            if (length>remaining)length=remaining;
            if (length>rows.row_bytes-column)length=rows.row_bytes-column;
            if (!length || address>UINT64_MAX-length) { error=kIOReturnUnsupported;break; }
            uint64_t relative=address>=base?address-base:UINT64_MAX;uint32_t kind=1;
            if (relative<=UINT32_MAX && relative<=vram && length<=vram-relative) { address=relative;kind=0; }
            if (!VIMAppendMaskedSegment(segments,&count,address,static_cast<uint32_t>(length),kind)) { error=kIOReturnUnsupported;break; }
            stream+=static_cast<uint32_t>(length);remaining-=static_cast<uint32_t>(length);
        }
    }
    if (!error && !VIMAppendMaskedSegment(segments,&count,0,request.bytes-stream,2))error=kIOReturnUnsupported;
    if (!error) {
        m_scatter_memory=memory;m_scatter_segments=segments;m_scatter_count=count;m_scatter_bytes=request.bytes;
        m_scatter_capacity=capacity;m_scatter_masked=true;
        if (!++m_scatter_serial)++m_scatter_serial;
        m_scatter_token=m_scatter_serial;
    } else {
        if (prepared)memory->complete();memory->release();
        if (segments)IOFree(segments,capacity*sizeof(VIMGLScatterMemorySegment));
    }
    IOFree(entries,table_bytes);return error;
}
bool CLASS::prepare_direct_upload(IOMemoryDescriptor* memory,void* command,uint32_t bytes)
{
    // The ordinary transfer has already prepared this owning-task descriptor
    // for its lifetime. Only replace staging after the complete table fits.
    if (!memory || !bytes || bytes>VIM_GL_CHUNK || memory->getLength()<bytes)return false;
    VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(command);
    VIMGLScatterMemorySegment* segments=reinterpret_cast<VIMGLScatterMemorySegment*>(packet+1);
    uint64_t base=0,vram=0;uint32_t count=0,position=0;
    if (!m_provider->directGLVRAMRange(&base,&vram))return false;
    while (position<bytes) {
        IOByteCount length=0;addr64_t address=VIMPhysicalSegment(memory,position,&length);
        if (length>bytes-position)length=bytes-position;
        if (!length || address>UINT64_MAX-length)return false;
        uint32_t kind=1;
        if (address>=base && address-base<vram) {
            address-=base;kind=0;
            if (length>vram-address)return false;
        }
        if (count && segments[count-1].kind==kind &&
            segments[count-1].address+segments[count-1].bytes==address)
            segments[count-1].bytes+=static_cast<uint32_t>(length);
        else if (count<VIM_GL_RAM_SCATTER_MAX_RANGES) {
            VIMGLScatterMemorySegment& segment=segments[count++];
            segment.address=address;segment.bytes=static_cast<uint32_t>(length);segment.kind=kind;
        } else return false;
        position+=static_cast<uint32_t>(length);
    }
    packet->action=10;packet->reserved=count;return true;
}
IOReturn CLASS::direct_gl_scatter(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if (!input || input_size<sizeof(VIMGLScatterRequest) || !output || !output_size ||
        *output_size!=sizeof(VIMGLScatterResult) || !m_direct_lock)return kIOReturnBadArgument;
    VIMGLScatterRequest request;memcpy(&request,input,sizeof(request));
    if (request.action!=6) {
        IOLockLock(m_direct_lock);
        IOReturn error=direct_gl_scatter_locked(input,output,input_size,output_size);
        IOLockUnlock(m_direct_lock);return error;
    }
    // Metadata is an immutable in-band IOKit snapshot. Pin/validate every row
    // BEFORE sending even the prefix. Failures after any stream submission are
    // fatal, never an invitation to replay an already-consumed GL operation.
    VIMGLScatterUpload upload;
    if(input_size<sizeof(request)+sizeof(upload))return kIOReturnBadArgument;
    memcpy(&upload,static_cast<const char*>(input)+sizeof(request),sizeof(upload));
    if(!upload.prefix_bytes || upload.prefix_bytes>VIM_GL_UPLOAD_PREFIX_LIMIT || upload.reserved ||
        input_size!=sizeof(request)+sizeof(upload)+upload.prefix_bytes)return kIOReturnBadArgument;
    struct { VIMGLScatterRequest request;VIMGLScatterRows rows; } prepare={request,upload.rows};
    prepare.request.action=4;
    IOLockLock(m_direct_lock);
    if(m_scatter_memory || m_scatter_access){IOLockUnlock(m_direct_lock);return kIOReturnExclusiveAccess;}
    VIMGLScatterResult prepared={0,0};size_t size=sizeof(prepared);
    IOReturn error=direct_gl_scatter_locked(&prepare,&prepared,sizeof(prepare),&size);
    if(!error) {
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_direct_buffer);
        bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=2;
        packet->token=m_direct_token;packet->bytes=upload.prefix_bytes;packet->result=-1;
        memcpy(packet+1,static_cast<const char*>(input)+sizeof(request)+sizeof(upload),upload.prefix_bytes);
        error=m_provider->directGLTransfer(packet);
        if(!error && packet->result!=static_cast<int32_t>(upload.prefix_bytes))error=kIOReturnIOError;
        VIMGLScatterResult sent={0,0};
        if(!error) {
            VIMGLScatterRequest send={5,0,prepared.token,request.bytes};size=sizeof(sent);
            error=direct_gl_scatter_locked(&send,&sent,sizeof(send),&size);
            if(!error && (sent.count!=static_cast<int32_t>(request.bytes) || sent.token!=prepared.token))error=kIOReturnIOError;
        }
        clear_direct_scatter(); // Same synchronous pin lifetime as 4 -> 5 -> 2.
        if(error)error=kIOReturnIOError; // No fallback/replay after prefix submission.
        else {sent.token=0;memcpy(output,&sent,sizeof(sent));}
    }
    IOLockUnlock(m_direct_lock);return error;
}
IOReturn CLASS::direct_gl_scatter_locked(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if (!input || input_size<sizeof(VIMGLScatterRequest) || !output || !output_size ||
        *output_size!=sizeof(VIMGLScatterResult) || !m_direct_lock)return kIOReturnBadArgument;
    VIMGLScatterRequest request;memcpy(&request,input,sizeof(request));
    const uint32_t maximum_rows=m_direct_ram_scatter_supported?VIM_GL_RAM_SCATTER_MAX_ROWS:VIM_GL_SCATTER_MAX_RANGES;
    const uint32_t capacity=m_direct_ram_scatter_supported?VIM_GL_RAM_SCATTER_MAX_RANGES:VIM_GL_SCATTER_MAX_RANGES;
    bool preparing_rows=request.action==0 || request.action==4;
    if (request.action>5 || (request.action!=3 && (request.count>maximum_rows ||
        input_size!=sizeof(request)+(preparing_rows?sizeof(VIMGLScatterRows):0) ||
        (!preparing_rows && request.count))))return kIOReturnBadArgument;
    IOReturn error=kIOReturnSuccess;
    VIMGLScatterResult result={0,0};
    if((preparing_rows || request.action==3) && !m_scatter_memory && !m_scatter_access && m_provider){
        // A persistent foreign user-buffer pin has unknown alias identity.
        // Reserve BEFORE prepare/native use, so a later whole-object gate
        // cannot overtake it. Never wait on our OWN deferred backing gate:
        // decline this optional fast path and retain the ordinary GL copy.
        if(m_deferred_loan || m_writer_loan)error=kIOReturnUnsupported;
        else {error=m_provider->borrowBackingAccess(0,&m_scatter_memory);m_scatter_access=!error;}
    }
    if(error)return error;
    if (!m_provider || !m_direct_token || !m_direct_scatter_supported)error=kIOReturnUnsupported;
    else if (request.action==3) {
        error=prepare_direct_mask(input,input_size);
        if (!error)result.token=m_scatter_token;
    } else if (preparing_rows) {
        if (m_scatter_memory)error=kIOReturnExclusiveAccess;
        else if (request.action==4 && (!m_direct_ram_scatter_supported || !m_direct_ram_upload_supported ||
            !m_provider->directGLRAMUploadEnabled()))error=kIOReturnUnsupported;
        else if (!request.count || request.token || !request.bytes || request.bytes>VIM_GL_SCATTER_MAX_BYTES)error=kIOReturnBadArgument;
        else {
            {
                VIMGLScatterRows rows;memcpy(&rows,static_cast<const char*>(input)+sizeof(request),sizeof(rows));
                uint64_t total=(uint64_t)rows.row_bytes*request.count,span=0;
                if (!rows.address || !rows.row_bytes || rows.reverse>1 || rows.stride<rows.row_bytes ||
                    total!=request.bytes || (request.count>1 && rows.stride>(UINT64_MAX-rows.row_bytes)/(request.count-1)))error=kIOReturnBadArgument;
                if (!error) {
                    span=(request.count-1)*rows.stride+rows.row_bytes;
                    // Pin the backing span once, not each overlapping row's
                    // pages separately. Bound holes too: a small transfer
                    // must never pin an arbitrarily large task allocation.
                    if (span>VIM_GL_SCATTER_MAX_BYTES || rows.address>UINT64_MAX-span)error=kIOReturnBadArgument;
                }
                IOMemoryDescriptor* memory=!error?IOMemoryDescriptor::withAddressRange(rows.address,span,
                    request.action==4?kIODirectionOut:kIODirectionIn,m_owning_task):0;
                if (!error && !memory)error=kIOReturnNoMemory;
                bool prepared=false;
                if (!error) { error=memory->prepare();prepared=!error; }
                VIMGLScatterMemorySegment* segments=!error?static_cast<VIMGLScatterMemorySegment*>(IOMalloc(capacity*sizeof(VIMGLScatterMemorySegment))):0;
                if (!error && !segments)error=kIOReturnNoMemory;
                uint64_t base=0,vram=0;uint32_t count=0;
                if (!error && !m_provider->directGLVRAMRange(&base,&vram))error=kIOReturnUnsupported;
                for (uint32_t row=0;!error && row<request.count;++row) {
                    uint64_t position=(rows.reverse?request.count-1-row:row)*rows.stride;
                    uint32_t remaining=rows.row_bytes;
                    while (!error && remaining) {
                        IOByteCount length=0;addr64_t address=VIMPhysicalSegment(memory,position,&length);
                        if (length>remaining)length=remaining;
                        uint64_t relative=address>=base?address-base:UINT64_MAX;
                        uint32_t kind=0;
                        if (!length || address>UINT64_MAX-length) { error=kIOReturnUnsupported;break; }
                        if (relative<=UINT32_MAX && relative<=vram && length<=vram-relative)address=relative;
                        else if (m_direct_ram_scatter_supported)kind=1;
                        else { error=kIOReturnUnsupported;break; }
                        // Coalesce only adjacent stream bytes and physical
                        // storage. Never fill pitch holes or reverse rows.
                        if (count && segments[count-1].kind==kind &&
                            segments[count-1].address+segments[count-1].bytes==address)
                            segments[count-1].bytes+=static_cast<uint32_t>(length);
                        else if (count<capacity) {
                            segments[count].address=address;segments[count].bytes=static_cast<uint32_t>(length);segments[count].kind=kind;++count;
                        } else { error=kIOReturnUnsupported;break; }
                        position+=length;remaining-=static_cast<uint32_t>(length);
                    }
                }
                if (!error) {
                    m_scatter_memory=memory;m_scatter_segments=segments;m_scatter_count=count;m_scatter_bytes=request.bytes;
                    m_scatter_capacity=capacity;
                    m_scatter_upload=request.action==4;
                    if (!++m_scatter_serial)++m_scatter_serial;
                    m_scatter_token=m_scatter_serial;result.token=m_scatter_token;
                } else {
                    if (memory) { if (prepared)memory->complete();memory->release(); }
                    if (segments)IOFree(segments,capacity*sizeof(VIMGLScatterMemorySegment));
                }
            }
        }
    } else if (!m_scatter_memory || !request.token || request.token!=m_scatter_token ||
               request.bytes!=((request.action==1 || request.action==5)?m_scatter_bytes:0) ||
               (request.action==1 && m_scatter_upload) || (request.action==5 && !m_scatter_upload))error=kIOReturnBadArgument;
    else if (request.action==2)clear_direct_scatter();
    else if (request.action==5) {
        // One user/kernel call for the whole prepared image. Reuse QEMU's
        // already-negotiated action 10. Its descriptor table, not its data,
        // occupies VRAM staging. A separately negotiated host can consume the
        // entire prepared range in one doorbell, without a larger allocation.
        // Older hosts retain the original 256-KB stream chunks.
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_direct_buffer);
        const VIMGLScatterMemorySegment* segments=static_cast<const VIMGLScatterMemorySegment*>(m_scatter_segments);
        uint32_t sent=0,index=0,within=0;
        uint32_t limit=m_direct_bulk_upload_supported && m_provider->directGLBulkUploadEnabled()?
            VIM_GL_SCATTER_MAX_BYTES:VIM_GL_CHUNK;
        while (!error && sent<m_scatter_bytes) {
            uint32_t bytes=m_scatter_bytes-sent;if (bytes>limit)bytes=limit;
            uint32_t remaining=bytes,count=0;
            VIMGLScatterMemorySegment* output=reinterpret_cast<VIMGLScatterMemorySegment*>(packet+1);
            while (remaining && index<m_scatter_count && count<VIM_GL_RAM_SCATTER_MAX_RANGES) {
                uint32_t amount=segments[index].bytes-within;if (amount>remaining)amount=remaining;
                output[count].address=segments[index].address+within;output[count].bytes=amount;output[count].kind=segments[index].kind;
                ++count;remaining-=amount;within+=amount;
                if (within==segments[index].bytes) { ++index;within=0; }
            }
            if (remaining) { error=kIOReturnBadArgument;break; }
            bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=10;packet->token=m_direct_token;
            packet->bytes=bytes;packet->reserved=count;packet->result=-1;
            error=m_provider->directGLTransfer(packet);
            if (!error && packet->result!=static_cast<int32_t>(bytes))error=kIOReturnIOError;
            if (!error)sent+=bytes;
        }
        if (!error) { result.count=sent;result.token=m_scatter_token; }
    }
    else {
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_direct_buffer);
        bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=m_scatter_masked?11:m_direct_ram_scatter_supported?8:7;packet->token=m_direct_token;
        packet->bytes=m_scatter_bytes;packet->reserved=m_scatter_count;packet->result=-1;
        if (m_direct_ram_scatter_supported)memcpy(packet+1,m_scatter_segments,m_scatter_count*sizeof(VIMGLScatterMemorySegment));
        else {
            const VIMGLScatterMemorySegment* source=static_cast<const VIMGLScatterMemorySegment*>(m_scatter_segments);
            VIMGLScatterSegment* target=reinterpret_cast<VIMGLScatterSegment*>(packet+1);
            for (uint32_t i=0;i<m_scatter_count;++i) { target[i].offset=static_cast<uint32_t>(source[i].address);target[i].bytes=source[i].bytes; }
        }
        error=m_provider->directGLTransfer(packet);
        if (!error && packet->result!=static_cast<int32_t>(m_scatter_bytes))error=kIOReturnIOError;
        if (!error) { result.count=packet->result;result.token=m_scatter_token; }
    }
    if(error && !m_scatter_memory && m_scatter_access){m_provider->returnBackingAccess(&m_scatter_memory);m_scatter_access=false;}
    if (!error)memcpy(output,&result,sizeof(result));
    return error;
}
