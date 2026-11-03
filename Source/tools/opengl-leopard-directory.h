// A narrow native DirectoryService adapter for the authorized account
// transaction. This is not an OpenDirectory runtime/framework replacement.
#pragma once
#import <Foundation/Foundation.h>
#import <DirectoryService/DirectoryService.h>
@class VIMLeopardRecord;
@interface VIMLeopardDirectory : NSObject {
@public
    tDirReference service;
    tDirNodeReference node;
}
+ (id)localDirectory:(NSError **)error;
- (VIMLeopardRecord *)recordWithRecordType:(NSString *)type name:(NSString *)name attributes:(NSArray *)attributes error:(NSError **)error;
- (VIMLeopardRecord *)createRecordWithRecordType:(NSString *)type name:(NSString *)name attributes:(NSDictionary *)attributes error:(NSError **)error;
- (NSArray *)recordsOfType:(NSString *)type named:(NSString *)name error:(NSError **)error;
@end
@interface VIMLeopardRecord : NSObject {
    VIMLeopardDirectory *directory;
    NSString *recordType,*recordName;
}
- (id)initWithDirectory:(VIMLeopardDirectory *)owner type:(NSString *)type name:(NSString *)name;
- (NSArray *)valuesForAttribute:(NSString *)attribute error:(NSError **)error;
- (BOOL)setValue:(id)value forAttribute:(NSString *)attribute error:(NSError **)error;
- (BOOL)synchronizeAndReturnError:(NSError **)error;
- (BOOL)verifyPassword:(NSString *)password error:(NSError **)error;
- (BOOL)changePassword:(NSString *)oldPassword toPassword:(NSString *)password error:(NSError **)error;
- (BOOL)enableWindowsSharingWithPassword:(NSString *)password error:(NSError **)error;
- (BOOL)addMemberRecord:(VIMLeopardRecord *)member error:(NSError **)error;
@end
