#import <Foundation/Foundation.h>

// Edits the stock NetInfo seed unpacked into a new Tiger disk, never a
// live account database. Returns the assigned UID, or zero on failure.
uint32_t VIMCreateTigerAccount(NSURL *seed, NSString *name, NSString *guid,
                              BOOL smb, NSError **error);
NSData *VIMClassicShadowHash(NSData *password, BOOL smb);
NSData *VIMClassicAutoLoginPassword(NSData *password);
