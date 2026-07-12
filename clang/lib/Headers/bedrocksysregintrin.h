#ifndef __BEDROCKSYSREGINTRIN_H
#define __BEDROCKSYSREGINTRIN_H

#include <stdint.h>

typedef enum __bedrock_segment_register {
  __BEDROCK_SEG_CS = 0,
  __BEDROCK_SEG_DS = 1,
  __BEDROCK_SEG_SS = 2,
  __BEDROCK_SEG_GS0 = 3,
  __BEDROCK_SEG_GS1 = 4,
  __BEDROCK_SEG_GS2 = 5,
  __BEDROCK_SEG_GS3 = 6,
  __BEDROCK_SEG_GS4 = 7
} __bedrock_segment_register_t;

static __inline__ void __bedrock_write_status(uint16_t value) {
  __builtin_bedrock_write_status(value);
}

#define __bedrock_read_control_register(selector)                              \
  __builtin_bedrock_read_control_register(selector)
#define __bedrock_write_control_register(selector, value)                      \
  __builtin_bedrock_write_control_register(selector, value)
#define __bedrock_read_segment_register(selector)                              \
  __builtin_bedrock_read_segment_register(selector)
#define __bedrock_write_segment_register(selector, image)                      \
  __builtin_bedrock_write_segment_register(selector, image)

#endif
