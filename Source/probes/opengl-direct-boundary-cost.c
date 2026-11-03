/* Actual guest boundary floor before inventing a new service-trap transport.
 * Same real owning endpoint, no driver/CPU policy or global-state changes.
 * Rejected trap/method calls consume no stream bytes. The real typed GL query
 * includes command interpretation; CPUID includes no GL work at all. These
 * are NOT equivalent workloads, subtractable costs, GPU time or desktop FPS.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
static int compare(const void *a,const void *b)
{double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
int main(int argc,char **argv)
{
    if(argc!=2 || strcmp(argv[1],"--real-device")){
        fputs("usage: opengl-direct-boundary-cost --real-device\n",stderr);return 2;
    }
    struct DGContext *c=DGCreateContext(NULL,NULL);
    if(!c || !c->connection->device)return 77;
    unsigned char pixels[16*16*4]={0};
    if(!DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,16,16)){DGDestroyContext(c);return 1;}
    GLint expected=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&expected);
    if(expected<1024 || glGetError()!=GL_NO_ERROR){DGDestroyContext(c);return 1;}
    uint64_t config[2]={0};uint32_t count=2;
    IOReturn error=IOConnectCallScalarMethod(c->connection->device,kIOVM2DGetConfig,NULL,0,config,&count);
    if(error || count!=2 || config[0]){DGDestroyContext(c);return 1;}
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    printf("renderer=%s native_max_texture=%d; boundaries are different workloads, NOT FPS\n",glGetString(GL_RENDERER),expected);
    const char *names[]={"CPUID-unhandled-leaf","rejected-owning-trap",
        "rejected-owning-struct-method","actual-driver-config","actual-native-GL-query"};
    int failed=0;volatile uint32_t consume=0;
    for(unsigned phase=0;phase<3 && !failed;++phase)for(unsigned kind=0;kind<5 && !failed;++kind){
        double values[1000];
        for(unsigned i=0;i<1050 && !failed;++i){
            uint64_t at=mach_absolute_time();
            if(kind==0){
                uint32_t a=0x4000fffd,b=0,d=0,sub=0;
                __asm__ volatile("cpuid":"+a"(a),"=b"(b),"+c"(sub),"=d"(d)::"memory");
                consume^=a^b^sub^d;
            }else if(kind==1){
                error=IOConnectTrap4(c->connection->device,VIM_GL_EXCHANGE_TRAP,0,0,0,0);
                failed=error!=kIOReturnBadArgument;
            }else if(kind==2){
                size_t size=sizeof(struct DGReply);struct DGReply out={0};
                error=IOConnectCallStructMethod(c->connection->device,kIOVM2DDirectGLExchange,NULL,0,&out,&size);
                failed=error!=kIOReturnBadArgument;
            }else if(kind==3){
                uint64_t out[2]={0};uint32_t n=2;
                error=IOConnectCallScalarMethod(c->connection->device,kIOVM2DGetConfig,NULL,0,out,&n);
                failed=error || n!=2 || out[0]!=config[0] || out[1]!=config[1];
            }else{
                GLint value=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&value);
                failed=value!=expected || c->connection->dead;
            }
            uint64_t elapsed=mach_absolute_time()-at;
            if(i>=50)values[i-50]=elapsed*(double)tb.numer/tb.denom/1000;
        }
        if(!failed){qsort(values,1000,sizeof(*values),compare);
            printf("phase=%u kind=%s count=1000 median_us=%.3f p95_us=%.3f max_us=%.3f\n",
                phase,names[kind],values[500],values[950],values[999]);}
    }
    GLint actual=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&actual);
    failed|=actual!=expected || glGetError()!=GL_NO_ERROR || c->connection->dead;
    DGDestroyContext(c);(void)consume;
    puts(failed?"FAIL actual boundary/native-value continuity":"PASS actual boundary/native-value continuity");
    return failed?1:0;
}
