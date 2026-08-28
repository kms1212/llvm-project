/*===---- bedrock_vector.h - Bedrock scalable-vector types ---------------===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/

#ifndef __BEDROCK_VECTOR_H
#define __BEDROCK_VECTOR_H

#ifndef __bedrock__
#error "<bedrock_vector.h> is only available for Bedrock targets"
#endif

#ifndef __bedrock_vector__
#error "<bedrock_vector.h> requires the Bedrock vector extension"
#endif

typedef __SVInt8_t bedrock_vint8_t;
typedef __SVUint8_t bedrock_vuint8_t;
typedef __SVInt16_t bedrock_vint16_t;
typedef __SVUint16_t bedrock_vuint16_t;
typedef __SVInt32_t bedrock_vint32_t;
typedef __SVUint32_t bedrock_vuint32_t;
typedef __SVInt64_t bedrock_vint64_t;
typedef __SVUint64_t bedrock_vuint64_t;
typedef __SVFloat16_t bedrock_vfloat16_t;
typedef __SVFloat32_t bedrock_vfloat32_t;
typedef __SVFloat64_t bedrock_vfloat64_t;
typedef __SVBool_t bedrock_predicate_t;

/* Compound scalable values are passed indirectly by the Bedrock C ABI. */
typedef __clang_svint32x2_t bedrock_vint32x2_t;
typedef __clang_svboolx2_t bedrock_predicate_x2_t;

#endif /* __BEDROCK_VECTOR_H */
