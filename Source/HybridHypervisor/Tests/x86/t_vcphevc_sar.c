// Local dependency fixture, not captured decoder state or a reconstructed HEVC function.
#include "flatlibc.h"

extern void vcp_case(const uint64_t in[4], uint64_t out[8]);
extern uint64_t vcp_batch(uint64_t iterations, uint64_t seed);
extern const unsigned char vcp_case_body[], vcp_case_end[], vcp_batch_body[], vcp_batch_end[];
static const unsigned char body[25] = {0x66,0xc1,0xfa,0x09,0x44,0x8d,0x04,0x81,0x4c,0x8b,0x5f,0x20,0x48,
                                     0x8b,0x47,0x28,0x48,0x89,0x45,0xa8,0x0f,0xbe,0xc2,0x85,0xf6};

// Integer oracle independent of x86 shifts, sign-extension instructions and processor flags.
static void oracle(const uint64_t in[4], uint64_t out[8]) {
    int n = (int)(in[0] & 65535);
    if (n >= 32768) n -= 65536;
    int shifted = n >= 0 ? n / 512 : -((-n + 511) / 512);
    int byte = shifted & 255;
    if (byte >= 128) byte -= 256;
    unsigned value = (unsigned)in[3], low = value & 255, parity = 0;
    for (unsigned i = 0; i < 8; i++) parity ^= (low >> i) & 1;
    out[0] = (in[0] & ~65535ULL) | ((unsigned)shifted & 65535);
    out[1] = in[1];
    out[2] = (uint32_t)(in[1] + in[2] * 4);
    out[3] = 0x5a5a1234abcdef01ULL;
    out[4] = (uint32_t)byte;
    out[5] = in[3];
    out[6] = (value == 0 ? 64 : 0) | (value & 0x80000000U ? 128 : 0) | (parity ? 0 : 4);
    out[7] = 0xa5a5fedc98765432ULL;
}

static uint64_t hash_fields(uint64_t hash, const uint64_t fields[8]) {
    for (unsigned i = 0; i < 8; i++) hash = (hash ^ fields[i]) * 0x1000193;
    return hash;
}
static uint64_t micros(void) {
    struct timeval t;
    if (gettimeofday(&t, 0)) return 0;
    return (uint64_t)t.tv_sec * 1000000 + t.tv_usec;
}

int flat_main(unsigned long count, unsigned long seed) {
    if (!count) count = 100000;
    if (count > 10000000) return 64;
    if (vcp_case_end-vcp_case_body != 25 || vcp_batch_end-vcp_batch_body != 25 ||
        memcmp(vcp_case_body, body, 25) || memcmp(vcp_batch_body, body, 25)) return 2;
    static const uint64_t values[] = {0, 1, 0x80000000, 0xffffffff, 0x1234567800000000, 0xff, 0x80, 0x7f};
    for (unsigned i = 0; i < 65536; i++) {
        uint64_t in[] = {0x9234567800000000ULL | i, 0xfedcba9876543210ULL ^ i,
                         0x8765432100000000ULL + (uint64_t)i * 0x100000001ULL, values[i & 7]};
        uint64_t actual[8], expected[8];
        vcp_case(in, actual);
        oracle(in, expected);
        if (memcmp(actual, expected, sizeof actual)) {
            printf("FAIL vcphevc-sar input=%u\n", i);
            return 1;
        }
    }
    uint64_t begin = micros(), hash = vcp_batch(count, seed), end = micros();
    uint64_t expected_hash = 0xcbf29ce484222325ULL;
    for (unsigned long i = 0; i < count; i++) {
        uint64_t value = seed + i;
        uint64_t in[] = {value, ~value, value * 5 + 0x13579, (uint32_t)(value * 3)}, out[8];
        oracle(in, out);
        expected_hash = hash_fields(expected_hash, out);
    }
    if (!begin || end < begin || hash != expected_hash) {
        printf("FAIL vcphevc-sar hash=%016lx expected=%016lx\n", (unsigned long)hash, (unsigned long)expected_hash);
        return 1;
    }
    printf("PASS vcphevc-sar cases=65536 iterations=%lu elapsed_us=%lu hash=%016lx expected=%016lx\n",
           count, (unsigned long)(end-begin), (unsigned long)hash, (unsigned long)expected_hash);
    return 0;
}
