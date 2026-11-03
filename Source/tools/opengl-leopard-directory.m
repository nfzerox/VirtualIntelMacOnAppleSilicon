#import "opengl-leopard-directory.h"
#import "opengl-guest-machine.h"
#include <unistd.h>
#include <string.h>
// Contract: real 10.5 SDK DirServices{,Const,Utils}.h, independently exercised
// against the native Local/Default node. The 514.25 Local plugin source also
// confirms root password-setting and native shadow-hash initialization.
static void DirectoryError(NSError **error,tDirStatus status){
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.DirectoryService" code:status userInfo:nil];
}
static void Erase(void *memory,size_t count){
    volatile unsigned char *bytes=memory;while(count--)*bytes++=0;
}
static BOOL MayMutate(NSError **error){
    if(geteuid()==0&&VIMOpenGLGuestMachine())return YES;
    DirectoryError(error,eDSPermissionError);return NO;
}
static tDataNodePtr StringNode(VIMLeopardDirectory *owner,NSString *value){
    if(![value isKindOfClass:[NSString class]])return NULL;
    const char *text=[value UTF8String];
    return text&&strlen(text)==[value lengthOfBytesUsingEncoding:NSUTF8StringEncoding]?
        dsDataNodeAllocateString(owner->service,text):NULL;
}
static void FreeList(tDirReference service,tDataListPtr list){
    if(list){dsDataListDeallocate(service,list);free(list);}
}
// The native Local plugin's open-record cache matches names without checking
// record type. Never retain Users/name while opening SharePoints/name (or a
// same-named group). Wrappers keep identity, not an open native record; every
// individual operation acquires, verifies and closes its reference.
static tRecordReference OpenRecord(VIMLeopardDirectory *owner,NSString *type,NSString *name,NSError **error){
    tDataNodePtr t=StringNode(owner,type),n=StringNode(owner,name);tRecordReference reference=0;
    tDirStatus status=t&&n?dsOpenRecord(owner->node,t,n,&reference):eDSInvalidBuffFormat;
    if(t)dsDataNodeDeAllocate(owner->service,t);if(n)dsDataNodeDeAllocate(owner->service,n);
    tRecordEntryPtr entry=NULL;char *actual=NULL;
    if(!status)status=dsGetRecordReferenceInfo(reference,&entry);
    if(!status)status=dsGetRecordTypeFromEntry(entry,&actual);
    if(!status&&(!actual||strcmp(actual,[type UTF8String])))status=eDSInvalidRecordType;
    free(actual);if(entry)dsDeallocRecordEntry(owner->service,entry);
    if(status){if(reference)dsCloseRecord(reference);DirectoryError(error,status);return 0;}
    return reference;
}
static tDirStatus CloseRecord(tRecordReference reference,tDirStatus status){
    tDirStatus closed=reference?dsCloseRecord(reference):eDSNoErr;return status?status:closed;
}
@implementation VIMLeopardDirectory
+ (id)localDirectory:(NSError **)error {
    VIMLeopardDirectory *owner=[[[self alloc] init] autorelease];
    tDirStatus status=dsOpenDirService(&owner->service);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    // dsFindDirNodes(eDSLocalNodeNames) on the physical 8R3032 reference
    // returns this native NetInfo node, not Leopard's Local plugin path.
    const char *localPath="/NetInfo/DefaultLocalNode";
#else
    const char *localPath="/Local/Default";
#endif
    tDataListPtr path=status?NULL:dsBuildFromPath(owner->service,localPath,"/");
    if(!status)status=path?dsOpenDirNode(owner->service,path,&owner->node):eMemoryAllocError;
    FreeList(owner->service,path);
    if(status){DirectoryError(error,status);return nil;}return owner;
}
- (void)dealloc {if(node)dsCloseDirNode(node);if(service)dsCloseDirService(service);[super dealloc];}
- (VIMLeopardRecord *)recordWithRecordType:(NSString *)type name:(NSString *)name attributes:(NSArray *)attributes error:(NSError **)error {
    (void)attributes;tRecordReference reference=OpenRecord(self,type,name,error);if(!reference)return nil;
    tDirStatus status=CloseRecord(reference,eDSNoErr);
    if(status){DirectoryError(error,status);return nil;}
    return [[[VIMLeopardRecord alloc] initWithDirectory:self type:type name:name] autorelease];
}
- (NSArray *)recordsOfType:(NSString *)type named:(NSString *)name error:(NSError **)error {
    // Count exact matches, including continuation pages. Never reinterpret
    // a failed lookup as an absent record that is safe to create/overwrite.
    tDataNodePtr validType=StringNode(self,type),validName=StringNode(self,name);
    if(validType)dsDataNodeDeAllocate(service,validType);
    if(validName)dsDataNodeDeAllocate(service,validName);
    if(!validType||!validName){DirectoryError(error,eDSInvalidBuffFormat);return nil;}
    tDataListPtr types=dsBuildListFromStrings(service,[type UTF8String],NULL);
    tDataListPtr names=dsBuildListFromStrings(service,[name UTF8String],NULL);
    tDataListPtr attrs=dsBuildListFromStrings(service,kDSNAttrRecordName,NULL);
    tContextData context=0;UInt32 capacity=4096,total=0;tDirStatus status=eDSNoErr;
    tDataBufferPtr buffer=types&&names&&attrs?dsDataBufferAllocate(service,capacity):NULL;
    if(!buffer)status=eMemoryAllocError;
    for(unsigned batch=0;!status&&batch<64;batch++){
        UInt32 count=2;
        status=dsGetRecordList(node,buffer,names,eDSExact,types,attrs,true,&count,&context);
        if(status==eDSBufferTooSmall&&capacity<1024*1024){
            dsDataBufferDeAllocate(service,buffer);capacity*=2;buffer=dsDataBufferAllocate(service,capacity);
            status=buffer?eDSNoErr:eMemoryAllocError;continue;
        }
        if(status)break;total+=count;
        if(!context||total>1)break;
        if(batch==63)status=eDSInvalidContinueData;
    }
    if(context)dsReleaseContinueData(node,context);
    if(buffer)dsDataBufferDeAllocate(service,buffer);
    FreeList(service,types);FreeList(service,names);FreeList(service,attrs);
    if(status){DirectoryError(error,status);return nil;}
    if(!total)return [NSArray array];
    VIMLeopardRecord *found=[self recordWithRecordType:type name:name attributes:nil error:error];if(!found)return nil;
    return total==1?[NSArray arrayWithObject:found]:[NSArray arrayWithObjects:found,found,nil];
}
- (VIMLeopardRecord *)createRecordWithRecordType:(NSString *)type name:(NSString *)name attributes:(NSDictionary *)attributes error:(NSError **)error {
    if(!MayMutate(error))return nil;
    tDataNodePtr t=StringNode(self,type),n=StringNode(self,name);tRecordReference reference=0;
    tDirStatus status=t&&n?dsCreateRecordAndOpen(node,t,n,&reference):eDSInvalidBuffFormat;
    if(t)dsDataNodeDeAllocate(service,t);if(n)dsDataNodeDeAllocate(service,n);
    status=CloseRecord(reference,status);
    if(status){DirectoryError(error,status);return nil;}
    VIMLeopardRecord *created=[[[VIMLeopardRecord alloc] initWithDirectory:self type:type name:name] autorelease];
    // Publish the transaction's identity before less-important attributes.
    NSArray *identity=[NSArray arrayWithObjects:@kDS1AttrGeneratedUID,@kDS1AttrUniqueID,nil];
    for(NSUInteger index=0;index<[identity count];index++){
        NSString *key=[identity objectAtIndex:index];
        if([attributes objectForKey:key]&&![created setValue:[attributes objectForKey:key] forAttribute:key error:error])return nil;
    }
    NSEnumerator *enumerator=[attributes keyEnumerator];NSString *key;
    while((key=[enumerator nextObject])){
        if([key isEqual:@kDS1AttrGeneratedUID]||[key isEqual:@kDS1AttrUniqueID])continue;
        if(![created setValue:[attributes objectForKey:key] forAttribute:key error:error])return nil;
    }
    return [created synchronizeAndReturnError:error]?created:nil;
}
@end
@implementation VIMLeopardRecord
- (id)initWithDirectory:(VIMLeopardDirectory *)owner type:(NSString *)type name:(NSString *)name {
    if((self=[super init])){directory=[owner retain];recordType=[type copy];recordName=[name copy];}return self;
}
- (void)dealloc {[directory release];[recordType release];[recordName release];[super dealloc];}
- (NSArray *)valuesForAttribute:(NSString *)attribute error:(NSError **)error {
    tRecordReference record=OpenRecord(directory,recordType,recordName,error);if(!record)return nil;
    tDataNodePtr type=StringNode(directory,attribute);tAttributeEntryPtr info=NULL;
    tDirStatus status=type?dsGetRecordAttributeInfo(record,type,&info):eDSInvalidBuffFormat;
    NSMutableArray *values=[NSMutableArray array];
    if(!status&&!info)status=eDSInvalidBuffFormat;
    if(!status&&info->fAttributeValueCount>4096)status=eDSBufferTooSmall;
    for(UInt32 i=1;!status&&i<=info->fAttributeValueCount;i++){
        tAttributeValueEntryPtr entry=NULL;status=dsGetRecordAttributeValueByIndex(record,type,i,&entry);
        if(!status&&(!entry||entry->fAttributeValueData.fBufferLength>1024*1024))status=eDSBufferTooSmall;
        NSString *value=!status?[[[NSString alloc] initWithBytes:entry->fAttributeValueData.fBufferData
            length:entry->fAttributeValueData.fBufferLength encoding:NSUTF8StringEncoding] autorelease]:nil;
        if(!status&&!value)status=eDSInvalidBuffFormat;if(value)[values addObject:value];
        if(entry)dsDeallocAttributeValueEntry(directory->service,entry);
    }
    if(info)dsDeallocAttributeEntry(directory->service,info);if(type)dsDataNodeDeAllocate(directory->service,type);
    status=CloseRecord(record,status);
    if(status){DirectoryError(error,status);return nil;}return values;
}
- (BOOL)setValue:(id)value forAttribute:(NSString *)attribute error:(NSError **)error {
    if(!MayMutate(error))return NO;
    if(!value){DirectoryError(error,eDSInvalidBuffFormat);return NO;}
    NSArray *values=[value isKindOfClass:[NSArray class]]?value:[NSArray arrayWithObject:value];
    if(![values count]||[values count]>4096){DirectoryError(error,eDSInvalidBuffFormat);return NO;}
    tDataNodePtr type=StringNode(directory,attribute);tDataListPtr list=dsDataListAllocate(directory->service);
    tDirStatus status=type&&list?eDSNoErr:eMemoryAllocError;
    tRecordReference record=0;
    for(NSUInteger index=0;index<[values count];index++){
        id item=[values objectAtIndex:index];
        if(status)break;
        const char *text=[item isKindOfClass:[NSString class]]?[item UTF8String]:NULL;
        if(!text||strlen(text)>16384||strlen(text)!=[item lengthOfBytesUsingEncoding:NSUTF8StringEncoding])status=eDSInvalidBuffFormat;
        else status=dsAppendStringToListAlloc(directory->service,list,text);
    }
    if(!status){
        record=OpenRecord(directory,recordType,recordName,error);
        if(!record){
            if(type)dsDataNodeDeAllocate(directory->service,type);FreeList(directory->service,list);
            return NO; // Preserve the native lookup/type error, not a made-up replacement.
        }
    }
    tAttributeEntryPtr info=NULL;
    if(!status){
        status=dsGetRecordAttributeInfo(record,type,&info);
        if(status==eDSAttributeNotFound){
            tDataNodePtr first=StringNode(directory,[values objectAtIndex:0]);
            status=first?dsAddAttribute(record,type,NULL,first):eMemoryAllocError;
            if(first)dsDataNodeDeAllocate(directory->service,first);
        }
        if(!status)status=dsSetAttributeValues(record,type,list);
    }
    if(info)dsDeallocAttributeEntry(directory->service,info);if(type)dsDataNodeDeAllocate(directory->service,type);FreeList(directory->service,list);
    status=CloseRecord(record,status);
    if(status)DirectoryError(error,status);return status==eDSNoErr;
}
- (BOOL)synchronizeAndReturnError:(NSError **)error {
    if(!MayMutate(error))return NO;
    tRecordReference record=OpenRecord(directory,recordType,recordName,error);if(!record)return NO;
    tDirStatus status=CloseRecord(record,dsFlushRecord(record));if(status)DirectoryError(error,status);return status==eDSNoErr;
}
- (BOOL)authenticate:(NSString *)password method:(const char *)method error:(NSError **)error {
    if(![recordType isEqual:@kDSStdRecordTypeUsers]||![password isKindOfClass:[NSString class]]){DirectoryError(error,eDSAuthFailed);return NO;}
    char secret[512]={0};NSUInteger count=[password lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    if(count>=sizeof(secret)||![password getCString:secret maxLength:sizeof(secret) encoding:NSUTF8StringEncoding]||strlen(secret)!=count){
        Erase(secret,sizeof(secret));DirectoryError(error,eDSAuthFailed);return NO;
    }
    const char *user=[recordName UTF8String];uint32_t userLength=(uint32_t)strlen(user),passwordLength=(uint32_t)count;
    tDataNodePtr auth=dsDataNodeAllocateString(directory->service,method);
    tDataBufferPtr input=dsDataBufferAllocate(directory->service,8+userLength+passwordLength),output=dsDataBufferAllocate(directory->service,4096);
    tDirStatus status=eMemoryAllocError;tContextData context=0;
    if(auth&&input&&output){
        char *cursor=input->fBufferData;memcpy(cursor,&userLength,4);cursor+=4;memcpy(cursor,user,userLength);cursor+=userLength;
        memcpy(cursor,&passwordLength,4);cursor+=4;memcpy(cursor,secret,passwordLength);input->fBufferLength=8+userLength+passwordLength;
        status=dsDoDirNodeAuth(directory->node,auth,true,input,output,&context);
        if(!status&&context)status=eDSAuthFailed;
    }
    Erase(secret,sizeof(secret));
    if(context)dsReleaseContinueData(directory->node,context);
    for(unsigned n=0;n<2;n++){tDataBufferPtr buffer=n?output:input;if(buffer){
        Erase(buffer->fBufferData,buffer->fBufferSize);
        dsDataBufferDeAllocate(directory->service,buffer);
    }}
    if(auth)dsDataNodeDeAllocate(directory->service,auth);if(status)DirectoryError(error,status);return status==eDSNoErr;
}
- (BOOL)verifyPassword:(NSString *)password error:(NSError **)error {
    return [self authenticate:password method:kDSStdAuthNodeNativeClearTextOK error:error];
}
- (BOOL)changePassword:(NSString *)oldPassword toPassword:(NSString *)password error:(NSError **)error {
    if(oldPassword||!MayMutate(error))return NO;
    return [self authenticate:password method:kDSStdAuthSetPasswdAsRoot error:error];
}
- (BOOL)enableWindowsSharingWithPassword:(NSString *)password error:(NSError **)error {
    if(!MayMutate(error))return NO;
    // Actual DirectoryService514.25 Local plugin's authenticated self-opt-in.
    // pwpolicy -sethashtypes targets PasswordServer accounts on Darwin9;
    // it does not prepare this Local/Default user's Windows-sharing hash.
    // The native method verifies the current password, preserves secure
    // hashes, creates SMB-NT, and explicitly removes SMB-LAN-MANAGER.
    if(![self authenticate:password method:"dsAuthMethodStandard:dsAuthSetShadowHashWindows" error:error])return NO;
    NSArray *authorities=[self valuesForAttribute:@kDSNAttrAuthenticationAuthority error:error];
    BOOL ready=NO;
    for(NSUInteger index=0;index<[authorities count];index++){
        id authority=[authorities objectAtIndex:index];
        if(![authority isKindOfClass:[NSString class]]||[authority rangeOfString:@";ShadowHash;"].location==NSNotFound)continue;
        NSRange start=[authority rangeOfString:@"HASHLIST:<"];
        if(start.location==NSNotFound)continue;
        NSString *tail=[authority substringFromIndex:NSMaxRange(start)];NSRange end=[tail rangeOfString:@">"];
        if(end.location==NSNotFound)continue;
        NSArray *types=[[tail substringToIndex:end.location] componentsSeparatedByString:@","];
        if([types containsObject:@"SMB-LAN-MANAGER"]){DirectoryError(error,eDSAuthFailed);return NO;}
        if([types containsObject:@"SMB-NT"])ready=YES;
    }
    if(!ready){DirectoryError(error,eDSAuthFailed);return NO;}
    return [self verifyPassword:password error:error];
}
- (BOOL)addMemberRecord:(VIMLeopardRecord *)member error:(NSError **)error {
    if(!MayMutate(error)||![recordType isEqual:@kDSStdRecordTypeGroups]||![recordName isEqual:@"admin"]||
       ![member->recordType isEqual:@kDSStdRecordTypeUsers])return NO;
    NSArray *guids=[member valuesForAttribute:@kDS1AttrGeneratedUID error:error];if([guids count]!=1)return NO;
    NSArray *keys=[NSArray arrayWithObjects:@kDSNAttrGroupMembers,@kDSNAttrGroupMembership,nil];
    NSArray *items=[NSArray arrayWithObjects:[guids objectAtIndex:0],member->recordName,nil];
    for(NSUInteger i=0;i<[keys count];i++){
        NSString *key=[keys objectAtIndex:i],*item=[items objectAtIndex:i];NSError *issue=nil;
        NSArray *current=[self valuesForAttribute:key error:&issue];
        if(!current&&[issue code]!=eDSAttributeNotFound){if(error)*error=issue;return NO;}
        if([current containsObject:item])continue;
        NSMutableArray *updated=current?[[current mutableCopy] autorelease]:[NSMutableArray array];[updated addObject:item];
        if(![self setValue:updated forAttribute:key error:error])return NO;
    }
    return [self synchronizeAndReturnError:error];
}
@end
