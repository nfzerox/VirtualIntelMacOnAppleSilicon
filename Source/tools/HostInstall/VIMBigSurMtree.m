#import "VIMBigSurMtree.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

enum { Type=1, Stream=2, Inode=4, Sibling=8, Time=16, Birth=32, Change=64, Parent=128, Access=256 };
typedef struct {
    unsigned fields;
    uint8_t type;
    uint64_t stream,inode,sibling,times[5];
} Fields;

static BOOL Decimal(const char *bytes,uint64_t *value) {
    if(*bytes<'0'||*bytes>'9')return NO;
    uint64_t v=0;
    for(;*bytes;bytes++) {
        unsigned d=(unsigned)(*bytes-'0');
        if(d>9||v>(UINT64_MAX-d)/10)return NO;
        v=v*10+d;
    }
    *value=v;return YES;
}

static BOOL ParseTime(char *field,uint64_t *value) {
    char *dot=strchr(field,'.');
    if(!dot)return NO;
    *dot++=0;size_t digits=strlen(dot);uint64_t seconds,part;
    if(!digits||digits>9||!Decimal(field,&seconds)||!Decimal(dot,&part))return NO;
    for(;digits<9;digits++)part*=10;
    if(seconds>(UINT64_MAX-part)/1000000000)return NO;
    *value=seconds*1000000000+part;return YES;
}

static unsigned Field(const char *name) {
    const char *names[]={"type","xattrsdigest","inode","siblingid","time","btime","ctime","ptime","atime"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(name,names[i]))return 1U<<i;
    return 0;
}

static BOOL Attribute(Fields *out,char *token) {
    char *value=strchr(token,'=');
    if(!value||value==token||!value[1])return NO;
    *value++=0;unsigned field=Field(token);
    if(!field)return YES;
    out->fields|=field;
    if(field==Type) {
        out->type=!strcmp(value,"dir")?4:!strcmp(value,"file")?8:!strcmp(value,"link")?10:0;
        return out->type!=0;
    }
    if(field==Inode)return Decimal(value,&out->inode);
    if(field==Sibling)return Decimal(value,&out->sibling);
    if(field==Stream) {
        const char *separator=strrchr(value,'.');
        return separator&&Decimal(separator+1,&out->stream);
    }
    return ParseTime(value,&out->times[__builtin_ctz(field)-4]);
}

static NSData *DecodeComponent(const char *name) {
    if(!*name||strchr(name,'/'))return nil;
    if(!strchr(name,'\\'))return [NSData dataWithBytes:name length:strlen(name)];
    NSMutableData *out=[NSMutableData dataWithLength:strlen(name)];
    uint8_t *bytes=out.mutableBytes;size_t used=0;
    for(const unsigned char *c=(const unsigned char *)name;*c;c++) {
        uint8_t ch=*c;
        if(ch=='\\') {
            unsigned a=c[1]-'0';
            if(a>3||c[2]<'0'||c[2]>'7'||c[3]<'0'||c[3]>'7')return nil;
            ch=(uint8_t)(a*64+(c[2]-'0')*8+c[3]-'0');c+=3;
        }
        if(!ch||ch=='/')return nil;
        bytes[used++]=ch;
    }
    out.length=used;return out;
}

BOOL VIMBigSurVisitCanonicalTree(NSURL *mtree,BOOL (^visit)(NSData *,VIMBigSurCanonicalEntry),
    uint64_t *timestamp,NSError **error) {
    int fd=open(mtree.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat info;
    FILE *input=fd>=0&&!fstat(fd,&info)&&S_ISREG(info.st_mode)?fdopen(fd,"r"):NULL;
    char *line=NULL;size_t capacity=0,number=0;
    NSMutableData *joined=[NSMutableData data],*path=[NSMutableData data];
    NSMutableArray<NSNumber *> *parents=[NSMutableArray array];
    Fields defaults={0};BOOL root=NO,closed=NO;
    uint64_t stamp=0;
    int failure=input&&visit&&timestamp?0:EINVAL;
    while(!failure) @autoreleasepool {
        ssize_t length=getline(&line,&capacity,input);
        if(length<0)break;
        number++;
        if(length>65536||joined.length+(size_t)length>65536||memchr(line,0,(size_t)length)){failure=EBADMSG;break;}
        const char *whitespace=" \t\r\n";
        char *start=line+strspn(line,whitespace),*end=line+length;
        while(end>start&&strchr(whitespace,end[-1]))end--;
        if(start==end||*start=='#')continue;
        if(closed){failure=EBADMSG;break;}
        BOOL continued=end[-1]=='\\';
        if(continued||joined.length) {
            [joined appendBytes:start length:(size_t)(end-start)-(continued?1:0)];
            [joined appendBytes:continued?" ":"" length:1];
            if(continued)continue;
            start=joined.mutableBytes;
        } else *end=0;
        char *save=NULL,*name=strtok_r(start,whitespace,&save),*token=NULL;
        if(!name){failure=EBADMSG;break;}
        if(!strcmp(name,"..")) {
            if(!root||strtok_r(NULL,whitespace,&save)){failure=EBADMSG;break;}
            if(parents.count){path.length=parents.lastObject.unsignedIntegerValue;[parents removeLastObject];}
            else closed=YES;
        } else if(!strcmp(name,"/unset")) {
            while((token=strtok_r(NULL,whitespace,&save))) {
                if(!strcmp(token,"all"))defaults=(Fields){0};
                else defaults.fields&=~Field(token);
            }
        } else {
            Fields fields=defaults;
            while((token=strtok_r(NULL,whitespace,&save)))if(!Attribute(&fields,token)){failure=EBADMSG;break;}
            if(failure)break;
            if(!strcmp(name,"/set"))defaults=fields;
            else {
                const unsigned required=Type|Stream|Inode|Sibling;
                const uint64_t limit=UINT64_C(1)<<60;
                if((fields.fields&required)!=required||!fields.inode||fields.inode>=limit||
                   fields.stream>=limit||fields.sibling>=limit||(fields.sibling&&fields.type==4)){
                    failure=EBADMSG;break;
                }
                NSUInteger prior=path.length;
                if(!strcmp(name,".")) {
                    const unsigned dates=Time|Birth|Change|Parent;
                    if(root||fields.type!=4||(fields.fields&dates)!=dates){failure=EBADMSG;break;}
                    root=YES;stamp=fields.times[0];
                } else {
                    NSData *child=DecodeComponent(name);
                    if(!root||!child||[child isEqual:[NSData dataWithBytes:"." length:1]]||
                        [child isEqual:[NSData dataWithBytes:".." length:2]]){failure=EBADMSG;break;}
                    if(prior)[path appendBytes:"/" length:1];
                    [path appendData:child];
                    if(fields.type==4)[parents addObject:@(prior)];
                }
                for(unsigned i=0;i<5;i++)if((fields.fields&(Time<<i))&&fields.times[i]!=stamp)failure=EBADMSG;
                if(failure)break;
                if(!visit([path copy],(VIMBigSurCanonicalEntry){fields.inode,fields.stream,fields.sibling,fields.type})){
                    failure=ECANCELED;break;
                }
                if(fields.type!=4)path.length=prior;
            }
        }
        joined.length=0;
    }
    if(!failure&&(ferror(input)||joined.length||parents.count||!root||!closed))failure=EBADMSG;
    free(line);
    if(input)fclose(input);else if(fd>=0)close(fd);
    if(!failure){*timestamp=stamp;return YES;}
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurMtree" code:failure userInfo:@{
        NSLocalizedDescriptionKey:[NSString stringWithFormat:@"Could not read the Big Sur Intel seal manifest at line %zu.",number]}];
    return NO;
}
