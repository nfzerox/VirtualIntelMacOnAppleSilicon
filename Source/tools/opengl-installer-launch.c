/* Recovery-only exec wrapper. No global launchd environment or installed-OS
 * injection. Native minstall remains authoritative: older apps need a scoped
 * final-confirmation adapter; High Sierra needs its native document argument. */
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include "opengl-installer-paths.h"
#define ADAPTER "/var/run/VirtualIntelMac/opengl-installer-automation.dylib"
int main(int argc,char **argv){
    if(argc<2||geteuid()!=0)return 77;
    if(!strcmp(argv[1],VIM_MACOS_INSTALLER)){
        // High Sierra's AppKit does not turn -f into an open-file event.
        // Add its real -NSOpen argument AFTER Language Chooser has launched
        // us, so LCA does not interpret that document itself. exec preserves
        // LCA's child PID/supervision. The original signed app stays intact;
        // this path never injects the older confirmation adapter.
        struct utsname system;struct stat authorization,configuration,installer;struct statfs volume;
        const char *document="/System/Installation/Packages/Extras/minstallconfig.xml";
        if(uname(&system)||strncmp(system.release,"17.",3)||
           lstat("/var/tmp/VirtualIntelMac-InstallerAuthorization.plist",&authorization)||
           !S_ISREG(authorization.st_mode)||authorization.st_uid||(authorization.st_mode&0077)||authorization.st_nlink!=1||
           lstat("/var/tmp/VirtualIntelMac-minstallconfig.plist",&configuration)||
           !S_ISREG(configuration.st_mode)||configuration.st_uid||(configuration.st_mode&0077)||configuration.st_nlink!=1||
           lstat(argv[1],&installer)||!S_ISREG(installer.st_mode)||statfs(argv[1],&volume)||!(volume.f_flags&MNT_RDONLY))return 77;
        int matched=0;for(int i=2;i+1<argc;++i)
            if(!strcmp(argv[i],"-f")&&!strcmp(argv[i+1],document))matched=1;
        if(!matched)return 77;
        char **arguments=calloc((size_t)argc+2,sizeof(*arguments));if(!arguments)return 71;
        for(int i=1;i<argc;++i)arguments[i-1]=argv[i];
        arguments[argc-1]="-NSOpen";arguments[argc]=(char *)document;
        unsetenv("DYLD_INSERT_LIBRARIES");execv(argv[1],arguments);
        perror("Starting the macOS installer");free(arguments);return 1;
    }
    int lion=!strcmp(argv[1],VIM_LION_INSTALLER);
    if(!lion&&strcmp(argv[1],VIM_OSX_INSTALLER))return 77;
    const char *automaticPath=VIMOpenGLAutomaticInstaller(lion);
    struct stat state,adapter,automatic;struct statfs filesystem;
    if(!lstat("/var/tmp/VirtualIntelMac-InstallerAuthorization.plist",&state)&&
       S_ISREG(state.st_mode)&&state.st_uid==0&&!(state.st_mode&0077)&&state.st_nlink==1&&
       !lstat(ADAPTER,&adapter)&&S_ISREG(adapter.st_mode)&&adapter.st_uid==0&&adapter.st_nlink==1&&!(adapter.st_mode&0022)){
        // This alternate is immutable recovery media, not a writable user app.
        // Never fall back to a superficially successful but non-automated launch
        // if the generated automatic copy is absent or has unsafe permissions.
        if(lstat(automaticPath,&automatic)||!S_ISREG(automatic.st_mode)||automatic.st_nlink!=1||
           (automatic.st_mode&0022)||statfs(automaticPath,&filesystem)||!(filesystem.f_flags&MNT_RDONLY))return 77;
        if(setenv("DYLD_INSERT_LIBRARIES",ADAPTER,1))return 1;
        argv[1]=(char *)automaticPath;
    }else unsetenv("DYLD_INSERT_LIBRARIES");
    execv(argv[1],argv+1);perror("Starting the OS X installer");return 1;
}
