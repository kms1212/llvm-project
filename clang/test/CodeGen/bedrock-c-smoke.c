// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -S -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple bedrock -O2 -S -o - %s | FileCheck %s --check-prefix=OPT

int add(int a, int b) {
// CHECK-LABEL: add:
// CHECK: add.l
// CHECK: ret
// OPT-LABEL: add:
// OPT: add.l
// OPT: ret
  return a + b;
}

long load(long *p) {
// CHECK-LABEL: load:
// CHECK: mov.q
// CHECK: ret
  return *p;
}
