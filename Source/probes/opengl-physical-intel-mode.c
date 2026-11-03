/* Physical-reference-only AppleGraphicsControl operations adapted from
 * gfxCardStatus Classes/GSMux.m, 2077dbfc25bf87d11e14d1dbbbfeabb533c6bd21.
 * https://github.com/codykrieger/gfxCardStatus
 *
 * Copyright (c) 2010-2020, Cody Krieger. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of gfxCardStatus nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL CODY KRIEGER BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <IOKit/IOKitLib.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
static kern_return_t get(io_connect_t c,uint64_t state,uint64_t *value){
    uint64_t args[]={1,state};uint32_t count=1;
    return IOConnectCallScalarMethod(c,3,args,2,value,&count);
}
static kern_return_t set(io_connect_t c,uint64_t state,uint64_t value){
    uint64_t args[]={1,state,value};return IOConnectCallScalarMethod(c,2,args,3,NULL,NULL);
}
int main(int argc,char **argv){
    int integrated=argc>=2&&!strcmp(argv[1],"--integrated");
    int dynamic=argc==2&&!strcmp(argv[1],"--dynamic");
    int command=argc>=4&&integrated&&!strcmp(argv[2],"--");
    if((argc!=2&&!command)||(!integrated&&!dynamic&&strcmp(argv[1],"--status")))return 64;
    char model[64]={0};size_t bytes=sizeof(model);
    /* Only the two supplied physical references with inspected native
     * AppleGraphicsControl mux interfaces. This is never guest support. */
    if(sysctlbyname("hw.model",model,&bytes,NULL,0)||
       (strcmp(model,"MacBookPro8,2")&&strcmp(model,"MacBookPro11,3")))return 77;
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AppleGraphicsControl"));
    io_connect_t c=IO_OBJECT_NULL;if(!service)return 77;
    kern_return_t error=IOServiceOpen(service,mach_task_self(),0,&c);IOObjectRelease(service);
    if(error)return 1;
    error=IOConnectCallScalarMethod(c,0,NULL,0,NULL,NULL);
    uint64_t gpu=0;
    if(!error)error=get(c,7,&gpu);
    if(!error&&dynamic){
        /* Upstream's normal automatic-switching restore sequence, useful
         * after an aborted reference workload requested an offline GPU. */
        error=set(c,1,1);
        if(!error)error=set(c,5,0);
        if(!error)error=set(c,4,1);
    }
    if(!error&&integrated){
        {
            /* gfxCardStatus issues all policy requests before forceSwitch.
             * Both audited models can report a general error during
             * a transition; don't mistake it for a completed GPU switch.
             * a policy transition (also observed on 8,2 / Snow Leopard).
             * Permission/unsupported errors still stop. Actual state below,
             * not these requests, authorizes a reference workload. */
            const uint64_t states[]={4,0,5},values[]={0,1,2};
            for(unsigned i=0;i<3&&!error;++i){
                kern_return_t request=set(c,states[i],values[i]);
                printf("mux-request state=%llu result=%x\n",states[i],request);
                if(request&&request!=kIOReturnError)error=request;
            }
            if(!error)sleep(1); /* Upstream's native mux-transition interval. */
        }
        if(!error)error=get(c,7,&gpu);
        if(!error&&!gpu){
            error=set(c,2,0);             /* one native switch, only if discrete */
            /* Lion acknowledges the request before completing the mux switch.
             * Poll its actual state, never toggle again or start the GL probe
             * against the old GPU. This is a bounded reference-only wait. */
            for(unsigned attempt=0;!error&&!gpu&&attempt<500;++attempt){
                usleep(10000);
                error=get(c,7,&gpu);
            }
        }
    }
    if(!error)error=get(c,7,&gpu);
    // Upstream queries state 7, not setters 4/5. Those setters are not
    // necessarily readable states; printing initialized zeros was misleading.
    printf("mux-status=%x integrated=%llu\n",error,(unsigned long long)gpu);
    int result=error?1:(integrated&&!gpu?75:0);
    if(!result&&command){
        // Hold the real mux client for the lifetime of a bounded reference
        // probe. Its own GL_VENDOR/GL_RENDERER check remains authoritative.
        fflush(stdout);pid_t child=fork();
        if(child==0){execv(argv[3],argv+3);_exit(127);}
        int status=0;if(child<0||waitpid(child,&status,0)!=child)result=1;
        else result=WIFEXITED(status)?WEXITSTATUS(status):1;
    }
    IOConnectCallScalarMethod(c,1,NULL,0,NULL,NULL);IOServiceClose(c);
    return result;
}
