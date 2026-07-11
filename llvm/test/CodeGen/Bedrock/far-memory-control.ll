; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -verify-machineinstrs -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -dr %t.o | FileCheck %s --check-prefix=OBJ

target datalayout = "e-m:e-p:64:64-p1:128:128:128:64-i64:64-i128:128-n64-S128"

; CHECK-LABEL: load_far:
; CHECK: wrseg [[IMAGE:r[0-9]+]], gs4
; CHECK-NEXT: mov.q [gs4:[[ADDR:r[0-9]+]]], r0
define i64 @load_far(ptr addrspace(1) %p) {
  %value = load i64, ptr addrspace(1) %p, align 8
  ret i64 %value
}

; CHECK-LABEL: store_far:
; CHECK: wrseg [[IMAGE:r[0-9]+]], gs4
; CHECK-NEXT: mov.q {{r[0-9]+}}, [gs4:[[ADDR:r[0-9]+]]]
define void @store_far(ptr addrspace(1) %p, i64 %value) {
  store i64 %value, ptr addrspace(1) %p, align 8
  ret void
}

; CHECK-LABEL: load_far_float:
; CHECK: wrseg [[IMAGE:r[0-9]+]], gs4
; CHECK-NEXT: FMOV.S [gs4:[[ADDR:r[0-9]+]]], f0
define float @load_far_float(ptr addrspace(1) %p) {
  %value = load float, ptr addrspace(1) %p, align 4
  ret float %value
}

; CHECK-LABEL: far_identity:
; CHECK: lret
define bedrock_farcc i32 @far_identity(i32 %x) addrspace(1) {
  ret i32 %x
}

; CHECK-LABEL: call_far:
; CHECK: lcall {{r[0-9]+}}, far_identity
; OBJ-LABEL: <call_far>:
; OBJ: lcall {{r[0-9]+}}, 0
; OBJ-NEXT: R_BEDROCK_ABS32S far_identity
define i32 @call_far(i32 %x) {
  %result = call bedrock_farcc addrspace(1) i32 @far_identity(i32 %x)
  ret i32 %result
}

; CHECK-LABEL: call_far_indirect:
; CHECK: lcall [[SEG:r[0-9]+]], [[TARGET:r[0-9]+]]
define i32 @call_far_indirect(ptr addrspace(1) %fn, i32 %x) {
  %result = call bedrock_farcc addrspace(1) i32 %fn(i32 %x)
  ret i32 %result
}

; CHECK-LABEL: far_tail:
; CHECK: ljmp {{r[0-9]+}}, far_identity
; CHECK-NOT: lret
define bedrock_farcc i32 @far_tail(i32 %x) addrspace(1) {
  %result = tail call bedrock_farcc addrspace(1) i32 @far_identity(i32 %x)
  ret i32 %result
}
