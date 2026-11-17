// Local CPU/MMU isolation, not a shipping backend or upstream contribution.
// Put the universal upstream binary beside this launcher as qemu-reference-core.
// The real app still owns QMP/QGA, firmware, disk, lifecycle and all forwarding.
// This deliberately has no PVG, hybrid engine or native app display transport.
#include <mach-o/dyld.h>
#include <mach/machine.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;

// Remove only fork-specific properties, preserving every other comma option.
static char *without_property(const char *argument, const char *name) {
    char *copy = strdup(argument), *result = calloc(strlen(argument) + 1, 1);
    if (!copy || !result) exit(71);
    char *remaining = copy, *part;
    while ((part = strsep(&remaining, ","))) {
        if (!strncmp(part, name, strlen(name))) continue;
        if (*result) strcat(result, ",");
        strcat(result, part);
    }
    free(copy);
    return result;
}

int main(int argc, char **argv) {
    const char *single_setting = getenv("VIM_CPU_REFERENCE_SINGLE_THREAD");
    int single_thread = single_setting && !strcmp(single_setting, "1");
    const char *private_setting = getenv("VIM_CPU_REFERENCE_PRIVATE_RAM");
    int private_ram = private_setting && !strcmp(private_setting, "1");
    if (argc != 2 || strcmp(argv[1], "--version")) {
        const char *hybrid = getenv("LC_HVDBT");
        if (!hybrid || strcmp(hybrid, "0") ||
            !getenv("VIM_INSTALLER_DISPLAY_SOCKET")) {
            fputs("Upstream reference requires the app's explicit TCG/installer-display control.\n", stderr);
            return 64;
        }
    }
    uint32_t capacity = PATH_MAX;
    char executable[PATH_MAX], resolved[PATH_MAX];
    if (_NSGetExecutablePath(executable, &capacity) || !realpath(executable, resolved)) return 66;
    char *slash = strrchr(resolved, '/');
    if (!slash || (size_t)(slash - resolved) + sizeof("/qemu-reference-core") > sizeof(resolved)) return 66;
    strcpy(slash + 1, "qemu-reference-core");
    char **arguments = calloc((size_t)argc + 1, sizeof(*arguments));
    if (!arguments) return 71;
    arguments[0] = resolved;
    for (int i = 1; i < argc; ++i) {
        arguments[i] = argv[i];
        if (single_thread && i > 1 && !strcmp(argv[i - 1], "-accel") &&
            !strcmp(argv[i], "tcg,thread=multi"))
            arguments[i] = "tcg,thread=single";
        if (private_ram && i > 1 && !strcmp(argv[i - 1], "-object") &&
            !strncmp(argv[i], "memory-backend-shm,", strlen("memory-backend-shm,"))) {
            char *model = without_property(argv[i], "share=");
            memcpy(model + strlen("memory-backend-"), "ram", 3);
            arguments[i] = malloc(strlen(model) + sizeof(",share=off"));
            if (!arguments[i]) return 71;
            strcpy(arguments[i], model);
            strcat(arguments[i], ",share=off");
            free(model);
        }
        if (i > 1 && !strcmp(argv[i - 1], "-device")) {
            if (strstr(argv[i], "apple-gfx") || strstr(argv[i], "vim-gl")) {
                fputs("Upstream reference cannot replace a graphics-qualified launch.\n", stderr);
                return 64;
            }
            if (!strncmp(argv[i], "usb-kbd,", 8))
                arguments[i] = without_property(argv[i], "apple-layout=");
            if (!strncmp(argv[i], "virtio-sound-pci,", 17))
                arguments[i] = without_property(argv[i], "x-apple-reconfigure=");
        }
        if (i > 1 && !strcmp(argv[i - 1], "-cpu")) {
            // Upstream does not implement/advertise PCID. Do not transplant
            // the fork's CR3 code into the baseline merely to silence a warning.
            char *model = without_property(argv[i], "+pcid");
            arguments[i] = malloc(strlen(model) + sizeof(",-pcid"));
            if (!arguments[i]) return 71;
            strcpy(arguments[i], model);
            strcat(arguments[i], ",-pcid");
            free(model);
        }
    }
    fputs("[TCG reference] Upstream CPU/MMU; no PCID, PVG or hybrid; fork-only USB/audio properties omitted.\n", stderr);
    if (single_thread) fputs("[TCG reference] Explicit single-thread scheduling control.\n", stderr);
    if (private_ram) fputs("[TCG reference] Explicit private-RAM backing control; no graphics device.\n", stderr);
    const char *x86_setting = getenv("VIM_CPU_REFERENCE_X86_64");
    if (x86_setting && !strcmp(x86_setting, "1")) {
        // Diagnostic only: compare the same upstream CPU source/disk on the
        // x86 backend and its 4KiB Rosetta process, not another ARM fast path.
        posix_spawnattr_t attributes;
        int error = posix_spawnattr_init(&attributes);
        if (!error) {
            cpu_type_t preference = CPU_TYPE_X86_64;
            size_t accepted = 0;
            error = posix_spawnattr_setbinpref_np(&attributes, 1, &preference, &accepted);
            if (!error && accepted != 1) error = ENOEXEC;
            if (!error) error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETEXEC);
            if (!error) {
                fputs("[TCG reference] Explicit x86_64 execution-backend control.\n", stderr);
                pid_t replacement;
                error = posix_spawn(&replacement, resolved, NULL, &attributes, arguments, environ);
            }
            posix_spawnattr_destroy(&attributes);
        }
        errno = error;
        perror("Executing x86_64 upstream CPU reference");
        return 71;
    }
    execv(resolved, arguments);
    int failure = errno;
    perror("Executing upstream CPU reference");
    return failure == ENOENT ? 66 : 71;
}
