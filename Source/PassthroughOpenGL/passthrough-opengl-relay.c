/* Research guest transport service. Mavericks application.sb explicitly allows
 * local services under /private/var/run, even without network.client. Keep the
 * renderer unprivileged and do not alter application entitlements or profiles.
 * This copy relay is a bring-up boundary, not the eventual shared-memory path. */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define DG_LOCAL_SOCKET "/private/var/run/vim-passthrough-opengl.sock"
static unsigned clients;
static void *relay(void *opaque)
{
    int local=(int)(intptr_t)opaque,remote=socket(AF_INET,SOCK_STREAM,0),yes=1;
    unsigned char *buffer=malloc(65536);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(18091)};
    address.sin_addr.s_addr=inet_addr("10.0.2.2");
    if(!buffer || remote<0 || connect(remote,(void *)&address,sizeof(address))) goto done;
    setsockopt(remote,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    struct pollfd descriptors[2]={{local,POLLIN,0},{remote,POLLIN,0}};
    for(;;) {
        int result=poll(descriptors,2,-1);
        if(result<0 && errno==EINTR) continue;
        if(result<0) break;
        for(int i=0;i<2;++i) {
            if(descriptors[i].revents&POLLIN) {
                ssize_t count=read(descriptors[i].fd,buffer,65536);
                if(count<0 && errno==EINTR) continue;
                if(count<=0) goto done;
                for(ssize_t offset=0;offset<count;) {
                    ssize_t n=write(descriptors[1-i].fd,buffer+offset,(size_t)(count-offset));
                    if(n<0 && errno==EINTR) continue;
                    if(n<=0) goto done;
                    offset+=n;
                }
            } else if(descriptors[i].revents&(POLLERR|POLLHUP|POLLNVAL)) goto done;
        }
    }
done:
    free(buffer);if(remote>=0) close(remote);close(local);
    __sync_sub_and_fetch(&clients,1);return NULL;
}
int main(void)
{
    signal(SIGPIPE,SIG_IGN);
    int listener=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un address={.sun_family=AF_UNIX};
    strlcpy(address.sun_path,DG_LOCAL_SOCKET,sizeof(address.sun_path));
    struct stat existing;
    if(!lstat(DG_LOCAL_SOCKET,&existing)) {
        /* Never remove another file or a live listener. */
        int test=socket(AF_UNIX,SOCK_STREAM,0);
        int active=test>=0 && !connect(test,(void *)&address,sizeof(address));
        if(test>=0) close(test);
        if(active || !S_ISSOCK(existing.st_mode) || existing.st_uid!=geteuid() || unlink(DG_LOCAL_SOCKET)) {
            fprintf(stderr,"PassthroughOpenGL local endpoint already exists or is not owned by this service\n");return 1;
        }
    } else if(errno!=ENOENT) { perror("local endpoint");return 1; }
    if(listener<0 || bind(listener,(void *)&address,sizeof(address)) ||
       chmod(DG_LOCAL_SOCKET,0666) || listen(listener,32)) { perror("PassthroughOpenGL local listener");return 1; }
    /* Root is needed only to create the endpoint, not to relay graphics. */
    if(geteuid()==0) {
        struct passwd *user=getpwnam("nobody");
        if(!user || setgroups(0,NULL) || setgid(user->pw_gid) || setuid(user->pw_uid)) {
            perror("drop relay privileges");return 1;
        }
    }
    fprintf(stderr,"PassthroughOpenGL local relay ready\n");
    for(;;) {
        int fd=accept(listener,NULL,NULL);
        if(fd<0) { if(errno==EINTR) continue;perror("relay accept");return 1; }
        if(__sync_add_and_fetch(&clients,1)>128) { __sync_sub_and_fetch(&clients,1);close(fd);continue; }
        pthread_t thread;
        if(pthread_create(&thread,NULL,relay,(void *)(intptr_t)fd)) { __sync_sub_and_fetch(&clients,1);close(fd); }
        else pthread_detach(thread);
    }
}
