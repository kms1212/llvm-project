#ifndef __BEDROCKINTEGERINTRIN_H
#define __BEDROCKINTEGERINTRIN_H

#include <stdint.h>

static __inline__ uint8_t __bedrock_clmul_u8(uint8_t left, uint8_t right) {
  return __builtin_bedrock_clmul_u8(left, right);
}
static __inline__ uint16_t __bedrock_clmul_u16(uint16_t left, uint16_t right) {
  return __builtin_bedrock_clmul_u16(left, right);
}
static __inline__ uint32_t __bedrock_clmul_u32(uint32_t left, uint32_t right) {
  return __builtin_bedrock_clmul_u32(left, right);
}
static __inline__ uint64_t __bedrock_clmul_u64(uint64_t left, uint64_t right) {
  return __builtin_bedrock_clmul_u64(left, right);
}

#endif
