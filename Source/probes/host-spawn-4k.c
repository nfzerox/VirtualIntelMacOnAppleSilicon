/* Exercise the SDK's documented spawn attribute without entitlements. */
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
int main(int argc, char **argv)
{
    if (argc < 2) return 64;
    posix_spawnattr_t attr;
    int error = posix_spawnattr_init(&attr);
    if (error) return 1;
    error = posix_spawnattr_set_4k_page_size_np(&attr);
    if (error) {
        fprintf(stderr, "4K attribute: %s\n", strerror(error));
        posix_spawnattr_destroy(&attr);
        return 2;
    }
    pid_t child;
    // Diagnostic libraries belong to the child, not this launcher (which
    // deliberately does not load PVG or its Objective-C classes).
    const char *libraries = getenv("PVG_SPAWN_INSERT_LIBRARIES");
    if (libraries && setenv("DYLD_INSERT_LIBRARIES", libraries, 1)) return 3;
    error = posix_spawn(&child, argv[1], NULL, &attr, argv + 1, environ);
    posix_spawnattr_destroy(&attr);
    if (error) {
        fprintf(stderr, "spawn: %s\n", strerror(error));
        return 3;
    }
    int status;
    if (waitpid(child, &status, 0) < 0) return 4;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
