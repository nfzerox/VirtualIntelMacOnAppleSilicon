#pragma once
// Keep the transaction/journal shared. Only its native directory boundary
// differs; no OpenDirectory image or private runtime is donated to Leopard.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#import "opengl-leopard-directory.h"
typedef VIMLeopardDirectory VIMAccountNode;
typedef VIMLeopardRecord VIMAccountRecord;
// Both APIs use these same public DirectoryService attribute identifiers.
#define kODRecordTypeUsers @kDSStdRecordTypeUsers
#define kODRecordTypeGroups @kDSStdRecordTypeGroups
#define kODRecordTypeSharePoints @kDSStdRecordTypeSharePoints
#define kODAttributeTypeGUID @kDS1AttrGeneratedUID
#define kODAttributeTypeUniqueID @kDS1AttrUniqueID
#define kODAttributeTypePrimaryGroupID @kDS1AttrPrimaryGroupID
#define kODAttributeTypeFullName @kDS1AttrDistinguishedName
#define kODAttributeTypeNFSHomeDirectory @kDS1AttrNFSHomeDirectory
#define kODAttributeTypeUserShell @kDS1AttrUserShell
#define kODAttributeTypePassword @kDS1AttrPassword
#define kODAttributeTypeAllAttributes @kDSAttributesAll
static inline VIMAccountNode *VIMAccountLocalNode(void){return [VIMLeopardDirectory localDirectory:NULL];}
#else
#import <OpenDirectory/OpenDirectory.h>
typedef ODNode VIMAccountNode;
typedef ODRecord VIMAccountRecord;
static inline VIMAccountNode *VIMAccountLocalNode(void){
    return [ODNode nodeWithSession:[ODSession defaultSession] type:kODNodeTypeLocalNodes error:NULL];
}
#endif
