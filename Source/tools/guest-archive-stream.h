// Compression only: all tar and filesystem validation lives in guest-extract.h.
// Use the OS liblzma ABI, not an external xz command or a general tar extractor.
#include "../vendor/xz-api/lzma.h"
#include <zlib.h>
#include <stdio.h>

typedef struct {
    gzFile gzip;
    FILE *input;
    lzma_stream xz;
    uint8_t buffer[65536];
    BOOL inputEOF,finished;
} VIMArchiveStream;

static void ArchiveClose(VIMArchiveStream *file) {
    if(file->gzip){gzclose(file->gzip);file->gzip=NULL;}
    if(file->input){lzma_end(&file->xz);fclose(file->input);file->input=NULL;}
}
static void ArchiveOpen(VIMArchiveStream *file,NSString *path) {
    file->xz=(lzma_stream)LZMA_STREAM_INIT;
    FILE *input=fopen(path.fileSystemRepresentation,"rb");
    Check(input!=NULL,@"Cannot open payload archive");
    uint8_t magic[6];size_t size=fread(magic,1,sizeof(magic),input);
    if(size==6&&!memcmp(magic,"\xfd" "7zXZ\0",6)) {
        file->input=input;rewind(input);
        lzma_ret status=lzma_stream_decoder(&file->xz,64ULL*1024*1024,
                                          LZMA_TELL_NO_CHECK|LZMA_TELL_UNSUPPORTED_CHECK);
        Check(status==LZMA_OK,@"Cannot initialize the XZ payload decoder");
    } else {
        fclose(input);
        Check(size>=3&&magic[0]==0x1f&&magic[1]==0x8b&&magic[2]==8,
              @"Payload must be XZ or gzip compressed");
        file->gzip=gzopen(path.fileSystemRepresentation,"rb");
        Check(file->gzip!=NULL,@"Cannot open gzip payload archive");
    }
}
static unsigned ArchiveReadSome(VIMArchiveStream *file,void *buffer,unsigned count) {
    if(file->gzip) {
        int n=gzread(file->gzip,buffer,count);
        int status=Z_OK;gzerror(file->gzip,&status);
        Check(n>=0&&!gzdirect(file->gzip)&&(status==Z_OK||status==Z_STREAM_END),@"Corrupt or truncated gzip payload archive");
        Check(n||gzeof(file->gzip),@"Truncated gzip payload archive");return (unsigned)n;
    }
    if(file->finished)return 0;
    file->xz.next_out=buffer;file->xz.avail_out=count;
    while(file->xz.avail_out&&!file->finished) {
        if(!file->xz.avail_in&&!file->inputEOF) {
            size_t n=fread(file->buffer,1,sizeof(file->buffer),file->input);
            Check(!ferror(file->input),@"Cannot read XZ payload archive");
            file->inputEOF=feof(file->input);
            file->xz.next_in=file->buffer;file->xz.avail_in=n;
        }
        size_t beforeIn=file->xz.avail_in,beforeOut=file->xz.avail_out;
        lzma_ret status=lzma_code(&file->xz,file->inputEOF?LZMA_FINISH:LZMA_RUN);
        Check(status!=LZMA_MEMLIMIT_ERROR,@"XZ payload exceeds the decoder memory limit");
        Check(status==LZMA_OK||status==LZMA_STREAM_END,@"Truncated, corrupt or unsupported XZ payload archive");
        if(status==LZMA_STREAM_END) {
            // A packaged payload is exactly one checked stream, not an
            // arbitrary concatenation. Do not silently ignore appended data.
            Check(file->xz.avail_in==0&&fgetc(file->input)==EOF&&!ferror(file->input),
                  @"Unexpected trailing compressed payload data");
            file->finished=YES;
        } else Check(beforeIn!=file->xz.avail_in||beforeOut!=file->xz.avail_out||!file->inputEOF,
                     @"Truncated XZ payload archive");
    }
    return count-(unsigned)file->xz.avail_out;
}
static void ArchiveRead(VIMArchiveStream *file,void *buffer,unsigned count) {
    uint8_t *p=buffer;
    while(count){unsigned n=ArchiveReadSome(file,p,count);Check(n>0,@"Truncated or corrupt payload archive");p+=n;count-=n;}
}
