// Diagnostic-only full-pixel reduction. Keep both implementations available
// for exact-output qualification and older GPU families. Neither runs unless
// presentation fingerprinting is explicitly enabled.
#import <Foundation/Foundation.h>

// 0: original tree; 1: SIMD partials + one barrier; 2: one SIMD per tile.
static NSString *VIMDisplayFingerprintSourceWithCopy(NSUInteger reductionMode, BOOL copyImage) {
    BOOL simdReduction=reductionMode!=0, singleSIMD=reductionMode==2;
    NSString *prefix=@"#include <metal_stdlib>\nusing namespace metal;\n"
        "uint mixword(uint x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }\n"
        "kernel void fingerprint(texture2d<float,access::read> image [[texture(0)]], device uint4 *out [[buffer(0)]], constant uint4 &roi [[buffer(1)]], "
        "uint2 xy [[thread_position_in_grid]], uint2 group [[threadgroup_position_in_grid]], "
        "uint2 groups [[threadgroups_per_grid]], uint tid [[thread_index_in_threadgroup]]";
    if(copyImage)prefix=[prefix stringByAppendingString:
        @", texture2d<float,access::write> destination [[texture(1)]], constant uint2 &extent [[buffer(2)]]"];
    if(simdReduction)prefix=[prefix stringByAppendingString:
        @", uint lane [[thread_index_in_simdgroup]], uint sg [[simdgroup_index_in_threadgroup]], uint count [[simdgroups_per_threadgroup]], uint width [[threads_per_simdgroup]]"];
    NSString *start=singleSIMD ?
        @") {\nuint4 total=uint4(0,0,0xffffffffu,0); for(uint pixel=lane;pixel<256;pixel+=width){ xy=group*16+uint2(pixel&15,pixel>>4); uint4 h=uint4(0,0,0xffffffffu,0);\n" :
        @") {\nthreadgroup uint4 sums[256]; uint4 h=uint4(0,0,0xffffffffu,0);\n";
    NSString *pixels=copyImage ?
        @"if(xy.x<extent.x && xy.y<extent.y) { float4 pixel=image.read(xy); destination.write(pixel,xy); uint4 c=uint4(round(pixel*255.0f)); "
         "uint v=c.x|(c.y<<8)|(c.z<<16); uint index=xy.y*extent.x+xy.x; " :
        @"if(xy.x<image.get_width() && xy.y<image.get_height()) { uint4 c=uint4(round(image.read(xy)*255.0f)); "
         "uint v=c.x|(c.y<<8)|(c.z<<16); uint index=xy.y*image.get_width()+xy.x; ";
    pixels=[pixels stringByAppendingString:@""
        "h.xy=uint2(mixword(v^mixword(index)),mixword(v+index*0x9e3779b9u+0x85ebca6bu)); "
        "if(xy.x>=roi.x && xy.x<roi.z && xy.y>=roi.y && xy.y<roi.w) { h.w=mixword(v^mixword(index)); "
        // Snow's yellow minimize button has an orange upper rim that passed
        // the old loose red test before the actual close button. Track the
        // red button, not that shifting highlight. Diagnostic only.
        "if(c.x>180 && c.y<140 && c.z<140 && c.x>c.y+80 && c.x>c.z+80 && c.y<=c.z+24 && c.z<=c.y+24) h.z=(xy.y<<16)|xy.x; } }\n"];
    NSString *reduction=singleSIMD ?
        @"total.xy+=h.xy; total.z=min(total.z,h.z); total.w+=h.w; }\n"
        "uint4 sub=uint4(simd_sum(total.x),simd_sum(total.y),simd_min(total.z),simd_sum(total.w));\n"
        "if(!lane)out[group.y*groups.x+group.x]=sub; }\n" : simdReduction ?
        @"uint4 sub=uint4(simd_sum(h.x),simd_sum(h.y),simd_min(h.z),simd_sum(h.w));\n"
        "if(!lane)sums[sg]=sub; threadgroup_barrier(mem_flags::mem_threadgroup);\n"
        "if(!tid){ uint4 total=uint4(0,0,0xffffffffu,0); for(uint n=0;n<count;n++){total.xy+=sums[n].xy; total.z=min(total.z,sums[n].z); total.w+=sums[n].w;} out[group.y*groups.x+group.x]=total;} }\n" :
        @"sums[tid]=h; threadgroup_barrier(mem_flags::mem_threadgroup);\n"
        "for(uint n=128;n;n>>=1) { if(tid<n){sums[tid].xy+=sums[tid+n].xy; sums[tid].z=min(sums[tid].z,sums[tid+n].z); sums[tid].w+=sums[tid+n].w;} threadgroup_barrier(mem_flags::mem_threadgroup); }\n"
        "if(!tid)out[group.y*groups.x+group.x]=sums[0]; }\n";
    return [[[prefix stringByAppendingString:start] stringByAppendingString:pixels] stringByAppendingString:reduction];
}

static NSString *VIMDisplayFingerprintSource(NSUInteger reductionMode) {
    return VIMDisplayFingerprintSourceWithCopy(reductionMode, NO);
}
