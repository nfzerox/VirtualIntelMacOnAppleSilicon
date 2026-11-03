/* Research-only source-of-corruption evidence, compiled out normally.
 * After the real producer fence completes, retain the existing immutable
 * version while writing its menu/Dock bands. This deliberately adds CPU
 * access/disk latency: NEVER use these runs as pacing/performance evidence.
 * No GL state, guest pixels or presentation-success decisions are modified.
 */
static int DGJournalWrite(int fd,const void *data,size_t bytes)
{
    const unsigned char *p=data;
    while(bytes){ssize_t n=write(fd,p,bytes);if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;p+=n;bytes-=(size_t)n;}
    return 1;
}
static void DGJournalSurfaceFrame(struct DGSurfaceSnapshot *v,uint64_t generation,uint64_t ticket)
{
    static int initialized,folder=-1,armed,remaining;
    static uint64_t written;
    const uint64_t budget=512u*1024u*1024u;
    if(!initialized){
        initialized=1;const char *path=getenv("VIM_DISPLAY_JOURNAL_DIR");
        if(!path || !*path)return;
        folder=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct stat st;
        if(folder>=0 && (fstat(folder,&st) || !S_ISDIR(st.st_mode) ||
           st.st_uid!=geteuid() || (st.st_mode&0077))){close(folder);folder=-1;}
    }
    if(folder<0)return;
    struct stat arm;
    int enabled=!fstatat(folder,"arm",&arm,AT_SYMLINK_NOFOLLOW) &&
        S_ISREG(arm.st_mode) && arm.st_uid==geteuid() && !(arm.st_mode&0077);
    if(armed && !enabled){close(folder);folder=-1;return;}
    if(!armed){
        if(!enabled)return;
        armed=1;remaining=128;
        fprintf(stderr,"Research display-band journal armed: at most 128 versions / 512 MiB; not a performance run\n");
    }
    if(!v || !v->storage || v->width<1 || v->height<1)return;
    uint32_t top=v->height<64?(uint32_t)v->height:64;
    uint32_t bottom=v->height-top<256?(uint32_t)v->height-top:256;
    struct {uint64_t magic,version,generation,ticket,epoch,at;uint32_t width,height,top,bottom;} header={
        UINT64_C(0x314c4e524a4d4956),1,generation,ticket,v->epoch,mach_absolute_time(),
        (uint32_t)v->width,(uint32_t)v->height,top,bottom};
    size_t row=(size_t)v->width*4,bytes=(size_t)(top+bottom)*row;
    if(sizeof(header)+bytes>budget-written){remaining=0;}
    else if(!IOSurfaceLock(v->storage,kIOSurfaceLockReadOnly,NULL)){
        const unsigned char *base=IOSurfaceGetBaseAddress(v->storage);
        size_t pitch=IOSurfaceGetBytesPerRow(v->storage),allocation=IOSurfaceGetAllocSize(v->storage);
        char name[128];snprintf(name,sizeof(name),"frame-%d-%020llu-%020llu.vimjournal",getpid(),
            (unsigned long long)generation,(unsigned long long)ticket);
        int fd=-1,good=base && pitch>=row && pitch<=DG_MAX_BYTES &&
            (size_t)(v->height-1)*pitch+row<=allocation;
        if(good){fd=openat(folder,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);good=fd>=0;}
        if(good)good=DGJournalWrite(fd,&header,sizeof(header));
        for(uint32_t y=0;good && y<top;++y)good=DGJournalWrite(fd,base+(size_t)y*pitch,row);
        for(uint32_t y=v->height-bottom;good && y<(uint32_t)v->height;++y)
            good=DGJournalWrite(fd,base+(size_t)y*pitch,row);
        if(fd>=0){if(close(fd))good=0;if(!good)unlinkat(folder,name,0);}
        if(IOSurfaceUnlock(v->storage,kIOSurfaceLockReadOnly,NULL))good=0;
        if(good){written+=sizeof(header)+bytes;--remaining;}
        else {fprintf(stderr,"Research display-band journal stopped after write/lock failure\n");remaining=0;}
    }else {fprintf(stderr,"Research display-band journal stopped after lock failure\n");remaining=0;}
    if(!remaining){close(folder);folder=-1;
        fprintf(stderr,"Research display-band journal finished: %llu bytes\n",(unsigned long long)written);}
}
