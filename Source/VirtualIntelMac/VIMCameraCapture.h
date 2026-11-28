#import <Foundation/Foundation.h>
@import AVFoundation;
#include "../vendor/qemu/hw/usb/vim-camera-stream.h"

static inline NSArray<NSDictionary<NSString *,NSString *> *> *VIMHostCameraDevices(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSArray<AVCaptureDevice *> *found=[AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
    NSMutableArray *devices=[NSMutableArray new];
    for(AVCaptureDevice *device in found) {
        NSString *name=device.localizedName, *uid=device.uniqueID;
        if(name.length&&uid.length)[devices addObject:@{@"Name":name,@"UID":uid,
            @"BuiltIn":[device.deviceType isEqual:AVCaptureDeviceTypeBuiltInWideAngleCamera]?@"yes":@"no"}];
    }
    return [devices sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){
        BOOL builtA=[a[@"BuiltIn"] isEqual:@"yes"], builtB=[b[@"BuiltIn"] isEqual:@"yes"];
        if(builtA!=builtB)return builtA?NSOrderedAscending:NSOrderedDescending;
        return [a[@"Name"] localizedStandardCompare:b[@"Name"]];
    }];
}
static inline NSString *VIMCameraSelection(NSDictionary *configuration) {
    NSString *value=configuration[@"CameraDevice"];
    return [value isKindOfClass:NSString.class]&&value.length?value:@"disabled";
}
static inline NSString *VIMCameraMode(NSDictionary *configuration) {
    NSString *mode=configuration[@"CameraMode"];
    if([mode isEqual:@"constant"]||[mode isEqual:@"autosense"]||[mode isEqual:@"testpattern"])return mode;
    return @"off";
}
static inline BOOL VIMCameraTestPatternOffered(NSString *mode) {
    return [NSUserDefaults.standardUserDefaults boolForKey:@"VIMShowCameraTestPattern"]||[mode isEqual:@"testpattern"];
}
static inline BOOL VIMCameraDeviceAdvertised(NSDictionary *configuration) {
    return ![VIMCameraSelection(configuration) isEqual:@"disabled"];
}
static inline BOOL VIMCameraModeUsesHostCamera(NSString *mode) {
    return [mode isEqual:@"constant"]||[mode isEqual:@"autosense"];
}
static inline BOOL VIMCameraChimesEnabled(NSDictionary *configuration) {
    id value=configuration[@"CameraChimesEnabled"];
    return [value respondsToSelector:@selector(boolValue)]?[value boolValue]:YES;
}
static inline BOOL VIMCameraRoutingEnabled(NSDictionary *configuration) {
    NSString *mode=VIMCameraMode(configuration), *selection=VIMCameraSelection(configuration);
    if([mode isEqual:@"off"]||!VIMCameraDeviceAdvertised(configuration))return NO;
    if([mode isEqual:@"testpattern"])return YES;
    if([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]!=AVAuthorizationStatusAuthorized)return NO;
    if([selection isEqual:@"default"])return VIMHostCameraDevices().count>0;
    for(NSDictionary *device in VIMHostCameraDevices())if([device[@"UID"] isEqual:selection])return YES;
    return NO;
}
static inline NSArray<NSDictionary<NSString *,NSString *> *> *VIMCameraFunctions(NSDictionary *configuration) {
    NSString *mode=VIMCameraMode(configuration);
    if([mode isEqual:@"off"]||!VIMCameraDeviceAdvertised(configuration))return @[];
    if([mode isEqual:@"testpattern"])
        return @[@{@"Name":@"Test Pattern",@"UID":@"testpattern"}];
    NSArray *devices=VIMHostCameraDevices();
    if(devices.count>VIM_CAMERA_MAX_FUNCTIONS)
        devices=[devices subarrayWithRange:NSMakeRange(0,VIM_CAMERA_MAX_FUNCTIONS)];
    NSString *selection=VIMCameraSelection(configuration);
    if([VIMCameraMode(configuration) isEqual:@"constant"]&&selection.length&&![selection isEqual:@"default"]&&![selection isEqual:@"disabled"]) {
        NSUInteger found=[devices indexOfObjectPassingTest:^BOOL(NSDictionary *device,NSUInteger index,BOOL *stop){return [device[@"UID"] isEqual:selection];}];
        if(found!=NSNotFound&&found>0) {
            NSMutableArray *ordered=[devices mutableCopy];
            NSDictionary *chosen=ordered[found];
            [ordered removeObjectAtIndex:found];
            [ordered insertObject:chosen atIndex:0];
            devices=ordered;
        }
    }
    return devices;
}
static inline NSString *VIMCameraPreferredUID(NSDictionary *configuration) {
    NSString *selection=VIMCameraSelection(configuration);
    if([VIMCameraMode(configuration) isEqual:@"testpattern"])return @"testpattern";
    if([selection isEqual:@"disabled"])return @"";
    if(![selection isEqual:@"default"])return selection;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    AVCaptureDevice *fallback=[AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
    return fallback.uniqueID?:@"";
}

NSData *VIMCameraYUY2FromBGRA(const uint8_t *pixels,NSInteger width,NSInteger height,NSInteger bytesPerRow);
@interface VIMCameraCapture : NSObject
- (void)startWithSocketPath:(NSString *)path functions:(NSUInteger)functions guestOpened:(void (^)(NSString *uid))guestOpened log:(void (^)(NSString *message))log;
- (void)updateWithCameras:(NSArray<NSDictionary<NSString *,NSString *> *> *)cameras mode:(NSString *)mode preferredUID:(NSString *)uid enabled:(BOOL)enabled;
- (void)stop;
@property unsigned frameRate;
@property(copy) void (^autosenseActivityChanged)(BOOL active);
@property(copy) void (^driverChanged)(BOOL loaded);
@end
