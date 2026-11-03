// Tiger lacks launchctl bsexec. Use its native task bootstrap-port API in
// this short-lived child only, then permanently drop to the actual console
// user before exec. No injection, parent/global environment change or daemon.
#import "opengl-period-foundation.h"
#include "opengl-boot-identity.h"
#include "opengl-guest-machine.h"
#include <mach/mach.h>
#include <mach/task_special_ports.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
static int Fail(const char *why){fprintf(stderr,"Tiger desktop connection: %s\n",why);return 75;}
static BOOL Allowed(const char *path){
    return !strcmp(path,"/usr/bin/open")||!strcmp(path,"/bin/launchctl")||
        !strcmp(path,"/Library/Application Support/Virtual Intel Mac/GuestTools/guest-display")||
        !strcmp(path,"/Library/Application Support/Virtual Intel Mac/GuestTools/guest-host-connection");
}
int main(int argc,char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];struct utsname kernel;struct stat console,program;
    if(argc<3||strcmp(argv[1],"--")||geteuid()!=0||uname(&kernel)||strncmp(kernel.release,"8.",2)||
       !VIMOpenGLGuestMachine()||!Allowed(argv[2])||lstat(argv[2],&program)||!S_ISREG(program.st_mode)||
       program.st_uid||(program.st_mode&(0022|S_ISUID|S_ISGID))||!(program.st_mode&0111))return 77;
    if(stat("/dev/console",&console)||console.st_uid<501)return Fail("sign in to this virtual Mac’s desktop first");
    int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_UID,(int)console.st_uid};size_t bytes=0;
    if(sysctl(mib,4,NULL,&bytes,NULL,0)||!bytes||bytes>1024*1024)return Fail("the console process list is unavailable");
    // Leave bounded headroom for processes created between the two sysctls.
    size_t capacity=bytes+16*sizeof(struct kinfo_proc);if(capacity>1024*1024)return 75;
    struct kinfo_proc *processes=malloc(capacity);bytes=capacity;
    if(!processes)return 74;
    if(sysctl(mib,4,processes,&bytes,NULL,0)||bytes>capacity||bytes%sizeof(*processes)){free(processes);return 75;}
    pid_t finder=0;VIMOpenGLProcessInfo birth;
    size_t i;for(i=0;i<bytes/sizeof(*processes);i++){
        pid_t pid=processes[i].kp_proc.p_pid;
        if(pid>1&&processes[i].kp_eproc.e_ucred.cr_uid==console.st_uid&&
           [[VIMOpenGLProcessExecutable(pid) description] isEqual:@"/System/Library/CoreServices/Finder.app/Contents/MacOS/Finder"]){
            if(finder){free(processes);return Fail("the active Finder session is ambiguous");}
            finder=pid;
        }
    }free(processes);
    if(!finder||!VIMOpenGLReadProcessBirth(finder,&birth)||birth.pbi_uid!=console.st_uid)return 75;
    task_t task=MACH_PORT_NULL;mach_port_t port=MACH_PORT_NULL;
    kern_return_t result=task_for_pid(mach_task_self(),finder,&task);
    if(result!=KERN_SUCCESS)return Fail("the native Finder task could not be opened");
    result=task_get_bootstrap_port(task,&port);mach_port_deallocate(mach_task_self(),task);
    if(result!=KERN_SUCCESS||!MACH_PORT_VALID(port))return Fail("the native desktop bootstrap port is unavailable");
    VIMOpenGLProcessInfo check;struct stat current;
    BOOL same=VIMOpenGLReadProcessBirth(finder,&check)&&check.pbi_uid==console.st_uid&&
        check.pbi_start.tv_sec==birth.pbi_start.tv_sec&&check.pbi_start.tv_usec==birth.pbi_start.tv_usec&&
        !stat("/dev/console",&current)&&current.st_uid==console.st_uid;
    if(!same){mach_port_deallocate(mach_task_self(),port);return 75;}
    result=task_set_bootstrap_port(mach_task_self(),port);mach_port_deallocate(mach_task_self(),port);
    if(result!=KERN_SUCCESS)return Fail("the child could not enter the desktop session");
    struct passwd *user=getpwuid(console.st_uid);
    if(!user||!user->pw_name||!user->pw_dir)return 77;
    gid_t group=user->pw_gid;char *username=strdup(user->pw_name),*directory=strdup(user->pw_dir);
    if(!username||!directory)return 74;
    if(initgroups(username,group)||setgid(group)||setuid(console.st_uid)||
       geteuid()!=console.st_uid||getuid()!=console.st_uid)return 77;
    char *home=NULL,*name=NULL,*login=NULL;
    if(asprintf(&home,"HOME=%s",directory)<0||asprintf(&name,"USER=%s",username)<0||
       asprintf(&login,"LOGNAME=%s",username)<0)return 74;
    char *environment[]={"PATH=/usr/bin:/bin:/usr/sbin:/sbin",home,name,login,NULL};
    [pool drain];execve(argv[2],argv+2,environment);perror("Tiger desktop exec");return 74;
}
