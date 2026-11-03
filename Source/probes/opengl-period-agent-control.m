// Finite unprivileged fixture for the actual period agent's child workers.
// Never opens a UART, creates an account, changes a service or calls its main.
// Child processes are THIS binary and operate only on their own stdio pipes.
#define main VIMAgentUnusedMain
#include "../tools/opengl-guest-agent.m"
#undef main
#include <sys/time.h>

static int Child(const char *mode) {
    if(!strcmp(mode,"echo")){
        unsigned char buffer[4096];ssize_t count;
        while((count=read(0,buffer,sizeof(buffer)))){
            if(count<0){if(errno==EINTR)continue;return 1;}
            if(!WriteAll(1,buffer,(size_t)count))return 1;
        }
        return WriteAll(2,"separate stderr",15)?0:1;
    }
    if(strcmp(mode,"large"))return 64;
    unsigned char buffer[4096];memset(buffer,'V',sizeof(buffer));
    for(int fd=1;fd<=2;fd++)for(unsigned i=0;i<512;i++)if(!WriteAll(fd,buffer,sizeof(buffer)))return 1;
    return 0;
}
static NSDictionary *RunFixture(NSString *path,NSString *mode,NSData *input){
    NSMutableDictionary *tasks=[NSMutableDictionary dictionary],*files=[NSMutableDictionary dictionary];
    NSMutableDictionary *args=[NSMutableDictionary dictionaryWithObjectsAndKeys:path,@"path",
        [NSArray arrayWithObjects:@"--child",mode,nil],@"arg",nil];
    if(input)[args setObject:[input base64Encoding] forKey:@"input-data"];
    NSDictionary *reply=Execute([NSDictionary dictionaryWithObjectsAndKeys:@"guest-exec",@"execute",args,@"arguments",nil],tasks,files);
    NSNumber *pid=[[reply objectForKey:@"return"] objectForKey:@"pid"];if(!pid)return nil;
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:30];
    while([deadline timeIntervalSinceNow]>0){
        reply=Execute([NSDictionary dictionaryWithObjectsAndKeys:@"guest-exec-status",@"execute",
            [NSDictionary dictionaryWithObject:pid forKey:@"pid"],@"arguments",nil],tasks,files);
        NSDictionary *result=[reply objectForKey:@"return"];
        if([[result objectForKey:@"exited"] boolValue])return result;
        usleep(10000); // Finite fixture observation, not the production reader.
    }
    return nil;
}
int main(int argc,char **argv){
    if(argc==3&&!strcmp(argv[1],"--child"))return Child(argv[2]);
    if(argc!=1||argv[0][0]!='/')return 64;
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    NSData *input=[@"Leopard control: stdin, stdout, stderr and native threads." dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *small=RunFixture([NSString stringWithUTF8String:argv[0]],@"echo",input);
    NSData *output=[[[NSData alloc] initWithBase64Encoding:[small objectForKey:@"out-data"]] autorelease];
    NSData *errors=[[[NSData alloc] initWithBase64Encoding:[small objectForKey:@"err-data"]] autorelease];
    BOOL exact=small&&[[small objectForKey:@"exitcode"] intValue]==0&&[input isEqual:output]&&
        [errors isEqual:[@"separate stderr" dataUsingEncoding:NSUTF8StringEncoding]]&& ![[small objectForKey:@"out-truncated"] boolValue];
    printf("exact-input-output=%d\n",exact);
    NSDictionary *large=RunFixture([NSString stringWithUTF8String:argv[0]],@"large",nil);
    output=[[[NSData alloc] initWithBase64Encoding:[large objectForKey:@"out-data"]] autorelease];
    errors=[[[NSData alloc] initWithBase64Encoding:[large objectForKey:@"err-data"]] autorelease];
    BOOL bounded=large&&[[large objectForKey:@"exitcode"] intValue]==0&&[output length]==1024*1024&&
        [errors length]==1024*1024&&[[large objectForKey:@"out-truncated"] boolValue]&&[[large objectForKey:@"err-truncated"] boolValue];
    printf("bounded-concurrent-pipes=%d\n",bounded);
    NSDictionary *value=[NSDictionary dictionaryWithObject:@"period-native" forKey:@"marker"];
    NSData *encoded=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    id decoded=encoded?[NSPropertyListSerialization propertyListWithData:encoded options:0 format:NULL error:NULL]:nil;
    BOOL propertyList=[value isEqual:decoded];printf("native-property-list=%d\n",propertyList);
    unsigned char bytes[256];for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)i;
    NSData *binary=[NSData dataWithBytes:bytes length:sizeof(bytes)];
    NSData *roundtrip=[[[NSData alloc] initWithBase64Encoding:[binary base64Encoding]] autorelease];
    BOOL base64=[binary isEqual:roundtrip];
    NSArray *invalidValues=[NSArray arrayWithObjects:@"AB==",@"AAB=",@"AA=A",@"====",@"A",@"AA==AAAA",nil];
    for(NSUInteger index=0;index<[invalidValues count];index++){
        NSString *invalid=[invalidValues objectAtIndex:index];
        NSData *rejected=[[[NSData alloc] initWithBase64Encoding:invalid] autorelease];if(rejected)base64=NO;
    }
    printf("strict-base64=%d\n",base64);
    VIMOpenGLProcessInfo process;memset(&process,0,sizeof(process));
    int returned=VIMOpenGLReadProcessBirth(getpid(),&process)?(int)sizeof(process):0;
    uint64_t seconds=VIMOpenGLAgentStartSeconds(&process),microseconds=VIMOpenGLAgentStartMicroseconds(&process);
    struct timeval now;gettimeofday(&now,NULL);
    BOOL birth=returned==(int)sizeof(process)&&process.pbi_pid==(unsigned)getpid()&&process.pbi_uid==getuid()&&
        seconds>0&&seconds<=(uint64_t)now.tv_sec&&(uint64_t)now.tv_sec-seconds<300&&microseconds<1000000;
    printf("native-process-birth=%d bytes=%d expected=%zu start-offset=%zu pid-match=%d uid-match=%d seconds=%llu microseconds=%llu\n",
        birth,returned,sizeof(process),offsetof(VIMOpenGLProcessInfo,pbi_start),process.pbi_pid==(unsigned)getpid(),process.pbi_uid==getuid(),
        (unsigned long long)seconds,(unsigned long long)microseconds);
    BOOL executable=[VIMOpenGLProcessExecutable(getpid()) isEqual:[NSString stringWithUTF8String:argv[0]]];
    printf("native-process-executable=%d\n",executable);
    BOOL identityNeeded=VIMOpenGLNeedsControlIdentity();printf("native-control-epoch-required=%d\n",identityNeeded);
    [pool drain];return exact&&bounded&&propertyList&&base64&&birth&&executable&&identityNeeded?0:1;
}
