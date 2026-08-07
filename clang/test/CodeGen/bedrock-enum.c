// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -emit-llvm -o - %s | FileCheck %s

enum NonNegative { Zero, One };
enum IncludesNegative { MinusOne = -1, PositiveOne = 1 };

_Static_assert((enum NonNegative)-1 < 0,
               "baseline Bedrock enums use signed int");
_Static_assert((enum IncludesNegative)-1 < 0,
               "negative Bedrock enums use signed int");

// CHECK-LABEL: define{{.*}} i32 @pass_nonnegative(
// CHECK-SAME: i32 noundef signext %value)
enum NonNegative pass_nonnegative(enum NonNegative value) {
  return value;
}

// CHECK-LABEL: define{{.*}} i32 @pass_negative(
// CHECK-SAME: i32 noundef signext %value)
enum IncludesNegative pass_negative(enum IncludesNegative value) {
  return value;
}
