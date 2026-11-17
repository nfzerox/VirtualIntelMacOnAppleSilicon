#import "VIMHFSCatalog.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <stdio.h>

static BOOL CatalogFailure(NSString *reason) {
    fprintf(stderr,"HFS+ metadata finalization: %s\n",reason.UTF8String);return NO;
}

typedef struct {
    __strong NSURL *bands;
    uint64_t bandSize, size, openBand;
    int fd;
} BandImage;

static NSURL *BandURL(BandImage *image,uint64_t band) {
    return [image->bands URLByAppendingPathComponent:[NSString stringWithFormat:@"%llx",(unsigned long long)band]];
}
static BandImage OpenBundle(NSURL *url) {
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfURL:[url URLByAppendingPathComponent:@"Info.plist"]];
    NSURL *bands=[url URLByAppendingPathComponent:@"bands" isDirectory:YES];
    uint64_t bandSize=[info[@"diskimage-bundle-type"] isEqual:@"com.apple.diskimage.sparsebundle"]?
        [info[@"band-size"] unsignedLongLongValue]:0;
    return (BandImage){.bands=bands,.bandSize=bandSize,.size=[info[@"size"] unsignedLongLongValue],.fd=-1};
}
static BOOL AccessBand(BandImage *image, uint64_t offset, void *data, size_t length, BOOL write) {
    if(offset>image->size||length>image->size-offset)return NO;
    uint8_t *bytes=data;
    while(length) {
        uint64_t band=offset/image->bandSize,start=offset%image->bandSize;
        size_t span=(size_t)MIN(length,image->bandSize-start);
        if(image->fd<0||image->openBand!=band) {
            if(image->fd>=0)close(image->fd);
            NSURL *file=BandURL(image,band);
            image->fd=open(file.fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC);
            if(image->fd<0)return NO;
            image->openBand=band;
        }
        ssize_t done=write?pwrite(image->fd,bytes,span,(off_t)start):pread(image->fd,bytes,span,(off_t)start);
        if(done<0&&errno==EINTR)continue;
        if(done<=0)return NO;
        bytes+=done;offset+=done;length-=done;
    }
    return YES;
}
static uint16_t Read16(const uint8_t *p) {return ((uint16_t)p[0]<<8)|p[1];}
static uint32_t Read32(const uint8_t *p) {return ((uint32_t)Read16(p)<<16)|Read16(p+2);}
static void Write16(uint8_t *p,uint16_t v) {p[0]=v>>8;p[1]=v;}
static void Write32(uint8_t *p,uint32_t v) {Write16(p,v>>16);Write16(p+2,v);}

typedef struct {
    uint64_t offset,length;
    uint32_t blockSize,blocks,start[8],count[8];
} CatalogFork;

static BOOL CatalogIO(BandImage *image,const CatalogFork *fork,uint64_t offset,
                      void *bytes,size_t length,BOOL write) {
    if(offset>fork->length||length>fork->length-offset)return NO;
    for(unsigned i=0;i<8&&length;i++) {
        uint64_t span=(uint64_t)fork->count[i]*fork->blockSize;
        if(offset>=span){offset-=span;continue;}
        size_t n=(size_t)MIN(length,span-offset);
        uint64_t physical=fork->offset+(uint64_t)fork->start[i]*fork->blockSize+offset;
        if(!AccessBand(image,physical,bytes,n,write))return NO;
        length-=n;bytes=(uint8_t *)bytes+n;offset=0;
    }
    return length==0;
}

static BOOL Finalize(BandImage *image,uint64_t offset,uint64_t size,
                     NSDictionary<NSNumber *,NSArray<NSNumber *> *> *permissions,
                     uint32_t folder,uint32_t boot) {
    uint8_t header[512],backup[4],tree[64];
    if(size<4096||offset>image->size||size>image->size-offset||
       !AccessBand(image,offset+1024,header,sizeof(header),NO)||
       !AccessBand(image,offset+size-1024,backup,sizeof(backup),NO)||
       memcmp(header,"H+\0\4",4)||memcmp(header,backup,4)||!(Read32(header+4)&0x100))
        return CatalogFailure(@"The cleanly unmounted volume headers or geometry are unavailable.");
    CatalogFork fork={.offset=offset,.length=((uint64_t)Read32(header+272)<<32)|Read32(header+276),
                      .blockSize=Read32(header+40),.blocks=Read32(header+44)};
    if(!fork.blockSize||(uint64_t)fork.blockSize*fork.blocks!=size)return CatalogFailure(@"The HFS+ allocation geometry differs from the partition.");
    uint64_t blocks=0;
    for(unsigned i=0;i<8;i++) {
        fork.start[i]=Read32(header+288+8*i);fork.count[i]=Read32(header+292+8*i);
        if((uint64_t)fork.start[i]+fork.count[i]>fork.blocks)return CatalogFailure(@"A catalog extent is outside the volume.");
        blocks+=fork.count[i];
    }
    // Fresh disks have an inline catalog fork. Do not touch another format.
    if(blocks!=Read32(header+284))return CatalogFailure([NSString stringWithFormat:
        @"The catalog has overflow extents (%llu inline blocks, %u allocated).",(unsigned long long)blocks,Read32(header+284)]);
    if(!CatalogIO(image,&fork,0,tree,sizeof(tree),NO)||tree[8]!=1)return CatalogFailure(@"The catalog B-tree header could not be read.");
    uint16_t nodeSize=Read16(tree+32);
    if(nodeSize<4096||(nodeSize&(nodeSize-1)))return CatalogFailure(@"The catalog node size is unsupported.");
    NSMutableData *buffer=[NSMutableData dataWithLength:nodeSize];
    NSMutableSet *pending=[NSMutableSet setWithArray:permissions.allKeys];
    [pending addObject:@(folder)];[pending addObject:@(boot)];
    NSMutableDictionary<NSNumber *,NSData *> *changes=[NSMutableDictionary dictionary];
    NSMutableSet *visited=[NSMutableSet set];
    uint32_t node=Read32(tree+24);
    while(node&&pending.count) {
        @autoreleasepool {
            if([visited containsObject:@(node)])return CatalogFailure(@"The catalog leaf chain contains a cycle.");
            [visited addObject:@(node)];
            uint8_t *leaf=buffer.mutableBytes;
            if(!CatalogIO(image,&fork,(uint64_t)node*nodeSize,leaf,nodeSize,NO)||leaf[8]!=255)return CatalogFailure(@"A catalog leaf could not be read.");
            unsigned count=Read16(leaf+10);
            if(2*(count+1)>nodeSize-14)return CatalogFailure(@"A catalog leaf has an invalid record count.");
            unsigned limit=nodeSize-2*(count+1);
            BOOL dirty=NO;
            for(unsigned i=0;i<count;i++) {
                unsigned start=Read16(leaf+nodeSize-2*(i+1)),end=Read16(leaf+nodeSize-2*(i+2));
                if(start<14||start+2>end||end>limit)return CatalogFailure(@"A catalog record range is invalid.");
                unsigned data=start+2+Read16(leaf+start);
                if(data+2>end)return CatalogFailure(@"A catalog record key is truncated.");
                uint8_t *record=leaf+data;
                unsigned kind=Read16(record);
                if(kind!=1&&kind!=2)continue;
                if(data+48>end)return CatalogFailure(@"A catalog file record is truncated.");
                uint32_t cnid=Read32(record+8);
                if(![pending containsObject:@(cnid)])continue;
                if((cnid==folder&&kind!=1)||(cnid==boot&&kind!=2))return CatalogFailure(@"A blessed catalog record has an unexpected type.");
                [pending removeObject:@(cnid)];
                NSArray<NSNumber *> *entry=permissions[@(cnid)];
                if(!entry)continue;
                uint32_t uid=entry[0].unsignedIntValue,gid=entry[1].unsignedIntValue;
                uint16_t mode=entry[2].unsignedShortValue,existing=Read16(record+42);
                if((mode&S_IFMT)!=(existing&S_IFMT))return CatalogFailure([NSString stringWithFormat:
                    @"Catalog ID %u has type %o instead of %o.",cnid,existing&S_IFMT,mode&S_IFMT]);
                if(uid!=Read32(record+32)||gid!=Read32(record+36)||mode!=existing) {
                    // Preserve Finder data, owner flags, and the link count.
                    Write32(record+32,uid);Write32(record+36,gid);Write16(record+42,mode);dirty=YES;
                }
                // HFSPlusBSDInfo.adminFlags is the superuser flag byte at
                // BSDInfo+8. Add only explicitly requested bits; all existing
                // admin/owner flags, Finder metadata and hard links survive.
                uint8_t admin=entry.count>3?entry[3].unsignedCharValue:0;
                if((record[40]|admin)!=record[40]){record[40]|=admin;dirty=YES;}
            }
            if(dirty)changes[@(node)]=buffer.copy;
            node=Read32(leaf);
        }
    }
    if(pending.count)return CatalogFailure([NSString stringWithFormat:@"%lu catalog IDs were not found: %@.",
        (unsigned long)pending.count,[[pending.allObjects sortedArrayUsingSelector:@selector(compare:)] subarrayWithRange:NSMakeRange(0,MIN(pending.count,8))]]);
    for(NSNumber *key in changes) {
        NSData *leaf=changes[key];
        if(!CatalogIO(image,&fork,key.unsignedLongLongValue*nodeSize,(void *)leaf.bytes,nodeSize,YES))return CatalogFailure(@"A catalog leaf could not be written.");
    }
    uint8_t blessing[8];Write32(blessing,folder);Write32(blessing+4,boot);
    uint64_t primary=offset+1024,alternate=offset+size-1024;
    return AccessBand(image,primary+80,blessing,8,YES)&&AccessBand(image,alternate+80,blessing,8,YES)&&
           AccessBand(image,primary+100,blessing,4,YES)&&AccessBand(image,alternate+100,blessing,4,YES);
}

BOOL VIMFinalizeHFSCatalog(NSURL *url,uint64_t offset,uint64_t size,
                           NSDictionary<NSNumber *,NSArray<NSNumber *> *> *permissions,
                           uint32_t systemFolder,uint32_t bootFile,NSError **error) {
    BandImage image=OpenBundle(url);
    BOOL ok=image.bandSize&&Finalize(&image,offset,size,permissions,systemFolder,bootFile);
    if(image.fd>=0)close(image.fd);
    if(!ok&&error)*error=[NSError errorWithDomain:@"VirtualIntelMac.HostInstall" code:1 userInfo:@{
        NSLocalizedDescriptionKey:@"Could not restore ownership and boot metadata on the new HFS+ disk."}];
    return ok;
}

enum { Sector = 512, TableSize = 128*128 };
typedef struct {
    uint8_t header[Sector], entries[TableSize];
    uint64_t lba, table, first, last, peer;
    uint32_t headerLength;
} PartitionTable;

static uint32_t LE32(const uint8_t *bytes) {
    uint32_t value;memcpy(&value,bytes,sizeof(value));return CFSwapInt32LittleToHost(value);
}
static uint64_t LE64(const uint8_t *bytes) {
    uint64_t value;memcpy(&value,bytes,sizeof(value));return CFSwapInt64LittleToHost(value);
}
static void PutLE32(uint8_t *bytes,uint32_t value) {
    value=CFSwapInt32HostToLittle(value);memcpy(bytes,&value,sizeof(value));
}
static void PutLE64(uint8_t *bytes,uint64_t value) {
    value=CFSwapInt64HostToLittle(value);memcpy(bytes,&value,sizeof(value));
}
static uint32_t CRC(const uint8_t *bytes,uint32_t length) {
    return (uint32_t)crc32(0,bytes,length);
}
static BOOL Zeroes(const uint8_t *bytes,size_t length) {
    while(length--)if(*bytes++)return NO;
    return YES;
}
static BOOL ReadGPT(BandImage *image,PartitionTable *gpt,uint64_t lba) {
    uint64_t sectors=image->size/Sector;
    uint8_t *h=gpt->header;
    if(sectors<68||lba>=sectors||!AccessBand(image,lba*Sector,h,Sector,NO)||
       memcmp(h,"EFI PART",8)||LE32(h+8)!=0x10000||LE64(h+24)!=lba||
       LE32(h+80)!=128||LE32(h+84)!=128)return NO;
    gpt->lba=lba;gpt->headerLength=LE32(h+12);gpt->peer=LE64(h+32);
    gpt->first=LE64(h+40);gpt->last=LE64(h+48);gpt->table=LE64(h+72);
    if(gpt->headerLength<92||gpt->headerLength>Sector||gpt->first<34||
       gpt->last>=sectors-33||gpt->first>gpt->last||
       gpt->table!=(lba==1?2:sectors-33))return NO;
    uint32_t expected=LE32(h+16);PutLE32(h+16,0);
    return CRC(h,gpt->headerLength)==expected&&
        AccessBand(image,gpt->table*Sector,gpt->entries,TableSize,NO)&&
        CRC(gpt->entries,TableSize)==LE32(h+88);
}
static BOOL WriteGPT(BandImage *image,PartitionTable *gpt,uint8_t *entries) {
    PutLE32(gpt->header+88,CRC(entries,TableSize));
    PutLE32(gpt->header+16,CRC(gpt->header,gpt->headerLength));
    return AccessBand(image,gpt->table*Sector,entries,TableSize,YES)&&
        AccessBand(image,gpt->lba*Sector,gpt->header,Sector,YES);
}

static const uint8_t GPTTypeEFI[]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b};
static const uint8_t GPTTypeHFS[]={0,0x53,0x46,0x48,0,0,0xaa,0x11,0xaa,0x11,0,0x30,0x65,0x43,0xec,0xac};
static const uint8_t GPTTypeAPFS[]={0xef,0x57,0x34,0x7c,0,0,0xaa,0x11,0xaa,0x11,0,0x30,0x65,0x43,0xec,0xac};

static BOOL ReadTwoPartitionGPT(BandImage *image,PartitionTable *main,PartitionTable *backup,const uint8_t *type) {
    return image->bandSize&&!(image->size%Sector)&&ReadGPT(image,main,1)&&
        main->peer==image->size/Sector-1&&ReadGPT(image,backup,main->peer)&&backup->peer==1&&
        main->first==backup->first&&main->last==backup->last&&!memcmp(main->header+56,backup->header+56,16)&&
        !memcmp(main->entries,backup->entries,TableSize)&&!memcmp(main->entries,GPTTypeEFI,16)&&
        !memcmp(main->entries+128,type,16)&&Zeroes(main->entries+256,TableSize-256);
}

static BOOL ReadStagingGPT(BandImage *image,PartitionTable *main,PartitionTable *backup) {
    return ReadTwoPartitionGPT(image,main,backup,GPTTypeHFS);
}

static BOOL APFSPartitionSectors(const PartitionTable *table,uint64_t *start,uint64_t *end) {
    *start=LE64(table->entries+128+32);*end=LE64(table->entries+128+40);
    return *start>=table->first&&*start>LE64(table->entries+40)&&*end>=*start&&*end<=table->last&&
        !(*start%8)&&!((*end+1)%8);
}

static BandImage OpenDetachedImage(NSURL *url,BOOL allowBundle) {
    // A detached flat image is read as one band; the same GPT checks apply
    // to it and to Catalina's banded staging image.
    BandImage image={.fd=-1};struct stat path,opened;
    if(!lstat(url.fileSystemRepresentation,&path)) {
        if(allowBundle&&S_ISDIR(path.st_mode))image=OpenBundle(url);
        else if(S_ISREG(path.st_mode)) {
            image.fd=open(url.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
            if(image.fd>=0&&!fstat(image.fd,&opened)&&S_ISREG(opened.st_mode)&&opened.st_size>0&&
                path.st_dev==opened.st_dev&&path.st_ino==opened.st_ino)image.bandSize=image.size=(uint64_t)opened.st_size;
        }
    }
    return image;
}

BOOL VIMExtendSparseInstallSeed(NSURL *raw,uint64_t bytes,NSError **error) {
    int fd=open(raw.fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat st;struct statfs fs;int failure=EINVAL;
    BandImage image={.fd=-1};
    BOOL ok=NO;
    if(fd<0){failure=errno;goto out;}
    if(fstat(fd,&st)||fstatfs(fd,&fs)){failure=errno;goto out;}
    // ftruncate on exFAT would materialize the advertised capacity. The worker
    // supplies an APFS scratch volume when its library cannot preserve holes.
    if(!S_ISREG(st.st_mode)||strcmp(fs.f_fstypename,"apfs")||st.st_size<0||
        bytes<=(uint64_t)st.st_size||bytes>INT64_MAX||bytes%4096)goto out;
    image.fd=fd;image.size=image.bandSize=(uint64_t)st.st_size;
    PartitionTable main,backup;uint8_t mbr[Sector];
    if(!ReadStagingGPT(&image,&main,&backup)||
       !AccessBand(&image,0,mbr,sizeof(mbr),NO)||mbr[510]!=0x55||mbr[511]!=0xaa||
       mbr[450]!=0xee||LE32(mbr+454)!=1)goto out;
    uint64_t sectors=bytes/Sector,end=((sectors-33)&~7ULL)-1;
    if(end<=LE64(main.entries+128+32))goto out;
    if(ftruncate(fd,(off_t)bytes)){failure=errno;goto out;}
    image.size=image.bandSize=bytes;
    PutLE32(mbr+458,(uint32_t)MIN(sectors-1,UINT32_MAX));
    PutLE64(main.entries+128+40,end);
    PutLE64(main.header+32,sectors-1);PutLE64(main.header+48,sectors-34);
    backup.lba=sectors-1;backup.table=sectors-33;
    PutLE64(backup.header+24,backup.lba);PutLE64(backup.header+48,sectors-34);
    PutLE64(backup.header+72,backup.table);
    if(!WriteGPT(&image,&backup,main.entries)||!WriteGPT(&image,&main,main.entries)||
       !AccessBand(&image,0,mbr,sizeof(mbr),YES)||fsync(fd)){failure=errno?:EIO;goto out;}
    ok=YES;
out:
    if(fd>=0)close(fd);
    if(!ok&&error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:failure userInfo:@{
        NSLocalizedDescriptionKey:@"Could not prepare the sparse temporary disk."}];
    return ok;
}

BOOL VIMGetAPFSPartition(NSURL *url,uint64_t *offset,uint64_t *length,NSError **error) {
    BandImage image=OpenDetachedImage(url,YES);
    PartitionTable main,backup;
    uint64_t start,end;uint8_t magic[8];
    BOOL ok=ReadTwoPartitionGPT(&image,&main,&backup,GPTTypeAPFS)&&APFSPartitionSectors(&main,&start,&end)&&
        AccessBand(&image,start*Sector+32,magic,sizeof(magic),NO)&&!memcmp(magic,"NXSB\0\x10\0\0",sizeof(magic));
    if(image.fd>=0)close(image.fd);
    if(ok){*offset=start*Sector;*length=(end+1-start)*Sector;}
    else if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSGPT" code:1 userInfo:@{
        NSLocalizedDescriptionKey:@"Could not locate the new detached APFS partition."}];
    return ok;
}

static NSData *ReadAPFSDriver(BandImage *image,uint64_t start,uint64_t end) {
    // Apple File System Reference: nx_superblock_t, apfs_efi_jumpstart_t.
    enum { Block=4096, ExtentOffset=176, ExtentSize=16 };
    uint64_t offset=start*Sector,capacity=(end+1-start)/(Block/Sector);
    uint8_t block[Block],record[Block];
    if(!AccessBand(image,offset,block,sizeof(block),NO)||memcmp(block+32,"NXSB",4)||LE32(block+36)!=Block)return nil;
    uint64_t count=LE64(block+40),jump=LE64(block+0x4f8);
    if(!jump||jump>=count||count>capacity||
       !AccessBand(image,offset+jump*Block,record,sizeof(record),NO)||memcmp(record+32,"JSDR",4)||
       LE32(record+36)!=1)return nil;
    uint32_t size=LE32(record+40),extents=LE32(record+44);
    if(size<2||size>(16<<20)||!extents||extents>(Block-ExtentOffset)/ExtentSize)return nil;
    NSMutableData *driver=[NSMutableData dataWithLength:size];
    NSUInteger copied=0;
    for(uint32_t i=0;i<extents;i++) {
        const uint8_t *extent=record+ExtentOffset+ExtentSize*i;
        uint64_t first=LE64(extent),blocks=LE64(extent+8);
        if(!first||first>=count||!blocks||blocks>count-first||
           blocks>(size-copied+Block-1)/Block)return nil;
        for(uint64_t j=0;j<blocks;j++) {
            size_t n=MIN(Block,size-copied);
            if(!AccessBand(image,offset+(first+j)*Block,(uint8_t *)driver.mutableBytes+copied,n,NO))return nil;
            copied+=n;
        }
    }
    return copied==size&&!memcmp(driver.bytes,"MZ",2)?driver:nil;
}

NSData *VIMReadIntelAPFSJumpstart(NSURL *rawRecovery,NSError **error) {
    BandImage image=OpenDetachedImage(rawRecovery,YES);PartitionTable main,backup;
    NSData *driver=nil;
    BOOL valid=image.bandSize&&!(image.size%Sector)&&ReadGPT(&image,&main,1)&&
        main.peer==image.size/Sector-1&&ReadGPT(&image,&backup,main.peer)&&backup.peer==1&&
        main.first==backup.first&&main.last==backup.last&&!memcmp(main.header+56,backup.header+56,16)&&
        !memcmp(main.entries,backup.entries,TableSize);
    if(valid) {
        uint64_t start=0,end=0;
        for(size_t index=0;index<TableSize;index+=128)if(!memcmp(main.entries+index,GPTTypeAPFS,16)) {
            if(start){valid=NO;break;}
            start=LE64(main.entries+index+32);end=LE64(main.entries+index+40);
            if(start<main.first||end<start||end>main.last){valid=NO;break;}
        }
        if(valid&&start)driver=ReadAPFSDriver(&image,start,end);
    }
    if(image.fd>=0)close(image.fd);
    const uint8_t *bytes=driver.bytes;
    if(driver.length>=64) {
        uint32_t pe=LE32(bytes+0x3c);
        if(pe>=64&&pe<=driver.length&&driver.length-pe>=94&&
            !memcmp(bytes+pe,"PE\0\0\x64\x86",6)&&!memcmp(bytes+pe+24,"\x0b\x02",2)&&
            !memcmp(bytes+pe+92,"\x0b\0",2))return driver;
    }
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSGPT" code:1 userInfo:@{
        NSLocalizedDescriptionKey:@"The original Recovery disk does not contain a usable Intel APFS EFI driver."}];
    return nil;
}

BOOL VIMPublishConvertedAPFSPartition(NSURL *url,NSData *driver,NSError **error) {
    BandImage image=OpenBundle(url);PartitionTable main,backup;
    BOOL ok=ReadStagingGPT(&image,&main,&backup);
    if(ok) {
        uint8_t *entry=main.entries+128;
        uint64_t start,end;
        ok=APFSPartitionSectors(&main,&start,&end)&&[ReadAPFSDriver(&image,start,end) isEqualToData:driver];
        if(ok) {
            memcpy(entry,GPTTypeAPFS,sizeof(GPTTypeAPFS));
            ok=WriteGPT(&image,&backup,main.entries)&&WriteGPT(&image,&main,main.entries);
        }
    }
    if(image.fd>=0)close(image.fd);
    if(!ok&&error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSGPT" code:1 userInfo:@{
        NSLocalizedDescriptionKey:@"The converted disk does not have the expected APFS partition and installer EFI driver."}];
    return ok;
}

static BOOL CopyBandToRaw(int band,int raw,uint64_t start,uint64_t end,uint64_t bandStart,uint64_t origin,
                          uint8_t *buffer,size_t capacity) {
    while(start<end) {
        size_t size=(size_t)MIN(capacity,end-start);
        size_t done=0;
        while(done<size) {
            ssize_t n=pread(band,buffer+done,size-done,(off_t)(start-bandStart+done));
            if(n<0&&errno==EINTR)continue;
            if(n<=0)return NO;
            done+=(size_t)n;
        }
        if(!Zeroes(buffer,size)) {
            done=0;
            while(done<size) {
                ssize_t n=pwrite(raw,buffer+done,size-done,(off_t)(start-origin+done));
                if(n<0&&errno==EINTR)continue;
                if(n<=0)return NO;
                done+=(size_t)n;
            }
        }
        start+=size;
    }
    return YES;
}

BOOL VIMExportSparseBundleRange(NSURL *url,uint64_t offset,uint64_t length,NSURL *raw,NSError **error) {
    BandImage image=OpenBundle(url);
    enum { Chunk=1024*1024 };
    struct statfs fs;
    BOOL ok=image.bandSize&&length&&length<=INT64_MAX&&offset<=image.size&&length<=image.size-offset&&
        !statfs(raw.URLByDeletingLastPathComponent.fileSystemRepresentation,&fs)&&!strcmp(fs.f_fstypename,"apfs");
    int output=ok?open(raw.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600):-1;
    int bands=output>=0?open(image.bands.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC):-1;
    DIR *entries=bands>=0?fdopendir(bands):NULL;
    uint8_t *buffer=entries?malloc(Chunk):NULL;
    ok=buffer&&!ftruncate(output,(off_t)length);
    while(ok) {
        errno=0;struct dirent *entry=readdir(entries);
        if(!entry){ok=errno==0;break;}
        const char *name=entry->d_name;
        if(!*name||strspn(name,"0123456789abcdef")!=strlen(name))continue;
        errno=0;uint64_t bandNumber=strtoull(name,NULL,16);
        if(errno||bandNumber>(image.size-1)/image.bandSize){ok=NO;break;}
        uint64_t base=bandNumber*image.bandSize;
        if(base>=offset+length||base+MIN(image.bandSize,image.size-base)<=offset)continue;
        int band=openat(bands,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        struct stat info;
        if(band<0){ok=NO;break;}
        ok=!fstat(band,&info)&&S_ISREG(info.st_mode)&&info.st_size>=0&&(uint64_t)info.st_size<=image.bandSize;
        if(ok)ok=CopyBandToRaw(band,output,MAX(base,offset),
            base+MIN((uint64_t)info.st_size,offset+length-base),base,offset,buffer,Chunk);
        close(band);
    }
    free(buffer);
    if(entries)closedir(entries);else if(bands>=0)close(bands);
    if(output>=0) {
        if(close(output))ok=NO;
        if(!ok)unlink(raw.fileSystemRepresentation);
    }
    if(!ok&&error)*error=[NSError errorWithDomain:@"VirtualIntelMac.RawExport" code:1 userInfo:@{
        NSLocalizedDescriptionKey:@"Could not export the detached disk to a sparse scratch file."}];
    return ok;
}

// DiskImages represents missing bands and the unwritten tail of a band
// as zeroes. Keep new zero bands sparse, but clear stale bytes where the
// original HFS+ shrink left allocated bands in the reserved area.
static BOOL CopyRecovery(BandImage *dest,BandImage *source,uint64_t start) {
    uint8_t buffer[256*1024];
    for(uint64_t offset=0;offset<source->size;) @autoreleasepool {
        uint64_t position=start+offset,srcOffset=offset%source->bandSize,dstOffset=position%dest->bandSize;
        size_t count=(size_t)MIN(sizeof(buffer),MIN(source->size-offset,
            MIN(source->bandSize-srcOffset,dest->bandSize-dstOffset)));
        int fd=open(BandURL(source,offset/source->bandSize).fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0&&errno!=ENOENT)return NO;
        size_t readCount=0;
        if(fd>=0) {
            while(readCount<count) {
                ssize_t n=pread(fd,buffer+readCount,count-readCount,(off_t)(srcOffset+readCount));
                if(n<0&&errno==EINTR)continue;
                if(n<0){close(fd);return NO;}
                if(!n)break;
                readCount+=(size_t)n;
            }
            close(fd);
        }
        memset(buffer+readCount,0,count-readCount);
        NSURL *path=BandURL(dest,position/dest->bandSize);
        fd=open(path.fileSystemRepresentation,O_WRONLY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0) {
            if(errno!=ENOENT)return NO;
            if(!Zeroes(buffer,count)) {
                fd=open(path.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
                if(fd<0)return NO;
            }
        }
        if(fd>=0) {
            size_t copied=0;
            while(copied<count) {
                ssize_t n=pwrite(fd,buffer+copied,count-copied,(off_t)(dstOffset+copied));
                if(n<0&&errno==EINTR)continue;
                if(n<=0){close(fd);return NO;}
                copied+=(size_t)n;
            }
            close(fd);
        }
        offset+=count;
    }
    return YES;
}

static BOOL AddRecovery(BandImage *image,BandImage *recovery) {
    PartitionTable main,backup;
    const uint8_t appleBoot[]={0x74,0x6f,0x6f,0x42,0,0,0xaa,0x11,0xaa,0x11,0,0x30,0x65,0x43,0xec,0xac};
    uint64_t size=(uint64_t)VIMHFSRecoverySectors*Sector;
    uint8_t header[Sector],alternate[Sector];
    if(!recovery->bandSize||recovery->size!=size||!ReadStagingGPT(image,&main,&backup)||
       !AccessBand(recovery,1024,header,Sector,NO)||!AccessBand(recovery,size-1024,alternate,Sector,NO)||
       memcmp(header,"H+\0\4",4)||memcmp(header,alternate,4)||
       !(Read32(header+4)&0x100)||!(Read32(alternate+4)&0x100)||
       (uint64_t)Read32(header+40)*Read32(header+44)!=size||!Read32(header+80)||!Read32(header+84))return NO;
    uint64_t hfsEnd=LE64(main.entries+128+40);
    if(hfsEnd>=main.last||main.last-hfsEnd!=VIMHFSRecoverySectors+7)return NO;
    uint64_t start=hfsEnd+1,end=start+VIMHFSRecoverySectors-1;
    if(start%8||!CopyRecovery(image,recovery,start*Sector))return NO;
    uint8_t *entry=main.entries+256;
    memcpy(entry,appleBoot,16);
    uuid_t unique;[NSUUID.UUID getUUIDBytes:unique];memcpy(entry+16,unique,16);
    // GPT stores the first three UUID fields little endian.
    entry[16]=unique[3];entry[17]=unique[2];entry[18]=unique[1];entry[19]=unique[0];
    entry[20]=unique[5];entry[21]=unique[4];entry[22]=unique[7];entry[23]=unique[6];
    PutLE64(entry+32,start);PutLE64(entry+40,end);
    const char *label="Recovery HD";
    for(unsigned i=0;label[i];i++)entry[56+2*i]=(uint8_t)label[i];
    return WriteGPT(image,&backup,main.entries)&&WriteGPT(image,&main,main.entries);
}

BOOL VIMAddHFSRecovery(NSURL *url,NSURL *recoveryURL,NSError **error) {
    BandImage image=OpenBundle(url),recovery=OpenBundle(recoveryURL);
    BOOL ok=AddRecovery(&image,&recovery);
    if(image.fd>=0)close(image.fd);
    if(recovery.fd>=0)close(recovery.fd);
    if(!ok&&error)*error=[NSError errorWithDomain:@"VirtualIntelMac.HostInstall" code:2 userInfo:@{
        NSLocalizedDescriptionKey:@"Could not add Recovery HD to the new HFS+ disk."}];
    return ok;
}
