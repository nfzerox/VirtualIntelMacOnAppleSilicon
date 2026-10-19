#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <objc/message.h>
#include <dispatch/dispatch.h>
#include <os/log.h>

/* Native Catalina PipelineLibrarySerializer and host differential probe
 * probes/pipeline-descriptor-wire.m: a compact property list has count:u8,
 * then (tag:u8,size:u8,value) records. String references are absolute u32
 * offsets from this payload's start. MetalSerializer supplies GPU framing.
 */
static void field(NSMutableData *data, uint8_t tag, uint32_t value) {
    uint8_t header[2] = {tag,4};
    [data appendBytes:header length:2]; [data appendBytes:&value length:4];
    ++*(uint8_t *)data.mutableBytes;
}

// Nested object references are relative to the containing object's start.
// Object/array children are four-byte aligned; strings have byte alignment.
static void children(NSMutableData *data, NSArray *fixups) {
    for (NSArray *fixup in fixups) {
        NSUInteger alignment=[fixup[2] unsignedIntegerValue];
        while(data.length % alignment) [data increaseLengthBy:1];
        uint32_t offset=(uint32_t)data.length;
        memcpy((uint8_t *)data.mutableBytes+[fixup[0] unsignedIntegerValue],&offset,4);
        [data appendData:fixup[1]];
    }
}
static void object(NSMutableData *data, NSMutableArray *fixups, uint8_t tag, NSData *child, NSUInteger alignment) {
    if(!child) return;
    NSUInteger offset=data.length+2; field(data,tag,0);
    [fixups addObject:@[@(offset),child,@(alignment)]];
}
static NSData *labelData(NSString *label) {
    if(!label)return nil;
    const char *s=label.UTF8String;
    return [NSData dataWithBytes:s length:strlen(s)+1];
}
static NSData *arrayData(NSArray *entries) {
    if(!entries.count)return nil;
    uint32_t count=(uint32_t)entries.count;
    NSMutableData *data=[NSMutableData dataWithBytes:&count length:4];
    [data increaseLengthBy:4*count];
    NSMutableArray *fixups=[NSMutableArray array];
    for(NSUInteger i=0;i<count;++i) [fixups addObject:@[@(4+4*i),entries[i],@4]];
    children(data,fixups);return data;
}
static NSData *buffers(MTLPipelineBufferDescriptorArray *descriptors) {
    NSMutableArray *entries=[NSMutableArray array];
    for(unsigned i=0;i<31;++i) {
        if(descriptors[i].mutability==MTLMutabilityDefault)continue;
        NSMutableData *d=[NSMutableData dataWithLength:1];
        field(d,0,i);field(d,1,(uint32_t)descriptors[i].mutability);[entries addObject:d];
    }
    return arrayData(entries);
}
#define VALUE(data,tag,obj,defaults,property) do { \
    if((obj).property!=(defaults).property)field(data,tag,(uint32_t)(obj).property); \
} while(0)
static NSData *attachments(MTLRenderPipelineColorAttachmentDescriptorArray *descriptors) {
    NSMutableArray *entries=[NSMutableArray array];
    MTLRenderPipelineColorAttachmentDescriptor *defaults=[MTLRenderPipelineColorAttachmentDescriptor new];
    for(unsigned i=0;i<8;++i) {
        MTLRenderPipelineColorAttachmentDescriptor *a=descriptors[i];
        NSMutableData *d=[NSMutableData dataWithLength:1];field(d,0,i);
        VALUE(d,1,a,defaults,pixelFormat);VALUE(d,2,a,defaults,blendingEnabled);
        VALUE(d,3,a,defaults,sourceRGBBlendFactor);VALUE(d,4,a,defaults,destinationRGBBlendFactor);
        VALUE(d,5,a,defaults,rgbBlendOperation);VALUE(d,6,a,defaults,sourceAlphaBlendFactor);
        VALUE(d,7,a,defaults,destinationAlphaBlendFactor);VALUE(d,8,a,defaults,alphaBlendOperation);
        VALUE(d,9,a,defaults,writeMask);
        if(*(const uint8_t *)d.bytes>1)[entries addObject:d];
    }
    [defaults release];return arrayData(entries);
}
static NSData *vertex(MTLVertexDescriptor *v) {
    if(!v)return nil;
    MTLVertexDescriptor *defaults=[MTLVertexDescriptor new];
    NSMutableArray *attrs=[NSMutableArray array], *layouts=[NSMutableArray array];
    for(unsigned i=0;i<31;++i) {
        MTLVertexAttributeDescriptor *a=v.attributes[i], *b=defaults.attributes[i];
        NSMutableData *d=[NSMutableData dataWithLength:1];field(d,0,i);
        VALUE(d,1,a,b,format);VALUE(d,2,a,b,offset);VALUE(d,3,a,b,bufferIndex);
        if(*(const uint8_t *)d.bytes>1)[attrs addObject:d];
        MTLVertexBufferLayoutDescriptor *l=v.layouts[i], *m=defaults.layouts[i];
        d=[NSMutableData dataWithLength:1];field(d,0,i);
        VALUE(d,1,l,m,stepFunction);VALUE(d,2,l,m,stepRate);VALUE(d,3,l,m,stride);
        if(*(const uint8_t *)d.bytes>1)[layouts addObject:d];
    }
    [defaults release];if(!attrs.count&&!layouts.count)return nil;
    NSMutableData *data=[NSMutableData dataWithLength:1];NSMutableArray *fixups=[NSMutableArray array];
    object(data,fixups,0,arrayData(attrs),4);object(data,fixups,1,arrayData(layouts),4);
    children(data,fixups);return data;
}
static BOOL function(NSMutableData *data,uint8_t tag,id f) {
    if(!f)return YES;
    SEL selector=sel_registerName("functionRef");
    if(![f respondsToSelector:selector])return NO;
    unsigned (*send)(id,SEL)=(void *)objc_msgSend;field(data,tag,send(f,selector));return YES;
}

dispatch_data_t PVGMojaveSerializeRender(MTLRenderPipelineDescriptor *d) {
    MTLRenderPipelineDescriptor *defaults=[MTLRenderPipelineDescriptor new];
    NSMutableData *data=[NSMutableData dataWithLength:1];NSMutableArray *fixups=[NSMutableArray array];
    object(data,fixups,0,labelData(d.label),1);object(data,fixups,3,vertex(d.vertexDescriptor),4);
    VALUE(data,4,d,defaults,sampleCount);VALUE(data,5,d,defaults,alphaToCoverageEnabled);
    VALUE(data,6,d,defaults,alphaToOneEnabled);VALUE(data,7,d,defaults,rasterizationEnabled);
    // Catalina serializer 2b542..2b57f: coverage float tag27, mask tag28.
    // Mojave has native accessors for both; preserve float bits, not a cast.
    SEL coverageSelector=sel_registerName("sampleCoverage");
    SEL maskSelector=sel_registerName("sampleMask");
    if([d respondsToSelector:coverageSelector]) {
        float (*get)(id,SEL)=(void *)objc_msgSend;
        float coverage=get(d,coverageSelector), baseline=get(defaults,coverageSelector);
        if(coverage!=baseline){uint32_t bits;memcpy(&bits,&coverage,4);field(data,27,bits);}
    }
    if([d respondsToSelector:maskSelector]) {
        NSUInteger (*get)(id,SEL)=(void *)objc_msgSend;
        NSUInteger mask=get(d,maskSelector);
        if(mask!=get(defaults,maskSelector))field(data,28,(uint32_t)mask);
    }
    object(data,fixups,8,attachments(d.colorAttachments),4);
    VALUE(data,9,d,defaults,depthAttachmentPixelFormat);VALUE(data,10,d,defaults,stencilAttachmentPixelFormat);
    VALUE(data,11,d,defaults,inputPrimitiveTopology);
    VALUE(data,12,d,defaults,tessellationPartitionMode);VALUE(data,13,d,defaults,maxTessellationFactor);
    VALUE(data,14,d,defaults,tessellationFactorScaleEnabled);VALUE(data,15,d,defaults,tessellationFactorFormat);
    VALUE(data,16,d,defaults,tessellationControlPointIndexType);VALUE(data,17,d,defaults,tessellationFactorStepFunction);
    VALUE(data,18,d,defaults,tessellationOutputWindingOrder);
    object(data,fixups,21,buffers(d.vertexBuffers),4);object(data,fixups,22,buffers(d.fragmentBuffers),4);
    [defaults release];
    if(!function(data,1,d.vertexFunction)||!function(data,2,d.fragmentFunction))return NULL;
    children(data,fixups);
    return dispatch_data_create(data.bytes,data.length,NULL,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
}

dispatch_data_t PVGMojaveSerializeCompute(MTLComputePipelineDescriptor *d) {
    MTLComputePipelineDescriptor *defaults = [MTLComputePipelineDescriptor new];
    BOOL unsupported = d.stageInputDescriptor &&
        ![d.stageInputDescriptor isEqual:defaults.stageInputDescriptor];
    unsupported |= d.buffers && ![d.buffers isEqual:defaults.buffers];
    [defaults release];
    if (unsupported) {
        os_log_error(OS_LOG_DEFAULT, "PVG Mojave: non-default compute stage-input/buffer serialization not implemented yet");
        return NULL;
    }
    NSMutableData *data = [NSMutableData dataWithLength:1];
    NSUInteger labelField = 0;
    if (d.label) { labelField=data.length+2; field(data,2,0); }
    if (d.threadGroupSizeIsMultipleOfThreadExecutionWidth) field(data,1,1);
    if (d.maxTotalThreadsPerThreadgroup) field(data,8,(uint32_t)d.maxTotalThreadsPerThreadgroup);
    if (d.computeFunction) {
        SEL selector = sel_registerName("functionRef");
        if (![d.computeFunction respondsToSelector:selector]) return NULL;
        unsigned (*send)(id,SEL)=(void *)objc_msgSend;
        field(data,0,send(d.computeFunction,selector));
    }
    if (labelField) {
        uint32_t offset=(uint32_t)data.length;
        memcpy((uint8_t *)data.mutableBytes+labelField,&offset,4);
        const char *label=d.label.UTF8String;
        [data appendBytes:label length:strlen(label)+1];
    }
    return dispatch_data_create(data.bytes,data.length,NULL,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
}
