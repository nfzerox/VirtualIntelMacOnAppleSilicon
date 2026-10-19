#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
extern dispatch_data_t PVGMojaveSerializeRender(MTLRenderPipelineDescriptor *);
int main(void) {
    @autoreleasepool {
        MTLRenderPipelineDescriptor *d=[MTLRenderPipelineDescriptor new];
        ((void(*)(id,SEL,float))objc_msgSend)(d,sel_registerName("setSampleCoverage:"),0.5f);
        ((void(*)(id,SEL,NSUInteger))objc_msgSend)(d,sel_registerName("setSampleMask:"),5);
        dispatch_data_t result=PVGMojaveSerializeRender(d);
        if(!result)return 2;
        const void *bytes=NULL;size_t length=0;
        dispatch_data_t mapped=dispatch_data_create_map(result,&bytes,&length);
        const unsigned char expected[]={2,27,4,0,0,0,0x3f,28,4,5,0,0,0};
        BOOL ok=length==sizeof(expected)&&!memcmp(bytes,expected,sizeof(expected));
        printf("sample coverage/mask wire: %s (%zu bytes)\n",ok?"PASS":"FAIL",length);
        dispatch_release(mapped);dispatch_release(result);[d release];return ok?0:1;
    }
}
