#include <metal_stdlib>
using namespace metal;
kernel void test(device uint *p [[buffer(0)]], uint i [[thread_position_in_grid]]) {
    p[i]=i*3+7;
}
