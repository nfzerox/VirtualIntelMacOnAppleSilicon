// Read-only inspection of one original Apple Archive entry, including its xattrs.
#include <AppleArchive/AppleArchive.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if(argc!=3)return 2;
    int fd=open(argv[1],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return 1;
    AAByteStream file=AAFileStreamOpenWithFD(fd,0);
    AAArchiveStream archive=file?AADecodeArchiveInputStreamOpen(file,NULL,NULL,0,1):NULL;
    AAHeader header=NULL;
    int status=1,result=1;
    while(archive&&(status=AAArchiveStreamReadHeader(archive,&header))>0) {
        char path[4096];size_t length=0;
        if(AAHeaderGetFieldStringWithKey(header,(AAFieldKey){.skey="PAT"},sizeof(path),path,&length)!=1)break;
        if(strcmp(path,argv[2]))continue;
        uint64_t size=0,offset=0;
        if(AAHeaderGetFieldBlobWithKey(header,(AAFieldKey){.skey="XAT"},&size,&offset)!=1||size>16*1024*1024)break;
        void *bytes=malloc((size_t)size);
        if(!bytes)break;
        if(AAArchiveStreamReadBlob(archive,(AAFieldKey){.skey="XAT"},bytes,(size_t)size)<0){free(bytes);break;}
        AAEntryXATBlob blob=AAEntryXATBlobCreateWithEncodedData(bytes,(size_t)size);
        free(bytes);
        if(!blob)break;
        result=0;
        for(uint32_t i=0;i<AAEntryXATBlobGetEntryCount(blob);i++) {
            size_t nameSize=0,valueSize=0;
            if(AAEntryXATBlobGetEntry(blob,i,0,NULL,&nameSize,0,NULL,&valueSize)<0){result=1;break;}
            char *name=calloc(nameSize+1,1);unsigned char *value=malloc(valueSize?:1);
            if(!name||!value||AAEntryXATBlobGetEntry(blob,i,nameSize+1,name,NULL,valueSize,value,NULL)<0)result=1;
            else {
                printf("%s: %s (%zu bytes) =",path,name,valueSize);
                for(size_t j=0;j<valueSize;j++)printf(" %02x",value[j]);
                putchar('\n');
            }
            free(name);free(value);
            if(result)break;
        }
        AAEntryXATBlobDestroy(blob);
        break;
    }
    if(header)AAHeaderDestroy(header);
    if(archive)AAArchiveStreamClose(archive);
    if(file)AAByteStreamClose(file);
    close(fd);
    return status<0?1:result;
}
