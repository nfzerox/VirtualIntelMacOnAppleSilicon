#include "../PassthroughOpenGL/display-surface-message.h"
#include <servers/bootstrap.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
extern char **environ;
int main(int argc,char **argv)
{
    if(argc==2){
        mach_port_t target=0,reply=0;
        assert(!bootstrap_look_up(bootstrap_port,argv[1],&target));
        assert(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&reply));
        CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
        int values[]={640,480,4,0x42475241};
        for(unsigned i=0;i<4;++i){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);CFDictionarySetValue(d,keys[i],n);CFRelease(n);}
        IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);assert(s);
        assert(!IOSurfaceLock(s,0,NULL));*(uint32_t *)IOSurfaceGetBaseAddress(s)=0xff123456;
        assert(!IOSurfaceUnlock(s,0,NULL));
        assert(!VIMSurfaceFrameSend(target,reply,s,3,17,1,640,480,5000));
        CFRelease(s); /* receiver's capability now keeps the allocation alive */
        uint32_t status=VIMSurfaceFailed;
        assert(!VIMSurfaceReleaseReceive(reply,3,17,1,&status,5000));
        assert(status==VIMSurfaceGPUCompleted);
        mach_port_destroy(mach_task_self(),reply);mach_port_deallocate(mach_task_self(),target);return 0;
    }
    mach_port_t receive=0;assert(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&receive));
    assert(!mach_port_insert_right(mach_task_self(),receive,receive,MACH_MSG_TYPE_MAKE_SEND));
    char name[128];snprintf(name,sizeof(name),"com.virtualintelmac.test.surface.%d",getpid());
    assert(!bootstrap_register(bootstrap_port,name,receive));
    pid_t child;char *args[]={argv[0],name,NULL};assert(!posix_spawn(&child,argv[0],NULL,NULL,args,environ));
    struct VIMSurfaceFrameMessage frame;IOSurfaceRef surface=NULL;
    assert(!VIMSurfaceFrameReceive(receive,geteuid(),child,&frame,&surface,5000));
    assert(frame.generation==3 && frame.ticket==17 && frame.slot==1 && IOSurfaceGetWidth(surface)==640);
    assert(!IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL));
    assert(*(uint32_t *)IOSurfaceGetBaseAddress(surface)==0xff123456);
    assert(!IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL));CFRelease(surface);
    assert(!VIMSurfaceFrameRelease(&frame,VIMSurfaceGPUCompleted,5000));
    assert(VIMSurfaceFrameRelease(&frame,VIMSurfaceGPUCompleted,5000)==KERN_INVALID_ARGUMENT);
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    mach_port_t stale=0,once=0;mach_msg_type_name_t disposition=0;
    assert(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&stale));
    assert(!mach_port_extract_right(mach_task_self(),stale,MACH_MSG_TYPE_MAKE_SEND_ONCE,&once,&disposition));
    frame.header.msgh_remote_port=once;frame.generation=2;
    assert(!VIMSurfaceFrameRelease(&frame,VIMSurfaceGPUCompleted,5000));
    uint32_t staleStatus=99;
    assert(VIMSurfaceReleaseReceive(stale,3,17,1,&staleStatus,5000)==KERN_INVALID_ARGUMENT);
    assert(staleStatus==99);mach_port_destroy(mach_task_self(),stale);
    /* launchd retires this test-only registration when its receive right dies;
     * replacing an existing registration with NULL is rejected on this host. */
    mach_port_destroy(mach_task_self(),receive);
    puts("PASS cross-process IOSurface capability, renderer PID authentication, release identity, stale rejection and one-use replies");return 0;
}
