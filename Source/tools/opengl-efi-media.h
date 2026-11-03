// OS X 10.8's msdos.fs predates the native EFI-partition probe entry added
// in Apple's msdosfs-198. An EFI-only firmware disk otherwise has no probe
// candidate and DiskArbitration calls the whole unit unreadable. Reuse the
// installed FAT32 probe (including its period-correct executable path), and
// explicitly prohibit automount. Never change other filesystem candidates.
static NSDictionary *VIMOpenGLEFIMediaInfo(NSDictionary *original)
{
    NSString *efi=@"C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
    NSDictionary *types=[original objectForKey:@"FSMediaTypes"];
    if(![types isKindOfClass:[NSDictionary class]])return nil;
    // A later OS/security update may supply its own native registration.
    // Keep that registration intact rather than overriding Apple's policy.
    if([types objectForKey:efi])return original;
    NSDictionary *fat=[types objectForKey:@"DOS_FAT_32"];
    NSDictionary *properties=[fat isKindOfClass:[NSDictionary class]]?[fat objectForKey:@"FSMediaProperties"]:nil;
    if(![properties isKindOfClass:[NSDictionary class]]||
       ![[properties objectForKey:@"Content Hint"] isEqual:@"DOS_FAT_32"]||
       ![[properties objectForKey:@"Leaf"] isEqual:[NSNumber numberWithBool:YES]]||
       ![[fat objectForKey:@"FSProbeArguments"] isEqual:@"-p"]||
       ![[fat objectForKey:@"FSProbeExecutable"] isKindOfClass:[NSString class]]||
       ![[fat objectForKey:@"FSProbeOrder"] isKindOfClass:[NSNumber class]])return nil;
    NSMutableDictionary *candidate=[[fat mutableCopy] autorelease];
    NSMutableDictionary *match=[[properties mutableCopy] autorelease];
    [match setObject:efi forKey:@"Content Hint"];
    [candidate setObject:match forKey:@"FSMediaProperties"];
    [candidate setObject:[NSNumber numberWithBool:NO] forKey:@"autodiskmount"];
    NSMutableDictionary *updatedTypes=[[types mutableCopy] autorelease];
    [updatedTypes setObject:candidate forKey:efi];
    NSMutableDictionary *updated=[[original mutableCopy] autorelease];
    [updated setObject:updatedTypes forKey:@"FSMediaTypes"];
    return updated;
}
