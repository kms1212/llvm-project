#ifndef __BEDROCKSYSREGINTRIN_H
#define __BEDROCKSYSREGINTRIN_H

#include <stdint.h>

#define __BEDROCK_SEGMENT_IMAGE(base_page, exponent, mantissa, bounds_only)   \
  ((((uint64_t)(base_page)) << 12) |                                         \
   (((uint64_t)(exponent) & 0x1f) << 7) |                                    \
   (((uint64_t)(mantissa) & 0x3f) << 1) |                                    \
   ((bounds_only) ? UINT64_C(1) : UINT64_C(0)))

#define __BEDROCK_SEGMENT_IMAGE_FOR_BASE(base, exponent, mantissa,            \
                                         bounds_only)                         \
  __BEDROCK_SEGMENT_IMAGE((uint64_t)(base) >> 12, exponent, mantissa,         \
                          bounds_only)

#define __BEDROCK_SEGMENT_DISABLED UINT64_C(0)

typedef enum __bedrock_segment_register {
  __BEDROCK_SEG_DS = 0,
  __BEDROCK_SEG_SS = 1,
  __BEDROCK_SEG_GS0 = 2,
  __BEDROCK_SEG_GS1 = 3,
  __BEDROCK_SEG_GS2 = 4,
  __BEDROCK_SEG_GS3 = 5,
  __BEDROCK_SEG_GS4 = 6,
  __BEDROCK_SEG_GS5 = 7
} __bedrock_segment_register_t;

typedef enum __bedrock_control_register {
  __BEDROCK_CR_PTCR = 0x0000,
  __BEDROCK_CR_ASCR = 0x0001,
  __BEDROCK_CR_ECR = 0x0002,
  __BEDROCK_CR_SPC = 0x0100,
  __BEDROCK_CR_SCS = 0x0101,
  __BEDROCK_CR_SDS = 0x0102,
  __BEDROCK_CR_URPC = 0x0108,
  __BEDROCK_CR_URSP = 0x0109,
  __BEDROCK_CR_URCS = 0x010A,
  __BEDROCK_CR_URDS = 0x010B,
  __BEDROCK_CR_URSS = 0x010C,
  __BEDROCK_CR_URCTL = 0x010D,
  __BEDROCK_CR_EPC = 0x0110,
  __BEDROCK_CR_ECS = 0x0111,
  __BEDROCK_CR_EDS = 0x0112,
  __BEDROCK_CR_SSS = 0x0200,
  __BEDROCK_CR_SSP = 0x0201,
  __BEDROCK_CR_ISS = 0x0210,
  __BEDROCK_CR_ISP = 0x0211,
  __BEDROCK_CR_FSS = 0x0220,
  __BEDROCK_CR_FSP = 0x0221,
  __BEDROCK_CR_DSS = 0x0230,
  __BEDROCK_CR_DSP = 0x0231,
  __BEDROCK_CR_BOOTPC = 0x1000,
  __BEDROCK_CR_BOOTCFG = 0x1001,
  __BEDROCK_CR_PMC = 0x1100
} __bedrock_control_register_t;

static __inline__ void __bedrock_write_status(uint16_t value) {
  __builtin_bedrock_write_status(value);
}

#define __bedrock_read_control_register(selector)                              \
  __builtin_bedrock_read_control_register(selector)
#define __bedrock_write_control_register(selector, value)                      \
  __builtin_bedrock_write_control_register(selector, value)
#define __bedrock_read_segment_register(selector)                              \
  __builtin_bedrock_read_segment_register(selector)
#define __bedrock_read_code_segment() __builtin_bedrock_read_code_segment()
#define __bedrock_write_segment_register(selector, image)                      \
  __builtin_bedrock_write_segment_register(selector, image)

#endif
