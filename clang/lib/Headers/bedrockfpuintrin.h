#ifndef __BEDROCKFPUINTRIN_H
#define __BEDROCKFPUINTRIN_H

#include <stdint.h>

static __inline__ uint16_t __bedrock_read_fstatus(void) {
  return __builtin_bedrock_read_fstatus();
}
static __inline__ void __bedrock_write_fstatus(uint16_t value) {
  __builtin_bedrock_write_fstatus(value);
}
static __inline__ uint16_t __bedrock_read_fflags(void) {
  return __builtin_bedrock_read_fflags();
}
static __inline__ void __bedrock_write_fflags(uint16_t value) {
  __builtin_bedrock_write_fflags(value);
}
static __inline__ uint16_t __bedrock_fclass_f32(float value) {
  return __builtin_bedrock_fclass_f32(value);
}
static __inline__ uint16_t __bedrock_fclass_f64(double value) {
  return __builtin_bedrock_fclass_f64(value);
}

#define __BEDROCK_APPROX_UNARY(NAME)                                         \
  static __inline__ float __bedrock_##NAME##_f32(float value) {              \
    return __builtin_bedrock_##NAME##_f32(value);                            \
  }                                                                           \
  static __inline__ double __bedrock_##NAME##_f64(double value) {            \
    return __builtin_bedrock_##NAME##_f64(value);                            \
  }

__BEDROCK_APPROX_UNARY(facosa)
__BEDROCK_APPROX_UNARY(fasina)
__BEDROCK_APPROX_UNARY(fatana)
__BEDROCK_APPROX_UNARY(fatanha)
__BEDROCK_APPROX_UNARY(fcosa)
__BEDROCK_APPROX_UNARY(fcosha)
__BEDROCK_APPROX_UNARY(fetoxa)
__BEDROCK_APPROX_UNARY(fetoxm1a)
__BEDROCK_APPROX_UNARY(flog10a)
__BEDROCK_APPROX_UNARY(flog2a)
__BEDROCK_APPROX_UNARY(flogna)
__BEDROCK_APPROX_UNARY(flognp1a)
__BEDROCK_APPROX_UNARY(fsina)
__BEDROCK_APPROX_UNARY(fsinha)
__BEDROCK_APPROX_UNARY(ftana)
__BEDROCK_APPROX_UNARY(ftanha)
__BEDROCK_APPROX_UNARY(ftentoxa)
__BEDROCK_APPROX_UNARY(ftwotoxa)

#undef __BEDROCK_APPROX_UNARY

static __inline__ void __bedrock_fsincosa_f32(float value, float *sin_result,
                                              float *cos_result) {
  __builtin_bedrock_fsincosa_f32(value, sin_result, cos_result);
}

static __inline__ void __bedrock_fsincosa_f64(double value,
                                              double *sin_result,
                                              double *cos_result) {
  __builtin_bedrock_fsincosa_f64(value, sin_result, cos_result);
}

#endif
