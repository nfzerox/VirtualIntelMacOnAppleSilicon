#import <Foundation/Foundation.h>
// Mojave-only dependency adapter. Catalina Foundation metadata verifies offsets
// encoding=8, value=16, size24. Donor GLD's eight __objc_intobj entries match.
@interface NSConstantIntegerNumber : NSNumber {
    const char *_encoding;
    long long _value;
}
@end
@implementation NSConstantIntegerNumber
- (const char *)objCType { return _encoding; }
- (void)getValue:(void *)output {
    NSUInteger size=0;NSGetSizeAndAlignment(_encoding,&size,NULL);
    if(size>sizeof(_value)) [NSException raise:NSInvalidArgumentException format:@"Invalid constant integer encoding"];
    memcpy(output,&_value,size);
}
- (void)getValue:(void *)output size:(NSUInteger)available {
    NSUInteger size=0;NSGetSizeAndAlignment(_encoding,&size,NULL);
    if(available<size) [NSException raise:NSInvalidArgumentException format:@"Constant integer destination too small"];
    [self getValue:output];
}
- (BOOL)boolValue {return _value!=0;}
- (char)charValue {return (char)_value;}
- (unsigned char)unsignedCharValue {return (unsigned char)_value;}
- (short)shortValue {return (short)_value;}
- (unsigned short)unsignedShortValue {return (unsigned short)_value;}
- (int)intValue {return (int)_value;}
- (unsigned)unsignedIntValue {return (unsigned)_value;}
- (long)longValue {return (long)_value;}
- (unsigned long)unsignedLongValue {return (unsigned long)_value;}
- (long long)longLongValue {return _value;}
- (unsigned long long)unsignedLongLongValue {return (unsigned long long)_value;}
- (NSInteger)integerValue {return (NSInteger)_value;}
- (NSUInteger)unsignedIntegerValue {return (NSUInteger)_value;}
- (float)floatValue {return (float)_value;}
- (double)doubleValue {return (double)_value;}
- (NSDecimal)decimalValue {return [[NSNumber numberWithLongLong:_value] decimalValue];}
- (id)retain {return self;}
- (oneway void)release {}
- (id)autorelease {return self;}
- (NSUInteger)retainCount {return NSUIntegerMax;}
- (BOOL)_tryRetain {return YES;}
- (BOOL)_isDeallocating {return NO;}
- (id)copyWithZone:(NSZone *)zone {(void)zone;return self;}
@end
