// RUN: %clang_cc1 -triple bedrock-unknown-unknown -S -O1 -o - %s \
// RUN:   | FileCheck %s

int add(int a, int b) {
  return a + b;
}

// CHECK-LABEL: add:
// CHECK: ADD.L D1, D0
// CHECK-NEXT: RET

long choose_min(long a, long b) {
  return a < b ? a : b;
}

// CHECK-LABEL: choose_min:
// CHECK: MINS.Q D1, D0
// CHECK-NEXT: RET

long load_long(long *p) {
  return *p;
}

// CHECK-LABEL: load_long:
// CHECK: MOV.Q [A0], D0
// CHECK: RET
// CHECK-NOT: .ident
// CHECK-NOT: .addrsig
