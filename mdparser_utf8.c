/*
  +----------------------------------------------------------------------+
  | Copyright (c) 2025-2026, Ilia Alshanetsky                            |
  | Copyright (c) 2025-2026, Advanced Internet Designs Inc.              |
  +----------------------------------------------------------------------+
  | This source file is subject to the BSD 3-Clause license that is      |
  | bundled with this package in the file LICENSE.                       |
  +----------------------------------------------------------------------+
  | Author: Ilia Alshanetsky <ilia@ilia.ws>                              |
  +----------------------------------------------------------------------+
*/

#include "mdparser_utf8.h"

#include <stdint.h>

/* Vectorized UTF-8 validation after Keiser and Lemire, "Validating UTF-8
 * In Less Than One Instruction Per Byte" (2021), as used by simdutf and
 * simdjson. Three 16-entry nibble lookups classify each (byte, previous
 * byte) pair; a separate check requires third and fourth bytes to be
 * continuations. Errors are tested per 64-byte chunk so the caller can
 * resume scalar decoding near the first bad byte. */

#if !defined(MDPARSER_DISABLE_SIMD) && (defined(__x86_64__) || defined(_M_X64))
# define MDU8_X86 1
# include <immintrin.h>
# if defined(_MSC_VER)
#  include <intrin.h>
#  define MDU8_TARGET
# else
#  include <cpuid.h>
#  define MDU8_TARGET __attribute__((target("ssse3")))
# endif
#elif !defined(MDPARSER_DISABLE_SIMD) && defined(__aarch64__) \
    && (defined(__ARM_NEON) || defined(__ARM_NEON__))
# define MDU8_NEON 1
# include <arm_neon.h>
#endif

#define MDU8_TOO_SHORT      0x01
#define MDU8_TOO_LONG       0x02
#define MDU8_OVERLONG_3     0x04
#define MDU8_TOO_LARGE      0x08
#define MDU8_SURROGATE      0x10
#define MDU8_OVERLONG_2     0x20
#define MDU8_TOO_LARGE_1000 0x40
#define MDU8_OVERLONG_4     0x40
#define MDU8_TWO_CONTS      0x80
#define MDU8_CARRY (MDU8_TOO_SHORT | MDU8_TOO_LONG | MDU8_TWO_CONTS)

#define MDU8_B1_HIGH \
    MDU8_TOO_LONG, MDU8_TOO_LONG, MDU8_TOO_LONG, MDU8_TOO_LONG, \
    MDU8_TOO_LONG, MDU8_TOO_LONG, MDU8_TOO_LONG, MDU8_TOO_LONG, \
    MDU8_TWO_CONTS, MDU8_TWO_CONTS, MDU8_TWO_CONTS, MDU8_TWO_CONTS, \
    MDU8_TOO_SHORT | MDU8_OVERLONG_2, \
    MDU8_TOO_SHORT, \
    MDU8_TOO_SHORT | MDU8_OVERLONG_3 | MDU8_SURROGATE, \
    MDU8_TOO_SHORT | MDU8_TOO_LARGE | MDU8_TOO_LARGE_1000 | MDU8_OVERLONG_4

#define MDU8_L5 (MDU8_CARRY | MDU8_TOO_LARGE | MDU8_TOO_LARGE_1000)
#define MDU8_B1_LOW \
    MDU8_CARRY | MDU8_OVERLONG_3 | MDU8_OVERLONG_2 | MDU8_OVERLONG_4, \
    MDU8_CARRY | MDU8_OVERLONG_2, \
    MDU8_CARRY, MDU8_CARRY, \
    MDU8_CARRY | MDU8_TOO_LARGE, \
    MDU8_L5, MDU8_L5, MDU8_L5, MDU8_L5, MDU8_L5, MDU8_L5, MDU8_L5, MDU8_L5, \
    MDU8_L5 | MDU8_SURROGATE, \
    MDU8_L5, MDU8_L5

#define MDU8_C8 (MDU8_TOO_LONG | MDU8_OVERLONG_2 | MDU8_TWO_CONTS)
#define MDU8_B2_HIGH \
    MDU8_TOO_SHORT, MDU8_TOO_SHORT, MDU8_TOO_SHORT, MDU8_TOO_SHORT, \
    MDU8_TOO_SHORT, MDU8_TOO_SHORT, MDU8_TOO_SHORT, MDU8_TOO_SHORT, \
    MDU8_C8 | MDU8_OVERLONG_3 | MDU8_TOO_LARGE_1000 | MDU8_OVERLONG_4, \
    MDU8_C8 | MDU8_OVERLONG_3 | MDU8_TOO_LARGE, \
    MDU8_C8 | MDU8_SURROGATE | MDU8_TOO_LARGE, \
    MDU8_C8 | MDU8_SURROGATE | MDU8_TOO_LARGE, \
    MDU8_TOO_SHORT, MDU8_TOO_SHORT, MDU8_TOO_SHORT, MDU8_TOO_SHORT

#if defined(MDU8_X86) || defined(MDU8_NEON)
static const uint8_t mdu8_b1h[16] = { MDU8_B1_HIGH };
static const uint8_t mdu8_b1l[16] = { MDU8_B1_LOW };
static const uint8_t mdu8_b2h[16] = { MDU8_B2_HIGH };
static const uint8_t mdu8_incomplete_max[16] = { 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF, 0xDF, 0xBF };

/* A sequence that starts in the last three bytes before 'end' may still be
 * open there. Back up to its lead byte so the scalar pass re-reads it. */
static size_t mdu8_resume_point(const unsigned char *p, size_t end)
{
    for (size_t k = 1; k <= 3 && k <= end; k++) {
        unsigned char c = p[end - k];
        if (c >= 0xC0) return end - k;
        if (c < 0x80) break;
    }
    return end;
}
#endif

#ifdef MDU8_X86
/* ZTS threads can run the first detection concurrently. Every store writes
 * the same value and nothing else is published through it, so relaxed atomic
 * accesses are enough; plain ones would be a data race. */
static long mdu8_ssse3_cached = -1;

# if defined(_MSC_VER) && !defined(__clang__)
/* Interlocked rather than volatile: /volatile:iso drops the atomicity that
 * the default /volatile:ms gives volatile accesses. A compare-exchange of -1
 * with -1 is an atomic read that leaves the value as it was. */
#  define MDU8_CACHE_LOAD() _InterlockedCompareExchange(&mdu8_ssse3_cached, -1, -1)
#  define MDU8_CACHE_STORE(v) _InterlockedExchange(&mdu8_ssse3_cached, (v))
# else
#  define MDU8_CACHE_LOAD() __atomic_load_n(&mdu8_ssse3_cached, __ATOMIC_RELAXED)
#  define MDU8_CACHE_STORE(v) __atomic_store_n(&mdu8_ssse3_cached, (v), __ATOMIC_RELAXED)
# endif

static int mdu8_has_ssse3(void)
{
    long cached = MDU8_CACHE_LOAD();
    if (cached < 0) {
# if defined(_MSC_VER)
        int regs[4];
        __cpuid(regs, 1);
        cached = (regs[2] >> 9) & 1;
# else
        unsigned int eax, ebx, ecx, edx;
        cached = (__get_cpuid(1, &eax, &ebx, &ecx, &edx) && ((ecx >> 9) & 1)) ? 1 : 0;
# endif
        MDU8_CACHE_STORE(cached);
    }
    return (int)cached;
}

MDU8_TARGET static inline __m128i mdu8_check_ssse3(__m128i in, __m128i prev_in)
{
    const __m128i nib = _mm_set1_epi8(0x0F);
    const __m128i b1h_tbl = _mm_loadu_si128((const __m128i *)mdu8_b1h);
    const __m128i b1l_tbl = _mm_loadu_si128((const __m128i *)mdu8_b1l);
    const __m128i b2h_tbl = _mm_loadu_si128((const __m128i *)mdu8_b2h);

    __m128i prev1 = _mm_alignr_epi8(in, prev_in, 15);
    __m128i sc = _mm_and_si128(
        _mm_and_si128(
            _mm_shuffle_epi8(b1h_tbl, _mm_and_si128(_mm_srli_epi16(prev1, 4), nib)),
            _mm_shuffle_epi8(b1l_tbl, _mm_and_si128(prev1, nib))),
        _mm_shuffle_epi8(b2h_tbl, _mm_and_si128(_mm_srli_epi16(in, 4), nib)));

    __m128i prev2 = _mm_alignr_epi8(in, prev_in, 14);
    __m128i prev3 = _mm_alignr_epi8(in, prev_in, 13);
    __m128i is3 = _mm_subs_epu8(prev2, _mm_set1_epi8((char)(0xE0 - 0x80)));
    __m128i is4 = _mm_subs_epu8(prev3, _mm_set1_epi8((char)(0xF0 - 0x80)));
    __m128i must23 = _mm_and_si128(_mm_or_si128(is3, is4), _mm_set1_epi8((char)0x80));
    return _mm_xor_si128(must23, sc);
}

MDU8_TARGET static size_t mdu8_valid_prefix_ssse3(const unsigned char *p, size_t len)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i incomplete_max = _mm_loadu_si128((const __m128i *)mdu8_incomplete_max);
    int prev_incomplete = 0;
    size_t i = 0;

    while (i + 64 <= len) {
        __m128i v0 = _mm_loadu_si128((const __m128i *)(p + i));
        __m128i v1 = _mm_loadu_si128((const __m128i *)(p + i + 16));
        __m128i v2 = _mm_loadu_si128((const __m128i *)(p + i + 32));
        __m128i v3 = _mm_loadu_si128((const __m128i *)(p + i + 48));
        if (_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(v0, v1), _mm_or_si128(v2, v3))) == 0) {
            if (prev_incomplete) break;
            i += 64;
            continue;
        }
        __m128i prev = i ? _mm_loadu_si128((const __m128i *)(p + i - 16)) : zero;
        __m128i err = _mm_or_si128(
            _mm_or_si128(mdu8_check_ssse3(v0, prev), mdu8_check_ssse3(v1, v0)),
            _mm_or_si128(mdu8_check_ssse3(v2, v1), mdu8_check_ssse3(v3, v2)));
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(err, zero)) != 0xFFFF) break;
        prev_incomplete = _mm_movemask_epi8(_mm_cmpeq_epi8(
            _mm_subs_epu8(v3, incomplete_max), zero)) != 0xFFFF;
        i += 64;
    }
    return mdu8_resume_point(p, i);
}
#endif

#ifdef MDU8_NEON
/* OR of both 64-bit halves: cheaper than an across-vector UMAXV on
 * Neoverse cores, and enough for zero and high-bit tests. */
static inline uint64_t mdu8_neon_fold(uint8x16_t v)
{
    uint64x2_t w = vreinterpretq_u64_u8(v);
    return vgetq_lane_u64(w, 0) | vgetq_lane_u64(w, 1);
}

static inline uint8x16_t mdu8_check_neon(uint8x16_t in, uint8x16_t prev_in)
{
    const uint8x16_t nib = vdupq_n_u8(0x0F);

    uint8x16_t prev1 = vextq_u8(prev_in, in, 15);
    uint8x16_t sc = vandq_u8(
        vandq_u8(vqtbl1q_u8(vld1q_u8(mdu8_b1h), vshrq_n_u8(prev1, 4)),
                 vqtbl1q_u8(vld1q_u8(mdu8_b1l), vandq_u8(prev1, nib))),
        vqtbl1q_u8(vld1q_u8(mdu8_b2h), vshrq_n_u8(in, 4)));

    uint8x16_t prev2 = vextq_u8(prev_in, in, 14);
    uint8x16_t prev3 = vextq_u8(prev_in, in, 13);
    uint8x16_t is3 = vqsubq_u8(prev2, vdupq_n_u8(0xE0 - 0x80));
    uint8x16_t is4 = vqsubq_u8(prev3, vdupq_n_u8(0xF0 - 0x80));
    uint8x16_t must23 = vandq_u8(vorrq_u8(is3, is4), vdupq_n_u8(0x80));
    return veorq_u8(must23, sc);
}

static size_t mdu8_valid_prefix_neon(const unsigned char *p, size_t len)
{
    const uint8x16_t incomplete_max = vld1q_u8(mdu8_incomplete_max);
    int prev_incomplete = 0;
    size_t i = 0;

    while (i + 64 <= len) {
        /* A separate ASCII loop keeps GCC from if-converting the full check
         * into every iteration, which halved ASCII throughput on Neoverse-N1. */
        for (;;) {
            uint8x16_t any = vorrq_u8(vorrq_u8(vld1q_u8(p + i), vld1q_u8(p + i + 16)),
                                      vorrq_u8(vld1q_u8(p + i + 32), vld1q_u8(p + i + 48)));
            if (mdu8_neon_fold(any) & 0x8080808080808080ULL) break;
            if (prev_incomplete) return mdu8_resume_point(p, i);
            i += 64;
            if (i + 64 > len) return mdu8_resume_point(p, i);
        }
        uint8x16_t v0 = vld1q_u8(p + i);
        uint8x16_t v1 = vld1q_u8(p + i + 16);
        uint8x16_t v2 = vld1q_u8(p + i + 32);
        uint8x16_t v3 = vld1q_u8(p + i + 48);
        uint8x16_t prev = i ? vld1q_u8(p + i - 16) : vdupq_n_u8(0);
        uint8x16_t err = vorrq_u8(
            vorrq_u8(mdu8_check_neon(v0, prev), mdu8_check_neon(v1, v0)),
            vorrq_u8(mdu8_check_neon(v2, v1), mdu8_check_neon(v3, v2)));
        if (mdu8_neon_fold(err) != 0) break;
        prev_incomplete = mdu8_neon_fold(vqsubq_u8(v3, incomplete_max)) != 0;
        i += 64;
    }
    return mdu8_resume_point(p, i);
}
#endif

size_t mdparser_utf8_valid_prefix(const unsigned char *src, size_t len)
{
    if (len < 64) return 0;
#if defined(MDU8_X86)
    if (mdu8_has_ssse3()) return mdu8_valid_prefix_ssse3(src, len);
#elif defined(MDU8_NEON)
    return mdu8_valid_prefix_neon(src, len);
#endif
    return 0;
}
