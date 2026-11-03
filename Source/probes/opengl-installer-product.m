// Recovery-only investigation of High Sierra's native unattended-product
// contract. Creates only the explicitly supplied new VIM probe directory;
// does not launch an install, alter NVRAM, or modify Apple's executables.
#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <unistd.h>
@interface NSObject (VIMInstallerProductProbe)
+ (id)defaultProductManager;
- (BOOL)setMutableProductPath:(NSString *)path error:(NSError **)error;
- (NSMutableDictionary *)attributesDictionary;
- (BOOL)flushAttributesDictionary;
- (BOOL)automationFileIsSafeWithError:(NSError **)error;
- (id)mutableProduct;
@end
int main(int argc,char **argv){@autoreleasepool{
    if(argc!=2||geteuid()!=0)return 77;
    NSString *path=[NSString stringWithUTF8String:argv[1]];
    if(![path.lastPathComponent hasPrefix:@"VIM-ProductProbe-"]||[NSFileManager.defaultManager fileExistsAtPath:path])return 77;
    NSError *error=nil;
    if(![[NSBundle bundleWithPath:@"/System/Library/PrivateFrameworks/OSInstaller.framework"] loadAndReturnError:&error]){
        fprintf(stderr,"framework: %s\n",error.description.UTF8String);return 1;
    }
    id product=[NSClassFromString(@"OSProductManager") defaultProductManager];
    if(![product setMutableProductPath:path error:&error]){
        fprintf(stderr,"product: %s\n",error.description.UTF8String);return 1;
    }
    [[product attributesDictionary] setObject:[NSDate date] forKey:@"IAEndDate"];
    BOOL saved=[product flushAttributesDictionary];
    BOOL safe=[product automationFileIsSafeWithError:&error];
    printf("native-product=%s attributes-saved=%d automation-safe=%d error=%s\n",
        [[product mutableProduct] description].UTF8String,saved,safe,error.description.UTF8String?:"none");
    return saved&&safe?0:1;
}}
