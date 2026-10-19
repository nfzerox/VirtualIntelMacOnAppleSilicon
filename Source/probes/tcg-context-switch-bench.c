/* Local TCG diagnostic: blocking pipe round trips between two processes.
 * No guest configuration changes. Build for the guest, run several trials.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void transfer(int fd, int writing)
{
    char value = 1;
    ssize_t n;
    do {
        n = writing ? write(fd, &value, 1) : read(fd, &value, 1);
    } while (n < 0 && errno == EINTR);
    if (n != 1) {
        perror("pipe transfer");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    int a[2], b[2], status;
    long count = argc == 2 ? strtol(argv[1], NULL, 10) : 10000;
    struct timespec start, end;
    if (count <= 0 || count > 1000000 || pipe(a) || pipe(b)) {
        return 1;
    }
    pid_t child = fork();
    if (child < 0) {
        return 1;
    }
    if (child == 0) {
        close(a[1]);
        close(b[0]);
        for (long i = 0; i < count; ++i) {
            transfer(a[0], 0);
            transfer(b[1], 1);
        }
        _exit(0);
    }
    close(a[0]);
    close(b[1]);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (long i = 0; i < count; ++i) {
        transfer(a[1], 1);
        transfer(b[0], 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
        return 1;
    }
    double seconds = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("roundtrips=%ld seconds=%.6f us_per_roundtrip=%.3f\n",
           count, seconds, seconds * 1e6 / count);
    return 0;
}
