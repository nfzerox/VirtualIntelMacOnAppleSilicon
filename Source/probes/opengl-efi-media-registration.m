// Pure metadata fixture: never opens or changes a system filesystem bundle.
#import <Foundation/Foundation.h>
#import "../tools/opengl-efi-media.h"
#include <assert.h>
int main(void){@autoreleasepool{
    NSString *efi=@"C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
    NSDictionary *match=[NSDictionary dictionaryWithObjectsAndKeys:@"DOS_FAT_32",@"Content Hint",[NSNumber numberWithBool:YES],@"Leaf",nil];
    NSDictionary *fat=[NSDictionary dictionaryWithObjectsAndKeys:match,@"FSMediaProperties",@"-p",@"FSProbeArguments",@"../../msdos.util",@"FSProbeExecutable",[NSNumber numberWithInt:1000],@"FSProbeOrder",nil];
    NSDictionary *types=[NSDictionary dictionaryWithObjectsAndKeys:fat,@"DOS_FAT_32",@"unrelated",@"other",nil];
    NSDictionary *original=[NSDictionary dictionaryWithObjectsAndKeys:types,@"FSMediaTypes",@"unchanged",@"FSPersonalities",nil];
    NSDictionary *updated=VIMOpenGLEFIMediaInfo(original);
    NSDictionary *next=[updated objectForKey:@"FSMediaTypes"],*candidate=[next objectForKey:efi];
    assert(updated&&updated!=original&&[next count]==[types count]+1);
    assert([[next objectForKey:@"DOS_FAT_32"] isEqual:fat]);
    assert([[next objectForKey:@"other"] isEqual:@"unrelated"]);
    assert([[updated objectForKey:@"FSPersonalities"] isEqual:@"unchanged"]);
    assert([[candidate objectForKey:@"FSProbeExecutable"] isEqual:@"../../msdos.util"]);
    assert([[[candidate objectForKey:@"FSMediaProperties"] objectForKey:@"Content Hint"] isEqual:efi]);
    assert([[candidate objectForKey:@"autodiskmount"] isEqual:[NSNumber numberWithBool:NO]]);
    assert(![types objectForKey:efi]&&![fat objectForKey:@"autodiskmount"]);
    assert(VIMOpenGLEFIMediaInfo(updated)==updated);
    assert(!VIMOpenGLEFIMediaInfo([NSDictionary dictionary]));
    assert(!VIMOpenGLEFIMediaInfo([NSDictionary dictionaryWithObject:@"invalid" forKey:@"FSMediaTypes"]));
    NSData *xml=[NSPropertyListSerialization dataWithPropertyList:updated format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    assert([[NSPropertyListSerialization propertyListWithData:xml options:0 format:NULL error:NULL] isEqual:updated]);
    puts("PASS: EFI-only probe, no automount, unchanged FAT/user-media policy, idempotent native registration");
    return 0;
}}
