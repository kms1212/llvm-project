// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -emit-llvm -o - %s | FileCheck %s

// CHECK: target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n64-S128"
// CHECK: target triple = "bedrock"

struct Pair {
  long a;
  long b;
};

struct Big {
  long a;
  long b;
  long c;
};

// CHECK-LABEL: define{{.*}} i32 @narrow(
// CHECK-SAME: i8 noundef signext %a,
// CHECK-SAME: i16 noundef zeroext %b,
// CHECK-SAME: i32 noundef signext %c)
int narrow(signed char a, unsigned short b, int c) {
  return a + b + c;
}

// CHECK-LABEL: define{{.*}} [2 x i64] @ret_pair(
struct Pair ret_pair(long a, long b) {
  struct Pair p = {a, b};
  return p;
}

void take_pair(struct Pair);

// CHECK-LABEL: define{{.*}} void @pass_pair(ptr noundef byval(%struct.Pair) align 16 %p)
void pass_pair(struct Pair p) {
  take_pair(p);
}
// CHECK-LABEL: declare void @take_pair(ptr noundef byval(%struct.Pair) align 16)

// CHECK-LABEL: define{{.*}} void @ret_big(ptr dead_on_unwind noalias writable sret(%struct.Big) align 8 %agg.result,
struct Big ret_big(long a, long b, long c) {
  struct Big v = {a, b, c};
  return v;
}

void take_big(struct Big);

// CHECK-LABEL: define{{.*}} void @pass_big(ptr noundef byval(%struct.Big) align 16 %b)
void pass_big(struct Big b) {
  take_big(b);
}
// CHECK-LABEL: declare void @take_big(ptr noundef byval(%struct.Big) align 16)

// CHECK-LABEL: define{{.*}} i32 @var_int(ptr noundef %list)
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 16
int var_int(__builtin_va_list list) {
  return __builtin_va_arg(list, int);
}
