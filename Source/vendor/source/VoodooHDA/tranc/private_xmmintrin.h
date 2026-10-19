//
//  private_xmmintrin.h
//  VoodooHDA
//
//  Created by Zenith432 on November 19th, 2017.
//

#include "License.h"

#ifndef private_xmmintrin_h
#define private_xmmintrin_h

/*
 * SSE instructions (from xmmintrin.h)
 */

typedef int __v4si __attribute__((__vector_size__(16)));
typedef float __m128 __attribute__((__vector_size__(16)));

#define _mm_load_ps(p) (*(__m128 const*) (p))
#define _mm_loadu_ps(p) ({ __m128 tmp; __asm__ volatile ("movups %1, %0" : "=x"(tmp) : "m"(*(char const*) (p))); tmp; })
#define _mm_store_ps(p, a) (*((__m128*) (p)) = (a))
#define _mm_storeu_ps(p, a) { __asm__ volatile ("movups %1, %0" : "=m"(*(char*) (p)) : "x"((a))); }
#define _mm_mul_ps(a, b) ((a) * (b))
#define _mm_add_ps(a, b) ((a) + (b))
#define _mm_max_ps(a, b) __builtin_ia32_maxps((a), (b))
#define _mm_min_ps(a, b) __builtin_ia32_minps((a), (b))

/*
 * SSE2 instructions (from emmintrin.h)
 */

typedef long long __m128i __attribute__((__vector_size__(16)));

#define _mm_setzero_si128() (__m128i){ 0LL, 0LL }
#define _mm_setr_epi32(i0, i1, i2, i3) (__m128i)(__v4si){ (i0), (i1), (i2), (i3) }
#define _mm_load_si128(p) (*(__m128i const*) (p))
#define _mm_loadu_si128(p) ({ __m128 tmp; __asm__ volatile ("movups %1, %0" : "=x"(tmp) : "m"(*(char const*) (p))); tmp; })
#define _mm_store_si128(p, b) (*((__m128i*) (p)) = (b))
#define _mm_storeu_si128(p, a) { __asm__ volatile ("movups %1, %0" : "=m"(*(char*) (p)) : "x"((a))); }
#define _mm_cvtps_epi32(a) ({ __m128 tmp; __asm__ volatile ("cvtps2dq %1, %0" : "=x"(tmp) : "xm"(a)); tmp; })
#define _mm_packs_epi32(a, b) __builtin_ia32_packssdw128((a), (b))
#define _mm_slli_epi16(a, count) (__m128i)__builtin_ia32_psllwi128((a), (count))
#define _mm_srli_epi16(a, count) (__m128i)__builtin_ia32_psrlwi128((a), (count))
#define _mm_or_si128(a, b) ((a) | (b))
#define _mm_and_si128(a, b) ((a) & (b))
#define _mm_cvtepi32_ps(a) ({ __m128 tmp; __asm__ volatile ("cvtdq2ps %1, %0" : "=x"(tmp) : "xm"(a)); tmp; })
#define _mm_unpacklo_epi16(a, b) ({ __m128i tmp = (a); __asm__ volatile ("punpcklwd %1, %0" : "+x"(tmp) : "xm"((b))); tmp; })
#define _mm_unpackhi_epi16(a, b) ({ __m128i tmp = (a); __asm__ volatile ("punpckhwd %1, %0" : "+x"(tmp) : "xm"((b))); tmp; })
#define _mm_slli_si128(a, imm) ({ __m128i tmp = (a); __asm__ volatile ("pslldq %1, %0" : "+x"(tmp) : "N"((imm))); tmp; })
#define _mm_srli_si128(a, imm) ({ __m128i tmp = (a); __asm__ volatile ("psrldq %1, %0" : "+x"(tmp) : "N"((imm))); tmp; })
#define _mm_shufflelo_epi16(a, imm) ({ __m128i tmp; __asm__ volatile ("pshuflw %2, %1, %0" : "=x"(tmp) : "xm"((a)), "N"((imm))); tmp; })
#define _mm_shufflehi_epi16(a, imm) ({ __m128i tmp; __asm__ volatile ("pshufhw %2, %1, %0" : "=x"(tmp) : "xm"((a)), "N"((imm))); tmp; })

#endif /* private_xmmintrin_h */
