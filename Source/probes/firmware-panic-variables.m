// Offline, read-only panic-variable recovery, including deleted store entries.
// Reuses the shipping EDK II serialized-format helpers; never edits NVRAM.
// --unpack FILE... decodes explicitly selected chunks from one panic, without
// guessing generations from variable state/order or requiring Python.
#import "../VirtualIntelMac/VIMFirmwareBoot.m"

int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc>=3&&!strcmp(argv[1],"--unpack")) {
        NSMutableData *plain=[NSMutableData data];uint32_t bits=0;unsigned available=0;
        for(int i=2;i<argc;i++) {
            NSError *error=nil;NSData *chunk=[NSData dataWithContentsOfFile:@(argv[i]) options:0 error:&error];
            if(!chunk){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 66;}
            const uint8_t *bytes=chunk.bytes;
            for(NSUInteger j=0;j<chunk.length;j++) {
                bits|=(uint32_t)bytes[j]<<available;available+=8;
                while(available>=7) {
                    uint8_t value=bits&127;[plain appendBytes:&value length:1];bits>>=7;available-=7;
                }
            }
        }
        const uint8_t *bytes=plain.bytes;NSUInteger length=plain.length;
        while(length&&!bytes[length-1])length--;
        return fwrite(bytes,1,length,stdout)==length?0:74;
    }
    if(argc!=3)return 64;
    NSData *data=[NSData dataWithContentsOfFile:@(argv[1])];
    const uint8_t *b=data.bytes;NSUInteger n=data.length;
    if(n<100||memcmp(b+40,"_FVH",4))return 65;
    NSUInteger header=LE16(b+48);
    if(header>n-28)return 65;
    const uint8_t authenticated[]={0x78,0x2c,0xf3,0xaa,0x7b,0x94,0x9a,0x43,0xa1,0x80,0x2e,0x14,0x4e,0xc3,0x77,0x92};
    BOOL auth=!memcmp(b+header,authenticated,16);
    NSUInteger store=LE32(b+header+16),size=auth?60:32;
    if(store<28||store>n-header)return 65;
    NSUInteger end=header+store,offset=(header+31)&~(NSUInteger)3,count=0;
    while(offset<=end&&end-offset>=size) {
        const uint8_t *v=b+offset;if(LE16(v)!=0x55aa)break;
        NSUInteger names=LE32(v+(auth?36:8)),bytes=LE32(v+(auth?40:12));
        if(names<2||names%2||names>end-offset-size||bytes>end-offset-size-names)return 65;
        NSString *name=[[NSString alloc] initWithBytes:v+size length:names-2 encoding:NSUTF16LittleEndianStringEncoding];
        if([name.lowercaseString containsString:@"panic"]) {
            NSString *output=[@(argv[2]) stringByAppendingPathComponent:[NSString stringWithFormat:@"panic-%lu.bin",(unsigned long)count++]];
            NSData *value=[NSData dataWithBytes:v+size+names length:bytes];NSError *error=nil;
            if(![value writeToFile:output options:NSDataWritingWithoutOverwriting error:&error]) {
                fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 73;
            }
            printf("%s state=%02x bytes=%lu -> %s\n",name.UTF8String,v[2],(unsigned long)bytes,output.UTF8String);
        }
        offset=(offset+size+names+bytes+3)&~(NSUInteger)3;
    }
    printf("Recovered %lu panic variables (no NVRAM changes).\n",(unsigned long)count);return 0;
} }
