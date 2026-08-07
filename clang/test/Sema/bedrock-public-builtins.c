// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -internal-isystem %S/../../lib/Headers -fsyntax-only -verify %s
// RUN: not %clang_cc1 -triple bedrock -std=c11 -ffreestanding -target-feature -fpu -emit-llvm -o /dev/null %s -DTEST_NO_FPU 2>&1 | FileCheck %s --check-prefix=NOFPU

#include <bedrockintrin.h>

_Static_assert(__BEDROCK_PMC_CYCLE == 1, "");
_Static_assert(__BEDROCK_PMC_INSTRET == 2, "");
_Static_assert(__BEDROCK_PMC_PTWALK == 3, "");

#ifndef TEST_NO_FPU
void immediate_errors(unsigned value) {
  __bedrock_rdpmc(value); // expected-error {{argument to '__builtin_bedrock_rdpmc' must be a constant integer}}
  __bedrock_rdpmc(65536); // expected-error {{argument value 65536 is outside the valid range [0, 65535]}}
  __bedrock_trace(value); // expected-error {{argument to '__builtin_bedrock_trace' must be a constant integer}}
  __bedrock_trace(65536); // expected-error {{argument value 65536 is outside the valid range [0, 65535]}}
}

#else
// NOFPU: error: '__builtin_bedrock_fclass_f32' needs target feature fpu
uint16_t no_fpu(float value) {
  return __builtin_bedrock_fclass_f32(value);
}
#endif
