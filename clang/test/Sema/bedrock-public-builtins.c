// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -internal-isystem %S/../../lib/Headers -fsyntax-only -verify %s
// RUN: not %clang_cc1 -triple bedrock -std=c11 -ffreestanding -target-feature -fpu -emit-llvm -o /dev/null %s -DTEST_NO_FPU 2>&1 | FileCheck %s --check-prefix=NOFPU

#include <bedrockintrin.h>

typedef int *__far far_int_ptr;
typedef float *__far far_float_ptr;

#ifndef TEST_NO_FPU
void immediate_errors(unsigned value) {
  __bedrock_rdpmc(value); // expected-error {{argument to '__builtin_bedrock_rdpmc' must be a constant integer}}
  __bedrock_rdpmc(65536); // expected-error {{argument value 65536 is outside the valid range [0, 65535]}}
  __bedrock_trace(value); // expected-error {{argument to '__builtin_bedrock_trace' must be a constant integer}}
  __bedrock_trace(65536); // expected-error {{argument value 65536 is outside the valid range [0, 65535]}}
}

void far_errors(int *near_pointer, far_int_ptr far_pointer,
                far_float_ptr other_far_pointer, float value) {
  (void)__builtin_bedrock_far_address(near_pointer); // expected-error {{bedrock builtin requires a far pointer operand}}
  (void)__builtin_bedrock_far_same_encoding(far_pointer, other_far_pointer); // expected-error {{bedrock builtin requires compatible far pointer operands}}
  (void)__builtin_bedrock_far_ptr_init(far_pointer, value, 0); // expected-error {{argument 2 to bedrock far pointer builtin must have integer type}}
}
#else
// NOFPU: error: '__builtin_bedrock_fclass_f32' needs target feature fpu
uint16_t no_fpu(float value) {
  return __builtin_bedrock_fclass_f32(value);
}
#endif
