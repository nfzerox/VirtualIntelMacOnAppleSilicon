// Read-only period-kernel proc ABI probe. Does not create a control identity,
// change login policy, request graphics access or require administrator access.
#include <libproc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    struct proc_bsdinfo info;
    char path[PROC_PIDPATHINFO_MAXSIZE];
    memset(&info, 0, sizeof(info));
    memset(path, 0, sizeof(path));
    int bytes = proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    int length = proc_pidpath(getpid(), path, sizeof(path));
    int valid = bytes == (int)sizeof(info) && info.pbi_pid == (uint32_t)getpid() &&
        info.pbi_uid == getuid() && info.pbi_start_tvsec &&
        info.pbi_start_tvusec < 1000000 && length > 0 && path[0] == '/';
    printf("proc_bsdinfo expected=%zu actual=%d PID/UID/birth/path valid=%d\n",
        sizeof(info), bytes, valid);
    return valid ? 0 : 1;
}
