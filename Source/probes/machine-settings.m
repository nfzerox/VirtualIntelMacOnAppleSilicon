// Development command line adapter for the production staged-settings model.
#import <Foundation/Foundation.h>
#import "VIMMachine.h"
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=3)return 64;NSError *error=nil;
    VIMMachine *machine=[VIMMachine loadURL:[NSURL fileURLWithPath:@(argv[1])] error:&error];
    NSData *data=[@(argv[2]) dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *settings=[NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if(!machine||![settings isKindOfClass:NSDictionary.class]||![machine saveSettings:settings error:&error]){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    puts("Settings saved through production configuration model");return 0;
}}
