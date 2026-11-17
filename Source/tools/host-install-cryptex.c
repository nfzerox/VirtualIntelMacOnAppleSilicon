// Native RIDIFF10 decoder for Apple-signed InstallAssistant assets. The
// RawImagePatch ABI follows blacktop/ipsw's pkg/ota/ridiff/ridiff.go.
//
// Copyright (c) 2018-2026 blacktop
// SPDX-License-Identifier: MIT
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct image_request {
    int64_t reserved[2];
    const char *old_image, *output, *patch;
    uint32_t not_cryptex_cache, threads, verbose;
};

int main(int argc,char **argv) {
    const char *library_path="/usr/lib/libParallelCompression.dylib";
    if(argc>=3&&!strcmp(argv[1],"--library")) {
        library_path=argv[2];argc-=2;argv+=2;
    }
    int availability=argc==2&&!strcmp(argv[1],"--availability");
    if(!availability&&(argc!=3||!access(argv[2],F_OK))){
        fprintf(stderr,"usage: host-install-cryptex [--library DYLIB] --availability | /original-ridiff /new-output-image\n");
        return 64;
    }
    void *library=dlopen(library_path,RTLD_LOCAL|RTLD_NOW);
    if(!library){fprintf(stderr,"%s\n",dlerror());return 1;}
    int32_t (*apply)(struct image_request *)=dlsym(library,"RawImagePatch");
    if(!apply){fprintf(stderr,"%s\n",dlerror());dlclose(library);return 1;}
    if(availability){puts("Host-native RawImagePatch is available");dlclose(library);return 0;}
    struct image_request request={.old_image="",.output=argv[2],.patch=argv[1],.threads=2};
    int32_t error=apply(&request);
    if(error)fprintf(stderr,"RawImagePatch: %d\n",error);
    dlclose(library);
    if(!error) {
        char magic[4];int fd=open(argv[2],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        int valid=fd>=0&&pread(fd,magic,sizeof(magic),32)==(ssize_t)sizeof(magic)&&!memcmp(magic,"NXSB",4);
        if(fd>=0)close(fd);
        if(!valid){fputs("The decoded Cryptex is not an APFS image.\n",stderr);error=1;}
    }
    return error?1:0;
}
