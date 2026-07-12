#ifndef __BEDROCKCACHEINTRIN_H
#define __BEDROCKCACHEINTRIN_H

#include <stddef.h>

static __inline__ void __bedrock_flush_dcache(void *address, size_t length) {
  __builtin_bedrock_flush_dcache(address, length);
}
static __inline__ void __bedrock_invalidate_dcache(void *address,
                                                   size_t length) {
  __builtin_bedrock_invalidate_dcache(address, length);
}
static __inline__ void __bedrock_invalidate_icache(void *address,
                                                   size_t length) {
  __builtin_bedrock_invalidate_icache(address, length);
}
static __inline__ void __bedrock_writeback_dcache(void *address,
                                                  size_t length) {
  __builtin_bedrock_writeback_dcache(address, length);
}
static __inline__ void __bedrock_sync_cache(void *address, size_t length) {
  __builtin_bedrock_sync_cache(address, length);
}

#endif
