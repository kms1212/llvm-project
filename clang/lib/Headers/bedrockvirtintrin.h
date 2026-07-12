#ifndef __BEDROCKVIRTINTRIN_H
#define __BEDROCKVIRTINTRIN_H

#include <stddef.h>
#include <stdint.h>

typedef struct __bedrock_instruction_descriptor {
  uint64_t control;
  uint64_t operand1;
  uint64_t operand2;
  uint64_t operand3;
} __bedrock_instruction_descriptor_t;

static __inline__ intptr_t __bedrock_encode_instruction(
    void *destination, const __bedrock_instruction_descriptor_t *descriptor) {
  return __builtin_bedrock_encode_instruction(
      destination, descriptor->control, descriptor->operand1,
      descriptor->operand2, descriptor->operand3);
}

#endif
