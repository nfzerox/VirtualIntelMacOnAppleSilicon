/* Actual decoder/native GPU dependency chain. No intermediate CPU read,
 * client fence wait, repairing ReadPixels or fabricated completion. This is
 * the HOST prerequisite, NOT loaded-kernel CPU-gate/desktop qualification. */
static struct DGMessage writer_begin(unsigned context,uint64_t epoch,uint64_t serial)
{
    struct DGMessage m={.op=DG_BACKING_RENDER_BEGIN,.context=context,.flags=DG_REPLY|DG_INPUT_ONLY};
    m.args[1]=Bytes;m.args[2]=m.args[7]=W;m.args[3]=m.args[8]=H;m.args[4]=Pitch*4;
    m.args[10]=19110;m.args[11]=1;m.args[12]=epoch;m.args[13]=1;m.args[15]=serial;
    return m;
}
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
static void writer_alias_pixels(void *stream,unsigned context,GLuint alias,const uint32_t expected[W*H])
{
    GLuint fbo=0;uint32_t zero=0,image[W*H]={0};
    struct DGMessage m={.op=DG_OP_gen_framebuffers_EXT,.context=context,.flags=DG_REPLY,.bytes=4,.args={1,1}};
    struct DGReply r=exchange_output(stream,&m,&zero,&fbo);assert(!r.error && fbo);
    m=(struct DGMessage){.op=DG_OP_bind_framebuffer_EXT,.context=context,.args={GL_FRAMEBUFFER_EXT,fbo}};send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_framebuffer_texture2D_EXT,.context=context,
        .args={GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,alias,0}};send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_check_framebuffer_status_EXT,.context=context,.flags=DG_REPLY,.args={GL_FRAMEBUFFER_EXT}};
    r=exchange(stream,&m,NULL);assert(!r.error && r.result==GL_FRAMEBUFFER_COMPLETE_EXT);
    m=(struct DGMessage){.op=DG_OP_read_buffer,.context=context,.args={GL_COLOR_ATTACHMENT0_EXT}};send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_read_pixels,.context=context,.flags=DG_REPLY,.bytes=sizeof(image),
        .args={0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,1}};
    r=exchange_output(stream,&m,image,image);assert(!r.error && !memcmp(image,expected,sizeof(image)));
    m=(struct DGMessage){.op=DG_OP_bind_framebuffer_EXT,.context=context,.args={GL_FRAMEBUFFER_EXT,0}};send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_delete_framebuffers_EXT,.context=context,.flags=DG_REPLY,.bytes=4,.args={1,1}};
    r=exchange(stream,&m,&fbo);assert(!r.error);
}
#endif
static void qualify_backing_writers(const uint64_t pages[Pages],volatile uint32_t *pixels,CGLContextObj sentinel)
{
    enum {Rounds=64};const uint64_t space=2110;
    void *stream=api->open(),*service=backing_service(space);assert(stream);
    struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT)};
    m.args[0]=DG_MAGIC;m.args[1]=DG_VERSION;m.args[2]=1;
    m.args[3]=0x6173796e632d7465ULL;m.args[4]=space;
    struct DGReply r=exchange(stream,&m,DG_LAYOUT);
    assert(!r.error && (r.result&DG_CAP_BACKING_WRITER_CONTINUATION));
    unsigned contexts[4];
    for(unsigned i=0;i<4;++i){
        m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};
        // 0/1 share GPU objects; 2 is a foreign share group, 3 a cache miss.
        if(i==1 || i==3)m.args[0]=contexts[0];
        r=exchange(stream,&m,NULL);assert(!r.error && r.result);contexts[i]=(unsigned)r.result;
        m=(struct DGMessage){.op=test_prepared?DG_PREPARE_DRAWABLE:DG_RESIZE,.context=contexts[i],.flags=DG_REPLY,.args={W,H,1}};
        r=exchange(stream,&m,NULL);assert(!r.error);
        if(i==3)continue;
        memset((void*)pixels,0xa7,Bytes);
        for(unsigned object=0;object<2;++object){
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)pixels[y*Pitch+x]=0xff102030u;
            if(i==0){
                // This route needs an EXACT already-current GPU epoch, not
                // permission to ignore CPU-newer data. Seed that genuine
                // state through the public resource wire before first Begin.
                clear(stream,contexts[i],16.0f/255,32.0f/255,48.0f/255);
                m=(struct DGMessage){.op=DG_SURFACE_ACQUIRE,.context=contexts[i],.flags=DG_REPLY,
                    .args={19110+object,1,W,H}};
                r=exchange(stream,&m,NULL);assert(!r.error && r.result);
                m=(struct DGMessage){.op=DG_SURFACE_PUBLISH,.context=contexts[i],.flags=DG_REPLY,
                    .args={r.result,0,0,1}};
                r=exchange(stream,&m,NULL);assert(!r.error);
            }
#endif
            // The second idle registration leaves the actual chain target
            // in the bounded spare cache. First continuation must move it
            // without invoking the ordinary eviction/CPU-wait path.
            m=writer_begin(contexts[i],1,object+1);m.args[10]+=object;
            m.bytes=Pages*sizeof(uint64_t);m.args[14]=Pages;
            r=exchange(stream,&m,pages);assert(!r.error && r.result==1);
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
            assert(dg_scatter_stats[0]>0 && dg_scatter_stats[3]>0);
#endif
            m=(struct DGMessage){.op=DG_BACKING_RENDER_END,.context=contexts[i],.flags=DG_REPLY,.args={object+1}};
            r=exchange(stream,&m,NULL);assert(!r.error && r.result==1);
        }
    }
    memset((void*)pixels,0xa7,Bytes);
    uint32_t expected[W*H];
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        expected[y*W+x]=0xff102030u;pixels[y*Pitch+x]=expected[y*W+x];
    }
    uint64_t tickets[Rounds],serial[2]={2,2};
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    int early_notification=getenv("VIM_TEST_NOTIFY_EARLY")!=NULL;
    struct backing_early_notification early;
    if(early_notification)assert(dg_backing_cpu_coalescing && !getenv("VIM_TEST_NOTIFY_ALL") &&
        !getenv("VIM_TEST_NOTIFY_OLDEST_FIRST") && !getenv("VIM_TEST_COALESCING_VETO"));
#endif
    uint64_t waits=dg_backing_sync_waits,continues=dg_backing_writer_continuations;
    uint64_t submitted=dg_backing_deferred_submissions;
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
    uint64_t self_publications=dg_scope_canonical_stats[2];
    uint64_t canonical_writers=dg_canonical_writer_stats[0],alias_writers=dg_canonical_writer_stats[1];
    uint32_t first_hold=0,first_pixels[W*H];GLuint live_alias=0;
#endif
    for(unsigned n=0;n<Rounds;++n){
        unsigned ci=n&1,context=contexts[ci];uint64_t epoch=n+1;
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
        if(n==2){
            // A real persistent alias to the canonical target, shared by both
            // producers. Continuing in a DIFFERENT RAM target would split it.
            uint32_t zero=0;
            m=(struct DGMessage){.op=DG_OP_gen_textures,.context=contexts[0],.flags=DG_REPLY,.bytes=4,.args={1,1}};
            r=exchange_output(stream,&m,&zero,&live_alias);assert(!r.error && live_alias);
            m=(struct DGMessage){.op=DG_OP_bind_texture,.context=contexts[0],.args={GL_TEXTURE_RECTANGLE_ARB,live_alias}};
            send_bytes(stream,&m,sizeof(m));
            m=(struct DGMessage){.op=DG_BIND_DRAWABLE_TEXTURE,.context=contexts[0],.flags=DG_REPLY,.args={GL_RGBA8,W,H}};
            r=exchange(stream,&m,NULL);assert(!r.error);
        }
#endif
        if(test_prepared && n==2){
            // New empty reservation must discover aliases of its DESTINATION,
            // not conclude that color=0 means the canonical surface is unaliased.
            m=(struct DGMessage){.op=DG_DROP_DRAWABLE,.context=context,.flags=DG_REPLY,.args={1}};
            r=exchange(stream,&m,NULL);assert(!r.error);
            m=(struct DGMessage){.op=DG_PREPARE_DRAWABLE,.context=context,.flags=DG_REPLY,.args={W,H,1}};
            r=exchange(stream,&m,NULL);assert(!r.error);
        }
        m=writer_begin(context,epoch,++serial[ci]);
        if(n){m.op=DG_BACKING_RENDER_CONTINUE;m.args[5]=tickets[n-1];}
        if(n==1){
            struct DGMessage bad=m;
            bad.context=contexts[2];bad.args[15]=3; // registered but native share group is foreign
            r=exchange(stream,&bad,NULL);assert(!r.error && !r.result);
            bad.context=contexts[3]; // native group shares, but NO registration: do not import pending RAM
            r=exchange(stream,&bad,NULL);assert(!r.error && !r.result);
            bad=m;bad.args[11]++;r=exchange(stream,&bad,NULL);assert(r.error || !r.result);
            bad=m;bad.args[12]++;r=exchange(stream,&bad,NULL);assert(r.error || !r.result);
            bad=m;bad.args[4]+=32;r=exchange(stream,&bad,NULL);assert(r.error || !r.result);
            bad=m;bad.args[15]=1;r=exchange(stream,&bad,NULL);assert(r.error==GL_INVALID_OPERATION);
            bad=m;bad.args[6]=1;r=exchange(stream,&bad,NULL);assert(r.error==GL_INVALID_OPERATION);
        }
        r=exchange(stream,&m,NULL);assert(!r.error && r.result==1);
        if(n){r=exchange(stream,&m,NULL);assert(r.error==GL_INVALID_OPERATION && !r.result);}
        unsigned x=(n*7)%W,y=(n*3)%H,w=(W-x)>11?11:W-x,h=(H-y)>9?9:H-y;
        m=(struct DGMessage){.op=DG_OP_enable,.context=context,.args={GL_SCISSOR_TEST}};send_bytes(stream,&m,sizeof(m));
        m=(struct DGMessage){.op=DG_OP_scissor,.context=context,.args={x,y,w,h}};send_bytes(stream,&m,sizeof(m));
        unsigned color=n%3;
        clear(stream,context,color==0,color==1,color==2);
        uint32_t value=color==0?0xffff0000u:color==1?0xff00ff00u:0xff0000ffu;
        for(unsigned yy=y;yy<y+h;++yy)for(unsigned xx=x;xx<x+w;++xx)expected[yy*W+xx]=value;
        if(n%3==1){
            // Force the producer's private-snapshot + LATER private writes
            // route. Its copy context can still write RAM after the source
            // snapshot fence; continuation must order the LAST RAM writer.
            m=(struct DGMessage){.op=DG_BACKING_RENDER_MATERIALIZE,.context=context,
                .flags=DG_REPLY,.args={serial[ci]}};
            r=exchange(stream,&m,NULL);assert(!r.error && r.result==1);
            m=(struct DGMessage){.op=DG_OP_scissor,.context=context,.args={0,0,3,2}};
            send_bytes(stream,&m,sizeof(m));clear(stream,context,1,1,0);
            for(unsigned yy=0;yy<2;++yy)for(unsigned xx=0;xx<3;++xx)expected[yy*W+xx]=0xffffff00u;
        }
        m=(struct DGMessage){.op=DG_BACKING_RENDER_DEFER,.context=context,.flags=DG_REPLY,.args={serial[ci],1}};
        r=exchange(stream,&m,NULL);assert(!r.error && r.result);tickets[n]=r.result;
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
        if(!n && early_notification){
            const uint64_t early_namespace[2]={0x6173796e632d7465ULL,space};
            begin_early_backing_notification(&early,early_namespace,tickets[0]);
        }
#endif
        assert(dg_backing_sync_waits==waits); // no CPU completion introduced by writer admission
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
        if(!n){
            memcpy(first_pixels,expected,sizeof(first_pixels));
            m=(struct DGMessage){.op=DG_SURFACE_HOLD,.flags=DG_REPLY,.args={19110,1,W,H,2}};
            r=exchange(service,&m,NULL);assert(!r.error && r.result && r.result<=UINT32_MAX);first_hold=(uint32_t)r.result;
        }
#endif
    }
    assert(dg_backing_writer_continuations==continues+Rounds-1);
    assert(dg_backing_deferred_submissions==submitted+Rounds);
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(getenv("VIM_TEST_CLOSE_BEFORE_NOTIFY")){
        assert(!early_notification); // That diagnostic deliberately holds an auxiliary context lock.
        assert(!api->close(stream));stream=NULL;
    }
#endif
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
    assert(dg_scope_canonical_stats[2]==self_publications+Rounds);
    /* The first continuation switches this context from its second warmed
     * object to the chain object. Post-attachment identity validation now
     * correctly admits that canonical writer too; the persistent alias is
     * deliberately created one round later. */
    assert(dg_canonical_writer_stats[0]==canonical_writers+Rounds-1 &&
        dg_canonical_writer_stats[1]==alias_writers+Rounds-2);
    printf("PASS combined canonical/future writer chain: %u same-storage publications, no redundant publication blit\n",Rounds);
#endif
    // In the scatter candidate, request the LAST ticket's actual callback
    // FIRST. It must materialize the whole predecessor chain in CPU order,
    // including both native contexts; no earlier service Wait can repair it.
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    const uint64_t namespace_id[2]={0x6173796e632d7465ULL,space};
    unsigned notify_index=Rounds-1;
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    struct DGBackingTicket *vetoed=NULL;uint64_t saved_incarnation=0;
    int veto=getenv("VIM_TEST_COALESCING_VETO")!=NULL;
    if(veto){
        // Corrupt ONLY immutable relationship metadata, not the pixels or
        // CPU address. The actual ordered-copy route remains a valid oracle.
        // A purported adjacent-epoch chain must revalidate this, even though
        // its successor name was recorded before this injected defect.
        assert(!getenv("VIM_TEST_NOTIFY_OLDEST_FIRST") && !getenv("VIM_TEST_NOTIFY_ALL"));
        pthread_mutex_lock(&dg_backing_ticket_lock);vetoed=dg_backing_tickets;
        while(vetoed && !DGBackingTicketMatches(vetoed,namespace_id,tickets[1]))vetoed=vetoed->next;
        assert(vetoed);__sync_add_and_fetch(&vetoed->refs,1);
        saved_incarnation=vetoed->writer_backing[6];++vetoed->writer_backing[6];
        pthread_mutex_unlock(&dg_backing_ticket_lock);
    }
    if(getenv("VIM_TEST_NOTIFY_OLDEST_FIRST"))notify_index=0;
    uint64_t copies_before_chain=dg_scatter_stats[1];
    uint64_t skipped_before_chain=dg_cpu_coalescing_stats[1];
#endif
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(early_notification)finish_early_backing_notification(&early);
    else if(getenv("VIM_TEST_NOTIFY_ALL"))qualify_backing_notifications_all(namespace_id,tickets,Rounds);
    else if(dg_backing_cpu_coalescing && !notify_index)
        qualify_backing_notification_blocked_by(namespace_id,tickets[notify_index],tickets[Rounds-1]);
    else
#endif
        qualify_backing_notification(namespace_id,tickets[notify_index]);
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(dg_backing_cpu_coalescing){
        if(veto){
            assert(dg_scatter_stats[1]==copies_before_chain+Rounds && dg_cpu_coalescing_stats[1]==skipped_before_chain);
            puts("PASS mismatched backing-incarnation relationship vetoes coalescing: all64 real ordered CPU scatters retained");
        }else{
            assert(dg_scatter_stats[1]==copies_before_chain+1 && dg_cpu_coalescing_stats[1]==skipped_before_chain+Rounds-1);
            printf("PASS %s-first actual notification: one CPU scatter for all64 GPU writers, exact first CPU image follows\n",notify_index?"last":"oldest");
        }
    }
    if(vetoed){
        pthread_mutex_lock(&dg_backing_ticket_lock);vetoed->writer_backing[6]=saved_incarnation;
        pthread_mutex_unlock(&dg_backing_ticket_lock);DGBackingTicketPut(vetoed);
    }
#endif
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Pitch;++x)
        assert(pixels[y*Pitch+x]==(x<W?expected[y*W+x]:0xa7a7a7a7u));
    uint64_t copies_after_last=dg_scatter_stats[1];
#endif
    // Wait for ALL actual native completion queues before the FIRST CPU read
    // in the ordinary candidate. Repeated older Wait must NOT replay scatter.
    for(unsigned i=0;i<Rounds;++i){
        m=(struct DGMessage){.op=DG_BACKING_COMPLETION_WAIT,.flags=DG_REPLY,.args={tickets[i]}};
        r=exchange(service,&m,NULL);assert(!r.error && r.result==1);
    }
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    assert(dg_scatter_stats[1]==copies_after_last);
    puts("PASS actual chain notification: ordered two-context CPU materialization; older duplicate Wait does not replay CPU copies");
#endif
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Pitch;++x)
        assert(pixels[y*Pitch+x]==(x<W?expected[y*W+x]:0xa7a7a7a7u));
    for(unsigned i=Pitch*H*4;i<Bytes;++i)assert(((volatile unsigned char*)pixels)[i]==0xa7);
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
    // The oldest immutable GPU version survives all later canonical writes.
    uint32_t old_image[W*H]={0};
    m=(struct DGMessage){.op=DG_SURFACE_HOLD_READ,.flags=DG_REPLY|DG_OUTPUT_ONLY,
        .bytes=sizeof(old_image),.args={first_hold,2,0,0,W,H}};
    r=exchange_output(service,&m,NULL,old_image);assert(!r.error && !memcmp(old_image,first_pixels,sizeof(old_image)));
    m=(struct DGMessage){.op=DG_SURFACE_UNHOLD,.flags=DG_REPLY,.args={first_hold}};
    r=exchange(service,&m,NULL);assert(!r.error);
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(!stream)goto retire_writer_tickets;
#endif
    // Separate correctness observation, AFTER the 64-version GPU-only chain:
    // a consumer must see new canonical pixels even BEFORE this writer End.
    // The readback is a diagnostic, not a nonblocking throughput claim.
    m=writer_begin(contexts[0],Rounds+1,++serial[0]);m.op=DG_BACKING_RENDER_CONTINUE;m.args[5]=tickets[Rounds-1];
    r=exchange(stream,&m,NULL);assert(!r.error && r.result==1);
    m=(struct DGMessage){.op=DG_OP_disable,.context=contexts[0],.args={GL_SCISSOR_TEST}};send_bytes(stream,&m,sizeof(m));
    clear(stream,contexts[0],1,0,1);
    m=(struct DGMessage){.op=DG_OP_flush,.context=contexts[0]};send_bytes(stream,&m,sizeof(m));
    for(unsigned i=0;i<W*H;++i)expected[i]=0xffff00ffu;
    writer_alias_pixels(stream,contexts[1],live_alias,expected);
    m=(struct DGMessage){.op=DG_BACKING_RENDER_DEFER,.context=contexts[0],.flags=DG_REPLY,.args={serial[0],1}};
    r=exchange(stream,&m,NULL);assert(!r.error && r.result);uint64_t observed_ticket=r.result;
    uint64_t pending_rejections=dg_cpu_canonical_stats[1];
    m=writer_begin(contexts[0],Rounds+2,++serial[0]);m.bytes=Pages*sizeof(uint64_t);m.args[14]=Pages;
    r=exchange(stream,&m,pages);assert(!r.error && !r.result && dg_cpu_canonical_stats[1]>pending_rejections);
    m=(struct DGMessage){.op=DG_BACKING_COMPLETION_WAIT,.flags=DG_REPLY,.args={observed_ticket}};
    r=exchange(service,&m,NULL);assert(!r.error && r.result==1);check_pixels(pixels,0xffff00ffu);
    m=(struct DGMessage){.op=DG_BACKING_COMPLETION_RELEASE,.flags=DG_REPLY,.args={observed_ticket}};
    r=exchange(service,&m,NULL);assert(!r.error && r.result==1);
    // CPU-current Begin can retain the SAME canonical target only after
    // actual completion, at the exact epoch. An invented newer CPU epoch
    // must not authorize aliasing or silently skip a required CPU upload.
    m=writer_begin(contexts[0],Rounds+3,++serial[0]);m.bytes=Pages*sizeof(uint64_t);m.args[14]=Pages;
    r=exchange(stream,&m,pages);assert(!r.error && !r.result);
    uint64_t cpu_admissions=dg_cpu_canonical_stats[0];
    m=writer_begin(contexts[0],Rounds+2,++serial[0]);m.bytes=Pages*sizeof(uint64_t);m.args[14]=Pages;
    r=exchange(stream,&m,pages);assert(!r.error && r.result==1 && dg_cpu_canonical_stats[0]==cpu_admissions+1);
    clear(stream,contexts[0],0,1,1);
    m=(struct DGMessage){.op=DG_OP_flush,.context=contexts[0]};send_bytes(stream,&m,sizeof(m));
    for(unsigned i=0;i<W*H;++i)expected[i]=0xff00ffffu;
    writer_alias_pixels(stream,contexts[1],live_alias,expected); // actual visibility BEFORE CPU-current End
    m=(struct DGMessage){.op=DG_BACKING_RENDER_END,.context=contexts[0],.flags=DG_REPLY,.args={serial[0]}};
    r=exchange(stream,&m,NULL);assert(!r.error && r.result==1);check_pixels(pixels,0xff00ffffu);
    // Last immutable magenta publication remains magenta despite later cyan
    // drawing in the canonical resource and actual completed CPU writeback.
    m=(struct DGMessage){.op=DG_SURFACE_HOLD,.flags=DG_REPLY,.args={19110,1,W,H,Rounds+2}};
    r=exchange(service,&m,NULL);assert(!r.error && r.result);uint64_t cpu_hold=r.result;
    m=(struct DGMessage){.op=DG_SURFACE_HOLD_READ,.flags=DG_REPLY|DG_OUTPUT_ONLY,
        .bytes=sizeof(old_image),.args={cpu_hold,Rounds+2,0,0,W,H}};
    r=exchange_output(service,&m,NULL,old_image);assert(!r.error);
    for(unsigned i=0;i<W*H;++i)assert(old_image[i]==0xffff00ffu);
    m=(struct DGMessage){.op=DG_SURFACE_UNHOLD,.flags=DG_REPLY,.args={cpu_hold}};
    r=exchange(service,&m,NULL);assert(!r.error);
    m=(struct DGMessage){.op=DG_DROP_DRAWABLE,.context=contexts[0],.flags=DG_REPLY,.args={1}};
    r=exchange(stream,&m,NULL);assert(!r.error);
    writer_alias_pixels(stream,contexts[1],live_alias,expected);
    m=(struct DGMessage){.op=DG_VRAM_STATS,.context=contexts[1],.flags=DG_REPLY|DG_OUTPUT_ONLY,
        .bytes=4*sizeof(uint64_t),.args={DG_VRAM_STATS_CANONICAL_WRITER}};
    uint64_t counts[4]={0};r=exchange_output(stream,&m,NULL,counts);assert(!r.error);
    for(unsigned i=0;i<4;++i)assert(counts[i]==__atomic_load_n(&dg_canonical_writer_stats[i],__ATOMIC_RELAXED));
    puts("PASS canonical GPU target: 62 live-alias chain continuations, pending/stale CPU-Begin veto, exact completed CPU epoch admitted, alias read BEFORE both kinds of End, immutable magenta publication survives cyan CPU writer, retirement and first CPU pixels");
#endif
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
retire_writer_tickets:
#endif
    for(unsigned i=0;i<Rounds;++i){
        m=(struct DGMessage){.op=DG_BACKING_COMPLETION_RELEASE,.flags=DG_REPLY,.args={tickets[i]}};
        r=exchange(service,&m,NULL);assert(!r.error && r.result==1);
    }
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(!stream){
        assert(!api->close(service) && !api->live_count());DGSnapshotCleanupDrain();
        assert(!dg_backing_ticket_count && !dg_backing_tickets && !dg_surfaces && !dg_surface_leases &&
            !dg_surface_count && !dg_surface_bytes && !dg_snapshot_bytes && CGLGetCurrentContext()==sentinel);
        puts("PASS64-writer producer-close BEFORE notifications: retained native queues/images/SG pages, FIRST final CPU pixels, oldest immutable GPU image and balanced retirement");
        return;
    }
#endif
    // Context 0's drawable was deliberately retired by the canonical test.
    // Use the still-live context so this tests the retired TICKET, not merely
    // the earlier "no drawable" argument guard.
    m=writer_begin(contexts[1],Rounds,++serial[1]);m.op=DG_BACKING_RENDER_CONTINUE;m.args[5]=tickets[Rounds-1];
    r=exchange(stream,&m,NULL);assert(r.error || !r.result); // retired dependency cannot authorize a writer
    for(unsigned kind=0;kind<2;++kind){
        uint64_t counts[4]={0};m=(struct DGMessage){.op=DG_VRAM_STATS,.context=contexts[0],
            .flags=DG_REPLY|DG_OUTPUT_ONLY,.bytes=sizeof(counts),
            .args={kind?DG_VRAM_STATS_WRITER_ADMISSION:DG_VRAM_STATS_WRITER_REFUSALS}};
        r=exchange_output(stream,&m,NULL,counts);assert(!r.error);
        for(unsigned i=0;i<4;++i)assert(counts[i]==__atomic_load_n(
            kind?&dg_writer_admission_refusals[i]:&dg_writer_refusals[i],__ATOMIC_RELAXED));
        if(!kind)assert(counts[0]>=Rounds && counts[1] && counts[2]);
    }
    for(unsigned kind=0;kind<2;++kind){
        uint64_t counts[4]={0};m=(struct DGMessage){.op=DG_VRAM_STATS,.context=contexts[1],
            .flags=DG_REPLY|DG_OUTPUT_ONLY,.bytes=sizeof(counts),
            .args={kind?DG_VRAM_STATS_WRITER_ERROR_FIELDS:DG_VRAM_STATS_WRITER_ERRORS}};
        r=exchange_output(stream,&m,NULL,counts);assert(!r.error);
        for(unsigned i=0;i<4;++i)assert(counts[i]==__atomic_load_n(
            kind?&dg_writer_error_fields[i]:&dg_writer_errors[i],__ATOMIC_RELAXED));
        if(!kind)assert(counts[0] && counts[1] && counts[2]);
    }
    assert(!api->close(stream) && !api->close(service) && !api->live_count());
    DGSnapshotCleanupDrain();
    assert(!dg_backing_ticket_count && !dg_backing_tickets && !dg_surfaces && !dg_surface_leases &&
        !dg_surface_count && !dg_surface_bytes && !dg_snapshot_bytes && CGLGetCurrentContext()==sentinel);
    puts("PASS 64-version GPU writer chain: 63 server dependencies, two sharing contexts, private later-write ordering, exact first CPU pixels/padding, no intermediate CPU wait, foreign-group/cache-miss/stale refusal and balanced retirement");
}
