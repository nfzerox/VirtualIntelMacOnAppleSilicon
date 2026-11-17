// Local regression control: execute an unmodified, receipt-verified older QEMU
// beside this wrapper as qemu-historical-core. The real app owns the VM and PVG.
// Never install this wrapper as a shipping backend.
#include <mach-o/dyld.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--version")) {
        const char *hybrid = getenv("LC_HVDBT");
        if (!hybrid || strcmp(hybrid, "0")) {
            fputs("Historical runtime control requires explicit TCG selection.\n", stderr);
            return 64;
        }
    }
    char executable[PATH_MAX], resolved[PATH_MAX];
    uint32_t capacity = sizeof(executable);
    if (_NSGetExecutablePath(executable, &capacity) || !realpath(executable, resolved)) return 66;
    char *slash = strrchr(resolved, '/');
    if (!slash || (size_t)(slash - resolved) + sizeof("/qemu-historical-core") > sizeof(resolved)) return 66;
    strcpy(slash + 1, "qemu-historical-core");
    char **arguments = calloc((size_t)argc + 1, sizeof(*arguments));
    if (!arguments) return 71;
    arguments[0] = resolved;
    for (int i = 1; i < argc; i++) {
        arguments[i] = argv[i];
        if (i > 1 && !strcmp(argv[i - 1], "-device") &&
            !strncmp(argv[i], "usb-kbd,", 8)) {
            // The historical executable predates the international-layout
            // property. This diagnostic uses ANSI, its original supported form.
            const char *layout = strstr(argv[i], ",apple-layout=");
            if (layout) {
                const char *tail = strchr(layout + 1, ',');
                size_t value_length = tail ? (size_t)(tail - (layout + 14)) : strlen(layout + 14);
                if (value_length != 4 || strncmp(layout + 14, "ansi", 4)) {
                    fputs("Historical runtime control supports ANSI keyboard only.\n", stderr);
                    return 64;
                }
                size_t prefix = (size_t)(layout - argv[i]);
                size_t length = prefix + strlen(",apple-ansi=on") + (tail ? strlen(tail) : 0) + 1;
                arguments[i] = malloc(length);
                if (!arguments[i]) return 71;
                snprintf(arguments[i], length, "%.*s,apple-ansi=on%s", (int)prefix, argv[i], tail ? tail : "");
            }
        }
    }
    fputs("[Historical TCG] Unmodified November 10 runtime; PVG and app-owned controls retained.\n", stderr);
    execv(resolved, arguments);
    int failure = errno;
    perror("Executing historical TCG runtime");
    return failure == ENOENT ? 66 : 71;
}
