// Host-only check of the production media policy, without modifying the DVD.
#import "../VirtualIntelMac/VIMTigerRestoreProfile.h"
#import "../VirtualIntelMac/VIMInstallerSource.h"
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=1&&argc!=2)return 64;NSError *error=nil;
    for(NSString *hook in @[@"",@"<installation-check script=\"differentModelEligibility()\"/>"]){
        NSString *xml=[NSString stringWithFormat:@"<installer-gui-script minSpecVersion='1'>%@<volume-check script='nativeVolume()'/><choices-outline><line choice='System'/><line choice='OtherDisc'/></choices-outline><choice id='System'><pkg-ref id='core'/></choice><choice id='OtherDisc'><pkg-ref id='extra'/></choice><pkg-ref id='core'>../BaseSystem.pkg</pkg-ref><pkg-ref id='extra'>x-disc://2/Extras.pkg</pkg-ref></installer-gui-script>",hook];
        NSData *result=VIMTigerPrepareDiscOneDistribution([xml dataUsingEncoding:NSUTF8StringEncoding],NULL,&error);
        if(!result){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 11;}
        NSXMLDocument *plan=[[NSXMLDocument alloc] initWithData:result options:NSXMLNodeLoadExternalEntitiesNever error:&error];
        if([plan nodesForXPath:@"//choice[@id='System']" error:NULL].count!=1||
           [plan nodesForXPath:@"//choice[@id='OtherDisc']|//line[@choice='OtherDisc']|//pkg-ref[starts-with(text(),'x-disc:')]" error:NULL].count||
           [plan nodesForXPath:@"//volume-check[@script='nativeVolume()']" error:NULL].count!=1)return 12;
        if(hook.length&&[plan nodesForXPath:@"//installation-check[@script='true']" error:NULL].count!=1)return 13;
        if(!VIMTigerPrepareDiscOneDistribution(result,NULL,NULL))return 14;
    }
    if(VIMTigerPrepareDiscOneDistribution([@"not XML" dataUsingEncoding:NSUTF8StringEncoding],NULL,NULL))return 15;
    puts("PASS: retail/no-hook and alternate-model plans, optional-disc pruning, native volume check, repeat preparation");
    if(argc==1)return 0;
    NSURL *download=[NSURL URLWithString:@"https://dn720202.ca.archive.org/0/items/10.4.10-8-r-4088-acdt/10.4.10-8R4088-iMac7%2C1-Disc-1.dmg"];
    if(![VIMInstallerSource isRestoreDVDAsset:download forGuest:@"tiger"]||![VIMInstallerSource isApprovedURL:download]||
       [VIMInstallerSource isRestoreDVDAsset:download forGuest:@"leopard"]||
       [VIMInstallerSource isApprovedURL:[NSURL URLWithString:[download.absoluteString stringByAppendingString:@"?other=1"]]])return 10;
    NSURL *mount=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]] isDirectory:YES];
    if(!VIMTigerValidateRestoreDVD(mount,&error)){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    NSURL *path=[mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg/Contents/OSInstall.dist"];
    NSData *source=[NSData dataWithContentsOfURL:path];NSDictionary *receipt=nil;
    NSData *prepared=VIMTigerPrepareDiscOneDistribution(source,&receipt,&error);
    if(!prepared){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    NSXMLDocument *original=[[NSXMLDocument alloc] initWithData:source options:NSXMLNodeLoadExternalEntitiesNever error:&error];
    NSXMLDocument *result=[[NSXMLDocument alloc] initWithData:prepared options:NSXMLNodeLoadExternalEntitiesNever error:&error];
    if(!original||!result)return 2;
    if([result nodesForXPath:@"//pkg-ref[starts-with(text(),'x-disc:')]" error:&error].count)return 3;
    for(NSString *name in @[@"options",@"volume-check",@"license",@"welcome",@"title",@"script"]){
        NSArray *before=[original.rootElement elementsForName:name],*after=[result.rootElement elementsForName:name];
        if(before.count!=after.count)return 4;
        for(NSUInteger i=0;i<before.count;i++)if(![[before[i] XMLString] isEqual:[after[i] XMLString]])return 5;
    }
    // Every non-bundled choice is unchanged, not just a hardcoded core subset.
    for(NSXMLElement *after in [result.rootElement elementsForName:@"choice"]){
        BOOL matched=NO;
        for(NSXMLElement *before in [original.rootElement elementsForName:@"choice"])
            if([[before attributeForName:@"id"].stringValue isEqual:[after attributeForName:@"id"].stringValue]){matched=[before.XMLString isEqual:after.XMLString];break;}
        if(!matched)return 6;
    }
    NSMutableData *wrong=[source mutableCopy];((unsigned char *)wrong.mutableBytes)[wrong.length-1]^=1;
    if(VIMTigerPrepareDiscOneDistribution(wrong,NULL,NULL)||!VIMTigerPrepareDiscOneDistribution(prepared,NULL,NULL))return 7;
    if(![[NSData dataWithContentsOfURL:path] isEqual:source])return 8;
    NSData *report=[NSJSONSerialization dataWithJSONObject:receipt options:NSJSONWritingPrettyPrinted error:&error];
    if(!report)return 9;fwrite(report.bytes,1,report.length,stdout);fputc('\n',stdout);return 0;
}}
