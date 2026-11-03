#pragma once
// Cold desktop admission only. Tiger has no libproc; use its own SDK's
// KERN_PROC_UID records, then the same bounded executable-path reader used by
// the control-session identity. Never infer a desktop from a process name or
// expose the process's arguments/environment in the host status report.
#include "opengl-boot-identity.h"
#include <errno.h>
#include <stdlib.h>

static inline int VIMOpenGLUserProcesses(uid_t uid,pid_t *pids,size_t capacity) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_UID,(int)uid};
    unsigned attempt;
    for(attempt=0;attempt<3;attempt++) {
        size_t bytes=0;
        if(sysctl(mib,4,NULL,&bytes,NULL,0)||bytes>4*1024*1024)return -1;
        // Allow a small amount of process churn between the two snapshot calls.
        if(bytes>4*1024*1024-16*sizeof(struct kinfo_proc))return -1;
        bytes+=16*sizeof(struct kinfo_proc);
        struct kinfo_proc *snapshot=malloc(bytes);if(!snapshot)return -1;
        if(sysctl(mib,4,snapshot,&bytes,NULL,0)) {
            int saved=errno;free(snapshot);if(saved==ENOMEM)continue;return -1;
        }
        if(bytes%sizeof(*snapshot)){free(snapshot);return -1;}
        size_t count=bytes/sizeof(*snapshot),index;
        if(count>capacity){free(snapshot);return -1;}
        for(index=0;index<count;index++) {
            if(snapshot[index].kp_eproc.e_ucred.cr_uid!=uid){free(snapshot);return -1;}
            pids[index]=snapshot[index].kp_proc.p_pid;
        }
        free(snapshot);return (int)count;
    }
    return -1;
#else
    int bytes=proc_listpids(PROC_UID_ONLY,uid,pids,(int)(capacity*sizeof(*pids)));
    return bytes>0&&bytes<=(int)(capacity*sizeof(*pids))&&bytes%(int)sizeof(*pids)==0?
        bytes/(int)sizeof(*pids):-1;
#endif
}

static inline BOOL VIMOpenGLDesktopRunning(uid_t uid) {
    if(uid<501)return NO;
    pid_t pids[2048];int count=VIMOpenGLUserProcesses(uid,pids,sizeof(pids)/sizeof(*pids));
    if(count<0)return NO;
    BOOL finder=NO,dock=NO,setup=NO;int index;
    for(index=0;index<count;index++) {
        NSString *path=VIMOpenGLProcessExecutable(pids[index]);
        if([path isEqual:@"/System/Library/CoreServices/Finder.app/Contents/MacOS/Finder"])finder=YES;
        if([path isEqual:@"/System/Library/CoreServices/Dock.app/Contents/MacOS/Dock"])dock=YES;
        if(path&&[path rangeOfString:@"/Setup Assistant.app/Contents/MacOS/"].location!=NSNotFound)setup=YES;
    }
    return finder&&dock&&!setup;
}
