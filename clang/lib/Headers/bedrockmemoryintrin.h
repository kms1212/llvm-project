#ifndef __BEDROCKMEMORYINTRIN_H
#define __BEDROCKMEMORYINTRIN_H

#include <stdint.h>

static __inline__ void __bedrock_read_fence(void) {
  __builtin_bedrock_read_fence();
}
static __inline__ void __bedrock_write_fence(void) {
  __builtin_bedrock_write_fence();
}
static __inline__ void __bedrock_address_fence(void) {
  __builtin_bedrock_address_fence();
}
static __inline__ void __bedrock_nontemporal_store_u8(uint8_t *destination,
                                                      uint8_t value) {
  __builtin_bedrock_nontemporal_store_u8(destination, value);
}
static __inline__ void __bedrock_nontemporal_store_u16(uint16_t *destination,
                                                       uint16_t value) {
  __builtin_bedrock_nontemporal_store_u16(destination, value);
}
static __inline__ void __bedrock_nontemporal_store_u32(uint32_t *destination,
                                                       uint32_t value) {
  __builtin_bedrock_nontemporal_store_u32(destination, value);
}
static __inline__ void __bedrock_nontemporal_store_u64(uint64_t *destination,
                                                       uint64_t value) {
  __builtin_bedrock_nontemporal_store_u64(destination, value);
}

#endif
