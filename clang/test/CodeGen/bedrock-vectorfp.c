// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c17 -ffreestanding -O1 \
// RUN:   -target-feature -fpu -target-feature -vector \
// RUN:   -target-feature +vectorfp -internal-isystem %S/../../lib/Headers \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple bedrock -std=c17 -ffreestanding -O1 \
// RUN:   -target-feature +vector -target-feature +fpu \
// RUN:   -target-feature -vectorfp -internal-isystem %S/../../lib/Headers \
// RUN:   -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: not %clang_cc1 -triple bedrock -std=c17 -ffreestanding -O1 \
// RUN:   -target-feature +vector -target-feature -vectorfp \
// RUN:   -internal-isystem %S/../../lib/Headers -DTEST_NO_VECTORFP \
// RUN:   -S -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=NO-VECTORFP

#include <bedrock_vector.h>

#ifndef TEST_NO_VECTORFP

// IR-LABEL: define{{.*}} <vscale x 4 x float> @add_vectors(
// IR: fadd <vscale x 4 x float>
// IR: attributes #{{[0-9]+}} = {{.*}}"target-features"="+fpu,+vector,+vectorfp"
// ASM-LABEL: add_vectors:
// ASM: vfadd.s
__attribute__((target("vectorfp")))
bedrock_vfloat32_t add_vectors(bedrock_vfloat32_t lhs,
                               bedrock_vfloat32_t rhs) {
  return lhs + rhs;
}

#else

// NO-VECTORFP: error: bedrock vector type 'bedrock_vfloat32_t' (aka '__SVFloat32_t') requires target feature vectorfp
bedrock_vfloat32_t add_without_vectorfp(bedrock_vfloat32_t lhs,
                                        bedrock_vfloat32_t rhs) {
  return lhs + rhs;
}

#endif
