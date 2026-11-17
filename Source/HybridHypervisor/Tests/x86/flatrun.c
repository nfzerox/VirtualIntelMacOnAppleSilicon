// Runs a flat x86-64 image under Rosetta exactly as the engine would: same bytes at the same address, real Darwin syscalls.
// Build for x86_64. Usage: flatrun image.macho [arg0 arg1]
#include <fcntl.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: flatrun image [a0 a1]\n");
        return 64;
    }
    int fd = open(argv[1], O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) {
        perror("open");
        return 1;
    }
    unsigned char *file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    struct mach_header_64 *h = (void *)file;
    unsigned char *lc = (unsigned char *)(h + 1);
    uint64_t entry = 0;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        struct load_command *c = (void *)lc;
        if (c->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *s = (void *)lc;
            if (strcmp(s->segname, "__LINKEDIT") && s->vmsize) {
                void *at = mmap((void *)s->vmaddr, s->vmsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
                if (at != (void *)s->vmaddr) {
                    perror("mmap fixed");
                    return 1;
                }
                memcpy(at, file + s->fileoff, s->filesize);
                if (!strcmp(s->segname, "__TEXT") && mprotect(at, s->vmsize, PROT_READ | PROT_EXEC)) {
                    perror("mprotect");
                    return 1;
                }
            }
        } else if (c->cmd == LC_UNIXTHREAD) {
            entry = ((uint64_t *)(lc + 16))[16];
        }
        lc += c->cmdsize;
    }
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    unsigned long a0 = argc > 2 ? strtoul(argv[2], 0, 0) : 0, a1 = argc > 3 ? strtoul(argv[3], 0, 0) : 0;
    ((void (*)(unsigned long, unsigned long))entry)(a0, a1);
    return 0;
}
