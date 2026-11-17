#import "../tools/HostInstall/VIMBigSurFirmlinks.h"

int main(int argc,char **argv) { @autoreleasepool {
    BOOL reserve=argc==4&&!strcmp(argv[1],"--reserve");
    if(argc!=3&&!reserve)return 64;
    NSError *error=nil;
    NSURL *zip=[NSURL fileURLWithPath:@(argv[reserve?2:1])];
    NSDictionary *plan;
    if(reserve) {
        NSDictionary *reserved=VIMBigSurReserveFirmlinks(zip,[NSURL fileURLWithPath:@(argv[3])],&error);
        if(reserved)plan=@{@"FirmlinkCount":@([reserved[@"FirmlinkInodes"] count])};
    }
    else {
        NSString *text=[NSString stringWithContentsOfFile:@(argv[2]) encoding:NSUTF8StringEncoding error:&error];
        plan=text?VIMBigSurFirmlinkPlan(zip,text,&error):nil;
    }
    if(!plan){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String?:"Could not plan the original firmlinks.");return 1;}
    NSData *output=[NSPropertyListSerialization dataWithPropertyList:plan format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    if(!output){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    [NSFileHandle.fileHandleWithStandardOutput writeData:output];return 0;
}}
