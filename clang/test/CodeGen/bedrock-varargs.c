// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o - %s | FileCheck %s

#include <stdarg.h>

struct Pair {
  long low;
  long high;
};

// CHECK-LABEL: define{{.*}} i64 @read_integer(i32 noundef signext %tag, ...)
// CHECK: call void @llvm.va_start.p0
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 16
// CHECK: load i32, ptr %{{.*}}, align 16
long read_integer(int tag, ...) {
  va_list list;
  va_start(list, tag);
  return va_arg(list, int);
}

// Aggregate varargs carry the address of the caller-owned copy in one slot.
// CHECK-LABEL: define{{.*}} i64 @read_pair(i32 noundef signext %tag, ...)
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 16
// CHECK: load ptr, ptr %{{.*}}, align 16
// CHECK: call void @llvm.memcpy
long read_pair(int tag, ...) {
  va_list list;
  va_start(list, tag);
  struct Pair value = va_arg(list, struct Pair);
  return value.low + value.high;
}

extern long old_style();

// Bedrock represents a no-prototype call with no fixed parameters so every
// actual argument receives the unnamed 16-byte-slot ABI treatment.
// CHECK-LABEL: define{{.*}} i64 @call_old_style()
// CHECK: call i64 (...) @old_style(i32 noundef signext 1, double noundef 2.000000e+00)
long call_old_style(void) { return old_style(1, 2.0); }

// CHECK: declare{{.*}} i64 @old_style(...)
