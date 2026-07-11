// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -emit-llvm -o - %s | FileCheck %s

typedef int * __far far_int_ptr;
typedef int (* __far far_fn)(int);

// CHECK: target datalayout = "e-m:e-p:64:64-p1:128:128:128:64-i64:64-i128:128-n64-S128"

// CHECK-LABEL: define dso_local bedrock_farcc i32 @remote(i32 noundef signext %x) addrspace(1) #0 {
int __far remote(int x) { return x + 1; }

// CHECK-LABEL: define dso_local i32 @call_remote(i32 noundef signext %x)
// CHECK: call bedrock_farcc addrspace(1) i32 @remote(i32 noundef signext %{{.*}})
int call_remote(int x) { return remote(x); }

// CHECK-LABEL: define dso_local i32 @call_indirect(ptr addrspace(1) noundef %fn,
// CHECK: call bedrock_farcc addrspace(1) i32 %{{.*}}(i32 noundef signext %{{.*}})
int call_indirect(far_fn fn, int x) { return fn(x); }

// CHECK-LABEL: define dso_local ptr addrspace(1) @widen(ptr noundef %p)
// CHECK: addrspacecast ptr %{{.*}} to ptr addrspace(1)
far_int_ptr widen(int *p) { return (far_int_ptr)p; }

// CHECK-LABEL: define dso_local ptr @narrow(ptr addrspace(1) noundef %p)
// CHECK: addrspacecast ptr addrspace(1) %{{.*}} to ptr
int *narrow(far_int_ptr p) { return (int *)p; }

// CHECK-LABEL: define dso_local i128 @raw(ptr addrspace(1) noundef %p)
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i128
unsigned __int128 raw(far_int_ptr p) { return (unsigned __int128)p; }

// CHECK-LABEL: define dso_local ptr addrspace(1) @rebuild(i128 noundef %value)
// CHECK: inttoptr i128 %{{.*}} to ptr addrspace(1)
far_int_ptr rebuild(unsigned __int128 value) { return (far_int_ptr)value; }

// CHECK-LABEL: define dso_local ptr addrspace(1) @null_far()
// CHECK: ret ptr addrspace(1) null
far_int_ptr null_far(void) { return 0; }

// CHECK-LABEL: define dso_local ptr addrspace(1) @index_far(ptr addrspace(1) noundef %p, i64 noundef %index)
// CHECK: getelementptr inbounds i32, ptr addrspace(1) %{{.*}}, i64 %{{.*}}
far_int_ptr index_far(far_int_ptr p, long index) { return p + index; }

// CHECK-LABEL: define dso_local i32 @equal_far(ptr addrspace(1) noundef %a, ptr addrspace(1) noundef %b)
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i64
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i64
// CHECK: icmp eq i64
int equal_far(far_int_ptr a, far_int_ptr b) { return a == b; }

// CHECK-LABEL: define dso_local i32 @less_far(ptr addrspace(1) noundef %a, ptr addrspace(1) noundef %b)
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i64
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i64
// CHECK: icmp ult i64
int less_far(far_int_ptr a, far_int_ptr b) { return a < b; }

// CHECK-LABEL: define dso_local i64 @difference_far(ptr addrspace(1) noundef %a, ptr addrspace(1) noundef %b)
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i64
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i64
// CHECK: sub i64
long difference_far(far_int_ptr a, far_int_ptr b) { return a - b; }

// CHECK: attributes #0 = { noinline
