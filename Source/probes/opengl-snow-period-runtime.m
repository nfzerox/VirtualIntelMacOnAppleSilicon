// Finite, non-mutating period-control and pthread TLS qualification. Run the
// same compiled x64 binary on a real 10.6 VM/reference; not a GPU benchmark.
#import "../tools/opengl-period-foundation.h"
#include "../PassthroughOpenGL/period-thread-storage.h"
#include <stdio.h>
struct ProbeThread {unsigned value;};
VIM_DEFINE_PERIOD_STORAGE(struct ProbeThread,vim_probe)
static void *Thread(void *opaque) {
    unsigned value=(unsigned)(uintptr_t)opaque;
    struct ProbeThread *state=vim_probe_storage();
    if(state->value)return (void *)1;
    state->value=value;
    for(unsigned i=0;i<100000;++i)if(vim_probe_storage()->value!=value)return (void *)1;
    return NULL;
}
int main(void){@autoreleasepool {
    NSString *input=@"{\"nonce\":9223372036854775807,\"text\":\"snow \\u2603\\n\\\"\",\"bool\":true,\"a\":[null]}";
    NSDictionary *value=[VIMPeriodJSON JSONObjectWithData:[input dataUsingEncoding:NSUTF8StringEncoding] options:0 error:NULL];
    NSData *encoded=[VIMPeriodJSON dataWithJSONObject:value options:0 error:NULL];
    if(!value||!encoded||![[value objectForKey:@"nonce"] isEqual:[NSNumber numberWithLongLong:INT64_MAX]]||
       ![[VIMPeriodJSON JSONObjectWithData:encoded options:0 error:NULL] isEqual:value])return 1;
    if([VIMPeriodJSON JSONObjectWithData:[@"{\"x\":1} garbage" dataUsingEncoding:NSUTF8StringEncoding] options:0 error:NULL])return 2;
    id pattern=[VIMPeriodRegularExpression regularExpressionWithPattern:@"^[a-z][a-z0-9_-]{0,30}$" options:0 error:NULL];
    if([pattern numberOfMatchesInString:@"valid_user" options:0 range:NSMakeRange(0,10)]!=1||
       [pattern numberOfMatchesInString:@"bad;user" options:0 range:NSMakeRange(0,8)]!=0)return 3;
    int fd=open("/dev/null",O_RDONLY|O_CLOEXEC);
    if(fd<0||!(fcntl(fd,F_GETFD)&FD_CLOEXEC))return 4;close(fd);
    fd=open("/dev/null",O_RDONLY|O_DIRECTORY);if(fd>=0){close(fd);return 5;}
    if(errno!=ENOTDIR)return 6;
    pthread_t threads[8];
    for(unsigned i=0;i<8;++i)if(pthread_create(&threads[i],NULL,Thread,(void *)(uintptr_t)(i+1)))return 7;
    for(unsigned i=0;i<8;++i){void *result=NULL;if(pthread_join(threads[i],&result)||result)return 8;}
    puts("PASS: strict JSON/Unicode/int64, account identity regex, descriptor guards, eight independent pthread slots");
    return 0;
}}
