#ifndef __BEDROCKCOREINTRIN_H
#define __BEDROCKCOREINTRIN_H

#include <stdint.h>

typedef enum __bedrock_performance_counter {
  __BEDROCK_PMC_CYCLE = 1,
  __BEDROCK_PMC_INSTRET = 2,
  __BEDROCK_PMC_PTWALK = 3
} __bedrock_performance_counter_t;

static __inline__ uint64_t __bedrock_cpuid(uint64_t selector) {
  return __builtin_bedrock_cpuid(selector);
}
static __inline__ uint16_t __bedrock_read_status(void) {
  return __builtin_bedrock_read_status();
}
#define __bedrock_rdpmc(counter_id) __builtin_bedrock_rdpmc(counter_id)
static __inline__ void __bedrock_breakpoint(void) {
  __builtin_bedrock_breakpoint();
}
#define __bedrock_trace(marker) __builtin_bedrock_trace(marker)
static __inline__ void __bedrock_yield(void) { __builtin_bedrock_yield(); }
static __inline__ void __bedrock_wait(void) { __builtin_bedrock_wait(); }

#endif
