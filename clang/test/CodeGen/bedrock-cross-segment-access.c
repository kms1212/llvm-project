// RUN: %clang_cc1 -triple bedrock -std=c11 -emit-llvm -o - %s | FileCheck %s

__attribute__((cross_segment_access)) int explicit_access(int *);
int __far remote_access(int *p) { return *p; }

// CHECK-LABEL: define dso_local bedrock_farcc i32 @remote_access
// CHECK-SAME: addrspace(1) #[[FAR:[0-9]+]]

// CHECK-LABEL: define dso_local i32 @call_both
// CHECK: call i32 @explicit_access({{.*}}) #[[CALL:[0-9]+]]
// CHECK: call bedrock_farcc addrspace(1) i32 @remote_access({{.*}}) #[[CALL]]
int call_both(int *p) {
  return explicit_access(p) + remote_access(p);
}

// CHECK: declare i32 @explicit_access({{.*}}) #[[EXPLICIT:[0-9]+]]
// CHECK: attributes #[[FAR]] = { cross_segment_access noinline {{.*}}memory(readwrite)
// CHECK: attributes #[[EXPLICIT]] = { cross_segment_access {{.*}}memory(readwrite)
// CHECK: attributes #[[CALL]] = { cross_segment_access memory(readwrite) }
