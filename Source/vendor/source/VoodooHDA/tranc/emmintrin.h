/* APPLE LOCAL file mainline 2005-06-30 Radar 4131077 */
/* Copyright (C) 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, if you include this header file into source
   files compiled by GCC, this header file does not by itself cause
   the resulting executable to be covered by the GNU General Public
   License.  This exception does not however invalidate any other
   reasons why the executable file might be covered by the GNU General
   Public License.  */

/* Implemented from the specification included in the Intel C++ Compiler
   User Guide and Reference, version 9.0.  */

#ifndef _EMMINTRIN_H_INCLUDED
#define _EMMINTRIN_H_INCLUDED

#ifdef __clang__

#ifndef __EMMINTRIN_H
#define __EMMINTRIN_H

#ifndef __SSE2__
#error "SSE2 instruction set not enabled"
#else

#include "xmmintrin.h"

typedef double __m128d __attribute__((__vector_size__(16)));
typedef long long __m128i __attribute__((__vector_size__(16)));

/* Type defines.  */
typedef double __v2df __attribute__ ((__vector_size__ (16)));
typedef long long __v2di __attribute__ ((__vector_size__ (16)));
typedef short __v8hi __attribute__((__vector_size__(16)));
typedef char __v16qi __attribute__((__vector_size__(16)));

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_add_sd(__m128d __a, __m128d __b)
{
    __a[0] += __b[0];
    return __a;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_add_pd(__m128d __a, __m128d __b)
{
    return __a + __b;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_sub_sd(__m128d __a, __m128d __b)
{
    __a[0] -= __b[0];
    return __a;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_sub_pd(__m128d __a, __m128d __b)
{
    return __a - __b;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_mul_sd(__m128d __a, __m128d __b)
{
    __a[0] *= __b[0];
    return __a;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_mul_pd(__m128d __a, __m128d __b)
{
    return __a * __b;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_div_sd(__m128d __a, __m128d __b)
{
    __a[0] /= __b[0];
    return __a;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_div_pd(__m128d __a, __m128d __b)
{
    return __a / __b;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_sqrt_sd(__m128d __a, __m128d __b)
{
    __m128d __c = __builtin_ia32_sqrtsd(__b);
    return (__m128d) { __c[0], __a[1] };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_sqrt_pd(__m128d __a)
{
    return __builtin_ia32_sqrtpd(__a);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_min_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_minsd(__a, __b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_min_pd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_minpd(__a, __b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_max_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_maxsd(__a, __b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_max_pd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_maxpd(__a, __b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_and_pd(__m128d __a, __m128d __b)
{
    return (__m128d)((__v4si)__a & (__v4si)__b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_andnot_pd(__m128d __a, __m128d __b)
{
    return (__m128d)(~(__v4si)__a & (__v4si)__b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_or_pd(__m128d __a, __m128d __b)
{
    return (__m128d)((__v4si)__a | (__v4si)__b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_xor_pd(__m128d __a, __m128d __b)
{
    return (__m128d)((__v4si)__a ^ (__v4si)__b);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpeq_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 0);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmplt_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 1);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmple_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 2);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpgt_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__b, __a, 1);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpge_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__b, __a, 2);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpord_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 7);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpunord_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 3);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpneq_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 4);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpnlt_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 5);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpnle_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__a, __b, 6);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpngt_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__b, __a, 5);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpnge_pd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmppd(__b, __a, 6);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpeq_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 0);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmplt_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 1);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmple_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 2);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpgt_sd(__m128d __a, __m128d __b)
{
    __m128d __c = __builtin_ia32_cmpsd(__b, __a, 1);
    return (__m128d) { __c[0], __a[1] };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpge_sd(__m128d __a, __m128d __b)
{
    __m128d __c = __builtin_ia32_cmpsd(__b, __a, 2);
    return (__m128d) { __c[0], __a[1] };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpord_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 7);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpunord_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 3);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpneq_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 4);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpnlt_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 5);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpnle_sd(__m128d __a, __m128d __b)
{
    return (__m128d)__builtin_ia32_cmpsd(__a, __b, 6);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpngt_sd(__m128d __a, __m128d __b)
{
    __m128d __c = __builtin_ia32_cmpsd(__b, __a, 5);
    return (__m128d) { __c[0], __a[1] };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cmpnge_sd(__m128d __a, __m128d __b)
{
    __m128d __c = __builtin_ia32_cmpsd(__b, __a, 6);
    return (__m128d) { __c[0], __a[1] };
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_comieq_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_comisdeq(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_comilt_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_comisdlt(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_comile_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_comisdle(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_comigt_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_comisdgt(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_comige_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_comisdge(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_comineq_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_comisdneq(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_ucomieq_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_ucomisdeq(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_ucomilt_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_ucomisdlt(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_ucomile_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_ucomisdle(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_ucomigt_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_ucomisdgt(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_ucomige_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_ucomisdge(__a, __b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_ucomineq_sd(__m128d __a, __m128d __b)
{
    return __builtin_ia32_ucomisdneq(__a, __b);
}

static __inline__ __m128 __attribute__((__always_inline__, __nodebug__))
_mm_cvtpd_ps(__m128d __a)
{
    return __builtin_ia32_cvtpd2ps(__a);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cvtps_pd(__m128 __a)
{
    return __builtin_ia32_cvtps2pd(__a);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cvtepi32_pd(__m128i __a)
{
    return __builtin_ia32_cvtdq2pd((__v4si)__a);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cvtpd_epi32(__m128d __a)
{
    return __builtin_ia32_cvtpd2dq(__a);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_cvtsd_si32(__m128d __a)
{
    return __builtin_ia32_cvtsd2si(__a);
}

static __inline__ __m128 __attribute__((__always_inline__, __nodebug__))
_mm_cvtsd_ss(__m128 __a, __m128d __b)
{
    __a[0] = __b[0];
    return __a;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cvtsi32_sd(__m128d __a, int __b)
{
    __a[0] = __b;
    return __a;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cvtss_sd(__m128d __a, __m128 __b)
{
    __a[0] = __b[0];
    return __a;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cvttpd_epi32(__m128d __a)
{
    return (__m128i)__builtin_ia32_cvttpd2dq(__a);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_cvttsd_si32(__m128d __a)
{
    return __a[0];
}

static __inline__ __m64 __attribute__((__always_inline__, __nodebug__))
_mm_cvtpd_pi32(__m128d __a)
{
    return (__m64)__builtin_ia32_cvtpd2pi(__a);
}

static __inline__ __m64 __attribute__((__always_inline__, __nodebug__))
_mm_cvttpd_pi32(__m128d __a)
{
    return (__m64)__builtin_ia32_cvttpd2pi(__a);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cvtpi32_pd(__m64 __a)
{
    return __builtin_ia32_cvtpi2pd((__v2si)__a);
}

static __inline__ double __attribute__((__always_inline__, __nodebug__))
_mm_cvtsd_f64(__m128d __a)
{
    return __a[0];
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_load_pd(double const *__dp)
{
    return *(__m128d*)__dp;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_load1_pd(double const *__dp)
{
    struct __mm_load1_pd_struct {
        double __u;
    } __attribute__((__packed__, __may_alias__));
    double __u = ((struct __mm_load1_pd_struct*)__dp)->__u;
    return (__m128d){ __u, __u };
}

#define        _mm_load_pd1(dp)        _mm_load1_pd(dp)

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_loadr_pd(double const *__dp)
{
    __m128d __u = *(__m128d*)__dp;
    return __builtin_shufflevector(__u, __u, 1, 0);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_loadu_pd(double const *__dp)
{
    struct __loadu_pd {
        __m128d __v;
    } __attribute__((packed, may_alias));
    return ((struct __loadu_pd*)__dp)->__v;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_load_sd(double const *__dp)
{
    struct __mm_load_sd_struct {
        double __u;
    } __attribute__((__packed__, __may_alias__));
    double __u = ((struct __mm_load_sd_struct*)__dp)->__u;
    return (__m128d){ __u, 0 };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_loadh_pd(__m128d __a, double const *__dp)
{
    struct __mm_loadh_pd_struct {
        double __u;
    } __attribute__((__packed__, __may_alias__));
    double __u = ((struct __mm_loadh_pd_struct*)__dp)->__u;
    return (__m128d){ __a[0], __u };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_loadl_pd(__m128d __a, double const *__dp)
{
    struct __mm_loadl_pd_struct {
        double __u;
    } __attribute__((__packed__, __may_alias__));
    double __u = ((struct __mm_loadl_pd_struct*)__dp)->__u;
    return (__m128d){ __u, __a[1] };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_set_sd(double __w)
{
    return (__m128d){ __w, 0 };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_set1_pd(double __w)
{
    return (__m128d){ __w, __w };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_set_pd(double __w, double __x)
{
    return (__m128d){ __x, __w };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_setr_pd(double __w, double __x)
{
    return (__m128d){ __w, __x };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_setzero_pd(void)
{
    return (__m128d){ 0, 0 };
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_move_sd(__m128d __a, __m128d __b)
{
    return (__m128d){ __b[0], __a[1] };
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_store_sd(double *__dp, __m128d __a)
{
    struct __mm_store_sd_struct {
        double __u;
    } __attribute__((__packed__, __may_alias__));
    ((struct __mm_store_sd_struct*)__dp)->__u = __a[0];
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_store1_pd(double *__dp, __m128d __a)
{
    struct __mm_store1_pd_struct {
        double __u[2];
    } __attribute__((__packed__, __may_alias__));
    ((struct __mm_store1_pd_struct*)__dp)->__u[0] = __a[0];
    ((struct __mm_store1_pd_struct*)__dp)->__u[1] = __a[0];
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_store_pd(double *__dp, __m128d __a)
{
    *(__m128d *)__dp = __a;
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_storeu_pd(double *__dp, __m128d __a)
{
    __builtin_ia32_storeupd(__dp, __a);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_storer_pd(double *__dp, __m128d __a)
{
    __a = __builtin_shufflevector(__a, __a, 1, 0);
    *(__m128d *)__dp = __a;
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_storeh_pd(double *__dp, __m128d __a)
{
    struct __mm_storeh_pd_struct {
        double __u;
    } __attribute__((__packed__, __may_alias__));
    ((struct __mm_storeh_pd_struct*)__dp)->__u = __a[1];
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_storel_pd(double *__dp, __m128d __a)
{
    struct __mm_storeh_pd_struct {
        double __u;
    } __attribute__((__packed__, __may_alias__));
    ((struct __mm_storeh_pd_struct*)__dp)->__u = __a[0];
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_add_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)((__v16qi)__a + (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_add_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)((__v8hi)__a + (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_add_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)((__v4si)__a + (__v4si)__b);
}

static __inline__ __m64 __attribute__((__always_inline__, __nodebug__))
_mm_add_si64(__m64 __a, __m64 __b)
{
    return __a + __b;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_add_epi64(__m128i __a, __m128i __b)
{
    return __a + __b;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_adds_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_paddsb128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_adds_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_paddsw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_adds_epu8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_paddusb128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_adds_epu16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_paddusw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_avg_epu8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pavgb128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_avg_epu16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pavgw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_madd_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pmaddwd128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_max_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pmaxsw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_max_epu8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pmaxub128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_min_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pminsw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_min_epu8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pminub128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_mulhi_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pmulhw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_mulhi_epu16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pmulhuw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_mullo_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)((__v8hi)__a * (__v8hi)__b);
}

static __inline__ __m64 __attribute__((__always_inline__, __nodebug__))
_mm_mul_su32(__m64 __a, __m64 __b)
{
    return __builtin_ia32_pmuludq((__v2si)__a, (__v2si)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_mul_epu32(__m128i __a, __m128i __b)
{
    return __builtin_ia32_pmuludq128((__v4si)__a, (__v4si)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sad_epu8(__m128i __a, __m128i __b)
{
    return __builtin_ia32_psadbw128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sub_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)((__v16qi)__a - (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sub_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)((__v8hi)__a - (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sub_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)((__v4si)__a - (__v4si)__b);
}

static __inline__ __m64 __attribute__((__always_inline__, __nodebug__))
_mm_sub_si64(__m64 __a, __m64 __b)
{
    return __a - __b;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sub_epi64(__m128i __a, __m128i __b)
{
    return __a - __b;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_subs_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_psubsb128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_subs_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_psubsw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_subs_epu8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_psubusb128((__v16qi)__a, (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_subs_epu16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_psubusw128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_and_si128(__m128i __a, __m128i __b)
{
    return __a & __b;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_andnot_si128(__m128i __a, __m128i __b)
{
    return ~__a & __b;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_or_si128(__m128i __a, __m128i __b)
{
    return __a | __b;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_xor_si128(__m128i __a, __m128i __b)
{
    return __a ^ __b;
}

#define _mm_slli_si128(a, count) __extension__ ({ \
__m128i __a = (a); \
(__m128i)__builtin_ia32_pslldqi128(__a, (count)*8); })

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_slli_epi16(__m128i __a, int __count)
{
    return (__m128i)__builtin_ia32_psllwi128((__v8hi)__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sll_epi16(__m128i __a, __m128i __count)
{
    return (__m128i)__builtin_ia32_psllw128((__v8hi)__a, (__v8hi)__count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_slli_epi32(__m128i __a, int __count)
{
    return (__m128i)__builtin_ia32_pslldi128((__v4si)__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sll_epi32(__m128i __a, __m128i __count)
{
    return (__m128i)__builtin_ia32_pslld128((__v4si)__a, (__v4si)__count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_slli_epi64(__m128i __a, int __count)
{
    return __builtin_ia32_psllqi128(__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sll_epi64(__m128i __a, __m128i __count)
{
    return __builtin_ia32_psllq128(__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srai_epi16(__m128i __a, int __count)
{
    return (__m128i)__builtin_ia32_psrawi128((__v8hi)__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sra_epi16(__m128i __a, __m128i __count)
{
    return (__m128i)__builtin_ia32_psraw128((__v8hi)__a, (__v8hi)__count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srai_epi32(__m128i __a, int __count)
{
    return (__m128i)__builtin_ia32_psradi128((__v4si)__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_sra_epi32(__m128i __a, __m128i __count)
{
    return (__m128i)__builtin_ia32_psrad128((__v4si)__a, (__v4si)__count);
}


#define _mm_srli_si128(a, count) __extension__ ({ \
__m128i __a = (a); \
(__m128i)__builtin_ia32_psrldqi128(__a, (count)*8); })

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srli_epi16(__m128i __a, int __count)
{
    return (__m128i)__builtin_ia32_psrlwi128((__v8hi)__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srl_epi16(__m128i __a, __m128i __count)
{
    return (__m128i)__builtin_ia32_psrlw128((__v8hi)__a, (__v8hi)__count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srli_epi32(__m128i __a, int __count)
{
    return (__m128i)__builtin_ia32_psrldi128((__v4si)__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srl_epi32(__m128i __a, __m128i __count)
{
    return (__m128i)__builtin_ia32_psrld128((__v4si)__a, (__v4si)__count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srli_epi64(__m128i __a, int __count)
{
    return __builtin_ia32_psrlqi128(__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_srl_epi64(__m128i __a, __m128i __count)
{
    return __builtin_ia32_psrlq128(__a, __count);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmpeq_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)((__v16qi)__a == (__v16qi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmpeq_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)((__v8hi)__a == (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmpeq_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)((__v4si)__a == (__v4si)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmpgt_epi8(__m128i __a, __m128i __b)
{
    /* This function always performs a signed comparison, but __v16qi is a char
     which may be signed or unsigned. */
    typedef signed char __v16qs __attribute__((__vector_size__(16)));
    return (__m128i)((__v16qs)__a > (__v16qs)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmpgt_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)((__v8hi)__a > (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmpgt_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)((__v4si)__a > (__v4si)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmplt_epi8(__m128i __a, __m128i __b)
{
    return _mm_cmpgt_epi8(__b, __a);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmplt_epi16(__m128i __a, __m128i __b)
{
    return _mm_cmpgt_epi16(__b, __a);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cmplt_epi32(__m128i __a, __m128i __b)
{
    return _mm_cmpgt_epi32(__b, __a);
}

#ifdef __x86_64__
static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_cvtsi64_sd(__m128d __a, long long __b)
{
    __a[0] = __b;
    return __a;
}

static __inline__ long long __attribute__((__always_inline__, __nodebug__))
_mm_cvtsd_si64(__m128d __a)
{
    return __builtin_ia32_cvtsd2si64(__a);
}

static __inline__ long long __attribute__((__always_inline__, __nodebug__))
_mm_cvttsd_si64(__m128d __a)
{
    return __a[0];
}
#endif

static __inline__ __m128 __attribute__((__always_inline__, __nodebug__))
_mm_cvtepi32_ps(__m128i __a)
{
    return __builtin_ia32_cvtdq2ps((__v4si)__a);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cvtps_epi32(__m128 __a)
{
    return (__m128i)__builtin_ia32_cvtps2dq(__a);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cvttps_epi32(__m128 __a)
{
    return (__m128i)__builtin_ia32_cvttps2dq(__a);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cvtsi32_si128(int __a)
{
    return (__m128i)(__v4si){ __a, 0, 0, 0 };
}

#ifdef __x86_64__
static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_cvtsi64_si128(long long __a)
{
    return (__m128i){ __a, 0 };
}
#endif

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_cvtsi128_si32(__m128i __a)
{
    __v4si __b = (__v4si)__a;
    return __b[0];
}

#ifdef __x86_64__
static __inline__ long long __attribute__((__always_inline__, __nodebug__))
_mm_cvtsi128_si64(__m128i __a)
{
    return __a[0];
}
#endif

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_load_si128(__m128i const *__p)
{
    return *__p;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_loadu_si128(__m128i const *__p)
{
    struct __loadu_si128 {
        __m128i __v;
    } __attribute__((packed, may_alias));
    return ((struct __loadu_si128*)__p)->__v;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_loadl_epi64(__m128i const *__p)
{
    struct __mm_loadl_epi64_struct {
        long long __u;
    } __attribute__((__packed__, __may_alias__));
    return (__m128i) { ((struct __mm_loadl_epi64_struct*)__p)->__u, 0};
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set_epi64x(long long q1, long long q0)
{
    return (__m128i){ q0, q1 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set_epi64(__m64 q1, __m64 q0)
{
    return (__m128i){ (long long)q0, (long long)q1 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set_epi32(int i3, int i2, int i1, int i0)
{
    return (__m128i)(__v4si){ i0, i1, i2, i3};
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set_epi16(short w7, short w6, short w5, short w4, short w3, short w2, short w1, short w0)
{
    return (__m128i)(__v8hi){ w0, w1, w2, w3, w4, w5, w6, w7 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set_epi8(char b15, char b14, char b13, char b12, char b11, char b10, char b9, char b8, char b7, char b6, char b5, char b4, char b3, char b2, char b1, char b0)
{
    return (__m128i)(__v16qi){ b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set1_epi64x(long long __q)
{
    return (__m128i){ __q, __q };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set1_epi64(__m64 __q)
{
    return (__m128i){ (long long)__q, (long long)__q };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set1_epi32(int __i)
{
    return (__m128i)(__v4si){ __i, __i, __i, __i };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set1_epi16(short __w)
{
    return (__m128i)(__v8hi){ __w, __w, __w, __w, __w, __w, __w, __w };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_set1_epi8(char __b)
{
    return (__m128i)(__v16qi){ __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_setr_epi64(__m64 q0, __m64 q1)
{
    return (__m128i){ (long long)q0, (long long)q1 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_setr_epi32(int i0, int i1, int i2, int i3)
{
    return (__m128i)(__v4si){ i0, i1, i2, i3};
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_setr_epi16(short w0, short w1, short w2, short w3, short w4, short w5, short w6, short w7)
{
    return (__m128i)(__v8hi){ w0, w1, w2, w3, w4, w5, w6, w7 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_setr_epi8(char b0, char b1, char b2, char b3, char b4, char b5, char b6, char b7, char b8, char b9, char b10, char b11, char b12, char b13, char b14, char b15)
{
    return (__m128i)(__v16qi){ b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_setzero_si128(void)
{
    return (__m128i){ 0LL, 0LL };
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_store_si128(__m128i *__p, __m128i __b)
{
    *__p = __b;
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_storeu_si128(__m128i *__p, __m128i __b)
{
    __builtin_ia32_storedqu((char *)__p, (__v16qi)__b);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_maskmoveu_si128(__m128i __d, __m128i __n, char *__p)
{
    __builtin_ia32_maskmovdqu((__v16qi)__d, (__v16qi)__n, __p);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_storel_epi64(__m128i *__p, __m128i __a)
{
    struct __mm_storel_epi64_struct {
        long long __u;
    } __attribute__((__packed__, __may_alias__));
    ((struct __mm_storel_epi64_struct*)__p)->__u = __a[0];
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_stream_pd(double *__p, __m128d __a)
{
    __builtin_ia32_movntpd(__p, __a);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_stream_si128(__m128i *__p, __m128i __a)
{
    __builtin_ia32_movntdq(__p, __a);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_stream_si32(int *__p, int __a)
{
    __builtin_ia32_movnti(__p, __a);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_clflush(void const *__p)
{
    __builtin_ia32_clflush(__p);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_lfence(void)
{
    __builtin_ia32_lfence();
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_mfence(void)
{
    __builtin_ia32_mfence();
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_packs_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_packsswb128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_packs_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_packssdw128((__v4si)__a, (__v4si)__b);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_packus_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_packuswb128((__v8hi)__a, (__v8hi)__b);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_extract_epi16(__m128i __a, int __imm)
{
    __v8hi __b = (__v8hi)__a;
    return (unsigned short)__b[__imm];
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_insert_epi16(__m128i __a, int __b, int __imm)
{
    __v8hi __c = (__v8hi)__a;
    __c[__imm & 7] = __b;
    return (__m128i)__c;
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_movemask_epi8(__m128i __a)
{
    return __builtin_ia32_pmovmskb128((__v16qi)__a);
}

#define _mm_shuffle_epi32(a, imm) __extension__ ({ \
__m128i __a = (a); \
(__m128i)__builtin_shufflevector((__v4si)__a, (__v4si) _mm_set1_epi32(0), \
(imm) & 0x3, ((imm) & 0xc) >> 2, \
((imm) & 0x30) >> 4, ((imm) & 0xc0) >> 6); })

#define _mm_shufflelo_epi16(a, imm) __extension__ ({ \
__m128i __a = (a); \
(__m128i)__builtin_shufflevector((__v8hi)__a, (__v8hi) _mm_set1_epi16(0), \
(imm) & 0x3, ((imm) & 0xc) >> 2, \
((imm) & 0x30) >> 4, ((imm) & 0xc0) >> 6, \
4, 5, 6, 7); })

#define _mm_shufflehi_epi16(a, imm) __extension__ ({ \
__m128i __a = (a); \
(__m128i)__builtin_shufflevector((__v8hi)__a, (__v8hi) _mm_set1_epi16(0), \
0, 1, 2, 3, \
4 + (((imm) & 0x03) >> 0), \
4 + (((imm) & 0x0c) >> 2), \
4 + (((imm) & 0x30) >> 4), \
4 + (((imm) & 0xc0) >> 6)); })

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpackhi_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector((__v16qi)__a, (__v16qi)__b, 8, 16+8, 9, 16+9, 10, 16+10, 11, 16+11, 12, 16+12, 13, 16+13, 14, 16+14, 15, 16+15);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpackhi_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector((__v8hi)__a, (__v8hi)__b, 4, 8+4, 5, 8+5, 6, 8+6, 7, 8+7);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpackhi_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector((__v4si)__a, (__v4si)__b, 2, 4+2, 3, 4+3);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpackhi_epi64(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector(__a, __b, 1, 2+1);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpacklo_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector((__v16qi)__a, (__v16qi)__b, 0, 16+0, 1, 16+1, 2, 16+2, 3, 16+3, 4, 16+4, 5, 16+5, 6, 16+6, 7, 16+7);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpacklo_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector((__v8hi)__a, (__v8hi)__b, 0, 8+0, 1, 8+1, 2, 8+2, 3, 8+3);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpacklo_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector((__v4si)__a, (__v4si)__b, 0, 4+0, 1, 4+1);
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_unpacklo_epi64(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_shufflevector(__a, __b, 0, 2+0);
}

static __inline__ __m64 __attribute__((__always_inline__, __nodebug__))
_mm_movepi64_pi64(__m128i __a)
{
    return (__m64)__a[0];
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_movpi64_pi64(__m64 __a)
{
    return (__m128i){ (long long)__a, 0 };
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_move_epi64(__m128i __a)
{
    return __builtin_shufflevector(__a, (__m128i){ 0 }, 0, 2);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_unpackhi_pd(__m128d __a, __m128d __b)
{
    return __builtin_shufflevector(__a, __b, 1, 2+1);
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_unpacklo_pd(__m128d __a, __m128d __b)
{
    return __builtin_shufflevector(__a, __b, 0, 2+0);
}

static __inline__ int __attribute__((__always_inline__, __nodebug__))
_mm_movemask_pd(__m128d __a)
{
    return __builtin_ia32_movmskpd(__a);
}

#define _mm_shuffle_pd(a, b, i) __extension__ ({ \
__m128d __a = (a); \
__m128d __b = (b); \
__builtin_shufflevector(__a, __b, (i) & 1, (((i) & 2) >> 1) + 2); })

static __inline__ __m128 __attribute__((__always_inline__, __nodebug__))
_mm_castpd_ps(__m128d __in)
{
    return (__m128)__in;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_castpd_si128(__m128d __in)
{
    return (__m128i)__in;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_castps_pd(__m128 __in)
{
    return (__m128d)__in;
}

static __inline__ __m128i __attribute__((__always_inline__, __nodebug__))
_mm_castps_si128(__m128 __in)
{
    return (__m128i)__in;
}

static __inline__ __m128 __attribute__((__always_inline__, __nodebug__))
_mm_castsi128_ps(__m128i __in)
{
    return (__m128)__in;
}

static __inline__ __m128d __attribute__((__always_inline__, __nodebug__))
_mm_castsi128_pd(__m128i __in)
{
    return (__m128d)__in;
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_pause(void)
{
    __asm__ volatile ("pause");
}

#define _MM_SHUFFLE2(x, y) (((x) << 1) | (y))

#endif /* __SSE2__ */

#endif /* __EMMINTRIN_H */

#else /* !__clang__ */

#ifdef __SSE2__
#include "xmmintrin.h"

/* SSE2 */
typedef double __v2df __attribute__ ((__vector_size__ (16)));
typedef long long __v2di __attribute__ ((__vector_size__ (16)));
typedef int __v4si __attribute__ ((__vector_size__ (16)));
typedef short __v8hi __attribute__ ((__vector_size__ (16)));
typedef char __v16qi __attribute__ ((__vector_size__ (16)));

/* The Intel API is flexible enough that we must allow aliasing with other
   vector types, and their scalar components.  */
typedef long long __m128i __attribute__ ((__vector_size__ (16), __may_alias__));
typedef double __m128d __attribute__ ((__vector_size__ (16), __may_alias__));

/* Create a selector for use with the SHUFPD instruction.  */
#define _MM_SHUFFLE2(fp1,fp0) \
 (((fp1) << 1) | (fp0))

/* APPLE LOCAL begin nodebug inline 4152603 */
#define __always_inline__ __always_inline__, __nodebug__
/* APPLE LOCAL end nodebug inline 4152603 */

/* APPLE LOCAL begin radar 5618945 */
#undef __STATIC_INLINE
#ifdef __GNUC_STDC_INLINE__
#define __STATIC_INLINE __inline
#else
#define __STATIC_INLINE static __inline
#endif
/* APPLE LOCAL end radar 5618945 */

/* APPLE LOCAL begin radar 4152603 */
/* Create a vector with element 0 as F and the rest zero.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_sd (double __F)
{
  return __extension__ (__m128d){ __F, 0 };
}

/* Create a vector with both elements equal to F.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set1_pd (double __F)
{
  return __extension__ (__m128d){ __F, __F };
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_pd1 (double __F)
{
  return _mm_set1_pd (__F);
}

/* Create a vector with the lower value X and upper value W.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_pd (double __W, double __X)
{
  return __extension__ (__m128d){ __X, __W };
}

/* Create a vector with the lower value W and upper value X.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_setr_pd (double __W, double __X)
{
  return __extension__ (__m128d){ __W, __X };
}

/* Create a vector of zeros.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_setzero_pd (void)
{
  return __extension__ (__m128d){ 0.0, 0.0 };
}

/* Sets the low DPFP value of A from the low value of B.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_move_sd (__m128d __A, __m128d __B)
{
  return (__m128d) __builtin_ia32_movsd ((__v2df)__A, (__v2df)__B);
}

/* Load two DPFP values from P.  The address must be 16-byte aligned.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_load_pd (double const *__P)
{
  return *(__m128d *)__P;
}

/* Load two DPFP values from P.  The address need not be 16-byte aligned.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_loadu_pd (double const *__P)
{
  return __builtin_ia32_loadupd (__P);
}

/* Create a vector with all two elements equal to *P.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_load1_pd (double const *__P)
{
  return _mm_set1_pd (*__P);
}

/* Create a vector with element 0 as *P and the rest zero.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_load_sd (double const *__P)
{
  return _mm_set_sd (*__P);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_load_pd1 (double const *__P)
{
  return _mm_load1_pd (__P);
}

/* Load two DPFP values in reverse order.  The address must be aligned.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_loadr_pd (double const *__P)
{
  __m128d __tmp = _mm_load_pd (__P);
  return __builtin_ia32_shufpd (__tmp, __tmp, _MM_SHUFFLE2 (0,1));
}

/* Store two DPFP values.  The address must be 16-byte aligned.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_store_pd (double *__P, __m128d __A)
{
  *(__m128d *)__P = __A;
}

/* Store two DPFP values.  The address need not be 16-byte aligned.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_storeu_pd (double *__P, __m128d __A)
{
  __builtin_ia32_storeupd (__P, __A);
}

/* Stores the lower DPFP value.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_store_sd (double *__P, __m128d __A)
{
  *__P = __builtin_ia32_vec_ext_v2df (__A, 0);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE double __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsd_f64 (__m128d __A)
{
  return __builtin_ia32_vec_ext_v2df (__A, 0);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_storel_pd (double *__P, __m128d __A)
{
  _mm_store_sd (__P, __A);
}

/* Stores the upper DPFP value.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_storeh_pd (double *__P, __m128d __A)
{
  *__P = __builtin_ia32_vec_ext_v2df (__A, 1);
}

/* Store the lower DPFP value across two words.
   The address must be 16-byte aligned.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_store1_pd (double *__P, __m128d __A)
{
  _mm_store_pd (__P, __builtin_ia32_shufpd (__A, __A, _MM_SHUFFLE2 (0,0)));
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_store_pd1 (double *__P, __m128d __A)
{
  _mm_store1_pd (__P, __A);
}

/* Store two DPFP values in reverse order.  The address must be aligned.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_storer_pd (double *__P, __m128d __A)
{
  _mm_store_pd (__P, __builtin_ia32_shufpd (__A, __A, _MM_SHUFFLE2 (0,1)));
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi128_si32 (__m128i __A)
{
  return __builtin_ia32_vec_ext_v4si ((__v4si)__A, 0);
}

#ifdef __x86_64__
/* Intel intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE long long __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi128_si64 (__m128i __A)
{
  return __builtin_ia32_vec_ext_v2di ((__v2di)__A, 0);
}

/* Microsoft intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE long long __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi128_si64x (__m128i __A)
{
  return __builtin_ia32_vec_ext_v2di ((__v2di)__A, 0);
}
#endif

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_add_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_addpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_add_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_addsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sub_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_subpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sub_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_subsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mul_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_mulpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mul_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_mulsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_div_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_divpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_div_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_divsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sqrt_pd (__m128d __A)
{
  return (__m128d)__builtin_ia32_sqrtpd ((__v2df)__A);
}

/* Return pair {sqrt (A[0), B[1]}.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sqrt_sd (__m128d __A, __m128d __B)
{
  __v2df __tmp = __builtin_ia32_movsd ((__v2df)__A, (__v2df)__B);
  return (__m128d)__builtin_ia32_sqrtsd ((__v2df)__tmp);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_min_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_minpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_min_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_minsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_max_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_maxpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_max_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_maxsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_and_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_andpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_andnot_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_andnpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_or_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_orpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_xor_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_xorpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpeq_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpeqpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmplt_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpltpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmple_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmplepd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpgt_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpgtpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpge_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpgepd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpneq_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpneqpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpnlt_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpnltpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpnle_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpnlepd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpngt_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpngtpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpnge_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpngepd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpord_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpordpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpunord_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpunordpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpeq_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpeqsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmplt_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpltsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmple_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmplesd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpgt_sd (__m128d __A, __m128d __B)
{
  return (__m128d) __builtin_ia32_movsd ((__v2df) __A,
					 (__v2df)
					 __builtin_ia32_cmpltsd ((__v2df) __B,
								 (__v2df)
								 __A));
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpge_sd (__m128d __A, __m128d __B)
{
  return (__m128d) __builtin_ia32_movsd ((__v2df) __A,
					 (__v2df)
					 __builtin_ia32_cmplesd ((__v2df) __B,
								 (__v2df)
								 __A));
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpneq_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpneqsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpnlt_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpnltsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpnle_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpnlesd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpngt_sd (__m128d __A, __m128d __B)
{
  return (__m128d) __builtin_ia32_movsd ((__v2df) __A,
					 (__v2df)
					 __builtin_ia32_cmpnltsd ((__v2df) __B,
								  (__v2df)
								  __A));
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpnge_sd (__m128d __A, __m128d __B)
{
  return (__m128d) __builtin_ia32_movsd ((__v2df) __A,
					 (__v2df)
					 __builtin_ia32_cmpnlesd ((__v2df) __B,
								  (__v2df)
								  __A));
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpord_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpordsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpunord_sd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_cmpunordsd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_comieq_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_comisdeq ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_comilt_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_comisdlt ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_comile_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_comisdle ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_comigt_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_comisdgt ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_comige_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_comisdge ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_comineq_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_comisdneq ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_ucomieq_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_ucomisdeq ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_ucomilt_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_ucomisdlt ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_ucomile_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_ucomisdle ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_ucomigt_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_ucomisdgt ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_ucomige_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_ucomisdge ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_ucomineq_sd (__m128d __A, __m128d __B)
{
  return __builtin_ia32_ucomisdneq ((__v2df)__A, (__v2df)__B);
}

/* Create a vector of Qi, where i is the element number.  */

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_epi64x (long long __q1, long long __q0)
{
  return __extension__ (__m128i)(__v2di){ __q0, __q1 };
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_epi64 (__m64 __q1,  __m64 __q0)
{
  return _mm_set_epi64x ((long long)__q1, (long long)__q0);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_epi32 (int __q3, int __q2, int __q1, int __q0)
{
  return __extension__ (__m128i)(__v4si){ __q0, __q1, __q2, __q3 };
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_epi16 (short __q7, short __q6, short __q5, short __q4,
	       short __q3, short __q2, short __q1, short __q0)
{
  return __extension__ (__m128i)(__v8hi){
    __q0, __q1, __q2, __q3, __q4, __q5, __q6, __q7 };
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set_epi8 (char __q15, char __q14, char __q13, char __q12,
	      char __q11, char __q10, char __q09, char __q08,
	      char __q07, char __q06, char __q05, char __q04,
	      char __q03, char __q02, char __q01, char __q00)
{
  return __extension__ (__m128i)(__v16qi){
    __q00, __q01, __q02, __q03, __q04, __q05, __q06, __q07,
    __q08, __q09, __q10, __q11, __q12, __q13, __q14, __q15
  };
}

/* APPLE LOCAL begin 4220129 */
/* functions moved to end of file */
/* APPLE LOCAL end 4220129 */

/* Create a vector of Qi, where i is the element number.
   The parameter order is reversed from the _mm_set_epi* functions.  */

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_setr_epi64 (__m64 __q0, __m64 __q1)
{
  return _mm_set_epi64 (__q1, __q0);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_setr_epi32 (int __q0, int __q1, int __q2, int __q3)
{
  return _mm_set_epi32 (__q3, __q2, __q1, __q0);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_setr_epi16 (short __q0, short __q1, short __q2, short __q3,
	        short __q4, short __q5, short __q6, short __q7)
{
  return _mm_set_epi16 (__q7, __q6, __q5, __q4, __q3, __q2, __q1, __q0);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_setr_epi8 (char __q00, char __q01, char __q02, char __q03,
	       char __q04, char __q05, char __q06, char __q07,
	       char __q08, char __q09, char __q10, char __q11,
	       char __q12, char __q13, char __q14, char __q15)
{
  return _mm_set_epi8 (__q15, __q14, __q13, __q12, __q11, __q10, __q09, __q08,
		       __q07, __q06, __q05, __q04, __q03, __q02, __q01, __q00);
}

/* Create a vector with element 0 as *P and the rest zero.  */

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_load_si128 (__m128i const *__P)
{
  return *__P;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_loadu_si128 (__m128i const *__P)
{
  return (__m128i) __builtin_ia32_loaddqu ((char const *)__P);
}

/* APPLE LOCAL begin 4099020 */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_loadl_epi64 (__m128i const *__P)
{
  return  (__m128i)__builtin_ia32_loadlv4si ((__v2si *)__P);
}
/* APPLE LOCAL end 4099020 */

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_store_si128 (__m128i *__P, __m128i __B)
{
  *__P = __B;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_storeu_si128 (__m128i *__P, __m128i __B)
{
  __builtin_ia32_storedqu ((char *)__P, (__v16qi)__B);
}

/* APPLE LOCAL begin 4099020 */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_storel_epi64 (__m128i *__P, __m128i __B)
{
  __builtin_ia32_storelv4si ((__v2si *)__P, __B);
}
/* APPLE LOCAL end 4099020 */

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m64 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_movepi64_pi64 (__m128i __B)
{
  return (__m64) __builtin_ia32_vec_ext_v2di ((__v2di)__B, 0);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_movpi64_epi64 (__m64 __A)
{
  return _mm_set_epi64 ((__m64)0LL, __A);
}

/* APPLE LOCAL begin 4099020 */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_move_epi64 (__m128i __A)
{
  return (__m128i)__builtin_ia32_movqv4si ((__v4si)__A) ;
}
/* APPLE LOCAL end 4099020 */

/* Create a vector of zeros.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_setzero_si128 (void)
{
  return __extension__ (__m128i)(__v4si){ 0, 0, 0, 0 };
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtepi32_pd (__m128i __A)
{
  return (__m128d)__builtin_ia32_cvtdq2pd ((__v4si) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtepi32_ps (__m128i __A)
{
  return (__m128)__builtin_ia32_cvtdq2ps ((__v4si) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtpd_epi32 (__m128d __A)
{
  return (__m128i)__builtin_ia32_cvtpd2dq ((__v2df) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m64 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtpd_pi32 (__m128d __A)
{
  return (__m64)__builtin_ia32_cvtpd2pi ((__v2df) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtpd_ps (__m128d __A)
{
  return (__m128)__builtin_ia32_cvtpd2ps ((__v2df) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvttpd_epi32 (__m128d __A)
{
  return (__m128i)__builtin_ia32_cvttpd2dq ((__v2df) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m64 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvttpd_pi32 (__m128d __A)
{
  return (__m64)__builtin_ia32_cvttpd2pi ((__v2df) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtpi32_pd (__m64 __A)
{
  return (__m128d)__builtin_ia32_cvtpi2pd ((__v2si) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtps_epi32 (__m128 __A)
{
  return (__m128i)__builtin_ia32_cvtps2dq ((__v4sf) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvttps_epi32 (__m128 __A)
{
  return (__m128i)__builtin_ia32_cvttps2dq ((__v4sf) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtps_pd (__m128 __A)
{
  return (__m128d)__builtin_ia32_cvtps2pd ((__v4sf) __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsd_si32 (__m128d __A)
{
  return __builtin_ia32_cvtsd2si ((__v2df) __A);
}

#ifdef __x86_64__
/* Intel intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE long long __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsd_si64 (__m128d __A)
{
  return __builtin_ia32_cvtsd2si64 ((__v2df) __A);
}

/* Microsoft intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE long long __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsd_si64x (__m128d __A)
{
  return __builtin_ia32_cvtsd2si64 ((__v2df) __A);
}
#endif

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvttsd_si32 (__m128d __A)
{
  return __builtin_ia32_cvttsd2si ((__v2df) __A);
}

#ifdef __x86_64__
/* Intel intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE long long __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvttsd_si64 (__m128d __A)
{
  return __builtin_ia32_cvttsd2si64 ((__v2df) __A);
}

/* Microsoft intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE long long __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvttsd_si64x (__m128d __A)
{
  return __builtin_ia32_cvttsd2si64 ((__v2df) __A);
}
#endif

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsd_ss (__m128 __A, __m128d __B)
{
  return (__m128)__builtin_ia32_cvtsd2ss ((__v4sf) __A, (__v2df) __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi32_sd (__m128d __A, int __B)
{
  return (__m128d)__builtin_ia32_cvtsi2sd ((__v2df) __A, __B);
}

#ifdef __x86_64__
/* Intel intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi64_sd (__m128d __A, long long __B)
{
  return (__m128d)__builtin_ia32_cvtsi642sd ((__v2df) __A, __B);
}

/* Microsoft intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi64x_sd (__m128d __A, long long __B)
{
  return (__m128d)__builtin_ia32_cvtsi642sd ((__v2df) __A, __B);
}
#endif

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtss_sd (__m128d __A, __m128 __B)
{
  return (__m128d)__builtin_ia32_cvtss2sd ((__v2df) __A, (__v4sf)__B);
}

/* APPLE LOCAL 5814283 */
#define _mm_shuffle_pd(__A, __B, __C) ((__m128d)__builtin_ia32_shufpd ((__v2df)(__A), (__v2df)(__B), (__C)))

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpackhi_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_unpckhpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpacklo_pd (__m128d __A, __m128d __B)
{
  return (__m128d)__builtin_ia32_unpcklpd ((__v2df)__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_loadh_pd (__m128d __A, double const *__B)
{
  return (__m128d)__builtin_ia32_loadhpd ((__v2df)__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_loadl_pd (__m128d __A, double const *__B)
{
  return (__m128d)__builtin_ia32_loadlpd ((__v2df)__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_movemask_pd (__m128d __A)
{
  return __builtin_ia32_movmskpd ((__v2df)__A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_packs_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_packsswb128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_packs_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_packssdw128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_packus_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_packuswb128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpackhi_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpckhbw128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpackhi_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpckhwd128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpackhi_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpckhdq128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpackhi_epi64 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpckhqdq128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpacklo_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpcklbw128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpacklo_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpcklwd128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpacklo_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpckldq128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_unpacklo_epi64 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_punpcklqdq128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_add_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_add_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_add_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddd128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_add_epi64 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddq128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_adds_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddsb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_adds_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddsw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_adds_epu8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddusb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_adds_epu16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_paddusw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sub_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sub_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sub_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubd128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sub_epi64 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubq128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_subs_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubsb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_subs_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubsw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_subs_epu8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubusb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_subs_epu16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psubusw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_madd_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pmaddwd128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mulhi_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pmulhw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mullo_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pmullw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m64 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mul_su32 (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pmuludq ((__v2si)__A, (__v2si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mul_epu32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pmuludq128 ((__v4si)__A, (__v4si)__B);
}

#if 0
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_slli_epi16 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_psllwi128 ((__v8hi)__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_slli_epi32 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_pslldi128 ((__v4si)__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_slli_epi64 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_psllqi128 ((__v2di)__A, __B);
}
#else
#define _mm_slli_epi16(__A, __B) \
  ((__m128i)__builtin_ia32_psllwi128 ((__v8hi)(__A), (__B)))
#define _mm_slli_epi32(__A, __B) \
  ((__m128i)__builtin_ia32_pslldi128 ((__v8hi)(__A), (__B)))
#define _mm_slli_epi64(__A, __B) \
  ((__m128i)__builtin_ia32_psllqi128 ((__v8hi)(__A), (__B)))
#endif

#if 0
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srai_epi16 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_psrawi128 ((__v8hi)__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srai_epi32 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_psradi128 ((__v4si)__A, __B);
}
#else
#define _mm_srai_epi16(__A, __B) \
  ((__m128i)__builtin_ia32_psrawi128 ((__v8hi)(__A), (__B)))
#define _mm_srai_epi32(__A, __B) \
  ((__m128i)__builtin_ia32_psradi128 ((__v8hi)(__A), (__B)))
#endif

#if 0
static __m128i __attribute__((__always_inline__))
_mm_srli_si128 (__m128i __A, int __B)
{
  return ((__m128i)__builtin_ia32_psrldqi128 (__A, __B * 8));
}

static __m128i __attribute__((__always_inline__))
_mm_srli_si128 (__m128i __A, int __B)
{
  return ((__m128i)__builtin_ia32_pslldqi128 (__A, __B * 8));
}
#else
/* APPLE LOCAL begin 5919583 */
#define _mm_srli_si128  (__m128i)__builtin_ia32_psrldqi128_byteshift
#define _mm_slli_si128  (__m128i)__builtin_ia32_pslldqi128_byteshift
/* APPLE LOCAL end 5919583 */
#endif

#if 0
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srli_epi16 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_psrlwi128 ((__v8hi)__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srli_epi32 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_psrldi128 ((__v4si)__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srli_epi64 (__m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_psrlqi128 ((__v2di)__A, __B);
}
#else
#define _mm_srli_epi16(__A, __B) \
  ((__m128i)__builtin_ia32_psrlwi128 ((__v8hi)(__A), (__B)))
#define _mm_srli_epi32(__A, __B) \
  ((__m128i)__builtin_ia32_psrldi128 ((__v4si)(__A), (__B)))
#define _mm_srli_epi64(__A, __B) \
  ((__m128i)__builtin_ia32_psrlqi128 ((__v4si)(__A), (__B)))
#endif

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sll_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psllw128((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sll_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pslld128((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sll_epi64 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psllq128((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sra_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psraw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sra_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psrad128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srl_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psrlw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srl_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psrld128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_srl_epi64 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psrlq128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_and_si128 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pand128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_andnot_si128 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pandn128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_or_si128 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_por128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_xor_si128 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pxor128 ((__v2di)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpeq_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpeqb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpeq_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpeqw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpeq_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpeqd128 ((__v4si)__A, (__v4si)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmplt_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpgtb128 ((__v16qi)__B, (__v16qi)__A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmplt_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpgtw128 ((__v8hi)__B, (__v8hi)__A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmplt_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpgtd128 ((__v4si)__B, (__v4si)__A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpgt_epi8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpgtb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpgt_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpgtw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cmpgt_epi32 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pcmpgtd128 ((__v4si)__A, (__v4si)__B);
}

#if 0
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_extract_epi16 (__m128i const __A, int const __N)
{
  return __builtin_ia32_vec_ext_v8hi ((__v8hi)__A, __N);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_insert_epi16 (__m128i const __A, int const __D, int const __N)
{
  return (__m128i) __builtin_ia32_vec_set_v8hi ((__v8hi)__A, __D, __N);
}
#else
#define _mm_extract_epi16(A, N) \
  ((int) __builtin_ia32_vec_ext_v8hi ((__v8hi)(A), (N)))
#define _mm_insert_epi16(A, D, N) \
  ((__m128i) __builtin_ia32_vec_set_v8hi ((__v8hi)(A), (D), (N)))
#endif

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_max_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pmaxsw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_max_epu8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pmaxub128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_min_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pminsw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_min_epu8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pminub128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE int __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_movemask_epi8 (__m128i __A)
{
  return __builtin_ia32_pmovmskb128 ((__v16qi)__A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mulhi_epu16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pmulhuw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin 5814283 */
#define _mm_shufflehi_epi16(__A, __B) ((__m128i)__builtin_ia32_pshufhw ((__v8hi)(__A), (__B)))
#define _mm_shufflelo_epi16(__A, __B) ((__m128i)__builtin_ia32_pshuflw ((__v8hi)(__A), (__B)))
#define _mm_shuffle_epi32(__A, __B) ((__m128i)__builtin_ia32_pshufd ((__v4si)(__A), (__B)))
/* APPLE LOCAL end 5814283 */

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_maskmoveu_si128 (__m128i __A, __m128i __B, char *__C)
{
  __builtin_ia32_maskmovdqu ((__v16qi)__A, (__v16qi)__B, __C);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_avg_epu8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pavgb128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_avg_epu16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_pavgw128 ((__v8hi)__A, (__v8hi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_sad_epu8 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psadbw128 ((__v16qi)__A, (__v16qi)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_stream_si32 (int *__A, int __B)
{
  __builtin_ia32_movnti (__A, __B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_stream_si128 (__m128i *__A, __m128i __B)
{
  __builtin_ia32_movntdq ((__v2di *)__A, (__v2di)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_stream_pd (double *__A, __m128d __B)
{
  __builtin_ia32_movntpd (__A, (__v2df)__B);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_clflush (void const *__A)
{
  __builtin_ia32_clflush (__A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_lfence (void)
{
  __builtin_ia32_lfence ();
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE void __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_mfence (void)
{
  __builtin_ia32_mfence ();
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi32_si128 (int __A)
{
  return _mm_set_epi32 (0, 0, 0, __A);
}

#ifdef __x86_64__
/* Intel intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi64_si128 (long long __A)
{
  return _mm_set_epi64x (0, __A);
}

/* Microsoft intrinsic.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_cvtsi64x_si128 (long long __A)
{
  return _mm_set_epi64x (0, __A);
}
#endif

/* Casts between various SP, DP, INT vector types.  Note that these do no
   conversion of values, they just change the type.  */
/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_castpd_ps(__m128d __A)
{
  return (__m128) __A;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_castpd_si128(__m128d __A)
{
  return (__m128i) __A;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_castps_pd(__m128 __A)
{
  return (__m128d) __A;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_castps_si128(__m128 __A)
{
  return (__m128i) __A;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128 __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_castsi128_ps(__m128i __A)
{
  return (__m128) __A;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128d __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_castsi128_pd(__m128i __A)
{
  return (__m128d) __A;
}
/* APPLE LOCAL end radar 4152603 */

/* APPLE LOCAL begin 4220129, 4286110 */
/* Set all of the elements of the vector to A.  */

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set1_epi64x (long long __A)
{
  return _mm_set_epi64x (__A, __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set1_epi64 (__m64 __A)
{
  return _mm_set_epi64 (__A, __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set1_epi32 (int __A)
{
  return _mm_set_epi32 (__A, __A, __A, __A);
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set1_epi16 (short __A)
{
  __m128i temp, temp2, temp3; 
  temp = _mm_cvtsi32_si128((int)__A);
  temp2 = _mm_unpacklo_epi16(temp, temp);
  temp3 = _mm_shuffle_epi32(temp2, 0);
  return temp3;
}

/* APPLE LOCAL begin radar 5618945 */
__STATIC_INLINE __m128i __attribute__((__always_inline__))
/* APPLE LOCAL end radar 5618945 */
_mm_set1_epi8 (char __A)
{
    __m128i temp, temp2, temp3, temp4;
    temp = _mm_cvtsi32_si128 ((int)__A);
    temp2 = _mm_unpacklo_epi8 (temp, temp);
    temp3 = _mm_unpacklo_epi8 (temp2, temp2);
    temp4 = _mm_shuffle_epi32 (temp3, 0);
    return temp4;
}
/* APPLE LOCAL end 4220129, 4286110 */

/* APPLE LOCAL begin nodebug inline 4152603 */
#undef __always_inline__
/* APPLE LOCAL end nodebug inline 4152603 */

#endif /* __SSE2__  */

#endif /* __clang__ */

#endif /* _EMMINTRIN_H_INCLUDED */
