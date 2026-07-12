#ifndef __BEDROCKFARINTRIN_H
#define __BEDROCKFARINTRIN_H

#include <stdint.h>

typedef unsigned __int128 __bedrock_far_uintptr_t;
typedef unsigned __int128 __bedrock_far_func_uintptr_t;

#define __BEDROCK_FAR_PTR_INIT(pointer_type, address, segment_image)           \
  ((pointer_type)(((__bedrock_far_uintptr_t)(uint64_t)(segment_image) << 64) | \
                  (__bedrock_far_uintptr_t)(uint64_t)(address)))
#define __BEDROCK_FAR_FLAT_PTR_INIT(pointer_type, address)                     \
  __BEDROCK_FAR_PTR_INIT(pointer_type, address, 0)
#define __BEDROCK_FAR_NULL(pointer_type) ((pointer_type)0)
#define __BEDROCK_FAR_PTR_FROM_SEGMENT(pointer_type, offset, segment_image)    \
  __BEDROCK_FAR_PTR_INIT(pointer_type, offset, segment_image)

#define __bedrock_far_address(pointer) __builtin_bedrock_far_address(pointer)
#define __bedrock_far_same_encoding(left, right)                               \
  __builtin_bedrock_far_same_encoding(left, right)

#endif
