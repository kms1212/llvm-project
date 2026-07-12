// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -internal-isystem %S/../../lib/Headers -fsyntax-only -verify %s
// RUN: not %clang_cc1 -triple bedrock -std=c11 -ffreestanding -target-feature -virtaccel -emit-llvm -o /dev/null %s -DTEST_NO_VIRT 2>&1 | FileCheck %s --check-prefix=NOVIRT

#include <bedrocksystemintrin.h>

#ifndef TEST_NO_VIRT
void bad_immediates(unsigned value, uint64_t image) {
  (void)__bedrock_read_control_register(value); // expected-error {{must be a constant integer}}
  (void)__bedrock_read_control_register(65536); // expected-error {{outside the valid range [0, 65535]}}
  __bedrock_write_control_register(value, image); // expected-error {{must be a constant integer}}
  (void)__bedrock_read_segment_register(8); // expected-error {{outside the valid range [0, 7]}}
  __bedrock_write_segment_register(__BEDROCK_SEG_CS, image); // expected-error {{outside the valid range [1, 7]}}
  __bedrock_invalidate_asid(value); // expected-error {{must be a constant integer}}
  __bedrock_invalidate_asid(65536); // expected-error {{outside the valid range [0, 65535]}}
  (void)__bedrock_page_table_query(value, 0); // expected-error {{must be a constant integer}}
  (void)__bedrock_page_table_query(8, 0); // expected-error {{outside the valid range [0, 7]}}
}
#else
// NOVIRT: error: '__builtin_bedrock_encode_instruction' needs target feature virtaccel
long no_virt(void *destination) {
  return __builtin_bedrock_encode_instruction(destination, 0, 0, 0, 0);
}
#endif

