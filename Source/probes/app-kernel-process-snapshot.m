/* Development-only, read-only recovery diagnosis. Compile as a normal ARC
 * dylib and call VIMSnapshotKernelProcessList from the visible development
 * app's debugger, passing its EXISTING VIMQMPConnection. Never link/package
 * this into the app. No second QMP client, guest pause, or memory writes.
 *
 * allproc/kmod must come from the EXACT booted kernel (see the companion symbol
 * probe). 1088 bytes is the period x86_64 proc record; interpretation requires
 * independent source/type checks. Dumps contain kernel process/module metadata,
 * not user-space arguments, environments, passwords, files or address spaces.
 * Keep outputs private. This is not a generic production memory-read API.
 *
 * Unlike an escaping LLDB expression block, these callbacks are compiled ARC
 * heap blocks with normal object lifetimes after the debugger detaches.
 * Compile with clang -fobjc-arc -fblocks -dynamiclib -framework Foundation.
 * Resolve the intended VIMSession's actual control object explicitly. NSApp's
 * mainWindow can be nil while inactive; a successful debugger expression on
 * nil is NOT a successful snapshot. Check that the result file really exists.
 */
#import <Foundation/Foundation.h>
#include <stdint.h>
#include <stdio.h>

@interface VIMQMPConnection : NSObject
- (void)execute:(NSString *)command arguments:(NSDictionary *)arguments
    completion:(void (^)(id,NSError *))completion;
@end

@interface VIMKernelMetadataSnapshot : NSObject
@property VIMQMPConnection *control;
@property NSMutableArray *records;
@property NSMutableSet *seen;
@property NSString *destination;
@property unsigned words;
- (void)read:(uint64_t)address head:(BOOL)head;
@end

@implementation VIMKernelMetadataSnapshot
- (void)finish:(NSString *)error {
    if(error)[self.records addObject:@{@"Error":error}];
    [self.records writeToFile:self.destination atomically:YES];
}
- (void)read:(uint64_t)address head:(BOOL)head {
    if(!address){[self finish:nil];return;}
    uint64_t minimum=self.words==25?0xffffff0000000000ULL:0xffffff8000000000ULL;
    if(address<minimum||[self.seen containsObject:@(address)]||self.seen.count>=256){
        [self finish:@"Invalid, repeated, or excessive process list"];return;
    }
    [self.seen addObject:@(address)];
    NSString *command=[NSString stringWithFormat:@"x /%ugx 0x%llx",head?1:self.words,(unsigned long long)address];
    [self.control execute:@"human-monitor-command" arguments:@{@"command-line":command} completion:^(id result,NSError *error){
        if(error||![result isKindOfClass:NSString.class]){[self finish:error.localizedDescription?:@"Missing HMP result"];return;}
        unsigned long long actual=0,next=0;
        if(sscanf([result UTF8String],"%llx: %llx",&actual,&next)!=2||actual!=address){[self finish:@"Unexpected HMP memory format"];return;}
        if(!head)[self.records addObject:@{@"Address":@(address),@"Memory":result}];
        [self read:next head:NO];
    }];
}
@end

__attribute__((visibility("default")))
static void Snapshot(VIMQMPConnection *control,uint64_t allproc,unsigned words,const char *output){
    if(!control||!output||allproc<0xffffff8000000000ULL)return;
    NSString *path=[NSString stringWithUTF8String:output];
    if(!path.isAbsolutePath||[NSFileManager.defaultManager fileExistsAtPath:path])return;
    VIMKernelMetadataSnapshot *snapshot=[VIMKernelMetadataSnapshot new];
    snapshot.control=control;snapshot.destination=path;snapshot.words=words;
    snapshot.records=[NSMutableArray array];snapshot.seen=[NSMutableSet set];
    [snapshot read:allproc head:YES];
}
__attribute__((visibility("default")))
void VIMSnapshotKernelProcessList(VIMQMPConnection *control,uint64_t allproc,const char *output){Snapshot(control,allproc,136,output);}
__attribute__((visibility("default")))
void VIMSnapshotKernelModules(VIMQMPConnection *control,uint64_t kmod,const char *output){Snapshot(control,kmod,25,output);}

// Single bounded metadata/stack read for an already identified kernel object.
// No user address spaces, guest mutation, or second monitor connection.
__attribute__((visibility("default")))
void VIMReadKernelMetadata(VIMQMPConnection *control,uint64_t address,unsigned words,const char *output){
    if(!control||!output||address<0xffffff0000000000ULL||(address&7)||!words||words>1024)return;
    NSString *path=[NSString stringWithUTF8String:output];
    if(!path.isAbsolutePath||[NSFileManager.defaultManager fileExistsAtPath:path])return;
    NSString *command=[NSString stringWithFormat:@"x /%ugx 0x%llx",words,(unsigned long long)address];
    [control execute:@"human-monitor-command" arguments:@{@"command-line":command} completion:^(id result,NSError *error){
        if(!error&&[result isKindOfClass:NSString.class])[result writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
    }];
}

// Exact 11G63 x86_64 task/thread layout, checked against its decoded kernel
// and the matching XNU source. Capture only kernel stacks, never task maps or
// user address spaces. This finite diagnostic is not an installed guest tool.
@interface VIMLionKernelTaskSnapshot : NSObject
@property VIMQMPConnection *control;
@property NSMutableArray *records;
@property NSMutableSet *seen;
@property NSString *destination;
@property uint64_t head;
- (void)read:(uint64_t)address;
@end

static NSArray *MetadataWords(NSString *text) {
    if(![text isKindOfClass:NSString.class])return nil;
    NSRegularExpression *expression=[NSRegularExpression regularExpressionWithPattern:@"0x[0-9a-fA-F]{16}" options:0 error:NULL];
    NSMutableArray *words=[NSMutableArray new];
    for(NSTextCheckingResult *match in [expression matchesInString:text options:0 range:NSMakeRange(0,text.length)]){
        unsigned long long value=0;
        if(sscanf([[text substringWithRange:match.range] UTF8String],"%llx",&value)!=1)return nil;
        [words addObject:@(value)];
    }
    return words;
}

@implementation VIMLionKernelTaskSnapshot
- (void)finish:(NSString *)error {
    if(error)[self.records addObject:@{@"Error":error}];
    [self.records writeToFile:self.destination atomically:YES];
}
- (void)read:(uint64_t)address {
    if(address==self.head){[self finish:nil];return;}
    if(address<0xffffff8000000000ULL||(address&7)||self.seen.count>=32||[self.seen containsObject:@(address)]){
        [self finish:@"Invalid, repeated or excessive thread list"];return;
    }
    [self.seen addObject:@(address)];
    // task_threads() advances through thread + 0x2d0, NOT links at zero
    // (the run/wait queue). Its queue entries point to thread bases directly.
    NSString *command=[NSString stringWithFormat:@"x /96gx 0x%llx",(unsigned long long)address];
    [self.control execute:@"human-monitor-command" arguments:@{@"command-line":command} completion:^(id result,NSError *error){
        NSArray *words=MetadataWords(result);
        if(error||words.count!=96){[self finish:error.localizedDescription?:@"Missing thread metadata"];return;}
        uint64_t next=[words[90] unsignedLongLongValue],stack=[words[14] unsignedLongLongValue];
        NSMutableDictionary *record=[@{@"Address":@(address),@"Memory":result} mutableCopy];
        [self.records addObject:record];
        if(!stack){[self read:next];return;}
        if(stack<0xffffff8000000000ULL||(stack&0xfff)){[self finish:@"Invalid kernel stack"];return;}
        record[@"StackAddress"]=@(stack+8192);
        NSString *read=[NSString stringWithFormat:@"x /1024gx 0x%llx",(unsigned long long)(stack+8192)];
        [self.control execute:@"human-monitor-command" arguments:@{@"command-line":read} completion:^(id memory,NSError *failure){
            if(failure||![memory isKindOfClass:NSString.class]){[self finish:failure.localizedDescription?:@"Missing kernel stack"];return;}
            record[@"StackMemory"]=memory;[self read:next];
        }];
    }];
}
@end

__attribute__((visibility("default")))
void VIMSnapshotKernelTaskThreads(VIMQMPConnection *control,uint64_t task,const char *output){
    if(!control||!output||task<0xffffff8000000000ULL||(task&7))return;
    NSString *path=[NSString stringWithUTF8String:output];
    if(!path.isAbsolutePath||[NSFileManager.defaultManager fileExistsAtPath:path])return;
    VIMLionKernelTaskSnapshot *snapshot=[VIMLionKernelTaskSnapshot new];
    snapshot.control=control;snapshot.destination=path;snapshot.head=task+64;
    snapshot.records=[NSMutableArray new];snapshot.seen=[NSMutableSet new];
    NSString *command=[NSString stringWithFormat:@"x /1gx 0x%llx",(unsigned long long)snapshot.head];
    [control execute:@"human-monitor-command" arguments:@{@"command-line":command} completion:^(id result,NSError *error){
        NSArray *words=MetadataWords(result);
        if(error||words.count!=1){[snapshot finish:error.localizedDescription?:@"Missing task thread queue"];return;}
        [snapshot read:[words[0] unsignedLongLongValue]];
    }];
}
