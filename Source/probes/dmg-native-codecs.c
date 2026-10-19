#include <bzlib.h>
#include <compression.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const unsigned char source[]="Virtual Intel Mac direct DMG codec test. No converted disk is created.";
    unsigned char compressed[4096],decoded[sizeof(source)];
    const compression_algorithm algorithms[]={COMPRESSION_LZFSE,COMPRESSION_LZMA};
    if(!BZ2_bzlibVersion())return 1;
    for(unsigned i=0;i<2;i++) {
        size_t count=compression_encode_buffer(compressed,sizeof(compressed),source,sizeof(source),NULL,algorithms[i]);
        if(!count||compression_decode_buffer(decoded,sizeof(decoded),compressed,count,NULL,algorithms[i])!=sizeof(source)||memcmp(decoded,source,sizeof(source)))return 1;
    }
    puts("PASS: native LZFSE/LZMA and system bzip2 linkage");return 0;
}
