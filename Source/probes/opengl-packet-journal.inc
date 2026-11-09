/* Explicitly compiled diagnostic only. Record accepted wire inputs before
 * native execution, including the final packet if the driver crashes.
 * Captures may contain guest pixels/shader source; keep the directory private.
 */
#include <fcntl.h>
#include <sys/stat.h>
struct DGJournalRecord {uint32_t kind,bytes;uint64_t thread;};
#define DG_JOURNAL_MAGIC UINT32_C(0x4a474c44)
static volatile uint64_t dg_journal_bytes;
static unsigned dg_journal_serial;
static int DGJournalWrite(int fd,const void *data,size_t bytes)
{
    const char *p=data;
    while(bytes){
        ssize_t n=write(fd,p,bytes);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        p+=n;bytes-=(size_t)n;
    }
    return 1;
}
static void DGJournalClose(struct DGInterpreter *s)
{
    if(s->journal_fd>0){
        const struct DGJournalRecord end={1,0,0};
        DGJournalWrite(s->journal_fd-1,&end,sizeof(end));
        close(s->journal_fd-1);
    }
    s->journal_fd=-1;
}
static void DGJournalPacket(struct DGInterpreter *s,const struct DGMessage *m)
{
    if(s->journal_fd<0)return;
    if(!s->journal_fd){
        const char *directory=getenv("VIM_GL_PACKET_JOURNAL");
        if(!directory || !*directory){s->journal_fd=-1;return;}
        struct stat st;
        if(lstat(directory,&st) || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&0077)){
            s->journal_fd=-1;return;
        }
        char path[PATH_MAX];
        unsigned serial=__sync_fetch_and_add(&dg_journal_serial,1);
        snprintf(path,sizeof(path),"%s/stream-%06u.dgj",directory,serial);
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0){s->journal_fd=-1;return;}
        const uint32_t header[]={DG_JOURNAL_MAGIC,1,sizeof(struct DGMessage),0};
        if(!DGJournalWrite(fd,header,sizeof(header))){close(fd);s->journal_fd=-1;return;}
        s->journal_fd=fd+1;
    }
    size_t bytes=sizeof(*m)+((m->flags&DG_OUTPUT_ONLY)?0:m->bytes);
    uint64_t total=__sync_fetch_and_add(&dg_journal_bytes,bytes);
    if(s->journal_bytes+bytes>32u*1024u*1024u || total+bytes>512u*1024u*1024u){
        const struct DGJournalRecord end={2,0,0};
        DGJournalWrite(s->journal_fd-1,&end,sizeof(end));
        close(s->journal_fd-1);s->journal_fd=-1;return;
    }
    struct DGJournalRecord record={0,(uint32_t)bytes,0};
    pthread_threadid_np(NULL,&record.thread);
    if(!DGJournalWrite(s->journal_fd-1,&record,sizeof(record)) ||
       !DGJournalWrite(s->journal_fd-1,m,bytes)){
        close(s->journal_fd-1);s->journal_fd=-1;return;
    }
    s->journal_bytes+=bytes;
}
