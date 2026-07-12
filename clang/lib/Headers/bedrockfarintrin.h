#ifndef __BEDROCKFARINTRIN_H
#define __BEDROCKFARINTRIN_H

#include <stdint.h>

typedef unsigned __int128 __bedrock_far_uintptr_t;
typedef unsigned __int128 __bedrock_far_func_uintptr_t;

#define __BEDROCK_FAR_SEGMENT_BASE_MASK UINT64_C(0xfffffffffffff000)
#define __BEDROCK_FAR_SEGMENT_MANTISSA_MASK UINT64_C(0x7e)
#define __BEDROCK_FAR_INVALID_VALUE (~(__bedrock_far_uintptr_t)0)

#define __BEDROCK_FAR_BOUNDS_IMAGE(segment_image)                             \
  ((uint64_t)(segment_image) |                                                \
   (((uint64_t)(segment_image) & __BEDROCK_FAR_SEGMENT_MANTISSA_MASK) != 0))
#define __BEDROCK_FAR_RAW(address, segment_image)                              \
  ((uint64_t)(address) == 0                                                   \
       ? (__bedrock_far_uintptr_t)0                                           \
       : ((__bedrock_far_uintptr_t)(uint64_t)(segment_image) << 64) |         \
             (__bedrock_far_uintptr_t)(uint64_t)(address))
#define __BEDROCK_FAR_ENCODE(pointer_type, address, segment_image)             \
  ((pointer_type)__BEDROCK_FAR_RAW(address, segment_image))
#define __BEDROCK_FAR_CONST_FROM_SEGMENT(pointer_type, offset, segment_image)  \
  ((pointer_type)(                                                            \
      (uint64_t)(offset) >                                                    \
              UINT64_MAX -                                                    \
                  ((uint64_t)(segment_image) &                                \
                   __BEDROCK_FAR_SEGMENT_BASE_MASK)                           \
          ? __BEDROCK_FAR_INVALID_VALUE                                      \
          : __BEDROCK_FAR_RAW(                                               \
                ((uint64_t)(segment_image) &                                  \
                 __BEDROCK_FAR_SEGMENT_BASE_MASK) +                          \
                    (uint64_t)(offset),                                       \
                __BEDROCK_FAR_BOUNDS_IMAGE(segment_image))))

#define __BEDROCK_FAR_PTR_INIT(pointer_type, address, segment_image)           \
  __builtin_choose_expr(                                                      \
      __builtin_constant_p(address) && __builtin_constant_p(segment_image),   \
      __BEDROCK_FAR_ENCODE(pointer_type, address,                             \
                           __BEDROCK_FAR_BOUNDS_IMAGE(segment_image)),         \
      __builtin_bedrock_far_ptr_init((pointer_type)0, address, segment_image))
#define __BEDROCK_FAR_FLAT_PTR_INIT(pointer_type, address)                     \
  __builtin_choose_expr(                                                      \
      __builtin_constant_p(address),                                          \
      __BEDROCK_FAR_ENCODE(pointer_type, address, 0),                         \
      __builtin_bedrock_far_flat_ptr_init((pointer_type)0, address))
#define __BEDROCK_FAR_NULL(pointer_type) ((pointer_type)0)
#define __BEDROCK_FAR_PTR_FROM_SEGMENT(pointer_type, offset, segment_image)    \
  __builtin_choose_expr(                                                      \
      __builtin_constant_p(offset) && __builtin_constant_p(segment_image),    \
      __BEDROCK_FAR_CONST_FROM_SEGMENT(pointer_type, offset, segment_image),  \
      __builtin_bedrock_far_ptr_from_segment((pointer_type)0, offset,         \
                                              segment_image))

#define __bedrock_far_address(pointer) __builtin_bedrock_far_address(pointer)
#define __bedrock_far_same_encoding(left, right)                               \
  __builtin_bedrock_far_same_encoding(left, right)

#endif
