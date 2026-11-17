/* Read original Big Sur digest.db using Darwin's Berkeley DB 1.85 API. */
#include "host-install-bigsur-digest.h"
#include <sys/types.h>
#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

int VIMVisitBigSurDigests(const char *path,VIMBigSurDigestVisitor visit,void *context) {
    if(!path||!visit){errno=EINVAL;return -1;}
    DB *db = dbopen(path, O_RDONLY, 0, DB_BTREE, NULL);
    if (!db) return -1;
    DBT key = {0}, value = {0};
    int status,problem=0;
    unsigned action = R_FIRST;
    while ((status = db->seq(db, &key, &value, action)) == RET_SUCCESS) {
        action = R_NEXT;
        if (key.size != 16 || value.size != 36) {
            problem=EBADMSG;
            break;
        }
        problem=visit(key.data,value.data,context);
        if(problem)break;
    }
    if(!problem&&status!=RET_SPECIAL)problem=errno?:EIO;
    if(db->close(db)!=RET_SUCCESS&&!problem)problem=errno?:EIO;
    if(problem){errno=problem;return -1;}
    return 0;
}

#ifndef VIM_BIGSUR_DIGEST_LIBRARY
static int WriteRecord(const uint8_t key[16],const uint8_t value[36],void *context) {
    FILE *file=context;
    return fwrite(key,16,1,file)==1&&fwrite(value,36,1,file)==1?0:(errno?:EIO);
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("Usage: host-install-bigsur-digest DIGEST.DB\n", stderr);
        return 1;
    }
    if(VIMVisitBigSurDigests(argv[1],WriteRecord,stdout)||fflush(stdout)) {
        perror("Big Sur digest records");return 1;
    }
    return 0;
}
#endif
