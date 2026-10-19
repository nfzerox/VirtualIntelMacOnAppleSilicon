@interface Output:NSObject<LocationBase>

@property(assign) CacheSet* cache;
@property(assign) CacheImage* cacheImage;
@property(retain) ImageHeader* header;
@property(retain) NSMutableData* data;
@property(retain) NSMutableDictionary<NSNumber*,Address*>* fixups;
@property(retain) NSMutableSet<NSNumber*>* syntheticFixups;
@property(retain) NSMutableArray<Address*>* exports;
@property(retain) NSMutableDictionary<NSString*,Selector*>* sels;
@property(retain) NSMutableDictionary<NSNumber*,NSNumber*>* expandedMethodLists;
@property(retain) NSMutableDictionary<NSString*,NSNumber*>* restoredStrings;
@property(assign) long baseAddressDelta;
@property(assign) long objcRestoreCursor;
@property(assign) long objcRestoreEnd;

+(void)runWithCache:(CacheSet*)cache image:(CacheImage*)image outPath:(NSString*)outPath;

@end
