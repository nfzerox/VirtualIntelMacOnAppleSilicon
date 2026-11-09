/* Offline diagnostic replay through the actual inline interpreter.
 * Not a GPU/guest-memory capture format: use self-contained offscreen streams.
 * Never run captures from an untrusted source. No guest/VM is modified.
 */
#define VIM_PASSTHROUGH_OPENGL_INPROCESS 1
#define VIM_PASSTHROUGH_OPENGL_PACKET_JOURNAL 1
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
/* A diagnostic control, not an executor workaround: materialize the native
 * driver's own current subroutine selections without changing their values. */
static void refreshSubroutines(struct DGInlineStream *stream,unsigned context)
{
    typedef void (*StageProc)(GLuint,GLenum,GLenum,GLint *);
    typedef void (*GetProc)(GLenum,GLint,GLuint *);
    typedef void (*SetProc)(GLenum,GLsizei,const GLuint *);
    StageProc stage=(StageProc)dlsym(RTLD_DEFAULT,"glGetProgramStageiv");
    GetProc get=(GetProc)dlsym(RTLD_DEFAULT,"glGetUniformSubroutineuiv");
    SetProc set=(SetProc)dlsym(RTLD_DEFAULT,"glUniformSubroutinesuiv");
    if(!stage || !get || !set || !context || context>=stream->interpreter->next)return;
    CGLContextObj previous=CGLGetCurrentContext();
    if(CGLSetCurrentContext(stream->interpreter->contexts[context].cgl))return;
    GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    const GLenum stages[]={0x8b31,0x8e88,0x8e87,0x8dd9,0x8b30};
    for(unsigned s=0;program && s<sizeof(stages)/sizeof(*stages);++s){
        GLint count=0;stage((GLuint)program,stages[s],0x8e47,&count);
        if(count<=0 || count>4096)continue;
        GLuint *indices=calloc((size_t)count,sizeof(*indices));
        if(!indices)break;
        for(GLint i=0;i<count;++i)get(stages[s],i,indices+i);
        fprintf(stderr,"REFRESH stage=%x selections=%d first=%u\n",stages[s],count,indices[0]);
        set(stages[s],count,indices);free(indices);
    }
    CGLSetCurrentContext(previous);
}
static int replay(const char *path,int refresh,int rename)
{
    FILE *file=fopen(path,"rb");if(!file){perror(path);return 1;}
    uint32_t header[4];
    if(fread(header,sizeof(header),1,file)!=1 || header[0]!=DG_JOURNAL_MAGIC ||
       header[1]!=1 || header[2]!=sizeof(struct DGMessage)){fclose(file);return 1;}
    struct DGInlineStream *stream=DGInlineOpen();
    if(!stream){fclose(file);return 1;}
    struct DGJournalRecord record;unsigned count=0;int complete=0,result=0;
    while(fread(&record,sizeof(record),1,file)==1){
        if(record.kind){complete=record.kind==1;break;}
        if(record.bytes<sizeof(struct DGMessage) || record.bytes>sizeof(struct DGMessage)+DG_MAX_BYTES){result=1;break;}
        struct DGMessage *message=malloc(record.bytes);
        if(!message){result=1;break;}
        if(fread(message,record.bytes,1,file)!=1 || !DGHeaderValid(message) ||
           record.bytes!=sizeof(*message)+((message->flags&DG_OUTPUT_ONLY)?0:message->bytes)){
            free(message);result=1;break;
        }
        fprintf(stderr,"REPLAY packet=%u op=%u context=%u bytes=%u thread=%llu\n",
            count++,message->op,message->context,message->bytes,(unsigned long long)record.thread);
        fflush(stderr);
        /* Equal-length identifier control for the captured all-stage shader.
         * No reflection queries follow these names in that stream. */
        if(rename && message->op==DG_OP_shader_source_ARB){
            char *source=(char *)(message+1);
            for(size_t i=0;i+8<=message->bytes;++i)if(!memcmp(source+i,"function",8)){
                char name[9];snprintf(name,sizeof(name),"pick%04u",(unsigned)(message->args[0]%10000));
                memcpy(source+i,name,8);i+=7;
            }
        }
        if(refresh && message->op==DG_DRAW)refreshSubroutines(stream,message->context);
        ssize_t written=DGInlineWrite(stream,message,record.bytes);
        free(message);
        if(written!=(ssize_t)record.bytes){result=1;break;}
        /* Do not block waiting for a reply to a command without one. */
        unsigned char discard[4096];
        while(stream->queued){
            size_t n=stream->queued<sizeof(discard)?stream->queued:sizeof(discard);
            if(DGInlineRead(stream,discard,n)!=(ssize_t)n){result=1;break;}
        }
        if(result)break;
    }
    if(ferror(file))result=1;
    fclose(file);
    fprintf(stderr,"REPLAY packets=%u clean-close=%d accepted=%d\n",count,complete,!result);
    DGInlineClose(stream);
    return result;
}
int main(int argc,char **argv)
{
    if(argc<2){fprintf(stderr,"Usage: %s [--refresh-subroutines | --rename-subroutines] STREAM.dgj [...]\n",argv[0]);return 64;}
    int refresh=!strcmp(argv[1],"--refresh-subroutines"),rename=!strcmp(argv[1],"--rename-subroutines");
    int first=1+refresh+rename;
    if(first==argc)return 64;
    unsetenv("VIM_GL_PACKET_JOURNAL");
    if(DGConfigureHost())return 1;
    for(int i=first;i<argc;++i)if(replay(argv[i],refresh,rename))return 1;
    return 0;
}
