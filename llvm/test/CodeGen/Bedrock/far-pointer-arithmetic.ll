; RUN: llc -O2 -mtriple=bedrock < %s | FileCheck %s
; RUN: llc -O0 -mtriple=bedrock < %s | FileCheck %s

target datalayout = "e-m:e-p:64:64-p1:128:128:128:64-i64:64-i128:128-n64-S128"
target triple = "bedrock"

; Far pointer arithmetic updates only the 64-bit pre-segment address in R0.
; The segment image arrives and returns unchanged in R1.

; CHECK-LABEL: far_gep_i8:
; CHECK: add.q r2, r0
; CHECK-NOT: r1
; CHECK: ret
define ptr addrspace(1) @far_gep_i8(ptr addrspace(1) %pointer, i64 %index) {
  %result = getelementptr i8, ptr addrspace(1) %pointer, i64 %index
  ret ptr addrspace(1) %result
}

; CHECK-LABEL: far_gep_i32:
; CHECK: lea.l [ds:r0 + r2], r0
; CHECK-NOT: r1
; CHECK: ret
define ptr addrspace(1) @far_gep_i32(ptr addrspace(1) %pointer, i64 %index) {
  %result = getelementptr i32, ptr addrspace(1) %pointer, i64 %index
  ret ptr addrspace(1) %result
}

; CHECK-LABEL: far_gep_constant:
; CHECK: lea.q [r0 - 8], r0
; CHECK-NOT: r1
; CHECK: ret
define ptr addrspace(1) @far_gep_constant(ptr addrspace(1) %pointer) {
  %result = getelementptr i8, ptr addrspace(1) %pointer, i64 -8
  ret ptr addrspace(1) %result
}

; CHECK-LABEL: far_indexed_load:
; CHECK-NOT: {{add|sub|inc|dec}}.q {{.*}}r1
; CHECK: wrseg r1, gs4
; CHECK: mov.l [gs4:r0], r0
; CHECK: ret
define i32 @far_indexed_load(ptr addrspace(1) %pointer, i64 %index) {
  %address = getelementptr i32, ptr addrspace(1) %pointer, i64 %index
  %value = load i32, ptr addrspace(1) %address, align 4
  ret i32 %value
}
