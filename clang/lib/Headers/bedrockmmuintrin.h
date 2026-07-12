#ifndef __BEDROCKMMUINTRIN_H
#define __BEDROCKMMUINTRIN_H

#include <stdint.h>

typedef struct __bedrock_query_result {
  uint64_t value;
  uint16_t flags;
  uint16_t __reserved[3];
} __bedrock_query_result_t;

static __inline__ void __bedrock_invalidate_tlb(void) {
  __builtin_bedrock_invalidate_tlb();
}
static __inline__ void __bedrock_invalidate_page(const void *page) {
  __builtin_bedrock_invalidate_page(page);
}
#define __bedrock_invalidate_asid(asid) __builtin_bedrock_invalidate_asid(asid)
static __inline__ void __bedrock_switch_page_table(uint64_t ptcr) {
  __builtin_bedrock_switch_page_table(ptcr);
}
static __inline__ void __bedrock_switch_page_table_asid(uint64_t ptcr,
                                                        uint16_t asid) {
  __builtin_bedrock_switch_page_table_asid(ptcr, asid);
}
static __inline__ __bedrock_query_result_t
__bedrock_virtual_to_physical(uint64_t address) {
  __bedrock_query_result_t result = {0};
  __builtin_bedrock_virtual_to_physical(address, &result.value, &result.flags);
  return result;
}
#define __bedrock_page_table_query(level, address)                             \
  __extension__({                                                              \
    __bedrock_query_result_t __result = {0};                                   \
    __builtin_bedrock_page_table_query((level), (address), &__result.value,    \
                                       &__result.flags);                       \
    __result;                                                                  \
  })

#endif
