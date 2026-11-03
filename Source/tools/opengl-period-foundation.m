#import "opengl-period-foundation.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
#import "../vendor/source/JSONKit/JSONKit.h"
#include <fcntl.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#undef NSPropertyListSerialization
@implementation VIMPeriodPropertyList
+ (NSData *)dataWithPropertyList:(id)value format:(NSPropertyListFormat)format options:(NSUInteger)options error:(NSError **)error {
    if(options)return nil;
    NSString *description=nil;
    NSData *data=[NSPropertyListSerialization dataFromPropertyList:value format:format errorDescription:&description];
    if(!data&&error)*error=[NSError errorWithDomain:@"VirtualIntelMac.PeriodPropertyList" code:1
        userInfo:description?[NSDictionary dictionaryWithObject:description forKey:NSLocalizedDescriptionKey]:nil];
    [description release];return data;
}
+ (id)propertyListWithData:(NSData *)data options:(NSUInteger)options format:(NSPropertyListFormat *)format error:(NSError **)error {
    if(options)return nil;
    NSString *description=nil;
    id value=[NSPropertyListSerialization propertyListFromData:data mutabilityOption:NSPropertyListImmutable format:format errorDescription:&description];
    if(!value&&error)*error=[NSError errorWithDomain:@"VirtualIntelMac.PeriodPropertyList" code:1
        userInfo:description?[NSDictionary dictionaryWithObject:description forKey:NSLocalizedDescriptionKey]:nil];
    [description release];return value;
}
@end
#endif
@implementation VIMPeriodJSON
+ (id)JSONObjectWithData:(NSData *)data options:(NSUInteger)options error:(NSError **)error {
    // These cold control messages use Foundation's strict immutable default,
    // not fragments, mutable containers or permissive comments/NaN parsing.
    if(options||![data isKindOfClass:[NSData class]])return nil;
    return [[JSONDecoder decoderWithParseOptions:JKParseOptionStrict] objectWithData:data error:error];
}
+ (NSData *)dataWithJSONObject:(id)object options:(NSUInteger)options error:(NSError **)error {
    if(options||(![object isKindOfClass:[NSArray class]]&&![object isKindOfClass:[NSDictionary class]]))return nil;
    return [object JSONDataWithOptions:JKSerializeOptionNone error:error];
}
@end
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
static int VIMPeriodBase64Digit(unsigned char c){
    if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;
}
@implementation NSData (VIMLeopardBase64)
- (id)initWithBase64Encoding:(NSString *)string {
    NSData *encoded=[string isKindOfClass:[NSString class]]?[string dataUsingEncoding:NSASCIIStringEncoding allowLossyConversion:NO]:nil;
    NSUInteger length=[encoded length];
    if(!encoded||(length&3)){[self release];return nil;}
    NSMutableData *decoded=[NSMutableData dataWithLength:(length/4)*3];
    const unsigned char *source=[encoded bytes];unsigned char *target=[decoded mutableBytes];NSUInteger count=0;
    for(NSUInteger i=0;i<length;i+=4){
        int a=VIMPeriodBase64Digit(source[i]),b=VIMPeriodBase64Digit(source[i+1]);
        int c=source[i+2]=='='?-2:VIMPeriodBase64Digit(source[i+2]);
        int d=source[i+3]=='='?-2:VIMPeriodBase64Digit(source[i+3]);
        if(a<0||b<0||c==-1||d==-1||(c==-2&&d!=-2)||
           ((c==-2||d==-2)&&i+4!=length)||(c==-2&&(b&15))||(d==-2&&c>=0&&(c&3))){[self release];return nil;}
        target[count++]=(unsigned char)((a<<2)|(b>>4));
        if(c>=0){target[count++]=(unsigned char)((b<<4)|(c>>2));if(d>=0)target[count++]=(unsigned char)((c<<6)|d);}
    }
    return [self initWithBytes:target length:count];
}
- (NSString *)base64Encoding {
    static const char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    NSUInteger count=[self length],maximum=~(NSUInteger)0;
    if(count>maximum-2||(count+2)/3>maximum/4)return nil;
    NSMutableData *encoded=[NSMutableData dataWithLength:((count+2)/3)*4];
    const unsigned char *source=[self bytes];unsigned char *target=[encoded mutableBytes];NSUInteger out=0;
    for(NSUInteger i=0;i<count;){
        unsigned a=source[i++],b=0,c=0;BOOL hasB=i<count;if(hasB)b=source[i++];BOOL hasC=i<count;if(hasC)c=source[i++];
        target[out++]=digits[a>>2];target[out++]=digits[((a&3)<<4)|(b>>4)];
        target[out++]=hasB?digits[((b&15)<<2)|(c>>6)]:'=';target[out++]=hasC?digits[c&63]:'=';
    }
    return [[[NSString alloc] initWithBytes:[encoded bytes] length:[encoded length] encoding:NSASCIIStringEncoding] autorelease];
}
@end
#endif
@implementation VIMPeriodRegularExpression
+ (id)regularExpressionWithPattern:(NSString *)pattern options:(NSUInteger)options error:(NSError **)error {
    (void)error;
    // Only the three static anchored identity checks used by our helpers.
    // No silent approximation of NSRegularExpression's general search API.
    if(options||![pattern hasPrefix:@"^"]||![pattern hasSuffix:@"$"])return nil;
    VIMPeriodRegularExpression *value=[[[self alloc] init] autorelease];
    value->predicate=[[NSPredicate predicateWithFormat:@"SELF MATCHES %@",pattern] retain];return value;
}
- (NSUInteger)numberOfMatchesInString:(NSString *)string options:(NSUInteger)options range:(NSRange)range {
    if(options||range.location||range.length!=[string length]||![string isKindOfClass:[NSString class]])return 0;
    return [predicate evaluateWithObject:string]?1:0;
}
- (void)dealloc {[predicate release];[super dealloc];}
@end
@implementation NSFileHandle (VIMPeriodNullDevice)
+ (id)fileHandleWithNullDevice {
    int fd=open("/dev/null",O_RDWR|O_CLOEXEC);
    return fd<0?nil:[[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES] autorelease];
}
@end
@implementation NSURL (VIMPeriodDirectoryURL)
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
+ (NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)directory {
    // Only local cold-control paths. A trailing separator preserves directory
    // URL resolution for a not-yet-created destination on native Tiger.
    return [NSURL fileURLWithPath:directory&&![path hasSuffix:@"/"]?[path stringByAppendingString:@"/"]:path];
}
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
- (NSURL *)URLByAppendingPathComponent:(NSString *)component {
    // Guest controls only construct local staging/volume paths. Do not
    // approximate Foundation's later arbitrary network-URL path semantics.
    return [self isFileURL]?[NSURL fileURLWithPath:[[self path] stringByAppendingPathComponent:component]]:nil;
}
#endif
- (NSURL *)URLByAppendingPathComponent:(NSString *)component isDirectory:(BOOL)directory {
    if(![self isFileURL])return [self URLByAppendingPathComponent:component];
    return [NSURL fileURLWithPath:[[self path] stringByAppendingPathComponent:component] isDirectory:directory];
}
@end
@implementation NSFileManager (VIMPeriodDirectoryURL)
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
- (BOOL)createDirectoryAtPath:(NSString *)path withIntermediateDirectories:(BOOL)parents attributes:(NSDictionary *)attributes error:(NSError **)error {
    BOOL directory=NO;
    if(parents&&[self fileExistsAtPath:path isDirectory:&directory]) {
        if(directory)return YES;
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:ENOTDIR userInfo:nil];return NO;
    }
    NSString *parent=[path stringByDeletingLastPathComponent];
    if(parents&&![parent isEqual:path]&&[parent length]&&
       ![self createDirectoryAtPath:parent withIntermediateDirectories:YES attributes:attributes error:error])return NO;
    if([self createDirectoryAtPath:path attributes:attributes])return YES;
    if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:errno?:EIO userInfo:nil];return NO;
}
#endif
- (BOOL)createDirectoryAtURL:(NSURL *)URL withIntermediateDirectories:(BOOL)parents attributes:(NSDictionary *)attributes error:(NSError **)error {
    return [URL isFileURL]&&[self createDirectoryAtPath:[URL path] withIntermediateDirectories:parents attributes:attributes error:error];
}
@end
#endif
