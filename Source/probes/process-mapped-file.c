// Read-only native backing-file identity for a reported process address.
#include <libproc.h>
#include <sys/proc_info.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) return 64;
    char *end;
    long pid = strtol(argv[1], &end, 10);
    if (*end || pid <= 0 || pid > INT32_MAX) return 64;
    uint64_t address = strtoull(argv[2], &end, 0);
    if (*end) return 64;
    struct proc_regionwithpathinfo region;
    int size = proc_pidinfo((int)pid, PROC_PIDREGIONPATHINFO, address,
                           &region, sizeof(region));
    if (size != sizeof(region)) { perror("proc_pidinfo"); return 1; }
    printf("pid=%ld address=0x%" PRIx64 " mapping=0x%" PRIx64
           " length=%" PRIu64 " offset=%" PRIu64 " inode=%" PRIu64
           " device=%u path=%s\n", pid, address,
        region.prp_prinfo.pri_address, region.prp_prinfo.pri_size,
        region.prp_prinfo.pri_offset, region.prp_vip.vip_vi.vi_stat.vst_ino,
        (unsigned)region.prp_vip.vip_vi.vi_stat.vst_dev, region.prp_vip.vip_path);
    return 0;
}
