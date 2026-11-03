#include "VIMHFSInstallerDisk.h"
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include <libkern/OSByteOrder.h>

static uint32_t CRC(const void *data,size_t size) {
    const uint8_t *bytes=data;uint32_t crc=~0u;
    for(size_t i=0;i<size;i++){crc^=bytes[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
    return ~crc;
}
static int Write(int fd,const void *data,size_t size,off_t offset) {
    const uint8_t *bytes=data;
    while(size){ssize_t n=pwrite(fd,bytes,size,offset);if(n<0&&errno==EINTR)continue;
        if(n<=0){if(!n)errno=EIO;return -1;}bytes+=n;size-=n;offset+=n;}
    return 0;
}
int VIMCopyHFSInstallerVolume(const char *source,const char *destination,
    bool (*progress)(uint64_t,uint64_t,void *),void *context) {
    int input=-1,output=-1,result=-1,saved=0;uint8_t *buffer=NULL;struct stat st;
    uint64_t blocks=0;uint32_t blockSize=0;uint8_t header[65536];
    input=open(source,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(input<0||fstat(input,&st))goto done;
    if(!S_ISBLK(st.st_mode)&&!S_ISCHR(st.st_mode)){errno=EINVAL;goto done;}
    if(ioctl(input,DKIOCGETBLOCKCOUNT,&blocks)||ioctl(input,DKIOCGETBLOCKSIZE,&blockSize))goto done;
    if(blockSize<512||blockSize>65536||(blockSize&(blockSize-1))||
       blocks<2048||blocks>(64ULL<<30)/blockSize){errno=ENOEXEC;goto done;}
    uint64_t size=blocks*blockSize;
    size_t headerBytes=blockSize>2048?blockSize:2048;
    ssize_t count;do{count=pread(input,header,headerBytes,0);}while(count<0&&errno==EINTR);
    if(count!=(ssize_t)headerBytes){if(count>=0)errno=EIO;goto done;}
    const uint8_t *hfs=header+1024;
    uint32_t allocation=OSReadBigInt32(hfs,40),total=OSReadBigInt32(hfs,44);
    if((memcmp(hfs,"H+",2)&&memcmp(hfs,"HX",2))||allocation<512||
       (allocation&(allocation-1))||!total||(uint64_t)allocation*total>size){errno=ENOEXEC;goto done;}
    output=open(destination,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(output<0||ftruncate(output,(off_t)size))goto done;
    buffer=malloc(1048576);if(!buffer){errno=ENOMEM;goto done;}
    uint64_t copied=0;
    while(copied<size){
        if(progress&&!progress(copied,size,context)){errno=ECANCELED;goto done;}
        size_t amount=(size_t)(size-copied<1048576?size-copied:1048576);
        ssize_t n=pread(input,buffer,amount,(off_t)copied);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){if(!n)errno=EIO;goto done;}
        if(Write(output,buffer,(size_t)n,(off_t)copied))goto done;copied+=(uint64_t)n;
    }
    int flushed;do{flushed=fsync(output);}while(flushed<0&&errno==EINTR);
    if(flushed)goto done;
    if(progress&&!progress(size,size,context)){errno=ECANCELED;goto done;}
    result=0;
done:
    saved=errno;free(buffer);if(input>=0)close(input);
    if(output>=0){close(output);if(result)unlink(destination);}
    errno=saved;return result;
}
static void GUID(uint8_t *output) {
    uuid_t value;uuid_generate_random(value);memcpy(output,value,16);
    output[0]=value[3];output[1]=value[2];output[2]=value[1];output[3]=value[0];
    output[4]=value[5];output[5]=value[4];output[6]=value[7];output[7]=value[6];
}
static void Header(uint8_t *header,uint64_t current,uint64_t other,uint64_t entries,
                   uint64_t sectors,const uint8_t *guid,uint32_t entriesCRC) {
    memset(header,0,512);memcpy(header,"EFI PART",8);
    OSWriteLittleInt32(header,8,0x10000);OSWriteLittleInt32(header,12,92);
    OSWriteLittleInt64(header,24,current);OSWriteLittleInt64(header,32,other);
    OSWriteLittleInt64(header,40,34);OSWriteLittleInt64(header,48,sectors-34);
    memcpy(header+56,guid,16);OSWriteLittleInt64(header,72,entries);
    OSWriteLittleInt32(header,80,128);OSWriteLittleInt32(header,84,128);
    OSWriteLittleInt32(header,88,entriesCRC);OSWriteLittleInt32(header,16,CRC(header,92));
}
int VIMWrapHFSInstallerDisk(const char *source,const char *destination,
    bool (*progress)(uint64_t,uint64_t,void *),void *context) {
    int input=-1,output=-1,result=-1,saved=0;uint8_t *buffer=NULL;
    struct stat st;uint8_t hfs[512],mbr[512]={0},header[512],table[16384]={0},guid[16];
    input=open(source,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(input<0)goto done;
    if(fstat(input,&st))goto done;
    if(!S_ISREG(st.st_mode)||st.st_size<1048576||st.st_size%512){errno=ENOEXEC;goto done;}
    ssize_t headerSize;
    do{headerSize=pread(input,hfs,sizeof(hfs),1024);}while(headerSize<0&&errno==EINTR);
    if(headerSize!=sizeof(hfs)){if(headerSize>=0)errno=EIO;goto done;}
    uint32_t blockSize=OSReadBigInt32(hfs,40),blocks=OSReadBigInt32(hfs,44);
    if((memcmp(hfs,"H+",2)&&memcmp(hfs,"HX",2))||blockSize<512||
       (blockSize&(blockSize-1))||!blocks||(uint64_t)blocks*blockSize>(uint64_t)st.st_size){errno=ENOEXEC;goto done;}
    uint64_t first=2048,last=first+(uint64_t)st.st_size/512-1,sectors=last+34;
    if(sectors>UINT32_MAX){errno=EFBIG;goto done;}
    // GUID partition type 48465300-0000-11AA-AA11-00306543ECAC, disk byte order.
    static const uint8_t type[16]={0x00,0x53,0x46,0x48,0,0,0xaa,0x11,0xaa,0x11,0,0x30,0x65,0x43,0xec,0xac};
    memcpy(table,type,16);GUID(table+16);GUID(guid);
    OSWriteLittleInt64(table,32,first);OSWriteLittleInt64(table,40,last);
    const char *name="Mac OS X Install DVD";
    for(size_t i=0;name[i];i++)OSWriteLittleInt16(table,56+i*2,(uint16_t)name[i]);
    mbr[446+1]=0;mbr[446+2]=2;mbr[446+4]=0xee;
    memset(mbr+446+5,0xff,3);OSWriteLittleInt32(mbr,446+8,1);
    OSWriteLittleInt32(mbr,446+12,(uint32_t)(sectors-1));mbr[510]=0x55;mbr[511]=0xaa;
    output=open(destination,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(output<0)goto done;
    if(ftruncate(output,(off_t)(sectors*512))||Write(output,mbr,512,0))goto done;
    uint32_t tableCRC=CRC(table,sizeof(table));
    Header(header,1,sectors-1,2,sectors,guid,tableCRC);
    if(Write(output,header,512,512)||Write(output,table,sizeof(table),1024))goto done;
    Header(header,sectors-1,1,sectors-33,sectors,guid,tableCRC);
    if(Write(output,table,sizeof(table),(off_t)((sectors-33)*512))||
       Write(output,header,512,(off_t)((sectors-1)*512)))goto done;
    buffer=malloc(1048576);if(!buffer){errno=ENOMEM;goto done;}
    uint64_t copied=0;
    while(copied<(uint64_t)st.st_size){
        if(progress&&!progress(copied,(uint64_t)st.st_size,context)){errno=ECANCELED;goto done;}
        size_t amount=(size_t)(((uint64_t)st.st_size-copied)<1048576?(uint64_t)st.st_size-copied:1048576);
        ssize_t n=pread(input,buffer,amount,(off_t)copied);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){if(!n)errno=EIO;goto done;}
        if(Write(output,buffer,(size_t)n,(off_t)(first*512+copied)))goto done;
        copied+=(uint64_t)n;
    }
    int flushed;
    do{flushed=fsync(output);}while(flushed<0&&errno==EINTR);
    if(flushed)goto done;
    if(progress&&!progress(copied,(uint64_t)st.st_size,context)){errno=ECANCELED;goto done;}
    result=0;
done:
    saved=errno;free(buffer);if(input>=0)close(input);
    if(output>=0){close(output);if(result)unlink(destination);}
    errno=saved;return result;
}
