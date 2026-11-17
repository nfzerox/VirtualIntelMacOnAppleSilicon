#pragma once

#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

static bool signGuestCache(const char* path)
{
    // Keep the SHA-1 primary CodeDirectory used by these guests and a
    // SHA-256 alternate. Modern hosts reject SHA-1-only signatures.
    const char* tool = "/usr/bin/codesign";
    const char* args[] = {tool, "--force", "--sign", "-", "--digest-algorithm=sha1,sha256", path, nullptr};
    pid_t child;
    int status;
    if (posix_spawn(&child, tool, nullptr, nullptr, const_cast<char* const*>(args), environ))
        return false;
    pid_t done;
    do {
        done = waitpid(child, &status, 0);
    } while (done == -1 && errno == EINTR);
    return done == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
