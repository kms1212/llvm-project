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

#endif
