// Synthetic local context, not a captured CABAC/decoder state. All fragment-written registers and final ADD flags checked.
#include "flatlibc.h"
extern void vcp_cl_initialize(void);
extern void vcp_cl_case(const uint64_t in[10], uint64_t out[8]);
extern uint64_t vcp_cl_batch(uint64_t iterations, uint64_t seed);
#define POW4(n) (1ULL<<(n)),(1ULL<<((n)+1)),(1ULL<<((n)+2)),(1ULL<<((n)+3))
static const uint64_t powers[64] = {
    POW4(0),POW4(4),POW4(8),POW4(12),POW4(16),POW4(20),POW4(24),POW4(28),
    POW4(32),POW4(36),POW4(40),POW4(44),POW4(48),POW4(52),POW4(56),POW4(60)
};

static void oracle(const uint64_t in[10], uint64_t out[8]) {
    unsigned shift = in[3] & 63, byte = (in[8] * 73 + 19) & 255;
    uint64_t a = in[7], b = in[3], sum = a + b;
    unsigned parity = 0;
    for (unsigned i = 0; i < 8; i++) parity ^= (sum >> i) & 1;
    // Multiplication by a precomputed power, avoiding the tested variable-SHL instruction in this oracle.
    out[0] = in[0] * powers[shift];
    out[1] = in[1] * powers[shift];
    out[2] = in[2] - 1;
    out[3] = in[4] ^ in[5];
    out[4] = (in[6] & ~255ULL) | byte;
    out[5] = sum;
    out[6] = (sum < a ? 1 : 0) | (parity ? 0 : 4) | ((a ^ b ^ sum) & 16) |
             (!sum ? 64 : 0) | (sum >> 63 ? 128 : 0) | ((~(a ^ b) & (a ^ sum)) >> 63 ? 2048 : 0);
    out[7] = byte;
}
static uint64_t micros(void) {
    struct timeval t;
    if (gettimeofday(&t, 0)) return 0;
    return (uint64_t)t.tv_sec * 1000000 + t.tv_usec;
}
int flat_main(unsigned long count, unsigned long seed) {
    if (!count) count = 100000;
    if (count > 10000000) return 64;
    vcp_cl_initialize();
    static const uint64_t values[] = {0,1,~0ULL,0x8000000000000000ULL,0x7fffffffffffffffULL,
                                     0x0123456789abcdefULL,0xfedcba9876543210ULL,0xffffffff00000000ULL};
    unsigned checks = 0;
    for (unsigned cl = 0; cl < 256; cl++)
        for (unsigned v = 0; v < 8; v++)
            for (unsigned high = 0; high < 2; high++) {
                uint64_t in[] = {values[v],values[(v+1)&7],values[(v+2)&7],
                                 (high ? 0xffffffffffffff00ULL : 0) | cl,values[(v+3)&7],values[(v+4)&7],
                                 0x923456789abcdef0ULL ^ cl,values[(v+5)&7],(cl+v)&255,(cl+v)&31};
                uint64_t actual[8], expected[8];
                vcp_cl_case(in, actual);
                oracle(in, expected);
                checks++;
                if (memcmp(actual, expected, sizeof actual)) {
                    printf("FAIL vcphevc-cl cl=%u vector=%u high=%u\n",cl,v,high);
                    return 1;
                }
            }
    uint64_t begin = micros(), hash = vcp_cl_batch(count, seed), end = micros();
    uint64_t expected_hash = 0xcbf29ce484222325ULL;
    for (unsigned long i = 0; i < count; i++) {
        uint64_t value = seed+i, in[] = {value*5+0x13579,value*3-0x2468,value,value&255,~value,value*9,
                                       0x9234567800000000ULL^value,value*3-1,value&255,value&31}, out[8];
        oracle(in,out);
        for (unsigned j = 0; j < 8; j++) expected_hash = (expected_hash ^ out[j]) * 0x1000193;
    }
    if (!begin || end < begin || hash != expected_hash) {
        printf("FAIL vcphevc-cl hash=%016lx expected=%016lx\n",(unsigned long)hash,(unsigned long)expected_hash);
        return 1;
    }
    printf("PASS vcphevc-cl cases=%u iterations=%lu elapsed_us=%lu hash=%016lx expected=%016lx\n",
           checks,count,(unsigned long)(end-begin),(unsigned long)hash,(unsigned long)expected_hash);
    return 0;
}
