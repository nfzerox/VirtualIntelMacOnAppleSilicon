/* Native rc.cdrom.local control/minstall handoff. No account changes. */
#include <objc/objc.h>
#include <CoreFoundation/CoreFoundation.h>
#include "opengl-guest-machine.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define AGENT "/System/Installation/Packages/Extras/VirtualIntelMac/opengl-guest-agent"
#define PREPARE "/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-install-config"
int main(int argc,char **argv) {
    (void)argv;struct utsname system;struct stat file,log;struct statfs volume;
    if(argc!=1||geteuid()!=0||uname(&system)||strncmp(system.release,"8.",2)||!VIMOpenGLGuestMachine())return 77;
    if(lstat(AGENT,&file)||!S_ISREG(file.st_mode)||file.st_nlink!=1||!(file.st_mode&0111)||
       statfs(AGENT,&volume)||!(volume.f_flags&MNT_RDONLY))return 77;
    int fd=open("/var/tmp/VirtualIntelMac-Control.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW,0600);
    if(fd<0)return 74;
    if(fstat(fd,&log)||!S_ISREG(log.st_mode)||log.st_uid||log.st_nlink!=1||(log.st_mode&0077)||log.st_size>262144){close(fd);return 77;}
    int input=open("/dev/null",O_RDONLY);if(input<0){close(fd);return 74;}
    // Keep the recovery/control owner alive while Disk Arbitration prepares
    // the volumes. A slow mount must not also make diagnostics unreachable.
    pid_t child=fork();if(child<0){close(input);close(fd);return 71;}
    if(child==0){
        if(setsid()<0||dup2(input,0)<0||dup2(fd,1)<0||dup2(fd,2)<0)_exit(74);
        if(input>2)close(input);if(fd>2)close(fd);
        execl(AGENT,AGENT,"--device","/dev/cu.serial2",(char *)0);
        perror("Starting Tiger control service");_exit(71);
    }
    // Native rc.cdrom checks /etc/minstallconfig.xml AFTER this hook. Prepare
    // an authorized automatic install now, before LCA could open its language
    // picker. The configuration explicitly selects English. Disabled/manual
    // policies publish no file and retain Apple's normal language picker.
    if(!lstat(PREPARE,&file)&&S_ISREG(file.st_mode)&&(file.st_mode&0111)) {
        pid_t prepare=fork();if(prepare<0){close(input);close(fd);return 71;}
        if(prepare==0){
            if(dup2(input,0)<0||dup2(fd,1)<0||dup2(fd,2)<0)_exit(74);
            if(input>2)close(input);if(fd>2)close(fd);
            execl(PREPARE,PREPARE,"--prepare-automatic",(char *)0);_exit(71);
        }
        int status=0;pid_t waited;do{waited=waitpid(prepare,&status,0);}while(waited<0&&errno==EINTR);
        // A failed preparation must not stop the recovery control channel or
        // silently approve another volume. Its diagnostic remains in RAM.
        if(waited<0||!WIFEXITED(status)||WEXITSTATUS(status)) {
            static const char message[]="Native automatic installation was not authorized.\n";
            (void)write(fd,message,sizeof(message)-1);
        }
    }
    close(input);close(fd);return 0;
}
