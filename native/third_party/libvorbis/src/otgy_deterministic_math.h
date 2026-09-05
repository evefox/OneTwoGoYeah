#ifndef OTGY_DETERMINISTIC_MATH_H
#define OTGY_DETERMINISTIC_MATH_H

/*
 * The pinned Vorbis snapshot calls the platform libm directly.  Apple ships
 * different correctly-rounded implementations for arm64 and x86_64; their
 * results can differ in the low bits and eventually change a Vorbis bitstream.
 * Keep the codec's existing libm implementation, but quantize its binary64
 * result at one explicit precision before the value enters the codec.  The
 * retained 46-bit significand is far finer than the float state used by
 * Vorbis, while making the boundary deterministic for the supported targets.
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

#if defined(_MSC_VER)
#define OTGY_DETERMINISTIC_INLINE __inline
#else
#define OTGY_DETERMINISTIC_INLINE inline
#endif

static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_math_round(double value) {
  union {
    double value;
    uint64_t bits;
  } u;
  uint64_t magnitude;
  const uint64_t exponent_mask = UINT64_C(0x7ff0000000000000);
  const uint64_t sign_mask = UINT64_C(0x8000000000000000);
  const uint64_t discarded_mask = UINT64_C(0x000000000000007f);
  const uint64_t half = UINT64_C(0x0000000000000040);

  memcpy(&u.bits, &value, sizeof(u.bits));
  if ((u.bits & exponent_mask) == exponent_mask ||
      (u.bits & exponent_mask) == 0U) {
    return value;
  }
  magnitude = u.bits & ~sign_mask;
  magnitude += half;
  magnitude &= ~discarded_mask;
  u.bits = (u.bits & sign_mask) | magnitude;
  memcpy(&value, &u.bits, sizeof(value));
  return value;
}

static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_sin(double value) {
  return otgy_deterministic_math_round(sin(value));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_cos(double value) {
  return otgy_deterministic_math_round(cos(value));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_log(double value) {
  return otgy_deterministic_math_round(log(value));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_exp(double value) {
  return otgy_deterministic_math_round(exp(value));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_pow(double value, double exponent) {
  return otgy_deterministic_math_round(pow(value, exponent));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_atan(double value) {
  return otgy_deterministic_math_round(atan(value));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_acos(double value) {
  return otgy_deterministic_math_round(acos(value));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_sqrt(double value) {
  return otgy_deterministic_math_round(sqrt(value));
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_floor(double value) {
  return floor(value);
}
static OTGY_DETERMINISTIC_INLINE double otgy_deterministic_ldexp(double value, int exponent) {
  return ldexp(value, exponent);
}

#define sin otgy_deterministic_sin
#define cos otgy_deterministic_cos
#define log otgy_deterministic_log
#define exp otgy_deterministic_exp
#define pow otgy_deterministic_pow
#define atan otgy_deterministic_atan
#define acos otgy_deterministic_acos
#define sqrt otgy_deterministic_sqrt
#define floor otgy_deterministic_floor
#define ldexp otgy_deterministic_ldexp

#undef OTGY_DETERMINISTIC_INLINE

#endif
